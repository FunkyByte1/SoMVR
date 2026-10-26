# Shadow of Mordor (Feral Linux build): player, animation, input, combat, AI - the map for a physical-combat mod

Static analysis only (2026-10-18): Ghidra project `som-linux` (read-only queries through `scripts/ghidra_query.sh`)
and the TSV indexes in `reference/index/`. Nothing was launched. `SOM=/home/bara/Projects/flat2vr/ShadowOfMordor`.
Scratch (batch files `bN.txt`, raw outputs `oN.txt`, `combat_cvars.tsv`, `iem.tsv`): `$SOM/reference/work-gameplay/`
(git-ignored). Function renames made in the project are listed in section 10.

Conventions
- Addresses are som-linux virtual addresses (non-PIE ELF, base 0x400000 = the address in the running game).
- `FUN_xxxxxxxx` = Ghidra's default name, `Class::vf_NN` = slot NN of Class's primary vtable (SomVtables labels: a slot
  number, not a meaning). `+0x2d0` = byte offset into an object. `vtbl+0x80` = the slot at byte 0x80 (= slot 16).
- Confidence: **[high]** read in the decompile/disassembly, unambiguous; **[med]** read in the code, meaning inferred
  from strings, call pattern or LithTech conventions; **[low]** inference, check at runtime first.
- Engine: Monolith Firebird (LithTech lineage). Engine objects are handles (`HOBJECT`, here a struct with
  `+0x38` = the C++ game object and `+0x48` = its class-descriptor chain), node/socket/piece indices are `uint32`,
  results are `LT_OK = 0`, transforms are `LTRigidTransform` = `{float pos[3]; float quat[4] /*x,y,z,w*/}` (28 bytes).
  Single player runs the client and the server in one process: client code holds plain pointers to server objects.
- **World units are centimetres** (the physics ray cast multiplies by `0.01f` = `DAT_03360534` before Havok; the help
  of `Combat.Value.MaxADAdjustment` says "Distance (CM)") **[high]**. Up axis is +Y (`Player.SetOrientation` builds its
  rotation with the up vector `(0,1,0)`) **[med]**.
- Calling convention: SysV x86-64. A by-value `LTVector` goes in XMM0 (x,y) + XMM1 (z); Ghidra often shows that as
  `undefined8 param_1, undefined4 param_2` and shifts the integer parameters to `param_3...`.

## 0. Summary

| Need | Where | Conf. |
|---|---|---|
| Client shell | `CGameClientShell* g_pClientShell = *(void**)0x46a9560` (set in its ctor `FUN_009e8e00`; created by `FUN_009e06b0`, 0x82630 bytes) | high |
| Local player (client `Player`) | `Player* FUN_009e3570(g_pClientShell)` (every `Player.*` console command starts with it) | high |
| Server player | `HOBJECT hPlayer = *(*(Player+0x420)+8)`; `CPlayerObj* = *(hPlayer+0x38)` | high |
| Move the player | `FUN_00a368f0(Player*, LTRigidTransform*)` = teleport (`Teleport x y z` console command) -> `CPlayerObj` vtbl+0x2a0 | high |
| Bones of any character | `IEngineModel* = *(void**)0x46a9230`: GetNode = vtbl+0x80, GetNodeTransform = vtbl+0xb8 | high (pointer), med (slots) |
| Hide head/hood mesh | `IEngineModel` vtbl+0x20 = GetPiece(name), vtbl+0x38 = SetPieceHidden (64-bit mask per model) | high |
| Melee hit test today | the attack animation's `APPLYDAMAGE` model string -> range + angle test `FUN_026e49a0`; optional per-attack colliders (rays/shape casts sampled from the animation between `BCOL`/`ECOL` keys) | high |
| Deal a combat hit | `FUN_026d4ff0(LTVector hitDir, Combat* attacker, HOBJECT target, HRECORD attack, bool)` - both game hit paths end here | high |
| Raw damage | `DamageStruct` + `FUN_02763050(DamageStruct*, HOBJECT target)` (how `Player.FragSelf` kills) | high |
| Ray cast against the world/characters | `IEnginePhysics* = *(void**)0x466c758` (= `CWBEngine::vf_03`), vtbl+0x200 -> `FUN_008de5e0` (Havok castRay) | high |
| Best input injection | interpose SDL (`SDL_GameControllerGetAxis/GetButton/GetAttached`, libSDL2 is dynamically linked) or hook `CSDLController::vf_07` (`0x2045810`), which fills an `XINPUT_GAMEPAD` | high |
| Saves | Steam remote storage through Feral's `IFeralNetRemoteStorage` (FileWrite = vf_00 `0x1c15f10`, FileRead = vf_01 `0x1c15f60`) | high |

## 1. The player

### Objects and how to find them at runtime

- `CGameClientShell` (vtable 0x34e0bd8, 33 slots). Its constructor `FUN_009e8e00` writes `DAT_046a9560 = this`
  **[high]**. `FUN_009e06b0` (`IWBEngine` vf_14 -> `DAT_0466afa0`, then `new 0x82630` + ctor) creates it.
- **Local player:** `FUN_009e3570(shell)` looks up the active entry in the player map at `shell+0x80` (key at
  `shell+0xa0`) and returns the client `Player*` or 0 **[high]**. All of `Player.GetPos`, `Player.SetOrientation`,
  `Player.FragSelf`, `Teleport`, `PlayerInput.SetSimInput` use `FUN_009e3570(DAT_046a9560)`.
- `Player` (client, not polymorphic, constructor `FUN_00a43990`, Player.cpp) owns its components **[high]**:

| Offset | Component (ctor) | Notes |
|---|---|---|
| +0x2c8 | `PlayerMovement` (`FUN_00ab6640`, 0x460 B; `BaseMovement` + `IPlayerComponent`@0x210) | locomotion, 35 vfuncs |
| +0x2d0 | `CPlayerCamera` (`FUN_00a59390`, 0x510 B) | `Player.GetPos` prints `*(float[3]*)(cam+0x44c)` as "PlayerPos" |
| +0x2d8 | `PlayerBody` (`FUN_00a54ba0`, 0x5f0 B; animation owner: `IAnimationMgrCameraOwner`, `...CharacterPhysics`, ...) | `+0x1b8` = an engine object whose rotation (quat) is at `+0x14` (Teleport keeps it) **[med]** |
| +0x2e0 | `FUN_00abfb00` object (in the `CPlayerUI` address range) | **[low]** |
| +0x2e8 | `PlayerCombat` (`FUN_00a8f190`, 0xb18 B; `ISharedCombatInputHost`@0x10) | `PlayerCombat::vf_03` returns the player's server `Combat*` **[med]** |
| +0x2f0 | `PlayerVisionMode` (wraith vision) | |
| +0x2f8 / +0x300 | `PlayerMotionBlurSettings` / `PlayerDepthOfFieldSettings` | comfort: motion blur, DoF |
| +0x308 | `FUN_00a78670` object (0xc78 B) = `PlayerCameraPresentation` (re-m1m2.md G16, 10-19) | **[med]** |
| +0x310 | `FUN_00a08880` object (0x13a0 B, `IPlayerComponent`@0x10) | **[low]** |
| +0x318 | `PlayerPhotoMode` (`FUN_00e2ba80`) | |
| +0x420 | link to the server object: `*(+0x420)+8` = `HOBJECT` of `CPlayerObj` | **[high]** |
| +0xd48 | `+0x10` = `PlayerInput` (`FUN_00a36e60`) | sim-input queue at `PlayerInput+0x20` |

- Server side: `CPlayerObj` (si `CCharacter`, vtable 0x39959a0, 117 slots) - the object the AI attacks and the
  damage code hits. `CCharacter+0x6d0` = its `Combat*` (`FUN_02699c50`), `CCharacter+0x910` = inventory
  (`FUN_02699990`; `FUN_02311e80(inv)` = equipped weapon, slot index at `inv+0x280`) **[high]**. Orcs are `NPC`
  (vmi `CCharacter`, vtable 0x3a2b3d0, 124 slots).

### Position, rotation, moving and turning

- Position: the engine object of the character (`HOBJECT`) carries the transform; read it through the engine
  (`IEngineModel`/object API, or GetNodeTransform of the root node in world space, section 2). The quickest
  read-only value is `Player.GetPos`'s source `*(float[3]*)(*(Player+0x2d0)+0x44c)` (the camera component's copy of
  the player position) **[high that it is printed as PlayerPos, med what it is]**.
- **Teleport**: `FUN_00a368f0(Player*, LTRigidTransform* xf)` checks that `hPlayer`'s class chain contains
  `DAT_04c47040` (a character class descriptor; `FUN_026d5530` tests targets against the same one) and calls `CPlayerObj` vtbl+0x2a0 (slot 84) as
  `(this, &xf->pos, &xf->rot, bool, bool)` **[high]**. The `Teleport x y z` console command (`FUN_00a36ae0`) passes
  the current rotation from `*(PlayerBody+0x1b8)+0x14`.
- `Player.SetOrientation x y z` (`FUN_00a3c820`) only converts the Euler angles (`FUN_00a4a800`, up `(0,1,0)`) and
  applies nothing in this build **[high]** - no ready-made "turn the player" call.
- Movement in normal play is camera-relative: `PlayerMovement` turns stick input into motion relative to the camera
  yaw, so smooth locomotion = inject the left stick (section 3); turning = the camera yaw (the view helper's
  `CameraYaw`/`CameraControllerInput` modifiers). Root motion of attacks (`SnapAlign`, `MeleeTranslationScaling`,
  `[COMBAT|TRANSLATIONSCALING]` `FUN_023a0210..`) also moves the player - a comfort issue once the game plays attacks.
- Player states (`PlayerStateNone/Falling/Landing/Spectating/TransitionAnim`, names "StateSupported", "StateFalling",
  "StateCombat", "StateSpectating", "TransitionAnim" registered in the Player ctor) **[high]**.

### Spectator, clip and photo mode (hints for decoupling the camera)

- `ForceSpectatorClipMode`: registered in GameClientShell.cpp (`FUN_0056ff70`), but its handler `FUN_009e1440` is an
  empty `return;` in this build **[high]** - the clip/noclip spectator was compiled out. The spectating state
  (`PlayerStateSpectating`, `OnEventPlayerSpectatorInvisibleModeEnter/Exit` delegates on `PlayerBody`,
  `SetSpectatorInvisible` script function `FUN_023fe8e0`) still exists.
- Photo mode (retail): `PlayerPhotoMode` at `Player+0x318` (vtable 0x3595980, PlayerPhotoMode.cpp), free camera
  within `PhotoMode_Distance`, `FreeCamVelocity`; `PhotoMode.ForceExit`. It is proof that the camera can be driven
  independently of the body while the body stays put. The debug camera (`DebugCameraMgr.cpp`) is data-gated.

## 2. Skeleton and animation

### The model API (`IEngineModel`)

`IEngineModel* g_pModel = *(void**)0x46a9230` - the pointer slot of `CIMgrAPIHolder<IEngineModel>` at 0x46a9260,
filled at start-up from "IEngineModel.Default" (`_INIT_269`, 0x56fefc..) **[high]**. (Every .cpp keeps its own holder
copy; this one is used by the combat colliders and the NPC script functions.) Each slot is a stub
`(this, HOBJECT obj, ...)` that gets the model of the object with `FUN_008f3c20(obj)` (= `obj+0x38`'s vtbl+0x20,
i.e. the game object's `ISimpleModel*`) and forwards to `CSimpleModel` (vtable 0x38d82c8, 178 slots, SimpleModel.cpp).
The stubs return `result ^ 1` (0 = `LT_OK`), 0x3c on a null object. Full stub -> CSimpleModel map:
`reference/work-gameplay/iem.tsv` (slot, signature, inner offset).

| IEngineModel slot (byte) | CSimpleModel | Meaning | Conf. |
|---|---|---|---|
| 16 (+0x80) | vf_62 | `GetNode(obj, const char* name, uint32* node)` (hash lookup in the skeleton) | high |
| 17 (+0x88) | vf_63 | `GetNodeParent(obj, node)` -> parent or -1 | med |
| 14 (+0x70) | vf_58 | `GetNodeName(obj, node, char* buf, int len)` | med |
| 23 (+0xb8) | vf_68 | **`GetNodeTransform(obj, node, LTRigidTransform* out, bool world)`**: node or socket index; pose from the animation node's last evaluated pose (`FUN_0238fe60`), times the object transform when `world` | high (code), med (name) |
| 11 (+0x58) | vf_55 | `GetSocket(obj, const char* name, uint32* socket)` (strcasecmp over sockets) | med |
| 4 (+0x20) | vf_48 | `GetPiece(obj, const char* name, uint32* piece)` (used by `NPC.HidePiece`) | high |
| 7 (+0x38) | vf_50 | **`SetPieceHidden(obj, piece, bool)`**: bit `piece` (< 64) of the mask at `model+0x6c`, then marks the render node dirty | high |
| 38/39 (+0x130/+0x138) | vf_81/82 -> `FUN_0238d4e0`/`FUN_0238d560` | register a per-node entry (node, id, flags, owner, size) in the animation node's table at `+0x3b0`; used by the procedural body for Pelvis/Torso_Base/Spine4_joint (`FUN_02240f40`) | med (mechanism), low (exact semantics) |
| 51 (+0x198) | vf_94 | animation key lookup `(obj, tracker, anim, "BCOL"/"ECOL", ...)` -> key times (used by the shape-cast collider) | med |
| 54-96 (`undefined1` 2nd arg) | vf_96..141 | animation-tracker functions (tracker id byte): current anim, time, rate, ... | med |

- Bone naming: `L_`/`R_` prefixes and `_joint` suffix; names seen in code: `Pelvis`, `Torso_Base`, `Spine4_joint`,
  `Neck`, `L_Hand`, `R_Hand`, `L_Armu`, `R_Armu` (ragdoll stretching `FUN_020edb30`); sockets `SocketHead`
  (`FUN_00dc2720`), `SocketAttach`, `TargetSocket`; `CombatTargetNode` defaults to "LeftHand" (client targeting
  `FUN_00a9a140`) **[high for the strings]**. The full node lists are in the model data; enumerate them at runtime with
  GetNodeName over 0..n.
- To read an orc's head/hands/weapon: `GetNode(hOrc, "...")` once, then `GetNodeTransform(hOrc, node, &xf, true)`
  each frame. Weapons are separate attachments (attachment objects with their own models: `NodeAttach`,
  `ModelSocketOverride`, `HandBone`) - read the weapon object's model, or the hand socket.

### Animation system and frame timing

- Own engine: `CEngineAnimationImpl` (`IEngineAnimation`, 56 slots), `CEngineAnimationModel/Blend/Job/Tracker/Bank/
  Pose`, `CModelSkeleton`; animation jobs (`CEngineAnimationJob`, `CEngineModelAnimationJobEngineImpl` =
  `IAnimationEngine`) are evaluated as jobs. The game layer is `CAnimationMgr` (state machine "Amtrak", ~90
  `CAnimationMgr::*` modifiers incl. `IK`, `IKOutput`, `EnableFootIK`, `AlignHands`, `SyncBone`, `RayCast`,
  `CombatColliderSetModifier`, `InputCommandSet`, camera modifiers) **[high, names]**. Havok Animation (`hka*`) is
  linked (hkaSkeleton/mapper/`hkaRagdollInstance`) but the main pose pipeline is Firebird's own **[med]**.
- Procedural body (`ProceduralBody::Arm`, `LegPair`, `Pendulum`, `LeanAndTwist`, data `Character.ProceduralBody`,
  `ProceduralBody.IK.HandGuardDist`, `Character.Models.IKChain` with `BeginIKChain`/`EndIKChain`) runs after the
  animation pose and registers node entries through IEngineModel slot 38 **[med]**. That is the engine's own
  post-animation pose override, so a VR arm IK can either (a) register its own node entries the same way, or
  (b) hook the procedural-body arm update, or (c) write the pose buffer read by `FUN_0238fe60`
  (`animNode+0x58 -> +0x40`, 0x20 bytes per node) after evaluation and before skinning **[low, needs runtime work]**.
- When bones are final: `GetNodeTransform` reads the animation node's last evaluated pose on demand, so a value read
  after the frame's animation update and before rendering is the pose that gets drawn **[med]**. Frame entry points:
  `CGameClientShell::vf_02` (`0x9eabd0`, 4.3 KB, the client per-frame update; it also drives `RenderCinematic`)
  and `CGameServerShell::vf_10` (`0x2777c50`, the server update: AI, objects, managers) **[med]**. The exact order
  (server update -> animation jobs -> client update -> render) is not proven statically; log timestamps from hooks
  in experiment (a) to settle it.
- First-person body: hide the player's head/hood/hair pieces with `SetPieceHidden` (piece names from the player
  model; `HeadModelSwap` `FUN_02627e90` and `AttachmentHidePieces` `FUN_024e0e30` show the game itself swaps heads and
  hides attachment pieces) **[high mechanism, piece names unknown]**. Shadows will still be cast by the full mesh
  unless the render side splits it (for the view/render map).

## 3. Input

### Layers (bottom to top)

1. SDL 2.0.8 (dynamic `libSDL2-2.0.8.so`). `CSDLController::vf_07` (`0x2045810`, Feral) reads
   `SDL_GameControllerGetButton` 0..14 and `SDL_GameControllerGetAxis` 0..5 and fills an `XINPUT_GAMEPAD`
   (`ushort wButtons; BYTE lt, rt; SHORT lx, ly, rx, ry`) - the only caller of both SDL functions **[high]**.
   Keyboard: `SDL_GetKeyboardState` in `FUN_02046130` / `FUN_02040100`; events `SDL_PeepEvents` in `FUN_0203c5a0`.
2. Feral's XInput emulation (`XInputGetState/SetState/GetCapabilities/Enable` registered in `_INIT_1072/1073`;
   `xinput1_3.dll` / `xinput9_1_0.dll` names in `FUN_01b00320`) -> the game's `Win32GamePad` (`IWBInputDevice`,
   vtable 0x34c73f8). Its state is double-buffered (2 x 0x28 B, current index at `dev+0x58`): `+0x0c` wButtons
   (XInput bits: 0x1000 A, 0x2000 B, 0x4000 X, 0x8000 Y, 0x100 LB, 0x200 RB, 0x10 START, 0x20 BACK, 0x40 L3, 0x80 R3,
   1/2/4/8 d-pad), `+0x18/+0x1c` left stick, `+0x20/+0x24` right stick, `+0x28/+0x2c` triggers (floats). Device input
   ids 2000-2013 = buttons (mask table `DAT_045af5a0`), 2014-2019 = axes (`Win32GamePad::vf_02/04/05`) **[high]**.
   Also `Win32Keyboard`, `Win32Mouse`; engine side `CWBInput` (`IEngineInput`, 33 slots).
3. Bindings -> game commands: data `Profile.CommandSet`, `Profile.BindingSet`, `Profile.UIActionIDBindings(Keyboard)`;
   the command-name table is built by `FUN_025e1450` (names: `CombatPrimary01..04`, `CombatSecondary01..04`,
   `CombatTertiary01..06`, `CombatPrimaryAnalogX/Y`, `CombatSecondaryAnalogX/Y`, `CombatKBM01..20`,
   `SpecialAttack1..4`, `SpecialAttackModifier`, `CombatStruggle01..10`, `CombatHidden01..10`, `MNT_*`,
   `EquipSword1..4`, `VisionModeToggle`, `Friend*`, ...). Which button is attack/counter/stun/evade/drain/brand/aim is
   decided by data (`Combat.Input`, `Combat.InputString`, `InputDodgeCommandID(_KBM)`), not code **[high]**.
4. `CPlayerUserInputListener` (vtable 0x34ef7d0) -> `PlayerCombat` (`FUN_00a811e0`), player input states;
   `CAnimationMgr::InputCommandSet` = per-animation-state command sets **[med]**.
5. Combat input -> attack: `PlayerCombat` (`ISharedCombatInputHost`) scores the attack pool for the pressed command
   (section 4) and starts the attack on the server `Combat` (`FUN_026eb380`) **[med]**.

### Where to inject and suppress

- **Best: a virtual gamepad below the game.** Interpose `SDL_GameControllerGetAttached/GetButton/GetAxis` in our
  injected .so (they are PLT imports: `0x4142f0`, `0x412890`, `0x411fe0`), or hook `CSDLController::vf_07` and edit the
  `XINPUT_GAMEPAD` it returns. Everything above (bindings, menus, prompts, struggles, the KBM/pad UI switch) keeps
  working, analog values included, and suppression is trivial (never set the attack button) **[high]**.
- In-game simulated commands: `PlayerInput.SetSimInput` (`FUN_00aa9bb0` -> `FUN_00aa9a90`) queues
  `{command id, arg}` pairs in `PlayerInput+0x20` (`FUN_00aa97b0` pushes a command, `FUN_00aa9920` a wait
  `{0x15d, frames}`), run "once per frame" - digital, debug, good for scripted tests only **[high]**.
- Per-command suppression at the game level (e.g. keep the pad's attack button for menus but drop
  `CombatPrimary01` in fights) needs the command-state lookup in PlayerInput/PlayerCombat - not mapped yet (next steps).

## 4. Combat as it is

### Objects

- `Combat` (si `GameBase`, vtable 0x396bb40, Combat.cpp): the per-character combat object. `CCharacter+0x6d0` =
  `Combat*`; `FUN_0280fba0(HOBJECT)` returns the combat host of any object (class-chain check; `CombatProp` keeps
  it at `+0x2e8`) **[high]**. Fields: `+0x70` = owner `CCharacter*`, `+0x4a0` = owner `HOBJECT`, `+0x4d0` = current
  attack record, `+0x55c` = variation (1-based), `FUN_026d4510(combat)` = current attack record incl. variation
  **[high]**; `+0x268` combat-state object, flags `+0x27a/+0x27f/+0x287` **[low]**.
- Records come from the game database (`HRECORD`: `rec+8` = attribute table, 16 B per attribute, count at `+0xc`,
  value index at `+0xe`; `rec+0x10` = values). Attribute descriptors are cached globals (`DAT_04ba23c0` = the
  `Combat.Attack` schema: `FUN_00a91300(attack)` = its damage record (+0x38 attribute), `FUN_00a91380` another;
  `DAT_04ba7280` = `Combat.Damage` schema with range/angle attributes) **[med]**.
- `PlayerCombat` (client, `Player+0x2e8`) and `CPlayerCombatWeaponMode` (bow/wraith modes); `CombatProp`
  (`ICombatHostGeneric`, breakables/props); `CCombatSyncAction` (paired animations: executions, drains, brands,
  counters).

### The hit window and the animation keys

`FUN_022667f0` (shared, `[COMBAT|MODEL]` logging under `Combat.Debug.Model` = `*(bool*)0x4c44598`) parses the
attack animation's model-string keys into a per-attack state struct **[high]**:

| Key | Effect (byte in the struct) |
|---|---|
| `BHITWIN` / `EHITWIN` | hit window open/close (`+0x33`) |
| `BCANWIN` / `ECANWIN` | cancel window (`+0x31`) |
| `BPW` / `EPW` | **parry window** (`+0x39`, "Start/End Parry Window") - the counter cue for enemy attacks |
| `BDW` / `EDW` | dodge window (`+0x34`) |
| `EAL`, `AEE`, `BUFFTRANS`, `BT`/`ET` | auto links (`+0x37`), early exit, buffered transition, combo links (`+0x38`) |

The server-side key handler `FUN_026f9070` (16.6 KB; called from `CCharacter::vf_14` and `CombatProp::vf_14`)
handles the action keys: `APPLYDAMAGE`, `ADRIDER`, `ADATTACHED`, `APPLYBUFF`, `REMOVEBUFF`, `SEVER`,
`SEVERATTACHED`, `SEVERSELF`, `BRANDNOFX`, `HITBRANDED`, `THREATMOD`, `AI_BSLOWMO/AI_ESLOWMO`, `RESETSTANCE`,
`RHYTHM_WIN`, `DROPITEM`, `COLLISION_ON/OFF`, `DETACH`, `ADDITEM`, ... **[high]**.

### How a melee hit is detected today (two paths, one sink)

1. **`APPLYDAMAGE` key (the normal case).** `FUN_026f9070` -> `FUN_026e5ae0` -> `FUN_026e49a0(Combat* self, HRECORD attack, HOBJECT target)`:
   reads the damage record's `Range (min,max)` and `MaxAngle`, adds up to `Combat.Value.MaxADAdjustment`
   (`*(float*)0x4c43f98`, cm) to get the effective distance, computes distance (`FUN_02296aa0`) and the angle between
   the attacker's facing and the target, logs "Hit/Miss - Distance = .., Range = .., EffDist = .., Angle = ..,
   MaxAngle = .." (`Combat.Debug.ApplyDamage` = `*(bool*)0x4c44f18`), then calls `FUN_026d4ff0` **[high]**. The
   target is the attack's current target (`FUN_026c5990` may retarget to a closer one) - **no geometry: the animation
   decides when, a distance/angle cone decides whether**.
2. **Combat colliders** (per attack, data `Combat.Collider/ColliderSet`, enabled by `CombatColliderSetModifier`).
   `CCombatColliderShapecast::vf_03` (`0x270f600`, "Enable Collider '%s'") samples the attack animation between
   the `BCOL` and `ECOL` keys (IEngineModel vtbl+0x198/+0x200/+0x248) into 0x28-byte transform samples, i.e. a
   precomputed sweep. `CCombatColliderRaycast::vf_02` (`0x2712c80`) casts rays between consecutive chain points with
   `IEnginePhysics` vtbl+0x200 and, on a hit that passes `FUN_027107e0`, calls `FUN_026d5530(...)` -> `FUN_026d4ff0`
   **[high]**. Also `CCombatColliderAOE`, `CCombatColliderWMShapecast` (world models), `ChainCollision` debug lines.

### The damage pipeline

```
FUN_026d4ff0(LTVector hitDir /*XMM0,XMM1*/, Combat* attacker /*RDI*/, HOBJECT target /*RSI*/,
             HRECORD attack /*RDX; 0 = attacker's current attack*/, bool flag /*ECX*/)      [high: disassembly]
  hitInfo = {hitDir, ..., +0x20 {0.0f,1.0f}}  (FUN_02819890 init)
  weapon  = FUN_02311e80(FUN_02699990(attacker->owner))     (equipped weapon)
  host    = FUN_0280fba0(target)
    character target  -> FUN_0281a200(attackerHOBJECT, targetChar, &hitInfo, attack, weapon, ..., flag, ...)
    generic host/prop -> host->vtbl+0x50(...)
    else              -> FUN_028125f0(...)
  FUN_026c06b0(attacker, attack, &hitDir)                   (attacker-side bookkeeping)
```

- `FUN_0281a200` (10 KB, the server "apply an attack's damage"): alignment check ("Attacker's Alignment not allowed to
  damage target's alignment."), "Target can not be damaged during attack %s", "Attack %s cannot hit CharacterType
  %s", `Combat.AlwaysKill` (`*(bool*)0x4c44418`, read here and in `FUN_0281c960`), builds the damage context
  (`FUN_028194d0`) and a `DamageStruct` (`FUN_02814310`), picks the recoil/hit reaction (`FUN_02810af0`,
  `FUN_02810e80`, `FUN_02811140` "could not pick valid recoil for attack", `FUN_0280fe60` "Attack not allowed to
  cause a recoil against current attack"), applies buffs/DOTs (`FUN_026d90a0`), then `FUN_028198c0` -> 
  `FUN_026f2580(targetCombat, &result)` (damage numbers "%d") and `FUN_026f8a60` **[high for the strings/calls,
  med for the roles]**. Death: `FUN_026f61f0` (6 KB, tags the "PlayerKiller"; also called by `Player.FragSelf`)
  **[med]**; pre-death `ApplyCombatStatePreDeath` (`FUN_023f8b20`), AI-side "Entered PreDeath state" (`FUN_028ccc90`).
- `FUN_0281c960(attacker, target, x)` = the same without an attack instance (used by `FUN_026e5ae0` and sync
  actions) **[med]**.
- **Raw damage, no attack record** (`Player.FragSelf`, `FUN_00a337f0`) **[high]**: a stack `DamageStruct`
  (`vtable 0x34ef650` at +0, `WBObjRefNotifier` at +8, damager `WBObjRef` list +0x10/+0x18, damager object +0x20,
  `WBObjRef` +0x28 list +0x30/+0x38, float amount +0x48, `{0.0, 1.0}` +0x58, damage type +0x60
  (`FUN_021583f0("Explode")` = type by name), vec4 +0x70 = `DAT_033447b0`, zeros to +0x94), then
  `FUN_02763050(&ds, hTarget)` = `DoDamage`: class-chain check (`DAT_046b5860`), then `FUN_00c77890(target->obj,
  ds.damager, &ds)` (GameBase damage handler). Damage types are database records (`Damage.Type`).
- Executions/finishers, drains, brands, counters, struggles: paired animations (`CCombatSyncAction`,
  `Modifier.PlaySyncAction`, `[COMBAT|SYNC]` `FUN_02252bd0..`, struggles `CStruggle*` `[COMBAT|STRUGGLE]`
  `FUN_00e16ef0..`) - data records, chosen by the same attack pools **[high names]**.
- Severing/decapitation: `SEVER` key, `Combat.Debug.AlwaysSever` (`*(bool*)0x4c449d8`), `HeadStump`
  (`FUN_02629750`), `KilledByDecapitation` (permanent death for the Nemesis system) **[high names]**.

### Attack selection ("attacks are picked by scoring")

- `FUN_02261c10` = attack-pool selection (shared by player and AI): pool validation ("Pool validation failed for %s -
  EnergyRange / HitCounterRange / RequiredBuff / Active Item ..."), then "Attack Passed %s - Score - %d (Base=%d)
  (FrequencyBonus=%s)" or "(Explicit)"; reads `Combat.DisableEnergyCost` (`*(bool*)0x4c44398`) **[high]**.
- `FUN_02258db0` = attack predicates: ~40 "Rejected Attack: %s - Direction (To Input / Attacker to Target / Target to
  Attacker) / Parry Attempt / TargetBlocking / Recoil Counter / ..." **[high]**.
- `FUN_026eb380` = `Combat` start attack ("'%s' Start Attack: '%s', Node: '%s', Target: '%s'", duplicate/recoil
  rejections, SetMount), `FUN_026ee800` = stop attack; both log under `Combat.Debug.Attack` (`*(bool*)0x4c44ad8`)
  **[high]**.
- Counters/parries: an enemy attack's parry window (`BPW`..`EPW`) makes it counterable; the client evaluates
  `ParryWindowOpen`/`CanCounter` predicates (`FUN_00debb30` "EvaluteTarget:ParryWindowOpen", data
  `FUN_0250b410`/`FUN_0251ba30`) and shows the counter prompt (`[COMBAT|COUNTERPROMPT]` `Combat::vf_13`,
  `FUN_026f9070`); event structs `CombatAttackerParryWindowArgs`, `CombatStartRecoilArgs`, `CombatAttackHitArgs`
  (`IScriptEventStruct`) **[high names, med flow]**. Blocks: `[COMBAT|BLOCK]` `FUN_022642e0/FUN_022647f0`,
  `RecoilTypeHit/Block/Parry` attributes, AI `CAIActionCounterMeleeBlock`.

### Enemy attack timing and telegraphs (for VR warnings and real parries)

- An orc's attack exists as (1) the AI's decision (`FUN_028d0ef0` "received OnStartAttack, Target = %s"),
  (2) `Combat` start attack `FUN_026eb380`, (3) the parry window keys in its animation (`BPW` .. `EPW`, flag byte
  `+0x39`), (4) the `APPLYDAMAGE` key = the moment of impact. (1)-(3) come before (4): that interval is the
  telegraph the game already uses for its counter prompt. Read it per orc each frame, or hook `FUN_026eb380` and
  `FUN_022667f0` **[med]**.
- Real parries in VR: hook `FUN_026e49a0` (or `FUN_0281a200` when `target == player`) and cancel the hit when our
  blade blocks the orc's weapon at that moment, then give the orc the game's recoil (an attack-record recoil through
  the same functions as above) - details are next steps **[med plan]**.

### Combat console variables (all from `FUN_0062a4b0`, CombatVariablesInclude.h)

Object at A, name `A+8`, help `A+0x10`, **value at `A+0x38`**. Writing the value directly works from injected code
(no console needed). Full table (77 rows): `reference/work-gameplay/combat_cvars.tsv`. The ones that matter:

| Variable | Value address | Readers |
|---|---|---|
| `Combat.AlwaysKill` (bool) | `0x4c44418` | `FUN_0281a200` (+0x190, +0x4b1), `FUN_0281c960` |
| `Combat.DisableEnergyCost` / `DisableChargeCost` | `0x4c44398` / `0x4c44358` | `FUN_02261c10`, `FUN_02258db0`, energy `FUN_0271e430..FUN_02720b70`, `FUN_00e191c0` |
| `Combat.Lock.Energy` / `.Value` (float) | `0x4c44258` / `0x4c43f58` | energy component `FUN_0271d8a0..FUN_0271e0d0` |
| `Combat.Lock.HitCounter` / `.Value` (uint) | `0x4c44218` / `0x4c43ed8` | |
| `Combat.Enable.SlowMo` | `0x4c44318` | `FUN_02748e20`, `FUN_02748ee0`, `FUN_02749020` (slow-mo manager) |
| `Combat.EnableCameraOverride` | `0x4c44298` | |
| `Combat.Value.LogFileName` (string) | `0x4c45158` | `FUN_02751900` (the combat log file; `FUN_02751a50` = the logger) |
| `Combat.Value.MaxADAdjustment` (float, cm) | `0x4c43f98` | `FUN_026e49a0` |
| `Combat.Debug.ApplyDamage / Attack / Model / Collider / Pool / RejectedAttack / Stats / Block / Predeath / AttackerSelection / AIMgr` | `0x4c44f18 / 0x4c44ad8 / 0x4c44598 / 0x4c44bd8 / 0x4c44558 / 0x4c44e18 / 0x4c44cd8 / 0x4c44658 / 0x4c44498 / 0x4c44458 / 0x4c444d8` | the `[COMBAT|...]` loggers |
| `Combat.Debug.AlwaysCrit / AlwaysSever`, `Combat.AlwaysApplyState`, `Combat.AlwaysDoubleAttack` | `0x4c44a98 / 0x4c449d8 / 0x4c443d8 / 0x4c43fd8` | |

- `Difficulty.SetRelativeDifficulty` (`_INIT_1231`, handler thunk `FUN_02114990` -> `FUN_021149a0` via the console
  dispatcher `FUN_02aeea40`); `FUN_02114ec0` recomputes "CurrentDifficulty = .., Relative = .." and logs
  "AttackDelayScalar set to %0.4f" / "DoubleAttackChance set to %0.4f" (`Difficulty.Debug` = `*(bool*)0x4c43e58`;
  "(PlayerPreDeath) RelativeDifficulty" in `FUN_021195b0` = dynamic difficulty after near-deaths) **[high]**.
- `DBDiff.Apply` / `DBDiff.Save` (`_INIT_1230`, DatabaseDiff.cpp; handler thunks `FUN_02100a00` / `FUN_02100a10`):
  patch the game database at runtime ("Don't try this after sign-in.") **[high]** - the data route to change attack
  records (ranges, angles, recoils, cooldowns) without code.
- Console commands: registered with `FUN_0072ebc0`; handlers run through `FUN_02aeea40` (thunks `MOV EDI,handler;
  JMP 0x2aeea40`). Combat.cpp commands: `Combat.AwardPower` (`FUN_026bf6b0`), `Combat.TargetingDebugEnable`.

## 5. Physics

- Havok 2014.1 (`hkp*`) wrapped by Firebird: `CPhysicsRigidBody` (55 slots), `CPhysicsShape`, `CPhysicsPhantom`,
  `CPhysicsContainerShape` (`hkpCachingShapePhantom` = shape casts), `CPhysicsContainerAABB` (`hkpAabbPhantom`),
  `CPhysicsCharacter` (character proxy), `CCollisionGroupMgr` (`hkpGroupFilter`), `CPhysicsCollisionNotifier`
  (contact listener), `CRayCastBroadPhaseHandle`, constraints, cloth (`hcl*`) **[high names]**.
- **`IEnginePhysics* = *(void**)0x466c758`** (holder `CIMgrAPIHolder<IEnginePhysics>` 0x466c740, `_INIT_156`; also
  returned by `CWBEngine::vf_03`, `IWBEngine* = *(void**)0x466afa8`) **[high]**. Its slots are 15-20 byte forwards
  into the implementation (`JMP`):
  - vtbl+0x200 (slot 64) -> **`FUN_008de5e0(world, RayQuery* q, RayResult* r)`** = ray cast: `q` = `{from[3], to[3]}`
    in cm (times 0.01 -> Havok metres), `+0x18` uint flags (bit 1, 2, 4, 0x30 select modes), `+0x1c` collision
    filter, `+0x20` filter callback (the collider passes `FUN_0270c4c0`), `+0x28` 0, `+0x30` user data (the
    collider); `hkpWorld` read lock/unlock (`FUN_0174edf0`/`FUN_0174ee00`) around the cast (`FUN_0174d7d0`). The
    combat ray collider uses `flags = 0x58`, filter `0xfffffffd`; in the result `r+0x00` is a vec3 (hit point or
    normal; the collider passes it on as the hit vector) and `r+0x20` the hit object **[high code, med field names]**.
  - slots 65-72 forward to `FUN_008deb80`, `FUN_008df410`, `FUN_008df480` (similar query, `(world, id, ..., float*)`),
    `FUN_008df940`, ... (other queries/settings) **[low]**; slots 5-11 and 24-36 create shapes (sphere, box,
    capsule, ...: `CPhysicsShape` refcounting) **[med]**.
- So yes, injected code can query the game's own physics world (rays, and shape casts through phantoms) against
  characters and the world. Thread safety: the cast takes the hkpWorld read lock itself; call it from the game
  thread that runs combat (hook context) to be safe.
- Ragdolls: `hkaRagdollInstance`, `ConstraintRagdoll` (0x39d7700), `CConstraintRagdollPlugin`,
  `Combat.RagdollReaction`, ragdoll arm stretching `FUN_020edb30` (L/R_Hand, L/R_Armu). Physics runs at its own rate
  (`Physics.Client.PhysicsUpdateRate`, `MaxSteps`), `Net.PhysicsPause` **[high names]**.
- Our own physics (Jolt) can still run in parallel: feed it enemy bone capsules from GetNodeTransform each frame
  and use the game's ray cast only for world geometry (walls, so a blade does not pass through stone).

## 6. AI

- 362 `CAI*` classes: goals/actions/states (`CAIStateCombat` 0x365fa70, `CAIServiceCombat`,
  `CAITargetSelectCombatThreat`, `CAIGoalCounterMelee`, `CAIActionCounterMeleeBlock/Dodge`, `CAIActionCombatRecoil`,
  `CAIGoalCombatRecoil`, `CAIActionAttackPreemptive`, `CAIActionCombatTaunt`), morale `AIMoraleMgr`
  (`AI.Morale.*`), retreat `AIRetreatMgr`, alarm ("AlarmLevelChanger"), spawning `AISpawnMgr` **[high names]**.
- **Who attacks when - the fight circle** (a global AI combat manager, instance `DAT_0463e290` **[med]**, logging
  `Combat.Debug.AIMgr` / `.AttackerSelection`):
  - `FUN_028cd7a0` scores each candidate: rejects (not AI, Can Be Attacker = FALSE, "Only Attacker is set, and it's not
    us", too far, ring Inner/Middle/Outer/Special/None not allowed, target not visible, no path, no LOS, in a combat
    state, performing an attack/sync action, AlarmLevelChanger), else "Accepted - Total = ..; Distance, Priority,
    **Camera**, LastAttacker, StateScore, BuffScore" **[high]**. The Camera term means on-screen orcs are preferred:
    with an HMD the camera is the head, so this still works but feeds on head yaw.
  - `FUN_028cd4b0` "Can Attack: TRUE, score = .., %d attacks, at time .." (grants an attack) **[med]**;
    `FUN_028ccc90` lockout timer ("Lockout Timer set for %0.2f seconds") and pre-death **[high]**;
    `FUN_028d50c0` `ShouldCancelAttack` (13 reasons) **[high]**; `FUN_028d0ef0` `OnStartAttack` **[high]**;
    `FUN_028d4330` target switch; `FUN_028d7030` debug drawing.
- Delaying/telegraphing attacks: difficulty's `AttackDelayScalar` and `DoubleAttackChance` (`FUN_02114ec0`, data
  `AttackDelayScalar`, `DifficultyDoubleAttackChance`), the lockout timer, and the one-attacker lock. For VR:
  raise the delay (relative difficulty or `DBDiff`), or veto in `FUN_028cd7a0`/`FUN_028cd4b0` until the player
  has seen the warning **[med]**.
- Nemesis: `FactionMgr`, `SharedFactionMember` (death bookkeeping `PlayerKills`, `LastDeathType`,
  `LastDeathPlayerKiller` in `FUN_022e13b0`/`FUN_022cdd90`, accessors `FUN_022dbb70`/`FUN_022dbbc0`), sim events
  (`KilledByPlayerEvent`, `KilledByAIEvents`, `FactionMemberSevered`, `KilledByUnknownSource` in `FUN_024c9e60`),
  `OnDeathSimEvents`, `CCharacter.OnDeath` **[high names, med flow]**. A kill counts for the Nemesis system when it
  goes through the game's damage/death path with the player as the damager (the death handler `FUN_026f61f0` tags
  "PlayerKiller"); a decapitation/sever makes the death permanent. So: always deal damage through
  `FUN_026d4ff0`/`FUN_0281a200` (or a `DamageStruct` whose damager is the player), never by writing health.
  Captain identity (character -> faction member) is not mapped yet (next steps).

## 7. Saves

- `GameData.sav` is the profile: `CWin32_UserProfile::vf_07`/`vf_08` (`0xc58ca0`, the same function in two slots)
  sets the file name "GameData.sav" and the subtitle "SaveLoad_Profile_SubTitle"; chunks `SaveSlotInfo`,
  `GameDataChunk`, `PlayerDataChunk`, `FactionMgrChunk`, ... (`IProfileChunk`); `CProfileMgr`, `CBaseUserProfile`
  (24 slots), `CAutosaveMgr` **[high]**.
- In the Steam build the file lives in Steam remote storage (NOTES 2026-10-17: FeralNet -> SteamClient017 /
  STEAMREMOTESTORAGE_INTERFACE_VERSION013). Feral implements `ISteamRemoteStorage` itself:
  `IStubRemoteStorage` (vtable 0x45099a8) <- `IFeralNetRemoteStorage` (vtable 0x455cea8, 57 slots). Slots follow
  ISteamRemoteStorage v013 order **[high for 0/1, med for the rest]**:

| Slot | Function | Steam method |
|---|---|---|
| 0 | `0x1c15f10` | `FileWrite(name, data, size)` -> if `+0x58` (FeralNet storage on): `FUN_01c2f6e0(FUN_01c15900(name, 1), data, size)`, else `FUN_01c27060(+0x18)` (real Steam) |
| 1 | `0x1c15f60` | `FileRead(name, buf, size)` -> `FUN_01c2f670(FUN_01c15900(name, 0), buf, size, &read)` or `FUN_01c271c0` |
| 6 | `0x1c15fc0` | `FileDelete` |
| 13 / 15 / 16 | `0x1c15ff0` / `0x1c16020` / `0x1c16080` | `FileExists` / `GetFileSize` / `GetFileTimestamp` |
| 18 / 19 / 20 | `0x1c160e0` / `0x1c159e0` / `0x1c15cf0` | `GetFileCount` / `GetFileNameAndSize` / `GetQuota` |
| 2-5, 8-12 | stubs | async/stream variants unused |

- **Test-mode redirect (plan):** in the injected .so, replace slots 0, 1, 6, 13, 15, 16, 18, 19 of
  `IFeralNetRemoteStorage`'s vtable (0x455cea8 is in `.data.rel.ro` 0x44e0b40-0x45a9758, read-only after start: `mprotect` then write) with functions that read/write
  `$SOM/testsaves/<name>` (a copy of the real save made once). The game then never reaches Steam for saves; Steam
  Auto-Cloud still watches `userdata/<id>/241930/remote/`, which stays untouched. Verify with the existing
  before/after hash check of GameData.sav. Also keep `ClearSavesOnLaunch`, `GPDClear`, `Player.Save` out of tests
  **[med: the hook is straightforward, but confirm at runtime that every profile/save access goes through this
  vtable and none through `vf_55`/the FeralNet cloud path]**. Lower alternative: hook the VFS mapping of
  `${DozeSteamStorage}` (`CFeralGame::vf_53` `0x65cd10`).

## 8. Hook plan

Common ground for all three: an injected .so (LD_PRELOAD, the game is a non-PIE ELF, addresses fixed; check the
BuildID 87ec5d6f... at start and refuse on mismatch), one per-frame hook on the game thread
(`CGameClientShell::vf_02` entry/exit, or the server update `CGameServerShell::vf_10`), all addresses below.
The anti-debug (`TracerPid`/PTRACE_TRACEME) matters for debuggers, not for LD_PRELOAD.

### (a) Read-only experiment: log while the user plays normally

1. Resolve each frame: `shell = *(void**)0x46a9560`; `player = FUN_009e3570(shell)` (null in menus/loads);
   `hPlayer = *(*(player+0x420)+8)`; `model = *(void**)0x46a9230`.
2. Player pose: GetNode(hPlayer, "Pelvis"/head/hands) once, then GetNodeTransform(world=true) per frame; also log
   `*(float[3]*)(*(player+0x2d0)+0x44c)` to compare with `Player.GetPos`.
3. Nearest orc: candidates from the damage events (step 4) and from `FUN_028d0ef0` (`OnStartAttack`: attacker,
   target); for each, GetNodeTransform of head/hands/weapon socket. A full enemy list (object manager iteration)
   is a next step.
4. Damage events: inline-hook (trampoline) `FUN_026d4ff0` and `FUN_02763050`: log attacker (`*(Combat+0x4a0)`),
   target, attack record name (`FUN_025aa530(record)` = record name, as the loggers use), hit direction, frame time.
   Hook `FUN_026e49a0` to log hit/miss with distance/angle (or simply set `Combat.Debug.ApplyDamage`,
   `.Attack`, `.Model`, `.Collider`, `.Stats` = 1 and `Combat.Value.LogFileName` to a file in the run folder: zero
   code paths changed).
5. Telegraph timing: log `FUN_026eb380` (start attack), `FUN_022667f0` parry-window flags and the `APPLYDAMAGE`
   key time per orc -> the warning lead time the VR mod can use.
- Signatures: `long FUN_009e3570(void* shell)`; `int GetNode(IEngineModel*, HOBJECT, const char*, uint32*)` at
  vtbl+0x80; `int GetNodeTransform(IEngineModel*, HOBJECT, uint32, LTRigidTransform*, bool)` at vtbl+0xb8;
  `void FUN_026d4ff0(LTVector, Combat*, HOBJECT, HRECORD, bool)`; `void FUN_02763050(DamageStruct*, HOBJECT)`.
- Risks: wrong slot meaning (validate by printing the head node position against the camera); thread (call the model
  API only inside the frame hook); the save (none of this writes, but run with the save redirect from section 7
  anyway); the hooks' trampolines must copy whole instructions (use a length disassembler).

### (b) Apply damage to an orc from our own code

1. Preferred: `FUN_026d4ff0(hitDir, playerCombat, hOrc, attackRecord, false)` with `playerCombat =
   FUN_02699c50(CPlayerObj*)` (= `CCharacter+0x6d0`) and an attack record taken from the player's own attack set
   (log the records the game uses in (a); choose light/heavy/finisher by blade speed). This runs the game's own range-
   free path: alignment checks, damage, recoil/hit reaction, buffs, XP, death, sever, Nemesis bookkeeping.
2. If the attack-record path insists on an active attack (`attack == 0` means "current attack"), drive it one level
   lower: `FUN_0281a200(hPlayer, orcChar, &hitInfo, attackRecord, weapon, ...)` (hitInfo initialised with
   `FUN_02819890`: zeroes 0x31 bytes, `{0.0f, 1.0f}` at +0x20; `hitDir` at +0; reserve 0x40 bytes).
3. Fallback for tests: `DamageStruct` + `FUN_02763050` with damager = `hPlayer` (no recoil logic, raw health).
- Risks: `Combat` state assumptions (some code may expect the player to be in an attack: "Target can not be damaged
  during attack", hit counter/combo logic, `+0x287` flag cleared after a hit); calling from the wrong thread/time
  (do it inside the frame hook, after physics); recoils that move/rotate the orc toward a player who is not where the
  game thinks (snap-align uses the player object, which in VR follows the head); executions (sync actions) need the
  player body - decide per attack type; difficulty's dynamic adjustment still applies.

### (c) Suppress the game's own melee for the player, keep the AI

1. Do not send the attack/stun/execution inputs at all: the virtual gamepad (section 3) never presses the combat
   buttons in VR combat (keep counter/evade/drain/brand mapped to explicit VR gestures or buttons if wanted).
2. Belt and braces: hook `FUN_026eb380` (`Combat` start attack) and refuse it when `combat == playerCombat` and the
   attack's category is a melee swing; or veto in the pool selection `FUN_02261c10` for the player. The AI's
   `Combat` objects are untouched, so orcs still pick, start and land attacks (their `APPLYDAMAGE` on the player keeps
   working through `FUN_026e49a0`).
3. Parry/block: hook `FUN_026e49a0` (target == player): if the VR sword intersects the orc's weapon capsule in the
   last N ms, skip the hit and trigger the orc's block recoil (next steps: find the recoil starter in
   `FUN_02811140`/`FUN_02810e80`).
- Risks: the game's targeting/fight circle depends on the player being "in combat" (combat state set by player
  attacks; `StateCombat`); the counter prompt UI keeps popping (hide with `UI.Draw`/HUD flags or keep as a VR
  warning); the hit counter and combo/finisher unlocks never advance (drive them from our hits: `Combat.Lock.HitCounter`
  or the hit-counter functions - next steps); AI `CAIActionCounterMeleeBlock`/dodge may still trigger off player
  attack events we no longer send (orcs may stop blocking: needs our own "player is swinging" event).

### Biggest risks for the physical-combat plan

1. Damage today has no geometry: attack records carry ranges/angles, not blade shapes. Our blade decides, so every
   hit needs an attack record chosen by us; finding a stable set (and its recoil/sever/death behaviour) is runtime
   work.
2. The player body and the camera are coupled to animation-driven root motion, snap-align and sync actions; in VR
   the body must follow the head instead, and the game's assumptions (attack direction "To Input", "Direction
   (Attacker to Target)", fight-circle Camera score) must keep making sense.
3. Executions, drains, brands and struggles are paired animations that move the player: they need a third-person
   moment or a VR re-design.
4. Saves and Nemesis: any kill outside the game's damage path breaks the Nemesis memory; any save during tests must be
   redirected (section 7) - the user's GameData.sav is irreplaceable.
5. Threading/timing of the pose: animation jobs run as jobs; reading/writing bones at the wrong moment gives lag or
   tearing (one frame behind is visible in VR).

## 9. For the view/render map (pointers for the other helper)

- `CPlayerCamera` = `Player+0x2d0` (ctor `FUN_00a59390`, 0x510 B); `Player.GetPos` reads `cam+0x44c`.
- Photo mode `Player+0x318` (`PlayerPhotoMode`), debug camera data-gated, `ForceSpectatorClipMode` compiled out
  (`FUN_009e1440` empty).
- Comfort components: `PlayerMotionBlurSettings` (`Player+0x2f8`), `PlayerDepthOfFieldSettings` (`Player+0x300`);
  `Combat.Enable.SlowMo` `*(bool*)0x4c44318`; `Combat.EnableCameraOverride` `*(bool*)0x4c44298`.
- Hiding the player's head for the first-person view: `IEngineModel` vtbl+0x20/+0x38 (GetPiece/SetPieceHidden).
- Fight circle scores the on-screen position of orcs (`Camera` term in `FUN_028cd7a0`; predicates `AttackerOnScreen`,
  `MustBeOffScreen`): the stereo/HMD camera must keep feeding that "screen".
- `CGameClientShell::vf_02` (`0x9eabd0`) = the client per-frame update (also the `RenderCinematic` path).
- World units cm, +Y up (section 0).

## 10. Ghidra renames (som-linux)

One write run, 2026-10-18 01:55, `scripts/ghidra/SomRename.java` (new: renames only functions that still have
Ghidra's default `FUN_` name; TSV list in, report out) through `ghidra_script.sh run som-linux rw`: exit 0, saved,
47 renamed, 0 kept, 0 bad; a read-only re-run reports 47 "same". Ghidra renamed the thunk at 0x8ff410 with its
target. Before it: backup `reference/ghidra/som-linux-pre-renames-20261018/` (btrfs reflink copy under the
project flock; restore = `rm -rf` som-linux, `mv` the backup to som-linux). Afterwards `functions.tsv` and
`strings_xrefs.tsv` were re-exported (`SomIndex.java ... nocalls`, 23 s); `classes.tsv`/`vfuncs.tsv`/`calls.tsv`
are unchanged (no `vf_NN` label was renamed: the slot meanings are in sections 2-5 instead). List:
`reference/work-gameplay/renames.tsv`, reports `rename-dry.txt`, `rename-rw.txt`, `rename-check.txt`.

| Address | Old | New name | Evidence (section) |
|---|---|---|---|
| `0x9e06b0` | `FUN_009e06b0` | `GameClientShell_Create` | 1: IWBEngine vf_14 -> DAT_0466afa0, new 0x82630 + ctor |
| `0x9e8e00` | `FUN_009e8e00` | `GameClientShell_Ctor` | 1: writes CGameClientShell::vtable, DAT_046a9560 = this |
| `0x9e3570` | `FUN_009e3570` | `GameClientShell_GetLocalPlayer` | 1: every Player.* command |
| `0xa43990` | `FUN_00a43990` | `Player_Ctor` | 1: creates the Player components |
| `0xa368f0` | `FUN_00a368f0` | `Player_Teleport` | 1: CPlayerObj vtbl+0x2a0 (pos, rot, bool, bool) |
| `0xa36e60` | `FUN_00a36e60` | `Player_GetPlayerInput` | 1: *(player+0xd48)+0x10 |
| `0xa3ae90` | `FUN_00a3ae90` | `Cmd_PlayerGetPos` | 1: "PlayerPos %f %f %f" |
| `0xa3c820` | `FUN_00a3c820` | `Cmd_PlayerSetOrientation` | 1: Euler -> FUN_00a4a800 only |
| `0xa36ae0` | `FUN_00a36ae0` | `Cmd_Teleport` | 1: Teleport x y z |
| `0xa337f0` | `FUN_00a337f0` | `Cmd_PlayerFragSelf` | 4: DamageStruct "Explode" + DoDamage |
| `0x9e1440` | `FUN_009e1440` | `Cmd_ForceSpectatorClipMode_Empty` | 1: empty handler |
| `0xaa9a90` | `FUN_00aa9a90` | `Cmd_PlayerInputSetSimInput` | 3: SetSimInput / SetSimInputLoop |
| `0xaa97b0` | `FUN_00aa97b0` | `PlayerInput_PushSimCommand` | 3 |
| `0xaa9920` | `FUN_00aa9920` | `PlayerInput_PushSimWait` | 3: pushes {0x15d, n} |
| `0x2763050` | `FUN_02763050` | `DamageStruct_DoDamage` | 4 |
| `0x26d4ff0` | `FUN_026d4ff0` | `Combat_DealHit` | 4: sink of APPLYDAMAGE and collider hits |
| `0x26e49a0` | `FUN_026e49a0` | `Combat_ApplyDamageKey_RangeAngle` | 4: [COMBAT|APPLYDAMAGE] Hit/Miss |
| `0x26e5ae0` | `FUN_026e5ae0` | `Combat_ApplyDamageKey` | 4 |
| `0x26d5530` | `FUN_026d5530` | `Combat_OnColliderHit` | 4: from CCombatColliderRaycast::vf_02 |
| `0x26f9070` | `FUN_026f9070` | `Combat_OnModelString` | 4: APPLYDAMAGE, SEVER, ... |
| `0x22667f0` | `FUN_022667f0` | `SharedCombat_ParseWindowKeys` | 4: BHITWIN/EHITWIN/BCANWIN/BPW/BDW |
| `0x26eb380` | `FUN_026eb380` | `Combat_StartAttack` | 4: "Start Attack" |
| `0x26ee800` | `FUN_026ee800` | `Combat_StopAttack` | 4: "Stop AttackNoAnim" |
| `0x26d4510` | `FUN_026d4510` | `Combat_GetCurrentAttack` | 4: +0x4d0, variation +0x55c |
| `0x280fba0` | `FUN_0280fba0` | `Object_GetCombatHost` | 4 |
| `0x2699c50` | `FUN_02699c50` | `Character_GetCombat` | 4: +0x6d0 |
| `0x2699990` | `FUN_02699990` | `Character_GetInventory` | 4: +0x910 |
| `0x2311e80` | `FUN_02311e80` | `Inventory_GetEquippedWeapon` | 4 |
| `0x281a200` | `FUN_0281a200` | `ServerCombat_ApplyAttackDamage` | 4: alignment, AlwaysKill, DamageStruct, recoil |
| `0x2819890` | `FUN_02819890` | `HitInfo_Init` | 4 |
| `0x2261c10` | `FUN_02261c10` | `SharedCombat_SelectAttackFromPool` | 4: "Attack Passed %s - Score" |
| `0x2258db0` | `FUN_02258db0` | `SharedCombat_AttackPredicates` | 4: "Rejected Attack: %s - ..." |
| `0x62a4b0` | `FUN_0062a4b0` | `Init_CombatVariables` | 4: CombatVariablesInclude.h cvars |
| `0x2751a50` | `FUN_02751a50` | `Combat_LogPrintf` | 4: [COMBAT|...] logger |
| `0x25aa530` | `FUN_025aa530` | `DBRecord_GetName` | 4: record names in log lines |
| `0x21583f0` | `FUN_021583f0` | `DamageType_FromName` | 4: "Explode" |
| `0x2296aa0` | `FUN_02296aa0` | `Object_Distance` | 4: range test |
| `0x28cd7a0` | `FUN_028cd7a0` | `AICombatMgr_ScoreAttacker` | 6: "Accepted - Total" |
| `0x28d50c0` | `FUN_028d50c0` | `AICombatMgr_ShouldCancelAttack` | 6 |
| `0x28d0ef0` | `FUN_028d0ef0` | `AICombatMgr_OnStartAttack` | 6 |
| `0x28ccc90` | `FUN_028ccc90` | `AICombatMgr_OnPreDeath` | 6: PreDeath, lockout timer |
| `0x2114ec0` | `FUN_02114ec0` | `Difficulty_Recompute` | 4: AttackDelayScalar, DoubleAttackChance |
| `0x8de5e0` | `FUN_008de5e0` | `Physics_RayCast` | 5: IEnginePhysics vtbl+0x200 |
| `0x8f3c20` | `FUN_008f3c20` | `Object_GetSimpleModel` | 2: obj+0x38 vtbl+0x20 |
| `0x296ddb0` | `FUN_0296ddb0` | `NPC_HidePiece` | 2: GetPiece + SetPieceHidden |
| `0x238fe60` | `FUN_0238fe60` | `AnimNode_GetNodeTransform` | 2: pose buffer read |
| `0x2240f40` | `FUN_02240f40` | `ProceduralBody_RegisterNodes` | 2: Pelvis/Torso_Base/Spine4_joint |

## 11. Next steps

- Runtime: validate the IEngineModel slot meanings (GetNodeTransform on the player's head vs the camera) and the
  frame order; enumerate the player's pieces and nodes.
- Static: the per-command input state in PlayerInput/PlayerCombat (for game-level suppression); the recoil starter for
  VR parries; the object manager iteration (all orcs in range); character -> faction member (captain identity);
  `FUN_028198c0`/`FUN_026f61f0` in detail (death, sever, XP); the player's attack-record set (which records are
  safe to call with `FUN_026d4ff0` outside an attack).
