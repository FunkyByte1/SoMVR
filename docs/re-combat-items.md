# Shadow of Mordor (Feral Linux build): combat items for VR - projectiles, sword FX, the dagger, brutalize, sounds

Static analysis only (2026-10-19 evening): `objdump` ranges of `reference/bin/linux/ShadowOfMordor`, the TSV indexes in
`reference/index/`, earlier Ghidra output in `reference/work-combat/o2..o5.txt` (no new Ghidra run: a VR session was on),
the schema dumper `reference/work-combat/schema.py`, and run 3's `attacks.tsv`
(`logs/boot-check-20261019-190441/probe-out/`, record names only). Nothing was launched. Scratch (git-ignored):
`reference/work-combat-items/` (`dis.py` = annotated objdump of one function, `site.py` = SITE lines, `d_*.txt`).
Conventions as in `re-combat.md`: **[high]** read in the code, **[med]** meaning inferred, **[low]** guess, **RT** = needs
a boot. `objsys = *(void**)0x466afa0`, `shell = *(void**)0x46a9560` (`CGameClientShell`), `Combat*` as in re-combat.md,
`inv = Character_GetInventory(char)` (`0x2699990`, `CCharacter+0x910`). Record field `i` of a schema `*G` sits at
`G+8+0x30*i` (state int at `+0x18+0x30*i`, index byte at `+0x28+0x30*i`); names below come from `schema.py`.

Answers the design-vr.md section 3 markers: projectile hit path, sword FX, dagger shown / detached, brutalize, Wwise
events. AI attack events, hit-stop and animation key times are in `re-combat-vr.md` (another helper), not here.

## 0. Summary

| Item | Answer | Conf. | Sites |
|---|---|---|---|
| **Projectile hit** | `CProjectile` flies itself: per frame `CProjectile::vf_13` -> `FUN_027c53f0` -> move + ray/shape cast (`FUN_027c3ef0`) -> impact `FUN_027be5d0` -> the target's `CharacterRangedHitBox` is mapped to its owner, the target's current attack may refuse projectiles, the segment must pass one of the target's `AimAtNode` spheres, else the arrow flies on -> `FUN_026c8e10` (= a DealHit for projectiles) -> `ServerCombat_ApplyAttackDamage`. Block/deflect: a pre-hook on the update slot sees every live projectile with position, direction (its quaternion's +Z), speed and target; setting `+0x228` removes it, rewriting the quaternion redirects it | high (path), med (fields) | `PJ_UPDATE`, `PJ_CREATE`, `PJ_IMPACT`, `PJ_DAMAGE` |
| **Sword FX** | the game's own recipe is the `WEAPONFX <fxfile>` animation key: `CLIENTFX_CREATESTRUCT` ctor `FUN_00a09e70(cs, name, modelHandle, flags, parentObj = the active weapon's model object)` + create `FUN_00d8c7f0(*(shell+0x80c78)+0x30, &handle|0, cs, 1, 0, 0)`. Our code can do the same with any FX file and keep the handle. FX names are data, not in the binary (RT: log them). The stock "ready" glow most likely comes from data too (`Combat.BuffCounter.HitCounterBuffs` -> `Combat.Buff` with `ClientFXAttachItemSlot`), so our G8 hits may get it for free | high (calls), med (glow) | `FX_WEAPONFX_*`, `FX_CREATE*`, `FX_MELEEFX` |
| **Dagger shown** | weapons are per-item model objects attached by the item's node hashes: model entry `+0x30` = node while active (ActiveNode), `+0x34` = while inactive (InactiveNode); `FUN_0231e2e0(item)` re-attaches with the game's own code. `ACTIVATE_MELEEAUX` = inventory slot 1 (the dagger, med). For the left hand: write `+0x34` = hash of `L_Weapon`, call `FUN_0231e2e0`; restore to sheathe | high (code), med (slot = Acharn), RT (visibility) | `ITEM_REATTACH`, `INV_ACTIVATE`, `ITEM_VISIBLE` |
| **Dagger detach** | `CSimpleModel::vf_43(parentSm, hChild)` = DetachChild (`FUN_0235b3d0`); re-attach = `FUN_0231e2e0(item)` again. The `DETACH` key is unrelated (it ends an attack's `TargetCoupling`, `Combat+0x540`) | high (code), med (names) | `SM_DETACH`, `SM_ATTACH`, `ITEM_REATTACH` |
| **Brutalize** | the "Torment" attack family (`PC_Bip_StealthTorment_B/F`, `_Hang`, `_Dangle`, `_Sitting`, `Wall_Torment...`, 120 records) next to the stealth kill (`SneakInstaKill`) and stealth brand families. Paired = the record's `AnimationData.Sync` (field 8 of `*0x4ba8640`) is set; then `Combat_StartAttack` starts `FUN_028081a0` (G14) and both bodies are aligned. The kill family has victim records (`AI_Bip_PC_Sneak_IK_*`), the torment family none by name. The fear is a separate morale attack (`Atk_C_BrutalK_3xMorale_Rune`) | med (family), RT (Sync set?) | (G14 sites in re-m1m2.md) |
| **Sounds** | `CSoundMgr::vf_13(mgr, "event")` (slot `0x346da80`) = `AK::SoundEngine::PostEvent` (`0x1467870`) on the manager's 2D emitter "SoundManagerGlobalEmitter". 3D: register our own Wwise object (`0x1463890`), set its position with the game's wrapper `FUN_008c9b80` (also sets the `gp_distance` RTPC) and post on it. Event-name literals in the binary: `ui_lastchance_fail`, `ui_lastchance_succeed`, `ui_game_back`, ...; combat sounds (clank) are data (`ModelRecord.AudioBlockName`/`AudioAttackName`) | high (calls), med (AK names) | `SND_*`, `AK_*` |

## 1. Projectiles (archers' arrows, thrown spears)

### 1.1 The path

| Step | Where | What | Conf. |
|---|---|---|---|
| Spawn | `FUN_02332c50` (re-combat.md 4.2) -> objsys vtbl+0x50 create "CProjectile" | user data `{+0 inv, +8 shooter Combat*, +0x10 ranged item, +0x18 accuracy, +0x20 aim pos, +0x30 riding object, +0x38 node}` | high |
| Init | `CProjectile::vf_09` `0x27c3a50` (slot `0x3998728`) -> `FUN_027c2810` (transform, ranged data, spline points) | fills the fields of 1.2; target = the shooter's `Combat+0x4c0` at fire time | high |
| Per frame | `CProjectile::vf_13` `0x27c5d20` (slot `0x3998748`): if `+0x228` (done) -> objsys vtbl+0x58(obj) (remove); else `FUN_027c53f0(proj)` (call `0x27c5d6a`), then objsys vtbl+0xb0(obj, &proj+0x1b8) (object transform) and the same for the riding object `+0x160` (the spear model) | high (code), med (vtbl names) |
| Move + cast | `FUN_027c3ef0(proj, &hit, &frac, time)`: `step = min(speed*dt, maxRange - travelled)` along `rotate(quat, +Z)`; spline mode (`+0x22e`) evaluates a quadratic Bezier over `+0x1e4..+0x204`; gravity (`+0x22d`, weight `+0x224`); ray cast `IEnginePhysics` vtbl+0x200 at `0x27c43b7` (or the shape cast); true = something was hit | high (step, cast), med (gravity/spline) |
| Impact | `FUN_027be5d0(proj, ProjHit* hit, const vec3* segFrom, const vec3* segTo)`, called from 6 places: the flight (`0x27c57f7`, `0x27c5b73`), the hit wrappers `FUN_027c1e60` (shape cast), `FUN_027c20e0`, `FUN_027c2210`, `FUN_027c2340`, and `FUN_027c1d90` (ImmediateImpact from the fire call). The wrappers restore the old transform unless `+0x228` became 1 | high |
| Damage | `FUN_026c8e10` [`Combat_ApplyProjectileHit`] (only call `0x27bf8ba`) -> `ServerCombat_ApplyAttackDamage` (`0x26c8eb4`) | high |

`ProjHit` (the ray result): `+0x00` pos vec3, `+0x0c` normal vec3, `+0x18` float, `+0x20` hit object (HOBJECT), `+0x28` u16
node (-1), `+0x2a` byte **[med]**.

`FUN_026c8e10(Combat* shooter, ProjHit* hit, HRECORD attack, HRECORD rangedItem, -, HRECORD hitNode, bool mayIncHitCounter,
bool isCrit)` **[high: disassembly]**: `HitInfo_Init`, `hit.pos = hit->pos`, `HitInfo+0x28 = hitNode`; `host =
Object_GetCombatHost(hit->obj)`; character host: `ServerCombat_ApplyAttackDamage(shooter+0x4a0, host+0x70, &hitInfo,
attack, rangedItem, isCrit, forceKill = 0, mayIncHitCounter)`; prop host: `host+0x78` vtbl+0x50; none: `FUN_02812270`; a
player shooter (`+0x88`) gets the target's `AwardItemOnHitByPlayer`; then `FUN_026c06b0` (AOE on hit). `attack` =
`proj+0x190` (the shooter's attack at fire) or an `AttackOverrides` match (head shots, re-combat.md 4.2).

### 1.2 `CProjectile` fields (`GameBase`, vtable `0x39986e0`)

| Off | Meaning | Conf. |
|---|---|---|
| `+0x58` | shooter `Combat*` | high |
| `+0x60..+0x68` | objects already hit (32-byte refs; a hit object in it is skipped) | med |
| `+0x78..+0x80` | pass-through list: objects the projectile flew past (1.3) | med |
| `+0x120` | shooter owner HOBJECT (`FUN_02310250(inv)`) | med |
| `+0x140` | target HOBJECT (shooter `Combat+0x4c0` at fire; cleared when it is a dead character) | high (code), med (meaning) |
| `+0x160` | riding object moved with the projectile (`UseCombatAttachedObject`: the thrown weapon's model) | med |
| `+0x188` / `+0x190` / `+0x198` / `+0x1a8` | ranged item record / the shooter's attack at fire / accuracy / `RangedWeaponData` record | high |
| `+0x1b0` | spawn time (double) | med |
| `+0x1b8` | position vec3 (cm); `+0x1c4` quaternion (x,y,z,w): **flight direction = rotate(quat, +Z)** | high |
| `+0x1d4` | aim position vec3; `+0x1e0` target node | high / med |
| `+0x1e4..+0x204`, `+0x208` | spline control points (3 x vec3), spline timer | med |
| `+0x210` | speed cm/s (`Velocity`, default 100) | high |
| `+0x21c` / `+0x220` | travelled distance / `MaxRange` (default 10000) | high |
| `+0x224` | `GravityWeight` (default 10) | high |
| `+0x228` | done: vf_13 removes the object at its next call | high |
| `+0x22b` | `ImmediateImpact` | high |
| `+0x22c` | `EnableHoming`; cleared at the next step while the target's `Combat+0x56b` is set | high (code), low (+0x56b = `CancelProjectileHoming`) |
| `+0x22d` / `+0x22e` / `+0x22f` | `EnableGravity` / spline movement / crit | high / med / high |

### 1.3 Where impact is decided (and the game's own outs)

In `FUN_027be5d0`, in order **[high for the code]**:

1. A hit on a `CharacterRangedHitBox` (class descriptor `0x4c42da0`, name string checked) is replaced by its owner:
   `hit->obj = *(*(box+0x38)+0x28)+8` (`0x27be621`). The arrow's ray hits that per-character box (created by
   `CCharacter::vf_10`, re-combat.md 5), not the bones.
2. Already in `+0x60` -> skipped.
3. **The target's current attack decides**: `Combat_GetCurrentAttack(target)` (`0x27bec3c`) -> `AttackData`
   (`*0x4ba7280`) field 24 `CanBeHitByProjectiles`; if false, the projectile's attack category must be in field 25
   `RequiredHitProjCategories`, else return (no hit). So stock dodges/vaults whose record clears the flag are immune
   (RT: which records).
4. For characters with an `AimAtNode` setup (`FUN_027c6790(Combat+0x4c8)`): the segment `segFrom..segTo` must pass
   within `Radius * scale` of one of the setup's nodes (`FUN_027bb490`, log `[COMBAT|PROJ_HIT]`, switch byte
   `0x4c473d8`); the closest `ProjectileHitNodes` entry becomes the hit node. **No sphere hit -> the object goes into the
   pass-through list `+0x78` and the arrow flies on** (`LAB_027be672`).
5. `FUN_026c8df0(targetCombat)` (`Combat+0x78` vtbl+0x88) false -> also passes **[low: meaning]**.
6. Damage (1.1), then `NeverStop` -> keep flying; else `+0x228 = 1`, impact FX (`FUN_027be130`), sticky projectile
   (`StickyProjectileData`, manager `*0x4c4cfd0`), object removed.

VR consequence: **a real dodge works with no code** as long as Talion's bones (the `AimAtNode` spheres: head, torso)
follow the player's body; the ranged hit box follows the character object (body-follow, 4 cm threshold in combat).

### 1.4 VR hook: block or deflect with the blade

- **`PJ_UPDATE` pre-hook** (slot `0x3998748`, every live projectile once per frame, game thread **[med, RT]**): skip if
  `+0x228`; take `p = +0x1b8`, `d = rotate(+0x1c4, +Z)`, `v = +0x210`; predict this frame's segment `p .. p + d * v * dt`
  (one frame extra as margin; `dt` from the game's frame time, slow-mo included); test it against the blade capsule swept
  from last frame's pose. Only projectiles near the player (or `+0x140` == the player) need the test.
- **Block** (first version): set `+0x228 = 1` -> the same vf_13 call removes it before it moves; we play the spark FX
  (section 2 recipe at the contact point) and the clank (section 5) and the "blocked" haptic. No damage, no stick, no
  impact FX of the game.
- **Deflect** (later): write `+0x1b8` = the contact point + 5 cm along the new direction, `+0x1c4` = a quaternion whose +Z
  is the reflected (or aimed-back) direction, clear `+0x22c` (homing), `+0x22e` (spline), `+0x140` (target). It then flies
  and impacts by the game's rules. To hurt orcs it must count as the player's: `+0x58` = the player's `Combat*`
  (otherwise `ServerCombat_ApplyAttackDamage` step 4 rejects orc-on-orc by alignment) **[low: side effects of +0x120,
  +0x190 untested]**.
- **Safety net / log**: `PJ_DAMAGE` (`0x27bf8ba`) sees the final victim (`hit->obj` already resolved) before damage;
  `PJ_CREATE` (vf_09) gives spawn, shooter, target and the aim point for the ground-arrow/edge-marker warning.

### 1.5 Risks

- Speeds are data (`Velocity`); fast arrows cross 1 m per frame, so the test must use the swept blade and the predicted
  segment, not positions.
- Homing re-aims every step: clear `+0x22c` on deflect, or it turns back to the target.
- Thrown spears: CProjectile with a riding object is expected (`UseCombatAttachedObject`, `+0x160`), not proven (RT).
- `ImmediateImpact` weapons (`+0x22b`) hit their target at once from `FUN_02332c50` (`FUN_027c1d90`): no flight to block.
  Only the damage hook can cancel them.
- The pass-through list and the hit list are WBObjRef lists: do not edit them; only the scalar fields above.

### 1.6 Probe (new words for `mod/probe`, extends runtime-probes C8)

`projlog` (R): log `PJ_CREATE` (shooter/target names, ranged record, speed, range, homing/gravity/spline/immediate bytes)
and per frame for projectiles aimed at the player (pos, dir), and `PJ_DAMAGE` (victim, hit node, attack); turn on the
`0x4c473d8`/`0x4c473d3` logs. Answers: arrow speeds and flight times (warning lead time), thrown spears = CProjectile?,
homing in use, which stock moves set `CanBeHitByProjectiles` false. `projblock` (A): set `+0x228` for the next
projectile within 1.5 m of the player: gone, no damage, nothing left behind? `projreflect` (A): the deflect recipe with
and without the `+0x58` swap: does it fly back, does it hurt the archer?

**Runtime result (P7, 2026-10-20, `logs/boot-check-20261020-001117`; runtime-probes.md P7 results).** Holds: thrown
spears (`AI_Infantry_ThrowProjectile_Troll`, `AI_Spearman_Infight_ThrowProjectile`) and the cave troll's rocks are
`CProjectile`s: the update slot ran from 1.7 s after the throw began, then `PJ_DAMAGE` and in the same frame the HITEV
of the throwing attack (25 and 100 damage); FX `projectile_spear.fxp`, `troll_rockthrow_projectile.fxp`. Not answered:
no projectile came near Talion after `projlog on` (no archer), so the speeds, homing, `projblock` (+0x228) and
`projreflect` are still open (0 blocked, 0 reflected in 2 x 30 s). Next: a place with archers shooting at Talion.

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results).** Two
archers set on Talion by `aggro 2 Archer`.
- Arrows are `Orc_Archer_Proj`: speed 2800 cm/s (48 cm per frame), range 20000 cm, `gravity`=1 with weight 10 (no
  visible drop over 4 m), homing 0, spline 0, immediate 0. They spawn at the attack's `APPLYDAMAGE` key: 2.63-2.65 s
  into `AI_Archer_Shoot*`, 82 ms after its parry window closes. From 10 m that leaves ~0.36 s of flight.
- Arrows hit Talion (`PC_RightArm`) or whoever stands in the line (an orc once).
- Thrown spears are `Orc_Spearman_Proj`: 3000 cm/s, range 2000 cm.
- **`projblock` (+0x228 = 1): pass.** The arrow was gone at its first update (0.55 m from Talion, point blank): no
  `PROJHIT`, no damage, no recoil.
- **`projreflect ... own`: it flies back and counts as the player's**, but missed. A spear was turned at 2.58 m
  (aimed at the spearman's root + 120 cm; homing, spline and target cleared; the shooter = the player's Combat). It
  flew straight through that point at 30 m/s without hitting him: either he had moved in the 0.33 s, or he was on the
  spear's pass-through list (1.3 step 4: a miss of his `AimAtNode` spheres, or the thrower). 1.75 s later it hit
  another object ~52 m away, and that `PROJHIT` is credited to the player.
- So a deflect needs a lead on a moving shooter, or homing on him (`+0x22c` with `+0x140` = him), and the pass-through
  list has to be checked for the thrower.

## 2. Sword FX (the "ready" glow)

### 2.1 The create call

```
void   FUN_00a09e70(CLIENTFX_CREATESTRUCT* cs, const char* fxFile, void* modelHandle, uint flags, HOBJECT parent)  [high]
long   FUN_00d8c7f0(CClientFXMgr* mgr, FxHandle* outOr0, CLIENTFX_CREATESTRUCT* cs, bool start, bool, ?)    [high: calls]
void   FUN_009ec750(CLIENTFX_CREATESTRUCT* cs)                                     // destructor             [high]
```

- `FUN_00d8c7f0` returns 0 unless ClientFX is enabled (byte `0x46b72d1`), then jumps to `FUN_00d8bae0` (the real create,
  70 callers) **[high]**.
- Managers: `*(shell+0x80c78)`, `+0x80c80`, `+0x80c88` (the create uses `mgr+0x30`); `FUN_0229f390(override, obj)` picks
  one by `MGROverride` 1/2/3 or, with 0, `FUN_023361e0(obj)` (`+0x80c88` for objects of class `0x46b6a60` whose game
  object has `+0x118`, else `+0x80c78`) **[high: code]**; which is Simulation/RealTime/PlayerTime/Hierarchy time (the
  `FUN_009ea000` init strings) **[low]**.
- `CLIENTFX_CREATESTRUCT` (from the ctor and `StartClientFX`) **[med]**: `+0x000` FX file name (0x104 chars), `+0x104`
  flags (1 = loop, 2 = no smooth shutdown; from `Combat.MeleeFX` Loop/SmoothShutdown), `+0x108` parent HOBJECT, `+0x118`
  node (-1 = object root) and `+0x11c..+0x134` offset (pos 0, quat 0,0,0,1) **[low: node/offset]**, `+0x178` model
  handle (objsys vtbl+0x118(parent)), `+0x180` target object with `+0x194` = 1, `+0x1b0` FX context count + `+0x1b2` up to
  12 shorts (from `Combat+0x2a0`).
- Handle: an 8-byte slot. Stop: `FUN_02aeba60(*handle, 0, 0)` (the `HIDEWEAPONFX` path; `FUN_0231e700` uses the same call
  with 1 to show) **[low]**, release `FUN_02aebfd0(&handle)` **[med]**; `Combat.MeleeFX` stops by name hash
  `FUN_00d8d530(mgr, hash, 1)` **[med]**.

### 2.2 How the game puts an FX on a weapon (four routes)

| Route | Code | Parent | Conf. |
|---|---|---|---|
| `WEAPONFX <fxfile>` animation key | `Combat_OnModelString` ~`0x26fc6c4`..`0x26fc7c8`, player only (`Combat+0x2b8`) | `FUN_0231e150(FUN_02311e20(inv))` = the active weapon's first model object; flags 0 (one shot), no handle | high |
| `Combat.MeleeFX` record (`*0x4bb1320`: SetTargetObject, **AttachToActiveWeapon**, **AttachToWeaponModelIndex**, FXName, Loop, SmoothShutdown, FireAndForget, ClientFXContext, MGROverride) | `FUN_026e6a10(Combat*, record)` [`Combat_StartMeleeFX`] (from `Combat_OnModelString`, `Combat_StopAttack`, `FUN_026e7070`); handle at `Combat+0x458`, record `+0x450` | `FUN_0231e170(activeItem, AttachToWeaponModelIndex)` or the owner | high |
| item `ModelRecord.LoadedFX` (field 10 of `*0x4c33f80`: list of {FXFile, Looping, SmoothShutdown}) | `SHOWWEAPONFX <i>` / `HIDEWEAPONFX <i>` / `HIDEALLWEAPONFX` keys in `FUN_0231fa90(item, ...)`; handles at `item+0x88[i]` | the item's model | high |
| buffs: `Combat.Buff` (`*0x4bafca0`) ClientFXSequence, HierarchyClientFXSequence, **ClientFXAttachItemSlot**; hit counter: `Combat.BuffCounter` (`*0x4baf260`) field 10 `HitCounterBuffs` -> `CombatHitCounterBuff` (`*0x4baf640`: HitCounterRange, HitCounterCharged, RequiredItem, Buff), field 18 `ChargeColors` | buff code (not traced) | an item slot of the owner | high (fields), low (that the glow uses it) |

The last row is the likely stock "execution ready" glow: a buff at a hit-counter range with its FX on the sword slot.
Our G8 hits run the hit counter (re-combat.md 1.5), so the glow may appear with no code **[low, RT]**.

### 2.3 FX names

Not in the binary (no FX file names in the strings; `Combat/FX/MeleeFX` is only a DB category). They live in the game
database (`FXName`, `FXFile`, buff FX fields) and in animation keys. **RT**: `fxlog` below.

### 2.4 VR recipe (game thread, G1)

```
item = FUN_02311e20(inv)                          // active InventoryItem* (the sword while drawn)
obj  = FUN_0231e170(item, 0)                      // model object 0 (FUN_0231e150 = the same for index 0)
mh   = objsys->vtbl[0x118/8](objsys, obj)
FUN_00a09e70(&cs /* >= 0x1c8 bytes */, fxFile, mh, 1 /* loop */, obj)
FUN_00d8c7f0(*(void**)(shell + 0x80c78) + 0x30, &handle, &cs, 1, 0, 0)
FUN_009ec750(&cs)
stop: FUN_02aeba60(handle, 0, 0); FUN_02aebfd0(&handle)
```

Fallback without names: the overlay glow (design-vr.md). Risks: the FX follows the item's model, so it moves with the
blade only if the sword model follows our hand (G11, re-animation.md 4) **[high]**; a looping FX must be stopped before
the item is sheathed or swapped (the item's own FX code does not know our handle) **[med]**.

Probe: `fxlog` (R): entry hook on `FX_CREATE_IMPL` logs `cs+0` (name), `cs+0x104`, `cs+0x108` (parent: the sword's
model object?), `cs+0x118` and the caller, through a fight to a full hit streak and an execution: that is the name
table, and it shows whether a glow FX starts by itself. `weaponfx NAME [loop|stop]` (A): the recipe above.

**Runtime result (P7).** Holds: 2.1's create path works from our code on the active weapon's model object (`weaponfx`
one shot `rc=1`, loop `rc=1` with a handle, `stop` -> handle 0, `clientfx=1`); the look is not confirmed (the drawn
sword was in front of Talion, the camera behind, and the name used is a trail). Names (`fxnames-55.tsv`, 23): the game
puts **`fx\characters\player\elf\elf_lightchain.fxp` on the sword at each stock strike** (FX site `0x26fcbd7`, at the
`BCOL` key, 313-320 ms in) and **`fx\common\blood\blood_level01_weapontrail.fxp` at each `Combat_DealHit`** (site
`0x273c4fb`, our hits too), plus impact sparks/splinters on the struck. No glow by itself: the streak only reached x1,
so 2.2 row 4 (the hit-counter buff) is still open. Next: a streak of hits within the combo window, and a shot from the
front.

## 3. The dagger: shown outside stealth kills, detached, re-attached

### 3.1 Weapon slots

The inventory has 8 weapon slot records at `inv+0x30 + slot*0x38` (`InventoryItem*` at `+0`), active slot `inv+0x280`
(re-combat.md 4.1). The model-string keys in `FUN_02315f40` map to `FUN_02315ca0(inv, slot)` [`Inventory_ActivateSlot`]
**[high]**: `ACTIVATE_MELEE` 0, `ACTIVATE_MELEEAUX` 1, `ACTIVATE_RANGED` 2, `ACTIVATE_RANGEDAUX` 3, `ACTIVATE_WRAITH` 4,
`ACTIVATE_QUICKTHROW` 5; `SHEATH`/`UNSHEATH` -> `FUN_023116f0(inv, activeItem, 1/0)`; `EQUIPITEM <rec>` -> `FUN_023157e0`.
Slot 0 = Urfael, 1 = Acharn (the dagger), 2 = Azkar by elimination **[med, RT]**.

`Inventory_ActivateSlot` (`0x2315ca0`): no item, no record (`+0x78`) or already active (`item+0xa2`) -> 0; the old active
item: `FUN_0231e3d0(old, 1)`, `FUN_02323ef0(old, 0)` (inactive: InactiveNode, its FX), `FUN_0231e0a0(old)`; then
`inv+0x280 = slot`, `FUN_02323ef0(new, 1)`, `FUN_0231e0a0(new)`, change event `FUN_02315540`, the weapon objects to the
owner's combat (`FUN_026d63d0`) **[high]**. One active slot only: the game never shows sword and dagger both "active".

### 3.2 Item model entries and the re-attach

`InventoryItem` **[high for the reads, med for names]**: `+0x28..+0x30` vector of 0x48-byte model entries: `+0x00` FX
handle ptr, `+0x20` model object (HOBJECT; `+0x38` = its game object), `+0x28` offset/socket data, **`+0x30` node hash
while active (ActiveNode)**, **`+0x34` node hash while inactive (InactiveNode)**, `+0x42` skip, `+0x43` socket-style
attach; `+0x70` owner `CCharacter*`; `+0x78` record; `+0x88..+0x90` LoadedFX handles; `+0xa2` active; `+0xa4` shown
(`SHOWWEAPON`/`HIDEWEAPON`); `+0xa5` visibility enable.

`FUN_0231e2e0(item)` [`InventoryItem_Reattach`] (`0x231e2e0`): `parent = FUN_02310270(Character_GetInventory(owner))`
(owner model, `*(inv+0x228)`); for each entry: node = `item+0xa2 ? e+0x30 : e+0x34` - **for a sheathable record
(`FUN_023303b0`) `item+0xa3 == 0 ? e+0x30 : e+0x34` instead: `+0xa3` is the sheathed flag** (2026-10-20,
re-xr-pipeline.md 21.1); `FUN_02affd20(e.obj->+0x38,
parent, node, 0, 0, 0, 0, 1)` (call `0x231e392`; it ends in `CSimpleModel::vf_42` AttachChild, re-animation.md 4) or
`FUN_02aff110` when `e+0x43`; then `FUN_0231dc30(item)` **[high]**.

`FUN_0231e700(item, entry|-1)` [`InventoryItem_UpdateVisibility`]: shown = `item+0xa5 && item+0xa4`; per entry the client
model's `FUN_02afee00(model, shown)` (or objsys visibility), the entry FX `FUN_02aeba60(fx, shown, 0)`, the combat
weapon registration `FUN_026d63d0` **[high: code, low: what +0xa5 means]**.

### 3.3 VR recipes

- **Dagger in the left hand while the sword stays drawn**: `d = *(inv+0x30+1*0x38)`; for each entry save `e+0x34`, write
  `e+0x34 = somhash("L_Weapon")` (node 97, child of `L_Hand`, run 3 `nodes-2.tsv`); `FUN_0231e2e0(d)`. If the sheathed
  dagger is hidden: `d+0xa4 = 1; FUN_0231e700(d, -1)`. Put back: restore `e+0x34`, `FUN_0231e2e0(d)`. The game keeps using
  our hash whenever it re-attaches the inactive dagger (slot changes), which is what we want while it is drawn **[med]**.
  Arm IK (G11) then carries it with the hand.
- **Thrown dagger**: detach `CSimpleModel::vf_43(parentSm, hDaggerObj)` (slot `0x38d8420` -> `FUN_0235b3d0`: finds the
  attach entry whose child is `hChild`, clears the child's attached flag, removes the entry) **[high: code]**; drive its
  transform each frame after phase 7 with the listener's two objsys calls (re-animation.md 4); on recall `FUN_0231e2e0(d)`
  attaches it again **[med]**.
- **`DETACH` is not for weapons**: in `Combat_OnModelString` it releases `Combat+0x540`, the object made from the attack's
  `AnimationData.TargetCoupling` (field 19 of `*0x4ba8640`, created in `Combat_StartAttack`): coupled characters
  (grabs, carries) **[med]**.

Risks: whether the dagger model is visible on `dagger_sheath_back` (node 130) outside kills (RT; if not, the `+0xa4` step);
the stealth kill's `ACTIVATE_MELEEAUX` moves the dagger to its ActiveNode (right hand?) for the kill and swaps the sword to
its InactiveNode (RT, shots); a detached dagger stays registered as a weapon object (`FUN_026d63d0`) **[low]**.

Probe: the planned `weapons` word (runtime-probes A8) should also dump slots 0..7: record name, `+0xa2/+0xa4/+0xa5`,
entries with `+0x30/+0x34` hash -> node name, LoadedFX names. New `dagger hand|back|detach|attach` (A): the recipes
above with shots.

**Runtime result (P7).** Holds: slot 0 `PC_Sword_01`, **slot 1 `PC_Dagger_01`** (Acharn), slot 2 `PC_Bow_01`; the dagger
is shown on its sheath outside kills (`+0xa4 = 1`, `+0xa5 = 1`, shots 10/12: the `+0xa4` step is not needed). The node
hashes are **sockets**, not nodes: active (sword and dagger) = socket 16 on `R_Weapon` (`0x305e50e3`), inactive =
socket 24 on `scabbard_back` (sword, `0xdecfcc84`) and socket 23 on `Dagger_Sheath` (dagger, `0xb9770fb8`); the bow
active = socket 6 on `L_Weapon`, no inactive node. 3.3's recipe works with a node hash: `+0x34 = 0xfef266e`
(`L_Weapon`) + `FUN_0231e2e0` moved the dagger off the back to the left hand's node (2.6 cm by the next word) and the
restore put it back (0 entries changed by the game meanwhile). Detach/attach not tried.

## 4. Brutalize: is it a paired action?

- **Name** (run 3 `attacks.tsv`): no "brutal" string in the binary; the records are the **Torment** family: `PC_Bip_
  StealthTorment_B/F`, `_Hang`, `_Dangle`, `_Sitting`, `_Immune`, `_Resistance`, `PC_Bip_Wall_Torment_*`,
  `PC_Bip_Torment_FromAbove*`, `PC_RailLow_*`, `PC_WallCvr_*StlthTorment*`, `PC_Archer_StealthTorment` (DLC2 Sauron:
  `..._StealthBrutal_Resist_B/F`). It mirrors the stealth kill (`PC_Bip_SneakInstaKill_*`) and stealth brand
  (`PC_*_StealthBrand_*`) families **[med]**. The fear is its own attack: `Atk_C_BrutalK_3xMorale_Rune`,
  `Atk_HeadExp_Morale`, reactions `AI_Bip_Morale_Scared_*` **[med]**.
- **Paired** = `AnimationData.Sync` (field 8 of `*0x4ba8640`, read in `Combat_StartAttack` before `0x26eb93c`) holds a
  `Combat/Sync` record (`*0x4baa4a0`: SourceAttack, **TargetAttack**, **AttachSocket**, **AlignAttachSockets**,
  InterpolationPos/Rot, **TeleportDuringInterpolation**, InitialAlignment(BothRotate), AlignTranslation,
  KillTargetDamage, ...) **[high: code]**. The kill and brand families have victim records (`AI_Bip_PC_Sneak_IK_*`,
  `AI_Bip_PC_StealthBrand_*`), so they are probably syncs; the torment family has no victim record by name, so its
  `TargetAttack` is another name or it is not a sync **[low]**. **RT**: one record read decides.
- **Started** like every attack: the player's attack selection -> `Combat_StartAttack(playerCombat, ..., record, target)`
  (re-combat.md sites `PlayerAttackStarted`, `StartAttackNoPos`/`AtPos`); with Sync set, `FUN_028081a0` (G14,
  `G14_SYNC_START`) aligns Talion to the orc (moves him). **Needs**: the stock predicates (target unaware, direction, the
  skill item, the wraith modifier held) **[med]**.
- **VR**: if Sync is set, drive brutalize as the stock attack (the G3 chord with the target in front), not as a G8
  DealHit: DealHit alone gives a generic death, no victim animation and the fear only if the record's `AOEOnHit` carries
  the morale attack (`FUN_026c06b0` runs it, re-combat.md 1.1) **[med]**. Presentation per design section 1 (anchor
  frozen while G14 says sync).

Probe: `syncinfo NAME` (R): for an attack record print `AnimationData.Sync` and its fields above plus
`AttackPostData.AOEOnHit`; run on `PC_Bip_StealthTorment_B`, `_F`, `PC_Bip_SneakInstaKill_Loud_B` and a counter
(`PC_Bip_Sync_Cntr_F`) as the known-sync control.

**Runtime result (P7).** Brutalize **is a sync**, and the torment family uses the **stealth brand** sync:
`PC_Bip_StealthTorment_B/F` -> `AnimationData.Sync` = `PC_Bip_StealthBrand_B/F` (TargetAttack
`AI_Bip_PC_StealthBrand_B/F`, InterruptTable `Player_Initiated_InstantKill`, AttachSocket "Attach",
TeleportDuringInterpolation 1, InitialAlignmentBothRotate 1, AlignTranslation 1, CanApproachPlayer 1, KillTargetDamage
0), `AOEOnHit` `Rune_SBrnd_AoE_Brand` (not the morale rune), no stimuli; all 24 torment variants have a Sync.
`PC_Bip_SneakInstaKill_Loud_B` -> `PC_Bip_Sneak_InstantKill_Loud_B` (victim `AI_Bip_PC_Sneak_IK_Loud_B`, AOEOnHit
`Rune_BrutalK_3xMorale`, stimuli `Instakill_Loud`/`Player_StartleSlave`, persistent `Performing_Instakill`); the
control `PC_Bip_Sync_Cntr_F` has its own. Over the database 3146 of 10814 attack records have a Sync. So the VR
brutalize is the stock attack (G3 chord), as 4's VR bullet says; where the fear comes from in this save's brutalize is
open (no record here carries the morale rune).

## 5. Sounds: posting the game's own Wwise events

| Call | What | Conf. |
|---|---|---|
| `CSoundMgr::vf_13` `0x8ed730` (slot `0x346da80`), `mgr = *(void**)0x466c618` | `PostEventGlobal(mgr, const char* event)`: if `mgr+8 == 1` and the name is not empty: `FUN_008c9840(0x466df00, *(u32*)(mgr+0xc), event)`; `mgr+0xc` = the Wwise object registered as "SoundManagerGlobalEmitter" in `vf_03`; returns the AkPlayingID (0 = not posted) | high (code), med (2D emitter) |
| `FUN_008c9840(wrap, u32 obj, name)` -> `FUN_01467870` | `AK::SoundEngine::PostEvent(name, obj (-1 -> AK_INVALID), 0, 0, 0, 0, 0, 0)` | high (code), med (AK name) |
| `FUN_008c97c0(wrap, u32 obj, name)` -> `FUN_01463890(obj, name, 1)` / `FUN_008c97e0(wrap, obj)` -> `FUN_01463900` | RegisterGameObj (ids 0 and -1 refused) / UnregisterGameObj (also Wwise's `RendererProxyLocal` slots 2/3) | med |
| `FUN_008c9b80(wrap, u32 obj, const float pos[3], const float front[3])` -> `FUN_01460f90(obj, &{pos, front})` | SetPosition (proxy slot 7), then RTPC `gp_distance` = distance to the cached listener `0x466df08` (`FUN_014614e0` = SetRTPCValue) | high (code), med (AK names) |
| others | `FUN_008c98c0/9900/9930` = ExecuteActionOnEvent stop/pause/resume (`FUN_01462590`), `FUN_008c98f0` = StopAll(obj), `FUN_008c9960` = SetSwitch, `vf_22` = SetRTPCValue global | med |
| UI path | `FUN_00a0ea70(*(shell+0x80ff8), "ui_...")` (posts through `+0x34b8` vtbl+0x18) and `FUN_00a0e7f0(*(shell+0x80ff8), rec)` with `rec = FUN_02165870("ui_game_back")` (a DB list of event names) | high (code), low (classes) |

- Units: positions in game cm, the engine frame; the listener uses the same (re-stereo.md 9.3), no conversion.
- **VR recipe**: lock-in / denied (2D): `vf_13(mgr, name)`. Clank at the blade (3D): once `FUN_008c97c0(0x466df00,
  ID, "somvr_blade")` with our own id (say `0x50A70001`; the game's id scheme is not traced, RT: log RegisterGameObj
  ids); per sound `FUN_008c9b80(0x466df00, ID, contact, bladeDir)` then `FUN_008c9840(0x466df00, ID, name)`. Game thread
  (Wwise queues the calls itself) **[med]**.
- **Names in the binary** (Wwise events; `Menu_*` strings are localisation keys, not sounds): `ui_lastchance_fail`,
  `ui_lastchance_succeed` (struggle QTE: candidates for "denied" and "lock-in"), `ui_game_back`,
  `ui_collectable_arrow_collect`, `ui_enter_stronghold_area`, `ui_suspend`/`ui_resume`, `amb_rain_umbrella_enter/exit`,
  `dx_*`, `mus_*` **[high: strings and their users]**. RTPCs `gp_distance`, `gp_occlusion`, `gp_velocity`,
  `gp_pan_left_right`. **Clank**: data: the struck weapon's `ModelRecord.AudioBlockName` and the sword's
  `AudioAttackName` (fields 4/5 of `*0x4c33f80`) **[high: fields, RT: names; consumer not traced]**; the game's own hit
  and block sounds already come with the damage path.
- Risks: events must be in a loaded bank (`PostEvent` returns 0 otherwise); a name posted on the 2D emitter plays wherever
  that object sits if the event is 3D in the Wwise project (RT: does `ui_*` sound centred).

Probe: `soundlog` (R): entry hook `AK_POSTEVENT` logs name, object and the return value during a fight (blocks, shield
hits, parries, a struggle): the combat event names actually used. `sound NAME [global|blade]` (A): post one, log the
AkPlayingID (0 = unknown).

**Runtime result (P7).** Holds: `vf_13(mgr, "ui_game_back")` returns a playing id (4627), and our own object
`0x50a70001` posted at `R_Hand` (`sound last hand`) returns one too (13337); the harness's SDL dummy audio gives ids
only, so "centred" and "heard at the blade" are open. Combat event names in use: `cbt_imp_atk_player` (posted in the
frame of our `Combat_DealHit`: the game's own hit sound comes with G8), `cbt_imp_atk_biped`, `cbt_imp_atk_slave_imp`,
`cbt_pris_kd_imp`, `fol_cmn_recoil`, `fol_ply_cbt_swd_twirl`, `fol_cmn_unsheathe`/`sheathe`, `dx_cmbt_GenBasic`,
`vx_orc*_Enemy*`, and the combat music `mus_combatmode_default` + `mu_mode_combat` on the 2D emitter. No block, parry
or struggle sounds yet (none happened). The probe's name table is capped at 64 and fills in the front end.

## 6. Check sites

Verified with `python3 -I scripts/check_doc_sites.py reference/bin/linux/ShadowOfMordor docs/re-combat-items.md`. Hook
notes: `PJ_IMPACT`, `ITEM_REATTACH`, `AK_POSTEVENT` and `FX_CREATE_IMPL` start with plain pushes/moves (14+ bytes
patchable); `FX_CREATE` starts with a RIP-relative `cmp` and `SND_POSTGLOBAL`'s target with a short `jne` in its first
6 bytes (call them, do not patch them);
`K_ENTRY` lines of functions we only call pin the build.

```
SITE("PJ_CREATE", K_VSLOT, 0x3998728, 0x3998728, "50 3a 7c 02 00 00 00 00 e0 5d 7c 02 00 00 00 00 d0 8f ad 00 00 00 00 00", 0x27c3a50, "41 57 41 56 41 55 41 54 55 53 48 83 ec 48 48"),
SITE("PJ_UPDATE", K_VSLOT, 0x3998748, 0x3998748, "20 5d 7c 02 00 00 00 00 c0 6c c7 00 00 00 00 00 80 68 8c 00 00 00 00 00", 0x27c5d20, "55 53 48 89 fb 48 83 ec 08 f3 0f 10 05 ef ec"),
SITE("PJ_MOVE", K_CALL, 0x27c5d6a, 0x27c5d57, "00 00 5b 5d c3 0f 1f 40 00 48 89 df 48 8d ab b8 01 00 00 e8 81 f6 ff ff", 0x27c53f0, "41 54 55 48 89 fd 53 48 81 ec 90 00 00 00 48"),
SITE("PJ_IMPACT", K_ENTRY, 0x27be5d0, 0x27be5d0, "41 57 49 89 cf 41 56 49 89 fe 41 55 41 54 55 53 48 81 ec 68 02 00 00 48", 0x27be5d0, "41 57 49 89 cf 41 56 49 89 fe 41 55 41 54 55"),
SITE("PJ_IMPACT_FLIGHT", K_CALL, 0x27c57f7, 0x27c57e4, "0f 10 64 24 48 f3 0f 59 e1 f3 0f 58 c4 f3 0f 11 44 24 78 e8 d4 8d ff ff", 0x27be5d0, "41 57 49 89 cf 41 56 49 89 fe 41 55 41 54 55"),
SITE("PJ_DAMAGE", K_CALL, 0x27bf8ba, 0x27bf8a7, "5c 24 20 48 8b 54 24 70 4c 8b 8c 24 b0 00 00 00 48 89 de e8 51 95 f0 ff", 0x26c8e10, "41 57 49 89 f7 41 56 41 55 4d 89 cd 41 54 49"),
SITE("PJ_APPLY", K_CALL, 0x26c8eb4, 0x26c8ea1, "44 24 18 48 8b bb a0 04 00 00 41 55 6a 00 48 8d 54 24 40 e8 47 13 15 00", 0x281a200, "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 89"),
SITE("FX_CREATE", K_ENTRY, 0xd8c7f0, 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00 0f b6 c9 45 0f b6 c0 e9", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40"),
SITE("FX_CREATE_IMPL", K_ENTRY, 0xd8bae0, 0xd8bae0, "41 57 41 56 41 55 41 54 49 89 f4 55 48 89 d5 53 48 89 fb 48 89 d7 48 81", 0xd8bae0, "41 57 41 56 41 55 41 54 49 89 f4 55 48 89 d5"),
SITE("FX_CS_INIT", K_ENTRY, 0xa09e70, 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89 cd 41 54 48 c7 c1 ff ff ff", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("FX_CS_FREE", K_ENTRY, 0x9ec750, 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00 00 75 4e 48 8b 93 60 01 00", 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00"),
SITE("FX_MELEEFX", K_ENTRY, 0x26e6a10, 0x26e6a10, "48 85 f6 0f 84 df 02 00 00 41 57 41 56 41 55 41 54 55 48 89 fd 53 48 89", 0x26e6a10, "48 85 f6 0f 84 df 02 00 00 41 57 41 56 41 55"),
SITE("FX_WEAPONFX_CSINIT", K_CALL, 0x26fc738, 0x26fc725, "8b 71 08 4c 8d 6c 24 40 48 89 c2 4d 89 e0 31 c9 4c 89 ef e8 33 d7 30 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("FX_WEAPONFX_CREATE", K_CALL, 0x26fc7c0, 0x26fc7ad, "ea b9 01 00 00 00 31 f6 48 8b b8 78 0c 08 00 48 83 c7 30 e8 2b 00 69 fe", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40"),
SITE("FX_RELEASE", K_ENTRY, 0x2aebfd0, 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7 e8 dc fd ff ff 48 8b 3b 48", 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7"),
SITE("INV_ACTIVATE", K_ENTRY, 0x2315ca0, 0x2315ca0, "41 56 89 f2 41 55 48 8d 04 d5 00 00 00 00 41 54 48 29 d0 55 53 48 83 ec", 0x2315ca0, "41 56 89 f2 41 55 48 8d 04 d5 00 00 00 00 41"),
SITE("ITEM_REATTACH", K_ENTRY, 0x231e2e0, 0x231e2e0, "41 55 49 89 fd 41 54 55 53 48 83 ec 08 0f b6 9f a2 00 00 00 48 8b 7f 78", 0x231e2e0, "41 55 49 89 fd 41 54 55 53 48 83 ec 08 0f b6"),
SITE("ITEM_ATTACHOBJ", K_CALL, 0x231e392, 0x231e37f, "89 ee 6a 00 0f 44 d0 31 c9 45 31 c9 45 31 c0 48 83 c3 48 e8 89 19 7e 00", 0x2affd20, "41 57 41 56 45 89 c6 41 55 41 89 cd 41 54 41"),
SITE("ITEM_VISIBLE", K_ENTRY, 0x231e700, 0x231e700, "41 57 41 56 41 55 41 54 41 89 f4 55 53 48 89 fb 48 83 ec 18 48 8b 6f 70", 0x231e700, "41 57 41 56 41 55 41 54 41 89 f4 55 53 48 89"),
SITE("SM_ATTACH", K_VSLOT, 0x38d8418, 0x38d8418, "b0 7c 35 02 00 00 00 00 d0 09 35 02 00 00 00 00 f0 09 35 02 00 00 00 00", 0x2357cb0, "41 55 49 89 cd 41 54 41 89 d4 55 48 89 f5 53"),
SITE("SM_DETACH", K_VSLOT, 0x38d8420, 0x38d8420, "d0 09 35 02 00 00 00 00 f0 09 35 02 00 00 00 00 f0 fa 34 02 00 00 00 00", 0x23509d0, "48 8b bf 90 00 00 00 48 85 ff 74 0c e9 ef a9"),
SITE("SND_POSTGLOBAL", K_VSLOT, 0x346da80, 0x346da80, "30 d7 8e 00 00 00 00 00 90 d7 8e 00 00 00 00 00 b0 d7 8e 00 00 00 00 00", 0x8ed730, "80 7f 08 01 75 22 48 85 f6 74 1d 80 3e 00 74"),
SITE("SND_POST", K_ENTRY, 0x8c9840, 0x8c9840, "48 83 ec 08 89 f0 83 fe ff 48 c7 c6 ff ff ff ff 6a 00 48 0f 45 f0 48 89", 0x8c9840, "48 83 ec 08 89 f0 83 fe ff 48 c7 c6 ff ff ff"),
SITE("SND_POST_AK", K_CALL, 0x8c9865, 0x8c9852, "48 0f 45 f0 48 89 d7 45 31 c9 6a 00 45 31 c0 31 c9 31 d2 e8 06 e0 b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("SND_REGOBJ", K_ENTRY, 0x8c97c0, 0x8c97c0, "48 89 d0 48 83 ec 08 89 f7 ba 01 00 00 00 48 89 c6 e8 ba a0 b9 00 b8 01", 0x8c97c0, "48 89 d0 48 83 ec 08 89 f7 ba 01 00 00 00 48"),
SITE("SND_UNREGOBJ", K_ENTRY, 0x8c97e0, 0x8c97e0, "48 83 ec 08 89 f7 e8 15 a1 b9 00 b8 01 00 00 00 48 83 c4 08 c3 90 66 2e", 0x8c97e0, "48 83 ec 08 89 f7 e8 15 a1 b9 00 b8 01 00 00"),
SITE("SND_SETPOS", K_ENTRY, 0x8c9b80, 0x8c9b80, "41 54 41 89 f4 55 53 48 89 fb 48 83 ec 30 48 8b 02 80 7f 14 00 f3 44 0f", 0x8c9b80, "41 54 41 89 f4 55 53 48 89 fb 48 83 ec 30 48"),
SITE("AK_POSTEVENT", K_ENTRY, 0x1467870, 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5 41 54 45 89 cc 45 31 c9 55", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("AK_REGOBJ", K_ENTRY, 0x1463890, 0x1463890, "48 8d 4f ff 48 83 f9 fd 77 5e 53 b8 0b 00 00 00 48 83 ec 40 66 89 44 24", 0x1463890, "48 8d 4f ff 48 83 f9 fd 77 5e 53 b8 0b 00 00"),
SITE("AK_SETPOS", K_ENTRY, 0x1460f90, 0x1460f90, "48 85 ff 74 4b 53 b8 0d 00 00 00 48 83 ec 40 f3 0f 6f 06 48 8b 1d 76 ab", 0x1460f90, "48 85 ff 74 4b 53 b8 0d 00 00 00 48 83 ec 40"),
```

## 7. Runtime checks (summary of the new probe words)

| ID | Question | Probe word | Kind | M |
|---|---|---|---|---|
| CI1 | projectile speeds, flight times, homing/gravity/spline use; thrown spears = CProjectile? stock moves immune to arrows? | `projlog` | R | M4 |
| CI2 | `+0x228 = 1` removes a projectile cleanly (no damage, no FX, no stuck arrow) | `projblock` | A | M4 |
| CI3 | a rewritten quaternion (homing off, target cleared) flies back; with `+0x58` = the player it damages the archer | `projreflect` | A | M5 |
| CI4 | the FX name table; does a hit streak start a glow on the sword by itself? | `fxlog` | R | M4 |
| CI5 | our looping FX on the sword model, stopped cleanly | `weaponfx NAME loop`, `weaponfx stop` | A | M4 |
| CI6 | slot 1 = Acharn; the dagger visible on its sheath; retarget to `L_Weapon` and back; detach + re-attach | `weapons` (A8, extended), `dagger hand/back/detach/attach` | R/A | M5 |
| CI7 | `PC_Bip_StealthTorment_*` have `AnimationData.Sync`? their `TargetAttack`, `AOEOnHit` | `syncinfo NAME` | R | M5 |
| CI8 | combat event names in use; `ui_lastchance_*` play centred; our 3D object at the blade is heard there | `soundlog`, `sound NAME global/blade` | R/A | M4 |
