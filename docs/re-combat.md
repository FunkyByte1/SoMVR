# Shadow of Mordor (Feral Linux build): combat internals for a physical VR combat layer

Static analysis only (2026-10-18, 22:00 onwards). Ghidra project copy `som-linux-q4` (read-only `ghidra_query.sh`
batches `reference/work-combat/b2..b5.txt`, outputs `o2..o5.txt`, plus the gameplay helper's `reference/work-gameplay/o*.txt`), the TSV indexes, `objdump` over the read-only binary
copy, and two small offline scripts (`reference/work-combat/schema.py`, `sites.py`). Nothing was launched, nothing was
written to any Ghidra project. Builds on `docs/re-gameplay.md` (cited as **GP**, section numbers) and
`docs/re-strings-map.md`; it does not repeat what those establish.

Conventions as in GP: addresses are som-linux VAs (non-PIE, BuildID 87ec5d6f...), `FUN_xxxxxxxx` = Ghidra's name (the
GP renames from section 10 of GP are given in brackets the first time), world units cm, +Y up, SysV x86-64 (a
by-value `LTVector` travels in XMM0 = x,y and XMM1 = z). Confidence tags: **[high]** read in the decompile/disassembly,
unambiguous; **[med]** read in code, meaning inferred from names/data/call pattern; **[low]** inference. **RT** = needs
a runtime check before we rely on it.

## 0. Summary for the VR combat layer

- **One sink for our hits:** `FUN_026d4ff0` [`Combat_DealHit`](LTVector hitPos, Combat* attacker, HOBJECT target,
  HRECORD attack, bool forceKill). It needs an explicit `Combat/Attack` record but **no player animation and no active
  attack**. It runs the game's own alignment/immunity checks, damage calculation (defence, buffs, difficulty), the hit
  reaction (recoil/stagger, block recoil), combat states (stun, knockdown ... from data), hit counter (combo), resource
  gains, hit FX/audio events, damage numbers, death, XP, loot and the Nemesis bookkeeping, plus decapitation when the
  attack's data says "sever on kill" (section 1) **[high for the calls, med for "everything works without an anim", RT]**.
- **Reactions are data:** which recoil, which state, whether it severs, how much damage: all come from fields of the
  attack record. Section 1.4 decodes the field names of the attack schemas from the binary (`attrs.tsv`): we can
  read any attack record's data at runtime and pick "light / heavy / finisher / decapitating" attacks by their data
  instead of by guessing names **[high for the field map, RT for the actual records]**.
- **Dismember:** `FUN_026d4670` = sever(Combat* victim, HRECORD severType, LTVector* velocity, LTVector* rotation,
  HOBJECT attacker); used by the `SEVER` animation key and by the death handler for `SeverTargetOnKill` (section 2.4).
- **Enemy attacks:** per-orc window flags live at `Combat+0xb0` (parry window = byte `Combat+0xe9`, hit window
  `+0xe3`); the counter prompt is made by `FUN_026e5f90` at the `BPW` key with the window length in ms; the attack's
  `AttackType` (0 normal, 1 cannot-parry, 2 cannot-counter, 3 cannot-block, 4 ranged) decides the prompt FX
  (section 3).
- **Bow:** `FUN_02332c50` = fire projectile(Inventory*, Combat* shooter, HRECORD rangedItem|0, HRECORD accuracy|0,
  const LTRigidTransform* fireXf) - spawns a `CProjectile` along the transform's +Z (or at an aim target), speed and
  gravity from the item's `RangedWeaponData` (section 4).
- **Correction to GP 4:** `DamageStruct` amount is at `+0x50` and the damage type at `+0x68` (GP's offsets from `+0x48` on
  are 8 too low); the first `DealHit` argument is the hit *position*, the direction lives in `Combat+0x15c` (1.1, 1.3).
- **Hit zones:** the game's own per-character hit spheres are data (`Combat/AimAtNode` records: `NodeName`, `Radius`);
  the projectile code tests rays against them (section 5).

## 1. (a) Damage application

### 1.1 The entry point: `FUN_026d4ff0` [`Combat_DealHit`]

```
void FUN_026d4ff0(LTVector hitPos   /* XMM0 = x,y; XMM1 = z; saved at rsp+0x10 */,
                  Combat*  attacker /* RDI -> rbx */,
                  HOBJECT  target   /* RSI -> r15 */,
                  HRECORD  attack   /* RDX -> rbp; 0 = FUN_026d4510(attacker) = current attack (+variation) */,
                  bool     forceKill/* ECX -> r14d */)                                   [high: disassembly 0x26d4ff0]
```

- **`hitPos` is a position, not a direction.** GP called it `hitDir`. The `APPLYDAMAGE` path passes the target's
  position (`target+0x08..0x10`, `FUN_026e49a0`+0xa9b), the collider path its hit point, and the value ends up as
  `HitInfo+0x00`, as the hit-event position and as the spawn point of "AOE on hit" (`FUN_026c06b0`) **[high]**. The hit
  *direction* is a field of the attacker's `Combat`: `Combat+0x15c..+0x164` (normalized vec3). The game sets it just
  before every call (`FUN_026e49a0`: attacker->target, or the collider's own direction; `FUN_026d5530`
  [`Combat_OnColliderHit`]: the collider vector rotated by the attacker's quaternion) **[high]**. `FUN_028198c0` reads it
  back as the hit-event direction. **So our layer writes the blade's world velocity direction (normalized) to
  `attacker+0x15c` and passes the contact point as `hitPos`.**
- `forceKill`: the key path passes the byte `*(Combat+0x1c8)+0x58` (the attack's slow-mo/presentation object, set up in
  `Combat_StartAttack` via `FUN_02749020`; probably the slow-mo kill) **[med]**; inside, it sets the damage to
  1,000,000.0 (`0x49742400`) for the damage application only **[high]**.
- Flow **[high: decompile o2/GP o2]**:
  1. `HitInfo_Init(&hit)` (`FUN_02819890`), `hit.pos = hitPos`; weapon = `Inventory_GetEquippedWeapon(Character_GetInventory(attacker->owner))`
     (`attacker+0x70` = owner `CCharacter*`; the weapon is the item's **record**, `item+0x78`).
  2. `host = FUN_0280fba0(target)` [`Object_GetCombatHost`]. Character host (`host+0x70` != 0):
     `FUN_0281a200(attacker+0x4a0 /*attacker HOBJECT*/, host+0x70 /*target CCharacter*/, &hit, attack, weapon,
     attacker+0x27f /*crit*/, forceKill, mayIncrementHitCounter)`; generic host/prop: `host->+0x78` vtbl+0x50; no host:
     `FUN_028125f0`.
  3. `mayIncrementHitCounter` = 1 if `attacker+0x27a == 0` (this attack has not hit anything yet), else
     `AttackPostData.AllowMultiIncHitCounter` or the target's `AlwaysIncrementHitCounterOnHit` **[high]**.
  4. If the attacker is the player (`attacker+0x88` != 0): the target's `AwardItemOnHitByPlayer` item is given
     (`FUN_023198b0`).
  5. `FUN_026c06b0(attacker, attack, &hitPos)`: the attack's `AOEOnHit` list with `AOEOnHitChance`, spawned at hitPos
     (`FUN_02735930`, AOE manager `*0x4c43b80`) **[high]**.
- Its three game callers: `FUN_026e49a0` (`APPLYDAMAGE` range/angle test, GP 4), `FUN_026d5530` (colliders, tail call),
  `FUN_026d5a20` (attached/rider damage). Both game paths set `Combat+0x15c` first, then call with `forceKill` from
  the state flag (key path) or 0 (colliders) **[high]**.

### 1.2 The server apply: `FUN_0281a200` [`ServerCombat_ApplyAttackDamage`]

```
void FUN_0281a200(HOBJECT attacker /*RDI*/, CCharacter* target /*RSI*/, HitInfo* hit /*RDX*/, HRECORD attack /*RCX*/,
                  HRECORD weaponItem /*R8*/, bool isCrit /*R9*/, bool forceKill /*stack*/, bool mayIncHitCounter /*stack*/)
                                                                     [high: decompile + call site 0x26d50b9]
```

What it does, in order (`attackData` = `attack.AttackData`, `postData` = `attack.AttackPostData`, field names from
section 1.4) **[high for the calls and checks, med for the roles where marked]**:

| Step | Code | Effect |
|---|---|---|
| 1 | `target` vtbl+0x2c8 (slot 89); global `*(*(0x4c45e60+0x2a0)+0x18)` and `target` vtbl+0xd8 (slot 27) | immune target -> return; slot 27 behaves as "is the player" (also step 3 and 16), so the global looks like a player-invulnerability switch **[med]** |
| 2 | `attackData.SecondaryTargetAttack` / `UseSecondaryAttackIfHasAttached` | may swap in a secondary attack record |
| 3 | `Combat.AlwaysKill` (`*(bool*)0x4c44418`) and attacker vtbl+0xd8 | kill flag, only when the attacker is the player (the cvar help: "allow players to kill their targets with any attack") |
| 4 | `AlignmentOverride(Branded)`, alignment table `*0x4b55e78` vs `target+0x48` | "Attacker's Alignment not allowed to damage target's alignment." -> return |
| 5 | `FUN_0280e4b0(target's current attack, attack)` = the target attack's `CanTakeDamage`/`CanTakeDamageExceptions` | "Target can not be damaged during attack %s" -> return (orcs in some animations, the player in sync actions) |
| 6 | `HitCharacterTypeInclude/Exclude` vs target character type (`FUN_00a9f490(targetCombat+0x4c8)`) | "Attack %s cannot hit CharacterType %s" -> return |
| 7 | `FUN_028194d0(ctx, attacker, target HOBJECT, attack, weapon, hit, isCrit)` -> `FUN_028172e0` | builds the 0x1c8-byte damage context: defence record, block/parry result (`ctx+0x1c0/+0x1c1`, `ctx+0x160` = block-table entry), `Damage` record (`ctx+0x190`), damage amount record (`ctx+0x188`) **[med, RT]** |
| 8 | `FUN_02814310(ctx, hit, &ds)` | fills the `DamageStruct` (1.3): amount = `FUN_02813250(ctx)` (the logged "Starting Damage Calc ... Defense Record Calc ... Dyn Offensive Adjustment ... Clamped Damage"), damage type, direction, positions |
| 9 | `forceKill` or AlwaysKill | `ds.amount = 1e6` for the application, restored before the hit event |
| 10 | target alive (`CCharacter+0x178` bit 0 clear) | `FUN_026dd5a0` -> `FUN_026dc700`: apply DOTs / combat states of the hit (`Damage.StateMods`, chance `FUN_0280ae20`) **[med]** |
| 11a | normal hit **[med: the branch conditions]** | `FUN_02811140(attacker, target, attack, hit, &ds)`: damage + **hit recoil** (`attackData.RecoilTypeHit`), section 2.1 |
| 11b | blocked (`ctx+0x1c0`/`+0x1c1`) **[med: the branch conditions]** | `FUN_02810e80(...)` damage (usually reduced by the defence calc) + `FUN_02810af0(...)` **block recoil** (`attackData.RecoilTypeBlock` or the defender's `AIBlockTable` entry `FUN_028b1030(ctx+0x160)`) |
| 12 | `mayIncHitCounter` | 0: `postData.ResetHitCounterTimer` -> `FUN_026c53b0`; 1: `postData.IncrementHitCounter` and `Damage.CanIncrementHitCounter` -> `FUN_026d4940(attackerCombat, 1, attack)` (restart the hit-counter timers at `Combat+0x1f0..+0x208`) |
| 13 | attacker's weapon `DestructibleData` (`Explosions`, `BreakOnCarrierHit`, `ExcludedAttackCategories`, `DropItemFX`, `ActivateItemSlot`) | breakable weapons (orc shields/weapons) explode/drop |
| 14 | `FUN_028198c0(attacker, targetCombat, attack, weapon, &ds, hit, ctx, blocked, isCrit)` | hit event + all on-hit side effects (1.5) |
| 15 | target dead now, or its combat state is a pre-death state (`FUN_0281d910`) | `FUN_026f61f0(targetCombat, attack, attackerCombat, x)` = **death** (2.3) |
| 16 | `FUN_02297b10`/`FUN_0280a800`, `FUN_0297a9c0`, `FUN_0296fa20` | threat/aggro to AI, brand-related AI notification, AI reaction **[med]** |
| 17 | `attackData.RegisterStimulusOnHit` -> `FUN_0293e820(*0x4c500e0, ...)` | AI stimulus (others hear/see the fight) |

`FUN_0281c960(CCharacter* attackerChar, HRECORD attack, HRECORD weapon)` (GP: "same without an attack instance") is the
same pipeline for sync actions / self-damage (`Combat_ApplyDamageKey` when the character is attached), without the
alignment and character-type checks **[high]**.

### 1.3 Structures

**HitInfo** (0x31+ bytes, `HitInfo_Init` zeroes it and sets `+0x24 = 1.0f`) **[high for the fields read, med for names]**:

| Off | Type | Meaning |
|---|---|---|
| +0x00 | vec3 | hit position (the `hitPos` argument) |
| +0x0c | vec3 | source position override (used for the recoil/hit direction instead of the attacker position when `+0x30` is set) |
| +0x18..+0x24 | floats | extra pose data copied to the dead target (`CCharacter+0x1ac..+0x1c4`) when `+0x30` is set; `+0x20 = 0.0`, `+0x24 = 1.0` by default |
| +0x28 | qword | passed to the recoil picker `FUN_0229a190` and stored on a dead target (`+0x1e0`) - a recoil/hit-node hint **[low]** |
| +0x30 | byte | "+0x0c is valid" |

**DamageStruct** (0xa0 bytes, vtable `0x34ef650`) - **corrects GP section 4 (GP's offsets from +0x48 on are 8 low)**
**[high: FUN_02814310 writes and Cmd_PlayerFragSelf fills the same layout]**:

| Off | Meaning |
|---|---|
| +0x00 / +0x08 | vtable / `WBObjRefNotifier` |
| +0x10..+0x20 | `WBObjRef` to the damager (`+0x20` = damager HOBJECT) |
| +0x28..+0x48 | second `WBObjRef` (`+0x48` object) |
| +0x50 | float **amount** (negative = heal, `FUN_02810e80` routes it to `FUN_02763910`) |
| +0x58 | qword from the damage context (`ctx+0x180`) |
| +0x60 | float (default 5000.0 from a hit-physics record `*0x4baeb20` idx0, else 0) then `+0x64` = 1.0 - impulse/scale **[low]** |
| +0x68 | HRECORD damage type (`Damage.Type`; `DamageType_FromName("Explode")` in FragSelf) |
| +0x74 | byte "has direction" |
| +0x78 | vec3 normalized direction (target - source) |
| +0x84 | vec3 target position, `+0x90` vec3 source position |

**Hit event** (`FUN_0229ba20` fills it; `FUN_026f2580(targetCombat, &ev)` consumes it; probably the payload behind
`CombatAttackHitArgs`) **[high for the writes, med for names]**: `+0x00` vec3 position, `+0x0c` vec3 direction (=
attacker `Combat+0x15c` after the optional `attackData` hit-position override `*0x4baeba0`: `OverridePosition`,
`Position`, `OverrideDirection`, `Direction`), `+0x20..+0x30` WBObjRef attacker (`+0x30` HOBJECT), `+0x38` attack, `+0x40` the target's combat state, `+0x48` weapon, `+0x50` float damage (XMM0; damage numbers print `(int)`), `+0x54` short (attacker `FUN_026c7c90`), `+0x56` `Damage.EnableHitFX`, `+0x57` = 1 (melee hit), `+0x58` use block FX (blocked or `UseBlockFXInsteadOfHitFX`), `+0x59` `FUN_026c7cb0(target)`, `+0x5a` isCrit **[high for the writes: the definition's parameter order against the call in `FUN_028198c0`; med for the names]**.

**Combat fields used here** (`Combat` = `CCharacter+0x6d0`, GP 4) **[high where the code is unambiguous]**:

| Off | Meaning |
|---|---|
| +0x70 / +0x78 | owner `CCharacter*` / generic host |
| +0x80 | AI owner (non-null for NPCs; the counter-prompt and AI-manager code require it) **[med]** |
| +0x88 | player owner (non-null only for the player's Combat: "PlayerKiller", AwardItemOnHitByPlayer) **[med]** |
| +0xb0 | window state of the current attack (section 3.2) |
| +0x150 / +0x170 / +0x178 | aim position / aim target object / aim node index for the current attack (set by the start-attack wrappers, read by the projectile code) |
| +0x15c | vec3 hit direction for the next `DealHit` |
| +0x17c | int node index used by `SHOOT` (fire node) |
| +0x188 / +0x190 | current sync action object (`CCombatSyncAction`) |
| +0x1c8 | attack presentation/slow-mo state; `+0x58` in it = the kill flag passed as `forceKill` |
| +0x1d8 | random/chance helper (`FUN_02700f20(it, chance)` = roll) |
| +0x1f0..+0x208 | hit counter, recoil counter, block counter, dodge counter (`[COMBAT|HITCOUNTER]` objects, `FUN_027259c0`..) |
| +0x268 | current combat state record (`FUN_026c6a80`) |
| +0x27a | "this attack already hit something" |
| +0x27f | "this attack is a crit" (set at attack start; `Combat.Debug.AlwaysCrit` `0x4c44a98` forces it) |
| +0x287 | cleared after each APPLYDAMAGE hit (GP) |
| +0x288 | dead (set first thing in `FUN_026f61f0`) |
| +0x4a0 / +0x4c0 / +0x4c8 | own HOBJECT / current target HOBJECT / character combat record (`*0x4ba0ca0` schema) |
| +0x4d0 / +0x4d8 / +0x55c | current attack / its attack node / variation |
| +0x4e8 / +0x4ec | level / XP (player) |

`CCharacter`: `+0x08` its HOBJECT, `+0x178` bit 0 = dead, `+0x1c8..+0x1d0` last hit direction (`CCharacter::vf_60`),
`+0x130` health component (heal), `+0x1a0` death direction **[med]**. `HOBJECT`: `+0x08` position vec3, `+0x14` rotation
quat (x,y,z,w) - read by every distance/direction computation here **[high]**.

### 1.4 The attack record and its data (decoded field names)

Records are 0x30-byte entries (`+0x08` attribute table, `+0x10` values, `+0x28` name hash); a category such as
`Combat/Attack` keeps them sorted by hash. Each record type has a global pointer to a 0x30-byte-per-field accessor
array, filled at start-up by a schema function (`_INIT_1315` and others call `FUN_025xxxxx(array)`); code reads field
*i* as `*(GLOBAL) + i*0x30 + 8` (hash), `+0x18` (cache state), `+0x28` (index). `schema.py` disassembles the schema
functions and recovers the names, so every `DAT_xxx + 0xNNN` in a decompile now has a name
(`reference/work-combat/attrs.tsv`, ~1,000 fields of 49 record types). Checked against code whose meaning was already known
(`ApplyDamageRange` +0x1e8, `ApplyDamageMaxHalfAngle` +0x248 in the range test; `IsBlock` in the block state;
`HitCharacterTypeInclude` in step 6) **[high]**. Name hash = `h = h*0x397 + table[c]` over the name (table
`0x3925cc0`, case-folding) **[high]**.

| Record (global -> array) | Fields (index: name) |
|---|---|
| `Combat/Attack` (`*0x4ba23c0`) | 0 AnimationData, 1 **AttackData**, 2 **AttackPostData**, 3 Predicate, 4 MultiHitRecords (variations), 5 Category, 6 AIData |
| AttackData (`*0x4ba7280`, 104) | 7 Colliders, 9 StrikeRange, 10 ApplyDamageRange, 12 ApplyDamageMaxHalfAngle, 16 **RecoilTypeHit**, 19 **RecoilTypeBlock**, 20 **RecoilTypeParry**, 21 CanTakeDamage, 23 CanInterrupt, 26-28 (Can/Cannot)InterruptExceptions, 29 **CanCrit**, 35 **IsBlock**, 36 **AttackType**, 37 AlwaysDisplayCounterPrompts, 43 AOE, 48 BuffList, 52 PauseHitCounter, 54 RegisterStimulusOnHit, 57 **SeverTargetChance**, 58 **SeverTarget**, 59 **SeverTargetOnKill**, 60/61 SeverSelf(Chance), 62/63 SeverTargetVelocity/Rotation, 66/67 **BrandTarget(OnHit)**, 68/69 HitCharacterTypeInclude/Exclude, 72 RangedWeaponOverride, 73 RangedAccuracyOverride, 75 ApplyDamageOnStart, 76 HitBrandedOnStart, 82 TargetHealthMin, 88 ActivateBrandOnHit, 100 CanDecrementHitCounter (all 104 in attrs.tsv) |
| AttackPostData (`*0x4ba6580`, 68) | 2 CanNextAttackCrit, 22/23 AOEOnHit(Chance), 24 **IncrementHitCounter**, 25 AllowMultiIncHitCounter, 26 ResetHitCounterOnMiss, 27 ResetHitCounterTimer, 28 EnergyTokenCost, 29 MeleeFX, 33 CameraTag, 38 **CountAsHit**, 40 Prompt, 58 FXHitData, 59/60 HUDCritWidgetPrompt..., 61 UIMarkTargetAsDominated |
| Damage (`*0x4ba5900`) | 0 ApplyLastDuringDamageCalc, 1 ApplyMoraleChanges, 2 ApplyInfighting, 3 CanIncrementHitCounter, 4 EnableHitFX, 5 UseBlockFXInsteadOfHitFX, 6 MinHealth, 7 **DamageAmount**, 8 ThreatModifier, 9 HUDCritWidgetPrompt, 10 **StateMods** |
| damage amount (`*0x4bb4280`) | 0 TargetCurrentHealthPct, 1 TargetMaxHealthPct, 2 Damage, 3 WeaponDamageScalar, 4 DOT, 5 DamageType, 6/7 FloatingText(Crit) |
| state mod (`*0x4b9ee60`) | 0 BuffChance, 1 BaseChance, 2 **State**, 3 DOT, 5 Duration, 6 DurationOverrides |
| combat state (`*0x4b9f0e0`, `Combat/States/Definitions`) | 0 FailCondition, 1 PreDeathCondition, 2 InterruptOnHit, 3 EnableCollisionCapsule, 5 EnemyAICanTarget, 12 CommandSet, 19 StrugglePool |
| character combat record (`*0x4ba0ca0`, 106; `Combat+0x4c8`) | 0 Weapons, 1 Nodes, 2 RecoilNodes, 3 DeathNodes, 4 StateNodes, 14 DefensiveStats, 15 HitFXSetup, 35 CharacterType, 37 AimAtNode, 40-43 Hit/Recoil/Block/DodgeCounter, 44-51 InputBlock/Dodge/Counter/NextRanged CommandID (+_KBM), 56 AIBlockTable, 57 SeverSetup, 68 AttackTypeFX, 89 CanBeHitByAOE, 92 AlwaysIncrementHitCounterOnHit, 93 AwardItemOnHitByPlayer, 96 CanCounterWeaponModes, 97 CanCounterExcludedBehaviorStates, 103 SeverFXContext |
| AnimationData (`*0x4ba8640`) | 0 Name, 2 BehaviorNode, 4 WeaponAnimationName, 7 PlaybackRate, 17/18 RecoilSnapRotation/Direction, 19 TargetCoupling ... |

**Getting records at runtime** **[high, asm]**: each category has a block of helper functions followed by its "list records" function; for "Combat/Attack" (list `FUN_02166840`) the category object is `*(void**)0x4b568d0` and the helpers are `FUN_02166700()` = category (11 callers), `FUN_02166740()` = record count (`+0x28`), `FUN_02166760(uint i)` = record *i* (`+0x38` array, 0x30 each), `FUN_02166790(const char* name)` = record by name (name hash, binary search, 0 if missing; no callers but complete), `FUN_02166830(name)` = the same through the generic `FUN_022a4890(category, name)`; `FUN_025aa530` [`DBRecord_GetName`](rec) = name. (The block after it, `FUN_021668f0` etc. on `*0x4b568c8`, belongs to "Combat/Pool".) Field readers: `FUN_0280a0b0(rec, field, &default)` (record), `FUN_0280a2d0` (bool), `FUN_026bfb10` (float), `FUN_026bfe40` (string), `FUN_00a09d60(&list, rec, field)` (list). **Plan:** at start-up (in a test run) dump every Combat/Attack name with its AttackData/AttackPostData fields to a TSV; choose the player's attacks for VR from that list **[RT]**.

### 1.5 What one hit triggers (`FUN_028198c0` and below)

- **Hit event** `FUN_0229ba20` -> **`FUN_026f2580(targetCombat, &ev)`**: clears the target's parry nav-marker/FX
  (`FUN_026d8480`) if flagged, writes the client shell's "last hit" record (`*(0x46a9560+0x110)`, read by the
  client-side hit feedback `FUN_00eab1c0`: camera shake/rumble/HUD), block FX (`FUN_026d1660`), hit-direction FX at
  the hit node (`FUN_026f0640`, "CombatHitDirection", `HitFXSetup`), player-side reaction (`FUN_00a80760(target+0x2b8)`
  when the target is the player), **damage numbers** when `Combat.Vis.DamageNumbers` (`*(bool*)0x4c44058`), and the
  per-character event handler `FUN_00a7f8f0` **[high for the calls, med for the roles]**.
- **Attacker side** `FUN_026df7c0`: hit counter `FUN_027293c0` ("[COMBAT|HITCOUNTER] Attack Hit"), `CountAsHit`,
  resource gains (energy/wraith/heal through `FUN_02763910`), crit handling (`+0x27f`), event broadcast via the event
  manager (`*0x46b7310` vtbl+0xf8) **[med]**.
- **Target side** `FUN_026f8a60`: `BrandTargetOnHit` -> `FUN_026f8560` (brand), `ActivateItemSlotOnInterrupt`,
  item drop **[med]**.
- **Combo / hit counter:** the counter object at `Combat+0x1f0` counts *distinct* attacks that hit (list of attack
  records, `FUN_027293c0`), times out (`FUN_027259c0` "Calculating new value, NumHits/NumRecoils/NumBlocks"), and
  pauses during weapon modes/presentations; crit eligibility (`CanCrit`, `CanNextAttackCrit`, `+0x27f`) follows it.
  `Combat.Lock.HitCounter` (`0x4c44218`/value `0x4c43ed8`, GP) can pin it **[med]**. **For VR: use different attack
  records for successive swings (the counter ignores repeats of the same record) - RT.**
- **XP:** `FUN_026f46a0(Combat*, rec)` adds `FUN_0229fc20(rec)` XP to `Combat+0x4ec` ("[COMBAT|XP] %s - Add %d XP",
  `Combat.Debug.XP`), HUD event 0x5a, progression `FUN_02340ed0`; called from the death path (`FUN_026f4850`, killer
  side, also Nemesis/power) **[high]**.
- **Audio:** no direct sound call in this pipeline; hit/block audio comes from the weapon audio record (`*0x4bb3c80`:
  HitEvent, BlockEvent, HitSurfaceEvent, WeaponLocationParams) and the client FX of the hit event **[med, RT]**.

### 1.6 Calling it from our code

Recipe (game thread, inside a per-frame hook after the server update, GP 8) **[med; RT for every point]**:

```
Combat* pc   = Character_GetCombat(CPlayerObj*)          // *(CPlayerObj+0x6d0); GP 1/4
HRECORD atk  = FUN_02166790("<attack name>")              // chosen from the runtime dump (1.4)
float*  dir  = (float*)((char*)pc + 0x15c);              // save, write normalized blade direction (world)
uint8_t was27a = pc[0x27a], was27f = pc[0x27f];
pc[0x27a] = 0;                                            // first hit of "our" swing -> may increment the combo
pc[0x27f] = wantCrit;                                     // crit flag for the damage calc / crit FX
FUN_026d4ff0(contactPoint, pc, hOrc, atk, false);
restore dir / 0x27a / 0x27f
```

- **No attack animation is needed.** Nothing in `DealHit`/`ApplyAttackDamage` checks that the attacker is in an attack;
  the only "during attack" check is the *target's* `CanTakeDamage` (step 5) **[high]**.
- Preconditions: `attack != 0` (else the current attack is used, and with none the apply returns at once); target is
  a character with a `Combat`; attacker owner alive; call on the game thread **[high]**.
- **What we do not get** without the attack animation: `SEVER`/`SHOOT`/`APPLYBUFF`/`HITBRANDED` keys (they come from
  the animation, `Combat_OnModelString`), the attack's camera tag/slow-mo, `AICombatMgr_OnStartAttack` (the fight
  circle does not "see" the player attack) and the AI's counter-block reactions to a starting player attack. Decapitation
  still happens through `SeverTargetOnKill` in the death path (2.4) **[high for the list]**.
- The recoil direction and the snap use the attacker's object position (`attacker+0x4a0 -> +0x08`) unless
  `HitInfo+0x30` supplies a source position (`+0x0c`). In VR the player object follows the head, so for a sword hit
  calling `FUN_0281a200` directly with `hit.sourcePos = blade base` and `hit+0x30 = 1` gives the recoil the blade's
  side **[med, RT]**.
- Raw fallback (no recoil, no XP, no Nemesis): `DamageStruct` + `DamageStruct_DoDamage` (`FUN_02763050`), GP 4 with the
  corrected offsets above.

## 2. (b) Reactions on an enemy

### 2.1 Recoil = stagger / knockback / hit reaction

- **Normal hit:** `FUN_02811140(HOBJECT attacker, CCharacter* target, HRECORD attack, HitInfo*, DamageStruct*)`
  **[high]**: recoil record = `attack.AttackData.RecoilTypeHit`; if the target is alive it first calls
  `FUN_02810e80` (below) to apply the damage; then `postData.CountAsHit` -> `FUN_026d4bc0`; then the permission check
  `FUN_0280fe60`; then builds a direction context (`FUN_02280b30` attacker->target, or `FUN_022806c0` with
  `HitInfo+0x0c` as source when `HitInfo+0x30`), lets `FUN_0229a190(targetCombat, targetWeapon, recoilRecord,
  HitInfo+0x28, ctx, &anim)` pick the animation, sets `targetCombat+0x56a = 1`, aborts a running sync action of the
  target (`FUN_028062d0`) and plays it with **`target` vtbl+0x1e8 (slot 61)** `(target, anim, anim2, attacker)` =
  `NPC::vf_61` (`0x29699c0`, 3 KB) / `CPlayerObj::vf_61` (`0x27aad30`); `CCharacter::vf_61` is an empty `ret`.
- **Damage application inside it:** `FUN_02810e80(attacker, target, attack, recoilRecord, HitInfo*, DamageStruct*)`
  **[high]**: stores the hit direction on the target (`vf_60`, `CCharacter+0x1c8`), `amount > 0` -> `DamageStruct_DoDamage
  (ds, target HOBJECT)`, `amount < 0` -> heal (`FUN_02763910(target+0x130)`), `amount == 0` -> `target` vtbl+0x1b0
  (slot 54, `NPC::vf_54`). On a dead target it stores the death direction (`+0x1a0`), the recoil record (`+0x1d8`) and
  the HitInfo pose data. **So `FUN_02811140` with a zero-amount `DamageStruct` is a pure recoil call** **[med, RT]**.
- **Block recoil:** `FUN_02810af0(attacker, target, attack, recoilRecord|0, HitInfo*)` - default record
  `AttackData.RecoilTypeBlock`; the defender plays it (same picker and slot 61) **[high]**.
- **Permission ("hyper armour"):** `FUN_0280fe60(Combat* target, attacker, attack, newState, bool* abortSync)` -
  false when the target's *current* attack has `CanInterrupt = false` and our attack's `Category` is not in its
  `CanInterruptExceptions`, or is in `CannotInterruptExceptions(TargetBuff)`; logs "Attack (%s) not allowed to cause a
  recoil against current attack (%s)". Damage is still applied, only the stagger is skipped **[high]**.
- Which animation: the recoil record selects among the target's `RecoilNodes` (character combat record) by direction
  and the attacker's weapon; `AnimationData.RecoilSnapRotation/RecoilSnapDirection` turn the target toward the hit.
  Knockback distance is root motion of that animation **[med]**.
- `Combat.RagdollReaction` records (`*0x4bae980`: InitalVelocityScale, RelaxTime, StartFriction, EndFriction,
  StartTaoFactor, EndTaoFactor, Force) = a powered-ragdoll hit reaction blended over the animation **[med: names only,
  the user is not mapped]**.

### 2.2 Combat states: stun, knockdown, struggle, ...

- States are `Combat/States/Definitions` records (`*0x4b9f0e0`: FailCondition, PreDeathCondition, InterruptOnHit,
  EnableCollisionCapsule, AICanSeeThrough, EnemyAICanTarget, ..., Buff, CommandSet, ..., StrugglePool); which ones
  exist (Stunned, KnockedDown, ...) is data **[high for the fields]**.
- From a hit: `Damage.StateMods` -> state-mod records (`BuffChance`, `BaseChance`, `State`, `DOT`, `Duration`,
  `DurationOverrides`) -> chance `FUN_0280ae20(ctx, ?, stateMod)` (defender modifiers; "Combat State Rejected (%s) - not
  valid for the defending character"; `Combat.AlwaysApplyState` `0x4c443d8` = 100 %, logged as "Combat.AlwaysKD") ->
  `FUN_026dc700` **[high]**.
- **Set a state directly:** `FUN_026dbd00(Combat* c, HRECORD attackNode, HRECORD sourceAttack, HRECORD state,
  bool)` - `c+0x268 = state` ("[COMBAT|STATES] %s: New State = %s; Old State = '%s'", "will be applied for %0.4f
  Seconds (unless interrupted)"); `state = 0` clears (`FUN_026dc5c0`/`FUN_026dc630` = ClearAllCombatState pass
  `c+0x4d8`, `c+0x4d0`, 0, 1) **[high for the call shape, med for using it from outside, RT]**.

### 2.3 Death

`FUN_026f61f0(Combat* victim, HRECORD attack, Combat* killer, x)` **[high for the calls, med for roles]**:
`victim+0x288` guard (once only); `Combat.Debug.AlwaysSever` (`0x4c449d8`) path; `CharacterDeathArgs` broadcast through
the event manager (`*0x46b7310` vtbl+0xf8 -> `FUN_0240a7a0`); killer side `FUN_026f4850` (XP via `FUN_026f46a0`,
power, Nemesis); faction-member (Nemesis) death record `FUN_022eae30(victim member, victim record, level, ..., killer,
attack, inventory, ...)` and `FUN_022dbd20(member, attack, killer)`; loot (`CharacterLootDropArgs`); when the victim is
the player (`victim+0x88`): "PlayerKiller" tag of the killer (GP 6); **sever on kill** (2.4). The body: the death
direction from the `DamageStruct` (`CCharacter+0x1a0`) and the ragdoll death record (`*0x4c2a7e0`: BodyRagdoll,
BodyDeathScaleVelocity, BodyDeathClampVelocityMax, BodyDeathDirMinY, BodyDeathForce(Scale), BodyDirDeathForce(Scale),
BodyStickDist/Time, RagdollDeathAnimation) **[med]**. **A kill through `DealHit` with the player's `Combat` as
attacker is a normal player kill for the Nemesis system** (same path as the game's own) **[high]**.

### 2.4 Dismemberment and decapitation

```
void FUN_026d4670(Combat* victim /*RDI*/, HRECORD severType /*RSI*/, const LTVector* velocity /*RDX*/,
                  const LTVector* rotation /*RCX*/, HOBJECT attacker /*R8*/)         [high: disassembly + 2 callers]
```

- Needs `victim+0x90` (the character's model/sever component; returns early if null) and runs
  `FUN_0227c280(victim+0x530, victim, severType, model, model+0x120, vel, rot)` (spawn the severed part, hide pieces,
  stump), then `FUN_02697430(victimChar)`, `FUN_0281e1b0(char+0x898)`, `FUN_02747d80(victim+0x228)`, the Nemesis
  "severed" bookkeeping for faction members (`FUN_02301c60`/`FUN_023d49e0`; `IsSevered`, `FactionMemberSevered`,
  `KilledByDecapitation` make the death permanent) and an event to the attacker **[high for the calls, med for roles]**.
- **Callers:** (1) the `SEVER` animation key (`Combat_OnModelString`+0x25d1): target = the attacker's current target
  (`+0x4c0`), `severType = AttackData.SeverTarget[i]` (i = the key's argument), chance `SeverTargetChance` rolled with
  `attacker+0x1d8`, `SeverTargetVelocity`, `SeverTargetRotation`; `SEVERATTACHED` does the same for attached objects
  (`+0x540`), `SEVERSELF` uses `SeverSelf*`. (2) **the death handler** (`FUN_026f61f0`+0xe54): if the killing attack's
  `AttackData.SeverTargetOnKill` predicate passes, `SeverTarget[0]` with the same chance/velocity/rotation fields
  **[high]**.
- **Executions and combat finishers** are sync actions whose animations carry `APPLYDAMAGE`/`SEVER` keys (2.5,
  section 7). For VR: (a) kill with an attack record that has `SeverTargetOnKill` set -> the game decapitates on its own;
  (b) or, after our killing blow, call `FUN_026d4670(orcCombat, severType, &vel, &rot, hPlayer)` with a sever type taken
  from such an attack (`FUN_026fe6b0(attackData, i)` returns `SeverTarget[i]`) and `vel` = blade velocity **[med, RT:
  which sever types exist (head, arm?) is data; `Combat.SeverType`/`SeverFX`/`SeverSetup` records, character
  `SeverSetup`/`SeverFXContext`]**.
- Debug: `Combat.Debug.AlwaysSever` (`*(bool*)0x4c449d8`) - writing 1 makes every eligible kill sever (test aid).

### 2.5 Paired animations (executions, counters, drains, brands)

`Combat_StartAttack` (`FUN_026eb380`) starts a `CCombatSyncAction` when the attack has a `Combat/Sync` record
(`*0x4baa4a0`: SourceAttack, TargetAttack, InterruptTable, StrugglePool, BranchOnExternalDeath, ..., Interrogation,
AttachSocket, AlignAttachSockets, InterpolationPos/Rot, TeleportDuringInterpolation, InitialAlignment, ...,
KillTargetDamage, ..., EnableDynamicSlowMo, CanRecoilAfterStopAlign, ...): `FUN_028081a0(sync /*Combat+0x188*/,
syncRecord, target)` ("New Sync (%s)"), and the target side gets `Combat_StartAttack(targetCombat, 0, TargetAttack,
attackerHObj, 0)` **[high]**. These move both bodies (alignment, attach sockets): VR needs a presentation decision
(GP 8 risk 3).

## 3. (c) Enemy attacks, counters and blocks

### 3.1 How an orc attack is scheduled and when it lands

1. The AI fight circle grants the attack (GP 6: `AICombatMgr_ScoreAttacker` `FUN_028cd7a0`, `FUN_028cd4b0`, lockout
   `FUN_028ccc90`).
2. The NPC starts it: `NPC::vf_92` (`0x2976e60`) and the AI actions call the start wrappers below, which call
   `Combat_StartAttack` (`FUN_026eb380`); `AICombatMgr_OnStartAttack` (`FUN_028d0ef0`) is told (also by
   projectile fire) **[high]**.
3. The animation's model-string keys drive the timing (GP 4): `BPW`/`EPW` parry window -> counter prompt (3.3),
   `BHITWIN`/`EHITWIN` hit window, `APPLYDAMAGE` = the moment of impact (`Combat_OnModelString` ->
   `Combat_ApplyDamageKey(combat, currentAttack, combat+0x4c0)` -> range/angle test -> `Combat_DealHit`) **[high]**.
4. `Combat::vf_13` (`0x26f7b10`) is the per-frame `Combat` update (timers, hit counters `FUN_02727960`, DOTs,
   colliders, prompt visibility, sync-action aborts) **[med]**.

**Start-attack wrappers** (all `jmp Combat_StartAttack`; signature
`Combat_StartAttack(Combat* c, HRECORD attackNode, HRECORD attack, HOBJECT target, bool flag)`) **[high, asm]**:

| Wrapper | Before the jump | Callers |
|---|---|---|
| `FUN_026ed9b0(c, node, attack, target, bool)` | `c+0x150 = 0` (vec3), `c+0x170 = 0`, `c+0x178 = -1` | player `FUN_00a8c700`, `NPC::vf_92`, sync `FUN_02807d20`, `FUN_02741140`, `FUN_02a88710`, `CAIActionChangePrimaryWeapon::vf_11` |
| `FUN_026edd40(c, node, attack, target, const LTVector* aimPos, bool)` | `c+0x150 = *aimPos`, `+0x170 = 0`, `+0x178 = -1` | player (`PlayerCombat+0x458` = the aim point), `FUN_02a88710` |
| `FUN_026edd90(c, node, attack, target, HOBJECT aimObj, int aimNode, bool)` | `c+0x170 = aimObj`, `c+0x178 = aimNode`, `+0x150 = 0` | player (`PlayerCombat+0x498`, `+0x4c0`, `+0x4d8`: locked target/node) |

`Combat_StartAttack` itself: duplicate/recoil-overlap rejection (`[COMBAT|REJECT]`), crit decision (`+0x27f`), sets
`c+0x4d0 = attack`, `c+0x4d8 = node`, current target `c+0x4c0`, resets the window state (`FUN_022642e0(c+0xb0, node,
attack)`), the slow-mo/presentation (`FUN_02749020(c+0x1c8, ...)`), starts sync actions (2.5), `AttackPostData`
item-slot changes, buffs on start **[high for the listed calls]**.

### 3.2 The per-attack window state (`Combat+0xb0`) - poll it every frame

`SharedCombat_ParseWindowKeys` (`FUN_022667f0`) is called as `FUN_022667f0(combat+0xb0, keyArgs, combat+0x4a0)`
(verified at `0x26f9e7e`) and `FUN_022642e0(combat+0xb0, node, attack)` resets it at attack start, so for any
`Combat* c` **[high]**:

| Byte | Meaning (GP 4 table, offsets within the struct) |
|---|---|
| `c+0xdc` (+0x2c) | the current attack is a blocking attack (`AttackData.IsBlock`; "[COMBAT|BLOCK] Blocking = %s") |
| `c+0xe1` (+0x31) | cancel window |
| `c+0xe3` (+0x33) | **hit window** (`BHITWIN`..`EHITWIN`) |
| `c+0xe4` (+0x34) | dodge window |
| `c+0xe7` / `c+0xe8` (+0x37/+0x38) | auto links / combo links |
| `c+0xe9` (+0x39) | **parry (counter) window** (`BPW`..`EPW`) |

For the VR telegraph: per orc in range, `parryOpen = *(u8*)(orcCombat+0xe9)`, `attack = *(orcCombat+0x4d0)`,
`type = attack.AttackData.AttackType`, target `= *(orcCombat+0x4c0)`; impact comes at the `APPLYDAMAGE` key after the
window opens **[high for the fields, RT for the timing]**.

### 3.3 The counter prompt

`FUN_026e5f90(Combat* attacker /*the orc*/, int windowMs)` - called from `Combat_OnModelString`+0x32bb at the `BPW`
key; `windowMs` = key end - key start, default 250 ms **[high]**:

- needs `attacker+0x80` (AI owner) and a target that is the player (its `Combat+0x564` flag; otherwise "Not creating
  counter prompts: Target is not a PlayerObj") unless `AttackData.AlwaysDisplayCounterPrompts`; a player DOT can
  suppress it;
- the prompt FX and nav marker come from the orc's character record `AttackTypeFX` (`*0x4bb5b80`) by
  **`AttackData.AttackType`**: 0 = `NormalFX`/`NormalNavMarker`, 1 = `CannotParryFX`, 2 = `CannotCounterFX`,
  3 = `CannotBlockFX`, 4 = `RangedAttackFX` (+ the matching nav markers), overridable per attack
  (`OverrideAttackTypeFX`, `OverrideAttackTypeNavMarker`); FX handle at `attacker+0x468`, nav marker `+0x480`
  **[high for the switch, med for what each type means in play: RT]**;
- `PlayerCanCounter` = `FUN_00a80e20(PlayerCombat*)`: behaviour states (`CanCounterExcludedBehaviorStates`), the
  active item slot must be in `CanCounterWeaponModes`, during the player's own attack the window state must allow a
  counter (`FUN_02264f90(PlayerCombat+0x838)`, "Modelstrings not set to Counter"), or the companion can counter;
  `Combat.Debug.CounterPrompt` (`0x4c44998`) logs every decision **[high]**;
- event struct `CombatAttackerParryWindowArgs` (field `IsParryable`) -> script/HUD (HUD is another helper's area).

### 3.4 How the player's counter is resolved

The counter is an attack from the player's pools (data), not special code: the counter command (`InputCounterCommandID`
in the character record, GP 3) makes `PlayerCombat` evaluate its pool; the target predicates in `FUN_00a941b0`
check the target predicate record (`*0x4bad520`): `ParryWindowOpen` (the orc's `+0xe9`), `CanParry` (`FUN_02282e40`
on the orc's attack), `CanCounter` ("'%s' Rejected (%s)- Parry - Window not open / Can't Parry Attack / Can't Counter
Attack / No Target") **[high for the checks]**. The chosen counter attack starts through `FUN_00a8c700` ->
`Combat_StartAttack`; counters are usually sync actions (the orc plays the paired reaction) **[med]**. Attack
predicates (`*0x4ba8f80`, 106 fields: `TargetParryType`, `TargetBlocking`, `DodgeCounterRange`, `HitCounterRange`,
`WeaponSheathed`, ...) are evaluated by `SharedCombat_AttackPredicates` (GP 4).

### 3.5 Blocks, shields, unblockables

- Orcs that block: an orc attack with `IsBlock` = a blocking pose (`c+0xdc`); a hit on a blocking target takes the
  blocked branch of `FUN_0281a200` (block recoil, `UseBlockFXInsteadOfHitFX`, `BlockHitNodes`) **[high]**.
- Shields: `InventoryShield` (vtable `0x39ddf40`) with `ShieldWeaponData` (`*0x4c342c0`): HitPointRange, RecoilOverride,
  **AutoBlockingDirection**, DirectionDefinition, ExcludedStateSelfList, DefenseStats, CannotBlockCategories, IsBroken,
  DisplayPieces/HidePieces - shields block automatically from a direction until broken; our hit direction
  (`Combat+0x15c`) matters **[med, RT]**.
- Defender block tables: character `AIBlockTable`, AI `CAIActionCounterMeleeBlock`/`CAIGoalCounterMelee` (GP 6).
- Unblockable/uncounterable enemy attacks are `AttackType` 1-3 (prompt FX above); the player must evade.

### 3.6 Mapping a physical blade block onto the game

| Option | How | Pros / cons |
|---|---|---|
| A. Counter by input | when our blade meets the orc's weapon while `orcCombat+0xe9` is set and `AttackType == 0`, press the counter command for one frame through the virtual gamepad (GP 3) | all game rules, XP, hit counter, counter animations; but the counter is a paired animation that moves the player |
| B. Cancel + recoil | hook `Combat_ApplyDamageKey_RangeAngle` (`FUN_026e49a0`) for attacks whose target is the player; if our blade blocked inside the window, return without calling `DealHit`, then make the orc stagger: `FUN_02811140(hPlayer, orcChar, parryAttack, &hit, &ds /*amount 0*/)` with an attack record whose `RecoilTypeHit` is a parry stagger (or `FUN_02810af0` with the orc attack's `RecoilTypeParry`) | no player movement; but the orc's current attack must allow the recoil (`FUN_0280fe60`); hit counter/XP not advanced unless we also count it **[med, RT]** |
| C. Hybrid | B for normal blocks, A (counter) only for the "perfect" timing at the start of the window | |

Shared need: the orc's weapon pose (weapon model node or the hand bone, section 4) and the window state above.

## 4. (d) The player's weapons and projectiles

### 4.1 Inventory, slots and the weapon objects

- Inventory = `CCharacter+0x910` (GP). Accessors **[high, asm]**: `FUN_02311ee0(inv)` = active slot index
  (`inv+0x280`, 0..7); `FUN_02311e20(inv)`/`FUN_02311e50(inv)` = active `InventoryItem*` (`inv+0x30 + slot*0x38`);
  `Inventory_GetEquippedWeapon` (`FUN_02311e80`) = that item's record (`item+0x78`); `FUN_02310250(inv)` = owner
  HOBJECT (`*(inv+0x1f0)+8`). Slot names (enum strings in the item schemas): `MeleePrimary`, `MeleeSecondary`,
  `MeleeAux`, `RangedAux`, `QuickThrow`, plus wraith/collectible slots; the model-string keys `ACTIVATE_MELEE`,
  `ACTIVATE_MELEEAUX`, `ACTIVATE_RANGED`, `ACTIVATE_RANGEDAUX`, `ACTIVATE_WRAITH`, `ACTIVATE_QUICKTHROW`, `EQUIPITEM`,
  `HIDEITEM`, `SHOWITEM`, `SHEATH`, `UNSHEATH` (`FUN_02315f40`) and `HIDEWEAPON`/`SHOWWEAPON`(`FX`)/`HIDEALLWEAPONFX`
  (`FUN_0231fa90`) switch them from animations **[high for the strings]**. Urfael/Azkar/Acharn are data names (not in
  the binary); which slot holds which is **RT** (expected: sword = melee primary, dagger = melee secondary/aux used by
  stealth kills, bow = ranged) **[low]**.
- **Each weapon is its own model object:** `InventoryItem` (`GameBase`, vtable `0x38d0de8`) keeps a vector of model
  entries at `item+0x28` (0x48 bytes each); `FUN_0231e150(item)` returns the first entry's object, whose `+0x08` pos /
  `+0x14` quat are read as the weapon's transform by the damage code (step 13) **[med]**. The client draws it with
  `CClientInventoryItemModel` (`GameBase` + embedded `CSimpleModel` at `+0x28`, model record at `+0x250`; vtable
  `0x3b2dbf8`), created by `FUN_02320b00`/`FUN_02334d40` **[high for the class, med for the creators]**.
- **Where it attaches:** `Inventory.Item` (`*0x4c314c0`) field 2 `EquippedItemData` (`*0x4c34760`): **`ActiveNode`**
  (node/socket while drawn), **`InactiveNode`** (while sheathed), `Models` (list of {Weight, ModelRecord,
  CompanionModelRecord}), `InactiveAnimation`/`ActiveAnimation`(`Loop`), `PersistentClientFX`, `AlwaysUseFullModel`;
  `ModelRecord` (`*0x4c33f80`): ItemType, ItemSlot, PreviewModel, Model, AudioBlockName, AudioAttackName,
  BlockFXContext, ... (`FUN_02320b00` reads exactly these) **[high for the fields, RT for the node names]**. Other item
  data: field 4 `BaseWeaponData` (`*0x4c33300`: SurfaceFXType, CombatCirclePolicy, CanSheath, KeepDrawn,
  AlwaysCombatReady, AudioEmitterNode, BaseWeaponDamage), 5 `ShieldWeaponData`, 6 `RangedWeaponData`.
- **For a controller-driven sword:** (1) hide the game's sword (`HIDEWEAPON` path or `SetPieceHidden` on the item's
  model, GP 2) and draw our own; or (2) move the item model object to the controller pose each frame after the
  animation update (its transform is read back by the game only for FX/breaking, not for hits) **[low, RT]**. For
  parry detection against an orc's weapon: read the orc's active item model object (`FUN_0231e150(FUN_02311e20(inv))`)
  transform, or its hand node **[med, RT]**.

### 4.2 Arrows: how the game spawns a projectile

```
void FUN_02331c30(Inventory* inv, HRECORD rangedItem, WindowState* ws /*Combat+0xb0*/, int node /*Combat+0x17c*/,
                  LTRigidTransform* outFireXf)                              // fire transform from RangedWeaponData.FireNode
void FUN_02332c50(Inventory* inv, Combat* shooter, HRECORD rangedItem /*0 = active item*/,
                  HRECORD accuracyOverride /*0 = default*/, const LTRigidTransform* fireXf)   [high: both SHOOT callers]
```

- The `SHOOT` key (`Combat_OnModelString`+0x2bd2) and `Combat_StopAttack`+0x979 call `FUN_02331c30` then
  `FUN_02332c50` with `rangedItem = AttackData.RangedWeaponOverride`, `accuracy = AttackData.RangedAccuracyOverride`;
  `FUN_02331c30` gets the `FireNode` (`GetNode` + `GetNodeTransform(world)`, or `IEngineModel` vtbl+0x168 = the node at
  the key's animation time) **[high]**.
- `FUN_02332c50`: tells the AI manager (`AICombatMgr_OnStartAttack`), picks the **aim point**: `shooter+0x170`
  object/`+0x178` node (`GetNode` on the target), else `FUN_026c7cf0(shooter)` (= `shooter+0x150`, or the player's aim
  point when mounted), else `fireXf.pos + rotate(fireXf.quat, +Z) * MaxRange`; applies `OrientTowardsTarget`; builds a
  look-at quaternion; creates the object: `IWBEngine` (`*0x466afa0`) vtbl+0x78("CProjectile") = class, vtbl+0x50 =
  create (position = `fireXf.pos`, rotation = look-at), user data = {inv, shooter, record, target, aim pos, node}; with
  `ImmediateImpact` it ray casts at once (`IEnginePhysics` vtbl+0x200, GP 5) and resolves the hit in
  `FUN_027c1d90`; `FireStimulus`; `AttackSelfOnFire` -> `FUN_0281c960` **[high for the steps]**.
- `RangedWeaponData` (`*0x4c32880`, 52 fields): RangedCameraTag(OnEnemy), ProjectileModel, ShowModelWhileTraveling,
  RequireLOS, **FireNode**, RangedAccuracySetup, **ImmediateImpact**, **Velocity**, UseShapeCast, ShapeRadius,
  SlowMoVelocity/VelTransTime/VelBias, **MaxRange**, StickyProjectileData, Lifetime, RemoveModelOnImpact,
  OrientTowardsTarget, SurfaceFXType, WeaponFireFX, ..., ImpactFX(Sequence/HitPlayer/HitOther), FireStimulus,
  WorldImpactStimulus(FromHost), WorldImpactProp, MinDistance, **EnableGravity**, **GravityWeight**, **EnableHoming**,
  **MaxTurnSpeed**, AnimInAir, AnimImpact, UseCombatAttachedObject, **AttackOverrides**, NeverStop, KeepOnGround(Height),
  NeverStopAOE, UseSplineMovement, Spline..., AttackSelfOnFire **[high]**.
- `CProjectile` (`GameBase`, vtable `0x39986e0`): flight/impact `FUN_027be5d0` (13.9 KB): hit node choice and
  `AttackOverrides` ("Find Override RangedData = %s, CharType = %s, HitNode = %s", "Rejected %s, Required Hit Node";
  override record `*0x4c32720`: RequiredCharacterTypeList, **RequiredHitNode**, RequiredItem, TargetRequiredItem,
  Chance, **AttackOverride** = e.g. the head-shot attack) **[high for the strings/fields]**.
- **For a physical bow:** compute `fireXf` from the controller (nock position, rotation with +Z along the arrow), set
  `playerCombat+0x170 = 0`, `+0x178 = -1`, `+0x150 = 0` (or an aim point), and call
  `FUN_02332c50(inv, playerCombat, 0, 0, &fireXf)` on the game thread; draw strength -> choose between ranged item
  records or scale is not available (velocity is data) **[med, RT: ammo/focus cost, the bow weapon mode
  (`CPlayerCombatWeaponMode`, slow-mo) and whether the active slot must be the bow]**.

## 5. (e) Skeletons, hit zones and physics bodies

- **Bone names** in code (GP 2 plus this pass): `Pelvis`, `Torso_Base`, `Spine4_joint`, `Neck`, `L_Hand`, `R_Hand`,
  `L_Armu`, `R_Armu`, `R_Foot`; sockets `SocketHead`, `SocketAttach`, `TargetSocket`; default `CombatTargetNode`
  "LeftHand" **[high for the strings]**. The rest are only in the model data: enumerate per character at runtime
  (`IEngineModel` GetNodeName 0..n, GP 2) **[RT]**. Orcs (Uruk) and Talion appear to share the naming scheme
  (`L_`/`R_` prefixes, `_joint` suffixes) **[low]**.
- **The game's own hit spheres** (use them as our capsule radii): the character combat record field 37 `AimAtNode`
  / 55 `AimAtNodeSetup` -> `Combat/AimAtNode/Nodes` records (`*0x4bb5f60`): **`NodeName`, `Radius`**,
  `ProjectileHitNodes` (node-name list), `RemoveAttachedProjectilesOnSever`, `CanAutoAim`. `FUN_027bb490` tests the
  projectile segment against a sphere of `Radius * characterScale` (`FUN_00a9f3e0`) around `NodeName`
  ("AimAtNode: '%s' Accepted/Rejected - Did not intersect, Radius = %f") and then picks the closest of
  `ProjectileHitNodes` ("New Best ProjectileHitNode: '%s' DistanceSqr = %f"); model calls through the object's
  `ISimpleModel` vtbl+0x1f0 (GetNode) and vtbl+0x220 (GetNodeTransform, world) **[high]**. Debug log
  `[COMBAT|PROJ_HIT]` (`*(bool*)0x4c473d8`).
- Hit FX nodes per character: `HitFXSetup` (`*0x4baeca0`: HitFXMap, **HitNodes**, **BlockHitNodes**, ProximityFXMap)
  and `*0x4baeee0` (FXMgr, HitFX, HitNodeParentedFX, HitParentedFX, AttackerWeaponHitFX) - where blood/sparks attach
  **[high fields]**. `RecoilNodes`/`DeathNodes`/`StateNodes` are *animation* nodes (behaviour tree), not bones.
- `CharacterRangedHitBox` (`GameBase`, vtable `0x3967e60`) is created per character by `CCharacter::vf_10`
  (`0x26abcd0`): a separate object used as the ranged/aim hit box **[high for the creation, med for the use]**.
- **Physics bodies:** a live character has a character proxy capsule (`CPhysicsCharacter`, GP 5; combat states can
  switch it, `EnableCollisionCapsule`). Per-bone rigid bodies exist for ragdolls (`hkaRagdollInstance`,
  `ConstraintRagdoll`); the death/ragdoll switch is `FUN_026a45f0` (CCharacter, reads the ragdoll death record,
  "RigidBody", calls `IWBEngine::vf_03` = physics and the character-physics code `FUN_020ec140`, `FUN_020ec530`,
  `FUN_020ee1c0` "RagdollCollision", `FUN_020eb270` "RagdollStretching"); `RESUMERAGDOLL`/`DELAYRAGDOLL`/
  `DISALLOWRAGDOLL` keys; `Combat.RagdollReaction` (2.1) suggests short powered-ragdoll hit reactions **[med]**.
  Static reading found **no per-bone bodies on a live, animated orc that a ray or shape cast could hit**: the combat
  code never casts against limbs, it uses distances and the AimAtNode spheres. So the VR layer should build its own
  capsules from bone transforms (GP 5 plan), sized from `AimAtNode.Radius` where available **[med, RT: cast the
  `IEnginePhysics` ray at an orc and see what it returns]**.

## 6. (f) Physics impulses (kicks, shoves, ragdolls)

- **Rigid body API** (`CPhysicsRigidBody`, vtable `0x34c02e8`, 55 slots; Havok body at `this+0x48`, motion at
  `body+0x150`; each call first `hkpEntity::activate` `0x178dd30`; vectors in game units, ×0.01 -> Havok metres where
  noted) **[high for the Havok calls; names from the matching Havok actions: `hkpMouseSpringAction`/`hkpDashpotAction`
  use the point-impulse helper, `hkpGravityGun` the linear one, `hkpReorientAction` the angular one]**:

| Slot / function | Call | Meaning |
|---|---|---|
| vf_38 `0x99fbc0` (`this, const LTVector* v`) | v×0.01 -> `0x1794020` | SetLinearVelocity (cm/s) |
| vf_40 `0x99fcd0` (`this, const LTVector* w`) | `0x1794030` | SetAngularVelocity (rad/s) |
| vf_42 `0x9a0270` (`this, const LTVector* j`) | `0x17970f0` | ApplyAngularImpulse |
| **vf_51** `0x9a0190` (`this, const LTVector* impulse, const LTVector* point`) | both ×0.01 -> `0x17970e0` | **ApplyPointImpulse** (point in cm, world) |
| `FUN_009a00f0` (`this, const LTVector* impulse`) | ×0.01 -> `0x17970d0` | ApplyLinearImpulse (not in a vtable; no callers) |

- Level objects that push things: `PhysicsImpulseDirectional` (`0x39939a8`, fields ImpulseForce, RemoveWhenDone) and
  `PhysicsImpulseRadial` (`0x39946c0`, ImpulseForce, RadiusMin, RadiusMax) - spawnable through the object factory like
  `CProjectile` (4.2) for a radial "wraith shove" **[med]**.
- Death push: the hit direction (`DamageStruct+0x78`) is stored as the death direction (`CCharacter+0x1a0`) and the
  ragdoll death record scales it (`BodyDeathForce`, `BodyDirDeathForce`, `BodyDeathScaleVelocity`,
  `BodyDeathClampVelocityMax`, `BodyDeathDirMinY`) **[med]**: our killing blow's direction already throws the body that
  way.
- **Open:** how to get from an orc to its ragdoll `CPhysicsRigidBody` objects (the ragdoll instance owner in the
  character-physics code `FUN_020ec140`/`FUN_020ec530`, and `IEnginePhysics` slots 65-72, GP 5) - not mapped
  **[RT/next static step]**. For live orcs, use attack records with knockback recoils (2.1) rather than physics.

## 7. (g) Wraith abilities and finishers as actions

Everything here is an **attack record** (Combat/Attack) picked from the player's attack tree by input and predicates;
there is no "Drain()" function. Three ways in, from safest to most direct **[high for the structure]**:

1. **Input** (recommended for v1): press the ability's command for a frame through the virtual gamepad (GP 3:
   `CombatPrimary/Secondary/Tertiary..`, `SpecialAttack1..4`, `SpecialAttackModifier` - which button is which ability
   is data). The game evaluates predicates, picks the target, plays the animation, starts sync actions.
2. **Player attack start**: the per-frame `PlayerCombat` update `FUN_00a8e120(PlayerCombat*)` turns input into an
   attack: `FUN_022617f0` (input context) -> `FUN_0229aba0(combat, ctx, &node, &attack)` (tree/pool selection,
   `SharedCombat_SelectAttackFromPool`) -> target `FUN_00a85c90` -> **`FUN_00a8c700(PlayerCombat* pc, HRECORD node,
   HRECORD attack, HOBJECT target)`**, which updates the client side (`FUN_02274f10(pc+0x80, node, attack)` = input-buffer bookkeeping,
   "[COMBAT|INPUT] ClearBufferedInput"; `FUN_00a89200`/`FUN_02265bf0` on `pc+0x160` and the window state `pc+0x838`;
   `FUN_00a976f0`; HUD) and the server side (`FUN_026ed9b0`/`FUN_026edd40`/`FUN_026edd90` ->
   `Combat_StartAttack`, 3.1). Calling `FUN_00a8c700` with a chosen `node` (Combat/Node record, `*0x4ba20c0`: Type,
   RequiredTransitionType, Predicate, Pools, Links, LinkToRoot, ChordWindow, DoubleTapWindow,
   CanBeSelectedAsAttacker, FacialAnimTags, KBMNode) and `attack` skips the predicates **[med, RT: whether the
   animation really starts this way]**.
3. **Effects without animation**: `Combat_DealHit` with the ability's attack record applies its damage/state/brand
   data (1.6); brand directly with **`FUN_0297b770(NPC*)`** (what the `AI.AddBrand` console command `FUN_0297b9f0`
   calls through `NPC` vtbl+0x1a0, default `0x297b9e0`; also called for `BrandTargetOnHit` and from model strings)
   **[high for the call chain, med for "is the full brand"]**; combat states with `FUN_026dbd00` (2.2); AOE with the
   AOE manager `FUN_02735930(*0x4c43b80, hSource, &pos, aoeRecord)` (as `FUN_026c06b0` does).

What the data says about each (fields from `attrs.tsv`; concrete records are **RT**):

| Ability | Data hooks |
|---|---|
| Executions / finishers | sync actions (`Combat/Sync`, 2.5) with `KillTargetDamage`, `SEVER` keys; gated by `HitCounterRange` (combo) / `HitCounterCharge` predicates and the crit flag (`CanCrit`, `+0x27f`) |
| Drain | sync action + resource gains (`TargetResourceGainOnSrcDeath/Recoil`, energy/wraith tokens `WraithTokenRange`/`WraithTokenPct` predicates), usually needs a stunned target (combat state predicates `StateTargetList`) |
| Brand | `AttackData.BrandTarget`, `BrandTargetOnHit`, `ActivateBrandOnHit`, `HitBrandedOnStart`; keys `BRANDNOFX`, `HITBRANDED`; `Combat.BrandedTargetMgrData`; script `ActivateBrand` `FUN_024066e0`, `RemoveBrand` `FUN_023fa340` |
| Stun / wraith stun | an attack whose `Damage.StateMods` apply a stun state; `AttackData.AOE`/`AOEOnStart` for area versions |
| Wraith flash | AOE attack: `Combat/AOE` (`*0x4bac480`: Radius, Angle, MaxAIToHit, CanHitMainWorld, AttackOverrideChance, AttackOverride, Attack, AOEFX, Included/Excluded Item/CharacterType/CombatState lists, MustBeBranded, MustBeNamed, RequiredTargetHealthPct/Range, HitStickyProjectiles) |
| Bow / slow-mo aim | `CPlayerCombatWeaponMode` (`Combat/PlayerWeaponModes/*`), `PlayerWeaponModeSlowMo` predicate; arrows 4.2 |

Preconditions to respect when going direct: energy (`EnergyTokenCost`, `Combat.DisableEnergyCost` `0x4c44398`), the
target's state and the predicates (`SharedCombat_AttackPredicates`, 106 predicate fields `*0x4ba8f80` such as
`TargetHealthPctRange`, `RequiredBuffTarget`, `AIIsAware`, `TargetHasBrand`), and `Combat.AwardPower` (GP) for tests.

## 8. (h) API table for the VR combat layer

Signatures from the decompile/disassembly (SysV: RDI, RSI, RDX, RCX, R8, R9, then stack; `LTVector` by value = XMM0
x,y + XMM1 z). "Conf." is for the signature and effect; **RT** = verify in a test run before relying on it.

| # | Function | Address | Signature | Preconditions | Side effects | Conf. | RT |
|---|---|---|---|---|---|---|---|
| 1 | `Combat_DealHit` | `0x26d4ff0` | `void(LTVector hitPos, Combat* attacker, HOBJECT target, HRECORD attack, bool forceKill)` | game thread; `attack` != 0; set `attacker+0x15c` = hit dir; target has a Combat host | full hit pipeline (1.2, 1.5), death, XP, Nemesis, sever-on-kill, AOE-on-hit | high | yes (no-anim use) |
| 2 | `ServerCombat_ApplyAttackDamage` | `0x281a200` | `void(HOBJECT attacker, CCharacter* target, HitInfo*, HRECORD attack, HRECORD weapon, bool isCrit, bool forceKill, bool mayIncHitCounter)` | as 1; `HitInfo_Init` first; use when we need `HitInfo+0x0c/+0x30` (source position) | as 1 minus prop/AOE handling | high | yes |
| 3 | `HitInfo_Init` | `0x2819890` | `void(HitInfo*)` | 0x40-byte buffer | zero, `+0x24 = 1.0` | high | no |
| 4 | `FUN_02811140` hit + recoil | `0x2811140` | `void(HOBJECT attacker, CCharacter* target, HRECORD attack, HitInfo*, DamageStruct*)` | initialised `DamageStruct` (vtable `0x34ef650`, lists self-linked) | damage (amount from ds), `RecoilTypeHit` recoil via `NPC::vf_61` | high | yes (amount 0 = recoil only) |
| 5 | `FUN_02810af0` block recoil | `0x2810af0` | `void(HOBJECT attacker, CCharacter* defender, HRECORD attack, HRECORD recoil|0, HitInfo*)` | defender alive | defender plays `RecoilTypeBlock`/given recoil | high | yes |
| 6 | `FUN_0280fe60` can recoil | `0x280fe60` | `bool(Combat* target, HOBJECT attacker, HRECORD attack, HRECORD newState, bool* abortSync)` | - | none (query) | high | no |
| 7 | `DamageStruct_DoDamage` | `0x2763050` | `void(DamageStruct*, HOBJECT target)` | layout 1.3 | raw health change, no recoil/XP | high | no |
| 8 | `FUN_026dbd00` set combat state | `0x26dbd00` | `void(Combat*, HRECORD node, HRECORD sourceAttack, HRECORD state|0, bool)` | state = Combat/States record | stun/knockdown/... with its duration | med | yes |
| 9 | `FUN_026d4670` sever | `0x26d4670` | `void(Combat* victim, HRECORD severType, const LTVector* vel, const LTVector* rot, HOBJECT attacker)` | `victim+0x90` != 0; normally on a dying/dead target | dismember/decapitate, Nemesis "severed" | high | yes |
| 10 | `FUN_026f61f0` on death | `0x26f61f0` | `void(Combat* victim, HRECORD attack, Combat* killer, x)` | called by the pipeline; do not call twice (`+0x288` guard) | death events, XP, loot, Nemesis, sever-on-kill | high | no |
| 11 | Combat/Attack lookup | `0x2166700` / `0x2166740` / `0x2166760` / `0x2166790` | `void* cat()`, `uint count()`, `HRECORD byIndex(uint)`, `HRECORD byName(const char*)` | database loaded (in game) | none | high | yes (names) |
| 12 | `DBRecord_GetName` | `0x25aa530` | `const char*(HRECORD)` | - | none | high | no |
| 13 | field readers | `0x280a0b0` / `0x280a2d0` / `0x26bfb10` / `0x26bfe40` / `0xa09d60` | `T(HRECORD rec, Field* f /*= *GLOBAL + i*0x30 + 8*/, T* default)` | field globals from `attrs.tsv` | none | high | no |
| 14 | window state | `Combat+0xb0` | bytes `+0xe3` hit, `+0xe4` dodge, `+0xe9` parry window, `+0xdc` blocking attack | read on the game thread | - | high | timing |
| 15 | `FUN_026e5f90` counter prompt | `0x26e5f90` | `void(Combat* orc, int windowMs)` | hook to observe (do not call) | prompt FX + nav marker | high | no |
| 16 | `FUN_00a80e20` player can counter | `0xa80e20` | `bool(PlayerCombat*)` | - | none (query, logs) | high | no |
| 17 | `Combat_StartAttack` + wrappers | `0x26eb380`, `0x26ed9b0`, `0x26edd40`, `0x26edd90` | `void(Combat*, HRECORD node, HRECORD attack, HOBJECT target, bool)`; wrappers add aim pos / aim object+node | normally driven by the animation side | attack state, crit, sync actions, buffs | high | yes (direct use) |
| 18 | `FUN_00a8c700` player start attack | `0xa8c700` | `void(PlayerCombat*, HRECORD node, HRECORD attack, HOBJECT target)` | `PlayerCombat` = `Player+0x2e8` (GP); `pc+0xa08` server Combat | client attack + server start | med | yes |
| 19 | `FUN_02331c30` fire transform | `0x2331c30` | `void(Inventory*, HRECORD rangedItem, WindowState*, int node, LTRigidTransform* out)` | ranged item active | none | high | no |
| 20 | `FUN_02332c50` fire projectile | `0x2332c50` | `void(Inventory*, Combat* shooter, HRECORD rangedItem|0, HRECORD accuracy|0, const LTRigidTransform* fireXf)` | clear/set `shooter+0x150/+0x170/+0x178` (aim) | spawns `CProjectile`, AI notified, stimulus | high | yes |
| 21 | inventory accessors | `0x2311ee0` / `0x2311e20` / `0x2311e80` / `0x231e150` | slot index / active `InventoryItem*` / its record / its model object (HOBJECT-like: `+0x08` pos, `+0x14` quat) | - | none | high / high / high / med | model object: yes |
| 22 | `FUN_0297b770` brand NPC | `0x297b770` | `void(NPC*)` (then `FUN_026a1490(npc, 0, 1)` as `AI.AddBrand` does) | target is an `NPC` | brand state (alignment/AI) | med | yes |
| 23 | `FUN_026f46a0` add XP | `0x26f46a0` | `void(Combat*, HRECORD xpSource)` | xp from `FUN_0229fc20(rec)` | XP, HUD, progression | high | no |
| 24 | AOE spawn | `0x2735930` | `obj(AOEMgr* /* *0x4c43b80 */, HOBJECT source, const LTVector* pos, HRECORD aoe)` | Combat/AOE record | area attack | med | yes |
| 25 | rigid body impulses | vtable `0x34c02e8` slots 38/40/42/**51**, `0x9a00f0` | `void(CPhysicsRigidBody*, const LTVector* impulse[, const LTVector* point])` | a body pointer (ragdoll path open) | Havok velocity/impulse | high (calls) | yes |
| 26 | `Combat_ApplyDamageKey_RangeAngle` | `0x26e49a0` | `void(Combat* attacker, HRECORD attack, HOBJECT target)` | hook point for VR parry (target == player) | the orc's APPLYDAMAGE hit test | high | no |
| 27 | debug switches | `0x4c449d8` AlwaysSever, `0x4c44a98` AlwaysCrit, `0x4c443d8` AlwaysApplyState, `0x4c44998` Debug.CounterPrompt, `0x4c44058` Vis.DamageNumbers, `0x4c473d8` projectile-hit log, `0x4c44d98` Debug.XP | `bool` at the address (GP 4 cvar layout) | - | test aids | high | no |

### 8.1 Check sites (format of `mod/spike1/check_sites.py`: name, kind, site, context address, context bytes, target, target prologue)

For each function the site is one game call to it (or its vtable slot), so a check script can confirm both the call
and the target's first 15 bytes. Generated from the binary copy by `reference/work-combat/sites.py`.

```
SITE("DealHit", K_CALL, 0x26e5468, 0x26e5455, "ef f3 0f 7e 84 24 a8 00 00 00 f3 0f 10 8c 24 b0 00 00 00 e8 83 fb fe ff", 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55"),
SITE("ApplyAttackDamage", K_CALL, 0x26d50b9, 0x26d50a6, "a0 04 00 00 48 89 e9 45 0f b6 c9 4c 89 d6 48 8d 54 24 40 e8 42 51 14 00", 0x281a200, "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 89"),
SITE("HitInfoInit", K_CALL, 0x26d5024, 0x26d5011, "0f 11 4c 24 18 48 85 d2 0f 84 d9 02 00 00 48 8d 7c 24 30 e8 67 48 14 00", 0x2819890, "66 0f ef c0 48 c7 47 28 00 00 00 00 48 b8 00"),
SITE("HitAndRecoil", K_CALL, 0x26e4842, 0x26e482f, "00 00 00 c7 84 24 e8 00 00 00 00 00 00 00 4c 89 44 24 78 e8 f9 c8 12 00", 0x2811140, "55 48 89 e5 41 57 4d 89 c7 41 56 49 89 fe 41"),
SITE("ApplyDamageRaw", K_CALL, 0x2811234, 0x2811221, "4d 89 f9 4c 89 e1 48 89 de 48 8b 95 08 ff ff ff 4c 89 f7 e8 47 fc ff ff", 0x2810e80, "41 57 49 89 d7 41 56 49 89 ce 41 55 49 89 fd"),
SITE("BlockRecoil", K_CALL, 0x281aabc, 0x281aaa9, "48 8b 95 90 fb ff ff 31 c9 48 89 de 48 8b bd 70 fb ff ff e8 2f 60 ff ff", 0x2810af0, "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81"),
SITE("CanRecoil", K_CALL, 0x2810b77, 0x2810b64, "89 ee 48 89 c7 4c 8d 85 0f ff ff ff c6 85 0f ff ff ff 00 e8 e4 f2 ff ff", 0x280fe60, "41 57 41 56 41 55 49 89 f5 41 54 4d 89 c4 55"),
SITE("DoDamage", K_CALL, 0xa33982, 0xa3396f, "00 48 85 c0 0f 84 97 00 00 00 48 8b 70 08 48 8d 7c 24 10 e8 c9 f6 d2 01", 0x2763050, "48 85 f6 74 4b 48 8b 46 48 48 85 c0 74 42 48"),
SITE("ApplyDmgRangeAngle", K_CALL, 0x26e5af5, 0x26e5ae2, "41 55 41 54 49 89 d4 55 48 89 f5 53 48 89 fb 48 83 ec 50 e8 a6 ee ff ff", 0x26e49a0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 01"),
SITE("SetCombatState", K_CALL, 0x26dc609, 0x26dc5f6, "00 48 8b b3 d8 04 00 00 48 89 df 31 c9 41 b8 01 00 00 00 e8 f2 f6 ff ff", 0x26dbd00, "41 57 41 56 41 55 41 54 55 53 48 83 ec 28 4c"),
SITE("OnDeath", K_CALL, 0x281985d, 0x281984a, "48 89 c5 e8 be ac eb ff 31 c9 48 89 da 48 89 c6 48 89 ef e8 8e c9 ed ff", 0x26f61f0, "80 bf 88 02 00 00 00 0f 85 f3 0e 00 00 55 48"),
SITE("Sever", K_CALL, 0x26fb641, 0x26fb62e, "00 00 48 8d 54 24 28 66 0f d6 44 24 34 f3 0f 11 4c 24 3c e8 2a 90 fd ff", 0x26d4670, "41 57 4c 8d 8f 30 05 00 00 49 89 cf 41 56 49"),
SITE("SeverOnKill", K_CALL, 0x26f7044, 0x26f7031, "8d 8d 40 ff ff ff 48 8d 95 d0 fe ff ff 4c 89 ee 48 89 df e8 27 d6 fd ff", 0x26d4670, "41 57 4c 8d 8f 30 05 00 00 49 89 cf 41 56 49"),
SITE("ParseWindowKeys", K_CALL, 0x26f9e88, 0x26f9e75, "c2 ff 48 8b 93 a0 04 00 00 48 8d bb b0 00 00 00 48 89 ee e8 63 c9 b6 ff", 0x22667f0, "41 57 41 56 41 55 49 89 fd 41 54 49 89 f4 55"),
SITE("CounterPrompt", K_CALL, 0x26fc32b, 0x26fc318, "b7 40 16 66 39 c2 0f 83 bc 09 00 00 29 d0 89 c6 48 89 df e8 60 9c fe ff", 0x26e5f90, "48 83 bf 80 00 00 00 00 0f 84 32 02 00 00 41"),
SITE("PlayerCanCounter", K_CALL, 0x26e664c, 0x26e6639, "d2 fd ff ff 48 89 df e8 0b 36 fb ff 48 8b b8 b8 02 00 00 e8 cf a7 39 fe", 0xa80e20, "55 53 48 89 fb 48 83 ec 78 48 8b b7 d0 0a 00"),
SITE("NewSync", K_CALL, 0x26eb93c, 0x26eb929, "01 00 00 48 85 ff 0f 84 2d 19 00 00 48 8b 34 24 4c 89 e2 e8 5f c8 11 00", 0x28081a0, "41 57 41 56 49 89 f6 41 55 41 54 49 89 d4 55"),
SITE("StartAttackNoPos", K_CALL, 0xa8d04d, 0xa8d03a, "10 44 24 60 45 89 c8 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 5e 09 c6 01", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("StartAttackAtPos", K_CALL, 0xa8d02b, 0xa8d018, "4c 8d 83 58 04 00 00 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 10 0d c6 01", 0x26edd40, "f3 41 0f 10 08 f3 0f 11 8f 50 01 00 00 f3 41"),
SITE("PlayerAttackStarted", K_CALL, 0xa8dbf9, 0xa8dbe6, "c0 0f 84 39 01 00 00 4c 89 e1 4c 89 d2 4c 89 fe 48 89 df e8 02 eb ff ff", 0xa8c700, "41 57 41 56 41 55 41 54 55 53 48 81 ec 18 01"),
SITE("FireXf", K_CALL, 0x26fbc42, 0x26fbc2f, "00 00 c7 44 24 58 00 00 80 3f 48 c7 44 24 50 00 00 00 00 e8 e9 5f c3 ff", 0x2331c30, "41 57 41 89 cf 41 56 49 89 d6 41 55 49 89 f5"),
SITE("FireProjectile", K_CALL, 0x26fbc58, 0x26fbc45, "c3 ff 4c 89 f1 4c 8d 44 24 40 4c 89 ea 48 89 de 4c 89 e7 e8 f3 6f c3 ff", 0x2332c50, "41 57 41 56 41 55 41 54 55 48 89 f5 53 4c 89"),
SITE("AddXP", K_CALL, 0x26f4eb8, 0x26f4ea5, "48 89 85 78 fe ff ff 74 0f 48 8d b5 40 fe ff ff 48 89 df e8 e3 f7 ff ff", 0x26f46a0, "41 54 55 48 89 f5 53 48 83 bf 88 00 00 00 00"),
SITE("BrandNPC", K_CALL, 0x26f8c1d, 0x26f8c0a, "3d 4c 89 f7 e8 7d e5 f9 ff 48 8b bb 80 00 00 00 41 89 c6 e8 4e 2b 28 00", 0x297b770, "41 54 55 53 48 89 fb 48 83 ec 10 48 8b 3d 8e"),
SITE("AttackCategory", K_CALL, 0x22925d7, 0x22925c4, "c7 84 24 90 00 00 00 49 ba 85 03 48 89 84 24 98 00 00 00 e8 24 41 ed ff", 0x2166700, "48 8b 05 c9 01 9f 02 c3 0f 1f 84 00 00 00 00"),
SITE("AttackByIndex", K_CALL, 0x22cc3b8, 0x22cc3a5, "ee 0f 86 f0 03 00 00 39 6c 24 30 0f 86 e6 03 00 00 89 ef e8 a3 a3 e9 ff", 0x2166760, "48 8b 05 69 01 9f 02 48 85 c0 74 1e 3b 78 28"),
SITE("OnHitEvent", K_CALL, 0x2819a9a, 0x2819a87, "a8 ff 4c 8b b5 38 ff ff ff 48 83 c4 40 4c 89 e6 4c 89 f7 e8 e1 8a ed ff", 0x26f2580, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("RB_ApplyPointImpulse", K_VSLOT, 0x34c0480, 0x34c0480, "90 01 9a 00 00 00 00 00", 0x9a0190, "41 55 49 89 fd 41 54 55 48 89 f5 53 48 89 d3"),
SITE("RB_SetLinearVelocity", K_VSLOT, 0x34c0418, 0x34c0418, "c0 fb 99 00 00 00 00 00", 0x99fbc0, "41 54 49 89 fc 55 53 48 89 f3 48 83 ec 10 48"),
SITE("NPC_PlayRecoil", K_VSLOT, 0x3a2b5b8, 0x3a2b5b8, "c0 99 96 02 00 00 00 00", 0x29699c0, "41 57 41 56 41 55 41 54 49 89 cc 55 48 89 d5"),
SITE("Combat_Update", K_VSLOT, 0x396bba8, 0x396bba8, "10 7b 6f 02 00 00 00 00", 0x26f7b10, "41 57 41 56 41 55 41 54 55 53 48 89 fb 48 81"),
SITE("CPlayerObj_IsPlayer", K_VSLOT, 0x3995a78, 0x3995a78, "20 78 7b 02 00 00 00 00", 0x27b7820, "b8 01 00 00 00 c3 66 2e 0f 1f 84 00 00 00 00"),
SITE("StartAttack", K_CALL, 0x26ed1e6, 0x26ed1d3, "c7 82 78 01 00 00 ff ff ff ff 4c 89 14 24 44 88 4c 24 20 e8 95 e1 ff ff", 0x26eb380, "41 57 45 31 ff 41 56 49 89 f6 41 55 41 54 49"),
```

## 9. What needs a runtime check, open questions

Runtime checks, in the order a first combat spike would hit them:

1. Dump the attack database once (Combat/Attack count/byIndex/name + AttackData/AttackPostData/Damage fields) and the
   player's character combat record; pick candidate light/heavy/finisher/kill attacks **[needed for everything]**.
2. `Combat_DealHit` from our frame hook on a grunt with the player's Combat, no animation: damage, recoil, death,
   XP, damage numbers (`0x4c44058`), and the Nemesis record of a captain kill; with `Combat.Debug.ApplyDamage/Stats/
   Attack` logging on (GP 4).
3. `+0x15c` direction and `HitInfo+0x0c/+0x30`: does the recoil follow the blade?
4. Hit counter: do successive `DealHit`s with different attack records raise the combo; does `+0x27a` need resetting?
5. Decapitation: a kill with an attack that has `SeverTargetOnKill`; then `FUN_026d4670` directly.
6. Window flags `+0xe9`/`+0xe3` timing per orc vs the `APPLYDAMAGE` moment (the VR telegraph lead time).
7. Counter by virtual input inside the window vs cancel+recoil (3.6 B).
8. Arrows: `FUN_02332c50` with our own transform; MaxRange/aim behaviour; ammo/focus.
9. The weapon model object (`FUN_0231e150`) - can we move/hide it; the orc weapon pose for parries.
10. `IEnginePhysics` ray cast against an orc: what object does it return (capsule only?).

Open (static, next steps):

- `FUN_028172e0` (damage context): exact block/parry/crit rules (`ctx+0x1c0/+0x1c1/+0x160`) and the defence tables.
- The ragdoll owner: from a dead orc to its `CPhysicsRigidBody` list (`FUN_026a45f0`, `FUN_020ec140`).
- Who plays the player's attack animation: `FUN_00a8c700` calls no obvious animation function (`FUN_02274f10` is input
  buffering); candidates `FUN_00a89200`/`FUN_02265bf0` (`PlayerCombat+0x160`) or the animation state machine polling
  `PlayerCombat+0xac0` (current attack). Decides whether option 2 of section 7 can start abilities without input.
- `Combat+0x564` (the "target is the player" flag for prompts) and `Combat+0x1c8+0x58` (kill flag) owners.
- Sever type records (`*0x4b9ce00`: SeverType, NativePredicate, SimpleModelRemoveHeadAttachment, SeverFXList): which
  types exist besides the head.
