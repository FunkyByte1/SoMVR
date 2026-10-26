# Shadow of Mordor (Feral Linux build): traversal (climb, ledges, vaults, falls), climb surfaces, the proxy's gravity, mounts

Static analysis only (2026-10-19): Ghidra copy `som-linux-q4` (read-only, `scripts/ghidra_query.sh`; every batch went
through the heavy-job gate, none ran while the user was in VR), `objdump` on `reference/bin/linux/ShadowOfMordor`, the
TSV indexes in `reference/index/`, the full disassembly `reference/work-ui/full.dis.gz` and the run 3 dumps
(`logs/boot-check-20261019-190441/probe-out/bindings-2.tsv`). Nothing was launched, the install was not touched.
Scratch (git-ignored): `reference/work-traversal/` (batches `bN.txt`, outputs `oN.txt`, `acp.asm`/`acp.c` = the movement
solver, `surf.asm` = the surface schema, `sites*.spec` -> SITE lines through `reference/work-firstperson/sitegen.py`).

Conventions as in `re-gameplay.md` / `re-m1m2.md`: addresses = runtime VAs, `FUN_x` = Ghidra's name, `Class::vf_NN` =
vtable slot NN, `+0x2d0` = byte offset; **[high]** read in the code, **[med]** meaning inferred, **[low]** guess, **RT** =
needs a boot. `P` = client `Player*` (`FUN_009e3570(*0x46a9560)`), `pm = *(P+0x2c8)` (PlayerMovement), `body =
*(P+0x2d8)` (PlayerBody), `mgr = *(body+0x3b8)` (the player's CAnimationMgr), `pc = *(pm+0x50)` (Talion's character proxy).
Run 3 facts used: `P_PMROOT` (= `PlayerMovement::vf_11`) runs once per frame; the `pm+0x428` extra move collides and
slides along walls (runtime-probes.md P5 results); `move` drives the player; `state` logs `mount=` and the TAGS names.

## 0. Summary

| Item | Answer | Conf. |
|---|---|---|
| Climb state machine | Two layers. **Code**: a small player FSM in `P`: states at `P+0x7c0`, **current id byte `P+0x7f0`** (previous `+0x7f1`): 1 None, 3 Spectating, 6 Supported, 7 Combat, 8 **Falling**, 9 **TransitionAnim**, 10 **Landing**, 11 MPHydra. **Data**: climb, ledge hang, shimmy, vault, mantle, leap and drop are animation-graph states of `mgr` (context actions, edge/wall/railing finders, `Amtrak` rail movement); there is no code enum for them. The graph drives the FSM through `Modifier.PlayerStateEvent` (events 0x1f6-0x201) | high (FSM), high (no enum), med (which graph piece does what) |
| "Climbing now" read | per frame in G1: `fsm = *(u8*)(P+0x7f0)`, the state TAGS (re-firstperson.md 5; names RT), `supported = pc->vf_14()`, `gravity = *(u8*)(pc+0x60)`, mounted `= *(P+0x1d8) != 0`. Falls/landings are code-level (8/10); climb/ledge/vault need the TAG names from one traversal drill (probe word `trav`, section 1.6) | high (reads), RT (tag names) |
| Climb surfaces | per surface type: the `Surfaces/Surfaces` record's bool **`CanWallClimb`** (schema `FUN_0261cae0`, attribute 13), read by the wall-plant finder `FUN_021a3a60` (feeler rays, `ClimbingWall` flag in the state's data), the wall-climb-edge test `FUN_021922a0` and `FUN_021902d0(&{surfaceId,1})`. Ray hit -> surface record = `FUN_02ae5d90(RayResult*)` (triangle id `r+0x28` when `r+0x2a`, else the object's `+0x5a`). Ledges/railings are authored lines/brushes (`CContextActionDB`), found by the context-action finder | high (code), med (roles) |
| Proxy gravity | `CPhysicsCharacter` (`pc`, vtable `0x34b6820`): integrate `vf_08` `0x98db50` adds `up * g * dt` when **`pc+0x60`** is set, `g = pc+0x58` = **-9.8** (m/s², Havok units; ctor `FUN_0098e1e0`). Each step the solver `FUN_020cda10` sets `pc+0x60` (`vf_12`) = `PlayerMovement::vf_06()` AND tunables. **Switch for the player only: the `PlayerMovement::vf_06` slot `0x34fc810` (return 0)**; scale: write `pc+0x58` | high |
| Mount state | client `Player+0x1b8` = **`MountInfo`** (0x110 bytes): `+0x20` = `P+0x1d8` mount `HOBJECT`, the rider's stick and camera (`P+0x200..0x22c`), the rider's command values (`P+0x2b0` list). Mount/dismount: anim modifier `SetMount` -> server `CCharacter::vf_50` (mount) / `vf_49` (dismount) -> client `FUN_00a3df70(P, mount, stateName)` | high |
| Mount steering | the beast's own anim graph is driven by **the rider's camera-relative stick**: `CAIStateMounted::vf_05` -> `rider->vf_47()` (= `P+0x1b8`) -> `CAIAnimationController::vf_29` `0x2a3cb40` copies `DeltaYaw`, `RawMoveMag`, `CameraDeltaYaw`, camera pitch and `CameraYawMinusPlayerYaw` into the mount's graph (`UsingMountedInput` = 1). With F1/G2 putting the camera on the HMD, steering is **head-relative with no extra code** | high (code), med (one frame late, RT) |
| Rider camera / attachment | `FUN_020d9750(mgr+0x3968, mount)`: the camera block follows the mount; the rider is pinned by the data modifier `AttachToObject` (`FUN_020cac10(movement, 1, mountHObj, node, ...)`: `+0x1a6` attached, `+0x1f8` parent, `+0x200` node). Saddle node name = data (RT) | high (code), med (that riding uses it) |

## 1. The traversal state machine

### 1.1 Two layers

- **Player FSM (client, code)**: built in the Player ctor `FUN_00a43990` (`FSMState<...,IPlayerState,PlayerStateContext>`
  objects, 0x30 bytes: `+0x8` id byte, `+0x10/+0x18` transitions vector, `+0x28` the state implementation). It switches
  command sets and the movement's velocity mode; it does not know about climbing **[high]**.
- **Animation graph (data)**: `CAnimationMgr` states, predicates and modifiers registered by name in `_INIT_1316`
  (`0x5ffe60`, 504 strings) and bound in `FUN_0207eaa0` / `FUN_02084xxx`. All traversal moves live here; the graph tells
  the FSM about falls and landings through `Modifier.PlayerStateEvent` **[high]**.

### 1.2 The player FSM

| Field | Where | Conf. |
|---|---|---|
| states | vector `P+0x7c0` .. `P+0x7c8` of `FSMState*`; add `FUN_00a49fc0`, parent link `FUN_00a4a120` | high |
| **current state id** | byte **`P+0x7f0`**; previous `P+0x7f1`; ctor sets both to 1 | high |
| event queue | list head `P+0x7f8`; push `FUN_00a37080(P, id)`; step `FUN_00a37360(P)` (look up the current state, run its handler for the event, on a transition: exit (0x1f5) old, `P+0x7f0` = new, enter (0x1f4) new) | high |
| per-frame events | the live update `FUN_00a40fe0` pushes 0x1fd and steps (`0xa4119f`), then 0x1fc and steps (`0xa411e4`), before PlayerCombat and the animation | high |
| who reads it | `IAnimationMgrPlayerStateListener` slot 3 `0xa55790` returns `*(u8*)(P+0x7f0)` (the graph's `Predicate.PlayerState`); `FUN_00a3c8d0` (anim inputs) returns at once when it is 1 | high |

State ids, their implementation and command set (the `FUN_021700d0("...")` record each state enables on enter with
`FUN_00aab940(PlayerInput, set)`):

| Id | State (vtable) | Enter | Command set | Velocity mode on enter (`FUN_020c6ff0(pm, impl+0x18 == 0)`) | Conf. |
|---|---|---|---|---|---|
| 1 | `PlayerStateNone` | | - | | high |
| 3 | `PlayerStateSpectating` | `0xabdba0` | `StateSpectating` | momentum (`pm+0x1a2` = 0) | high |
| 5 | `PlayerStateTransitions` (parent of the alive states, holds the transitions) | | - | | high |
| 6 | `AliveStateSupported` | `0xc97c60` | `StateSupported` | root motion (`pm+0x1a2` = 1) | high |
| 7 | `AliveStateCombat` | `0xc97a10` | `StateCombat` | root motion | high |
| 8 | `PlayerStateFalling` | `0xabc7e0` | `StateFalling` | momentum (`pm+0x1a2` = 0) | high |
| 9 | `PlayerStateTransitionAnim` | `0xa49540` | `TransitionAnim` | root motion | high |
| 10 | `PlayerStateLanding` | `0xabd760` | `StateSupported` | root motion (`pm+0x1a2` = 1) | high |
| 11 | `MPHydraState` | `0xebba40` | - | | high |

Events (transitions of state 5, so from any alive state): **0x1f6** -> 1, **0x1f7** -> 6, **0x1f8** -> 8 (Falling),
**0x1f9** -> 7, **0x1fb** -> 3, **0x1ff** -> 9 (TransitionAnim), **0x200** -> 10 (Landing), **0x201** -> 11. Internal:
0x1f4 enter (slot 3), 0x1f5 exit (slot 4), 0x1fc / 0x1fd per frame (slots 5 / 6; the handler offsets 0x19/0x21/0x29/0x31
are virtual member-function pointers) **[high]**.

Who sends them: code sends only 0x1f7 (entering the world `FUN_00a40b90` `0xa40c28`; spectator exit `FUN_00a3aac0`
`0xa3ab07`; server `0x27571bc`), 0x1fb (`0xa3abb6`) and 0x1f9 (PlayerCombat `0xa89b4d`, `0xa8d275`;
`AliveStateCombat::vf_04` `0xc97823`). **Falling, Landing, TransitionAnim and None come only from the graph**:
`CAnimationMgr::PlayerStateEvent::vf_13` `0x21b9780` reads the event id from its data (default 0x1f6) and calls
`mgr+0x8c8` = PlayerBody's listener (`@0x18`), slot 2 `0xa57300` -> `FUN_00a37080(P, id)` **[high]**; a second route
with a notification first: PlayerBody's `IAnimationMgrPlayerHandler` (`@0x48`) slot 3 `0xa4d380` -> `FUN_00a378c0`
(not while `P+0x4c4` = 1) -> `FUN_00a375a0` -> `FUN_00a37080` **[high code, med that the graph uses it]**.

Falling bookkeeping: `PlayerStateFalling::vf_03` stores the start velocity (`pm+0xe0..+0xe8`) at `impl+0x34` and the
start height (`*(body+0x1b8)+0xc` = root y) at **`impl+0x40`**; `impl = *(fsmState+0x28)` of the state with id 8; the
graph also gets `Game_FallingTime` (`FUN_00a3c8d0`) **[high]**.

### 1.3 The graph's traversal pieces

| Piece (data name) | Code | What it does | Conf. |
|---|---|---|---|
| `Modifier.FindContextAction`, `Predicate.FoundContextAction*`, `Value.ContextActionType` | `CAnimationMgr::ContextActionFinder` (vtable `0x38a4f48`, update `0x21d0cd0`); `FindContextActionOptim` `FUN_0219b380`, `FindContextActionEnds` `FUN_021a29f0`, `FindContextActionOutput` `FUN_021a9eb0`; PlayerBody's handler `@0x30` slot 2 `0xa4d290` -> `FUN_0219b380(..., mgr)` (`[this+0x388]` of the `@0x30` subobject = `body+0x3b8`) | finds authored context actions (ledges, beams, corners, ends) near the player; "Stores the flags of the ContextActionType that was found" | high (names), med |
| `Modifier.DetectEdge`, `Predicate.DetectedEdge` | `CAnimationMgr::EdgeDetector` (update `0x21cbd90`, `DetectEdgeData`) | ledge/edge detection by casts | med |
| `Modifier.FindWallPlant`, `Predicate.CanWallPlant` | `CAnimationMgr::WallPlant` (update `0x21c3f40`) and predicate `FUN_0209d600` -> **`FUN_021a3a60` ("FindWallPlant")** | feeler rays at the wall; with the data flag `ClimbingWall` the hit surface must be `CanWallClimb`; writes the wall point and normal yaw to graph variables (`WallPositionOutput`, `WallNormalOutput`) | high (code), med (names of the outputs) |
| `Predicate.FoundWallClimbEdge` ("at an edge of an axe climb wall") | `FUN_0209e350` -> `FUN_021922a0` (also tests `CanWallClimb`) | the top/side edge of a climbable wall | med |
| `Modifier.UpdateSlope`, `Predicate.OnSlope` | `0x21ceda0`, `FUN_0209d730` -> `FUN_02195c10` (tests `IsSlippery`) | slopes and slides | med |
| `Modifier.Amtrak`, `Modifier.AmtrakDFA` | `CAnimationMgr::Amtrak` (`0x38a44c8`) | moves the character along a rail (context-action line): shimmy / beam walking | low |
| `Modifier.FindRailing`, `Modifier.FindTrampoline`, `Modifier.AlignToBrush`, `Modifier.MoveWithBrush` | `RailingFinder`, `FindTrampoline`, `AlignToBrush`, `MoveWithBrush` | railings, launch points, brush-relative motion | low |
| `Modifier.AttachToObject` | `0x21bd9c0` (begin), `0x21b7460` (end) -> `FUN_020cac10` | pins the character's movement to another object's node (mounts, 3) | high |
| `Modifier.PlayerStateEvent`, `Modifier.AddStateTags` | `0x21b9780`; `0x21b84c0` (parse, up to 10 tags) / `0x21b72f0` (adds them every frame to `mgr+0x8d0`'s building set) | the FSM events of 1.2; the TAGS the probe logs | high |
| `PhysicsSupported` (native predicate/value) | registered by the player binding at `0xa57398`: `FUN_0208dac0(mgr, "PhysicsSupported", FUN_00a4be10, ...)` | `pc->vf_14()` (proxy has ground support) | high |

Commands traversal uses (run 3 bindings, KBM mode): 16 `Jump` (Space / pad A), 18 **`FreeRun`** (pad A, held = free-run,
the "wraith-run" of design-vr.md), 27 **`LedgePullUp`** (Space / A), 28 **`LedgeDropdown`** (E / B), 15 `Crouch` (RB)
**[high]**. `FreeRunHandler` (`0x2a4cd70`, "Hero Free Run") is an AI path handler, not the player **[med]**.

### 1.4 Reads for the VR camera and comfort

All on the game thread in G1 (after the player update), all plain loads except `vf_14`:

| Signal | Read | Use | Conf. |
|---|---|---|---|
| FSM state | `*(u8*)(P+0x7f0)`; rising edge of 8 | **fall**: Head mode, fall vignette from 3 m (`root.y` vs `impl+0x40`); 10 = landing dip; 9 = a transition animation (RT which ones) | high |
| proxy support | `pc->vf_14()` (`0x98da20`: `hkpCharacterProxy::checkSupport` != unsupported), `vf_15` (`0x98da90`: sliding); `pc` from `pm+0x50`, take `vf_02`/`vf_03` (AddRef/Release) around it as `FUN_00a4be10` does; or call `FUN_00a36db0(P)` (the client's own test: mounted -> the beast's movement `vf_34`, else `PlayerBody::vf_16`) | `fsm == 6 && !supported` = on a wall/ledge (the graph did not declare a fall) **[med; RT]** | high (read) |
| gravity flag | `*(u8*)(pc+0x60)` | off while the solver parks the player (standing still, see 2.3) and in attacks without gravity; RT whether climbs clear it | high (read), RT |
| attached | `*(u8*)(pm+0x1a6)`, `*(pm+0x1f8)` parent `HOBJECT`, `*(u32*)(pm+0x200)` node | rider on a mount, or any attach-to-object move | med (that `pm` is the attached movement) |
| velocity mode | `*(u8*)(pm+0x1a2)` (1 = velocity from root motion, 0 = momentum) | momentum = airborne | high |
| state TAGS | `FUN_0239d980(body+0x3c0)` + `DBRecord_GetName` (re-firstperson.md 5) | **the** per-move classifier (climb, hang, shimmy, vault, mantle, leap, drop): a table of names | high (read), RT (names) |
| mounted | `*(P+0x1d8) != 0`; tag `Mount` (`FUN_00a42fc0` `0xa4391b`) | Seat mode | high |
| paired action / presentation | G14, G16 (re-m1m2.md 6, 7) | stage view / window | high |

Recommended classifier until the TAG table exists: Seat if mounted; Head + fall vignette if `fsm == 8`; Head if
`fsm == 9`, or `fsm == 6 && !supported`, or `|root.y - last.y| / dt > 150 cm/s` while `fsm != 8`; else Root. Replace the
middle rules with TAG names after the drill **[med]**.

### 1.5 Which state means what

| Move | Static answer | RT |
|---|---|---|
| fall | FSM 8 (graph event 0x1f8), start height `impl+0x40` | the fall-damage height (Falling `vf_05` `0xabd230` reads it from the player record) |
| landing / roll | FSM 10 (event 0x200), root-motion velocity | duration |
| climb up/down/sideways on a wall | graph: WallPlant / FoundWallClimbEdge on `CanWallClimb` surfaces; FSM stays alive (6 or 9) | TAG names; whether the proxy is unsupported and gravity on |
| ledge hang, shimmy, pull-up, drop | context actions (ledge lines), Amtrak rail movement; commands 27/28 | TAGS; whether `pm` attaches |
| vault / mantle | context action or edge + transition animation (FSM 9 likely) | TAGS, FSM, **vault duration** (design-vr.md section 3) |
| leap between ledges | `Jump` (16) while free-running, then falling (8) or a caught ledge | TAGS |
| free-run ("wraith-run") | `FreeRun` (18) held; vaults/leaps chosen by the graph | sprint speed (`Game_SmoothedVelocity`, `pm+0xf8..+0x100`) |
| forge tower | climbed as walls; the top is a sync action (G14) plus a presentation (G16) | record names |

### 1.6 Probes for item 1

- `state` (exists): add `fsm=%d/%d` (`P+0x7f0`/`+0x7f1`) to the STATE line; it changes rarely, so the line stays cheap.
- **`trav SECS`** (new, read-only): every tick for SECS: `fsm`, `pm+0x1a2`, `pm+0x1a6`/`+0x1f8`/`+0x200`, `pc+0x60`,
  `vf_14`/`vf_15`, root position, head node y, `Game_FallingTime`; the TAGS names on change; with the `TRAV_PM_STEP` slot
  hook (section 4) also the step's MoveParams bytes `+0x11`, `+0x13`, `+0x14`, `+0x8 != 0` and `dt`.
- **Traversal drill** (cmd file, dev save, a wall and a ledge near the start): `mark climb; trav 30; pad A 4000;
  move 0 1 4000` (free-run into a wall), `cmd LedgePullUp`, `cmd LedgeDropdown`, a vault over a low wall, a jump from a
  roof (`cmd Jump` while running), a 5 m drop; shots at each mark. Gives the TAG table, the vault duration, the FSM
  path of each move and which moves keep gravity on.

**Runtime result (P7, 2026-10-20, `logs/boot-check-20261020-001117`; runtime-probes.md P7 results; a smoke test, not the
drill).** Holds: `fsm=` (`P+0x7f0`/`+0x7f1`) reads sensibly - 1/1 while the save loads, **6/1** (`AliveStateSupported`)
in the world, **7/6** (`AliveStateCombat`) from the first stock strike, back to **6/7** 3.8 s later - and `trav` reads
velocity mode 1, `attached=0`, `gravity=1`, `supported=1`. The planned 1 m drop did not happen: the
`Teleport` put the root 130 cm above the y asked (the root is at pelvis height) on the ground, and Talion then slid
114 cm down and about 1 m sideways in 6.1 s while the tags said `Gnd Idl` and the FSM stayed 6 with support: a slope
slide is not a fall for the game, so 1.4's `|root.y - last.y| / dt > 150 cm/s` rule would not fire on it (18 cm/s)
and none is needed. **Wrong in 2.4: `pc+0x58` reads -20 at runtime**, not the constructor's -9.8 (something writes
it after construction; a gravity scale must start from the live value). Wrong in the probe:
`head_y=0.0` in every `TRAV` line (the head node read fails). Tags seen: `Gnd`,
`Idl`, `Allow_Showdown`, `CloseToWalls`, `Tight`, `ApproachingWall`, `InCombat`, `CombatCam`, `Stl`, `Teleport`,
`InGrassCam`. Open: everything else of T1-T5 (the drill).

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`).** The probe's head read is fixed: `head_y`
is the `Head` node's height and `head_dy` = 71.9-73.5 cm above the root while idle (run 3: 72.5). The Teleport's own
tick reads one stale value (-1723). Also seen in the fight: the FSM goes to 7/6 at every hit on Talion (his recoil)
and back to 6/7 ~1.4 s later.

**Runtime result (P10, 2026-10-20 15:46, `logs/boot-check-20261020-154655`; runtime-probes.md P10 results).** No
traversal state was reached. Both wall runs (`FreeRun` + the full stick at the nearest obstacle, stock and with the
probe's `loco`) ran inside a patrol's melee: recoils every ~2 s (FSM 7/6 as in P8), no FSM 9, no climb. `heading
wall` took the orcs for walls (0.5 and 1.2 m). The evade at an orc 3.2 m away was **`PC_Dash`**, not a vault: an
attack in FSM 7 with root motion and no sync, 5.9 m in 0.95 s, up to ~12 m/s, yaw set once at its start. Tags
besides P7's list: `Lnd` (each start from rest), `Run` and `Spr` (with `FreeRun`). `head_dy` 72.7-73.4 cm while idle,
37-58 in the fight. LR6/LR9 (design-locomotion.md 2.10) are P11's, away from any patrol.

## 2. Climb surfaces and the proxy's gravity

### 2.1 How a surface becomes climbable

- **Surface records**: category `Surfaces/Surfaces` (`FUN_021749c0`), schema `Surface.Surface` built by `FUN_0261cae0`
  into `0x4c1cd40` (pointer `*0x4c1cd00`, set in `_INIT_1317` at `0x61ef0d`). Attribute descriptors are 0x30 bytes:
  `+0x0` name, `+0x8` hash, `+0xc` type, `+0x18` "slow path" count, `+0x28` index. Order: 0 `CanSeeThrough`, 1
  `CanShootThrough`, 2 `CanDoFootsteps`, 3 `CanMeleeThrough`, 4 `FootPrintFX`, 5 `FootPrintLifetime`, 6
  `SoundSurfaceType`, 7 `LandingSnd`, 8 `ActivationSnd`, 9 `MoveNoiseMod`, 10 `WeaponFX`, 11 `OcclusionFactor`, 12
  `CollisionProperty` (record link), **13 `CanWallClimb` (bool)**, 14 `IsSlippery`, 15 `CanWargJump`, 16 `FXContextList`
  **[high]**.
- **Readers of `CanWallClimb`** (`schema+0x288`/`+0x298`): `FUN_021902d0` (by surface id), `FUN_021922a0` (wall-climb
  edge), `FUN_021a3a60` (wall plant). `IsSlippery` is read by `FUN_02195c10` (slopes), `CanWargJump` by `0x20ae390`
  **[high]**.
- **Surface of a hit**: `FUN_02ae5d90(RayResult* r)`: if the hit object (`r+0x20`) has a surface id `*(u16*)(obj+0x5a)`,
  that one, else if `r+0x2a` the triangle's id `*(u16*)(r+0x28)`; the id is looked up in the table
  `[*0x4c5cb50, *0x4c5cb58)` of `{u16 id; Record* rec}` (16 bytes each). By id: `FUN_02ae5e60(&{u16 id, u8 1})` **[high]**.
- **Wall plant** `FUN_021a3a60(mgr+0x34d8, data, xf, out, mgr)`: up to four feeler casts through IEnginePhysics
  vtbl+0x200 (= G21) from offsets in the state's data record (schema `FUN_0258c8b0`: `MinimumHeightFromGround`,
  `AttachAngle`, `FeelerDist`, `FeelerLocalOffsetPos/Yaw`, `PlayerDimWidth`, `ClimbingWall`, `AxeClimbFeelerOffset`,
  `WallPositionOutput`, `WallNormalOutput`); if `ClimbingWall` is set and the surface is not `CanWallClimb`, no plant;
  on success it writes the wall point and the normal's yaw into graph variables (names = data) **[high code, med names]**.
- **Ledges and edges** are authored: `CContextActionDB` (world loader `CContextActionWorldLoader`, query
  `FUN_020fb1f0`, cache `FUN_020f6330`), records `Character/Models/ContextActions`, `.../ContextActionData/
  ContextActionPoint|Group|Rig`; the finder walks lines ("LookForContextActionLines" `FUN_02406be0`) **[high names, med]**.
- **Character limits**: `CapsuleMaxClimbAngle`, `DeadlyEdgeLedgeCheckDistance` in the character schema `FUN_02604b90`;
  `AxeClimb` (`FUN_02572ba0`) **[med]**.

So the game's "climbable" = authored context-action geometry (ledges, edges, beams) **or** a wall whose collision
surface says `CanWallClimb`, found by data-driven feeler rays. A VR hand can ask the same question (2.2).

### 2.2 Reading climbability from the mod (for B's grip test and A)

```c
/* G21 ray (re-gameplay.md 5) from the palm along its forward, 25 cm; r = the RayResult */
void *rec = FUN_02ae5d90(&r);       /* surface record: object id *(u16*)(*(r+0x20)+0x5a), else triangle id r+0x28 if r+0x2a */
char *desc = *(char **)0x4c1cd00;   /* Surface.Surface schema; CanWallClimb descriptor at +0x270 */
bool climb = false;
if (rec && *(int *)(desc + 0x288) >= 2) {                       /* fast path, as FUN_021902d0 */
    uint8_t *e = *(uint8_t **)(rec + 8) + 16 * *(uint8_t *)(desc + 0x298);
    uint16_t bit = *(uint16_t *)(e + 0xe);
    climb = *(uint16_t *)(e + 0xc) && ((*(uint32_t *)(*(char **)(rec + 0x10) + 8 * (bit >> 5)) >> (bit & 31)) & 1);
} else if (rec)
    climb = FUN_021901b0(rec, desc + 0x278, &tmp);              /* slow path */
```

With a surface id instead of a record: `FUN_021902d0(&(struct { uint16_t id; uint8_t one; }){ id, 1 })` **[high code;
RT that a G21 ray from the mod fills `+0x20`/`+0x28`/`+0x2a` like the game's feelers]**. Ledges are not surfaces: for
a ledge grab use the game's own context action (B) **[med]**.

### 2.3 The movement step and the proxy

`PlayerMovement::vf_10` (`0xab59a0`, slot `0x34fc830`; caller = the engine's movement code, indirect, RT) calls
**`FUN_020cda10(pm, delta*, MoveParams* p, 0)`** (`0xab59b5`), the shared solver (NPCs: `CAIMovement::vf_10`):

| Step | Code | Conf. |
|---|---|---|
| gravity wish | `on = pm->vf_06()` (`0xab58a0`) `&& *(u8*)0x4b55b83 && (p+0x11 || *(u8*)0x4b55c27)` (both bytes are BaseMovement tunables, default 1, set at `0x5d5429`/`0x5d52fa`) | high |
| proxy create | if `pm+0x50` is 0: `FUN_020c72f0` (shape) + IEnginePhysics vtbl+0xc0 (create character); capsule rebuild `FUN_020c8560` (`FUN_02231c90` capsule / `FUN_02232130`) from the player record `pm+0x448` | high |
| set gravity | `pc->vf_12(on)` (`0x98d9c0`: `pc+0x60 = on`) | high |
| desired velocity | `(delta + shape offset) / dt` into `pm+0xe0..+0xe8`; `pm+0x1a2` = 1 (root-motion mode) sets x/z to it, and y too only while gravity is off (with gravity on `vy` keeps integrating); `pm+0x1a2` = 0 adds it to the previous velocity (momentum mode, `pm+0x1a1` subtracts the last root velocity `pm+0x104..`) | high |
| park on ground | if `on` and supported and not sliding and `|v| <= *(float*)0x4b55b84` (75 cm/s): gravity off for this step, `vy = 0` | high |
| proxy path (`p+0x14 == 0`) | `pc->vf_06(v)` set velocity, `vf_08(dt)` integrate (2.4), `vf_09` position, `vf_10` velocity; checks "Havok returned invalid velocity/position"; gravity back on after | high |
| kinematic path (`p+0x14 != 0`) | no proxy integrate: the clipper at `pm+0x58` (`FUN_0224b630`/`FUN_0224b920`) moves the point | high (code), low (when the graph uses it) |
| context-action clipping | if `*(u8*)0x4b55ca4`: `FUN_020ca3c0` ("UpdateContextActionClipping") clips the move against context-action geometry | high (code), med |
| attack scaling | `pm+0xec..+0xf4` per-axis scale; during an attack the attack's factors (`combat+0x56c`) | high |

`p` (MoveParams): `+0x0` dt, `+0x8` sync action (aligned test `FUN_02805c90`), `+0x11` gravity allowed, `+0x13` keep on
ground in syncs, `+0x14` kinematic **[high offsets, med meanings, RT values per state]**.

**Runtime result (P10, 2026-10-20 15:46; runtime-probes.md P10 results).**
- **The record's capsule and gravity**: StandCapsuleHeight 125, Radius 35, StepHeight 40, PhysicsCharacterGravity
  -2000 (cm/s^2), read through `pm+0x448`. The proxy holds the same as -20 (m/s^2, `pc+0x58`), so 2.4's "-20 at
  runtime" is the record's value. FallMinHeight 100 cm. **FallDamageMin/Max are 0**: the record deals no fall damage
  at any height (the FallDamage heights 500/1200 cm are moot unless another path deals it, RT).
- **A supported body keeps no vertical launch.** Momentum mode (`FUN_020c6ff0(pm, 0)`), `pm+0xe0..` = (0, 600, 0)
  or (376, 500, 137) and gravity on, written in the probe tick: one step later `vy` = 0 with x and z kept, still
  supported, FSM 6. `|v|` was far above the park rule's 75 cm/s, so something else drops it: the proxy's ground
  contact, or FSM 6's own handling of a supported body **[RT fact, the code not traced]**. The horizontal part then
  glides on in momentum mode (3.7 m in 0.98 s, 376 -> 331 cm/s on a slight upslope) until root-motion mode comes back.
  The graph never brought it back here; P10's probe did. A launch needs its liftoff first (design-locomotion.md 2.7).
- **Characters deflect, they do not block.** The probe walked Talion (250 cm/s, our root delta) at an `AI_Infantry`
  2.3 m ahead. The solver's velocity turned up to ~57 deg around it and the root slowed to 136 cm/s, then passed it
  56.5 cm root to root (0.81 of the asked speed on average). The orc moved 33 cm. A one-shot `pm+0x428` push (20 cm)
  passed it too.

### 2.4 Gravity

- **Applied** in `CPhysicsCharacter::vf_08` `0x98db50` (`pc, float dt`): `checkSupport(-up)`; `v = proxy velocity`; if
  supported: `v += surface velocity`, `pc+0x61 = 1`; else if `pc+0x61` (was on ground) drop the vertical part,
  `pc+0x61 = sliding`; **if `pc+0x60`: `v += up(proxy+0x90) * pc+0x58 * dt`**; set velocity; world lock;
  `hkpCharacterProxy::integrate` (`0x17adcd0`) **[high]**.
- **Value**: `pc+0x58` = -9.8 (float `0xc11ccccd`, m/s², the proxy works in metres: positions/velocities are scaled by
  100 to cm in `vf_09`/`vf_20`); `pc+0x60` = 1 at construction (`FUN_0098e1e0`) **[high]**. No code writes `+0x58` after
  the ctor (searched the CPhysicsCharacter methods) **[med]**.
- **Switches** (player only): (a) **`PlayerMovement::vf_06` slot `0x34fc810`** -> return 0 = no gravity from the next
  step on (NPCs use `CAIMovement`'s vtable, untouched); (b) per step after `FUN_020cda10`'s `vf_12`: write `pc+0x60` = 0
  (overwritten next step); (c) scale: `pc+0x58` (e.g. -9.8 * k). `PlayerMovement::vf_06` itself returns the current flag
  except during an attack whose attribute check `FUN_026d3a10` fails, then `combat+0x56e` **[high]**.

**Runtime result (P7).** The player's `pc+0x58` reads **-20** in the world (every `TRAV` line), not -9.8: the [med]
"no code writes `+0x58` after the ctor" is wrong (the writer is outside the CPhysicsCharacter methods searched).
`pc+0x60` was 1 while standing and sliding. Switch (c) must scale the live value. Next: find the writer (a
write watch on `pc+0x58` from the world load).

### 2.5 A hand-over-hand ("A") recipe and risks

Mechanism (static plan, every step RT):
1. While the player's hands hold the wall (2.2 says climbable), override `PlayerMovement::vf_06` -> 0 (gravity off).
2. Move Talion by the hands' motion with the run 3-proven extra delta `pm+0x428` / `+0x444` (`PM_ROOTMOTION`, re-m1m2.md
   3): it goes through the proxy, so it collides and slides along walls (no tunnelling at hand speeds; run 3: 30 cm into a
   wall gave 17.8 cm of slide). In root-motion mode with gravity off the delta's y becomes the vertical velocity too
   (2.3), so pulling down on the wall lifts Talion.
3. Keep the graph out of the fall: it decides falling from `PhysicsSupported` / `IsSupported` (data). Replace the
   callback registered at `0xa57398` (`FUN_00a4be10`) and `IAnimationMgrCharacterPhysics` slot 2 (`0x34f2a10` ->
   `PlayerBody::vf_16`) with "true while holding" **[med]**. Without it the graph sends 0x1f8 and plays a fall while the
   proxy hangs.
4. Release: restore both; gravity returns at the next step.

Risks:
- **The graph keeps running its own traversal**: with "supported" faked it may start ground locomotion against the wall,
  or its own WallPlant climb (which is B). A needs a graph state that holds a neutral pose; none is known statically.
  Highest risk; RT (watch TAGS and FSM with `grav off`).
- **Sticking**: the proxy depenetrates by itself; a hand pulling into a ceiling is clipped by the proxy like a wall.
  Low risk while all motion goes through `pm+0x428` **[med]**.
- **Falling through**: only if the motion bypasses the proxy (Teleport, kinematic `p+0x14`); A must not use them
  **[med]**.
- **Ledge tops**: getting over an edge needs a pull-up animation; A would hand over to the stock `LedgePullUp` (27).
- `pm+0x1a2` root-motion mode means no drift when the stick is idle; in momentum mode (Falling, state 8)
  the velocity persists: start A only in state 6 **[high]**.

B (recommended) needs none of this: the stick (`move`, proven in run 3) and commands 16/18/27/28 drive the game's own
climb; the grip test of 2.2 only decides when the hands count as holding.

**Status (2026-10-20): a prototype of this recipe is in `mod/somvr`, behind `SOMVR_CLIMB=b`** (re-xr-pipeline.md 12;
offline-tested, never booted). Steps 1, 2 and 4 as above with P8's lessons: gravity by the flag `pc+0x60` itself (0
while a hand holds, kept 0 at every PM call, written 1 at every end; the `vf_06` slot is not patched: it is the probe's
`P_GRAV`), the move through `pm+0x428` with its vertical part, the grip test of 2.2 (rays along the palm and the
fingers, Talion's own body skipped by the cast's filter callback as the combat collider uses it, else by stepping past
him), the surface's CanWallClimb by `FUN_021902d0`. Not done: step 3 (the supported lie). Instead the climb stands down
as soon as the FSM leaves 6 (a fall declared by the graph, a transition animation, combat), so the first boot answers
whether the graph lets Talion hang (12.6 check 3). Ledge tops: release, the mod's own lift, or the stock `LedgePullUp`
pulse (RT whether it does anything from our hang).

### 2.6 Probes for item 2

- **`surf`** (new, read-only): from the head (and each hand once controllers exist) cast G21 forward 2 m; log the hit
  point, `r+0x28`/`+0x2a`, the hit object's `+0x5a`, the surface record name (`DBRecord_GetName`) and `CanWallClimb` /
  `IsSlippery`. Stand at a climbable wall, a plain rock and a wooden wall.
- **`grav off|on`** (new, active): the `PlayerMovement::vf_06` slot override; with `pmmove 0 30 0` at a wall: does
  Talion stay up, what do FSM/TAGS do, does the fall start when restored. Then `grav off fake` adds the supported lie.
- `trav` (1.6) logs `pc+0x60` and the MoveParams during the stock climb: does the game itself turn gravity off on walls.

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results; at the tower
top, not at a wall).**
- **`surf` (T6):** the G21 ray from the mod returns a hit with a readable surface record (`DBRecord_GetName`,
  CanWallClimb, IsSlippery all read). But both rays hit **Talion himself** first: from the head along his forward,
  `Flesh_Player` (surface 59251) at 0 cm; from the camera along the view, at 135.9 cm. The normal (0, 0, 0) and the
  fraction 0 were not filled in. A hand or head ray must skip the player's own body (a filter on the hit object, or a
  start point outside his capsule) before it can test a wall.
- **`grav off` (T7):** switch (a) holds Talion. After `grav off` + `pmmove 0 30 0` the root rose 30 cm in one tick and
  stayed there 3.6 s, with `pc+0x60` = 0, `fsm 6/1`, `supported=1` and the tags `Gnd Idl`: the graph did not notice,
  and there was no fall state.
- **But `grav on` does not give gravity back.** With the game's `PlayerMovement::vf_06` again, `pc+0x60` stayed 0 and
  Talion hung 30 cm up until a Teleport reset the flag. This is 2.4's "vf_06 returns the current flag": once forced
  to 0, it latches. Recipe A's release must write `pc+0x60 = 1` (or switch (b) per step) when the hands let go.

## 3. Mounts

### 3.1 Objects

| Piece | Where | Conf. |
|---|---|---|
| **`MountInfo`** (vtable `0x38a63f0`, `ISharedCombatInputHost`, ctor `FUN_021e12c0`) | member of the client `Player` at **`P+0x1b8`** (the Player dtor `FUN_00a463f0` resets its vtable there) | high |
| mount `HOBJECT` | `mi+0x20` = **`P+0x1d8`** (WBObjRef at `P+0x1c0`); transform at `obj+0x8` (pos) / `+0x14` (quat x,y,z,w) | high |
| rider inputs (written by `FUN_00a3c8d0` every frame) | `P+0x200` `RawMoveMag`, `+0x204` `DeltaYaw`, `+0x208` `CameraDeltaYaw`, `+0x20c` `LastSpike`, `+0x210` `WalkAndTalkScale` (= `mi+0x48..+0x58`); `+0x220` camera quat `cam+0x484 x cam+0x458` (= `mi+0x68`); `+0x214..+0x21c` a world point the mount aims at (`mi+0x5c`, writer not traced) | high (fields), low (`+0x214`) |
| pending state name | `P+0x230` (`mi+0x78`, 0x80 chars): the mount animation state requested by `SetMount`; cleared by `CPlayerObj::vf_48` | high |
| rider commands | `mi+0xf8` array of `{int cmdId; float value}`, count `+0x108` (`P+0x2b0`, `P+0x2c0`); filled each frame by `FUN_00a3e450` from the PlayerInput active command list (`FUN_021e16f0`, `FUN_021e17d0`); read by `MountInfo::vf_04` (value) / `vf_05` (pressed, > threshold) | high |
| commands | 181 `MNT_Mount`, 182 `MNT_GetOff`, 183 `MNT_Jump`, 184 `MNT_Charge`, 185-188 `MNT_Attack_1..4` (no KBM bindings in run 3's world set: they belong to the mounted command set, RT) | high (ids) |
| server | rider `CCharacter`: `this[2]+0xc0` = mount ref (tag `Mount` is added while it is set, `FUN_00a42fc0`); mount NPC: `npc+0x40` = rider, `npc+0xc40` = its `CAIAnimationController`, `npc+0xc0` its movement | high |
| rider anim var | `MountWarg_Yaw` = the mount's yaw (`FUN_00a3c8d0`) | high |

### 3.2 Mount and dismount

| Event | Chain | Conf. |
|---|---|---|
| **mount** | the rider's graph runs `Modifier.SetMount` (`CAnimationMgr::SetMount::vf_12` `0x21c5d30` begin, `vf_13` `0x21c64d0` update; object from a graph variable, logs "[Mounting][Anim]") -> rider `vf_50` = `CCharacter::vf_50` `0x26a2b60` (Mount: checks the beast's rider slot `+0x40`, enables the mount's state) -> for the player `FUN_00a3df70(P, mountHObj, stateName)` (`0x26a3085`) | high |
| client set | `FUN_00a3df70`: validates the beast (character class; refuses it while its game object's `+0x944` is set, likely dead **[med]**), copies the state name to `P+0x230`, **`FUN_020d9750(mgr+0x3968, mount)`** (camera block follows the mount, `0xa3e038`), graph bool `MountAlive` = 1, `P+0x1d8` = mount, `FUN_00da9db0(P+0x738, beast)` (the walk-and-talk follower), a broadcast with the beast's `+0x160/+0x164` (most likely the UI's `MountSetParams`, `CUIHUDCrosshair::OnMountSet` **[med]**) | high |
| **dismount** | `CCharacter::vf_49` `0x2693b30` (server) -> `FUN_00a3df70(P, 0, 0)` (`0x2693c7c`); from the graph `FUN_021c62d0` (`0x21c633f`); the beast dead or invalid: `FUN_00a3e450` (`0xa3e4c7`, every frame) | high |
| client unset | `P+0x1d8` = 0; Talion's body is set upright with its flat yaw (objsys vtbl+0x98 on `*(body+0x1b8)`); `FUN_00da9790(P+0x738, P)` | high |
| prompts / sync | branding a beast and dropping onto one are graph states and sync actions (G14); `MNT_Mount` / `MNT_GetOff` are the commands | med |

Detect without hooks: poll `*(P+0x1d8)` in G1 (rising = mounted, falling = off) **[high]**.

### 3.3 Steering: what the beast reads

- Every AI update of the beast: `CAIStateMounted::vf_05` `0x2a90420`: `rider = *(npc+0x40)`; `mi = rider->vf_47()`
  (`CPlayerObj::vf_47` `0x27aa100` = client `Player + 0x1b8`); if `mi+0x78` names a state, activate it on the beast's graph
  and clear it (`vf_48`); then `FUN_02a43030(*(npc+0xc40), mi)` -> **`CAIAnimationController::vf_29(ctrl, mi)`**
  (`0x2a3cb40`, shared by the base, `...Enemy` and `...Hero` vtables) **[high]**.
- `vf_29` on the beast's graph: `UsingMountedInput` = 1; `AIDesiredWorldYaw` = its own desired heading
  (`*(npc+0xc58)+0x258`); **`DeltaYaw` = `mi+0x4c`, `RawMoveMag` = `mi+0x48`, `CameraDeltaYaw` = `mi+0x50`**, `LastSpike`,
  `WalkAndTalkScale`; `Game_CameraPitch` and **`CameraYawMinusPlayerYaw` = camera yaw (`mi+0x68`) - the beast's yaw**;
  `Game_CombatAimAtPitch/Yaw` toward `mi+0x5c`. `vf_30` (`0x2a3cea0`) zeroes them and sets `UsingMountedInput` = 0
  **[high]**.
- So the beast steers with **the rider's stick as computed for the rider**: `DeltaYaw` = the signed yaw from the
  rider's facing to "stick rotated by the camera yaw" (`FUN_00a3c350`, re-input.md 4). The rider rides attached, so his
  facing is the beast's **[med]**. The beast's graph (data) turns and runs from those values; its combat input host is
  the same `MountInfo` (MNT_* commands) **[high code, med meaning]**.

### 3.4 Rider attachment and camera

- Camera: `FUN_020d9750` stores the beast in the camera block (`mgr+0x3968`, ref `+0x738`, object `+0x748`); the
  mounted camera modifiers frame the beast. F1 replaces the camera in VR anyway **[high]**.
- Body: the mounted graph state pins the rider with `Modifier.AttachToObject`: `vf_12` `0x21bd9c0` takes the object
  from a graph variable, finds a node by the data's name with IEngineModel vtbl+0x80 (GetNode) and calls
  **`FUN_020cac10(movement, 1, parentHObj, node, ...)`**: `+0x1a6` = attached, `+0x1e8` WBObjRef -> `+0x1f8` parent,
  `+0x200` node, `+0x1ab..+0x1ad` options; `vf_14` `0x21b7460` detaches (`FUN_020cac10(movement, 0, 0, -1, ...)`).
  `movement = FUN_0208d540(mgr)` = `*(*(mgr+0x560)+0x38)+0xc0` **[high code; med that the player's mount uses it, and
  which movement object that is for the player: RT]**.
- Saddle node: its **name is data** (the `AttachToObject` record); at runtime it is `*(u32*)(movement+0x200)` on the
  beast `*(movement+0x1f8)`; read its world pose with G12 GetNodeTransform **[RT]**.

### 3.5 For the VR design (section 5)

- **Head-relative steering is free**: F1/G2 write the gameplay camera from the HMD before `FUN_00a3c8d0` of the next
  frame computes `DeltaYaw` and the camera quat at `P+0x220`; the beast reads them in its AI update. Latency: one frame
  (the server AI runs after the client player update) **[med, RT]**.
- **Other steering modes** (hand- or seat-relative): rotate the stick before `FUN_00a3c350` (re-input.md 4, affects on
  foot too), or mount only: pre-hook `CAIAnimationController::vf_29` (slots `MOUNT_AI_INPUT*`) and rewrite `mi+0x4c`
  (`DeltaYaw`, degrees, the value read back from the rider's graph **[med unit]**), `mi+0x68` (camera quat) and
  `mi+0x48` (magnitude). Nothing else reads those three fields of `MountInfo` **[med: only vf_29 found]**.
- **Car-style yaw (D13)**: the beast's yaw = the quat at `*(P+0x1d8)+0x14` (or the rider's `MountWarg_Yaw`); `psi += Δyaw`.
- **Seat anchor**: the saddle node (3.4) or, simpler and data-free, Talion's root/pelvis while `P+0x1d8 != 0` (he is
  attached, so the root follows the saddle); smoothing as designed.
- While mounted: no G13 body turns and no `pm+0x428` moves (they would fight the attachment) **[med]**.
- Mount/dismount blink: poll `P+0x1d8`; the transition animation itself runs before the client set (paired action, G14).

### 3.6 Risks and probes for item 3

- RT: which movement object `AttachToObject` writes for the player; whether the beast's graph turns by `DeltaYaw` or by
  `CameraYawMinusPlayerYaw` (both come from the head camera, so head-relative either way); the frame order of the
  client update and the beast's AI update.
- **`mount`** (new, read-only): while `P+0x1d8 != 0`, every tick: the `MountInfo` fields (`+0x48..+0x6c`, the command
  list), the beast's transform, `pm+0x1a6/+0x1f8/+0x200` (and on the beast's movement), the attach node's name (GetNode
  table of the beast's model, as `nodes` does for the player); one line per change of the pending state name.
- **Mount drill**: a dev save next to a caragor (or one branded in the run); `state` gives `mount=`; `move 0 1 3000`
  then `move 1 0 2000` with `fpcam on` (the head camera): the beast must run where the camera faces; `cmd MNT_GetOff`.

**Runtime result (P8).** `mount 3` at the tower top (not mounted): 0 lines, as expected. The word runs; T8/T9 need the
mount drill.

## 4. Sites

Generated from the binary with `reference/work-firstperson/sitegen.py` (`reference/work-traversal/sites*.spec`).
K_CALL / K_VSLOT sites are hook candidates in the probe's style (call-site rel32 or vtable slot, never prologues); the
K_ENTRY lines only prove functions the mod calls.

```c
SITE("TRAV_FSM_STEP", K_CALL, 0xa4119f, 0xa4118f, "be fd 01 00 00 48 89 df e8 e4 5e ff ff 48 89 df e8 bc 61 ff ff", 0xa37360, "41 56 41 55 41 54 55 48 8d af f8 07 00 00 53"),
SITE("TRAV_FSM_STEP2", K_CALL, 0xa411e4, 0xa411d4, "24 20 0f 29 74 24 10 f3 0f 11 44 24 28 48 89 df e8 77 61 ff ff", 0xa37360, "41 56 41 55 41 54 55 48 8d af f8 07 00 00 53"),
SITE("TRAV_FSM_EVENT", K_VSLOT, 0x34f2800, 0x34f27f0, "90 55 a5 00 00 00 00 00 40 56 a5 00 00 00 00 00 00 73 a5 00 00 00 00 00 90 57 a5 00 00 00 00 00", 0xa57300, "48 8b 7f f0 e9 77 fd fd ff 0f 1f 80 00 00 00"),
SITE("TRAV_FSM_GET", K_VSLOT, 0x34f2808, 0x34f2808, "90 57 a5 00 00 00 00 00", 0xa55790, "48 8b 47 f0 0f b6 80 f0 07 00 00 c3 0f 1f"),
SITE("TRAV_GRAVITY_WISH", K_VSLOT, 0x34fc810, 0x34fc800, "b0 79 ab 00 00 00 00 00 60 b2 ab 00 00 00 00 00 a0 58 ab 00 00 00 00 00", 0xab58a0, "41 54 55 53 48 8b 5f 50 48 85 db 0f 84 7f 00"),
SITE("TRAV_PM_STEP", K_VSLOT, 0x34fc830, 0x34fc828, "30 bb ab 00 00 00 00 00 a0 59 ab 00 00 00 00 00", 0xab59a0, "41 55 31 c9 41 54 49 89 d4 55 48 89 f5 53 48"),
SITE("TRAV_ACP", K_CALL, 0xab59b5, 0xab59a0, "41 55 31 c9 41 54 49 89 d4 55 48 89 f5 53 48 89 fb 48 83 ec 38 e8 56 80 61 01", 0x20cda10, "55 48 89 e5 41 57 41 56 49 89 d6 41 55 41 54"),
SITE("TRAV_PC_INTEGRATE", K_VSLOT, 0x34b6860, 0x34b6858, "80 eb 98 00 00 00 00 00 50 db 98 00 00 00 00 00 e0 d9 98 00 00 00 00 00", 0x98db50, "66 0f ef c9 0f 2f c8 0f 83 6b 02 00 00 55 b8"),
SITE("TRAV_PC_GRAVITY_SET", K_VSLOT, 0x34b6880, 0x34b6880, "c0 d9 98 00 00 00 00 00 b0 d9 98 00 00 00 00 00", 0x98d9c0, "40 88 77 60 c3 90 66 2e"),
SITE("TRAV_PC_SUPPORTED", K_VSLOT, 0x34b6890, 0x34b6890, "20 da 98 00 00 00 00 00", 0x98da20, "48 83 ec 58 b8 00 80 ff ff 48 8b 7f 40 66 0f"),
SITE("TRAV_PHYS_SUPPORTED_REG", K_CALL, 0xa57398, 0xa57387, "ba 10 be a4 00 be 99 fd 4e 03 48 89 df 48 8b 4d 08 e8 23 67 63 01", 0x208dac0, "0f b6 06 45 31 c0 84 c0 74 22 66 0f 1f 44 00"),
SITE("TRAV_ANIM_SUPPORTED", K_VSLOT, 0x34f2a10, 0x34f2a00, "b0 55 a5 00 00 00 00 00 80 56 a5 00 00 00 00 00 60 c0 a4 00 00 00 00 00", 0xa4c060, "48 83 ef 50 e9 67 ff ff ff 0f 1f 80 00 00 00"),
SITE("TRAV_SUPPORTED_FN", K_ENTRY, 0xa36db0, 0xa36db0, "48 8b 87 20 04 00 00 48 85 c0 74 24 48 8b 40 10", 0xa36db0, "48 8b 87 20 04 00 00 48 85 c0 74 24 48 8b 40"),
SITE("SURF_FROM_RAY", K_ENTRY, 0x2ae5d90, 0x2ae5d90, "48 8b 47 20 48 85 c0 74 45 0f b7 48 5a 89 ce 85", 0x2ae5d90, "48 8b 47 20 48 85 c0 74 45 0f b7 48 5a 89 ce"),
SITE("SURF_CANWALLCLIMB", K_ENTRY, 0x21902d0, 0x21902d0, "48 83 ec 18 e8 87 5b 95 00 48 89 c7 31 c0 48 85", 0x21902d0, "48 83 ec 18 e8 87 5b 95 00 48 89 c7 31 c0 48"),
SITE("WALLPLANT_PRED", K_CALL, 0x209d698, 0x209d688, "ee 0f 29 0c 24 48 89 44 24 10 f3 0f 11 44 24 18 e8 c3 63 10 00", 0x21a3a60, "41 57 41 56 49 89 d6 41 55 49 89 cd 41 54 55"),
SITE("WALLPLANT_MOD", K_CALL, 0x21c4291, 0x21c4281, "04 d0 4c 89 e1 48 89 df 48 8d 54 24 50 48 89 c6 e8 ca f7 fd ff", 0x21a3a60, "41 57 41 56 49 89 d6 41 55 49 89 cd 41 54 55"),
SITE("CLIMBEDGE_PRED", K_CALL, 0x209e3fa, 0x209e3ea, "18 48 8d bb d8 34 00 00 48 8d 54 24 10 48 89 ee e8 a1 3e 0f 00", 0x21922a0, "41 56 41 55 41 54 55 48 89 f5 53 48 89 d3 48"),
SITE("MOUNT_SRV_ON", K_CALL, 0x26a3085, 0x26a3070, "74 09 41 80 3c 24 00 4d 0f 44 e6 48 8b 75 08 4c 89 e2 48 89 c7 e8 e6 ae 39 fe", 0xa3df70, "41 55 41 54 45 31 e4 55 48 89 f5 53 48 89 fb"),
SITE("MOUNT_SRV_OFF", K_CALL, 0x2693c7c, 0x2693c6f, "0f 84 83 02 00 00 31 d2 31 f6 48 89 c7 e8 ef a2 3a fe", 0xa3df70, "41 55 41 54 45 31 e4 55 48 89 f5 53 48 89 fb"),
SITE("MOUNT_ANIM_DISMOUNT", K_CALL, 0x21c633f, 0x21c6330, "d5 4c 00 48 85 c0 74 0c 31 d2 31 f6 48 89 c7 e8 2c 7c 87 fe", 0xa3df70, "41 55 41 54 45 31 e4 55 48 89 f5 53 48 89 fb"),
SITE("MOUNT_DEADCHECK", K_CALL, 0xa3e4c7, 0xa3e4c0, "31 d2 31 f6 4c 89 f7 e8 a4 fa ff ff", 0xa3df70, "41 55 41 54 45 31 e4 55 48 89 f5 53 48 89 fb"),
SITE("MOUNT_CAMFOLLOW", K_CALL, 0xa3e038, 0xa3e020, "48 8b 83 d8 02 00 00 48 89 ee 48 8b b8 b8 03 00 00 48 81 c7 68 39 00 00 e8 13 b7 69 01", 0x20d9750, "48 3b b7 48 07 00 00 74 7d 48 8b 97 38 07 00"),
SITE("MOUNT_INPUTS", K_CALL, 0xa8c8d7, 0xa8c8c7, "c6 83 02 0b 00 00 01 48 8b 7b 08 48 85 ff 74 05 e8 f4 ff fa ff", 0xa3c8d0, "80 bf f0 07 00 00 01 0f 84 fb 10 00 00 41 57"),
SITE("MOUNT_CMDS", K_CALL, 0xa413fd, 0xa413ed, "48 8b 93 48 0d 00 00 88 82 88 00 00 00 48 89 df e8 4e d0 ff ff", 0xa3e450, "48 8b 97 d8 01 00 00 48 85 d2 0f 84 50 0d 00"),
SITE("MOUNT_AI_INPUT", K_VSLOT, 0x3a8fde0, 0x3a8fde0, "40 cb a3 02 00 00 00 00 a0 ce a3 02 00 00 00 00", 0x2a3cb40, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("MOUNT_AI_INPUT_ENEMY", K_VSLOT, 0x3a90c58, 0x3a90c58, "40 cb a3 02 00 00 00 00 a0 ce a3 02 00 00 00 00", 0x2a3cb40, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("MOUNT_AI_INPUT_HERO", K_VSLOT, 0x3a918d8, 0x3a918d8, "40 cb a3 02 00 00 00 00 a0 ce a3 02 00 00 00 00", 0x2a3cb40, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("MOUNT_INFO_GET", K_VSLOT, 0x3995b18, 0x3995b18, "00 a1 7a 02 00 00 00 00", 0x27aa100, "48 83 ec 08 e8 27 9f ee ff 48 85 c0 48 8d 90"),
SITE("MOUNT_SRV_DISMOUNT", K_VSLOT, 0x3995b28, 0x3995b20, "b0 9f 7a 02 00 00 00 00 30 3b 69 02 00 00 00 00 60 2b 6a 02 00 00 00 00", 0x2693b30, "55 48 89 fd 53 48 83 ec 68 48 8b 4f 10 48 8b"),
SITE("MOUNT_AI_STATE", K_VSLOT, 0x3adc808, 0x3adc808, "20 04 a9 02 00 00 00 00", 0x2a90420, "41 56 41 55 41 54 55 53 48 89 fb 48 83 ec 70"),
```

Notes: `TRAV_FSM_GET`'s target bytes contain `movzx eax, byte [rax+0x7f0]` (the FSM offset in this build) and
`MOUNT_INPUTS`'s target starts with `cmp byte [rdi+0x7f0], 1` (skip in state None). `TRAV_GRAVITY_WISH` and
`TRAV_PM_STEP` are slots of the player's `PlayerMovement` vtable only; `P_PMROOT` (slot 11, `0x34fc838`) is the probe's,
these are disjoint. `MOUNT_SRV_DISMOUNT` is `CPlayerObj` slot 49; slot 50 (`0x3995b30`, Mount) is in its context bytes.

## 5. Runtime checks

| ID | Question | Measure | Probe | M |
|---|---|---|---|---|
| T1 | FSM path of each traversal move | `fsm=` in STATE, `trav` during the drill (1.6) | `state` (+fsm), `trav` | M2 (traversal comfort), M6 |
| T2 | TAG names of climb, hang, shimmy, vault, mantle, leap, drop, tower top | TAGS lines per drill mark | `state`, `trav` | M2, M6 |
| T3 | does the stock climb keep gravity on, is the proxy unsupported on a wall, does `pm` attach | `pc+0x60`, `vf_14`, `pm+0x1a6` in `trav` | `trav` | M6 |
| T4 | MoveParams per move (`+0x11`, `+0x14`) | `TRAV_PM_STEP` slot hook in `trav` | `trav` | M6 |
| T5 | vault and pull-up durations | mark to mark in the drill | `trav` | M2, M4 (vault blink) |
| T6 | a mod ray gets the surface; `CanWallClimb` true on climbable walls only | `surf` at three walls | `surf` | M6 (B grip), stretch (A) |
| T7 | gravity off holds Talion; graph reaction; supported lie | `grav off`, `pmmove 0 30 0` | `grav` | stretch (A, after M9) |
| T8 | head-relative steering with the head camera; frame order | `mount` lines + `fpcam on` + `move` | `mount` | M7 |
| T9 | the saddle node and the attached movement | `mount` (attach fields, node name) | `mount` | M7 |
| T10 | the hand-over-hand prototype: the hand's ray, the flag holding Talion with the FSM at 6, the pull through the collision, the tops | `CLIMB` lines (re-xr-pipeline.md 12.6) | `SOMVR_CLIMB=b` | stretch (A) |

## 6. design-vr.md markers (for the coordinator)

| Line text (design-vr.md) | Status | Answer |
|---|---|---|
| section 4 table: "**needs RE: the climb state machine, the climb surfaces data, the character proxy's gravity**; high risk of falling through or sticking in walls" | answered (static) | sections 1.2-1.3 (FSM `P+0x7f0` + data graph), 2.1 (`Surfaces/Surfaces.CanWallClimb`, `FUN_02ae5d90`, wall plant, context actions), 2.4 (gravity in `CPhysicsCharacter::vf_08`, switch `PlayerMovement::vf_06` slot `0x34fc810`, `pc+0x58` = -9.8); risk 2.5; RT T3, T6, T7 |
| section 4 table, B column: "**RT: only a "climbing now" flag** (animation state tags, re-firstperson.md 5)" | still RT, narrowed | read set 1.4 (FSM, supported, gravity, attached, TAGS); tag names RT T2 |
| section 4 end: "**Needs RE for the whole section**: the "climbing now" flag, and which player states mean vault, mantle, leap and fall (Player states `StateFalling`, `TransitionAnim` are registered in the Player ctor; the animation state names from `CAnimationMgr` cover the rest)" | answered (static) -> RT | FSM ids 8 Falling / 9 TransitionAnim / 10 Landing and their events (1.2); climb/vault/mantle/leap/drop are graph data (1.5); RT T1, T2, T5 |
| section 5: "**RT: its saddle node**; fallback: Talion's pelvis bone while mounted" | still RT, narrowed | the rider is pinned by `AttachToObject` (`FUN_020cac10`: parent `+0x1f8`, node `+0x200`), node name is data (3.4); RT T9 |
| section 3 evade: "~0.5 s, **RT: its duration**, measured in a traversal drill" | still RT | drill in 1.6 (T5) |
| section 5 "Steering: the left stick goes to the game (the beast steers relative to the camera, which is the head)" (not a marker) | confirmed | 3.3 (`CAIAnimationController::vf_29` reads the rider's camera-relative `DeltaYaw` and camera quat) |
