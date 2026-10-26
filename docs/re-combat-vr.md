# Shadow of Mordor (Feral Linux build): combat for VR - the AI's view of an attack, hit-stop, animation key times

Static analysis only (2026-10-19, 21:50 onwards): `objdump` ranges of the read-only binary
`reference/bin/linux/ShadowOfMordor`, the TSV indexes in `reference/index/`, earlier Ghidra output in
`reference/work-gameplay/o1.txt` (the shapecast collider decompile) and `reference/work-combat/`, and small offline
scripts. No new Ghidra run (a VR session was on), nothing launched, nothing written outside the workspace. Scratch
(git-ignored): `reference/work-combat-vr/` (`iemmap.py` = IEngineModel slot -> CSimpleModel -> tracker function,
`iemcalls.py` = call sites of an interface slot, `timercalls.py` = IEngineTimer call sites, `immrefs.py` = code sites
of an immediate, `holders.py` = interface pointer slots, `fn.py`, `thunk.py`, listings `d_*.txt`, `sites.list` +
`sites_raw*.txt` = the SITE lines below).

Builds on `re-combat.md` (**CB**), `re-animation.md` (**AN**), `re-gameplay.md` (**GP**) and `design-vr.md` (**DV**,
section 3). Conventions as there: addresses = runtime addresses (non-PIE, BuildID 87ec5d6f...), `vf_NN` = vtable slot
NN, `vtbl+0x130` = byte offset into a vtable. `m = *(IEngineModel**)0x46a9230`, `c`/`oc`/`pc` = a `Combat*`
(`CCharacter+0x6d0`; `oc` an orc's, `pc` the player's server Combat), `ws = c+0xb0` (the window state, CB 3.2),
`trk = *(sm+0x178)` (the tracker node, AN 1.1), `sub` = one animation tracker inside it. LT results: the IEngineModel
stubs return `inner ^ 1`, so 0 = OK, 0x3c = null object. Confidence: **[high]** read in the code, unambiguous;
**[med]** read in the code, meaning inferred; **[low]** inference; **RT** = needs a runtime check.

## 0. Summary

| Question (DV 3) | Answer | Conf. |
|---|---|---|
| What the stock attack start tells the AI | **AI stimuli**: `Combat_StartAttack` (`0x26eb380`) calls `FUN_026c7180(c, attackData, target)`, which registers every record of `AttackData.RegisterStimulus` (field 53) and the one `PersistentStimulus` (field 55) with `CAIStimulusMgr::RegisterStimulus` (`FUN_0293e820`, manager `*0x4c500e0`): at once when the animation has no `STIMULUS` key, else at that key (`Combat_OnModelString`), and again at `Combat_StopAttack` | high |
| What the orcs' counter AI reads | `CAIActionCounterMeleeBlock/Dodge::vf_19` (`0x29c5510`, `0x29c6590`) check the AI's current target (a character, in front, no sync) and pick a block or dodge attack from **the orc's own pools** (their predicates decide); the dodge reads that dodge attack's `CombatAIData.DodgeChance` and also fetches the target's current attack (`FUN_026d4510`, use not followed). Whether a hit is blocked is decided at damage time: the target's current attack has `AttackData.IsBlock` (`FUN_026d4dc0`) or its shield faces the hit | high (reads), med (roles), RT (what triggers the goal) |
| Not the carrier | `CombatAttackStartArgs` (a script hook for per-Combat listener lists), `AICombatMgr_OnStartAttack` (fight-circle bookkeeping for AI attackers only) | high |
| Raising it for a VR swing | `FUN_026c7180(pc, FUN_00a91300(rec), hOrc)` at swing start; if the dodge/block AI also needs a live player attack, expose `rec` as `pc+0x4d0` for the swing, or run a server-only `Combat_StartAttack` | high (call), RT (effect) |
| Per-character animation rate | every model's animation time comes from **its object's timer** (`HOBJECT+0x68`, frame ms at `timer+0x0c`); each tracker also has its own rate (`sub+0xa0`) and playing flag (`sub+0xb5` bit 0) | high |
| Hit-stop | give the orc its own `IEngineTimer` timer (scale 0, or 1/20) for 0.12 s with `SetObjectTimer`, then give the old one back - the same swap the engine does for sync actions and companions | high (mechanism), RT (side effects) |
| `SlowMO.*` | global only: they write the game timer `*(*(void**)0x463e290+8)`; the slow-mo manager also scales the **player's** own timer (`FUN_02824bf0`), never an NPC's | high |
| Key times | the game stores the current attack's key times at every start: `Combat+0xcc` = `APPLYDAMAGE` (impact), `+0xd4/+0xd6` = `BPW/EPW`, `+0xca` = first `BCOL`, ... (ushort ms), anim index at `Combat+0x17c` | high |
| Arbitrary key / time | `IEngineModel` +0x198 = `KeyLookup(anim, "KEY", arg, &frame, &ms, fromMs)` (any key, so `APPLYDAMAGE` too), +0x168 = `SampleNode(anim, node, ms, &xf)` at any time, +0x248 = the tracker's current time | high |

## 1. The AI's view of a player attack (DV 3 "raise that event"; risk table "a synthesised swing event")

### 1.1 What the stock start emits

The player's start `FUN_00a8c700` reaches `Combat_StartAttack` (`FUN_026eb380`) through the wrappers (CB 3.1); the
orcs' starts take the same function. Inside, in order **[high: disassembly]**:

| Site | Call | Effect |
|---|---|---|
| `0x26eb768` | `FUN_022642e0(ws, node, attack)` | window state reset (CB 3.2) |
| `0x26eb77a` | `FUN_02263590(ws, c+0x4a0, attack)` | the attack's **key-time table** (section 3.1) |
| (in `FUN_026e7070`, called by `Combat_StartAttack`) `0x26e76aa` | `m->vtbl+0x1e8` GetAnimIndex(`hObj`, `AnimationData.Name`) | `c+0x17c` = the attack's animation index |
| (in `FUN_026e7070`) `0x26e8bcc` | `CombatAttackStartArgs` (vtable `0x38fa528`) through the event manager `*0x46b7310` vtbl+0xf8 -> `FUN_0240aa00` | only when the attack is in the Combat's listener list `c+0x1a8..+0x1b0`: a **script** hook, not an AI broadcast **[med: role]** |
| `0x26ebbd9` -> `0x26ed119` | `FUN_026c7180(c, attackData, target)` when the owner `c+0x70` is set and `*(u16*)(c+0xda) == 0xffff` (no `STIMULUS` key, section 3.1) | the attack's AI stimuli, now |
| `Combat_OnModelString` `0x26fd1d7` | `FUN_026c7180(c, FUN_00a91300(FUN_026d4510(c)), c+0x4c0)` on the key string object `0x4c43178` = `"STIMULUS"` (`0x38bfddc`) | the stimuli at the key, when the animation has one |
| `Combat_StopAttack` `0x26eed28` | `FUN_026c7180(c, ., .)` when `c+0x70` is set | again at the stop |

`FUN_00a91300(attack)` = the attack's `AttackData` record (Combat/Attack field 1); `FUN_026d4510(c)` = the current
attack, `c+0x4d0` or its variation `MultiHitRecords[c+0x55c - 1]` **[high]**.

### 1.2 `FUN_026c7180` = register the attack's AI stimuli

```
void FUN_026c7180(Combat* c /*RDI*/, HRECORD attackData /*RSI*/, HOBJECT target /*RDX*/)     [high: 0x26c7180]
```

- If `c+0x168` (the id of this Combat's persistent stimulus) is set, it first removes it:
  `FUN_0293e150(*0x4c500e0, id)` (`0x26c7776`), `c+0x168 = 0` **[high]**.
- For every record of `AttackData.RegisterStimulus` (field 53, a list; accessor offsets `+0xa08/+0xa18` on
  `*0x4ba7280`) it fills a 0x90-byte descriptor on the stack and calls `FUN_0293e820(*0x4c500e0, &desc)`
  (`CAIStimulusMgr::RegisterStimulus`, "Stimulus Position is NaN") at `0x26c747f` **[high]**. Descriptor
  **[high for the writes, med for the names]**: `+0x00` stimulus record, `+0x08` WBObjRef source (HOBJECT `c+0x4a0`
  at `+0x20`), `+0x28` vec3 position = `FUN_00c78090(c+0x70)` (the attacker's position), `+0x38` = `owner+0x48`,
  `+0x40` WBObjRef target (HOBJECT at `+0x58`), `+0x60` float 1.0, `+0x68` int "persistent".
- Then `AttackData.PersistentStimulus` (field 55, one record) the same way with `+0x68 = 1`; the returned id goes
  to `c+0x168` (`0x26c76bf`) **[high]**.
- `RegisterStimulus` returns -1 without registering when the manager is off (`mgr+0x24`) or when
  `*(u8*)0x4c47090` is set and the source is of class `0x4c47040` (the class the sync code uses to find the player,
  `FUN_02254290`) - a "no stimuli from the player" switch **[med]**. A stimulus with the same record, source and
  target that is still live is not added twice **[med]**.
- Stimulus records are data (`AI/StimulusType/Audio|Visual|Tactile`, "Defines an AI Stimulus record.") **[high
  names]**; which ones the player's melee records carry is **RT** (the C1 attack dump has names only).

### 1.3 What the orc's counter AI reads

- `CAIGoalCounterMelee` (vtable `0x3a4e0c0`): `vf_07` (`0x29d6d80`) runs the base and stores the time of the AI's
  current target info (`FUN_029accf0(npc)+0x80`) at `goal+0x48`; `vf_11` (`0x29d6c80`) sets its world-state props
  (6, 0x1d). Its relevance is the base class's, data driven **[med]**. What raises it (the stimulus of 1.2, or the
  sensing of the target's attack) was not traced: **RT** (probe `aiwatch`, section 4).
- `CAIActionCounterMeleeBlock::vf_19` (`0x29c5510`, the context check) **[high for the reads, med for the
  meaning]**: refuses while the brain (`npc+0xc78`) `+0x3f0` is busy; needs the AI's current target
  (`FUN_029accf0(npc)+0x68`) to be a character (class `0x4c41500`), **in front** (target minus `npc+0xd2c` dotted
  with the forward of `FUN_0296c360` >= 0), its own Combat not in a sync (`+0x190 == 0`) and a weapon; then picks a
  block attack from the orc's pools (`FUN_022617f0` input context with `ctx+0x46 = 1`,
  `FUN_0280f740(ctx, &out, target, 1, 0)`).
- `CAIActionCounterMeleeDodge::vf_19` (`0x29c6590`) **[high for the reads]**: the same target checks plus the brain
  byte `+0x99`; a dodge attack from pool kind 9 (`r12 = FUN_0280f740(..., 9, 0)`), then **that dodge attack's**
  `AIData` (Combat/Attack field 6) field 1 `DodgeChance` (`0x29c66c7`: the record is `r12`). It also fetches the
  target's current attack `FUN_026d4510(FUN_02699c50(targetChar))` (`0x29c66bb`); its use was not followed
  **[low]**. So the orc's own block/dodge records and their pool predicates decide, not data of the incoming
  attack **[med]**.
- `CombatAIData` (schema `FUN_02519550`, array `*0x4ba60a0`, decoded with `reference/work-combat/schema.py`)
  **[high]**: 0 BlockLoopLength, 1 DodgeChance, 2 CollisionAvoidanceRadius, 3 AIMustSeeTarget, 4 AICloseQuarters,
  5 AIChasingTarget, 6 AttackTargetsRider, 7 CreateAIPerformAttackEvent, 8 AllowAlignmentChange,
  9 BlockInitialThreatAgainstTarget, 10 RequestedPosture, 11 AppliedPosture, 12 PreventAttackerSelection,
  13 FallBehaviorNode, 14 FallShouldClearCombatStates.
- **The block itself is decided at damage time** (CB 1.2 step 7): the damage context `FUN_028172e0` calls
  `FUN_026d4dc0(targetCombat)` (`0x2817689`) = "the target's current attack has `AttackData.IsBlock`" (field 35,
  `+0x6a8/+0x6b8`) and sets `ctx+0x1c1` (blocked) unless the attacker's `Combat+0x55b` is set; a shield item (class
  `0x4c4d6e0`) passing the direction test `FUN_028adf30` sets `ctx+0x1c0` **[high for the calls, med for the
  roles]**. Both lead to the blocked branch (block recoil). So a G8 hit on an orc that is in its block attack, or
  whose shield faces the blade, is blocked by the game's own rules.

Not the carriers **[high]**: `CombatAttackStartArgs` (1.1); `AICombatMgr_OnStartAttack` (`FUN_028d0ef0(mgr
*0x4c4e1a0, NPC* attacker, HOBJECT target)`, uses `attacker+0xcd0`; called only for Combats with an AI owner
(`c+0x80`): `Combat_ApplyDamageKey` `0x26e5bd3`, `FUN_02332c50`, `FUN_026bf7e0`, `FUN_026c58d0`) - the fight circle's
record of AI attacks; the combat dispatcher `FUN_02717060` (`[COMBAT|DISPATCH]` filters in `FUN_02717150`), which
carries hit, kill, sever, brand and state events (callers `FUN_026df7c0`, `FUN_026f4850`, `FUN_026d4670`,
`FUN_026f8560`, `Combat_StopAttack`, ...) **[med: roles]**.

### 1.4 Raising it for a physical swing (proposal)

On the game thread (G1), once per swing and orc, when the blade crosses 3 m/s within 1.5 m heading at orc `O`
(DV 3), with `rec` = the player melee record G8 will use for this swing:

1. **Stimuli (always):** `FUN_026c7180(pc, FUN_00a91300(rec), hO)` - the exact call `Combat_StartAttack` makes.
   Needs `pc` (player server Combat, `*(CPlayerObj+0x6d0)`), `rec` (a record whose `RegisterStimulus` is not empty:
   pick it from the attack dump, RT) and `hO`. At the swing's end remove the persistent one like the function does
   (`FUN_0293e150(*0x4c500e0, pc+0x168)`, `pc+0x168 = 0`) **[high for the calls, RT for the effect]**.
2. **The swing as the player's current attack (if RT shows 1 is not enough):** for the swing's duration save and set
   `pc+0x4d0 = rec`, `pc+0x55c = 0`, `pc+0x4c0 = hO`, restore at the end. The dodge check's target-attack lookup
   and pool predicates that look at the target (1.6) then see `rec`. Risk: `Combat::vf_13` (`0x26f7b10`) and the
   hit code treat it as a live attack **[low, RT]**.
3. **Server-only start (most stock, last resort):** `FUN_026ed9b0(pc, node, rec, hO, 0)` (StartAttackNoPos, CB 3.1)
   and `Combat_StopAttack(pc, node, rec, hO, 0)` (`FUN_026ee800`, args RDI..R8) at the swing's end; G4 must let our
   own call through (a thread-local flag). The client side (`FUN_00a8c700`, `PlayerCombat+0xac0`) is not touched,
   so Talion's attack animation may not start - **RT** (CB 9 "who plays the player's attack animation"). Use records
   without `Combat/Sync`, with `Combat.Enable.SlowMo` off (GP).

### 1.5 Sites

```
SITE("CV_STIM_START", K_CALL, 0x26ed119, 0x26ed110, "4c 89 e2 4c 89 ee 48 89 df e8 62 a0 fd ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("CV_STIM_KEY", K_CALL, 0x26fd1d7, 0x26fd1ca, "48 8b 93 c0 04 00 00 48 89 df 48 89 c6 e8 a4 9f fc ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("CV_STIM_STOP", K_CALL, 0x26eed28, 0x26eed1f, "4c 89 ea 48 89 ee 48 89 df e8 53 84 fd ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("CV_REGSTIM", K_CALL, 0x26c747f, 0x26c7470, "48 8b 3d 69 8c 58 02 48 8d b4 24 80 00 00 00 e8 9c 73 27 00", 0x293e820, "80 7f 24 00 75 4f 80 3d 63 88 30 02 00 74 51"),
SITE("CV_REGSTIM_PERSIST", K_CALL, 0x26c76bf, 0x26c76b0, "48 8b 3d 29 8a 58 02 48 8d b4 24 80 00 00 00 e8 5c 71 27 00", 0x293e820, "80 7f 24 00 75 4f 80 3d 63 88 30 02 00 74 51"),
SITE("CV_STIM_REMOVE", K_CALL, 0x26c7776, 0x26c7769, "8b b3 68 01 00 00 48 8b 3d 6a 89 58 02", 0x293e150, "83 fe ff 74 29 80 7f 24 00 75 23 48 8b 47 10"),
SITE("CV_KEYTIMES", K_CALL, 0x26eb77a, 0x26eb76d, "48 8b b3 a0 04 00 00 4c 89 ff 48 89 ea e8 11 7e b7 ff", 0x2263590, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("CV_DMG_TARGETBLOCKING", K_CALL, 0x2817689, 0x2817682, "49 89 87 98 01 00 00 e8 32 d7 eb ff", 0x26d4dc0, "41 55 41 54 55 31 ed 53 48 83 ec 18 e8 3f f7"),
SITE("CV_AI_BLOCK_VALID", K_VSLOT, 0x3a3d828, 0x3a3d828, "10 55 9c 02 00 00 00 00", 0x29c5510, "41 54 49 89 fc 55 53 48 83 ec 70 48 8b 6f 08"),
SITE("CV_AI_BLOCK_ACT", K_VSLOT, 0x3a3d7e8, 0x3a3d7e8, "d0 61 9c 02 00 00 00 00 60 62 9c 02 00 00 00 00", 0x29c61d0, "53 48 89 fb 48 83 ec 20 e8 a3 7a ff ff 48 8b"),
SITE("CV_AI_DODGE_VALID", K_VSLOT, 0x3a3e428, 0x3a3e428, "90 65 9c 02 00 00 00 00", 0x29c6590, "41 57 41 56 41 55 41 54 55 53 48 81 ec 98 01"),
SITE("CV_AI_DODGE_ACT", K_VSLOT, 0x3a3e3e8, 0x3a3e3e8, "90 7e 9c 02 00 00 00 00 60 89 9c 02 00 00 00 00", 0x29c7e90, "41 57 41 56 45 31 f6 41 55 45 31 ed 41 54 49"),
SITE("CV_AI_GOAL_COUNTER", K_VSLOT, 0x3a4e0f8, 0x3a4e0f8, "80 6d 9d 02 00 00 00 00", 0x29d6d80, "53 48 89 fb e8 07 c5 ff ff 48 8b 7b 08 e8 5e"),
SITE("CV_AIMGR_ONSTART", K_CALL, 0x26e5bd3, 0x26e5bc9, "48 8b 3d d0 85 56 02 4c 89 e2 e8 18 b3 1e 00", 0x28d0ef0, "41 56 48 8d 47 38 41 55 41 54 55 53 48 8b 5f"),
SITE("CV_CURATTACK", K_ENTRY, 0x26d4510, 0x26d4510, "55 53 48 83 ec 08 0f b6 87 5c 05 00 00 48 8b", 0x26d4510, "55 53 48 83 ec 08 0f b6 87 5c 05 00 00 48 8b"),
SITE("CV_ATTACKDATA", K_ENTRY, 0xa91300, 0xa91300, "48 8b 35 b9 10 11 04 83 7e 48 01 7e 33 48 85", 0xa91300, "48 8b 35 b9 10 11 04 83 7e 48 01 7e 33 48 85"),
SITE("CV_STARTATTACK_NOPOS", K_ENTRY, 0x26ed9b0, 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("CV_STOPATTACK", K_ENTRY, 0x26ee800, 0x26ee800, "41 57 4d 89 c7 41 56 41 55 49 89 cd 41 54 55", 0x26ee800, "41 57 4d 89 c7 41 56 41 55 49 89 cd 41 54 55"),
```

`CV_STIM_*`/`CV_REGSTIM*` are the hook points for logging (and the calls we reuse); `CV_AI_*` are vtable slots for
logging hooks only (`CAIActionCounterMeleeBlock` vtable `0x3a3d790`, `...Dodge` `0x3a3e390`, slots 11 and 19;
`CAIGoalCounterMelee` slot 7).

### 1.6 Open risks

- The consumer of the stimulus (sensing -> `CAIGoalCounterMelee`) is not traced; orcs may react only to the
  attacker's live attack (the dodge check's `FUN_026d4510` lookup, the pool predicates), then step 2 or 3 is needed
  **[RT]**.
- The player's melee records may carry no `RegisterStimulus`; then step 1 does nothing for them, and a record that
  has one (any orc attack record, data) would change what the orcs "hear" **[RT]**.
- The block pools' predicates (`SharedCombat_AttackPredicates`: "Parry Attempt", "TargetCategory", "Target Combat
  State") may require the player's current attack; same answer as above **[RT]**.
- `*(u8*)0x4c47090` (player stimuli off) must be 0 in our runs; nothing in this pass writes it **[med]**.

### 1.7 Runtime probe

Baseline then synthetic, in a P2-style fight (runtime-probes.md P2), with new words (section 4): `stimlog on`,
`aiwatch on`. Run (a): stock attacks (`cmd CombatPrimary..` presses) - which stimulus records the player's attacks
register, when, and which orcs then run `CV_AI_GOAL_COUNTER` / `CV_AI_BLOCK_*` / `CV_AI_DODGE_*`. Run (b): no stock
attack, `swingevent auto` every 2 s (step 1), then `swingevent auto 400 cur` (step 1 + 2), then `srvatk auto 400`
(step 3, with shots: does Talion animate?); `dealhit auto` 0.3 s after each: `HITEV` block flag (`ev+0x58`) counts the
blocks. Pass = run (b) shows the same AI lines as run (a) for at least one step.

**Runtime result (P7, 2026-10-20, `logs/boot-check-20261020-001117`; runtime-probes.md P7 results).** Holds: 1.2's
registration (`rc` >= 0, `player_off=0`, `mgr_off=0`, so `0x4c47090` is 0) and step 1 from our code (4 swings, one
registration each). Wrong for the plan: the player's stock light strikes carry **only `WeaponImpactSound`** (a sound
stimulus; the 499 `PC_HC_*` records: `WeaponImpactSound` 420, `Weapon_XSwing` 47 on the `PC_HC_High_*` tier, none 32),
and neither step 1 nor step 1 + 2 (`cur`) made the target react: `goal_counter`/`block_*`/`dodge_*` 0 for all 4,
`CAIGoalCounterMelee::vf_07` never ran in the whole run, the block/dodge checks 6 times (result 0), `HITEV block=0` on
all hits. Not conclusive: the orcs were in alert/suspicion states and never fought Talion, and run (a) had no baseline
(the stock strikes reached nobody). Next: run (a) and (b) in a fight the orcs started, `swingevent` with a
`Weapon_XSwing` record, then step 3 (`srvatk`, not built).

**Runtime result (P8, 2026-10-20 02:21, `logs/boot-check-20261020-022145`; runtime-probes.md P8 results).** Three
orcs fought Talion (`aggro`, through `SetCombatScriptedTarget`). Still no reaction, to anything:
- Run (a): three stock strikes with a target. Two hit the orc (10 damage, `block=0`, a hit reaction); their stimuli
  were `WeaponImpactSound` twice and none for `PL_LedgeKick`.
- Run (b): `swingevent PC_HC_High_L1_D0_F_LR_RR_UD_2` (step 1, a `Weapon_XSwing` registration, rc >= 0) at an orc 4.6 m
  away: all counters 0. The two `cur` swings (step 2) were refused: Talion was in a recoil (the player's recoils are
  `pc+0x4d0` attacks).
- `CAIGoalCounterMelee::vf_07` and the four block/dodge checks ran **0 times** in the whole run; every hit had
  `block=0`.
- So these `AI_Infantry` do not evaluate a block or dodge against light strikes at all, and step 1's effect still has
  no baseline.
- Seen on the way: an orc's attack start registers `CharacterAttackSound` at its target; every recoil start of the
  player registers `PlayerHit` at the attacker.
- Next: a blocking enemy (shield `AI_Defender`, a captain) for run (a), `cur` with one attacker, then step 3.

**Runtime result (P9, 2026-10-20 09:46, `logs/boot-check-20261020-094644`; runtime-probes.md P9 results).** Spawned
test orcs, with re-combat-spawn.md 2's trigger (the AIBlockTable roll, not the stimulus) hooked:
- **A shield defender blocks every frontal hit by itself.** 9 of 9 stock strikes and 4 of 4 `dealhit`s gave
  `HITEV block=1` with 0 damage, the orc's `AI_Defender_Recoil_Block_Deflect_L` and Talion's `PC_Deflected_*`. So a
  G8 hit on a defender from the front is rejected by the game's own rules (1.3's "decided at damage time").
- **Our notification (step 1b, `FUN_029ac890`) makes a defender raise its block**: `block_valid 1`, then `block_go`
  -> `AI_Defender_Block_Into`, but only when the call comes while the orc is out of its own attack (1 of 3 calls).
- `AI_Defender` and `AI_Berserker` have **no AIBlockTable**, so the stock roll never notifies them. On an
  `AI_Infantry` (table `Infantry`, 0.10 for the light strikes) a forced notify gave `block_valid 0` and `dodge_valid
  0`, and the hit landed.
- Not shown: a berserker's dodge (one invalid check, with Talion recoiling) and anything from the captain (he was in
  his showdown).
- For `SOMVR_COMBAT_REACT` (section 6): the roll it copies never fires for a defender or a berserker, and `force`
  only wins rolls that have an entry. The defender's AI block needs the call without a roll, timed outside the orc's
  own attack. The shield needs nothing from us. Not changed here.

## 2. Hit-stop: freezing one orc (DV 3 "needs RE: the per-character animation rate")

### 2.1 Where a character's animation time comes from

- Phase 0 (AN 1.2): `CSimpleModelTrackerNode::vf_06(0)` (`0x238fa50`) takes `dt = FUN_023888d0(trk)` (`0x238fad0`)
  and runs `FUN_0238f7b0(trk, dt)`, which advances every tracker of the model with `FUN_02ad9370(sub, dtMs, ctx)`
  (`0x238f855`) **[high]**.
- `FUN_023888d0(trk)`: with `hObj = trk+0x168`, `dt = *(u32*)(GetObjectTimer(hObj)+0x0c)` (this frame's ms of the
  **object's timer**); without an object, the game timer `*(*(void**)0x463e290+8)+0x0c` **[high]**.
- `FUN_02ad9370`: no advance unless `sub+0xb5` bit 0 (playing); `rate = *(float*)(sub+0xa0)`, rate 0.0 = no advance;
  else `FUN_02ad88a0(sub, (int)((dt + carry) * rate))` with the fraction kept at `sub+0xc8` **[high]**.
- Trackers: the vector `trk+0x338..+0x340` (or the single `trk+0x330`); id byte `sub+0xb4`; 0xff = the main tracker,
  0..31 the others (`FUN_00c73800` searches them in that order) **[high]**.

So there are two per-character knobs: the **object timer** (all its trackers, and everything else that runs on that
object's time) and the **per-tracker rate / playing flag**.

### 2.2 The object-timer API (`IEngineTimer`, `CWBTimer` vtable `0x3473cd0`)

`T = *(void**)0x466c718` (the only holder; also `(*(IWBEngine**)0x466afa8)->vtbl+0x30()`, `0x8bd210`) **[high]**:

| Slot (byte) | Function | Signature (C, SysV) | Effect |
|---|---|---|---|
| 3 (+0x18) | `0x8f8fc0` | `HTIMER* CreateTimer(HTIMER* out, void* T)` | new `CEngineTimer` (`FUN_008c0c30` -> `0x975de0`): ref 1, num/den 1/1, scale 1.0, min frame 1 ms, max 0xffffffff, no parent (never updated until attached) |
| 4 (+0x20) | `0x8f8f60` | `int AttachTimer(void* T, HTIMER* parent, HTIMER* child)` | `FUN_00975f40(*parent, *child)`: child updated with the parent's scaled time |
| 5 (+0x28) | `0x8f8f90` | `int DetachTimer(void* T, HTIMER* t)` | `FUN_00975f90` |
| 8 (+0x40) | `0x8f8f00` | `HTIMER* GetSimulationTimer(HTIMER* out, void* T)` | `*(*(void**)0x466aee0+0x380)`, +ref |
| 9 (+0x48) | `0x8f9050` | `int SetObjectTimer(void* T, HOBJECT h, HTIMER* t)` | `h+0x68 = *t` (ref++ new, ref-- old); `*t == 0` = the default `*(*(h+0xe0)+0xc088)` |
| 10 (+0x50) | `0x8f8ec0` | `HTIMER* GetObjectTimer(HTIMER* out, void* T, HOBJECT h)` | `*(h+0x68)`, +ref |

Release a held timer with `FUN_009ec850(HTIMER* p)` (refcount `+0x08`; at 0 `vtbl+0x10` deletes) **[high]**.

Timer object (`CEngineTimer`, vtable `0x34a4e08`; update = `vf_03` `0x9758f0(timer, double parentMs)`) **[high]**:
`+0x08` refcount, `+0x0c` u32 frame ms, `+0x10` float frame s, `+0x14` u32 total ms, `+0x18` double total s,
`+0x20` u32 num, `+0x24` u32 den, `+0x28` float scale (num/den), `+0x2c` min frame ms, `+0x30` max frame ms, `+0x34`
byte paused, `+0x38` link in the parent's list, `+0x50` children list, `+0x78` double carry. Update: paused, num 0 or
den > 0xffff = a 0 ms frame (min clamp skipped); else `clamp(num/den * parent, min, max)`; children get this
timer's result.

### 2.3 Precedents and the SlowMO answer

- `SlowMO.Set` (`FUN_02824cd0`), `.Pause` (`0x2824a60`: num 0, scale 0.0), `.Resume` (`0x2824bc0`) and `.Stop`
  (`0x2824a30`) write the **global** game timer `*(*(void**)0x463e290+8)` only **[high]**. `FUN_02824bf0(num, den)`
  writes the **player's** object timer (`FUN_026b9eb0` = the player, then `GetObjectTimer`) - the SlowMo record's
  `PlayerTimeScale` next to `SimulationTimeScale` (`FUN_0261c1f0`) **[high code, med names]**. No slow-mo path
  takes an NPC.
- NPC set-up `FUN_029782a0` (from `NPC::vf_10`): data-flagged NPCs get the player's timer **[high]**.
- Sync actions: `FUN_02254290` ("Starting Sync Action") gives the participant the player's timer when the other is
  the player; the release `FUN_02252430` gives back the default with a null pointer **[high calls, med roles]**.
  Items copy the owner's timer (`FUN_0231da90`) **[med]**.
- Other per-object time users (`ObjectContextTimer`, vtable `0x34ef630`, 36 sites): combat timers
  (`FUN_026d8920`, `FUN_02709df0`, energy `FUN_02720c60`, hit counter `FUN_02729cc0`, slow-mo presentation
  `FUN_0274b3e0`), AI (`CAIAnimationController::vf_02`, `CAIServiceHeadTracking::vf_03`,
  `CAIActionGetInNodeRange::vf_11`), struggles (`CStruggleHold::vf_04`) **[high for the list]**.

So swapping an NPC's object timer at runtime is normal engine behaviour, and a 0 ms frame for a whole world is a
state the game reaches with `SlowMO.Pause` **[med]**.

### 2.4 Recipe A (recommended): the orc's own frozen timer

Game thread (G1), on a heavy hit, skipped while the orc is in a sync action (`oc+0x188`/`+0x190` set) or dead:

```
void* T = *(void**)0x466c718;
HTIMER old = 0, mine = 0;
GetObjectTimer(&old, T, hOrc);                    // +ref
CreateTimer(&mine, T);
AttachTimer(T, &old, &mine);                      // world slow-mo still composes
copy old+0x14 (u32), +0x18 (double), +0x78 (double) to mine   // the orc's clock stays continuous
mine+0x20 = 0; mine+0x24 = 1; mine+0x28 = 0.0f;   // freeze (SlowMO.Pause's values); or 1/20 = near-freeze
SetObjectTimer(T, hOrc, &mine);
// 0.12 s later on our own clock (not the orc's), or at once on death / sync start / unload:
SetObjectTimer(T, hOrc, &old);  DetachTimer(T, &mine);  FUN_009ec850(&mine);  FUN_009ec850(&old);
```

Everything on the orc's time stops together: animation (so its `APPLYDAMAGE`, windows and root motion wait), its
combat timers and AI animation control **[high for the animation, med for the rest]**. With 1/20 the min clamp makes
each frame >= 1 ms (about 0.09x at 90 Hz), which avoids exact-zero frames.

**Recipe B (animation only):** for every tracker that exists (`GetCurAnim != -1` for 0xff and 0..31): save
`GetAnimRate` and `SetAnimRate(0.0)` (IEngineModel +0x268 / +0x260), or `SetPlaying(false)` (+0x238), restore after.
Weaker: blend nodes drive some trackers themselves (`CComplexBlendNode::vf_51` `0x20b7290` sets their rate to 0 and
writes their time with +0x250), and the animation system may write rates on transitions **[med]**. Useful for the
vault blink option of DV 3 only on the player, never through the player's object timer (that scales the whole
player).

### 2.5 Sites

```
SITE("HS_TRK_DT", K_CALL, 0x238fad0, 0x238fac7, "48 83 c4 18 5b 5d c3 66 90 e8 fb 8d ff ff", 0x23888d0, "53 48 83 ec 20 48 83 bf 68 01 00 00 00 74 61"),
SITE("HS_TRK_ADVANCE", K_CALL, 0x238f855, 0x238f84d, "48 8d 54 24 10 44 89 fe e8 16 9b 74 00", 0x2ad9370, "0f b6 8f b5 00 00 00 31 c0 f6 c1 01 74 72 41"),
SITE("HS_TIMER_CREATE", K_VSLOT, 0x3473ce8, 0x3473ce8, "c0 8f 8f 00 00 00 00 00", 0x8f8fc0, "53 48 8b 05 18 1f d7 03 48 89 fb 48 8d b8 70"),
SITE("HS_TIMER_ATTACH", K_VSLOT, 0x3473cf0, 0x3473cf0, "60 8f 8f 00 00 00 00 00", 0x8f8f60, "48 8b 3e 48 85 ff 74 18 48 8b 32 48 85 f6 74"),
SITE("HS_TIMER_DETACH", K_VSLOT, 0x3473cf8, 0x3473cf8, "90 8f 8f 00 00 00 00 00", 0x8f8f90, "48 8b 3e 48 85 ff 74 18 48 83 ec 08 e8 ef cf"),
SITE("HS_SIM_TIMER", K_VSLOT, 0x3473d10, 0x3473d10, "00 8f 8f 00 00 00 00 00", 0x8f8f00, "48 8b 15 d9 1f d7 03 48 89 f8 48 85 d2 74 31"),
SITE("HS_OBJ_TIMER_SET", K_VSLOT, 0x3473d18, 0x3473d18, "50 90 8f 00 00 00 00 00", 0x8f9050, "48 85 f6 0f 84 4f 01 00 00 53 48 89 f3 48 83"),
SITE("HS_OBJ_TIMER_GET", K_VSLOT, 0x3473d20, 0x3473d20, "c0 8e 8f 00 00 00 00 00", 0x8f8ec0, "48 89 f8 48 85 d2 74 28 48 8b 52 68 48 89 17"),
SITE("HS_TIMER_UPDATE", K_VSLOT, 0x34a4e20, 0x34a4e20, "f0 58 97 00 00 00 00 00", 0x9758f0, "41 55 66 0f ef c9 41 54 55 48 89 fd 53 48 83"),
SITE("HS_TIMER_RELEASE", K_ENTRY, 0x9ec850, 0x9ec850, "48 8b 3f 48 85 ff 74 18 8b 47 08 8d 50 ff 85", 0x9ec850, "48 8b 3f 48 85 ff 74 18 8b 47 08 8d 50 ff 85"),
SITE("HS_SLOWMO_PLAYER", K_ENTRY, 0x2824bf0, 0x2824bf0, "41 54 55 89 f5 53 89 fb 48 83 ec 10 e8 af 52", 0x2824bf0, "41 54 55 89 f5 53 89 fb 48 83 ec 10 e8 af 52"),
SITE("HS_SLOWMO_PAUSE", K_ENTRY, 0x2824a60, 0x2824a60, "48 83 3d 58 42 42 02 00 74 25 48 8b 05 1f 98", 0x2824a60, "48 83 3d 58 42 42 02 00 74 25 48 8b 05 1f 98"),
SITE("HS_SET_ANIMRATE", K_VSLOT, 0x3484390, 0x3484390, "90 bd 91 00 00 00 00 00", 0x91bd90, "48 85 f6 74 4b 53 48 89 f7 0f b6 da 48 83 ec"),
SITE("HS_GET_ANIMRATE", K_VSLOT, 0x3484398, 0x3484398, "80 a5 91 00 00 00 00 00", 0x91a580, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48"),
SITE("HS_SET_PLAYING", K_VSLOT, 0x3484368, 0x3484368, "d0 bc 91 00 00 00 00 00", 0x91bcd0, "48 85 f6 74 4b 55 48 89 f7 0f b6 e9 53 0f b6"),
SITE("HS_GET_PLAYING", K_VSLOT, 0x3484360, 0x3484360, "80 bc 91 00 00 00 00 00", 0x91bc80, "48 85 f6 74 3b 53 48 89 f7 89 d3 e8 90 7f fd"),
SITE("HS_TRK_RATE_WRITE", K_ENTRY, 0x238f0e0, 0x238f0e0, "41 54 41 89 f4 55 48 89 fd 53 89 f3 48 83 ec", 0x238f0e0, "41 54 41 89 f4 55 48 89 fd 53 89 f3 48 83 ec"),
```

IEngineModel signatures used here **[high]**: +0x260 `int SetAnimRate(m, HOBJECT, u8 trk, float rate /*XMM0*/)`,
+0x268 `int GetAnimRate(m, HOBJECT, u8 trk, float* out)`, +0x230 `GetPlaying(m, HOBJECT, u8 trk)` (bit 0 of
`sub+0xb5`), +0x238 `SetPlaying(m, HOBJECT, u8 trk, bool)` (**[med]**: compares with the bit; the write path
`0x238f218` was not read); all through `CSimpleModel` thunks to the tracker node
(`0x238f0e0`, `0x238c720`, `0x23886d0`, `0x238f190`).

### 2.6 Open risks

- Code that divides by the object's frame time: `SlowMO.Pause` shows a 0 ms world frame is survivable; use 1/20 if a
  run shows trouble **[med, RT]**.
- Movement not driven by root motion (AI steering on another clock) could slide a frozen orc **[low, RT]**.
- At the restore the orc's clock jumps by the freeze length (like a 0.12 s hitch) **[med]**.
- A frozen timer must never outlive its 0.12 s: restore on death, sync start, level unload, and in a per-frame
  sanity check in G1.

### 2.7 Runtime probe

New words (section 4): `timers` (dump `GetObjectTimer` for the player and the nearest orcs, `GetSimulationTimer`,
num/den/frame ms: are orcs on the simulation timer by default?) and `hitstop MS [timer|rate|pause] [N]` on the
nearest orc in an attack (recipe A, B, or `SetPlaying`). Pass: in shots the orc holds its pose for MS and then goes
on, its `APPLYDAMAGE` (probe `P_APPLYDMG`) comes MS later than without, no warnings, the timer pointer is back
afterwards.

**Runtime result (P7).** Holds: recipe A's swap on a live orc and its restore (120 and 600 ms: 0 ms frames during the
hold, `its timer is back`; recipe B 1500 ms: `its rates are back`), with no warning, crash or stuck orc. 2.3's
question: **every orc has its own object timer** (all 25-36 live orcs `own`, none on the simulation timer, which is
also the game timer; 16-17 ms frames), the player too. Side effect found: the 120 ms hold delayed the orc's hit
reaction (`AI_Bip_L100_L2R_F`) to the hold's end, the 600 ms hold **lost it** (no reaction afterwards), so holds must
stay short (the design's 120 ms) or start after the reaction has begun. Not measured: the animation time during the
hold (the probe read tracker 0xff, 0 ms throughout) and a delayed `APPLYDAMAGE` (the held orcs were not attacking).
Next: read the playing tracker (`FindTrackerPlaying` of `c+0x17c`) during a hold on an attacking orc.

**Runtime result (P8).** Recipe A on an attacking orc, held 120 ms from its BPW (`hitstop 120 onbpw`, the playing
tracker by FindTracker of `c+0x17c`):
- the attack's animation time stood at 33 ms through the hold (+0 ms) and the timer came back;
- the impact (`APPLYDAMAGE` at Talion) came 0.933 s after BPW instead of 0.800 s: **late by the hold** (133 ms = 8
  frames);
- no side effect; the orc fought on.
2.7's pass list holds. The recoil question (does a 120 ms hold keep the hit reaction) was not tested again, because
`dealhit auto` took the player's current recoil record (1 damage, no reaction, held or not); P7's answer stands.

## 3. Animation key times (DV 3 "whether slot 51 finds APPLYDAMAGE", "calling it for an arbitrary time")

### 3.1 What the game already computes at every attack start

`FUN_02263590(ws, hObj, attack)` (`0x26eb77a` in `Combat_StartAttack`; also the player's `FUN_00a89200`,
`FUN_00a88c20`) fills ushort **ms from the animation start** into `ws` (0xffff = no such key). Source: a per-animation
timing table (`*0x46b7310` vtbl+0x88 -> `FUN_02287a90(db, animNameHash)`, the hash from IEngineModel +0x160), else
the animation's own keys (IEngineModel +0x158, the first key of each name, matched with `strcasecmp`) **[high]**:

| `Combat+` | `ws+` | Key(s) |
|---|---|---|
| `0xc8` | `0x18` | `EAL` |
| `0xca` | `0x1a` | `BCOL` (the first one) |
| **`0xcc`** | `0x1c` | **`APPLYDAMAGE` / `AD` / `BAD`** = the impact |
| `0xce` / `0xd0` / `0xd2` | `0x1e` / `0x20` / `0x22` | `STARTFACING` / `STOPFACING` / `BEFACING` |
| **`0xd4` / `0xd6`** | `0x24` / `0x26` | **`BPW` / `EPW`** = the parry window |
| `0xd8` | `0x28` | `SHOOT` |
| `0xda` | `0x2a` | `STIMULUS` (1.1) |

`Combat+0x17c` = the attack animation's index (HMODELANIM), from IEngineModel +0x1e8 GetAnimIndex of
`AnimationData.Name` in `FUN_026e7070` (`0x26e76aa`), -1 when none **[high]**. **Correction to CB 1.3:** `+0x17c` is
this animation index, not a fire node; `Combat_ApplyDamageKey_RangeAngle` samples the attacker's node at
`(anim = c+0x17c, ms = c+0xcc)` with +0x168 (`0x26e4e23`), and the `SHOOT` fire transform does the same with the
`SHOOT` time (CB 4.2) **[high / med]**.

### 3.2 The key and sampling API

| IEngineModel (byte) | Stub | Signature | Meaning | Conf. |
|---|---|---|---|---|
| +0x158 (43) | `0x91a370` | `AnimData* GetAnimData(m, HOBJECT, u32 anim)` | the animation's key data (3.3) | high |
| +0x168 (45) | `0x91c410` | `int SampleNode(m, HOBJECT, u32 anim, u32 node /*GetNode id*/, u32 ms, LTRigidTransform* out, bool flag /*game: 0*/)` | node or socket transform of `anim` at `ms` from a temporary tracker (`FUN_02ad8db0` id 0xff, `FUN_02ad8ac0` time, `FUN_02ad73e0` evaluate); the live trackers are not touched; null object = identity, 0x3c | high (calls), med (model space) |
| +0x198 (51) | `0x91b220` | `bool KeyLookup(m, HOBJECT, u32 anim, const char* key, const char* arg, u32* frame, u32* ms, u32 fromMs)` | first key command `key` (case-insensitive; if `arg` is not empty also argv[1] == `arg`) at a time >= `fromMs` (`CSimpleModel::vf_94` `0x2351dd0`) | high |
| +0x1a8 (53) | `0x91b150` | `bool FrameToTime(m, HOBJECT, u32 anim, u32 frame, u32* ms, const char** keyString)` | 30 fps frame -> ms (`FUN_02ad3e10`: exact on key frames, else 33/33/34 ms steps); `frame < frameCount` | high |
| +0x1e8 (61) | `0x91b950` | `int GetAnimIndex(m, HOBJECT, const char* name, u32* anim)` | name -> index, -1 | high |
| +0x200 (64) | `0x91ba10` | `int GetCurAnim(m, HOBJECT, u8 trk, u32* anim)` | `sub+0x38`; -1 when the tracker does not exist | high |
| +0x248 (73) | `0x91a520` | `int GetCurAnimTime(m, HOBJECT, u8 trk, u32* ms)` | `sub+0xa8`; 0 when the tracker does not exist | high |
| +0x250 (74) | `0x91bd30` | `int SetCurAnimTime(m, HOBJECT, u8 trk, u32 ms)` | `FUN_02ad8ac0` | high |
| - | `0xc73800` | `int FindTrackerPlaying(HOBJECT, u8* trk, u32 anim)` | tries `*trk`, 0xff, 0..31; 0 = found, `*trk` set (the shapecast calls it, `0x270f840`) | high |

The shapecast collider (`CCombatColliderShapecast::vf_03` `0x270f600`, GP 4) is these calls in a row: GetCurAnim of
its tracker, FindTrackerPlaying, GetCurAnimTime, KeyLookup `BCOL`/`ECOL` (from 0 ms, with the collider's key
argument), then per 30 fps frame `FrameToTime` + `SampleNode` into 0x28-byte samples (`xf` 0x1c, ms `+0x1c`, frame
`+0x20`) **[high]**.

### 3.3 The key data (`GetAnimData` result)

`+0x10` frame count (30 fps), `+0x14` key count, `+0x20` u32[count] key frame index, `+0x30` u32[count] packed
(`& 0xffffff` = time ms, `>> 24` = string index, 0xff = none), `+0x40` string table, `+0x48` string count. A key
string can hold several commands; `FUN_008bacf0` tokenises them **[high: `vf_94`, `FUN_02263590`,
`FUN_02ad3e10`]**.

### 3.4 Recipes (game thread; after phase 2 for an exact "now", AN 1.2)

- **Telegraph at the start** (G4 post-call, or `oc+0x4d0` changed): `anim = *(i32*)(oc+0x17c)`,
  `tImpact = *(u16*)(oc+0xcc)`, `tBPW/tEPW = *(u16*)(oc+0xd4/0xd6)`, `type = AttackData.AttackType` (CB 3.3).
- **Now and lead time:** `trk = 0xff; FindTrackerPlaying(hOrc, &trk, anim) == 0`, then `GetCurAnimTime(m, hOrc, trk,
  &now)` and `GetAnimRate(m, hOrc, trk, &r)`; `s` = the orc's timer scale (`+0x28`) times its parent's.
  `lead = (tImpact - now) / 1000 / (r * s)`. A blend-driven tracker plays with rate 0 (2.4): then count from the
  start with the orc's timer (`+0x14`) instead **[med]**.
- **Next hit frame (any key):** `KeyLookup(m, hOrc, anim, "APPLYDAMAGE", "", &f, &t, now)` (also `AD`, `BAD`);
  multi-hit attacks: repeat with `fromMs = t + 1` **[high]**.
- **Is the weapon collider active at t:** `KeyLookup(.."BCOL", arg, &f0, &t0, 0)` and `"ECOL"` -> active for
  `t0 <= t <= t1`; `arg` = the collider's key argument (`vf_03`'s 6th argument, from the collider set) **[low]**, an
  empty `arg` matches the first `BCOL`. Live: the hit-window byte `oc+0xe3` (`BHITWIN..EHITWIN`, CB 3.2) and the
  collider object's `+0x82` (enabled); the per-Combat collider list was not traced **[med]**.
- **Weapon or hand at the impact:** `GetNode(m, hOrc, "R_Hand" or the weapon socket, &node)` once, then
  `SampleNode(m, hOrc, anim, node, tImpact, &xf, 0)`; world ~ orc object transform now x `xf`. Root motion between
  now and the impact is not in it (a lunge moves the orc) **[med]**. One call per attack, not per frame (it builds a
  temporary tracker and a pose).

### 3.5 Sites

```
SITE("KT_KEYLOOKUP", K_VSLOT, 0x34842c8, 0x34842c8, "20 b2 91 00 00 00 00 00", 0x91b220, "41 57 41 56 41 55 41 54 55 53 48 83 ec 08 4c"),
SITE("KT_FRAMETOTIME", K_VSLOT, 0x34842d8, 0x34842d8, "50 b1 91 00 00 00 00 00", 0x91b150, "48 85 f6 74 5b 41 55 48 89 f7 4d 89 cd 41 54"),
SITE("KT_SAMPLENODE", K_VSLOT, 0x3484298, 0x3484298, "10 c4 91 00 00 00 00 00", 0x91c410, "41 56 41 55 41 54 55 53 4c 89 cb 44 8b 74 24"),
SITE("KT_GETANIMDATA", K_VSLOT, 0x3484288, 0x3484288, "70 a3 91 00 00 00 00 00", 0x91a370, "48 85 f6 74 33 53 48 89 f7 89 d3 e8 a0 98 fd"),
SITE("KT_GETANIMINDEX", K_VSLOT, 0x3484318, 0x3484318, "50 b9 91 00 00 00 00 00", 0x91b950, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 48 89"),
SITE("KT_GETCURANIM", K_VSLOT, 0x3484330, 0x3484330, "10 ba 91 00 00 00 00 00", 0x91ba10, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 0f b6"),
SITE("KT_GETANIMTIME", K_VSLOT, 0x3484378, 0x3484378, "20 a5 91 00 00 00 00 00", 0x91a520, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48"),
SITE("KT_SETANIMTIME", K_VSLOT, 0x3484380, 0x3484380, "30 bd 91 00 00 00 00 00", 0x91bd30, "48 85 f6 74 4b 55 48 89 f7 89 cd 53 0f b6 da"),
SITE("KT_FINDTRACKER", K_CALL, 0x270f840, 0x270f830, "8b 53 7c 48 8b 7b 60 48 8d 73 78 4c 8d 74 24 40", 0xc73800, "41 56 49 89 f6 41 55 41 89 d5 41 54 55 48 89"),
SITE("KT_SHAPECAST_ENABLE", K_VSLOT, 0x396df88, 0x396df88, "00 f6 70 02 00 00 00 00", 0x270f600, "41 57 4d 89 c7 41 56 44 0f b6 f2 41 55 49 89"),
SITE("KT_KEYLOOKUP_INNER", K_ENTRY, 0x2351dd0, 0x2351dd0, "41 57 49 89 cf 41 56 49 89 d6 41 55 41 54 55", 0x2351dd0, "41 57 49 89 cf 41 56 49 89 d6 41 55 41 54 55"),
SITE("KT_FRAME_TO_MS", K_ENTRY, 0x2ad3e10, 0x2ad3e10, "85 c9 0f 84 ee 00 00 00 44 8b 0e 41 39 f9 0f", 0x2ad3e10, "85 c9 0f 84 ee 00 00 00 44 8b 0e 41 39 f9 0f"),
```

(`CV_KEYTIMES` in 1.5 is the table builder's call site.)

### 3.6 Open risks

- The table holds the first key of each name; attacks with two impacts need `KeyLookup` from `now` **[high]**.
- `ws` times are relative to the animation start; a blend-in or a start offset shifts the real moment - compare with
  the live `APPLYDAMAGE` (C6) **[RT]**.
- `SampleNode` is the raw single animation (no blend, IK or additive layers) without root motion **[med]**.

### 3.7 Runtime probe

New word `keytimes` (section 4) at each orc attack start (hook `CV_KEYTIMES` post, or poll `oc+0x4d0`): log `anim`,
the ten `ws` times, the tracker found, `now`, rate, timer scale, `KeyLookup` of `APPLYDAMAGE`/`BPW`/`EPW`/`BCOL`/`ECOL`
and the predicted `R_Hand` at the impact; then at the real `APPLYDAMAGE` (`P_APPLYDMG`) the measured time since the
start and `GetNodeTransform(R_Hand)`. Pass: predicted vs measured impact within one frame, hand within 20 cm (more
with lunges: that sets the block bar's margin). Answers runtime-probes C6 without the timing table.

**Runtime result (P7).** Holds, with 3.4's fallback: every attack tracker plays at **rate 0** (blend-driven: 508 of
508, found 507), so the lead is counted from the start, and then the table's `APPLYDAMAGE` (`Combat+0xcc`) is the
impact: for 30 AI attacks with an impact key the measured hit came 0-18 ms after the table time 29 times (one frame)
and 48 ms once; `KeyLookup` agrees with the table. `SampleNode` is **model space** (x the object transform: `R_Hand`
within 20 cm 29 times of 30, mean 7 cm; as world 330-470 m off). Talion's own strikes: the time fits (400 ms vs 0.415
s), the hand does not (40-112 cm). Open: the same against Talion (no orc attacked him in P7).

**Runtime result (P8, C6).** The same against Talion, 51 attacks.
- Infantry melee: BPW is 0 (the window opens at the attack start; the counter prompt fires one tick later), EPW is
  747-800 ms, and the impact comes 815-817 ms after the start (the table's `APPLYDAMAGE` 800-811 ms + one frame). That
  is 16-70 ms after the window closes, 0.80 s after BPW.
- Archers: BPW 1566 / EPW 2566 ms; the release (`APPLYDAMAGE`, the arrow's spawn) at 2648 ms, 82 ms after EPW.
- `SampleNode` x the object put `R_Hand` within 3.6-34 cm (mean 22).
- So the VR parry window = `[BPW, EPW]` of the table, read at the attack start, and the hit lands a few frames after
  EPW.

## 4. New probe words (for `mod/probe`)

| Word | Does | Hooks / calls | Answers |
|---|---|---|---|
| `stimlog on\|off` | logs every stimulus registration: record name (`FUN_025aa530(desc+0x00)`), source, target, persistent, id, and which of the three callers | `CV_REGSTIM`, `CV_REGSTIM_PERSIST`; `CV_STIM_START/KEY/STOP` | 1.6 (which stimuli the player's attacks emit) |
| `aiwatch on\|off` | logs, per orc, entry/result of the counter goal and the block/dodge actions, with the player's current attack (`FUN_026d4510(pc)`) | vtable slots `CV_AI_GOAL_COUNTER`, `CV_AI_BLOCK_VALID/ACT`, `CV_AI_DODGE_VALID/ACT` | 1.3, 1.6 |
| `swingevent NAME\|auto [MS] [cur]` | step 1 of 1.4 toward the nearest orc; with `cur`, also step 2 for MS | `FUN_026c7180`, `FUN_0293e150` | 1.4 |
| `srvatk NAME\|auto MS` | step 3 of 1.4: server-only start, stop after MS | `FUN_026ed9b0`, `FUN_026ee800` | 1.4, CB 9 open question |
| `timers` | object timers of the player and nearby orcs, the simulation timer | IEngineTimer slots 8, 10 | 2.3 |
| `hitstop MS [timer\|rate\|pause]` | recipe A / B / SetPlaying on the nearest orc | 2.2, 2.4 | 2.6 |
| `keytimes on\|off` | 3.7 | `CV_KEYTIMES`, `KT_*` | 3.6, C6 |

Extend `attacks` (C1) with the columns `RegisterStimulus`, `PersistentStimulus`, `AIData.DodgeChance`, `IsBlock`
for every record (field readers CB 8 #13) - picks `rec` for 1.4.

## 5. For design-vr.md (markers this resolves)

- Section 3, "raise that event. **needs RE: the event the AI listens to** (`CombatAttackStartArgs`?)" -> 1.1-1.4:
  the AI stimuli of `FUN_026c7180` (and, if RT needs it, the swing as the player's current attack); RT
  `stimlog`/`aiwatch`/`swingevent`.
- Section 3, "**needs RE: the per-character animation rate** (IEngineModel tracker slots 54-96)" -> 2: the object
  timer (`SetObjectTimer`), per-tracker `SetAnimRate` +0x260 / `SetPlaying` +0x238; `SlowMO.*` global.
- Section 3 telegraph, "**needs RE: whether it finds `APPLYDAMAGE`**" -> 3.1-3.2: yes; and `Combat+0xcc` already
  holds it.
- Section 3 block bar, "**needs RE: calling it for an arbitrary time**" -> 3.2: `SampleNode` +0x168.
- Section 13, "a synthesised swing event (needs RE)" -> 1.4.

## 6. Status: the first combat module in mod/somvr (2026-10-19, offline; nothing launched)

`SOMVR_COMBAT=0|1|log` (re-xr-pipeline.md 6.3; pure part `mod/somvr/somvr_combat.c`, game side `combat_frame` in
`somvr.c`, after H4's body) uses this document as follows:
- 1.4 step 1 as written: at a swing's start (the 80 ms mean tip speed crosses 3 m/s) toward the nearest orc it heads at
  within 1.5 m, `FUN_026c7180(pc, FUN_00a91300(rec), hOrc)` with the light tier's next record; at the swing's end the
  persistent stimulus is removed exactly as `FUN_026c7180` does it at `0x26c7758` (`FUN_0293c390` lookup, then
  `FUN_0293e150`, `pc+0x168 = 0`), only while `pc+0x168` still holds the id our call left. Steps 2 and 3 are not built.
  The byte `0x4c47090` is logged once at the first stimulus.
- 2.4 recipe A with two changes: one timer for the run (made at the first hit-stop, never attached, so never updated:
  its frame stays 0 ms; no `AttachTimer`/`DetachTimer`), and the clock copy (`+0x14`, `+0x18`, `+0x78`) at each freeze.
  120 ms on our clock, back at once on death or a sync start (`+0x188`/`+0x190`), nothing written to an orc whose
  Combat/HOBJECT/`+0x68` no longer checks out (only the old timer's reference released); one orc at a time; 2.6's
  per-frame sanity check is the first thing of every combat frame.
- 3 (key times) is not used yet (no telegraph, block bar or parry).
- The hits are re-combat.md 1.6's recipe (the probe's `dealhit`): `pc+0x15c` = the blade's velocity direction,
  `pc+0x27a` = 0 for the call, both restored; `hitPos` = the contact point on the orc's capsule.
RT before relying on any of it: the first combat boot's checks in re-xr-pipeline.md 6.3 (C2 from H4's post, whether
the counter AI reacts to the stimulus, the timer swap on a live orc). **P7 (2026-10-20):** step 1 and the timer
swap work from code, but the stimulus of the light tier's records is `WeaponImpactSound` and no orc reacted (1.7's
result); a hold longer than ~120 ms can swallow the hit reaction (2.7's result).

**Status 2026-10-20 (offline; nothing launched): orc reactions, `SOMVR_COMBAT_REACT=1|force`** (re-xr-pipeline.md
6.3.1). This replaces 1.4's open question "what makes the AI react" with re-combat-spawn.md 2's answer.
- It does **step 1b** of re-combat-spawn.md 2.6. For every orc a swing heads at (the stimulus' test), once per swing in
  the swing's first 250 ms, it rolls the orc's `AIBlockTable` as `Combat_StartAttack` does: the record
  `FUN_026ff330(*(*(npc+0x10)+0x70))`, the first entry with no `Category` or one of the light record's categories,
  default 0.5, the game's `FUN_00c73440(0, 1)`. On a win it calls `FUN_029ac890(*(pc+0x70), npc)`.
- The stimulus (step 1) stays for awareness. Steps 2 and 3 are still not built.
- 1.3's "decided at damage time" holds for our hits too. `Combat_DealHit`'s damage context reads `FUN_026d4dc0` (the
  orc's current attack `IsBlock`) and takes the blocked branch itself (block recoil, block FX), so no blocked record
  is needed.
- The module reads the same state before the call. On a block it logs `REACT blocked`, gives the blocked haptic, and
  skips the hit-stop.
- Sites: the four functions only (`RX_*`, `K_ENTRY`), so no collision with the probe's `P_AIB_*` call-site hooks.
- RT: re-xr-pipeline.md 6.3.1's runtime checks; P9's AB1-AB3 answer whether a notify makes the orc block or dodge.
  **P9 (1.7):** a notify makes a defender block, but the roll never notifies defenders or berserkers (no table).

**Status 2026-10-20, the P9 fix (offline; nothing launched).** An orc whose character combat record has no
`AIBlockTable`, or one with no entries (P9: `AI_Defender`, `AI_Berserker`), now gets our own roll instead of none:
- the chance per archetype (the record's name): `SOMVR_COMBAT_REACT_DEFENDER` 0.6, `_BERSERKER` 0.4, `_OTHER` 0.5
  (0..1, 0 = never); the game's roll function, `force` = a win; a win makes the same `FUN_029ac890` call;
- only while the orc is **out of its own attack**: no call when `Combat+0x4d0` (its current attack) is set and both
  cancel bytes `+0xe0`/`+0xe1` are 0, the counter goal's own gate (`CAIGoalCounterMelee::vf_03`, proven by the new
  read-only site `RX_ATK_GATE`). P9: of three `incoming` calls only the one outside the defender's attack raised its
  block;
- still one call per swing per orc; an orc with a table rolls it as before; the shield's own block at damage time is
  untouched (P9 SH1: `block=1`, no damage);
- captains: they use their archetype's record (Ghâm is `AI_Infantry` with the `Infantry` table), so a defender or
  berserker captain gets that archetype's chance. A chance of its own for captains would need the character -> faction
  member link, which no doc maps yet (re-gameplay.md 6 and 11).
- RT: `REACT direct roll ... -> notify`, then (probe `aiblockwatch on`) `goal_counter relevant 0 -> 1` and `block_go`
  for a defender; the berserker's dodge is still open.
