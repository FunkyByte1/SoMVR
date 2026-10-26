# Shadow of Mordor (Feral Linux build): test orcs on demand, the AI block/dodge trigger, a test arena (P9 prep)

Static analysis only (2026-10-20): Ghidra batches on the `som-linux-q*` copies (read-only), `objdump` of the read-only
binary `reference/bin/linux/ShadowOfMordor` (BuildID 87ec5d6f...), the TSV indexes, and the run logs of run 3
(`logs/boot-check-20261019-190441`), P7 (`...-20261020-001117`) and P8 (`...-20261020-022145`). Nothing launched.
Scratch (git-ignored): `reference/work-combat-spawn/` (`b*.txt` batches, `o*.txt` decompiles, `tools/fieldrefs.py` =
code that reads a schema field, `tools/riprefs.py` = RIP-relative references to an address).

Builds on `re-combat-vr.md` (**CV**), `re-combat.md` (**CB**), `re-m1m2.md` 2 (**G22**, console), `re-gameplay.md`
(**GP**), `runtime-probes.md` P7/P8. Conventions as there: runtime addresses, `pc` = the player's server Combat,
`npc` = an NPC game object (class `0x4c52780`; `hobj+0x38`), `G = *(void**)0x46b7310` (the shared game services
object; `G->vtbl+0x78()` = faction manager, `+0xa8()` = weather/world state). Confidence: **[high]** read in the
code; **[med]** read, meaning inferred; **[low]** inference; **RT** = needs a run.

## 0. Summary

| Question | Answer | Conf. |
|---|---|---|
| A console command that spawns an orc or a captain | **none**. No `Spawn*`, `SpawnCaptain`, `Encounter*` command is registered; `AISpawnMgr.SetLimit`'s body is empty in this build; the only `*Spawn*` command (`DebugSpawnAI.NemesisForge.FetchNemesis`) queries Agora (online) | high |
| The game's debug spawner | `CDebugUI+0x15f0` (DebugTextSystems.cpp; `Debug_ToggleSpawnMode`/`Debug_SpawnAI`/... = command ids 234-242). Its input handler `FUN_00e821f0` has **no reference** in the binary, so `cmd Debug_SpawnAI` cannot work. Its server half is live: client message MID 5, sub `0x1a` (AI template) / `0x21` (the current faction target) -> `FUN_027b4560` | high |
| Spawn from our code | **`FUN_029a4830(hHit, "name", xf[7], aiTemplate, 1, 1, 0, factionMember|0, 0)`** -> `NPC*`, the exact call of the server's debug spawn (section 1.3); templates = category `AI/Templates`, by name `FUN_02153910(name)` | high (call), RT (template names) |
| Spawn a Nemesis captain | `exec Faction.SetTarget ROW COL`, then the same call with the target's member and template (section 1.4) | high (calls), med (ROW/COL order) |
| Archetype names | character combat records (`Combat+0x4c8`, the faction dump's `CombatTree`): **`AI_Defender`** (shield), **`AI_Berserker`**, `AI_Infantry`, `AI_Spearman`, `AI_Archer`; attack-name stems `Def`/`Bzrk`/`Inf`/`Spr`/`Arch` | high |
| What starts an orc's block/dodge | **not a stimulus.** `Combat_StartAttack(attacker, node, attack, hNpc)` rolls the target NPC's **`AIBlockTable`** (`Combat.AIBlockTable` = `Entries` of `{Category, BlockChance}`, default chance 0.5) against the attack's `Category` list; on success `FUN_029ac890(attackerOwner, npc)` writes an "incoming attack" fact into the NPC's working memory, which makes `CAIGoalCounterMelee` relevant; its actions pick a block (pool kind 1) or dodge (kind 9) attack from the orc's own pools | high (code), med (names) |
| Why P8 saw nothing | stimuli-only swings never reach the roll; the stock strikes did, but the goal is not relevant while the orc is mid-attack outside its cancel window (3 attackers, one hit every ~2 s), and the roll/entries are data (RT) | high (swingevent), med (stock) |
| Blocks without any AI | a shield (`AI_Defender`) blocks a frontal hit at damage time by itself (`FUN_028adf30`: shield HP > 0, attack category not in `CannotBlockCategories`, direction) | high |
| Test arena | the save's tower top (35400, 5402, -22400); no orc within ~50 m at load. Quiet ground: the slave group 53 m east (~40750, 3800, -22350). `Player.CanBeAttacked 0` stops attacks; `GameServerShell.AdvanceTimeOfDay` / `.ChangeWeather <Weather/Definition>` for light | med |

## 1. Spawning an orc near the player

### 1.1 What the console has (and has not)

All registrations come from `.init_array` (`CConsoleUserProgram`, G22). Relevant ones **[high]**:

| Command | Impl | Args | Use for P9 |
|---|---|---|---|
| `Faction.SetTarget R C` | `FUN_00abe0e0` -> `FUN_00abdff0` -> `FUN_023d5df0(fm, faction, R, C, 0)` | exactly 2 ints | selects the hierarchy member at row R (first arg indexes the faction's row list), column C as "the current faction target" **[med: order]** |
| `Interrogation.SetTarget F R C` | `FUN_00abe130` | 3 ints, F = faction record index | the same with a faction |
| `Faction.Dump/Save/Load <file>` | `FUN_022a60f0`/`...6100`/`...60e0` | 1 path | Dump works with `C:\Temp\...` (P7); Load = replaces the Nemesis state (not proposed) |
| `Faction.Reset[.Role|.Trait]`, `.SimEvent.*`, `.AddEvent`, `.Cheat.ForceNextRole`, `.FindMember*`, `.Report`, `.Validate`, `FactionMember.WTF` | `_INIT_1264/1270/1294` | | print to the console (P8: `CONOUT` gets nothing) or rewrite the Nemesis state: not useful |
| `Player.CanBeAttacked 0/1` | server, `FUN_027aa390` -> `FUN_028cbae0(*0x4c4e1a0, playerHObj, b)` | 1 int | sets the player's fight-circle entry `+0xfc`: 0 = AIs do not attack Talion **[high call, med role]** |
| `GameServerShell.AdvanceTimeOfDay` | server, `FUN_02776700` | none | resets the TOD timer (`*0x4c45e60+0x970..`) and runs `FUN_0287ad90` (the client's pass-time path) **[med]** |
| `GameServerShell.ChangeWeather <rec>` | server, `FUN_02776670` | exactly 1 | `FUN_02176750(name)` (category `Weather/Definition`, `*0x4b56300`) -> `FUN_023c8910(G->vtbl+0xa8(), rec)` **[high]** |
| `NextSpawnPoint` / `PrevSpawnPoint` | `FUN_00a33640/30` | none | the server teleports to the level's start points (`*0x4c460e0` list, msg sub `0x0b/0x0c`) **[med]** |
| `AISpawnMgr.SetLimit` | `FUN_029197e0` | | **empty body** |
| `Player.Save` | `FUN_00a335d0` | | "Triggers a delayed save": never in tests |

`AIDebug.*` (ListAIs, AIInfo, ShowMemory, ...) print only. Script bindings `SpawnAITemplate` (`AISpawner`, needs a
spawner object), `ForceAISpawnWithName`, `LockTimeOfDay`, `LoadWeather` are HavokScript functions with no console
entry **[high names, med reachability]**.

### 1.2 The debug spawner (dead input, live server)

- State `S = *(char**)(*(char**)0x46a9560 + 0x80ff8) + 0x1610` (= `CDebugUI+0x15f0`): `+0x1a` spawn mode on, `+0x1b`
  "branded" modifier, `+0x158/+0x160` template list/count, `+0x168/+0x170` filter list/count, `+0x1c/+0x20`
  selection/filter index; `FUN_00e42380` builds the on-screen help ("[DPad L / R] %s [X] Spawn", "[RB] Spawn current
  target" / "Warp current target", "[LB] Spawn random hierarchy member") **[high]**.
- Commands 0xea..0xf2 reach it only through `FUN_00e821f0(CDebugUI*, cmd)` (0xea toggles, 0xeb..0xf2 ->
  `FUN_00e42ff0`). `FUN_00e821f0` has no caller, no vtable slot and no immediate anywhere (data search for its
  address: 0 hits; the Ghidra "xrefs" are LSDA bytes) **[high]**. So the probe's `cmd` cannot open it.
- Client side `FUN_00e41460(S, player, toTarget)`: message bits `8:5`, `8:0x1a|0x21`, `1:branded`, `0xe0:` a
  transform (`*(player+0x2d0)+0x338`, pos3 + quat4; most likely the camera **[med]**), + `WriteDatabaseRecord(template)` for 0x1a; sent with
  `eng->vtbl+0x150`. The "warp" branch moves an already spawned member with the object system's `vtbl+0xb0` **[high]**.
- Server side `FUN_027b4560(serverPlayer, msg)` (called by `CGameServerShell::vf_12`) cases `0x1a`/`0x21` **[high]**:
  1. ray from the camera along its forward, **100 m** (`*(float*)0x34c0ed4`), through IEnginePhysics `vtbl+0x200`
     (G21) with flags `0x50`, filter `0xc`; no hit = no spawn;
  2. point = hit + 10 cm up; rotation = facing the player (yaw from `player+0xb94/+0xb9c` minus the point);
  3. `npc = FUN_029a4830(r+0x20 /*hit object*/, "DebugSpawnedAI", &xf, template, 1, 1, 0, fm, 0)`;
  4. `npc->vtbl+0x288(npc, FUN_022a2a80(&pos,0,0), 1)` **[low: role]**; no member: `FUN_026ee7c0(Combat, *(u32*)0x4c471cc)`
     (level, 0 by default); member: `FUN_02975eb0(npc, fm, 1)`, `if (*(fm+0xa0)) FUN_022cdad0(fm, 1)`,
     `FUN_026ee7c0(Combat, *(u32*)(fm+0x340))`; branded bit: `FUN_0297b770(npc)`.
  - `0x21` takes `fm = FUN_023d3f80(G->vtbl+0x78())` (the current faction target) and refuses if `fm+0x3a4` bit 1
    (already in the world) is set; its template = `FUN_022d71b0(fm)`.

### 1.3 Spawning a template from our code (proposal)

```c
/* game thread (G1), world loaded; all calls are the server path's own */
void *cat = *(void **)0x4b56fb0;                         /* AI/Templates; FUN_021538c0() = count (+0x28),     */
void *tmpl = FUN_02153910("NAME");                       /* FUN_021538e0(i) = record i (+0x38 + i*0x30)      */
RayQuery q = { from = {x, y + 200, z}, to = {x, y - 600, z}, .flags = 0x50, .filter = 0xc };   /* G21 down */
RayResult r = {0};  r.u16_28 = 0xffff;
if (phys->vtbl[0x200/8](phys, &q, &r)) {                 /* phys = *(void**)0x466c758 */
    float xf[7] = { r.pt.x, r.pt.y + 10.0f, r.pt.z, qx, qy, qz, qw };  /* yaw quat facing Talion */
    void *npc = FUN_029a4830(r.hobj /* r+0x20 */, "SomVR_P9", xf, tmpl, 1, 1, 0, 0, 0);
    if (npc) FUN_026ee7c0(Character_GetCombat(npc), level0);   /* optional, as the debug path */
}
```

- Signature `void* FUN_029a4830(HOBJECT ctx, const char* name, const float xf[7], HRECORD tmpl, u8 a5, u8 a6, void* a7,
  void* fm, void* a9)` (args from the call at `0x27b49e4`: RDI..R9, then 0, fm, 0 on the stack) **[high]**. `ctx` must
  be non-null (`objsys->vtbl+0x118(ctx)` feeds the name/section lookup `FUN_02820dc0`) **[med]**; template field 0 set
  -> class "NPC" through `FUN_029a44f0` (returns the `NPC*`, `+8` its HOBJECT), else field 1 -> "CombatProp"; with
  `a5 = 1` the NPC is destroyed again (`objsys->vtbl+0x58`) when `FUN_026992d0(npc)` fails (placement/nav check)
  **[med]**.
- The quaternion convention (x, y, z, w; forward +Z) is the transform layout read at `*(player+0x2d0)+0x338` **[med]**.
- Despawn: `(*(void***)0x466afa0)[0x58/8](objsys, hobj)` as `FUN_029a44f0` does **[med]**.

### 1.4 A Nemesis captain from our code (proposal)

`exec Faction.SetTarget R C`; then `fm = FUN_023d3f80(G->vtbl+0x78())`; require `!(*(u32*)(fm+0x3a4) & 2)`;
`tmpl = FUN_022d71b0(fm)`; spawn as 1.3 with `fm` as the 8th argument; then `FUN_02975eb0(npc, fm, 1)`, the
`fm+0xa0` / `FUN_022cdad0(fm, 1)` step and `FUN_026ee7c0(Combat, *(u32*)(fm+0x340))` as in 1.2 step 4 **[high calls,
RT effect]**. The captain keeps name, traits and Nemesis credit (a Talion death promotes him), so only on the run's
save copy, with `minhealth` and `Faction.Dump` before/after.

Candidates in P7's dump of the user's save (re-check with `Faction.Dump` at the P9 start; P8's new captain exists
only in P8's copy) **[med]**:

| Row,Col | Name | CombatTree | XP | Relevant traits | Note |
|---|---|---|---|---|---|
| 2,5 | Mozû the Unkillable | AI_Defender | 8 | `Quirk_UnAggressive`, `Reverse_Last_Chance` | shield, calm: **block** baseline |
| 2,0 | Felgrat Sawbones | AI_Defender | 9 | `Vulnerable_Combat`, `Quirk_HasGang`, GangLeader | brings a gang |
| 1,3 | Ghâm the Hook | AI_Infantry | 10 | **`Counters_Your_Counter_1`**, `Jump_Attack`, `AntiJumpOver` | **counter** |
| 3,3 | Ûkrom | AI_Berserker | 6 | `Quirk_FastRunner`, `Quirk_LifeSteal` | **dodge** (berserker pools) |
| 0,0 / 0,2 / 0,4 | Gorgûm / Olrok / Ishgha | AI_Defender | 13-15 | Warchiefs, `BackBlock`, `Quirk_ExtraBodyguards` | avoid (bodyguards) |

`Block_Immune_Combat` (on all 27) and `Block_Resistant_Stealth` read as trait-generation blockers (they block the
`Immune_Combat` / `Resistant_Stealth` traits), not combat blocking **[low]**.

## 2. What makes an orc block, dodge or counter

### 2.1 The trigger: the AIBlockTable roll at the attacker's attack start

In `Combat_StartAttack(c, node, attack, hTarget, flag)` (`0x26eb380`), after the attack is stored (`c+0x4d0`) and
before the stimuli (CV 1.1), when `hTarget` is an NPC (class `0x4c52780`) **[high: decompile + asm 0x26eb818..0x26ecd6a]**:

1. gates: the attacker's owner `c+0x70` set; `FUN_0229aef0(node, attack)` = the attack's `AttackData.IsBlock` is
   **not** set and the node's `Type` (Combat.Node field 0, default 5) is one of {0, 2, 3, 4, 5, 11, 15} (mask `0x883d`);
   the target Combat's `+0x268` (current combat state, CB) is 0.
2. `rec = FUN_026ff330(*(npc+0x10)+0x70)` (the NPC's character combat record), `tbl = rec.AIBlockTable` (field 56,
   `*0x4ba0ca0+0xa88`); the only reader of this field found (`fieldrefs.py`) **[med]**.
3. `tbl.Entries` (`*0x4ba6460+8`, list of `CombatAIBlockChance`): for each entry, `Category` (`*0x4ba64e0+8`) null, or
   contained in the attack's categories (`FUN_00a7c690(&list, attack, 0)` = Combat/Attack field 5) -> chance =
   `BlockChance` (`*0x4ba64e0+0x38`, default **0.5**); no matching entry = no roll.
4. `if (FUN_00c73440(0.0, 1.0) < chance) FUN_029ac890(*(c+0x70), npc)` (`0x26ecd6a`).

`FUN_029ac890(attackerOwner, npc)`: a new working-memory fact (`FUN_02a04440(npc+0xc70)`) with the attacker's object
at `+0x68` (list `+0x58`), `+0x78 = 0`, `+0x80` = game time (`*(*(0x463e290)+8)+0x18`); `FUN_02a04200` adds it;
brain `*(npc+0xc78)+0x401 = 1` (re-plan) **[high writes, med names]**.

### 2.2 The consumers

- `CAIGoalCounterMelee::vf_03` (relevance, `0x29d6b90`): relevant when the newest fact (`FUN_029accf0(npc)`) is newer
  (`+0x80`) than the goal's last one (`goal+0x48`, set in `vf_07`), the NPC has a Combat, and it is **not** in an attack
  whose bytes `+0xe0`/`+0xe1` (cancel window, CB 3.2) are both 0 **[high]**.
- `CAIActionCounterMeleeBlock::vf_19`: brain `+0x3f0 == 0`, a weapon, the fact's attacker (`+0x68`) a character
  (`0x4c41500`) **in front** of the NPC, own Combat `+0x190 == 0` (no sync), then a block attack from its pools
  (`FUN_0280f740(ctx, &out, attacker, 1, 0)`) **[high]**. `vf_17` re-arms a 0.3 s timer **[med]**.
- `CAIActionCounterMeleeDodge::vf_19`: the attacker must also be the brain's current target (`brain+0x20`), brain byte
  `+0x99` set, a dodge attack from pool kind 9, then that attack's `AIData.DodgeChance` **[high]**.
- Pool predicates (`*0x4ba8f80`) gate which records qualify: `TargetBlocking`, `RecoilCounterRange`,
  `BlockCounterRange`, `DodgeCounterRange`, `HitCounterRange`, `TargetParryType`, ... (data) **[high names, RT values]**.

So the stimuli (`WeaponImpactSound` on the light strikes, `Weapon_XSwing` on the high tier, `CharacterAttackSound`,
`PlayerHit`; CV 1.7) are not on this path: they feed the senses (alert/suspicion). CV 1.6's "consumer of the stimulus"
is answered: there is none for the counter AI **[high: the goal and both actions read only the fact and the brain;
med: no stimulus consumer creates this fact]**.

### 2.3 Blocked at damage time (no AI needed)

In the damage context `FUN_028172e0` (CB 1.2): the target's current attack `IsBlock` -> blocked (`ctx+0x1c1`), unless the
attacker's `Combat+0x55b`; else the target's shield item (class `0x4c4d6e0`): `FUN_028adf30(shield, attack, ...)` at
`0x28177c4` = shield HP `+0xa8` > 0, the attack's `Category` not in the shield record's `CannotBlockCategories`
(schema `*0x4c342c0`: HitPointRange, RecoilOverride, AutoBlockingDirection, DirectionDefinition, ExcludedStateSelfList,
DefenseStats, ItemToGive, CannotBlockCategories, IsBroken, ...), then the direction test -> `ctx+0x1c0` (shield block)
**[high calls, med field roles]**. So `dealhit` (or a G8 hit) on an `AI_Defender` from the front should show
`HITEV block=1` with no AI involvement.

### 2.4 The orcs' own records (P7 `attacks-full.tsv` = run 3's `attacks.tsv`, the same 10814 names)

| Archetype | IsBlock records | Dodge | Avoid | Counter / trait "Upgrade" |
|---|---|---|---|---|
| `AI_Infantry` (308) | `AI_Infantry_Block_Into`, `_Loop`, `_Backup_Loop`, `_Block_Into_RecoilCounter`, `_Recoil_Block_H/L/L_Projectile` | `AI_Inf_Dodge_QT_L/R`, `AI_Inf_Upgrade_Dodge_QT_L/R` | `AI_Infantry_Avoid_*` (8) | `AI_Inf_Recoil_Upgrade_CounterBlock_F/_Atk_F/_Atk_F_2ndHit`, `_Upgrade_BackBlock_Recoil`, `_WraithStunBlock_F` |
| `AI_Defender` (304) | 15: `AI_Defender_Block_Into/_Loop`, `_Recoil_Block_*`, `_Recoil_FLRB_*`, `_Recoil_F/B_AFB`, `_Upgrade_Recoil_Block_Wraith` | none | `AI_Defender_Avoid_*` (8) | `_Upgrade_BackBlock_Recoil`; `ShieldBash*` |
| `AI_Berserker` (219) | none | `AI_Bzrk_Dodge_QT_L/R`, `_Upgrade_Dodge_QT_L/R` | 13 | `AI_Bzrk_Recoil_Upgrade_CounterBlock_*`, `_BackBlock` |
| `AI_Spearman` (144) | none | `AI_Spr_Dodge_QT_L/R`, `_Upgrade_*`, `_Upgrade_DodgeAway` | 4 | `AI_Spr_Recoil_Upgrade_CounterBlock_*` |
| `AI_Archer` (179) | none | `AI_Arch_Dodge_QT_L/R`, `_Upgrade_*` | 4 | none |

Trait names that match record stems (dump vs records): `BackBlock` -> `*_Upgrade_BackBlock_Recoil`, `WraithStunBlock`
-> `*_Recoil_Upgrade_WraithStunBlock_F`, `Counters_Your_Counter_1` -> `*_Recoil_Upgrade_CounterBlock_*` **[low: name
match; the trait -> pool link is data]**. The infantry block needs a pool match (`..._RecoilCounter` suggests a
`RecoilCounterRange` predicate, i.e. after being hit) **[low]**.

### 2.5 P8 read again

- `swingevent` (step 1, stimuli) cannot raise the fact: no `Combat_StartAttack` with the orc as target **[high]**.
- The three stock strikes had orc 2 as target, so the roll ran unless a gate failed (node type, `+0x268`, no matching
  `AIBlockTable` entry) or the 50 % roll lost; even with a fact, `vf_03` drops the goal while the orc is mid-attack
  outside its cancel window, which was most of the time with three orcs hitting Talion every ~2 s **[med]**.
- The `cur` swing (step 2) only sets `pc+0x4d0`; it does not run the roll either **[high]**.

### 2.6 For the VR swing (CV 1.4, new step 1b)

`FUN_029ac890(*(void**)(pc+0x70), npc)` at swing start, once per targeted orc, is the game's own notification minus the
roll; do our own roll from the orc's table (2.1 step 3) with the swing's record categories to keep the stock odds
**[high call, RT effect]**. Stimuli (step 1) stay for awareness.

## 3. A test arena

Positions from the `ORC` lines (x, y, z in cm; the tower is the save's spawn; distances in the xz plane) **[high
positions, med "quiet"]**:

| Place | Position | From the tower | Who |
|---|---|---|---|
| Forge tower top (save) | 35400, 5402, -22400 | - | no orc within ~50 m at load (run 3, P7, P8); a warg below |
| P8 fight camp | 34100-34550, 3610-3640, -27130..-27880 (P8 teleport 33774.72 3733.07 -28189.33) | ~53 m S | 3 `AI_Infantry`, 2 `AI_Archer` (one on a ledge, 35 per arrow), slaves |
| Patrol | berserker at 34394, 3550, -24449 (P8, late) | ~23 m S | 1 `AI_Berserker`, walks |
| Slave group | 40500-40971, 3800-3815, -22307..-22420 | ~53 m E | slaves only among the 20 nearest |
| Warg ground | 31800-33200, 1980-2005, -23520..-26160 | 38-44 m SW, 34 m lower | `Warg_Brown` |
| Valley | 28570-29020, 1920-2040, -29350..-30070 | ~98 m SW | `Warg_Brown`, `AI_Berserker`, `AI_Defender` (killed by the warg in P8) |
| Campfire | 30570-31030, 3740-3840, -32120..-33020 | ~110 m SW | `AI_Spearman`, 4 `AI_Infantry` |
| Mine / north field | 26300-27150, ~3250, -19100..-20090 / 29970-31040, ~3420, -14660..-14980 | ~90 m | slaves, 2-3 sentries each |

Recipe: `exec Teleport 40400 3900 -21600` (8 m NW of the slaves; the ground snap puts the root ~1.3 m up, P7 T1),
`orcs` to confirm no armed AI within 30 m, `minhealth 20`, spawn one orc 5 m ahead (1.3), `aggro 1` on it, and
`exec Player.CanBeAttacked 0` whenever the orc must stand in combat without attacking (2.2: the goal needs it out of
its own attacks) **[med; terrain RT]**. Fallback: the tower top itself with the orc spawned 3 m ahead (platform size
RT). Never leave Talion in a camp with `minhealth off` (P8 death).

Light: `W = G->vtbl+0xa8()`: `*(W+0x60)` = the weather record (`DBRecord_GetName`), `*(int*)(W+0x8c)+1` = its index,
`FUN_025ccd10(*(u8*)(W+0x68))` = the TOD name (the debug text "Record(%s) GDBIndex(%d) TOD(%s)", `FUN_00e43190`)
**[high]**. Change with `exec GameServerShell.AdvanceTimeOfDay` (repeat until a day TOD) or
`exec GameServerShell.ChangeWeather <name>` (names: category `Weather/Definition`, `*0x4b56300`, count `+0x28`,
records `+0x38` x 0x30) **[high parse, RT names/effect]**.

## 4. Sites

```
SITE("CS_AIBLOCK_GATE", K_CALL, 0x26eb818, 0x26eb805, "00 4d 8b 7c 24 38 74 1b 4d 85 ff 74 16 48 89 ee 4c 89 f7 e8 d3 f6 ba ff", 0x229aef0, "48 85 ff 0f 84 57 01 00 00 41 54 48 89 f0 55"),
SITE("CS_AIBLOCK_REC", K_CALL, 0x26ecbc7, 0x26ecbb4, "68 02 00 00 00 0f 85 69 ec ff ff 49 8b 47 10 48 8b 78 70 e8 64 27 01 00", 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85"),
SITE("CS_AIBLOCK_ROLL", K_CALL, 0x26ecd50, 0x26ecd3d, "c5 00 f3 0f 10 0d 29 7a c5 00 66 0f ef c0 f3 0f 11 14 24 e8 eb 66 58 fe", 0xc73440, "48 83 ec 18 f3 0f 11 44 24 0c f3 0f 11 4c 24"),
SITE("CS_AIBLOCK_NOTIFY", K_CALL, 0x26ecd6a, 0x26ecd57, "10 14 24 0f 2f d0 0f 86 c5 ea ff ff 48 8b 7b 70 4c 89 fe e8 21 fb 2b 00", 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b"),
SITE("CS_AI_FACT_ENTRY", K_ENTRY, 0x29ac890, 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec", 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec"),
SITE("CS_GOAL_COUNTER_RELEVANT", K_VSLOT, 0x3a4e0d8, 0x3a4e0d8, "90 6b 9d 02 00 00 00 00", 0x29d6b90, "41 54 55 53 48 8b 6f 08 48 89 fb 48 8b 85 20"),
SITE("CS_SHIELD_TEST", K_CALL, 0x28177c4, 0x28177b1, "89 c2 f3 41 0f 10 87 bc 01 00 00 48 8b 74 24 50 48 89 ef e8 67 67 09 00", 0x28adf30, "f3 0f 10 8f a8 00 00 00 0f 2f 0d 49 a7 a9 00"),
SITE("CS_SPAWN_DEBUG", K_CALL, 0x27b49e4, 0x27b49d1, "00 00 00 53 6a 00 48 8b bc 24 90 00 00 00 48 8d 54 24 50 e8 47 fe 1e 00", 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45"),
SITE("CS_SPAWN_AI", K_ENTRY, 0x29a4830, 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48", 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48"),
SITE("CS_TMPL_BYNAME", K_ENTRY, 0x2153910, 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80", 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80"),
SITE("CS_FM_BIND", K_CALL, 0x27b4a9f, 0x27b4a8c, "85 db 0f 84 94 05 00 00 48 89 ef ba 01 00 00 00 48 89 de e8 0c 14 1c 00", 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89"),
SITE("CS_SETLEVEL", K_CALL, 0x27b4acf, 0x27b4abc, "b1 ff 48 89 ef e8 8a 51 ee ff 8b b3 40 03 00 00 48 89 c7 e8 ec 9c f3 ff", 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48"),
SITE("CS_FACTION_TARGET", K_ENTRY, 0x23d3f80, 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8", 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8"),
SITE("CS_FM_TEMPLATE", K_ENTRY, 0x22d71b0, 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03", 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03"),
SITE("CS_CANBEATTACKED", K_ENTRY, 0x28cbae0, 0x28cbae0, "48 8b 47 40 48 83 c7 38 48 39 f8 75", 0x28cbae0, "48 8b 47 40 48 83 c7 38 48 39 f8 75"),
```

`CS_AIBLOCK_*` and `CS_SHIELD_TEST` are logging hooks (and `CS_AIBLOCK_ROLL` the "force" switch); `CS_SPAWN_*`,
`CS_TMPL_BYNAME`, `CS_FM_*`, `CS_FACTION_TARGET`, `CS_SETLEVEL`, `CS_CANBEATTACKED`, `CS_AI_FACT_ENTRY` validate the
functions we call. `CS_GOAL_COUNTER_RELEVANT` is `CAIGoalCounterMelee` slot 3 (vtable `0x3a4e0c0`); CV 1.5 has slots 7,
11 and 19.

## 5. P9 probe words (proposals)

| Word | Does | Lines |
|---|---|---|
| `blocktable [N]` (read-only) | for the picked / N nearest orcs: the record of `FUN_026ff330(*(npc+0x10)+0x70)` and `Combat+0x4c8`, its `AIBlockTable` (`FUN_026c0060(rec, *(char**)0x4ba0ca0+0xa88, &0)`), each entry's `Category` (`FUN_026c0060(e, *(char**)0x4ba64e0+8, &0)`) and `BlockChance` (`FUN_026bfb10(e, *(char**)0x4ba64e0+0x38, &0.5f)`), entries via `FUN_00a09d60(&list, tbl, *(char**)0x4ba6460+8)`; `blocktable cat ATTACK` = that attack's categories (`FUN_00a7c690`) | `BLOCKTBL` |
| `aiblockwatch on/off` | hooks `CS_AIBLOCK_GATE` (node, attack, result), `CS_AIBLOCK_ROLL` (roll), `CS_AIBLOCK_NOTIFY` (attacker, npc, t), `CS_GOAL_COUNTER_RELEVANT` (result byte `goal+0x10` on change), `CS_SHIELD_TEST` (result, shield HP) | `AIBLOCK` |
| `blockroll force/off` | `CS_AIBLOCK_ROLL` returns 0.0f: every matching attack start notifies | `AIBLOCK roll forced` |
| `aiattacked [NAME]` | `FUN_029ac890(*(pc+0x70), npc)` for the picked orc (step 1b, no attack) | `AIFACT` |
| `templates [SUBSTR]` | AI/Templates names to `probe-out/templates.tsv` and the log | `TEMPLATE` |
| `spawn TEMPLATE [CM] [LEVEL]` | 1.3 at CM (default 500) ahead of Talion; remembers the HOBJECT | `SPAWN` |
| `spawncaptain R C [CM]` | `exec Faction.SetTarget R C`, log the target's name, then 1.4 | `SPAWN captain` |
| `despawn [all]` | removes what `spawn*` made | `DESPAWN` |
| `tod` / `weathers` | 3 "Light"; the Weather/Definition names | `TOD`, `WEATHER` |

Run sketch: tower (`tod;templates;exec Faction.Dump C:\Temp\somvr-faction-P9a.txt;blocktable 5`) -> arena (3) ->
`spawn <defender template>;pick;blocktable;aiblockwatch on;aiwatch on;stimlog on;aggro 1` ->
`exec Player.CanBeAttacked 0` -> 5 x `cmd CombatPrimary01` 1.5 s apart (stock baseline: `AIBLOCK` lines, `HITEV
block=1` from the shield) -> `blockroll force` + 5 strikes -> `blockroll off` + 3 x `aiattacked` (+ `dealhit` 0.3 s
later) -> `despawn` -> the same with a berserker template (dodge) -> `spawncaptain 1 3` (Ghâm) with
`exec Player.CanBeAttacked 1` and `autocounter CombatKBM09 3` (counter) -> `despawn all;exec Faction.Dump
C:\Temp\somvr-faction-P9b.txt`. Pass: a `CAIGoalCounterMelee` activation and an `*_Block_Into`/`*_Dodge_*` start
after a notify, for a stock strike and for `aiattacked`.

## 6. Open (RT)

- AI/Templates and Weather/Definition record names; each archetype's `AIBlockTable` entries and the categories of the
  player's records; the node `Type` enum names behind mask `0x883d`.
- `FUN_029a4830`'s `ctx` semantics (`objsys->vtbl+0x118`) and whether the ground ray's hit object always works; the
  `npc->vtbl+0x288` post-step.
- `Faction.SetTarget`'s argument order on screen (log the target's name before spawning).
- `AdvanceTimeOfDay`'s step size; whether `Player.CanBeAttacked 0` also stops archers.

**Runtime result (P9, 2026-10-20 09:46, `logs/boot-check-20261020-094644`; runtime-probes.md P9 results).**
- 1.3 works: `FUN_029a4830` from our code spawned `Orc_Defender` (the best of 9 templates for `AI_Defender`) and
  `Orc_Berserker` (6). Both were valid at 1 and 3 s, stood 1.8-3.0 m from the ray's point, and came at level 0
  unless one is given. `despawn` removed them cleanly. 122 AI/Templates in `templates.tsv`.
- 1.4 works, and **`Faction.SetTarget` is row, then column**: `1 3` gave Ghâm the Hook (level 10, `Orc_Infantry`),
  the dump's row 1, column 3. His entry changed (PresentedToPlayer, IntelDataLevel, encounter, three FactionEvents).
  He did his Nemesis showdown 30 s after the spawn. Despawning him in the middle of it left its camera and name card
  up, with every command inactive, for at least 12 s (gone after `AdvanceTimeOfDay`).
- 2.1: **`AI_Defender` and `AI_Berserker` have no AIBlockTable** (an empty name, 0 entries): no roll is ever made
  for them. The `Infantry` table (infantry, slaves, the captain): Block_Generic 0, PC_PoolAttacks_KB_EL1/EL2 0.25,
  EL3 0.10, Ranged_Generic 0, any attack 0.10. The light strikes rolled 0.100 against an infantry (EL3 or the
  catch-all; not told apart yet), and a won roll notified it.
- 2.2: a forced notify on an infantry made the goal relevant, but `block_valid` and `dodge_valid` were 0 and the
  strike hit. **Our `FUN_029ac890` on the defender raised its block** (`block_go` -> `AI_Defender_Block_Into`), but
  only when the call came outside its own attack; the two calls during an attack or recoil did nothing (the cancel
  window rule held). The berserker evaluated one dodge, invalid, while Talion was recoiling: its dodge is still open.
- 2.3 holds: the shield (`Orc_Defender_Melee_Breakable_Shield`, HP 300 throughout) blocked 9 of 9 stock strikes and 4
  of 4 `dealhit`s (`block=1`, damage 0), with the defender's `AI_Defender_Recoil_Block_Deflect_L` and Talion's
  `PC_Deflected_*`, without any AI.
- 3: the arena is not quiet. Four `AI_Infantry` 31-45 m west joined the fights, and the spot by the rock wall made the
  spawned orcs play `CantPath` taunts. `Player.CanBeAttacked 0` stopped no attack (the spawned orcs' melee, thrown
  spears). **One `AdvanceTimeOfDay` = Day -> Night** (`SON_Day02` -> `SON_Night_01`): a 0.5 s pause and the NPCs around
  replaced. 79 weather names; day without rain: `DayNoRain`, `AnyTimeNoRain`, `TEST_Day_01`..`_03`.
