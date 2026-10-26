# Shadow of Mordor (Feral Linux build): first-person camera and player body - the map for a 6DoF head camera

Static analysis only (2026-10-18): read-only Ghidra batches on `som-linux-q2` (`scripts/ghidra_query.sh`), `objdump`
of address ranges of `reference/bin/linux/ShadowOfMordor` and the TSV indexes. Nothing was launched, nothing in the
install was read. `SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`. Scratch (git-ignored):
`$SOM/reference/work-firstperson/` - batches `b1..b4.txt` and answers `q1..q4.txt`, objdump ranges `d_*.txt`
(`d_player.txt` 0xa34000-0xa62000, `d_anim.txt` 0x2000000-0x2300000, `d_playerobj.txt` 0x27a0000-0x27d0000,
`d_smodel.txt` 0x2370000-0x2392000), helpers `scan.py`/`scan2.py` (field-access scans) and `sitegen.py` (prints the
`SITE(...)` lines of section 6 from the binary on disk).

Builds on `docs/re-view-render.md` (section 2: CPlayerCamera, the view record, H1 = `FUN_00a5a820`),
`docs/re-gameplay.md` (player, `IEngineModel`) and `docs/re-stereo.md` (per-view body). Conventions as there:
addresses = runtime addresses (non-PIE), `vf_NN` = vtable slot, `vtbl+0xe0` = byte offset into a vtable,
units cm, +Y up, transforms `{pos[3], quat x,y,z,w}`. Confidence: **[high]** read in the code, unambiguous;
**[med]** read in the code, meaning inferred; **[low]** inference; **RT** = needs a runtime check.
Axis and quaternion conventions are another helper's task; input and combat internals are only referenced here.

Short names used below: `shell` = `*(CGameClientShell**)0x46a9560`, `P` = the client `Player*`
(`FUN_009e3570(shell)`), `cam` = `*(CPlayerCamera**)(P+0x2d0)`, `body` = `*(PlayerBody**)(P+0x2d8)`,
`mgr` = the player's `CAnimationMgr*` = `*(body+0x3b8)`, `blk` = the animation manager's camera block
`mgr+0x3968` (`FUN_00a367d0(P)` returns it), `objsys` = `*(void**)0x466afa0` (vtbl+0xb0 = set an engine object's
transform).

## 0. Summary

| Question | Answer | Conf. |
|---|---|---|
| Who computes the gameplay camera | the player's animation manager: camera modifier stack `FUN_0207be30` (orbit, collision, smoothing, springs ...) -> `IAnimationMgrCameraOwner::SetCameraTransform` (PlayerBody `0xa4d570`) -> `cam+0x44c` pos, `cam+0x458` quat, camera object `cam+0x338` | high |
| Post-process | `FUN_00a35340`: cinematic / scripted `Camera` override, WorldCamera volumes, transition blend, final SetCameraTransform (`0xa355c9`), shake/roll `FUN_00a5d250` (`0xa355f4`) | high |
| Frame order | Player update **`FUN_00a40fe0`** (variant B, the one that runs in normal play; run 3: 13285 calls, `FUN_00a416a0` 0): animation update `0xa41363` -> camera post `0xa413c5` -> (render) H1 `0x9ee7d4`, all in one frame on the game thread. `FUN_00a416a0` (variant A) is the same code compiled twice and runs only while the app-active byte `shell+0x80e48` is 0 (1.3) | high (order, run 3), med (meaning of the byte) |
| Effective camera rotation | `cam+0x458 (x) cam+0x484` everywhere ("PlayerOrientation" prints it) | high |
| Consumers | movement direction `FUN_00ad0c00` (camera yaw x stick), anim variables `FUN_00a3c8d0`, server copy for the AI `FUN_027aa490` -> `CPlayerObj+0xb94`, targeting `FUN_00a5b8b0`, combat host `PlayerCombat::vf_09`, audio/streaming `FUN_009ef390`, weather volumes, UI markers via `FUN_00a60920` | high |
| Make it first person | F1: post-hook `FUN_00a35340` (live call site `0xa413c5` in `FUN_00a40fe0`; A's twin optional) and write the head pose with the game's setter `0xa4bef0` + `FUN_00a5afb0`; H1 adds the eye offsets | high (plan), RT (side effects) |
| Head bone | object `*(mgr+0x560)`; node id = hash of the name; **no head bone name in the binary** (only `Neck`, `SocketHead`, `Pelvis` ...) -> find it at runtime; pose freshness vs H1 is RT | high / RT |
| Hide head/hood/hair | `GetPiece` + `SetPieceHidden` (IEngineModel vtbl+0x20/+0x38) -> one 64-bit mask on the skeletal render node (`+0x130`); shadow most likely lost too (RT); keep it with a larger eye near plane, or a per-pass mask swap. P7/P8: no piece or attached model is the head; built 2026-10-20 instead: `SOMVR_HIDE_HEAD=near` (12 cm eye near plane) and `=bone` (Head's subtree collapsed in the phase-2 pose; joints only, the pose is rigid), 3.1 status | high (mechanism), low (shadow) |
| Turning | no body-turn call; turn the VR base yaw (movement follows), zero the look input (owner slot 4); the body turns by animation | med |
| State flags | state tags `FUN_0239d980(body+0x3c0)` (+ `Mount`, `FinalBlow`, `CombatCam` ... from code), cinematic `*(shell+0xb0)+0x20`, scripted camera `cam+0x390`, mount `P+0x1d8`, life state `P+0x4c4`, paused `FUN_009e4770`, hierarchy menu `P+0x310`, photo mode `*(P+0x318)+0x30 != 3`. Built 2026-10-20 on them: `SOMVR_FALLBACK=auto`, the automatic third-person fallback (5, status) | high-med |
| Correction to earlier docs | `P+0x310` (used by `FUN_00a5a820`) is the Nemesis hierarchy menu (`CHierarchyMenuMgr`), not photo mode (`P+0x318`); `FUN_009f1d20`'s per-item function is empty | high |

## 1. The player camera system

### 1.1 Who computes the camera: the animation manager, not CPlayerCamera

The gameplay camera is computed by the **camera modifier stack of the player's animation manager** (the ~60
`CAnimationMgr::Camera*` / `Camera*` modifier classes: orbit, radius, collision, smoothing, pitch/yaw springs, lens,
look-at, framing ...). `CPlayerCamera` (`P+0x2d0`, PlayerCamera.cpp) only receives the result, post-processes it and
hands it to the renderer **[high]**.

`FUN_0207be30(mgr, dt)` = the animation manager's camera update (called from the manager update `FUN_020a4ac0` at
`0x20a4eb1`, only if `mgr+0x8d8` != 0) **[high]**:
1. `FUN_020d9ae0(blk, dt)`: camera block pre-update - copies last frame's transform `blk+0x7ec..` to `blk+0x7d0..`,
   refreshes the followed character's transform (`blk+0x8..`, from `*(blk)+0x560`), timers.
2. Runs every modifier in the list `mgr+0x890` (node at `entry+0x10`): `modifier->vf_08(dt)` (vtbl+0x40; a true
   result removes the modifier). The modifiers read and write the block: **`blk+0x7ec` pos, `blk+0x7f8` quat**
   (setters `FUN_020da290` transform, `FUN_020da390` position, `FUN_020da420` rotation "CameraModifierData::SetCameraRot";
   getter `FUN_020da240`), focus point `blk+0x84` (= `mgr+0x39ec`), FOV etc. The modifier's `this+8` is `mgr`.
3. `FUN_020da1c0(blk)`, then **`owner->vtbl[3](owner, &xf)`** with `owner = *(mgr+0x8d8)` (an
   `IAnimationMgrCameraOwner*`) and `xf = FUN_020da240(blk)` - call `0x207bf1c` (`call rbp`).

For the player the owner is `body+0x28` (PlayerBody's `IAnimationMgrCameraOwner` subobject, vtable `0x34f2868`).
Its slots, all forwarding to the camera **[high code, med names]**:

| Slot (byte) | Function | What it does |
|---|---|---|
| 2 (+0x10) | `PlayerBody::vf_0x28_02` `0xa4d470` | **GetCameraTransform**: copies the camera object transform (`cam+0x338` +0x8..+0x20) |
| 3 (+0x18) | `PlayerBody::vf_0x28_03` `0xa4d570` | **SetCameraTransform(xf)**: `cam+0x44c..0x454` = pos, `cam+0x478..0x480` = pos, `cam+0x458..0x464` = quat, then `objsys->vtbl+0xb0(objsys, cam+0x338, xf)` (the rendered camera object). Same code as `PlayerBody::vf_28` `0xa4bef0` (primary vtable slot 28, vtbl+0xe0) |
| 4 (+0x20) | `0xa4d330` -> `FUN_00a3a7f0(P, 0xc, 0xd, 0x16, 0x17)` | **GetCameraInput**: look-stick yaw/pitch from PlayerInput (with sensitivity and the profile's invert flag `+0x23a`); what `CameraControllerInput` turns |
| 5 (+0x28) | `0xa4d280` -> `FUN_00a5b980(cam)` | **SetFOV**: vertical FOV -> `cam+0x3fc/0x400` + projection (same role as `FUN_00a5c6c0`, re-view-render.md 2) |
| 6 (+0x30) | `0xa4d3a0` | GetFOV (`cam+0x3fc/0x400`, clamped to the minima `0x46b2810/14`) |
| 7 / 8 | `0xa4d270` -> `FUN_00a5b4d0` / `0xa4d250` -> `FUN_00a5b500` | start roll (`cam+0x4dc` = angle, state 1) / let the roll decay (state 2) |
| 9 (+0x48) | `0xa4d2e0` -> `FUN_00a5a3c0` | GetNearFar (`FUN_00d28f80` / `FUN_00d29120`) |
| 10 / 11 | `0xa4d140` / `0xa4d160` | get `cam+0x4d8` (float) / set `cam+0x4d0` (pointer) |
| 12 / 13 | `FUN_00a38df0` / `FUN_00a38e80` | look sensitivity (aim / normal) and a profile bool |
| 15 | `PlayerBody::vf_36` `0xa4ea90` | reads `cam+0x338`; not read further |

### 1.2 CPlayerCamera fields (additions to re-view-render.md section 2)

| Offset | Meaning | Conf. |
|---|---|---|
| `+0x8` | owner `Player*` | high |
| `+0x338` | the camera **engine object** (transform `+0x8` pos, `+0x14` quat); what is rendered | high |
| `+0x340..0x36f` | last scripted-camera result (`FUN_00a5d370`), `+0x368` its transition record ("Cut" = `FUN_0216ac50("Cut")`) | high |
| `+0x390` / `+0x391` | a level `Camera` object (scripted camera) is active / that camera's flag `FUN_0268b660` (read by `FUN_00a5b7a0(cam, 0/1)`, 15 callers) | high (code), med (meaning of +0x391) |
| `+0x44c..0x454`, `+0x458..0x464` | **gameplay camera position and rotation** (written by SetCameraTransform). `Player.GetPos` prints `+0x44c` | high |
| `+0x468..0x474` | a saved orientation (`FUN_00a3ad30`/`FUN_00a3ae20`: `+0x484 x +0x458` saved and restored, P+0x4c5..0x4cf flags) | med |
| `+0x478..0x480` | position copy (also written by SetCameraTransform; `FUN_00a5afb0` resets it from `+0x44c`) | high |
| `+0x484..0x490` | an **extra rotation** composed onto `+0x458` by every reader (`FUN_00a498c0(out, cam+0x484, cam+0x458)` = quat product); reset to identity by `FUN_00a5afb0`, folded into `+0x458` by `FUN_00a5df30`. No non-identity writer found | high (use), low (purpose) |
| `+0x4f0` | "render me" byte (`FUN_00a36000`; set by `FUN_00a5d780`) | high |
| `+0x4f1` | set when a camera cut/transition happened this frame (`FUN_00a5d370`; motion blur off for that frame in `FUN_00a5a5a0`) | med |

The effective gameplay camera rotation everywhere is **`q = cam+0x458 (x) cam+0x484`** (product `FUN_00a498c0`); the
console command "PlayerOrientation %f %f %f" (`FUN_00a3afd0`) prints exactly that, so the game's "player
orientation" is the camera's **[high]**.

### 1.3 Per-frame order

All on the game thread, inside `CGameClientShell::vf_02` (re-view-render.md 1), **after BeginFrame (`0x9eb0f2`) and
before the world render** **[high for the calls; which path every frame takes: run 3, below]**:

```
CGameClientShell::vf_02
  0x9eb617  FUN_00ab3e00(player list)            for each P with P+0x4c4 != 1 (FUN_00a36830): FUN_00a42fc0(P)
  0x9eb623..0x9eb664                             FUN_00ab41c0/FUN_00ab4270 agree (else no Player update); if
                                                 byte shell+0x80c98 is set, FUN_009e0ec0 first (clears it); the
                                                 FUN_00a10f00 test here leads to the same place in this build
                                                 (its else path asks FUN_00d949c0 -> 0x9e62c0 = return 0)
  0x9ebcae  FUN_00ab3f80(player list)            if byte shell+0x80e48 != 0 (app active = normal play):
              for each P with P+0x4c4 != 1 and FUN_00a10f80(im, P) (interface state 3 and the CPlayerUI
              state *(*(P+0x2e0)+0xb0) != 0): FUN_00a40fe0(P)          = THE Player update (variant B)
  0x9eb687  FUN_00ab3ec0(player list)            else, if the game timer is not paused:
              for each P with P+0x4c4 != 1: FUN_00a43970(P) -> FUN_00a42fc0, FUN_00a416a0(P)  (variant A)
FUN_00a40fe0(P)
  return early if the game timer is paused: FUN_009e4770(shell) = *(*(0x463e290)+8)+0x34
  ... input, 0xa41330 FUN_00a8e120(P+0x2e8) PlayerCombat update (only if photo mode is off: *(P+0x318)+0x30 == 3)
      -> FUN_00a8c700 -> FUN_00a3c8d0 (animation variables from the camera, section 1.5)
  0xa4134a  FUN_020d92b0(blk)                     camera block distance/blend bookkeeping
  0xa41363  FUN_00a51210(body)  [photo mode off]  -> FUN_020a4ac0(mgr) animation manager update
              0x20a4eb1  FUN_0207be30(mgr)        camera modifier stack -> 0x207bf1c owner->SetCameraTransform
  0xa41392  FUN_00a364c0(P), FUN_00a52930(body) [P+0x4c4 != 3 and P+0x4c8 == 0], FUN_00ad3ac0(P+0x2f0 PlayerVisionMode)
  0xa413c5  FUN_00a35340(P)                       camera post-process (below)
  0xa413d1  FUN_00abfda0(P+0x2e0 CPlayerUI) ...; 0xa41405 FUN_00a3b5f0(P) (the rest of the update, out of line);
            0xa41494 FUN_00a608d0(cam) -> FUN_00ca54f0 (weather volumes follow the camera)
(world render) FUN_009ef390 -> streaming + audio listeners from the camera, FUN_009ee170 -> FUN_00a5a820 (H1)
end of vf_02: previous-camera copy for motion blur (re-view-render.md 1)
```

**Which variant runs.** Run 3 (2026-10-19, `logs/boot-check-20261019-190441`, call hooks `P_ANIM_A/B` and
`P_CAMPOST_A/B` of runtime-probes.md, P1 results) counted `ANIM_B` = `CAMPOST_B` = 13285 calls (= `PMROOT`, once per
frame at 60 Hz, game thread `WinMain`) and `ANIM_A` = `CAMPOST_A` = 0: **in normal play the Player update is
`FUN_00a40fe0`; `FUN_00a416a0` never ran** **[high]**. Statically **[high]**: the two are one function compiled twice -
the code from the entry through `FUN_00abfda0` is the same call for call (only register, stack-slot and scheduling
choices differ; from `FUN_00a8e120` to `FUN_00abfda0` A's call sites are B's + 0x6f2: `0xa41a55`/`0xa41363`,
`0xa41ab7`/`0xa413c5`); B calls the rest out of line (`FUN_00a3b5f0`, B is its only caller), A has it inline (same
callees apart from two small helpers).
vf_02 picks the copy by byte `shell+0x80e48` **[med for the meaning]**: 1 from the shell constructor (`0x9e9062`),
cleared by `CGameSystemEventHandler` (`shell+0x80e38`, vtable `0x34e41f8`) slot 18 `0x9f3d30` after shell vf_24
`0x9e7960` (pause with reason `0x100000`, `FUN_009e7810`), set again by slot 19 `0x9f3d50` after shell vf_25
`0x9e7df0` (unpause `0x100000`, `FUN_009e7c30`); while it is 0 the look input reads 0 (`FUN_00a3a960`) and the server
shell pauses its timer (`0x27783d7`). So it is the application-active flag (focus; the path from
`Feral.GameShell.AppActivate`/`AppDeactivate` in `CFeralGame::vf_69`/`vf_70` to slots 18/19 was not traced), and
variant A runs only while the game window is deactivated and the game timer still runs - likely never, as
deactivation normally pauses the game **[low]**. Consequence: **every per-frame hook in the Player update goes on
B's sites** (6.2: `FP_CAMPOST_B` `0xa413c5`, `FP_ANIM_B` `0xa41363`); A's twins are optional (same hook body). B
also needs the CPlayerUI state != 0 (`FUN_00a10f80`), A does not check the interface at all.

`FUN_00a35340(P)` (`0xa35340`), the camera post-process **[high]**:
1. `FUN_00a5d780(cam)` unless the game timer is paused: if a **cinematic camera** is active
   (`*(shell+0xb0)+0x20`) its transform goes to `cam+0x44c/0x478` and the camera object, its lens to `FUN_00a5c6c0`;
   else `FUN_00a5d370`: if a level **`Camera` object** (scripted keyframed camera, list `DAT_04c41330`, see 1.4) is
   active its transform is written to the camera object, with "Cut" transitions; then `cam+0x4f0 = 1`.
2. If a cinematic camera is active: return (no further processing, no shake).
3. If photo mode is active (`*(P+0x318)+0x30 != 3`): `FUN_00e30e40` (PlayerPhotoMode update), return.
4. Normal gameplay: body transform `bxf = *(body+0x1b8)` (+0x8..), camera transform `cxf` = camera object;
   - if no scripted camera (`cam+0x391 == 0`): **WorldCamera influence** `FUN_00c53cd0(P+0x330, &bxf, &cxf, P)`
     (`WC_Controller`, designer `WorldCameraBrush`/`WorldCameraInfluencePoint` volumes; whole system gated by byte
     `0x46b54f0`), `P+0x400 = FUN_00c53310(...)`, **SetCameraTransform** (`body->vtbl+0xe0`, call `0xa354f8`),
     then the **camera transition blend** `FUN_00d26e60(&out, P+0x340, &bxf, &focus mgr+0x39ec, &cxf)`
     (`P+0x340` = the transition manager, "CameraTransitionCollision", "Transition [%s] %d%%");
     if the debug camera is not active (`FUN_00e75440(FUN_00e74730())`): **SetCameraTransform** again (`0xa355c9`) and
     **SetFOV** (`body->vtbl+0xf0`, `0xa355db`, FOV from `*(P+0x340)+0x60`);
   - else (scripted camera): `FUN_020da420(blk, ...)` (keeps the modifier block's rotation in step), transition blend.
   - `FUN_00c53510(P+0x330)` (WorldCamera debug print), then **`FUN_00a5d250(cam)` at `0xa355f4`** = shake (camera FX
     offset composed onto the camera object and FOV), controller rumble from camera FX, roll state machine
     (re-view-render.md 2, step 3).
5. Render time (re-view-render.md 2): `FUN_00a5a3f0` roll on the object, `FUN_00a5a820` builds the view record from
   the camera object, `FUN_00a5a560` restores.

So the camera object (`cam+0x338`) carries, in order: modifier stack -> WorldCamera -> transition blend -> shake; the
`cam+0x44c/0x458` copies carry the same minus the shake (shake writes the object only) **[high]**.

### 1.4 Where the pose comes from, relative to the player

| Layer | Where | Effect | Conf. |
|---|---|---|---|
| Orbit / radius / focus / collision / smoothing / pitch-yaw springs / auto-rotation / framing / look-at | camera modifier stack in `FUN_0207be30` (data: the animation state machine's camera states; bound in `FUN_00a4efb0` "CameraModifier.*") | everything relative to the followed character `blk+0x8` and the focus point `blk+0x84`; look input via owner slot 4 | high (mechanism), med (which modifier does what) |
| Combat camera choice | `FUN_00a84b70` ("CombatCam", "FinalBlow", "SingleEnemy", "Tight", "ApproachingWall", "CloseToWalls"), `FUN_00a812b0` ("OneOnOne", "UpdateOneOnOneCamera") in PlayerCombat; they test state tags (`FUN_0239d080`) | selects camera states/tags, not poses | med |
| Animation keys `OVERRIDECAMERA` / `RESETCAMERA` | PlayerBody key handler `FUN_00a514e0` | per-animation camera override | high (strings), low (effect) |
| Level `Camera` objects (scripted cameras; props `OneTime`, `AllowPlayerMovement`, `KeyframedCameraDisablePlayer`, `PreventAIDetection`, `CanSkip`) | list `DAT_04c41330`, `FUN_00a5b630`, `FUN_00a5d370` | replace the camera pose while active | high |
| WorldCamera volumes | `FUN_00c53cd0` (`P+0x330`) | designer bias of the gameplay camera | high (call), low (math) |
| Transition blend | `FUN_00d26e60` (`P+0x340`) | blends after cinematics / state changes, with collision | med |
| Cinematic camera | `FUN_00a5d780` -> `FUN_00c6bd80` (`shell+0xb0`) | replaces everything | high |
| Shake, FX FOV pulse, roll | `FUN_00a5d250` / render-time `FUN_00a5a3f0` | additive | high |
| Debug camera (DSDEBUGCAM cheat, index 9 of the cheat name table `0x3576740`; `CDebugCameraStateMachine` singleton `FUN_00e74730` = `0x46b8410`, active = `FUN_00e75440`) | `CAnimationMgr::DebugCamera` modifier writes the block; `FUN_00a35340` then skips the final SetCameraTransform | free camera | med |

### 1.5 Consumers of the camera pose besides rendering

Three ways the pose is read **[high]**: the camera object (`cam+0x338` +0x8 pos / +0x14 quat, e.g. `FUN_00a608d0(out,
cam)` and `FUN_00a5b8b0(cam, out)` - the latter returns the modifier block's pose instead while the debug camera
runs), the copies `cam+0x44c` (pos) and `cam+0x458 (x) cam+0x484` (rotation), and the server player's copy (below).

| Consumer | Where it reads | What | Conf. |
|---|---|---|---|
| **Camera-relative movement** | `FUN_00ad0c00(P, outQuat, defaultQuat)` reads `cam+0x458..0x464`, `cam+0x484..0x490` at `0xad0e6e..0xad0e97` | move stick (`FUN_00ad0af0`: PlayerInput analog 3/6, keys 1/2/4/5) turned into a world facing = **camera yaw only** (forward flattened to XZ) x stick angle; returns 0 without input. Callers: `FUN_00a3c8d0` (`0xa3d405`), `FUN_00abc8f0` (`0xabc979`), `FUN_00abccc0` (`0xabccea`, falling state) | high |
| **Locomotion animation variables** | `FUN_00a3c8d0(P, ...)` (from PlayerCombat `FUN_00a8c700` and server `0x27acbd0`/`0x27b7b60`) | sets `Game_CameraPitch`, `RawCameraMag`, `PlayerCameraDeltaYaw`, `CameraYawMinusPlayerYaw`, `CameraDeltaYaw`, `DeltaYaw`, `RawMoveMag`, `MountWarg_Yaw` ... in the animation state machine from `cam+0x484 (x) cam+0x458` and `FUN_00ad0c00`; the anim graph turns/strafes on these | high (code), med (effect) |
| **Server player's camera copy** (AI) | `FUN_027aa490(CPlayerObj*, P)`: `CPlayerObj+0xb94` pos = `cam+0x44c`, `+0xba0` quat = `cam+0x458 (x) cam+0x484`, `+0xbb0/+0xbbc` second copy, `+0xbcc/0xbd0` FOVs (callers `FUN_027adc60`, `FUN_027b6a80`); read through `CPlayerObj::vf_59` `0x27b77b0` (vtbl+0x1d8) | the fight circle's "Camera" score in `AICombatMgr_ScoreAttacker` `FUN_028cd7a0` (dot of camera forward and the direction to the orc) | high |
| **Freeflow / combat target selection** | `FUN_00a5b8b0` from `FUN_00a9a140` (`0xa9a1d0`, PlayerCombatTargeting update, "CombatTargetNode"/"LeftHand") and `FUN_00a9b620` (`0xa9b6de`); target spheres `FUN_0274ed10` ("CombatTargetSpheres") via `FUN_00a60920` | camera pose for scoring targets on screen | high (reads), med (use) |
| **Combat host camera rotation** | `PlayerCombat::vf_09` `0xa7d390` and `PlayerCombat::vf_0x10_08` `0xa7d820` (ISharedCombatInputHost): camera object quat `+0x14` | shared combat code (attack direction "To Input" and the like) | high (read), low (users) |
| Aiming (bow / wraith) | animation modifiers `Modifier.AimTracking`, `Modifier.AimAtTrackerRotate` (`FUN_00a8ae50`); camera rotation through the paths above | not traced (combat helper) | low |
| **Audio listener, world streaming** | `FUN_009ef390`: camera object position + `cam+0x458 (x) cam+0x484` (re-stereo.md 2) | listener `FUN_023a39e0`, streaming positions engine vtbl+0xa0 -> +0x28 | high |
| Weather / atmosphere volumes | `FUN_00a608d0(cam)` -> `FUN_00ca54f0(.., P+0x438)` in the Player update; rain lens state | which weather volume applies | high |
| Camera FX listeners | `FUN_00a5a5a0` -> `FUN_00d8b0f0(fxMgr, camXf, sceneParams)` for the three ClientFX managers | camera-attached effects | high |
| **UI markers (nav markers, struggle prompts)** | world-to-screen `FUN_00a60920` (photo-mode aware, projects with `FUN_00d28080`) from `CNavMarkerInstance` code (`FUN_00ec1e80`, `FUN_00ec4a50`), struggle UI (`FUN_00c83c70`/`FUN_00c84000` from `FUN_00ed07d0`.., `FUN_00ed1db0` "StruggleTarget"); camera position `FUN_00ad1610` (`cam+0x44c`); mouse ray `FUN_00a0fc90` (`FUN_00ad1700` camera + `FUN_00ad1680` FOV) | screen positions of markers/prompts | high (reads), med (which UI) |
| The "world HUD items" of re-view-render.md | `FUN_009f1d20` -> `FUN_00e7a490` is an **empty function** (1 byte) in this build | nothing | high |
| CGameUI, struggle aim, debug UIs | `FUN_00e41460`, `FUN_00e1e960` (CStruggleAim area), `FUN_00e704c0`, `FUN_00e75b50` read `cam+0x338`/`+0x44c`/FOV | | med |
| Culling, LOD, shadows | only the view record (re-stereo.md 3) | | high |

Consequence for the mod: anything written into **SetCameraTransform's targets (`cam+0x44c/0x458/0x478`, the camera
object) and `cam+0x484` = identity** reaches movement, animation, AI, targeting, audio, streaming and markers; the
view record alone (H1) reaches only rendering (section 6) **[high]**.

## 2. The player's head bone

### 2.1 Which object, which API

- The object to query is the one the animation manager animates: **`hChar = *(HOBJECT*)(mgr+0x560)`** (the
  camera block follows it, `FUN_020d9ae0`; the Rotator modifier passes it to `IEngineModel` vtbl+0x138) **[high]**.
  The client player object `*(body+0x1b8)` (`FUN_00a36880(P)`, transform `+0x8`/`+0x14`) and the server
  `*(*(P+0x420)+8)` (re-gameplay.md 1) are very likely the same engine object - **RT** (compare the pointers once).
- `IEngineModel* m = *(void**)0x46a9230` (re-gameplay.md 2). Stubs: GetNode vtbl+0x80 (`0x91a750`),
  GetNodeTransform vtbl+0xb8 (`0x91c360`), GetPiece vtbl+0x20 (`0x91aeb0`), SetPieceHidden vtbl+0x38 (`0x91b310`).
- **A node id is the name's hash, not an index** **[high]**: `CSimpleModel::vf_62` (`0x23530c0`) hashes the name
  case-insensitively (`h = h*0x397 + tolower-table[c]`, table `0x38d7480`), checks that the skeleton (`*(model+0x28)+0x10`,
  0x40-byte entries) has it (`FUN_02ad5290`) and returns the hash. GetNodeTransform maps the hash back with the same
  lookup. So the hash can be computed offline once the name is known.
- `GetNodeTransform(m, hChar, node, &xf, world=1)` reads the model's **last evaluated output pose**
  (`*(model+0x170)` -> `+0x58` -> `+0x40`, 0x20 bytes per node, model space) and multiplies it with the object
  transform (`model+0x50` +0x8); there is no wait or evaluation inside (`CSimpleModel::vf_68` `0x2354df0`,
  `AnimNode_GetNodeTransform` `0x238fe60`) **[high]**.

### 2.2 The name

- The binary contains **no "Head" bone name**: no `Head`, `Head_joint`, `Neck_joint`, `Jaw` string exists
  (byte search; the only `Head` bytes are the tail of `SocketHead`, and nothing points there) **[high]**.
- Names the code looks up on humanoids (all can be overridden per character by data): `Pelvis` (`0x387c36c`, many
  modifiers), `Torso_Base` (`0x38b9ec6`), **`Neck`** (`0x38b9ed1`) and `Spine4_joint` (`0x38b9ed6`) - the defaults
  of `ProceduralBody_RegisterNodes` `FUN_02240f40` (GetNode at `0x22410e9` with the record's name or "Neck");
  hands `L_Hand`/`R_Hand`/`L_Armu`/`R_Armu`; the socket **`SocketHead`** (`0x3585cf8`, `FUN_00dc2720`); `LookAtNode`
  (a data attribute name) **[high for the strings]**.
- So the head node must be found at runtime **(RT)**: walk the skeleton table `*(*(model+0x28)+0x10)` (count at
  `+0x8`, 0x40-byte entries; the name field inside an entry is not identified yet) for `model = Object_GetSimpleModel(hChar)`
  (`FUN_008f3c20`: `obj+0x38` vtbl+0x20), or simply try GetNode with candidates ("Head", "head", "Head_joint",
  "Neck1", "Neck"), and try GetSocket (`vtbl+0x58`) "SocketHead". Then fix the eye offset from the head node once
  (log the node and the gameplay camera in a first-person pose).
- IEngineModel slot 14 (vtbl+0x70, `CSimpleModel::vf_58` `0x2352090`), which re-gameplay.md calls GetNodeName,
  enumerates the names in the model data's **piece** table (`*(model+0x28)+0x38`, count `+0x40`, the same table
  GetPiece/GetNumPieces use), so it is most likely a piece-name enumerator - useful for section 3 **[med]**.

### 2.3 When the pose is final relative to H1

| Step (same frame) | Where |
|---|---|
| animation manager update incl. the camera modifier stack | `FUN_00a51210` -> `FUN_020a4ac0` (Player update `FUN_00a40fe0`, `0xa41363`; variant A `0xa41a55`) |
| camera post-process (WorldCamera, transition, shake) | `FUN_00a35340` (`0xa413c5`; variant A `0xa41ab7`) |
| world render: view record | `FUN_00a5a820` (H1, `0x9ee7d4`) |

- Statically, nothing between the animation manager update and H1 waits for animation jobs; `GetNodeTransform`
  reads whatever the output pose buffer holds. The animation job system (`EngineAnimationJobThread1..3`,
  `FUN_00942ec0`; queue at `animEngine+0xc018`; **drain-and-wait = `FUN_00942fe0`**, reached through
  `IAnimationEngine` slot 9 `0x8ab7c0`, slot 7 `0x8ab780` = wake the workers) evaluates poses, but the frame position
  of the wait was not found (the `IAnimationEngine` holders are engine-side, outside the dumped ranges) **[high for
  the functions, low for the timing]**.
- **RT (first runtime check of this doc)**: in one boot, log per frame at (a) the return of `FUN_00a51210`,
  (b) H1, (c) entry of the next `FUN_00a51210`: `GetNodeTransform(hChar, "Neck" or the head)` (world) and the object
  position `*(body+0x1b8)+0x8`, plus the frame counter `renderer+0x38`. If (a) == (b) and the bone moves rigidly
  with the object, the pose is this frame's; if (b) only changes at (c) the pose lags one frame and the camera should
  be built from the object transform plus last frame's local head offset.
- Practical camera anchor until that is known: the **object transform + a local head offset** (the head's offset in
  the character's space changes slowly except in attacks/finishers), refreshed from GetNodeTransform each frame.
  The camera block's followed point `blk+0x8` and focus `blk+0x84` (`mgr+0x39ec`) are camera-rig points, not the
  head **[med]**.

## 3. Hiding the head, hood and hair (optionally the body) but keeping the shadow

### 3.1 The piece mechanism, end to end

| Step | Where | Conf. |
|---|---|---|
| Characters are one `CSimpleModel` assembled from part models: `SMC_BaseModel`, `SMC_BodyModel`, `SMC_HeadSimpleModel`/`SMC_HeadModel`, `SMC_LegsModel`, `SMC_HelmetModel`, `SMC_HairModel`, `SMC_ArmorModel1..4`, `SMC_AccessoryModel1/2`, `SMC_HeadScarModel`, ... with mesh indices per part (`SMC_*MeshIndex`); built by `FUN_0236b820` ("Constructing SimpleModel for '%s'", logs "\tHead:\t%s", "\tHair:\t%s" under `[SIMPLEMODEL|CONFIG]`); data overrides `HeadReplace`, `HairOverride`, `HairOverrideIsHelmet`, `HeadModelSwap`/`RemoveHairModel` (`FUN_02627e90`) | strings + `FUN_02364b10`/`FUN_02364470` | high (strings), med (that each part becomes its own piece) |
| Look up a piece: `GetPiece(m, hChar, "name", &piece)` vtbl+0x20 -> `CSimpleModel::vf_48` (`FUN_0218c1e0` name lookup in `*(model+0x28)`); count = slot 3 (vtbl+0x18, `vf_47`: `*(*(model+0x28)+0x40)`); names by index = slot 14 (vtbl+0x70, `vf_58`, section 2.2) | `0x234fc30`, `0x234f730`, `0x2352090` | high (code), med (slot 14 = piece names) |
| Hide: `SetPieceHidden(m, hChar, piece, bool)` vtbl+0x38 -> `CSimpleModel::vf_50` `0x23511b0`: bit `piece` (< 64) of the mask `model+0x6c/0x70`, dirty byte `model+0x68`, then `FUN_02382080(renderable, model+0x68)`: renderable (`*(model+0xb0)`, `CSimpleModelRenderable`) `+0x40` = dirty, `+0x44/+0x48` = mask | | high |
| Apply: `CSimpleModelRenderable::vf_02` (render update) at `0x237ff18`: if dirty, `node->vtbl+0x1a8(node, lo, hi)` with `node = renderable+0x50` = `TRenderNodeMeshSkeletalHandle` slot 53 (`0x712460`) - queues a render-thread command whose executor is the member pointer `0x29` = **`CRenderNodeMeshSkeletal::vf_05` `0x71d510`: `node+0x130/+0x134` = hidden-piece mask** | | high |
| The game already does it: `NPC.HidePiece` (`NPC_HidePiece` `0x296ddb0` = GetPiece + SetPieceHidden), animation keys `HIDE_PIECE_KEY`/`SHOW_PIECE_KEY` on the player (PlayerBody key handler `FUN_00a514e0`) and props (`Prop::vf_14`), `AttachmentHidePieces` (`FUN_024e0e30`) | | high |

So hiding the head/hood/hair pieces of the player is two calls per piece, once (re-apply after a model rebuild: armour
or head swaps rebuild the SimpleModel) **[high mechanism; the piece names are RT: enumerate them once with slot 3 +
slot 14 and log them]**. The hood is most likely the `SMC_HairModel` or `SMC_HelmetModel` part ("HairOverrideIsHelmet")
**[low]**. Weapons and the wraith are separate attachment objects with their own models (re-gameplay.md 2).

**Runtime result (P7 + P8, 2026-10-20; runtime-probes.md P7/P8 results, P8 = `logs/boot-check-20261020-022145`).**
The mechanism above runs, but it does not reach the head:
- P7: `SetPieceHidden` on each of hChar's 17 pieces removed nothing visible.
- P8: all 17 at once (`piecehide all`; the mask `0x1ffff` reached the model, the renderable and the handle's node)
  removed nothing either.
- P8: the player model's attach list holds **7 children, all weapon models**: the sword's item object and two more
  models on `scabbard_back`, the dagger's likewise on `Dagger_Sheath`, the bow.
- Hiding child 1 or 4 (by piece or by the render node's `+0x40` byte) removes the sheathed sword's or dagger's hilt.
  The head, the hair and the hood stayed in every shot.
So Talion's visible head and hair are **neither hChar's pieces nor an attached model**: no IDs for head hiding yet.
Still open for 3.3:
- the near plane (option 1, untested);
- the render node(s) that actually draw the head (re-resolution.md 2.4: the hair's deep-buffer node, the part models);
- scaling the `Head` node in the phase-2 pose, which the `armpose` write point now makes possible (it passed in P8),
  though option 4 notes it loses the head's shadow.

**Status (2026-10-20): built offline** as `SOMVR_HIDE_HEAD=near|bone|bone+near` in `mod/somvr`, both only with
`SOMVR_BASE=head` (re-xr-pipeline.md 6.7 has the switches, the logs and the checks for the next headset look). Nothing
launched.
- `near` = option 1: a 12 cm near plane on the eye views while the frame's base is the Head node (`SOMVR_NEAR_CM`
  wins). The game's own plane is 27.5 cm. The shadow keeps the whole head.
- `bone` = the phase-2 pose edit (ARM_TRK_EVAL, without `SOMVR_ARMS` too): Head's whole subtree (from the skeleton's
  parent bytes: run 3's 57 nodes 29..85, face, eyes, jaw, hair) collapsed onto the Head joint at x0.001. Head itself is
  never written, so the eye base (`GetNodeTransform` at H4, after phase 2) stays the game's.
- **A true scale is not possible**: the pose has no scale lane and the skinning masks the 4th position lane
  (`0x237ce40`, rodata `0x3341230` = `{~0, ~0, ~0, 0}`; re-animation.md 1.4, 2.1). So `bone` moves joints but
  cannot shrink meshes: the face and hair parts are pulled onto the joint at full size, the skull stays. Combined with
  `near` they lie inside the plane. The shadow shows the same collapse (one render node for every pass).
- First look: `near`; then `bone+near`.

### 3.2 Does a hidden piece still cast a shadow?

There is **one** hidden mask per skeletal render node (`node+0x130`), set by one command; no second (shadow) mask was
found in the `IRenderNodeMeshSkeletal` handle (56 slots, all setters mapped to their executors in
`work-firstperson/d_rnode.txt`) **[high for "one mask setter", low for how the passes use it]**. Shadows are rendered
inside each RenderScene from the same node lists (re-stereo.md 3). **RT**: hide the head piece in a test boot and look
at the shadow; expected result: the shadow loses the head too **[low]**.

Render-node flags that select different render paths, candidates for a "shadow only" / "not in the main view"
switch (all set through `TRenderNodeMeshSkeletalHandle` slots; executors read from the disassembly) **[high for the
bits, unknown meaning -> RT]**:

| Handle slot (vtbl byte) | Executor | Node field | Seen in the render code |
|---|---|---|---|
| 12 (+0x60) | `0x70a600` | `+0x41` bit 0 (re-lists the node when it changes) | `FUN_007e91a0` (the per-view list builder) tests it |
| 28 (+0xe0) | `0x70a630` | `+0x42` bit 2, then re-registers (`FUN_0071ef50`) | `FUN_008229a0` draws a node only with the bit **clear**, `FUN_00824570` iterates a list and draws only nodes with the bit **set** - two separate draw paths |
| 29 (+0xe8) / 30 (+0xf0) | `0x707770` / `0x707790` | `+0x41` bit 2 / bit 3 | `0x7fc178` tests bit 2; `FUN_00800ff0`, `FUN_008023f0` test bits 1-2 |
| 33 (+0x108) / 34 (+0x110) | `0x707710` / `0x707730` | `+0x42` bits 0-1 / bits 3-5 (a 3-bit value) | `FUN_007d3380` tests bits 3-5 |
| 15 (+0x78) / 16 (+0x80) | `0x7076e0` / `0x7076f0` | bytes `+0x44..+0x46` / `+0x47` | |

### 3.3 Ways to keep the shadow (best first)

1. **Near plane for the eye views** (no render work): the head, hood and hair sit within ~10-15 cm of the eyes; a
   near plane of that size clips them out of both eye views while the shadow maps (rendered from the light, inside
   RenderScene) keep the full body. Poke `0x46b65e4` (near override, re-view-render.md 2) or set `view->nearZ` in the
   H1 post-hook. Cost: hands and weapons closer than the near plane clip too, so it is a trade-off the first
   playtest decides (start at 8-10 cm with the head pieces hidden, raise only if the hood still shows) **[med]**.
2. **Hidden mask only for the eye passes**: if 3.2 shows the shadow loses the head, swap `node+0x130/+0x134` on the
   render thread - mask set before the eye's G-buffer pass, cleared for the shadow passes (cascades `FUN_007e2940`
   from `FUN_007dc0b0`, other casters from `FUN_007db760`, all in `FUN_007d6e20`, re-stereo.md 3); needs the pass
   order traced (static follow-up) and the player's node pointer (`*(*(model+0xb0)+0x50)` is the handle; the
   render-thread node behind it is not mapped) **[low]**.
3. **A node flag from 3.2's table**, if one of them turns out to be "shadow only": one poke per model build **[low]**.
4. Not useful: scaling the head bone (shadow goes too; and the pose is rigid, so a phase-2 edit can only move the
   head's joints, not shrink its meshes: 2026-10-20, re-xr-pipeline.md 6.7), hiding the whole object (`CPlayerObj` "SetHidden"/
   `SetInvisible` paths `FUN_027b2460`, `CPlayerObj::vf_21` - physical and AI invisibility; the shadow goes too, and
   the AI flag must stay off) **[med]**.

The debug camera / DSDEBUGCAM does not hide the player as far as the code shows (no piece or visibility call in
`CAnimationMgr::DebugCamera::vf_08` or `CDebugCameraStateMachine::vf_03`) **[low]**. Photo mode keeps the body.

## 4. Player orientation and turning

- **Two different "orientations"** **[high]**:
  - the *gameplay orientation* is the camera's: `cam+0x458 (x) cam+0x484`. The console command "PlayerOrientation"
    (`FUN_00a3afd0`) prints it, and movement, the locomotion variables and the AI camera score are all derived from
    it (section 1.5);
  - the *body facing* is the character object's rotation `*(body+0x1b8)+0x14` (= `hChar`, section 2.1).
- **How the body faces** **[high for the inputs, med for the mechanism]**: there is no "turn the player" call
  (`Player.SetOrientation` only converts Euler angles, re-gameplay.md 1). The body turns through animation:
  - `FUN_00ad0c00` gives the anim graph the wanted world facing = camera yaw x move-stick angle;
  - `FUN_00a3c8d0` gives it `CameraYawMinusPlayerYaw`, `PlayerCameraDeltaYaw`, `DeltaYaw`, `RawMoveMag` ...;
  - turn/move animations with root motion rotate the character; the Rotator modifier
    (`CAnimationMgr::Rotator::vf_14` `0x21c5590`, a procedural entry on "Pelvis" via IEngineModel vtbl+0x138) and
    snap-align during attacks (re-gameplay.md 1) rotate it too.
  - With no stick input the body keeps its facing while the camera orbits.
- **Teleport with rotation**: `Player_Teleport(P, xf)` -> `CPlayerObj::vf_84` `0x27b6ec0` sets position and rotation,
  but it also re-aims the camera block (`FUN_020da420(blk, rot)`) and resets animation-manager state
  (`FUN_0209a2e0`, `FUN_020a46e0`, engine `FUN_008bf9a0`). Too heavy to call every frame; possibly usable for snap
  turns (**RT**: does it interrupt the current animation?) **[high calls, low side effects]**.

What this means for VR turning **[med, plan]**:
1. **Snap/smooth turn = rotate the VR base yaw**, not the body. The camera written into the game (section 6, F1) is
   `baseYaw x HMD`; movement (`FUN_00ad0c00` uses only the camera yaw) and the anim graph follow at once, and the
   body turns through its own animations the next time the player moves. Zero the game's look input (owner slot 4,
   F4) so the orbit rig does not fight.
2. **Movement relative to the controller or the head**: hook `FUN_00ad0c00` (3 call sites, F3) and replace its
   camera yaw with the locomotion yaw of your choice; everything else stays head-relative.
3. **Body follows the head when standing**: no game mechanism exists. Options, all **RT**: (a) write the yaw-only
   rotation into the character object (`objsys` vtbl+0xb0 on `hChar`, same position) when the head yaw leaves a dead
   zone - the risk is that root motion or physics overwrite it or that it pops; (b) feed a tiny fake move-stick input
   for a few frames so the locomotion graph turns in place (only if a turn-in-place exists for the current state);
   (c) Teleport with rotation (heavy).
4. During attacks, finishers, mounts and sync actions the body rotation belongs to the animation; never couple the
   camera yaw to the body yaw (comfort).

## 5. Game-state flags for the camera modes

Per frame, after `P = FUN_009e3570(shell)` (0 in menus/loads). The player's **state tags** are the most general
signal: `tags = FUN_0239d980(body+0x3c0)` (current set of the double-buffered `StateTagMgr`; `FUN_0239d960` = the set
being built), up to 20 `HRECORD`s at `tags+0x0..0x98`, count at `tags+0xa0`; `HasTag(tags, &rec)` = `FUN_0239d080`
(`AddTag` = `FUN_0239d0f0`), a tag record by name = `FUN_0216c030("name")` (thunk `0x239d9e0`), its name =
`DBRecord_GetName` `FUN_025aa530`. Animation states add their tags from data (`CAnimationMgr::AddStateTags`), so most
tag names live in the game database; the code itself adds `Mount`, `Spectating`, `Teleport`, `InCombat`, `CombatCam`,
`OneOnOne`, `FinalBlow`, `NamedTarget`, `SingleEnemy`, `Tight`, `CloseToWalls`, `ApproachingWall`, `InGrassCam`, `Stl`
**[high]**. **RT (cheap, high value)**: log the tag names every frame during climbing, ledges, mounting, an execution,
a drain/brand, a dialogue and a death; then the VR camera-mode switch is a table of tag names.

| State | Where to read it | Conf. |
|---|---|---|
| **Cinematic / cutscene camera** | `*(*(shell+0xb0)+0x20) != 0` (active `CClientCinematicCamera`; `FUN_00a5d780`, `FUN_00a35340`, `CGameModeClientHydra::vf_02` render it instead of the player view). Pre-rendered movies: `IGameplayVideoMgr` (re-view-render.md 6) | high |
| Scripted level camera (`Camera` objects: dialogue framings, reveals; `KeyframedCameraDisablePlayer`, `AllowPlayerMovement`) | `cam+0x390` (`FUN_00a5b7a0(cam, 0)`), its flag `cam+0x391` (`FUN_00a5b7a0(cam, 1)`; which property `FUN_0268b660` reads is not resolved) | high (flag), med (meaning) |
| Camera cut this frame | `cam+0x4f1` | med |
| **Climbing / parkour / ledge** | no code flag; animation states with data tags (**RT** tag dump). Data hints: animation predicate `Predicate.FoundWallClimbEdge`, attributes `ClimbingWall`, `ClimbTracking`, `LedgeLethal`/`LedgeNonLethal`, `AxeClimb` | low |
| **Mounted (caragor / graug)** | `P+0x1d8` = the mount `HOBJECT` (WBObjRef at `P+0x1c0`), set by `FUN_00a3df70` (which also makes the camera block follow the mount, `FUN_020d9750(blk, mount)`, and sets the anim variable `MountAlive`); tag **`Mount`** added each frame in `FUN_00a42fc0` (`0xa4391b`); server side `CAnimationMgr::SetMount` ("Update Dismount Player(%s)") | med |
| **Executions / finishers / drains / brands** | paired animations (`CCombatSyncAction`, re-gameplay.md 4): detect through the combat side (`Combat_StartAttack` `0x26eb380`, the current attack record `Combat+0x4d0`) - combat helper's area; camera side: the tags `FinalBlow` (kill cam, added by `FUN_00a84b70` when `FUN_026d6220(target)`) and `CombatCam` | low (for "execution"), high (tag code) |
| Combat camera in use | tags `CombatCam`, `OneOnOne`, `InCombat` (PlayerCombat `FUN_00a84b70`, `FUN_00a812b0`) | high |
| **Dialogue** | conversations have their own camera records (`Conversation/Camera` database category) and script events `OnConversationBegin/End` (`FUN_0241a1d0`); in practice they arrive as the cinematic camera or a scripted `Camera` (rows above) - **RT** | low |
| **Pause / menus / map** | game timer paused: `FUN_009e4770(shell)` = byte `*(*(void**)0x463e290 + 8) + 0x34` (the Player update and the camera update stop); interface in game: `FUN_00a10f00(*(shell+0x80ff8))` (state 3); world hidden by UI: `FUN_00a10d30(interfaceMgr)`; the player UI state machine `*(*(P+0x2e0)+0xb0)` (state `+0xc`, or `+0x8` when `+0xc` is -1; strings "PauseMenu", "menu_pausemenu_enter", "SpectatorMode" in `CPlayerUIStateMachine::vf_03`; state numbers **RT**) | high (reads), med (meanings) |
| **Nemesis hierarchy screen** (3D army menu with its own camera) | `FUN_009fa370(*(P+0x310))` true (state not 0/7) - `P+0x310` is a `CHierarchyMenuMgr` (`MacroStateMachine`, vtable `0x34e6aa0`); `FUN_00a5a820` then takes the view from it. (re-view-render.md 2 and re-gameplay.md call this photo mode; it is the hierarchy menu) | high |
| **Photo mode** | `*(int*)(*(P+0x318)+0x30) != 3` (`PlayerPhotoMode`; 3 = off): the body animation update, the PlayerCombat update and the camera post-process are skipped, `FUN_00e30e40` runs the photo camera | high (code), med (3 = off) |
| Debug camera | `FUN_00e75440(FUN_00e74730())` | high |
| **Death / respawn** | client player state byte `P+0x4c4` from the server (`FUN_00a40ca0`): 1 = not in the world (reset by `FUN_00a40cd0`), 2 = alive in the world (entering 2 runs `FUN_00a40b90`), 3 = dying/dead (the Player update skips the body post-update; PlayerMovement/PlayerCombat ignore input) ; death handler `FUN_00a37f80` (fires "PlayerDeath"); respawn = back to 2 | med |
| Spectator modes | `P+0x4c8` (0 = normal; set by `FUN_00a3aac0`, which notifies PlayerCombat, CPlayerUI and the interface); tag `Spectating` | med |
| Wraith vision | `PlayerVisionMode` `P+0x2f0` (re-gameplay.md 1) | high (component) |

**Status (2026-10-20): the third-person fallback is built offline** as `SOMVR_FALLBACK=auto` in `mod/somvr` (default
off; only with `SOMVR_BASE=head`; re-xr-pipeline.md 6.8 has the switches, the logs and the runtime checks). Nothing
launched. Every stereo frame it reads the rows above that P7/P8 could check (no game call; 17 read-only sites prove the
offsets): the cinematic camera, the scripted camera `cam+0x390`, the presentation `*(P+0x308)+0x90`, the player's
sync action (`*(*(server Combat+0x188)+0x10)`), the struggles, the mount, the attached move `pm+0x1a6`, FSM 9, the life
state, photo mode, the spectator int `P+0x4c8`, the pause, and the state tags by name. Corrections found on the way:
- **Death does not show in `P+0x4c4`.** P8's death (runtime-probes.md "Results, P8") kept life 2 through
  `PC_RecoilAny_PreDeath`, `PC_PreDeathLoop` and `PC_DeathLoop`; the tags were `PreDeath_Kneeling`, then `PreDeath`.
  The fallback takes death from life 3 or a `PreDeath*` tag.
- `P+0x4c8` is an int (the setter writes 4 bytes), not a byte.
- The tag set: `FUN_0239d980` = `mgr + *(u8*)(mgr+0x158) * 0xa8 + 8`, up to 20 8-byte records, count at `+0xa0`.
When one of them holds (a sync only once it lasted 0.6 s or carried Talion 75 cm; debounce 0.1/0.4 s, at least 1.5 s
in each presentation), the view fades to black (the layer's black quad, from the frame's record), the base becomes the
game camera while black (the HMD still applied around it; no head hiding, the game's near plane, no follow, no arm
IK, no blade hits) and fades back; the same way back to the Head node. The climb and dialogue rows are still RT: the
climb tags (T2) go into `SOMVR_FALLBACK_TAGS` once the drill names them.

## 6. Hook plan

### 6.1 Design

Two levels, both needed for real first person:
1. **Gameplay camera = head** (F1): once per frame, after the game's own camera post-process, write the VR camera
   (`pos = head position`, `rot = baseYaw x HMD rotation`) through the game's own setter `PlayerBody::vf_28`
   (`0xa4bef0`, writes `cam+0x44c/0x478/0x458` and the camera object) and reset the extra rotation with
   `FUN_00a5afb0(cam)` (`cam+0x484` = identity). Then movement, the anim graph, the AI camera score (copied to the
   server at its next update), targeting, audio, streaming, weather and UI markers all follow the head (section 1.5).
   The animation manager's own rig (`blk`) keeps orbiting internally; nothing reads it back except the debug camera
   and the transition blend, which F1 overwrites **[high data flow, RT for side effects]**.
2. **Rendered views = eyes** (H1, spike1's `0xa5a820` post-hook): per eye, camera object pose + IPD/2 along the head's
   right axis, per-eye FOV (re-view-render.md 2, re-stereo.md 6). With F1 in place H1 only adds the eye offsets.

Comfort pokes stay as in re-view-render.md 7 (#2): `0x46b72d2` = 1 (camera FX/shake off), `cam+0x4dc` = 0 every
frame (roll; F1 runs before `FUN_00a5a3f0`), motion blur and DoF off.

### 6.2 Sites

| # | Name | Site | Signature / change | When it runs | Conf. |
|---|---|---|---|---|---|
| F1 | `FP_CAMPOST_B` (+ `_A`) | call of `FUN_00a35340(Player*)` at **`0xa413c5` (in `FUN_00a40fe0`, the live Player update, 1.3)**; the twin `0xa41ab7` (in `FUN_00a416a0`) runs only while the app is deactivated - same hook or none | post-hook: if state allows (section 5: not cinematic, not hierarchy menu, not photo mode, not debug camera, alive), call `PlayerBody::vf_28(body, &vrXf)` and `FUN_00a5afb0(cam)`; set `cam+0x4dc` = 0 | once per frame per player, game thread, after the animation update and before the render | high (site; run 3: B 13285 calls, A 0) |
| F1' | `FP_SHAKE` | call `FUN_00a5d250(cam)` at `0xa355f4` | alternative to F1 for the normal branch only: replace with "write the VR camera" (drops shake and roll at the source; rumble `FUN_00a5ad40` is lost unless called) | normal gameplay branch only | high |
| F2 | `FP_CINE` | call `FUN_00a5d780(cam)` at `0xa3560f` | post-hook: during cinematics (`*(shell+0xb0)+0x20`) or scripted cameras (`cam+0x390`) compose the HMD rotation onto the game camera, or leave it and switch to a theatre layer | every frame | high |
| F3 | `FP_MOVEDIR` / `_F1` / `_F2` | calls of `FUN_00ad0c00(Player*, quat* out, quat* dflt)` at `0xa3d405`, `0xabc979`, `0xabccea` | optional: movement relative to the controller instead of the head (replace the camera yaw inside, or recompute `out` from the stick and the chosen yaw; return 0 when no input) | per frame | high |
| F4 | `FP_OWNERLOOK` | `IAnimationMgrCameraOwner` slot 4 of PlayerBody, vtable slot `0x34f2888` -> `0xa4d330` | return `{0, 0}` (no look input to the orbit rig) and use the stick for snap/smooth turning of `baseYaw` | per frame, inside the modifier stack | high |
| F5 | `FP_OWNERSET` | slot 3 of the same vtable, `0x34f2880` -> `0xa4d570` (SetCameraTransform from the modifier stack) | observe only (log the third-person rig pose; useful for a "third-person moments" mode) | per frame | high |
| F6 | `FP_ANIM_B` (+ `_A`) | call of `FUN_00a51210(PlayerBody*)` at **`0xa41363`** (B, live); `0xa41a55` (A) as for F1 | post-hook: read the head node (section 2.3 RT decides whether this or H1 is the right moment) | per frame | high (site; run 3 as F1), RT (pose freshness) |
| F7 | `FP_ANIMCAM` | call of `FUN_0207be30(mgr)` at `0x20a4eb1` (inside the manager update; every animation manager with a camera owner) | not needed; listed for tracing the modifier stack (filter `mgr == *(body+0x3b8)`) | per frame | high |
| H1 | (spike1) | call of `FUN_00a5a820` at `0x9ee7d4` | per-eye view record | per view | high |

Functions the mod calls (check their first bytes too): `PlayerBody::vf_28` `0xa4bef0` `48 8b 4f 08 f3 0f 10 06 48 89 f2 f3`,
`FUN_00a5afb0` `48 8b 87 4c 04 00 00 f3 0f 10 87 54`, `GameClientShell_GetLocalPlayer` `0x9e3570`
`53 48 8d 9f a0 00 00 00 48 8d b7 80`, `FUN_009e4770` (paused) `48 8b 05 19 9b c5 03 48 8b 50 08 31`, the IEngineModel
stubs GetNode `0x91a750` `41 54 55 48 89 cd 53 48 85 f6 74 34`, GetNodeTransform `0x91c360` `48 85 f6 0f 84 97 00 00 00 41 55 48`,
GetPiece `0x91aeb0` `c7 01 ff ff ff ff 48 85 f6 74 4d 55`, SetPieceHidden `0x91b310` `48 85 f6 74 4b 55 48 89 f7 89 d5 53`,
slot 3 `0x91ae60` `48 85 f6 74 3b 53 48 89 f7 48 89 d3`, slot 14 `0x91b510` `48 85 f6 74 4b 41 54 48 89 f7 49 89`, state tags
`FUN_0239d980` `0f b6 87 58 01 00 00 48 8d 14 80 48`, `FUN_0239d080` `8b 87 a0 00 00 00 85 c0 7e 61 4c 8b`, tag by name
`FUN_0216c030` `48 89 fe 48 8b 3d 96 a6 9e 02 e9 51`, `DBRecord_GetName` `0x25aa530` `48 85 ff 74 1b f6 47 01 02 74 15 48`.

Check data in the `SITE(name, kind, addr, ctxaddr, "ctx bytes", target, "target prologue")` format of
`mod/spike1/check_sites.py` (generated from the binary with `work-firstperson/sitegen.py`; all verified against
build-id `87ec5d6f...`):

```c
SITE("FP_CAMPOST_A", K_CALL, 0xa41ab7, 0xa41aa3, "e8 88 0e 01 00 48 8b bb f0 02 00 00 e8 0c 20 09 00 48 89 df e8 84 38 ff ff", 0xa35340, "41 54 66 0f ef d2 55 53 48 89 fb 48 83 c4 80"),
SITE("FP_CAMPOST_B", K_CALL, 0xa413c5, 0xa413b1, "e8 7a 15 01 00 48 8b bb f0 02 00 00 e8 fe 26 09 00 48 89 df e8 76 3f ff ff", 0xa35340, "41 54 66 0f ef d2 55 53 48 89 fb 48 83 c4 80"),
SITE("FP_SHAKE", K_CALL, 0xa355f4, 0xa355e1, "48 8b bb 30 03 00 00 e8 23 df 21 00 48 8b bb d0 02 00 00 e8 57 7c 02 00", 0xa5d250, "48 83 bf 38 03 00 00 00 74 56 55 66 0f ef ed"),
SITE("FP_CINE", K_CALL, 0xa3560f, 0xa35608, "48 8b bb d0 02 00 00 e8 6c 81 02 00", 0xa5d780, "53 48 89 fb 48 83 ec 30 48 8b 05 d1 bd c4 03"),
SITE("FP_MOVEDIR", K_CALL, 0xa3d405, 0xa3d3f5, "48 8d 94 24 10 01 00 00 0f 29 84 24 10 01 00 00 e8 f6 37 09 00", 0xad0c00, "41 55 41 54 49 89 d4 55 48 89 fd 53 48 89 f3"),
SITE("FP_MOVEDIR_F1", K_CALL, 0xabc979, 0xabc96d, "8d 74 24 30 48 89 c7 0f 29 44 24 40 e8 82 42 01 00", 0xad0c00, "41 55 41 54 49 89 d4 55 48 89 fd 53 48 89 f3"),
SITE("FP_MOVEDIR_F2", K_CALL, 0xabccea, 0xabccdd, "20 0f 28 05 bb 88 8a 02 0f 29 44 24 50 e8 11 3f 01 00", 0xad0c00, "41 55 41 54 49 89 d4 55 48 89 fd 53 48 89 f3"),
SITE("FP_OWNERLOOK", K_VSLOT, 0x34f2888, 0x34f2888, "30 d3 a4 00 00 00 00 00", 0xa4d330, "48 8b 7f e0 41 b8 17 00 00 00 b9 16 00 00 00"),
SITE("FP_OWNERSET", K_VSLOT, 0x34f2880, 0x34f2868, "70 55 a5 00 00 00 00 00 00 56 a5 00 00 00 00 00 70 d4 a4 00 00 00 00 00 70 d5 a4 00 00 00 00 00 30 d3 a4 00 00 00 00 00 80 d2 a4 00 00 00 00 00", 0xa4d570, "48 8b 4f e0 f3 0f 10 06 48 89 f2 f3 0f 10 4e"),
SITE("FP_BODYSETCAM", K_VSLOT, 0x34f2728, 0x34f2728, "f0 be a4 00 00 00 00 00", 0xa4bef0, "48 8b 4f 08 f3 0f 10 06 48 89 f2 f3 0f 10 4e"),
SITE("FP_ANIM_A", K_CALL, 0xa41a55, 0xa41a48, "83 78 30 03 75 0c 48 8b bb d8 02 00 00 e8 b6 f7 00 00", 0xa51210, "53 48 89 fb c6 87 e0 05 00 00 00 48 8b bf b8"),
SITE("FP_ANIM_B", K_CALL, 0xa41363, 0xa41356, "83 78 30 03 75 0c 48 8b bb d8 02 00 00 e8 a8 fe 00 00", 0xa51210, "53 48 89 fb c6 87 e0 05 00 00 00 48 8b bf b8"),
SITE("FP_ANIMCAM", K_CALL, 0x20a4eb1, 0x20a4ea4, "74 10 f3 0f 10 83 10 42 00 00 48 89 df e8 7a 6f fd ff", 0x207be30, "41 57 4c 8d bf 68 39 00 00 41 56 41 55 41 54"),
SITE("H1", K_CALL, 0x9ee7d4, 0x9ee7c1, "48 8d 94 24 b0 00 00 00 48 8d b4 24 80 00 00 00 48 89 ef e8 47 c0 06 00", 0xa5a820, "41 55 49 89 d5 41 54 55 48 89 f5 53 48 89 fb"),
```

`FP_BODYSETCAM` is the primary-vtable slot of the same setter (`body->vtbl+0xe0`, used by `FUN_00a35340`); it is a check
for the function F1 calls, not a hook. Data pokes: near plane `0x46b65e4` (float, cm; section 3.3), camera FX off
`0x46b72d2`, WorldCamera off byte `0x46b54f0` (= 0 disables `FUN_00c53cd0`; only useful without F1).

### 6.3 Order inside one frame with F1

```
Player update: input -> PlayerCombat (reads last frame's camera = last frame's head) -> animation update
  (modifier stack, F4 zero look input) -> [F6 head bone] -> FUN_00a35340 -> [F1: camera := head x baseYaw x HMD]
render: H1 per eye (offset + FOV) -> FUN_009ef390 audio/streaming already use the F1 camera
server update (next frame): FUN_027aa490 copies the F1 camera to CPlayerObj for the AI
```
The gameplay side therefore sees the head pose with one frame of latency (PlayerCombat and the anim variables run
before F1), the rendered view none **[high order, RT for whether one frame matters]**.

## 7. Runtime checks and open questions

Runtime checks, in the order they unblock work:
1. **Pose freshness** (section 2.3): head node at the end of `FUN_00a51210` vs H1 vs the next frame.
2. **Head node name and eye offset**: GetNode with candidates / walk the skeleton table; log the head node and the
   gameplay camera.
3. **Piece names** of Talion (slot 3 count, slot 14 names) and whether hiding head/hood/hair also removes them from
   the shadow (section 3.2); if yes, try the near plane first, then the node flags of section 3.2.
4. **State tag dump** while climbing, mounting, executing, in dialogue, dying (section 5).
5. **F1 side effects**: does writing the camera every frame upset the transition blend, WorldCamera, the AI or the
   combat camera logic (`FUN_00a84b70` tags)? (Which variant runs is answered by run 3: B, `FUN_00a40fe0`, 1.3.)
6. **Body follow** experiments of section 4 (object yaw write vs Teleport).
7. Same object: `*(mgr+0x560)` == `*(body+0x1b8)` == `*(*(P+0x420)+8)`.

Open static questions (not needed for a first build):
- the frame position of the animation job wait (`FUN_00942fe0` via `IAnimationEngine` slot 9);
- what reads `CRenderNodeMeshSkeletal+0x130` on the render thread (main and shadow passes);
- the meaning of `cam+0x391` (`FUN_0268b660` on the active `Camera` object) and of the `CPlayerUI` state numbers;
- which property of a scripted `Camera` (`KeyframedCameraDisablePlayer`, `AllowPlayerMovement`) disables input.
