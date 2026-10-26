# Shadow of Mordor (Feral Linux build): the bow for VR - aim mode, arrow origin and direction, the shot, slow motion, wraith powers

Static analysis only (2026-10-20): Ghidra queries on `som-linux` (read-only, gate open, batches and output in
`reference/work-bow/b*.txt` / `o*.txt`, git-ignored), `objdump`, the TSV indexes, `reference/work-combat/schema.py`
(field names) and `reference/work-bow/schema_map.tsv` (every `_INIT_1315` schema global -> its array, count, filler
function). SITE generator: `reference/work-bow/sites.py`. Nothing was launched. Conventions as in re-combat.md:
**[high]** read in the code, **[med]** meaning inferred, **[low]** guess, **RT** = needs a boot. `P` = the client
`Player`, `pc` = `PlayerCombat` = `P+0x2e8` (`*(pc+8)` = `P`), `sc` = the player's server `Combat` = `*(pc+0xa08)`
(`*(sc+0x2b8)` = `pc`), `cam` = `*(CPlayerCamera**)(P+0x2d0)`, `wm` = the `CPlayerCombatWeaponMode` component =
`*(pc+0x8c8)` (`*(wm+0x668)` = `P`), `FA` = the free-aim block = `pc+0x458`. Units cm; `LTRigidTransform` = pos xyz +
quat xyzw (28 bytes), forward = the quat's +Z.

Answers design-vr.md 3 "The bow" (the RT markers "the ammo and the bow weapon mode") and re-combat.md 4.2/9.8.

## 0. Summary

| Item | Answer | Conf. | Sites |
|---|---|---|---|
| **Aim state** | a data-driven **player weapon mode** (`Combat/PlayerWeaponModes/Modes`, schema `Combat.PlayerWeaponMode` 28 fields: `Predicate`, `Data`, `SlowMo`, `VisionMode`, `UIMode`, `FreeAim`, `CameraFX`, `FX`, `AOE`, `Audio`, transitions, exits). `wm` evaluates every mode's `Predicate` per frame (`FUN_00e22900` from the PlayerCombat update `FUN_00a8e120`) and activates the winner (`FUN_00e20ca0`); each component enters/exits on change | high | `BOW_WM_UPDATE`, `BOW_WM_ACTIVATE` |
| **Aim command** | **data**: `Combat.WeaponModePredicate` `CommandID` + `UseRawInput`, `HeldDuration`, `IsToggle`, `MinEnergyToEnter/Remain`, `RequireToken`, `RequiredInventoryItem` ... No command id is hard-coded. Both input paths are fed by the command ON/OFF events (`wm`'s own press-time table and `pc+0x80`'s input buffer), so G3's events suffice. **P9: `CombatSecondary03` 124 (RMB / pad LT), held 0.2 s; 125 is the stealth mode, the fire is not mode data (section 9)** | high (mechanism), P9 (id) | `BOW_WM_CMDON` |
| **Camera** | irrelevant in VR: the mode's `CameraFX` and the anim-state camera are overridden by H1/G2. The aim ray reads the **gameplay camera object** `cam+0x338` (the object G2 writes), so stock aim in VR = aim with the head | high | `BOW_FA_CAMXF` |
| **Arrow origin** | `FUN_02331c30` = `RangedWeaponData.FireNode` on the active ranged item's model (the bow, attached to `L_Weapon`), else the node on Talion (sampled at the key time when the attack has an animation), else the item model's transform | high (code), med (which applies) | `BOW_FIREXF_KEY`, `BOW_FIREXF_NOANIM` |
| **Arrow direction** | `FUN_02332c50` aims from the fire transform at: `sc+0x170` object / `+0x178` node, else `FUN_026c7cf0(sc)` = for the player `P+0x214` (the live free-aim point, copied every frame from `FA+0`), else (zero point) fireXf +Z * `MaxRange` | high | `BOW_AIMPT_1..3`, `BOW_AIM_COPY` |
| **Free aim** | `FUN_00a9a140(FA, ctx)`: ray from `cam+0x338` along its +Z, length `MaxTargetDistance` + camera-player distance, `IEnginePhysics` vtbl+0x200 (G21), up to 8 hits, own exclusion list; result `FA+0` = the hit target's AimAtNode point (assist), else the hit point, else the ray end | high | `BOW_FA_UPDATE`, `BOW_FA_UPDATE2` |
| **Cheapest VR aim** | post-hook the **one call** `FUN_00a5b8b0(cam, &xf)` inside the free-aim update and overwrite `xf` with the bow ray (pos = nock point, quat with +Z from the string hand to the bow hand). Everything downstream (target pick, reticle, `P+0x214`, the stock arrow, Shadow Strike's target) follows | high (data flow), RT | `BOW_FA_CAMXF` |
| **The shot** | an attack picked by `pc`'s input machine from the active mode's pools (`FUN_00a8c700`). Two stock forms: `NoAnimShootOnStart` (AttackData 77: fires at once from `Combat_StopAttack`) or an animated attack with a `SHOOT` key. Input semantics are data (`Combat.Input`: `InputType` OnPress/HeldForPrevious/NotHeld, `Min/MaxHeldDuration`, `CancelOnRelease`). **No draw strength**: speed is data (`Velocity`, `SlowMoVelocity`) | high (paths), RT (which) | `BOW_NOANIM_STOP`, `BOW_FIRE_*` |
| **Arrows / Focus** | arrows = **tokens** of `*(sc+0x1c0)` (`+0x50` count, `+0x4c` max, `+0x61` unlimited -> 999; HUD `TokenCount`, `ui_collectable_arrow_collect`; script `GetAmmoAmount`, `AwardFocusToken`); add/consume `FUN_02720a90(res, delta)`. Focus = the float **energy** of the same object (`Combat.EnergySetup`), the bow mode's gate (`MinEnergyToEnter/Remain`) and drain (`CombatWeaponModeEnergyDrain.EnergyDecayRate`) | high (tokens), med (energy = Focus) | `BOW_TOKENS_ADD` |
| **Our own shot** | `FUN_02332c50(Character_GetInventory(*(sc+0x70)), sc, 0, 0, &fireXf)` with `*(sc+0x170) = 0`, `P+0x214` = our aim point (or 0 = along fireXf +Z), then `FUN_02720a90(*(sc+0x1c0), -1)`. Needs the bow active (it is while the stock bow mode is on) | high (call), med (preconditions) | `BOW_FIRE_*` |
| **Slow motion** | the mode's `SlowMo` component -> `FUN_02826ce0(*0x4c48cc0, rec)` = SlowMoMgr slot 3 **"CombatWeaponMode"** (10 priority slots); the record has `SimulationTimeScale`, `PlayerTimeScale`, `RampUp/Down`, sounds. **Not** gated by `Combat.Enable.SlowMo` (that only gates the attack presentations). VR can keep it | high | `BOW_SLOWMO_*` |
| **Wraith vision** | no hard-coded handler for `VisionModeToggle` 243 in the player code; vision is a mode `VisionMode` component -> `FUN_00ad1d20(P+0x2f0, rec)`. Direct: CCharacter vtbl+0xc8 `RequestVisionMode(bool)` | med | (`BOW_WM_*`) |
| **Shadow Strike, brand, drain** | attacks in mode/combat pools; commands are data (`Combat.Input.ActionId`/`HeldActionId`, chords, input strings) | high (mechanism), RT (ids) | - |
| **Force a mode** | `FUN_00a85e40(pc, modeRec, 1/0)` (= script `SetWeaponModeActive`): activates the mode with a script lock (its predicate is skipped), `0` removes it; records by name `FUN_02169390` | high (code), med (lock) | - |

## 1. Aiming

### 1.1 The weapon-mode machine

- Schema `Combat.PlayerWeaponMode` (`*0x4bb2320`, 28): 0 `OnDuration`, 1-8 `ExitOn*` (take damage, damage enemy, start any
  attack, camera presentation; with category exclusions), **9 `Predicate`**, **10 `Data`**, 11 `FX`, 12 `FXAlt`,
  **13 `SlowMo`**, **14 `VisionMode`**, **15 `UIMode`**, **16 `FreeAim`**, 17 `TargetFX`, 18 `CameraFX`, 19 `AOE`, 20
  `Audio`, 21 `MaterialTransform`, 22 `IncludedTransitions`, 23 `AllowedModes`, 24
  `ActivateIfSimultaneousModesAreInactive`, 25 `RequiredSimulutaneousModes`, 26 `ForcedModes`, 27 `AddToFrontOfList`
  **[high names]**. The player's list: character combat record field 61 `WeaponModeSetup` -> `Combat.PlayerWeaponModeSetup`
  {`Entries`, `EnergyDrain`} **[high names]**.
- Modes category (same shape as the attack lookup): cat `FUN_02169300` (`*0x4b567d0`), count `FUN_02169340`
  (`cat+0x28`), by index `FUN_02169360` (`cat+0x38 + i*0x30`), by name `FUN_02169390` **[high]**.
- `Combat.WeaponModePredicate` (`*0x4bb35a0`, 19): 0 `NeverExitBehaviorStates`, 1 `UseRawInput`, **2 `CommandID`**, 3
  `CannotStartAttackBTException`, 4 `IntoAttack`, 5 `FromAttack`, 6 `ExcludedAttackCategory`, **7 `HeldDuration`**, **8
  `IsToggle`**, 9/10 `Required/ExcludedBehaviorState`, **11 `RequireToken`**, 12 `CancelAttackCategory`, **13/14
  `MinEnergyToEnter/Remain`**, 15 `RequiredInventoryItem`, 16/17 `MountedCharacterType/ActiveItem`, 18
  `AudioEnergyFailure`; getters `FUN_00e24570` (CommandID), `FUN_00e244e0` (UseRawInput), `FUN_00e246e0`
  (HeldDuration), `FUN_00e24790` (IsToggle) **[high]**.
- Input check (`FUN_00e21a40` -> `FUN_00e191c0`): `UseRawInput` = `wm`'s press-time table (`wm+0x50..+0x58`, 12-byte
  entries `{u32 onMs, u32 offMs, u16 cmd}`, written by the `OnCommandOn/Off` delegates (vtables `0x3593aa8`/`0x3593ad8`,
  slot 2) with the game timer's ms `*(*0x463e290+8)+0x14`); otherwise `pc+0x80`'s combat input buffer
  (`FUN_02270120`, `FUN_02274a90`, `FUN_0226ed70`). Held = on later than off and now-on >= `HeldDuration` **[high
  code]**. Both are fed by the player command events, i.e. `FUN_00aab660/5d0` from G3; no polled read was seen **[med]**.
- Activation `FUN_00e20ca0(node, owner, mode, x)` (log "Activating Mode %s" with `Combat.Debug.WeaponMode` `0x4c44b58`); the
  components then run in `FUN_00e1f850`: Data `FUN_00e1bac0` (`wm+0x678`), SlowMo `FUN_00e1cf90` (`wm+0x6a0`),
  VisionMode `FUN_00e1d6e0` (`wm+0x6a8/0x6b0`), UIMode `FUN_00e1dc60`, FreeAim `FUN_00e1deb0` (`wm+0x6c0`), TargetFX,
  CameraFX, AOE, Audio, MaterialTransform (logs with `Combat.Debug.WeaponModeComponent` `0x4c44b18`). Active modes:
  list at `wm+0x5f8`, record at node `+0x30`, script lock byte `+0x47` **[high]**.
- `Combat.WeaponModeData` (8): 0 `ActivateWeaponSlot`, 1 `UseMountWeaponSlot`, 2 `WeaponSlot`, 3 `ClearCurrentAttack`,
  4 `NativePredicate`, 5 `PauseHitCounter`, 6 `PromptOverrides`, 7 `StateTags`: the mode makes the bow the active item
  (inventory slot 2 = `PC_Bow_01`, P7) **[high names, med effect]**.

### 1.2 Free aim (`FA` = `pc+0x458`)

`Combat.WeaponModeFreeAim` (`*0x4bb2ca0`, 13): 0 `Priority`, 1 `EnableAimAtRotateObject`, 2
`EnableAimAtProceduralSpine`, 3 `SmoothTimeForAim`, 4 `ClearRangedTarget`, 5 `TargetOverride`, 6 `LockTarget`, 7
`SendTargetInfoToUI`, **8 `MaxTargetDistance`**, 9 `AutoTargetDuration`, 10/11 `AutoTargetMaxScreen/WorldDist`, **12
`ModelNodeTestFirePos`** (default "LeftHand") **[high]**.

- Enter `FUN_00a860b0(pc, rec)` -> `FUN_00a9bea0(FA, rec, P)`; exit `FUN_00a860c0` (with `ClearRangedTarget`:
  `FUN_026d2d40(sc, 0)`). `FUN_00a8c700` asks `FUN_00e1a6d0(wm)` (= `wm+0x6c0 != 0`) **[high]**.
- Per frame `FUN_00a9a140(FA, ctx)` (from the PlayerCombat update `FUN_00a8e120`, called by the live player update
  `FUN_00a40fe0`; again from `FUN_00a8ae50` on a re-aim): `FUN_00a5b8b0(cam, &xf)` -> `xf` = `cam+0x338` (pos `+8`, quat
  `+0x14`), or the anim camera block in a UI state; ray from `xf.pos` along `xf` +Z, length `MaxTargetDistance` +
  |camera - player|; hits nearer than the `ModelNodeTestFirePos` node plane are skipped **[high]**.
- Layout **[high unless marked]**: `+0x00` aim point (the hit target's AimAtNode point `+0xec`, else the hit point
  `+0xc8`, else the ray end); `+0x0c` ray end; `+0x18` ray direction; `+0x40` / `+0x68` / `+0x80` (= `pc+0x498/+0x4c0/
  +0x4d8`) target, aim object, aim node used by the shot [med]; `+0x78` FreeAim record; `+0x88` `MaxTargetDistance`;
  `+0xa8` best target object; `+0xd4` its distance^2; `+0x120` current hit object; `+0x190` player pos/rot copy; `+0x1b8`
  exclusion list (16 x 0x20, count `+0x3b8`: the player, the mount, its riders); `+0x3d8` active.
- `FUN_00a8ae50(pc)` after the mode update: zeroes `P+0x214`, then (free aim on, `pc+0x830`) sets the anim parameters
  `Game_CombatAimAtPitch/Yaw`, **turns the player object's yaw toward the aim point** (objsys vtbl+0x98, slerp by
  `SmoothTimeForAim`, when `Modifier.AimAtTrackerRotate` runs or `EnableAimAtRotateObject`), and copies `FA+0` -> `P+0x214`
  **[high]**. Entering free aim also calls `FUN_00ab7a60(*(P+0x2c8), 1)` (likely the aiming locomotion; `PlayerMovement`
  data has `AimingPenOriginOffset`, `AimingRadiusScale`, `AimingTeleportRange`) **[low]**.

### 1.3 The arrow's origin and direction

- At the shot the stock code calls `FUN_02331c30(inv, rangedRec, &sc->window (+0xb0), *(sc+0x17c), &fireXf)` and
  `FUN_02332c50(inv, sc, rangedRec, accuracyRec, &fireXf)` (rangedRec/accuracyRec = AttackData 72/73
  `RangedWeaponOverride`/`RangedAccuracyOverride`, 0 = the active item's) **[high]**.
- `FUN_02331c30`: `FireNode` (RangedWeaponData 5) looked up on the item model object (`FUN_0231e150`, live
  GetNodeTransform) first, then on the owner (`FUN_02310270(inv)`): live, or **sampled** with IEngineModel +0x168 at the
  window's key time when the attack has an animation (the IK edit is not in a sampled pose), last the item model's own
  transform **[high]**. The bow model follows `L_Weapon`, so with G11 the live variants follow the left controller
  **[med, RT `aimray`]**.
- `FUN_02332c50` direction: target `sc+0x170` (node `+0x178`, `FUN_0229bc40`), else `*FUN_026c7cf0(sc)`: the owner is a
  player (CPlayerObj vf_27 `0x27b7820` = IsPlayer) -> `P+0x214`, else `sc+0x150`; a zero point -> `fireXf.pos + fireXf.+Z *
  MaxRange` (RangedWeaponData 14). Then `OrientTowardsTarget`, the look-at quaternion, `CProjectile` creation, AI notify,
  `FireStimulus`, `AttackSelfOnFire` (re-combat.md 4.2) **[high]**.
- The player's `Combat_StartAttack` wrappers: free aim on and `pc+0x498`, `+0x4c0` set -> `FUN_026edd90` (aim object +
  node, `sc+0x170/0x178`), else `FUN_026edd40(..., FA)` (`sc+0x150` = `FA+0`) **[high]**. For the player the
  `sc+0x150` copy is not what the arrow uses (`P+0x214` is) **[high]**.

### 1.4 Setting them from our code (cheapest first)

1. **Ray override (recommended)**: K_CALL hook `BOW_FA_CAMXF` (`0xa9a1d0`, `FUN_00a5b8b0(cam, LTRigidTransform* out)`):
   call the original, then while the VR bow is drawn write `out` = {nock point (string hand), quat with +Z = normalize(bow
   hand - string hand), +Y up}. The game's own ray then picks the target and assist node, fills `FA`, the reticle and
   `P+0x214`; the stock arrow spawns at the bow's `FireNode` and flies to that point. Keep it on until the arrow spawns
   (an animated shot reads `P+0x214` at its `SHOOT` key). Both `FUN_00a9a140` calls go through this one call. Game thread,
   inside the player update, after G1 has this frame's controller poses **[high flow, RT]**.
2. **Exact origin too**: post-hook `BOW_FIREXF_KEY` / `BOW_FIREXF_NOANIM` and overwrite the 28-byte `fireXf` with the nock
   transform (useful if the FireNode is sampled from the animation, not the IK hand).
3. **Own shot**: section 2.3.

## 2. Draw and release

### 2.1 What fires

- `pc`'s input machine (`pc+0x80`) validates the active mode's root/linked nodes and picks an attack with
  `SharedCombat_SelectAttackFromPool`; attack predicates include `ActiveWeaponMode`, `PlayerWeaponModeSlowMo` (fields
  97/96), `EnergyToken`/`EnergyTokensFull` (12/13); `FUN_00a8c700(pc, node, attack, target)` starts it **[high]**.
- An attack whose node has no animation goes straight to `Combat_StopAttack(sc, node, attack, target, FA|0)`
  (`BOW_NOANIM_STOP`); with `NoAnimShootOnStart` it sets `sc+0x150` and fires (`0x26ef179` / `0x26ef192`) **[high]**.
  Otherwise the animation's `SHOOT` key fires (`Combat_OnModelString`, `0x26fbc42` / `0x26fbc58`). Which form the bow
  uses: **RT** (`wmlog`).
- Charge/draw time: none in code. Hold/release behaviour and minimum hold are `Combat.Input` data (10 fields: `ActionId`,
  `InputType` {OnPress, HeldForPrevious, NotHeld}, `StickDirection*`, `HeldActionId`, `RequireAnalogInput`,
  `CancelOnRelease`, `IgnoreIfBlocking`, `MaxHeldDuration`, `MinHeldDuration`), plus `Combat.InputString`/`Chord`/
  `DoubleTap` **[high names]**. `FUN_02332c50` takes no strength; the speed is `Velocity` (+ `SlowMoVelocity/VelTransTime/
  VelBias` in slow motion). The P8 28 m/s was the orcs' `Orc_Archer_Proj`; Talion's ranged record is RT.

### 2.2 Arrows and Focus

- Resource object `res = *(sc+0x1c0)` **[high]**: tokens `+0x50` (count), `+0x4c` (max), `+0x61` unlimited (getters
  `FUN_0271d560`/`0271d570` return 999 then); `FUN_02720a90(res, delta)` adds (clamped to max and below by
  `Combat.DisableEnergyCost` `0x4c44398`), refreshes the HUD and sets the anim parameter `Game_NumTokens`. The HUD
  (`FUN_00b01ef0`) draws them as `Token` pips + `TokenCount`, plays `ui_collectable_arrow_collect` when they rise, shows
  `Infinity` when unlimited: **tokens = the arrows** (Elf-shot). Script names: `GetAmmoAmount`/`GetAmmoMax`
  (`FUN_026c6a40/6a60`), `AwardFocusToken` (`FUN_026c7c70`). `Combat.EnergySetup` 10/11 `TokenMax`/`TokenAward`
  **[high]**. Cost per shot: AttackPostData 28 `EnergyTokenCost` (consumer not traced) **[med]**.
- Energy (float, same object; `Combat.EnergySetup`: `EnergyMax`, `EnergyResetValue`, `UIEnergyToEnterWraith`,
  `CritEnergyModifier`, regen `NoThreat*`/`HasThreat*`, `PostEnergyDrainGainCooldown`, `EnergyGainForMoraleLoss`):
  regenerates per frame (`FUN_02720c60`), drained per active mode by `CombatWeaponModeEnergyDrain` {`ActiveWeaponMode`,
  `EnergyDecayRate`}, required by `MinEnergyToEnter/Remain`; current value `FUN_026c6a00(sc)`. This is the bow's
  **Focus** (the slow-motion budget) **[med]**. Test aids: `Combat.Lock.Energy` `0x4c44258`, `.Value` `0x4c43f58`.

### 2.3 Triggering the shot from our code

- **A (stock)**: on the VR release G3 sends the shot command's ON then OFF (data; expected `CombatSecondary04` 125 = pad
  RT, **RT** `wmdump`/`Combat.Debug.Input`), honouring a `MinHeldDuration` if the data has one. The game applies cost,
  predicates, the headshot `AttackOverrides`, XP. Needs the stock bow mode active (the aim command held by G3, or 2.4).
- **B (direct, instant)**: on the game thread `*(sc+0x170) = 0; *(int*)(sc+0x178) = -1; P+0x214 = aimPoint` (or zero =
  straight along fireXf +Z), `FUN_02332c50(Character_GetInventory(*(sc+0x70)) /*0x2699990*/, sc, 0 /*active item*/, 0,
  &fireXf)`, then `FUN_02720a90(*(sc+0x1c0), -1)`; refuse when the tokens are 0. `rangedRec = 0` returns without a shot
  unless the active item has `RangedWeaponData`, so keep the bow mode on or pass the bow's record **[high call, med
  preconditions, RT `bowfire`]**.

### 2.4 Entering the aim from code

`FUN_00a85e40(pc, mode, 1)` -> `FUN_00e21750(wm, mode, 1, *(pc+0xac0))`: adds the mode if absent (`FUN_00e20f50`), sets
its lock byte (`+0x47`, predicate re-evaluation skipped), applies the components; `0` removes it (`FUN_00e20170`) and
re-applies. Same as the script `SetWeaponModeActive` (`0x23f9da0` -> `FUN_02699fd0` -> `FUN_026d6280`); also
`ClearWeaponModeScriptLock` (`FUN_0269a020`), `ClearAllWeaponModes` (`FUN_0269a080`), `EvalCanActivateWeaponMode`
(`FUN_00a85fa0`) **[high code, med semantics]**. A locked mode ignores `MinEnergyToRemain`, so the stock command (A) keeps
the Focus rule; the force is the fallback if G3 cannot hold the aim command.

## 3. Slow motion while aiming

- The mode's `SlowMo` -> `Combat.WeaponModeSlowMo` {0 `SlowMo` (a `Combat/PlayerWeaponModes/SlowMo` record), 1
  `PreventAIDetection`, 2 `PreventDetectionExceptions`}. Enter `FUN_00e1cc50`: `FUN_02826ce0(*0x4c48cc0, rec)` and the AI
  detection block (`FUN_026d62e0`, `FUN_026d6460`); exit `FUN_00e1ce70`: `FUN_02826ce0(mgr, 0)` **[high]**.
- SlowMo record (`FUN_0261c1f0`, "This schema stores data related to SlowMo"): `SimulationTimeScale`, `PlayerTimeScale`,
  `RampUp`, `RampDown`, `SoundSlowMoInEvent`, `SoundSlowMoOutEvent` **[high names]**.
- SlowMoMgr (`*0x4c48cc0`): 10 slots of 0x30, the lowest occupied index wins (`FUN_02826230`; `+0x1e0` = active index,
  10 = none -> timers back to 1:1): 1 PhotoMode `FUN_02826f60`, 2 Script `FUN_02826b10`, **3 CombatWeaponMode
  `FUN_02826ce0`**, 4 Presentation `FUN_02826d80`, 5 CombatStruggle `FUN_02826a70`, 6 CombatBuff `FUN_02826e20`, 7
  CombatAI `FUN_02826ec0`, 8 CombatDynamic `FUN_02826ba0`, 9 CombatHint `FUN_02826c40`; slot 0 not named (console?). The
  winner writes the game timer `*(*0x463e290+8)+0x20/+0x24/+0x28` (simulation scale) and the player's timer
  (`FUN_02824bf0`, player scale), with ramps; `[SLOWMO]` log flag `*(bool*)0x4c48ccc` **[high]**.
- Gates: the mode predicate (`MinEnergyToEnter/Remain` = Focus, `RequireToken`), the mode's own exits
  (`ExitOnTakeDamage`, ...), the predicate `PlayerWeaponModeSlowMo` on attacks. `Combat.Enable.SlowMo` (`0x4c44318`) is
  read only by the attack slow-mo presentation (`FUN_02748e20/ee0/9020`), so it does **not** stop the bow's **[high]**.
  `SlowMO.Set` is overwritten on the manager's next recompute **[med]**.
- **VR: keep it.** The simulation slows; the HMD view, controller poses and the G11 arm edit run per render frame, not on
  the game timer. Notes: `PlayerTimeScale` < 1 also slows Talion's own timer (his locomotion, his anim time: RT the
  values); every VR speed must stay XR-space (design 3 already says so); the slow-mo sound events play. Comfort switch
  without touching data: entry hook `BOW_SLOWMO_SET`, pass `rec = 0` (or a milder record) only for this slot.

## 4. Wraith powers and their inputs

| Power | Mechanism | Command | Conf. |
|---|---|---|---|
| Wraith vision | a mode whose `VisionMode` component (`Combat.WeaponModeVisionMode` {`VisionMode`, `FXContext`, `ActivateVisionModeAfterPlayerFXIntro`}) enters `FUN_00e1d140` -> `FUN_00ad1d20(*(P+0x2f0), rec)`, `FUN_00ad3b60(.., 1)`; `PlayerVisionMode` component (vtable `0x3503bc0`). No `cmp ..,0xf3` (243) in the player code `0xa30000-0xae0000`. Direct: CCharacter vtbl+0xc8 `RequestVisionMode(bool)`, +0xd0 `SetVisionMode(rec)`; state `*(bool*)(*(char+0x10)+0x118)` (script `IsVisionModeOn`) | the mode's `CommandID`, likely `VisionModeToggle` 243 (`IsToggle`/`HeldDuration` decide hold vs toggle) | med, RT |
| Shadow Strike | an attack in the bow mode's pools; target = the free-aim target (1.4 makes it the one the bow points at); the move is the attack's alignment with `TeleportDuringInterpolation` | data | med |
| Brand / drain / combat drain | attacks (sync actions) gated by predicates (stunned target, `HitCounter*`, `Brand*`) and held inputs (`Combat.Input.HeldActionId`, `MinHeldDuration`) | `CombatSecondary*`/`CombatTertiary*`/`SpecialAttack*` + `SpecialAttackModifier` 160 (re-input.md 2.3), ids RT | high (mechanism) |
| Other bow abilities | other attacks in the same pools with their own `RangedWeaponOverride`/`AttackOverrides` (headshot etc.) | data | med |

Binding plan: G3 sends each power's own command by id (event pairs; holds for `HeldDuration`/`MinHeldDuration`), the
gesture picks the target as in design 3 "Wraith powers". The ids come from one `wmdump` + `Combat.Debug.Input` run.

## 5. SITE lines

```
SITE("BOW_FA_CAMXF", K_CALL, 0xa9a1d0, 0xa9a1bd, "a8 01 00 00 48 8b 03 48 8d 74 24 40 48 8b b8 d0 02 00 00 e8 db 16 fc ff", 0xa5b8b0, "55 48 89 fd 53 48 89 f3 48 83 ec 28 e8 6f 8e"),
SITE("BOW_FA_UPDATE", K_CALL, 0xa8e36e, 0xa8e35b, "98 fe ff ff e8 0c eb 00 00 48 8d b5 e0 fe ff ff 4c 89 f7 e8 cd bd 00 00", 0xa9a140, "80 bf d8 03 00 00 00 75 07 c3 66 0f 1f 44 00"),
SITE("BOW_FA_UPDATE2", K_CALL, 0xa8b54d, 0xa8b53a, "52 c1 00 00 48 8d bb 58 04 00 00 48 8d b4 24 80 00 00 00 e8 ee eb 00 00", 0xa9a140, "80 bf d8 03 00 00 00 75 07 c3 66 0f 1f 44 00"),
SITE("BOW_AIM_COPY", K_CALL, 0xa8e460, 0xa8e44d, "7f 03 00 00 45 31 ff 83 f8 02 0f 85 73 03 00 00 48 89 df e8 eb c9 ff ff", 0xa8ae50, "41 57 41 56 41 55 41 54 55 53 48 89 fb 48 81"),
SITE("BOW_WM_UPDATE", K_CALL, 0xa8e42c, 0xa8e419, "80 08 03 00 00 48 8b 80 d0 01 00 00 48 89 85 48 ff ff ff e8 cf 44 39 00", 0xe22900, "41 57 49 89 f7 41 56 41 55 41 54 55 53 48 89"),
SITE("BOW_WM_ACTIVATE", K_ENTRY, 0xe20ca0, 0xe20ca0, "41 56 49 89 f6 41 55 41 54 49 89 d4 55 48 89", 0xe20ca0, "41 56 49 89 f6 41 55 41 54 49 89 d4 55 48 89"),
SITE("BOW_WM_CMDON", K_VSLOT, 0x3593ab8, 0x3593ab8, "d0 41 e2 00 00 00 00 00", 0xe241d0, "48 b9 ab aa aa aa aa aa aa aa 48 8b 47 18 8b"),
SITE("BOW_WM_SLOWMO_COMP", K_CALL, 0xe1fcad, 0xe1fc9a, "e2 03 00 0f 85 cd 02 00 00 4c 89 a3 90 06 00 00 48 89 df e8 de d2 ff ff", 0xe1cf90, "41 56 41 55 4c 8d af f8 05 00 00 41 54 55 48"),
SITE("BOW_WM_FREEAIM_COMP", K_CALL, 0xe1fcc5, 0xe1fcb2, "48 89 df e8 26 db ff ff 48 89 df e8 9e df ff ff 48 89 df e8 e6 e1 ff ff", 0xe1deb0, "41 57 41 56 4c 8d b7 f8 05 00 00 41 55 41 54"),
SITE("BOW_FA_ENTER", K_CALL, 0xe1e027, 0xe1e014, "74 23 48 8b 85 68 06 00 00 4c 89 ee 48 8b b8 e8 02 00 00 e8 84 80 c6 ff", 0xa860b0, "48 8b 57 08 48 81 c7 58 04 00 00 e9 e0 5d 01"),
SITE("BOW_FA_EXIT", K_CALL, 0xe1dfff, 0xe1dfec, "48 85 f6 74 20 48 8b 85 68 06 00 00 48 8b b8 e8 02 00 00 e8 bc 80 c6 ff", 0xa860c0, "53 48 89 fb 48 89 f7 48 83 ec 10 48 8b 35 ce"),
SITE("BOW_START_ATPOS", K_CALL, 0xa8d02b, 0xa8d018, "4c 8d 83 58 04 00 00 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 10 0d c6 01", 0x26edd40, "f3 41 0f 10 08 f3 0f 11 8f 50 01 00 00 f3 41"),
SITE("BOW_START_ATOBJ", K_CALL, 0xa8cb19, 0xa8cb06, "08 48 89 ea 4c 89 ee 4c 89 f7 41 51 44 8b 8b d8 04 00 00 e8 72 12 c6 01", 0x26edd90, "4c 89 87 70 01 00 00 44 0f b6 44 24 08 44 89"),
SITE("BOW_NOANIM_STOP", K_CALL, 0xa8cf0b, 0xa8cef8, "44 24 78 00 00 00 00 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 f0 18 c6 01", 0x26ee800, "41 57 4d 89 c7 41 56 41 55 49 89 cd 41 54 55"),
SITE("BOW_FIREXF_NOANIM", K_CALL, 0x26ef179, 0x26ef166, "a0 01 00 00 00 00 00 00 c7 84 24 a8 01 00 00 00 00 80 3f e8 b2 2a c4 ff", 0x2331c30, "41 57 41 89 cf 41 56 49 89 d6 41 55 49 89 f5"),
SITE("BOW_FIRE_NOANIM", K_CALL, 0x26ef192, 0x26ef17f, "8b 44 24 20 4d 89 e0 4c 89 f9 4c 89 f2 48 89 de 48 89 c7 e8 b9 3a c4 ff", 0x2332c50, "41 57 41 56 41 55 41 54 55 48 89 f5 53 4c 89"),
SITE("BOW_FIREXF_KEY", K_CALL, 0x26fbc42, 0x26fbc2f, "00 00 c7 44 24 58 00 00 80 3f 48 c7 44 24 50 00 00 00 00 e8 e9 5f c3 ff", 0x2331c30, "41 57 41 89 cf 41 56 49 89 d6 41 55 49 89 f5"),
SITE("BOW_FIRE_KEY", K_CALL, 0x26fbc58, 0x26fbc45, "c3 ff 4c 89 f1 4c 8d 44 24 40 4c 89 ea 48 89 de 4c 89 e7 e8 f3 6f c3 ff", 0x2332c50, "41 57 41 56 41 55 41 54 55 48 89 f5 53 4c 89"),
SITE("BOW_AIMPT_1", K_CALL, 0x2332e33, 0x2332e20, "24 28 f3 0f 11 64 24 24 f3 0f 10 63 18 f3 0f 11 64 24 2c e8 b8 4e 39 00", 0x26c7cf0, "55 53 48 89 fb 48 83 ec 08 48 8b 47 70 48 85"),
SITE("BOW_AIMPT_2", K_CALL, 0x2333613, 0x2333600, "44 24 18 84 c0 0f 85 94 f8 ff ff 48 89 ef 4c 89 44 24 18 e8 d8 46 39 00", 0x26c7cf0, "55 53 48 89 fb 48 83 ec 08 48 8b 47 70 48 85"),
SITE("BOW_AIMPT_3", K_CALL, 0x2333afd, 0x2333aea, "2c f3 0f 11 64 24 28 f3 0f 11 54 24 24 f3 0f 11 44 24 18 e8 ee 41 39 00", 0x26c7cf0, "55 53 48 89 fb 48 83 ec 08 48 8b 47 70 48 85"),
SITE("BOW_SLOWMO_ON", K_CALL, 0xe1cc9f, 0xe1cc8c, "8b 43 10 48 8b 34 d0 48 85 f6 74 0c 48 8b 3d 21 c0 e2 03 e8 3c a0 a0 01", 0x2826ce0, "55 48 89 f5 53 48 89 fb 48 83 ec 08 80 3d d9"),
SITE("BOW_SLOWMO_OFF", K_CALL, 0xe1cebe, 0xe1ceab, "10 48 8b 04 d0 48 85 c0 74 0e 48 8b 3d 04 be e2 03 31 f6 e8 1d 9e a0 01", 0x2826ce0, "55 48 89 f5 53 48 89 fb 48 83 ec 08 80 3d d9"),
SITE("BOW_SLOWMO_SET", K_ENTRY, 0x2826ce0, 0x2826ce0, "55 48 89 f5 53 48 89 fb 48 83 ec 08 80 3d d9", 0x2826ce0, "55 48 89 f5 53 48 89 fb 48 83 ec 08 80 3d d9"),
SITE("BOW_TOKENS_ADD", K_ENTRY, 0x2720a90, 0x2720a90, "8b 47 50 01 c6 39 f0 0f 84 c3 00 00 00 55 31", 0x2720a90, "8b 47 50 01 c6 39 f0 0f 84 c3 00 00 00 55 31"),
```

Callable (no hook needed): `FUN_02332c50`, `FUN_02331c30`, `FUN_02720a90`, `FUN_00a85e40`, `FUN_02169300/9340/9360/
9390`, `FUN_02826ce0`, `Character_GetInventory` `0x2699990`, `DBRecord_GetName` `0x25aa530`.

## 6. Probe words (proposed; none written)

| ID | Question | Word | Kind | M |
|---|---|---|---|---|
| BW1 | every player weapon mode: predicate (`CommandID`, `UseRawInput`, `HeldDuration`, `IsToggle`, `RequireToken`, `MinEnergyToEnter/Remain`), `Data.WeaponSlot`, `SlowMo` record (`SimulationTimeScale`, `PlayerTimeScale`, ramps), `FreeAim` (`MaxTargetDistance`, `ModelNodeTestFirePos`, `LockTarget`, `AutoTarget*`, `EnableAimAtRotateObject`), `VisionMode`, `UIMode`; the player's `WeaponModeSetup.Entries` and `EnergyDrain` | `wmdump` (Modes category walk, `WMDUMP` lines + `probe-out/wmodes.tsv`) | R | M5 |
| BW2 | which command enters the bow and vision modes, which fires, which attack/record the shot is (NoAnim or `SHOOT`), the slow-mo slot and timer scales, tokens and energy over a draw | `wmlog on` (sets `0x4c44b58`, `0x4c44b18`, `0x4c44d18`, `0x4c44918`, `0x4c44798`, `0x4c44f98`, `0x4c48ccc`; `WMODE` lines from `BOW_WM_ACTIVATE`, `BOW_SLOWMO_SET`, `BOW_FIRE_*`: mode, record, `+0x1e0`, timer num/den, tokens, energy), with `cmd`/`pad` on `CombatSecondary01-04`, `CombatTertiary01-06`, `VisionModeToggle` | R/A | M5 |
| BW3 | the ray override steers the stock arrow; FireNode = the IK'd bow hand or a sampled node | `aimray YAW PITCH [hand]` (`BOW_FA_CAMXF` writes the camera pose turned by YAW/PITCH, origin `R_Hand` with `hand`; `AIMRAY` lines `FA+0`, `FA+0x18`, `P+0x214`, target) + a stock shot with `projlog`: spawn vs `L_Hand`, direction vs the ray | A | M5 |
| BW4 | our own shot works in and out of the bow mode; token cost; AI notice | `bowfire [YAW PITCH]` (`FUN_02332c50` from `L_Hand` along the turned camera, `sc+0x170 = 0`, `P+0x214 = 0`, then `FUN_02720a90(res, -1)`; `BOWFIRE` tokens before/after, active item, `PROJ new`) | A | M5 |
| BW5 | forcing the mode gives aim, slow-mo, reticle, the bow in hand; energy still drains; clean exit | `bowmode NAME on|off` (`FUN_00a85e40`) | A | M5 |
| BW6 | arrow and Focus counters | `tokens [+N]` (`res+0x50/+0x4c/+0x61`, energy `FUN_026c6a00`; `+N` = `FUN_02720a90`) | R/A | M5 |
| BW7 | does the aim turn Talion's body (yaw) under the VR rig | `aimray` with `trav`/`state` yaw logging while aiming | R | M5 |

## 7. Open, and notes for design-vr.md (not edited)

- RT: the aim and shot commands and the shot form (BW1/BW2); Talion's ranged record (speed, `SlowMoVelocity`,
  `MaxRange`, `ImmediateImpact`); the slow-mo scales; `EnergyTokenCost` = 1 per arrow; energy = the Focus HUD meter.
- The free aim turns the player object's yaw toward the aim point while aiming (`FUN_00a8ae50`): with the body following
  the head, either let the bow own the body yaw while drawn or clear `EnableAimAtRotateObject` (data); BW7.
- Design 3 "Aim": "G2 points the gameplay camera along that line" can become `BOW_FA_CAMXF` (only the free-aim ray, the
  rendered and gameplay cameras untouched); "simpler: fire our own `fireXf`" stays as 2.3 B, plus the token decrement.
- The `RangedCameraTag`/`RangedCameraTagOnEnemy` arrow camera (a presentation, slot 4 under the bow's 3) is G16's case.

## 8. Status (2026-10-20): built offline in somvr as `SOMVR_BOW=1`

Implemented in `mod/somvr` (pure logic `somvr_bow.c/h` + `tests/test_bow.c`, the game side in somvr.c's region
"SOMVR_BOW=1", selftest `bow_tests`; re-xr-pipeline.md 13 has the switches, logs, tests and runtime checks). Nothing
launched. What it uses from this file:

- **1.4 recipe 1**: `BOW_FA_CAMXF` (section 5's SITE line, now in somvr's `BOW_SITES` block) post-hooked: while the bow
  is aimed the free-aim transform = the nock point at the bow hand (+ Talion's move since the step), +Z from the string
  hand to the bow hand. The hook being called is also the "game's aim is on" signal (`FUN_00a9a140` returns unless
  `FA+0x3d8`). Exclusive with the probe's `P_BOW_CAMXF` (`aimray` needs `SOMVR_BOW` off; `mod/probe/build.sh`
  `EXCLUSIVE`).
- **1.1 / 2.3 A** (the fire part replaced after P9 by 2.3 B, section 10): the aim command held while aiming
  (`SOMVR_BOW_AIM_CMD`, default 124 `CombatSecondary03`) and the
  fire command tapped on the release (`SOMVR_BOW_FIRE_CMD`, default 125 `CombatSecondary04`; `SOMVR_BOW_FIRE=hold`
  holds it from the armed draw to the release), as command-level holds through G3's binding layer (the `wmhold`
  semantics). A over B: the game keeps the arrow cost, predicates, `AttackOverrides` and XP; `FUN_02332c50` is not
  called. The fire waits for the game's free aim and `SOMVR_BOW_AIM_MIN_MS` (the mode's `HeldDuration`); a shot without
  it within 600 ms is dropped and logged.
- **2.2**: the arrows (`*(sc+0x1c0)`: `+0x61` unlimited, else `+0x50`) are read for the logs only; two read-only K_ENTRY
  proofs (`FUN_026c6a40` GetAmmoAmount, `FUN_0271d560`).
- **3**: slow motion stays the game's own slot-3 record (nothing touched; `BOW_SLOWMO_SET` unused).
- **1.3 / design 3**: with `SOMVR_ARMS=1` the bow hand's weapon node (`L_Weapon`) takes `SOMVR_BOW_CAL` while the bow is
  out and is turned onto the shot while drawn (`SOMVR_BOW_ALIGN`).

Still RT: the aim and fire command ids and the mode's `HeldDuration`/`IsToggle` and the shot's `Combat.Input` (BW1/BW2
`wmdump`/`wmlog`); that the ray steers the stock arrow (BW3; `BOWRAY` lines + a shot); the body yaw while aiming (BW7);
the `FireNode` on the IK'd bow. Not built: Shadow Strike (X while aimed), wraith vision (Y is already the stock
`VisionModeToggle` hold), the slow-motion comfort switch, `bowfire`/`bowmode`/`tokens` probe words.

## 9. Runtime result (P9, 2026-10-20 09:46, `logs/boot-check-20261020-094644`; runtime-probes.md P9 results)

**The command ids (BW1, `wmdump`, 50 modes in `probe-out/wmodes-6.tsv`):**
- **Aim = `CombatSecondary03` 124 (RMB / pad LT), held, `HeldDuration` 0.2 s.** This is `PC_WraithRanged` and all
  its variants (FromStealth, Uprbdy_Rails, Mounted, the DLC OneRing ones; `DLC_PsnTrl_Ranged` 0.19 s). It is not a
  toggle and needs no token and no Focus (0/0). `RequiredInventoryItem` is `CP_BasicWraith`, `NativePredicate`
  "BowEquiped", weapon slot 2, and it exits on damage. **`SOMVR_BOW_AIM_CMD=124` is right**, and the default
  `SOMVR_BOW_AIM_MIN_MS` 250 covers the 0.2 s hold.
- **125 = `CombatSecondary04` (LShift / pad RT) is the entry command of `PC_Stealth`** (held, `HeldDuration` 0, the
  dagger slot 1, `PC_SheathMeleeToStealth`), **not a fire command.** A 125 press without the aim mode up puts Talion
  into stealth with the dagger. So somvr's rule that the fire waits for the game's free aim is needed.
- **The fire command is not in the mode data.** The shot is an attack in the aim mode's pools, picked through its
  `Combat.Input` (2.1), so `SOMVR_BOW_FIRE_CMD=125` stays a guess. On KBM the fire is presumably LMB (`CombatKBM05`
  140), and the hint in shot 20 reads "Hold [RMB] + tap [Z] to Shadow Strike" (Z = `CombatKBM07` 142). The pad's
  RT may well be 125 inside the aim mode. Next: `wmhold PC_WraithRanged` with `cmd CombatKBM05`, then in pad mode
  `CombatSecondary04`, with `projlog`.
- Wraith vision = `PC_WraithPing`: **`CombatSecondary01` 122 (V / pad LB), a toggle**, raw input. Section 4's guess,
  `VisionModeToggle` 243, is wrong. `PC_Radiance` is 122 held 0.85 s, with Focus >= 1 and the vision on.
- Slow motion = `PC_SlowMo`: 176 `CombatHidden06` (no binding), a toggle, Focus >= 1 to enter and to stay, only
  together with an aim mode (SlowMo record `WraithMode`).
- The aim's FreeAim record: `MaxTargetDistance` 11700, `AutoTargetDuration` 0.25 s, `AutoTargetMaxScreenDist` 0.15,
  `ModelNodeTestFirePos` "LeftHand", **`EnableAimAtRotateObject` 1** with `SmoothTimeForAim` 0 (BW7: by the data,
  aiming turns the body at once), `TargetOverride` `PC_WraithRanged`. Its vision mode `PingMode` (VM_Wraith) is on
  while aiming.
- The player's own list (`WeaponModeSetup` `Player_Character`): PC_SlowMo, PC_WraithPing,
  PC_WraithRanged_Uprbdy_Mounted, PC_WraithRanged_Uprbdy_Rails, PC_WraithRanged, PC_Stealth, PC_GrabDagger.

**BW3 and BW7, the aim ray: not run.** `wmhold Bow,freeaim` held `PC_Stealth` (125). No mode is named Bow, and the
probe's `freeaim` test is true for every mode, because each has a one-element FreeAim array, a null record for most.
The step also ran inside a Nemesis captain's showdown, where every press logged `active=0`. `aimray`: 0 calls of
`BOW_FA_CAMXF`, `FA active=0`, no `PROJ`; Talion's yaw unchanged. 1.4's recipe and 2.3 A are still RT.

## 10. Status after P9 (2026-10-20, offline; nothing launched): the default fire is our own shot

`SOMVR_BOW` (re-xr-pipeline.md 13) after section 9:
- The aim stays `SOMVR_BOW_AIM_CMD` 124 `CombatSecondary03`, held (P9: right).
- **125 is never sent by default.** `SOMVR_BOW_FIRE_CMD` is now unset (0) by default: no fire command at all. 125
  enters `PC_Stealth` (the dagger), so it was not a fire.
- **The release fires 2.3 B instead**, once the game's free aim is on and the aim was held 250 ms (as before). The call
  is `FUN_02332c50(inv, sc, 0, 0, &fireXf)`:
  - `fireXf` = the latched ray, from the nock with +Z along the shot, moved with Talion since the release;
  - `inv` = `*(owner+0x910)`, `owner` = `sc+0x70`;
  - before the call, `sc+0x170` = 0 and `sc+0x178` = -1, so no stale aim object steers it: the arrow flies at
    `P+0x214`, the free-aim point along our ray;
  - after the call, `FUN_02720a90(res, -1)` takes the arrow (unless unlimited).
- **Gates.** 2.3 B's "medium preconditions" are now checked before the call. A failing gate drops the shot, logs it
  (`dropped: our shot refused (...)`), gives the "denied" pulse and keeps the arrow:
  - the fire sites;
  - the game's aim;
  - the player's server Combat, its owner and the inventory;
  - an active item (slot 0..7, the item, its record) with `RangedWeaponData`. With `rangedRec` 0, `FUN_02332c50`
    returns without a shot when the active item has none (`0x2333f90`), so no arrow may be taken then;
  - an arrow, or unlimited;
  - a finite ray.
- **Sites.** Six read-only sites in `BOW_SITES` prove what it calls and reads: `BOW_FIRE`, `BOW_FIRE_ITEM`,
  `BOW_ITEM`, `BOW_INVENTORY`, `BOW_AIM_OBJ` (`FUN_026edd90`: `+0x170`/`+0x178`) and `BOW_TOKENS_ADD`.
- **`SOMVR_BOW_FIRE_CMD=<id|name>`** brings 2.3 A back (tap or `SOMVR_BOW_FIRE=hold`) once the real fire command is
  known. Setting it to 125 logs a WARN.
- What our shot skips:
  - the shot attack's own data (`AttackOverrides` such as the headshot, XP, `EnergyTokenCost`, its animation): it is
    no attack;
  - Shadow Strike.
- RT, the next bow boot:
  - `fire: our shot FUN_02332c50(...)` and an arrow from the bow hand along the ray (`projlog`: `PROJ new` near
    `L_Hand`);
  - the arrow count one less in the HUD;
  - whether the shot alerts and damages like the stock arrow;
  - no stealth mode at the release.

## 11. Runtime result (P10, 2026-10-20 15:46, `logs/boot-check-20261020-154655`; runtime-probes.md P10 results)

P10 was a locomotion run. The bow got one step, the Shadow Strike test (LR10), in KBM mode inside a patrol's fight:
- **The aim by the exact mode name works** (the P9 probe fix): `wmhold PC_WraithRanged` = 124 `CombatSecondary03` held
  3 s, `active=1`. Shot 52 shows the aim: the bow drawn, the wraith vision tint, the reticle. A heavy hit 0.3 s in
  (`Dagger_RRecoil_Heavy_U2D_B`, from the patrol) did not end it while 124 stayed held: the aim was up ~2 s later. So
  "exits on damage" (9) either re-enters on the held command or does not apply to every hit **[RT detail]**. The game
  polled the held 124 only 5 times in the 3 s (P9's 125 hold: 3477 polls).
- **The aim follows the game camera, not Talion's facing.** The probe turned the stick to the orc (-70 deg) and the
  camera stayed at 122 deg on a rock: "No Target Selected". `CombatKBM07` (the strike) was then never polled. In VR our
  ray is meant to replace the camera's (1.4, `BOW_FA_CAMXF`; still RT, BW3), so the aim point would be the player's.
  The probe needs `aimray` or a camera turn for the strike.
- **Nothing on the fire path**: no `CombatKBM05`, no `aimray`, no `projlog` this run. `SOMVR_BOW`'s default fire (our
  own shot, 10) and its RT list stand unchanged; `somvr_bow.c` not touched. P11: `wmhold PC_WraithRanged`, `aimray`,
  `cmd CombatKBM05` with `projlog`, then the strike at an orc in the aim, and the pad mode's `CombatSecondary04`.

## 12. Build 11: the bow on build 10's switches, at runtime (2026-10-25, headless; handoff/HANDOFF-bow.md "Round 1")

`SOMVR_BOW=1` on top of `scripts/headset-build10.env`'s 65 switches, driven by `scripts/bow-drive.sh` through the
layer's test hands (`SOMVR_XR_TEST`: the real input path; both hands written in one line so their distance is exact)
in the practice meadow. Runs (`logs/boot-check-20261025-*`): `000422` (boot 1, the code as built on 10-20), `002519`
(2), `004210` (3), `010840` (4), `013217` (5), `015515` (6), `021642` (7), `023643` (8), `030545` (9, build 11's
switches + `SOMVR_BOW_SLOWMO=off`), `031709` (10, the FX A/B). All: 0 WARN, the install unchanged, the real save
UNCHANGED.

### 12.1 What runs as built (boot 1, the 10-20 code, unchanged)

- The aim: the left trigger (`SOMVR_BOW_BUTTON=trigger`) = `enter (by the button)`, 124 held, the game's free aim on
  ~1 s later at ~20 free-aim calls a second (the game's player update, headless), `BOWRAY` overrides on every call
  (2325 of 2329). The wraith tint and the game's slow motion come with the mode.
- The string: the right grip within 15 cm of the nock (the left grip) = `nock`, `armed` at 20 cm, the release = our
  shot `FUN_02332c50` from the nock along the latched ray; the arrow counter 8 -> 1 over 7 shots (`FUN_02720a90`). No
  125 sent (0 fire ticks): no stealth, no dagger. `exit: apart` 0.8 s after the hands part.
- What did not hold up: (a) in a fight the aim mode drops on every hit (the data's `ExitOnTakeDamage`) and comes back
  ~1 s later: one release waited 600 ms and was dropped, one was refused (`the active item has no RangedWeaponData`:
  Talion's hit recoil draws the sword, slot 0 active); (b) every game draw of the sword during the bow was answered by
  the slots' SUSPEND with a put-back call (49 game calls in one fight) and a RESTORE draw after the bow although the
  player never had the sword; (c) **no shot ever hurt the orc** (12.3).

### 12.2 The changes (all behind `SOMVR_BOW=1`; offline tests in 12.6)

- **`SOMVR_BOW_RELEASE=now`** (default `wait` = as before): the release shoots at once whenever the bow is ready - the
  active item has `RangedWeaponData`, or (a hit put it away) slot 2 holds the bow, which is then activated for the shot
  by `Inventory_ActivateSlot` `FUN_02315ca0(inv, 2)` (new read-only-proven site `BOW_ACTIVATE`, the slots'
  `SL_ACTIVATE` bytes; what the aim mode's `ActivateWeaponSlot` does). Without the game's free aim `P+0x214` is stale,
  so it is zeroed for the call (the arrow straight along fireXf +Z, re-bow.md 0) and put back. A fire command
  (`SOMVR_BOW_FIRE_CMD`, the word `bow fire`) still waits for the game's aim: 125 outside the aim = the stealth mode.
- **The shot's attack** (`SOMVR_BOW_ATTACK`, default `PC_Ranged_Ground_Normal_Shoot_Light`, `none` = off): see 12.3.
- **The slots** (somvr.c's `SOMVR_SLOTS_GAMEDRAW=refuse` block): while the bow is out (and its 0.3 s tail) a game draw of
  a sword that was not in the player's hand the frame before is kept on the back like any other game draw (the
  `sw_away` move: no game call, no animation, no RESTORE afterwards). A sword held when the bow comes still goes back by
  the slots' SUSPEND and returns by its RESTORE. Boot 2: 43 draws kept on the back, 0 put-back calls, 0 restores.
- **Haptics**: the string hand within the nock radius while aimed = a light tick (`hover`, once per approach, out again
  at radius + 5 cm; HL:A's "you can grab this"); the bow not brought out by the game within `SOMVR_BOW_WAIT_MS` of the
  enter = one "denied" pulse on the bow hand (`denied: ...`). The nock click, the tension ticks, full draw, the
  release's kick, a let-down and a refused shot as built.
- **Evidence lines**: `BOW arrow p=.. new` (the first sight of a projectile whose shooter is Talion's Combat within 1 s
  of a shot, through SOMVR_THREAT's CProjectile::vf_13 hook: where it is against the ray, its flight against the ray,
  speed, range, the immediate/homing/gravity flags, its ranged and attack records), `BOW arrow .. end` (after the game's
  updates: done or not, travelled, the nearest watched orc), `BOW hit check 1.5 s after the shot` (every live orc
  within 40 m of the nock at the shot: health then and now, DEAD/gone), `BOW target` (twice a second while the bow has a
  ray: the nearest orc's chest, root + 40 cm, right/up of the ray in degrees; the driver aims with it), `BOW item` (the
  active ranged item's model entries, once). Test words: `bow fire N|NAME|ours`, `bow arrows N`, `bow attacks`.
- **The VR menu's Controls page**: `Bow: hold left trigger`, `Shoot: R grip, pull, let go`; `Swing` dropped and
  `Turn`/`Crouch` merged (`Turn: R stick, down=crouch`) to make room (the panel holds 18 lines at 32 px); the notes
  `Not yet: dash, wraith phase, / finishers, Shadow Strike`.
- `SOMVR_HP_NEARLOG` skips nodes larger than 50 m (the sky and terrain filled all 32 lines before).

### 12.3 Why our arrows did no damage, and the stock fire command in the aim

- Boot 3's `BOW arrow` lines: our projectile is `ImmediateImpact` (+0x22b = 1: it resolves its hit from the fire call,
  re-combat-items.md 1.2; it is done after its first update, `travelled` = the distance to what it hit), its ranged
  record the bow item's `PC_Bow_01`, **its attack record none**. `Combat_ApplyProjectileHit` (`FUN_026c8e10`) applies
  the projectile's attack (+0x190), and the projectile's init (`CProjectile::vf_09` -> `0x27c3b19`
  `FUN_026d4510(shooter Combat)`) takes it from the shooter's current attack `Combat+0x4d0` (an override from its list
  when `+0x55c` is set). Our own shot starts no attack, so +0x4d0 was empty or stale: no damage, ever.
- Boot 4: `bow fire 125` (the stock pad RT) tapped **inside** the aim (the game's free aim on) **is the stock shot**: a
  projectile 152 ms after the tap, ranged `PC_Bow_NormalArrow_Light`, attack `PC_Ranged_Ground_Normal_Shoot_Light`, its
  flight 0.0 deg off our ray (the ray override steers the game's own arrow), the arrow counter one less by the game. So
  P9's "125 is not a fire" holds only outside the aim; P9's `wmdump` could not see it because the shot is an attack in
  the mode's pools. The stock shot needs the aim mode up at the release (a hit knocks it out) and leaves 150 ms later.
- So our shot (kept as the default: at once, also when a hit knocked the mode out) now carries the stock shot's data:
  `Combat+0x4d0` = the attack `SOMVR_BOW_ATTACK` (looked up by name, `CS_ATK_BYNAME`; or the stock shot's once one was
  seen) and `+0x55c` = 0 for the call, put back after; `rangedRec` / `accuracyRec` = that attack's AttackData fields 72
  `RangedWeaponOverride` / 73 `RangedAccuracyOverride` (the stock shot's `PC_Bow_NormalArrow_Light`). Headshots are
  the projectile's attack's `AttackOverrides`, matched at the hit, so they come with it.
- The arrows' maximum is the save's: `bow arrows 40` added none (8 -> 8; `FUN_02720a90` clamps at `+0x4c`).

### 12.4 The bow as the eyes see it: the string, the wraith, the hold

- **The string to the sky** (every boot until 015515): the bow (the wraith item `CP_BasicWraith`'s one model entry, an
  object attached to node `0xc07ebcb6`, its own tracker) has 7 nodes (the `BOW skeleton` probe in ARM_TRK_EVAL's post):
  0 the root, 1 the grip (0,-5.5,0), **2 the string's middle**, 3/5 the lower limb and tip (0,-35.2,-2), 4/6 the upper
  limb and tip (0,23.8,-1.4). The aim animation holds node 2 at the stock full draw, (9.1,3.1,-73.7): 74 cm back along
  the bow's -Z, i.e. behind the VR player's head (the eye ~58 cm from the bow), so the two string halves ran from the
  tips to a point behind the eye and left the view at its top. **`SOMVR_BOW_STRING_POS=hand`**: node 2 written in that
  post (model space through the bow object's transform) = our string hand while drawing (carried by Talion's move since
  the step), the tips' midpoint otherwise. Boot 021642: no line to the sky; `the string node ours drawn/rest 1300/1537`.
- **The wraith over our hands**: the bow mode draws Talion as Celebrimbor's wraith (P10's flat shot 52: the wraith's
  arms, hair and circlet over Talion). In the eyes (our VR hands drawn, the game's hidden) its arms showed: with build
  10's body edit (`SOMVR_BODY=hands`, `line`) a glowing forearm stretched along the line through both wrists, with the
  edit off (`hands hide show`) the whole wraith from the shoulders. `nan` / `zero` keep it (the edit never hides the
  hands' subtrees). **`SOMVR_BOW_GHOST=nomat`** skips the player weapon modes' MaterialTransform component
  (`FUN_00e1f450(wm)`, the "MaterialTransform: Enter/Exit %s" logs; mode field 21; the bow mode's `'WraithRanged'`) at
  its two calls (`BOW_MAT_APPLY` 0xe1febd in FUN_00e1f850, `BOW_MAT_CLEAR` 0xe1f83c in FUN_00e1f760): boot 021642, 72
  calls skipped, the shoulders and the stretched forearm gone. A white wraith glove stays around the bow hand: not the
  mode's FX either (`SOMVR_BOW_FX=game|nofx|nofxalt|none` and the word `bow fx` skip the FX / FXAlt components at their
  four calls `BOW_FX_APPLY` 0xe1fa9c -> FUN_00e1c400, `BOW_FXALT_APPLY` 0xe1fc91 -> FUN_00e1c800(wm, rec), `BOW_FX_CLEAR`
  0xe1f7f4 -> FUN_00e1c510, `BOW_FXALT_CLEAR` 0xe1f7fc -> FUN_00e1ca10; boot 031709's A/B: the same picture in all four),
  so presumably the bow model's own (not built further; not for build 11's env). `hide`/`nan` (the body edit with the
  hands too) are kept as options and do not remove it.
- **The hold**: the bow node takes the sword's calibration (`SOMVR_BOW_CAL` = `SOMVR_ARMS_CAL_L`), so the bow was held
  like a sword: its limbs (+Y) along the shot (boot 021642: the string hand 57 cm along the bow's -Y; the lower tip 27 cm
  from the eye, the upper 85 cm: the bow seen end-on as a vertical stick). **`SOMVR_BOW_HOLD=auto`**: while aimed and
  not drawn with the hand steady (10 frames under 2 deg each) the bow's frame in the grip is measured (M = grip^-1 bow,
  world) and the bow node's calibration turned once, cal' = M^-1 cal, so that the bow's +Z (the shot) and +Y (the upper
  limb) lie on the grip's +Z (the controller's forward) and +Y; a check line 0.5 s later.

### 12.5 What is proven, and where

| Claim | Run (logs/boot-check-20261025-) | The lines |
|---|---|---|
| The left trigger brings the bow: the game's aim mode on (tint, slow motion, reticle) | every run; 023643 | `enter (by the button)`, the per-second line `the game's aim on`, ~20 free-aim calls/s, `BOWRAY` overrides on every call |
| The hover tick, the nock click, the tension ticks, full draw, the kick, a let-down | 002519, 015515, 023643 | `hover: the string hand 8.1 cm from the nock`, `nock`, `armed`, `release`, `let down: no shot (the draw reached 15.2 cm)`; `haptic frames` in BOW END |
| The arrow leaves from the bow hand along the ray | 004210, 010840, 013217 | `BOW arrow ... = 0.0 cm along the shot's ray from the nock, 0.0 cm off it; its flight ... 0.0 deg off the ray` |
| The ray steers the game's own aim and arrow | 010840, 013217 | the stock shot (`bow fire 125` in the aim): `its flight 0.0 deg off the ray`; 013217: its projectile's target = the orc (`target 0x21bc0600`, homing 1) |
| An arrow less per shot | every run | `arrows 8 -> 7` ... `-> 1`; then `dropped: our shot refused (no arrows ...)` + the denied pulse |
| No stealth at the release | every run without `bow fire 125` | 0 fire ticks, no 125 event (`stealth command lines: 0` from the driver); 125 only in the stock-fire rounds, inside the aim |
| Our arrow hurts an orc | 013217, 015515 | `BOW hit check ...: health 95.0 -> 85.0` after our shot (the attack set), 85 -> 75 |
| Our arrow kills an orc | 023643 | after `bow orchp 8` (the test word: one hit's worth left): `health 9.0 -> 0.0 DEAD`, `PRACTICE ... the Infantry orc is down` |
| A hit does not cancel the shot | 023643 | `the game's aim off: straight along the ray (P+0x214 zeroed for the call ...)` at the killing shot (the aim mode knocked out by the orc's blows) |
| The bow upright in the hand, the string on the string hand | 023643, 030545 | `BOW hold ... check 0.0 deg / 0.0 deg` (030545: seeded, the measurement already identity), `BOW skeleton`: the tips 69/73 cm from the eye, node 2 at (0,0,-57) when drawn; 023643 compose/frame1203.png, 030545 frame604 (summoned), 1194 (drawn), 1754 (aimed at the orc: the game's own "Shadow Strike (A)" prompt) |
| Build 11's switches together | 030545 | 3 shots, the kill (after `bow orchp 8`): `the bow was put away (the active slot 0, the game's aim off): slot 2 activated for the shot` then `health 8.8 -> 0.0 DEAD`; MaterialTransform 20 and SlowMo 20 calls skipped; the string node ours 735/852 frames; 0 stealth commands |
| The game's draws during the bow stay on the back | 002519 | 43 `kept on the back`, 0 put-back calls, 0 restores |

Long shots in the headless tests missed: the driver aims from the game's numbers (an orc's chest = its HOBJECT + 40 cm,
the test hands in the head's yaw frame that drifts under Monado's simulated head), and at 11-15 m 1 deg is 20-25 cm. The
hits are at 0.6-1.4 m. A player aims with their eyes; whether the ray matches the arrow's flight at range is for the
headset (the arrow is ImmediateImpact: a ray, no drop).

### 12.6 Offline tests

`build.sh --check` green (layer 133/133): `test_bow` 135 (+9: `release_now` - fired at the release when ready, a snap
shot, waiting until ready, dropped when never ready, the wait mode ignoring `bow_ready`, the gate with `any_aim`);
`test_vrmenu` 484 (the Bow and Shoot rows, the notes without the bow, the rows still fit at 32 px); `selftest` 0
failures with the new stand-ins (`BOW_ACTIVATE`, `BOW_MAT_APPLY`/`_CLEAR`) and a case 11b': slot 0 (a sword without
`RangedWeaponData`) active, `release_now`: the release activates slot 2 and shoots, an arrow less; `check_sites.py`'s
bow block 12 sites.

### 12.7 Build 11's switches (on top of headset-build10.env)

```
SOMVR_BOW=1
SOMVR_BOW_RELEASE=now
SOMVR_BOW_GHOST=nomat
SOMVR_BOW_STRING_POS=hand
SOMVR_BOW_HOLD=auto
```
Optional: `SOMVR_BOW_SLOWMO=off` (the player weapon modes' SlowMo component skipped at its two calls `BOW_SLOW_APPLY`
0xe1fcad / `BOW_SLOW_CLEAR` 0xe1f804 -> `FUN_00e1cf90`: no Focus slow motion while aiming; the default keeps the game's,
which is world time and never moves the view).
`SOMVR_BOW_ATTACK` stays at its default (`PC_Ranged_Ground_Normal_Shoot_Light`). Test-only, never in a player's env:
`SOMVR_HANDS_TEST`, `SOMVR_XR_TEST`, the words `bow fire|arrows|orchp|attacks`.

### 12.8 Open

- The headset: the draw's feel (20 cm armed, 55 cm full, the 15 cm nock radius), the hold seeded and auto-measured (a
  player's natural bow grip may want a tilt: `SOMVR_BOW_CAL`), the white glow left around the bow hand (the mode's FX,
  not the material), the arrow's look on the string, the aim at range.
- Shadow Strike (X while aimed): unlocked in the test save (the game's prompt "Hold [LT] + tap (A) to Shadow Strike",
  combat round 3's pictures) but not built: it teleports Talion to the orc, which needs the design's wraith-dash
  presentation (design-vr.md "The bow") first.
- Our shot skips the stock shot's own extras: XP, the attack's `EnergyTokenCost` (Focus), its animation. The stock
  fire (`SOMVR_BOW_FIRE_CMD=125`, only inside the aim) has them but waits 150 ms and needs the aim mode up.
- Focus / slow motion: the game's by default (world time, no view motion); `SOMVR_BOW_SLOWMO=off` skips it.
- The arrows' maximum (8 in the test save) and their refill are the game's.

## 13. Round 2 (2026-10-25, headless; handoff/HANDOFF-bow.md "Round 2"): the flat prompts, the buttons, the arrow's aim

Branch `t3/f45017b4`. Runs in logs/boot-check-20261025-: 040227 (A: prompts, buttons, strike, the orc at 1 m, snap),
042105 (B: strike, six shots at a fresh orc 9.4-9.6 m away), 043211 (C: the assist), 044033 (D: the head as the shot's
aim node), 044855 (E: the headshot rule), 045743 (F: + isCrit), 050551 (G: perfect head aims), 0512xx (H: the stock
shot). All on build 11's switches (headset-build10.env + the bow's five + `SOMVR_XR_HANDS_DEPTH=world SOMVR_COMBAT_DEATH=Block_Light
SOMVR_DEFENCE_CONTACT_MOVE_CM=15`) plus the round's switches; driver scripts/bow2-drive.sh.

### 13.1 The flat prompts while the bow is out: which path, which commands
- Not hints: SOMVR_HINTS=off's three hooks (34.4 of re-xr-pipeline.md) saw no event for them (030545: only the combat
  hints and a tutorial hide). They are **prompt-diamond entries**: CCharacterProxy's PromptShow event {+0 key, +8 action,
  +0x10 StringId, +0x18 IconId, +0x20 category, +0x24.. flags} reaches each of the HUD's three CUIHUDPrompts through a
  TypedDelegate (re-hud.md 4.2); slot 2 (`0xb231d0`) checks the category bit and calls the diamond's vf_02 (`0xc0e8f0`):
  the diamond picks the button's place by vf_0x28(action) and its picture from the action's binding (`FUN_00aaaf50` on
  the player's input). So the glyph is always the flat pad's button for that command. PromptHide {key, action} (slot 2
  `0xb23140`, diamond vf_03 `0xc0ee70`: an absent entry is skipped), PromptHoldUpdate {key, action, fill +0xc} (`0xb230b0`
  -> `FUN_00c0d330`).
- Logged (040227, `padprompts log`): the aim mode brings three, each show sent to all three elements:

  | Shown | Action | StringId | What our controls send for that button |
  |---|---|---|---|
  | `(Y) Distract` | 119 CombatPrimary02 (stock pad Y) | `UI_Wraith_Distraction` | Y = the wraith vision (122 taps; in the aim the vision is already on: no tap, 0 commands) |
  | `(R) Deactivate Focus` / `Activate Focus` (R3) | 127 CombatTertiary02 (stock R3) | `UI_Ranged_SlowMo_Deactivate` | the right stick click = PDA 95 (the map) |
  | `Shadow Strike (A)` | 121 CombatPrimary04 (stock pad A) | `UI_Wraith_Dash` | A = nothing on foot (SOMVR_GAMEMOVES=off), the climb's drop when hanging |

  119 and 126-131 are on the deny list (never sent, their records held at 0 in the world); 121 is sent only by pad A
  with the game's moves on. None of the three is right in VR.
- **`SOMVR_PAD_PROMPTS=game|log|vr`** (somvr_prompts_game.c; default game = nothing patched): the three slots hooked
  (sites PP_SHOW/PP_HIDE/PP_HOLD, K_VSLOT, contexts = the vtables' slots 0..3). vr drops a show unless a VR control
  sends the command in the world: Menu 14, Crouch 15, Activate 34, PDA 95, the bow's aim 124 (with SOMVR_BOW), and with
  SOMVR_GAMEMOVES=on pad A's 16/18/27/121 (121 never while the bow is out). Kept ones are relabelled to the Touch's
  button where the flat pad has it (`SOMVR_PAD_PROMPTS_MAP`, default `34:120,95:127`: Activate shown as pad B = our B,
  PDA as R3 = our right stick click; show, hide and hold alike, the text the game's). Hides always pass. A show and a
  hide hook both or neither. Words `padprompts [game|log|vr|map A:B,..]`.
- Proven: 040227 frame771 (log: `(Y) Distract`, `(R) Deactivate Focus` in the band) and frame1191 (vr: the band
  empty), `PROMPTS END vr: shows 942 (dropped 915, kept 27 [the log phase's], relabelled 0) ... 119 x516, 121 x324, 127
  x75`; 042105 `shows 357 (dropped 357)`. The relabel is unproven at runtime (no Activate/PDA prompt came up in the
  meadow); its offline test passes.

### 13.2 The buttons with the bow aimed at an orc (040227, SOMVR_GAMEMOVES=off, the guard on)
`bow where` before and after each press (Talion's root): A 32493.9,1873.2,-24797.0 -> 32492.6,1876.0,-24796.0; X ->
32491.8,1874.3,-24796.1 -> 32489.8,1873.9,-24795.0; Y -> 32489.7,1875.0,-24795.0 -> 32487.8,1874.4,-24793.0; L3 ->
32487.7,1875.3,-24793.0 -> 32486.0,1876.6,-24791.0: 1-3 cm per press, the orc's shoves at 1 m (the same drift between
presses). No `VIN press` line for A, X, Y or L3 (A: no pad A on foot; X: nothing on foot; Y: SOMVR_VISION=hold sees the
vision already on in the aim mode, `VISION END Y presses 1, taps 0`; L3: the sprint latch, no FreeRun without the
game's moves). The right stick click sent PDA 95: the map opened (ctx=menu), the aim ended (`aim over: Talion 17.4 cm
from where he stood at the enter (at most 17.9; one-frame jumps over 40 cm: 0)` - 25 s of the orc's shoves). B closed
the map.
- **`SOMVR_BOW_STRIKE=block|game`** (default block with SOMVR_BOW=1): while the bow is out, pad A and 121 are taken out
  of the input tick after every module (bo_guard). With GAMEMOVES=off it has nothing to cut from the buttons; it cut
  the test's own 121 (`bow fire 121`) at an orc with the Dash prompt up (042105: `strike guard: CombatPrimary04 121
  taken out ... 2 ticks cut`, Talion 1.0 cm). The move watch (every aim): `BOW ... moved:` for a one-frame jump over
  40 cm, `aim over: Talion N cm from where he stood at the enter` at the exit; 0 jumps in 8 + 8 aims.
- The positive control did not fire: `bow strike game` + `bow fire 121` released at the orc 9.4 m away reached the game
  (`VIN press cmd=121 CombatPrimary04 ... ctx=world`) and Talion did not move (0.4 cm). So a 121 tap from our input in
  the aim mode is not enough for the stock Shadow Strike (its input is data: a chord, a hold, the target lock - not
  traced). Nothing of ours sends 121 anyway.
- Focus (slow motion): the aim mode's own SlowMo component (world time, the view untouched); `(R) Activate/Deactivate
  Focus` toggles it on R3 (127), which nothing of ours sends: the player cannot switch it, it follows the aim.
  `SOMVR_BOW_SLOWMO=off` removes it. Fine for comfort (no view motion); a Settings row would be the way to offer it.

### 13.3 Our arrows: where they fly and why they missed
- 040227 (the orc at 0.8-1.4 m): the flight 22-48 deg off the ray with homing 0 and target 0, and 99-108 deg with
  homing 1 and the orc as target; 042105 (the orc at 9.4-9.6 m): the flight 0.0 deg off the ray, the arrow's aim
  position (`+0x1d4`) 117 m along it, `travelled 11700 cm`, no hit - even where the game's free-aim point was 9.3 m
  away, 0.9 deg off the ray (on the orc).
- Cause: FUN_02332c50 aims at `FUN_026c7cf0(sc)` = `P+0x214` for a player, else **`Combat+0x150`** - and our shots got
  Combat+0x150 (decompile: `*(sc+0x70)+0x40` vf_0xd8 / FUN_02693910 decide; round 1's "P+0x214 [high]" is wrong for our
  call). At range +0x150 was zero: `fireXf +Z x MaxRange` (117 m), so the arrow's segment never came near an AimAtNode
  sphere (the hit needs it, re-combat-items.md 1.3). Near an orc +0x150 is stale: the Combat_StartAttack wrappers copy
  the free-aim point there (`FUN_026edd40`), and Talion's recoils/attacks at 1 m start attacks: arrows to an old
  point, 22-48 deg off. The homing ones: the projectile's own target.
- Also found: the projectile's flight can't be steered after FUN_02332c50 returns: rewriting `+0x1c4` before its first
  update did not change the hit (040227: steered to the ray, the hit along the old line); the impact reads `+0x1d4`
  (the aim position) too - both are rewritten now (042105 shot r6: steered 8.4 deg).
- **`SOMVR_BOW_ASSIST_DEG=N`** (unset = round 1's; build 11: 5): at our shot the orcs within 40 m, each one's head (its
  `Head` node by GetNodeTransform, else the root + 80 cm) and chest (root + 40 cm); the one nearest the ray within N deg
  is the aim point, written to P+0x214 and Combat+0x150 for the call (put back after); none = straight along the ray
  (both zero). After the call, an arrow more than N + 1 deg off the ray (homing, a stale point) is turned onto it before
  its first update (quaternion, aim position, homing and target cleared). Logs `BOW t=.. assist: ...`,
  `BOW arrow .. steered`.
- **`SOMVR_BOW_HITLOG=1`** (somvr_bowhit_game.c, site BOW_PJ_HIT K_CALL `0x27bf8ba` -> `FUN_026c8e10`): the one call of
  Combat_ApplyProjectileHit in CProjectile's impact, logged for our arrows with the hit object, the hit node record and
  the attack (the projectile's own or an AttackOverrides match - the headshot is matched there, by RequiredHitNode,
  from the projectile's RangedWeaponData, ours = the stock arrow's). Test only.

### 13.4 The assist and the headshot at runtime (the orc spawned 10 m ahead, `SOMVR_PRACTICE_AHEAD_CM=1000`, no `bow orchp`)
| Run | Switches (+ PAD_PROMPTS=vr, HITLOG=1) | Shots | Result |
|---|---|---|---|
| 042105 | none (round 1's aim) | 6 at 9.4-9.6 m | 0 hits: the arrow's aim position 117 m along the ray, `travelled 11700 cm` |
| 043211 | ASSIST_DEG=5 | 6 at 9.4-9.5 m | **6 hits, 95 -> 85 each** (5 aimed at the head point 1.3-3.2 deg off the ray, 1 straight at 5.5 deg); the attack `PC_Ranged_Normal_Hit_Light_CharOrc` (the arrow's AttackOverrides match for an orc), hit node record '' |
| 044033 | + ASSIST_NODE (the head as Combat+0x170/+0x178) | 6 at 9.4 m | 6 hits, 10 each; the hit node record still '' |
| 044855 | + HEADSHOT_CM=20 | 6 at 4-9.5 m | 6 hits; 2 within 20 cm of the Head node (14.5, 13.2 cm): `PC_Ranged_Normal_Hit_Light_HeadOrc` passed instead - still 10 |
| 045743 | + HEADSHOT_CRIT=1 | 6 | 5 hits (the drive's aim 30-73 cm from the head), 1 miss at 2.9 m |
| 050551 | HEADSHOT_CM=20, the test word `bow aimhead` (the ray onto the Head node) | 6 at 1.9-9.3 m | 6 head hits (0.6-7.3 cm): HeadOrc 3x, HeadOrc + isCrit 3x: **every one 95 -> 85** |
| 051158 | the stock shot (`bow fire 125`) at the drive's aim | 5 | 1 hit: homing 1 on its target, the hit node record **'Bip_RightArm_Upper'**, CharOrc, 10; 4 misses (no target: the world at 9-12 m) |
| 051801 | + ASSIST_TARGET (the orc as Combat+0x4c0 for the call), `bow aimhead` | 6 at 3.0-9.1 m | **6 head hits: the hit node record 'Bip_Head', the game's own `PC_Ranged_Normal_Hit_Light_HeadOrc`, 95 -> 75 each** (our swap not needed) |
| 052409 | build 11's proposal (PAD_PROMPTS=vr, ASSIST_DEG=5; NODE and TARGET default), the drive's own aim | 4 at 5.4-9.4 m (+1 into the empty meadow) | **4 of 4 hits**: 'Bip_Head' + HeadOrc 3x (95 -> 75, 75, 65), 'Bip_Torso' + CharOrc 1x (95 -> 75) |

So the assist makes our arrows connect at range (0 of 6 -> 6 of 6). The head: CProjectile's impact (FUN_027be5d0,
the loop after `FUN_027c6150`) picks the hit node by comparing **the impact point** with each ProjectileHitNodes
entry's node (radius x scale); without a target our impact point is where the immediate ray meets the orc's
CharacterRangedHitBox, 1.2-1.4 m in front of the Head node (050551: the hit at 31733.8,1918.7,-24424.4, the head at
31608.2,1915.4,-24365.0): no node, the body's override, 10 - also with the head record swapped in or isCrit. The stock
shot's arrow homes onto its target (the shooter's Combat+0x4c0 -> the projectile's +0x140) and hits at the body
(051158: 'Bip_RightArm_Upper'). With the picked orc as our call's target (`SOMVR_BOW_ASSIST_TARGET`, default with the
assist) the game names the head itself ('Bip_Head'), picks its own head override and does **20 instead of 10** (051801).
That is the game's headshot for an orc in a fight (aware, charging): five arrows instead of ten. The one-arrow kill
the flat game shows is not reproduced here; it is presumably the stealth case (an unaware orc) or a skill - not tested
(every practice orc charges at once). `SOMVR_BOW_HEADSHOT_CM` (our own swap) and `SOMVR_BOW_HEADSHOT_CRIT` stay as
test switches, off.

### 13.5 Build 11's proposal end to end (052409) and the switches
Run 052409 (build 11's switches + `SOMVR_PAD_PROMPTS=vr SOMVR_BOW_ASSIST_DEG=5`, HITLOG for the evidence, the orc 10 m
ahead): the prompts (frame727, `padprompts log`: "Shadow Strike (A)" and "(R) Deactivate Focus" in the band; frame1237,
vr: the band empty), A/X/Y/L3 with the bow aimed at the orc (Talion 0-3 cm per press, 0 one-frame jumps, no command),
the right stick click the map, four shots at the drive's own aim all hitting (the hit node records the game's own:
head 3x, torso 1x; 20-30 each - with the target the torso hit does 20 too, 043211's 10 was the box-face impact), the
let-down (`let down: no shot (the draw reached ... min 20)`, the arrow kept), frame3951 (drawn at the orc 5 m away, no
flat prompt). 0 WARN, the install unchanged, the real save UNCHANGED (as in every run of this round).

For build 11's headset.env (on round 1's five bow lines):
```
SOMVR_PAD_PROMPTS=vr
SOMVR_BOW_ASSIST_DEG=5
```
Defaults that need no line: `SOMVR_BOW_STRIKE=block`, `SOMVR_BOW_ASSIST_NODE=1`, `SOMVR_BOW_ASSIST_TARGET=1`,
`SOMVR_PAD_PROMPTS_MAP=34:120,95:127`. Test only: `SOMVR_BOW_HITLOG`, `SOMVR_BOW_HEADSHOT_CM`, `SOMVR_BOW_HEADSHOT_CRIT`,
the words `padprompts ...`, `bow where|strike|aimhead|headcrit`.

### 13.6 Open
- The one-arrow headshot of the flat game: not reproduced on a charging orc (20-30 per head hit); try an unaware orc.
- The relabel (Activate as B, PDA as R3) never met a prompt at runtime.
- The Shadow Strike: a 121 tap inside the aim did nothing (042105); its real input and the wraith-dash presentation.
- VR words in place of the dropped prompts; the wraith wisps off the VR left hand (the brief's item 3) not started.
- Offline tests: test_bow unchanged; selftest pp_tests (the keep rule, the map parser, drops/relabel/hide/hold through
  stand-in slots, the words, a bad PP_HIDE puts PP_SHOW back, the strike guard) and bh_tests (the hit call through a
  stand-in call site); bo_assist_point and the headshot swap are proven at runtime only.

## 14. Build 12 (2026-10-25, headless; handoff/HANDOFF-wraith.md): the Shadow Strike with the wraith dash

Branch `t3/1cd1ac73`. Runs in logs/boot-check-20261025-: 063808 (the wisps' node tests; the strike step never aimed,
a driver bug), 064438 (the stock strike measured), 065308 (the hold, the stock again), 065907 (the driver too slow: the
orc came in before the press), 070448 (dash, blink, stock with the costs waived), 071430 and 072205 (the final code).
All on build 11's 76 switches (scripts/headset-build11.env) + `SOMVR_BOW_STRIKE=dash SOMVR_PRACTICE_AHEAD_CM=1000`
(+ `SOMVR_WISPS=hand SOMVR_XR_DUMP_WRAITH=3` from 071430) + the test switches; driver scripts/wraith-drive.sh, step
`strike`. Every run: 0 WARN, the install unchanged, the real save UNCHANGED.

### 14.1 Why round 2's 121 tap did nothing
Round 2 (042105) tapped 121 at the release while the strike's prompt was **hidden**: the `PROMPTS` lines show "Shadow
Strike (A)" (action 121 `UI_Wraith_Dash`) coming and going while aiming (it alternates with "(Y) Distract"); both taps
fell in a hidden phase. With the prompt up, X's 121 (0.15 s, through the binding layer as an ON/OFF pair) starts it at
once (064438, 065308, 070448 x3, 071430). With the prompt down the game refuses (065308 x2, 065907 every press, 071430 x2: the
orc 1-2 m away after charging in, or a spawn behind rocks after a strike): `STRIKE ... no move within 2000 ms of the
press (the prompt was not up at the press)`. The prompt is the game's verdict (range, target, line of sight, cost).
After one strike in a boot the prompt did not come up again for the next orc in 065308 and 071430; with the test word
`bow strike free` (the game's `Combat.DisableEnergyCost` and `DisableChargeCost` bools set) three strikes in a row
worked (070448): the strike costs something (the arrow count fell 8 -> 6 at the first strike, 065308; Focus or charge
not read). Real play: the game's rules decide; a refusal gives the "denied" buzz in the left hand.

### 14.2 What the stock strike does (064438, 065308: `bow strike stock` = X sends it, the view untouched)
- t+0: the attack `PC_WraithDash_KD_Start`; LOCO's owner `attack` (the lift below lets it move him).
- t+0.15..0.36 s: a **lunge** of 1.6 m toward the orc (root motion: 68, 41, 20, 25 cm per 50 ms frame, up to 14 m/s).
- t+0.36 s: the sync `PC_WraithDash_KD_Bip_Standing_F` (the orc's side `AI_Bip_PC_Sync_WraithDash_KD_Standing_F`):
  the body moments (SOMVR_BODYMOMENTS) **hold** the view (`act first -> hold by sync`).
- t+0.41..0.46 s: a **teleport** of 7.7-7.8 m in one frame to the orc (`ANCHOR ... TELEPORT (re-latched)`), 30-80 cm
  lower or higher with the ground.
- the knockdown sync runs ~3.6 s (Talion's root drifts ~0.5 m in it); the orc ends on the ground at Talion's feet
  (071430 frame 2064). Its health: 95 -> 95 (071430: `STRIKE ... the orcs 4.5 s after the press`): at this save's
  skills the Shadow Strike knocks down, it does no damage (what the flat game does with this save; the kill upgrades
  are skills).
- the stock presentation in VR: the view rides the lunge (the game moving the view: 68 cm in a frame), then the body
  moments hold it **8.2 m behind** at the lunge's end while Talion is at the orc, and release it 3.6 s later by a
  **blink** (`released by a blink (821.5 cm from the held point)`). Not comfortable, not readable.

### 14.3 Built: `SOMVR_BOW_STRIKE=dash|blink` (somvr_strike.h/.c pure, somvr_strike_game.c; default `block`)
- **The press**: X (the left lower button, `SOMVR_BTN_LOWER`) while the bow is out (aiming or drawn) holds 121 for
  0.15 s (`sk_tick`, after the bow's tick); bo_guard lets our own 121 through and still cuts pad A. X without the bow
  does nothing (counted). The prompt: kept with the bow out and shown with pad X's glyph (CombatPrimary01 118: "(X)
  Shadow Strike"; the relabel counts the HUD's three diamonds per key so each hide matches its show; logs `shown as 118`).
- **LOCO**: SOMVR_LOCO_HOLD=attack (build 11) would play the lunge in place; from the press until the dash ends the
  hold is lifted (`sk_lift`) so the game's own strike carries him (the teleport is the sync's own).
- **The view** (`sk_anchor`, after SOMVR_ANCHOR=root's step; position only, nothing turns the base): armed by the press
  for 2 s; a frame of the anchor moving faster than 600 cm/s (the lunge) **holds** the view where it was; a frame moving
  more than 100 cm (the teleport), or 0.8 s of holding, starts the **dash** from the held point to the anchor's point
  now: V = A0 + (P - A0) f(tau), f the eased constant-speed profile over 300 ms with 60 ms ends (the speed rises and
  falls as half cosines: no step in velocity); 9.2 m = 38.5 m/s at constant speed. `blink`: the same hold, then the view
  stays until the white is up (30 ms), jumps under full white, the white falls over 70 ms. No move in 2 s: the "denied"
  buzz, nothing moved. Switches: `SOMVR_BOW_STRIKE_MS` (300), `_EASE_MS` (60), `_TUNNEL` (0.4), `_WINDOW_MS` (2000),
  `_HOLD_MS` (800).
- **The body moments' hold** (the knockdown sync) takes our view point each frame of the hold and the dash
  (`sk_bm_follow`), so it holds where the dash arrived and releases by an **ease** (38 cm, 071430; 37.5 cm, 070448)
  instead of the blink from 8 m behind.
- **The wraith tunnel**: somvr_comfort's entry carries `SOMVR_CFF_WRAITH` with the amount in the flags' bits 16..23
  (`somvr_cff_tunnel_bits`; 0.4 x the speed's share) and the blink white; the layer draws the ring from a second,
  white-blue premultiplied image (`somvr_vig_image_wraith`, linear 0.62/0.80/1.0, made at the first dash) at least at the
  tunnel's amount (the vignette's own if larger); a layer without it draws its black blink and no tunnel. Dumps name the
  quads `comfort ring wraith` / `comfort blink wraith` (xr_compose.py draws them white-blue);
  `SOMVR_XR_DUMP_WRAITH=N` (tests) dumps the frame after each dash's first.
- **Lines**: `CONFIG strike=...`, `STRIKE t=... press|hold|start|end|no move|the orcs ...`, per stereo frame from the
  press until 1 s after the dash `STRIKE f=N t=... ARMED|HOLD|MOVE|idle root ... target ... view ... eye ... step S
  (view V) cm yaw ... base yaw ... f ... speed ... tunnel ... blink ... prompt P` (the eye = eye 0's world position the
  game rendered), `STRIKE END ...`; the layer `xr: comfort frame N: the wraith dash begins|ends`.
- Test words (headless only): `bow strike block|game|dash|blink|stock` (stock = X sends it, the view untouched),
  `bow strike free|cost` (the game's energy/charge costs off/on); `bow where` names the prompt's state.

### 14.4 Proven (headless, 20 Hz)
Per-frame traces: `python3 -I scripts/strike_trace.py RUN/somvr.log T0` tabulates the `STRIKE f=` lines (frame dt, the eye's step,
the view's step, the speed); pictures `scripts/xr_compose.py RUN -f N` (this branch's copy draws the wraith quads).
| Run | What |
|---|---|
| 064438 | the stock strike (`bow strike stock`): the lunge 68/41/20/25 cm per frame, the teleport 780 cm at 0.46 s, the view held 8.2 m behind by the body moments, released by a blink 3.6 s later (821.5 cm) |
| 065308 | the hold (stock strike; the dash's state machine): `hold` at 0.153 s, `start` at 0.454 s (771 cm jump, the dash 925.7 cm); two more presses refused (no prompt: the orc in at 1-2 m) |
| 070448 | **`SOMVR_BOW_STRIKE=dash`**: held 5 frames at the press point, the dash 924.0 cm in 7 frames (78, 192, 203, 197, 204, 97 cm: on the line 0.00 cm, at the designed point 0.00 cm, the eye's step = the view's), the white-blue tunnel on the 6 dash frames (`xr: comfort frame 2366: the wraith dash begins`), the body moments released by an **ease** (37.5 cm); the blink (945 cm under the white, then 10 cm); stock again; the costs waived (`bow strike free`) |
| 071430 | the final code: the dash 922.7 cm; the orc after it: health 95 -> 95, knocked down at Talion's feet (frame 2064), the release by an ease (38.3 cm); the mid-dash dump (2042: the ring; the composer then drew it black, fixed); two presses refused (no prompt) |
| 072205 | **the final proof** (the costs waived): dash, blink, dash all struck (`moves 3, dashes ended 3, no move 0`); dash 1: held 6 frames, 924.2 cm at 15.7 / 39.4 / 40.3 / 40.7 m/s then the last 29 % in a 100 ms frame (a headless hitch), the eye's step equal to the view's every frame, the base yaw -60.00 throughout; the blink: held, 945.7 cm in the frame at blink 1.00, then 0.31, 0; dash 2: 937.2 cm, the same shape; the body moments released each hold by an ease (39.3 cm); pictures 2043 (mid-dash: the white-blue tunnel, the orc ahead, the game's wraith at it), 2069 (the orc down at our feet), 2963 (the blink fading), 3838/3859 (dash 2); the prompt "(X) Shadow Strike" (`shown as 118`, 12 hides matched) |

What the trace proves (072205, 070448): no rotation (the base yaw constant; the eye's own yaw drifts 2.8 deg/s before,
during and after = the headless head pose), no step over the design (the view = the designed point to 0.01 cm, the
eye's step = the view's step + the head's 0.3 cm), no move before the teleport but the lunge's first 3-8 cm (two frames
under the 600 cm/s threshold), the hold through the lunge (0.3 s at the press point). The 60 ms eased ends are one
headless frame (20 Hz): only the headset shows them.

### 14.5 For a later build's env (the coordinator's call)
```
SOMVR_BOW_STRIKE=dash
```
(`blink` is the option; `SOMVR_BOW_STRIKE_MS`, `_EASE_MS`, `_TUNNEL`, `_WINDOW_MS`, `_HOLD_MS` stay unset.) Needs
build 11's SOMVR_BOW=1, SOMVR_INPUT=1, SOMVR_ANCHOR=root, SOMVR_BODYMOMENTS=1 (all in headset-build11.env). Never in a
player's env: `SOMVR_XR_DUMP_WRAITH`, the words `bow strike free|cost|stock`. The wrist menu's Controls page: no row
(the page is static and the code's default stays `block`: a row would name a control the default build does not have;
add "Strike: X, bow at an orc" with the default's change). The Controls page's "Not yet" note still lists Shadow Strike
(not changed: it drops with the default's change, the coordinator's call).

### 14.6 Open
- The headset: the dash's speed (9 m in 0.3 s = 38 m/s at constant speed: Jet Island fast, the user's "fast is fine
  if smooth"), the tunnel's 40 % and colour, the 0.3 s hold through the lunge (the view still, Talion's bow and our
  hands with us), the blink option.
- (Answered in 14.7: 2 arrows, no blocking cost; the refusals were the position.) The strike's cost and cooldown: after a strike the prompt did not come back for the next orc in two of three boots
  without the cost waiver (the arrows 8 -> 6 at a strike; which energy it spends not read). In play the game decides;
  the refusal buzz says so.
- (14.7: hidden in the eyes with the body; `hands` keeps them on the controllers.) The game's arms during the hold and the dash: the rig is up to 9 m from Talion, the IK targets are clamped and
  SOMVR_ARMS_STRETCH puts the hands on them (the bow stays in our hand); not looked at closely.
- Not tested: a strike with the bow drawn (X while drawing: the code sends it the same), a strike at a captain, the
  kill upgrades, a strike into the ivy or up a wall (the arrival's height: 30-80 cm differences seen, eased by the dash).

### 14.7 Round 2 (handoff/HANDOFF-wraith.md "Round 2"; branch `t3/08c59a22`): Talion seen from outside, the cost
Runs logs/boot-check-20261025-080201, 081048, 082001, 082932, 083545 (build 11's switches + `SOMVR_BOW_STRIKE=dash
SOMVR_WISPS=hand SOMVR_PRACTICE_AHEAD_CM=1000 SOMVR_HP_AWAY=hands SOMVR_XR_DUMP_MARKED=N` + the test switches; driver
scripts/away-drive.sh). Every run: 0 WARN, the install unchanged, the real save UNCHANGED.
- **Why the eyes saw him** (072205 frame 2043): H4 hid his nodes and let the layer draw our hands only on
  `head_ok && !bm_head_away() && !fb_third()`. The strike's sync freezes the view (body moments) from 0.36 s to ~4 s
  with his head 0.5-9.3 m away (~55 cm after the dash: the knockdown pose), so nothing was hidden for the whole sync;
  the teleport's frame is on the camera base (`head_ok` 0); and each press makes a skeletal node of the wraith's mesh
  0x158f5f40 with transforms of its own (SOMVR_HP_POSE cannot reach it). `fb_after_body` gave the arms to the animation.
- **`SOMVR_HP_AWAY=off|hide|hands`** (default off = build 11; word `hpaway`): a frame held away (a body moment's hold, the
  strike from the press to the dash's end; not third person) hides the body node, the pose's nodes, the cloth, the
  put-away weapons, every skeletal node of one of his meshes (learned from the pose rule's skips: 7-8 nodes, 3316
  forward / 3220 G-buffer skips in 081048) and a weapon whose arm is not on our controller. `hands`: such a hold keeps
  the arms' rig, so the arms (stretched to 923-936 cm at the teleport, hidden with the body) and our VR hands stay on
  the controllers and the bow stays in our hand (081048 strike 5: every frame; VR hands 201 of 201). Shadows kept.
- **Pictures** (both eyes, `scripts/xr_compose.py RUN -f N`): build 11's look reproduced in 082001 (off: 1200 mid-dash
  the white wraith-Talion at the orc, 1204 the arrival his white hair before the eyes); hands: 081048 645 before, 649
  mid-hold, 653 the start, 655 mid-dash, 680 the sync, 724 after; 080201 625-703; 082001 1733 mid-hold; 082932 the
  arrival 653. No part of Talion or his arms in any of them; the orc, the world, our hands, the bow, the wisps; the game's
  strike FX stay (a swoosh in the lunge, a streak at the teleport).
- **The cost**: 2 arrows per strike (8 -> 6, also with Combat.DisableEnergyCost/DisableChargeCost set). Nothing else
  blocks a second strike: after one at the game's costs the prompt was back within ~1 s (081048), six strikes 27 s apart
  all struck, no cooldown seen. Under 2 arrows the prompt still shows and nothing happens (1: 082932, 0: 082001). The wraith round's
  refusals: the orc behind the rock after a refused `practice go` (080201 frame 1997), or too close.
- **The denied buzz**: now at the press when the prompt is not up or fewer than 2 arrows are left (080201 presses 2-4;
  083545), at the 2 s window's end only when the prompt was up with the arrows there; before it always came 2 s late.
- For a later build's env: `SOMVR_HP_AWAY=hands` (with `SOMVR_BOW_STRIKE=dash`, `SOMVR_WISPS=hand`).
