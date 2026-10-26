# Shadow of Mordor VR: the runtime probe plan

Every open runtime question of the research docs, deduplicated, with the probe that answers it, and the probe runs
(boot scripts) they are grouped into. Written 2026-10-18 from `docs/re-firstperson.md`, `re-combat.md`,
`re-input.md`, `re-animation.md`, `re-ui-effects.md`, `re-xr-pipeline.md`, `re-stereo.md`, `re-gameplay.md`,
`re-view-render.md` and `design-vr.md`. Nothing was launched for it. 2026-10-19 night: P7 (the probe words asked for by
`re-combat-vr.md`, `re-combat-items.md`, `re-hud.md`, `re-resolution.md` and `re-traversal.md`), offline only.
2026-10-20: P8 (a fight at Talion, the head among the attached models, `re-arms-ik.md`'s and `re-ui-pointer.md`'s
words, `re-traversal.md`'s surf/grav/mount, two P7 readout fixes), offline only; run at 02:21 the same night (results
under P8). 2026-10-20 later: P9 (`re-combat-spawn.md`'s test orcs and the AIBlockTable roll behind the block/dodge AI, a
shield defender, the arena light; `re-bow.md`'s `wmdump` and aim ray; two P8 nits), offline only; run at 09:46 the
same day (results under P9). 2026-10-20 afternoon: P10 (`design-locomotion.md` 4.2's locomotion words, LR1-LR10, and
P9's `wmhold` bug), offline only.

Boots are scarce: ~20-minute slots, shared with Hand of Fate 2, which has priority. So a run must answer as many
questions as one slot allows, and every run is scripted in advance. The library that answers them is
`mod/probe/somvr_probe.so` (an LD_PRELOAD library loaded **next to** `mod/spike1/build/somvr_spike1.so`, which keeps
the fail-closed save guard). Its words are listed in the header of `mod/probe/somvr_probe.c`; the words used below are
quoted verbatim.

- **Risk** `R` = read-only (reads game memory, calls only query functions, patches only call sites/vtable slots for
  logging), `A` = active (writes game state or calls a game function that changes the world). Active words are
  refused unless spike1's save guard is in place (`SOMVR_PROBE_NOGUARD_OK=1` overrides; do not use it in a run that
  loads the user's save).
- **M** = the milestone of `design-vr.md` section 12 that the answer unblocks.

## 0. How a probe run works

```
scripts/boot_check.sh --steam --save-copy --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
  --preload $SOM/mod/spike1/build/somvr_spike1.so --preload $SOM/mod/probe/build/somvr_probe.so \
  --game-env SOMVR_COMFORT=1 \
  --game-env SOMVR_PROBE_AUTO='<the run's words>' \
  --cmds scripts/cmds/probe-P1.cmd --hold 900
```

(`$SOM` = the absolute ShadowOfMordor path. boot_check sets `SOMVR_PROBE_CMD=RUN/probe.cmd`,
`SOMVR_PROBE_LOG=RUN/probe.log` and `SOMVR_PROBE_OUT=RUN/probe-out` itself: RUN does not exist before the run.)

Two ways to send words, and both are needed:

1. **`SOMVR_PROBE_AUTO`** (works with today's harness): `EVENT[+SECS] WORDS | EVENT[+SECS] WORDS | ...` with
   `EVENT` = `start`, `menu` (the front end, stable for 3 s) or `world` (a live player in the world, not paused,
   stable for 3 s). The probe fires the words itself, so a run needs no harness support for timing. The whole P1
   word list fits in one `--game-env` value.
2. **the command file** `SOMVR_PROBE_CMD` (boot_check's `probe WORDS` command, section 4): the coordinator writes words
   while the run is going (tmp + rename; the probe reads every new version once). This is what makes a slot
   interactive - without it a run can only follow the script.

The probe writes `probe.log` next to the run's `somvr.log` and its TSV dumps into `probe-out/`. Log lines are
rate-limited (the first 60 of a category, then 8 per second, with `SUPPRESSED` counts), so a hot hook cannot flood
the slot.

## 1. The open-question register

### (a) Input (the P1-critical group)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **I1** | **Can our own code drive the game's input at all?** Does a command fired at `PlayerInput` (`FUN_00aab660`/`FUN_00aab5d0`) reach the player, and does a held value through `CWBInput::vf_22/vf_23/vf_25` make polled actions (move, sprint, aim) believe a pad is pressed? **Run 3: partly:** `move 0 1 4000` (held ForwardAxis/StrafeAxis records) ran Talion 616 cm forward, `polled=1446` in 241 ticks; `cmd`, `pad`, `look` not run yet. | re-input.md 3.2, 8; the coordinator's rule: no uinput device, in-process only | the player moves / the action happens in the shot and the log, with no device on the system | `cmd`, `move`, `look`, `pad`, `release` (the probe overrides the three query slots for the exact command records it holds and sends the ON/OFF events; `polled=` in the `VIN release` line counts how often the game read our value) | A | M2 |
| I2 | Which command id is strike, counter, stun, evade, ...? The default bindings of all 349 commands, in pad and in KBM mode. **Run 3: answered for KBM mode** (P1 results): 55 commands bound; CombatPrimary01-04 = pad X/Y/B/A. The pad-mode dump is still open. | re-input.md 2.2, 9.1 | the record's four input ids per command, decoded | `bindings` (TSV), then `padmode on`/`off` and `bindings` again | R | M2, M4 |
| I3 | Does the game accept a *held* analog value (dead zones are applied below us), i.e. is `FUN_00ad0af0`'s result ours? **Run 3: yes for ForwardAxis 1.0** (tags `Run`, 616 cm in 4 s); a partial value with `pose` still open. | re-input.md 3.3 | the move stick value in the log vs the animation reaction | `move 0 1 2000` with `pose`/shots | A | M2 |
| I4 | Pad vs KBM mode: do the auto-switch bytes `0x46b0d60/61` really hold 1/1, and does forcing pad mode stick while the harness types keys? **Run 3: partly:** `allow=1/1` and `padmode=0` in every `STATE` line; forcing pad mode not run. | re-input.md 3.4, 9.6 | the profile byte `+0x238` and the glyph style in a shot | `padmode on` (the probe also writes `0x46b0d61 = 0`), then `state` lines | A | M2 |
| I5 | Per-frame order and threads: device update before the player update, targeting inside `FUN_00a8e120`, which hook runs on which thread. **Run 3: partly:** every hook's `FIRST` is on the game thread (`WinMain`); the live player update is `FUN_00a40fe0` (P1 results); the in-frame order is still open. | re-input.md 9.3, re-animation.md 9.2, re-gameplay.md 2 | the `FIRST` line of every hook (tid and thread name) plus the order and `t=` of the lines inside one frame | every run (the probe logs `FIRST` on each hook's first call) | R | M1 |
| I6 | Is a command "swallowed" or outside the active command set when we fire it (`PlayerInput+0x130`, `+0x29`)? **Run 3: partly:** 86 of 349 commands active in the world, swallow byte 0 for all 349; `cmd` not run. | re-input.md 1.5 | `active=` and `swallow` in the `VIN`/`bindings` lines | `cmd`, `bindings` | R | M2 |

### (b) Animation pose, head and body

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **A1** | **Pose freshness:** is the head bone's pose in the player update this frame's or last frame's? (decides whether the VR camera is built from the bone or from the object + last frame's offset) | re-firstperson.md 2.3, 7.1; re-animation.md 0, 1.2 answers it statically: the player update runs *while* the animation jobs run, so it sees last frame's pose, and this frame's exists from phase 2 (`0x9eae9a`), before H1 | the node's world position before and after `FUN_00a51210`, after the camera post-process and at the renderer's visibility request (right after H1) in the same frame: the first two must be equal and the last one different while the player moves | `nodes` then `pose 120` (`POSE` lines, `POSE-SUMMARY` counters) | R | M1, M3 |
| **A2** | The head node's **name** and the eye offset from it; is there a `SocketHead` socket? Are the skeleton's per-node name pointers (`skel+0x38`) still valid at run time? **Run 3: answered:** `Head` (node 28, parent `Neck` 24) = the parent of `SocketHead` (socket 2); 168 of 168 names readable and hashing correctly; eye nodes and camera offset in P1 results (the first-person eye offset: P3). | re-firstperson.md 2.2, 7.2; re-animation.md 2.2, 2.4, 9.1 | the whole skeleton: index, parent byte, name, stored hash vs `somhash(name)`, world transform; the socket table; the head bone = the parent node of socket `SocketHead`; then head vs the gameplay camera position | `nodes` (TSV `nodes-N.tsv`, with the name/hash check counters in the `DONE` line) | R | M1, M3 |
| A3 | Is `*(mgr+0x560)` the same engine object as `*(body+0x1b8)`? **Run 3: yes:** `hChar = bodyObj = 0x3707e8d0`. | re-firstperson.md 7.7 | both pointers in one line | `nodes`, `pieces` (they log both) | R | M1 |
| A4 | Does writing the gameplay camera every frame (F1) upset the transition blend, WorldCamera, the combat camera or the AI? Does the `FUN_00a40fe0` variant ever run? **Run 3:** `FUN_00a40fe0` is the variant that runs (once per frame); `FUN_00a416a0`'s sites never fired. The `fpcam` part is open. | re-firstperson.md 7.5 | the game's own behaviour with the camera at the head node for a minute, plus the hook counts of both variants | `fpcam on` (then `state`, shots, `orcs`) | A | M1, M2 |
| A5 | The arm-IK write point (G11): does writing the model-space pose after the jobs reach the screen, and does nothing downstream overwrite it? Run 3 supplied the arm chain names (P1 results). **P8: pass** (`armpose`: `R_Hand` read back 0.0 cm, the arm moved in shots 31/32; the sword follow not tested, it was sheathed). | design-vr.md G11, risk 1; **re-animation.md 3.2, 8, 9.3 found the point**: post-hook `CSimpleModelTrackerNode::vf_03` (vtable slot `0x38df398`), the player's tracker only | rotate `R_Hand`'s subtree by 30 deg every frame: the hand and the sword turn on screen, `GetNodeTransform` agrees next frame, no flicker | **P8: `armpose rot DEG` / `armpose ik X Y Z`** (post-hook `P_TRK_EVAL` = re-arms-ik.md `ARM_TRK_EVAL`, the player's tracker only; `ARMPOSE` lines: the next update's read-back and the sword vs `object x pose'[node] x offset` through `P_ATTACH_LISTEN`) | A | M3 |
| A7 | Which systems move Talion's arms themselves (`CAnimationMgr::IK`, `ProceduralBody::Arm` node callbacks), and when are they removed? **P8: none in idle and melee** (0 IK setups, the slots unused, `IK_*Hand` at the origin, 0 callbacks on Talion). | re-animation.md 9.5, 9.6 | the callback type table `0x46a7e60` and `AddNodeCallback` calls for the player's model | **P8: `ikcb`** (IEngineModel slots 38/39 on Talion: type, node, owner) and **`ikwatch`** (`Modifier.IK` chain setups and updates, re-arms-ik.md 1.2) | R | M3 |
| A8 | Weapon attachments: which entries does the player's model carry with the sword drawn, sheathed, the dagger in a stealth kill, the bow aimed? Run 3: the weapon/sheath node names only (P1 results). **P8:** 7 attached children, all weapon models (sword 0-2 on `scabbard_back`, dagger 3-5 on `Dagger_Sheath`, bow 6); children 1 and 4 are the sheathed hilts. | re-animation.md 4, 9.7; re-combat.md 4.1, 9.9 | the attachment list `*(playerSm+0x90)` with node/socket and offset per entry | P7 `weapons` (the item entries); **P8 `attached`** (the attach node's entries, one level down too) and **`wnodes`** (the weapon model's skeleton and its entry) | R | M3, M5 |
| A9 | Does the physics node or a ragdoll ever take over the pose while Talion is alive (hit reactions, knockdowns)? | re-animation.md 7, 9.8 | `*(playerSm+0x180)` present, the ragdoll flag `+0x15d` over a fight | (next version: part of a `weapons`/`phys` word) | R | M4 |
| A10 | Does the character object move between the pose (phase 2) and H1 (the physics step), i.e. must the IK target be expressed relative to H1's object transform? | re-animation.md 9.4 | the object transform logged at both points while running and sprinting | `pose 120` logs the object transform at every sample point (`obj_pre`, `obj_post`, `obj_vis`) | R | M3 |
| A6 | Body follow: does writing the character object's yaw hold, or does root motion overwrite it? | re-firstperson.md 4, 7.6 | the object's quaternion over the following frames | (next version of the probe: a `bodyyaw` word; not implemented) | A | M2 |

### (c) First-person body: pieces and shadow

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **P1q** | The **piece names** of Talion's model (which ones are head, hood, hair, helmet). **Run 3: no usable names:** 17 pieces, only 0 and 1 named (`..._pd_Talion_Hair_Bones.` (cut by the probe; `.hkt`?), `..._pd_ragged_cloak.hkt`), 2-16 empty; the head/hood pieces must be found by index with shots (P3). **P7:** one shot per piece hidden by index (re-resolution.md 2.4). **P7 result: open** - no `piecehide` 0-16 removed anything visible; the head is not among these 17 pieces (P7 results). **P8:** `piecehide all` (is the visible body hChar's model at all?), the mask chain logged (`PIECEMASK`), then `attached` + `attachhide N` / `N node` per attached child with a shot each. **P8 result: open** - `piecehide all` hides nothing visible, and no attached child is the head (all 7 are weapon models). | re-firstperson.md 3.1, 7.3; re-resolution.md 2.4 | the piece count and every name; the head box in a shot per piece | `pieces`; `piecehide N|all|off` (P7/P8, A); `attached`, `attachhide ID [node]` (P8) | R/A | M2 |
| **P2q** | Does hiding those pieces also remove them **from the shadow**? **Answered statically** (re-resolution.md 2.1/2.2): yes, every pass (shadow maps too) tests the one mask `node+0x130`; a shadow-only head needs the four `HEAD_SHADOW_*` windows (not in this probe). **P7:** the shot was not taken (no piece hide showed). | re-firstperson.md 3.2, 7.3; re-resolution.md 2 | a shot with the sun behind the player, before and after `hidehead` | `hidehead`, `showhead` + shots | A | M2 |
| P3q | Does a 5-8 cm near plane clip the head/hood instead (and what does it cost at the hands)? | re-firstperson.md 3.3 | shots at near 27.5 vs 5 cm | `near 5`, `near off` + shots | A | M2 |

### (d) Combat

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **C1** | The **attack database**: which `Combat/Attack` records exist, and which are the player's light/heavy/finisher attacks? **Run 3: partly:** 10814 records in `attacks.tsv`; the player's attack hooks did not fire (no fight). | re-combat.md 1.4, 9.1 | every record name (and the player attacks the game itself starts) | `attacks` (TSV), plus the `ATK` lines of the player attack-start hooks while the player fights normally | R | M4 |
| **C2** | **Does `Combat_DealHit` work from our code**, with no player animation and no active attack: damage, recoil, hit FX, death, XP, Nemesis credit? **P7: yes** for damage, recoil, the stock blood FX and impact sound, the hit counter (6 hits); death, XP and Nemesis not tried. | re-combat.md 0, 1.6, 9.2 | the `HITEV`/`DEATH` lines and the orc's reaction in the shots; `exec Faction.Dump @out/FILE` for a captain (K1, P5) | `dealhit [NAME]`, `dealhit NAME kill` | A | M4 |
| C3 | Does the recoil follow the blade (`Combat+0x15c`, `HitInfo+0x0c/+0x30`)? **P7:** not judged (no readable shot). | re-combat.md 9.3 | the orc's stagger direction in shots with the hit direction set from different sides | `dealhit` (the probe sets `+0x15c` from the player to the target and restores it) | A | M4 |
| C4 | Does the hit counter (combo) rise from our hits; is `+0x27a` the reset? **P7: partly:** our hits start it (x1); no streak tried. | re-combat.md 1.5, 9.4 | the HUD streak in shots over several `dealhit`s with different records | `dealhit` x N | A | M4 |
| C5 | Decapitation: does a kill with a `SeverTargetOnKill` attack sever on its own? | re-combat.md 2.4, 9.5 | the body in the shot after `dealhit NAME kill` | `dealhit` + shots | A | M4 |
| **C6** | **Telegraph timing:** how long before the hit does `Combat+0xe9` (parry window) open, and when does `APPLYDAMAGE` land? **P7: partly** (orc against orc/troll): windows 566-898 ms, the impact 0.55-0.97 s after BPW; nothing aimed at Talion. **P8:** orcs aimed at Talion by `aggro`; `KEYHIT` now carries the target and the table's parry window against the measured impact. **P8: answered**: melee windows open at the start (BPW 0) and close at 747-800 ms, the impact 16-70 ms later (0.80 s after BPW); archers 1566..2566 ms, the release 82 ms after. | re-combat.md 3.2, 9.6 | `COUNTERPROMPT` (the `BPW` key, with the window length) and `APPLYDAMAGE` lines with `since_bpw` (both mark `(THE PLAYER)`); `KEYHIT ... parry window BPW..EPW: the impact inside/after` | `aggro`, `keytimes on` (P8) | R/A | M4 |
| C7 | Counter by input inside the window vs cancel + recoil. **P7:** not tested (no window against Talion, 0 presses). **P8:** `autocounter CombatPrimary02 3` while `aggro`'s orcs attack. **P8 result: no counter** (13 presses inside windows; KBM mode, where the prompt shows RMB = CombatKBM09). **P9: pass in KBM mode** (`autocounter CombatKBM09 3`: 4 counter syncs from 4 melee windows; 2 `PC_CounterAttempt_Fail` at thrown spears); pad mode untested. | re-combat.md 3.6, 9.7 | does pressing the counter command in the window produce the game's counter? | `autocounter CMD` (presses the command when a window opens) | A | M4 |
| C8 | Arrows from our own transform; weapon model object; a physics ray against an orc | re-combat.md 9.8-9.10 | (next version: `fire`, `weapon`, `ray` words; not implemented) | - | A | M5 |
| C9 | Which orcs are in range, and what the fight circle thinks. **Run 3: partly:** 29 live Combat objects, the nearest 53 m away; orcs in range: P2. **P7:** 37 and 31 candidates in the camp. | re-gameplay.md 6, re-combat.md 8 | distance, record name, current attack, window bytes per Combat object | `orcs` | R | M4 |
| **CV1** | **The AI's view of a swing:** which stimulus records the player's stock attacks register (and when), and does `FUN_026c7180(pc, AttackData(rec), hOrc)` from our code (step 1), plus the swing as the player's current attack (step 2), make the target run its counter goal / block / dodge? **P7: not shown:** the stock `PC_HC_*` strikes register only `WeaponImpactSound`; 4 synthetic swings (2 with `cur`) gave 0 AI reactions, `AI_GOAL` was never called, no block; no baseline (no orc fought Talion). Next: an engaged fight, a `Weapon_XSwing` record, step 3. **P8:** the same words against orcs `aggro` set on Talion, with `PC_HC_High_L1_D0_F_LR_RR_UD_2` (`Weapon_XSwing`). **P8 result: no reaction** to the `Weapon_XSwing` swing, and none to the stock strikes either (the AI goal/block/dodge hooks ran 0 times); `cur` refused (Talion recoiling). **P9:** re-combat-spawn.md found the trigger, the target's AIBlockTable roll at the attack start (no stimulus): AB1-AB3 below, on spawned orcs. **P9 result: partly:** a defender blocks after our `incoming` (`block_go` -> `AI_Defender_Block_Into`, when out of its own attack); a forced notify on an infantry gives `block_valid`/`dodge_valid` 0; `AI_Defender`/`AI_Berserker` have no AIBlockTable (no roll). | re-combat-vr.md 1.4, 1.6, 1.7 | `STIM`/`STIMREG` lines (caller start/key/stop/probe, `rc`, `player_off`), `AI` lines, `SWING ... result` counters; `HITEV block=` after a `dealhit` | `stimlog`, `aiwatch`, `swingevent auto [MS] [cur]`, `cmd CombatPrimary01` (P7); `aggro` (P8) | R/A | M4 |
| **CV2** | **Hit-stop by the orc's own timer** (SetObjectTimer swap): does the orc hold its pose for MS and go on, does its APPLYDAMAGE come later, any side effects, is the timer back? Are orcs on the simulation timer by default? **P7: the swap and the restore are clean** (120/600 ms timer, 1500 ms rate); every orc has its own timer; the 120 ms hold delayed the recoil to its end, the 600 ms hold lost it; the animation time not measured. **P8:** the hold reads the playing tracker (FindTracker of `Combat+0x17c`); `hitstop 120 onbpw` holds an orc attacking Talion from its BPW. **P8 result:** the tracker stood still (+0 ms), the impact came 133 ms late, the timer came back; the recoil case not tested again (`dealhit auto` used a recoil record). | re-combat-vr.md 2.3-2.7 | `TIMERS`/`TIMER` classes, `HITSTOP` lines (tracker ms at the start, during the hold, after; the orc's timer frame 0 ms; `its timer is back`), `HITSTOP ... APPLYDAMAGE x s after`, a mid-hold shot | `timers`, `hitstop MS [rate]`, `dealhit auto` (P7) | R/A | M4 |
| CV3 | **Key times at the attack start** (`Combat+0xc8..`, `+0x17c`) vs the real impact; KeyLookup and SampleNode work for an arbitrary attack and time; the hand at the impact. **P7: yes, counted from the start:** tracker rate 0 (blend-driven) everywhere; the table's APPLYDAMAGE = the impact within one frame (29 of 30), the hand within 20 cm (29 of 30), SampleNode is model space. **P8:** the same against Talion (the impact exact, the hand 3.6-34 cm). | re-combat-vr.md 3.6, 3.7 | `KEYTIMES` (table, tracker, rate, keys, `impact in`) and `KEYHIT` (measured vs predicted s, hand error cm) per orc attack | `keytimes on` in a fight (P7) | R | M4 |
| CI1-3 | Projectiles: speeds, flight times, homing/gravity/spline, thrown spears = CProjectile?; `+0x228 = 1` removes one cleanly; a rewritten quaternion flies back (with `+0x58` = us it hurts the archer). **P7: open** (no projectile near Talion once the loggers were on); thrown spears and the troll's rocks are CProjectiles. | re-combat-items.md 1.6, 7 | `PROJ new/fly/end`, `PROJHIT`, `PROJBLOCK`/`PROJREFLECT` + `PROJWATCH` | `projlog`, `projblock [M]`, `projreflect [M] [own]` (P7; needs archers in range); P8: `pick Archer` + `aggro 2 Archer` first. **P8: answered**: arrows 2800 cm/s, range 200 m, no homing; `projblock` removes one cleanly; `projreflect` flies back as ours but missed the spearman. | R/A | M4, M5 |
| CI4-5 | The FX name table (does a hit streak start a sword glow by itself?); our FX on the active weapon's model object, one-shot and looping, stopped cleanly. **P7: partly:** 23 names; `elf_lightchain` at each stock strike's BCOL and the blood trail at each of our hits come by themselves; `weaponfx` one shot/loop/stop `rc=1`, the look not seen. | re-combat-items.md 2.3, 2.4, 7 | `FX` lines and the `FXNAMES` table (`probe-out/fxnames-N.tsv`), `WEAPONFX ... rc=`, shots | `fxlog`, `weaponfx last [loop]`, `weaponfx stop` (P7) | R/A | M4 |
| CI6 | Weapon slots (slot 1 = Acharn?), the dagger at `L_Weapon` via `FUN_0231e2e0` and back. **P7: pass:** slot 1 = `PC_Dagger_01`; to `L_Weapon` and back (shots 11, 12). | re-combat-items.md 3, 7 | `WEAPON` lines (records, nodes), `DAGGER` lines (model object vs node, cm), shots | `weapons`, `dagger hand`, `dagger back` (P7) | R/A | M5 |
| CI7 | Is brutalize a sync (`PC_Bip_StealthTorment_*`'s `AnimationData.Sync`, `TargetAttack`, `AOEOnHit`)? **P7: yes, a sync** (the StealthBrand sync, TargetAttack `AI_Bip_PC_StealthBrand_B/F`). | re-combat-items.md 4 | `SYNCINFO`/`SYNC` lines; `attacks-full.tsv` (Sync column) | `syncinfo NAME|PREFIX*`, `attacks full` (P7) | R | M5 |
| CI8 | The combat event names in use; does a posted event return a playing id (2D, and at the hand)? **P7: yes:** ids 4627 (2D) and 13337 (at the hand); the combat names in the P7 results. | re-combat-items.md 5, 7 | `SOUND` lines, `SOUNDNAMES`, `DONE ... sound ... playing id` | `soundlog`, `sound NAME|last [hand]` (P7; the harness's audio is the SDL dummy: ids only) | R/A | M4 |

### (e) UI, HUD and screen effects

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **U1** | **Who queues the Scaleform display** (`CRendererHandle::vf_105`), with which target and layer mask, and from where? **Run 3: front end only:** `this=0x461df60 target=0 mask=0xffffffff ra=0xa124c9`; 3 calls per frame in the world, callers not logged (`ui` not run). **P7: confirmed in the world:** `0xa124c9`/0, `0xa0dc3c`/1/3, `0xa0dc69`/1/0x38. | re-ui-effects.md 1.3, 8.1 | the return address, `esi` and `edx` of the slot | `ui` (arms 120 logged calls) | R | M2, M8 |
| **U2** | Is the UI surface really its own texture when `InterfaceWidth/Height` differ from the 3D size (and is its alpha straight or premultiplied)? **P7:** the formats `dev+0xa0`/`+0xe8` in the `ui` line; separate textures with the tall 3D size (`res`, R1). The alpha needs a CPU read of the UI surface: not in this probe (a D3D11 Map from the render thread risks a deadlock on Feral's threaded context); planned through somvr's H11b copy and the layer's dump (re-hud.md U9). **P7 result:** separate textures with a tall 3D size, both formats 28; the alpha open. | re-ui-effects.md 1.4, 8.2; re-stereo.md 4; re-hud.md 1.2, 6 | the device's two sizes and formats and the two texture pointers in one line | `ui`, `res` | R | M2, M8 |
| U3 | Which nav markers go through which projection site; does forcing "off-screen" hide the icon and the edge arrow? **P7:** the marker list (re-hud.md U3'): records, icon attributes, positions; the hide itself is somvr's G19. **P7 result:** 292/295 markers; `IconTexture` strings readable, `IconMovieClip` nearly unused, `ActionID` one value on all; the hide not tested. | re-ui-effects.md 2.3, 8.3; re-hud.md 3, 6 | `NAV` lines and `probe-out/nav-N.tsv` (record, shown bytes, position, IconMovieClip(PC), IconTexture, ActionID, RenderOffScreen, DisplayRange) vs the shots | `navdump` (P7) | R | M3 |
| U7 | Which prompts (action, StringId, IconId, category) are up for counter, drain, interact; where does `Combat.Debug.Prompt` print? **P7: open** (no prompt came up; no `[COMBAT|PROMPT]` output). | re-hud.md 4, 6 | `PROMPT` lines, `PROMPTS changed`, P8 `PROMPTS at a counter prompt at Talion`; `[COMBAT|PROMPT]` in the game's output, P8 `CONOUT` (the console's output). **P8:** the counter prompt is not a diamond entry (0 entries at 71 of 74 prompts at Talion; it is the RMB icon over the attacker); no debug or console output. | `prompts`, `prompts watch on`, `exec Combat.Debug.Prompt 1` (P7); `conlog on` (P8) | R | M2, M8 |
| U8 | Which menus load a 3D UI scene; its camera values. **P7: the main menu's** (pos, quat, fovY 12, fovX 21.17 from the UI aspect, near/far, DoF); the load screen open. | re-hud.md 2.2, 6 | `UISCENE` (manager, camera object), `UISCENEVIEW` (pos, quat, fovX/fovY, near/far, DoF) | `uiscene` at the main menu (P7) | R | M8 |
| U10 | The screen fade's bytes (`im+0x2ad8`) for XR-layer fades. **P7: open** (two pointers at that offset, no fade value). | re-hud.md 1.4, 6 | the `UI formats` line's fade bytes | `ui` (P7) | R | M2 |
| U4 | The values of `sp+0x130` (box blur) and `sp+0x144` (vignette), and who sets them | re-ui-effects.md 3.1, 8.4 | the scene-parameter bytes after `FUN_00a5a820` at low health and in wraith mode | (spike1's H1 hook logs `sp`; a `sp` word is a next-version item) | R | M2 |
| U5 | The pause-menu / player-UI state numbers. **Run 3: partly** (P1 results): front end, load screen, world, pause menu. | re-ui-effects.md 6, 8.7 | the state machine's numbers while the menus are opened | `state` (the `STATE` lines carry `ifs`, `menus`, `modal`, `video`) | R | M8 |
| U6 | Does a pushed or warped SDL mouse move the menu cursor (the laser pointer)? **Superseded** by re-ui-pointer.md's R1 (the CInterfaceMgr calls, no SDL): UP1-UP4 below. | re-ui-effects.md 5, 8.8; re-input.md 7 | - | `pointer`, `click` (P8) | A | M2 |

### (f) The XR pipeline (mod/somvr, not this probe)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| X1 | Does the Vulkan layer find `somvr_shared`, does the session run, does `xrLocateViews` on the game thread stay under 1 ms? | re-xr-pipeline.md 5.1 | the layer's and `somvr.so`'s own log lines | the `mod/somvr` library + its layer in a Monado/WiVRn run (**another helper's area**) | A | M1 |
| X2 | Present tags on the same thread as `vkQueuePresentKHR`; two H3 applies per frame; no reuse while standing still | re-xr-pipeline.md 5.2-5.3 | the records' `applied` bits and the tag counters | as X1 | A | M1 |
| X3 | Layout A or B: are both halves lit and post-processed; scale, horizon, swim | re-stereo.md 8; re-xr-pipeline.md 4.6, 5.4 | the sbs shot of spike1 run 2 (pending) | spike1 `trigger sbs` / `sbs0` (already scripted: `scripts/cmds/spike1-world.cmd`) | A | M0, M1 |
| X4 | Prediction quality, L/R exposure, recentre and snap | re-xr-pipeline.md 5.5-5.7 | as X1 | as X1 | A | M1 |

### (g) Console and movement (`re-m1m2.md` sections 2, 3 and 10)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **K1** | Does the console executor run in-process: does a cvar change, does the console find know the names, does `Teleport` move the root, where does `Faction.Dump` write an absolute path? **Run 3: yes** (P5 results), but `Faction.Dump` with a Unix path writes no file (Feral maps it to `C:/home/...`). **P7:** `@cout` = `C:\Temp`, which Feral's VFS mounts at `${AppPreferences}/VFS/Temp` (the mount table in `FUN_0065cd10`: `FUN_02019310("${AppPreferences}/VFS/Temp", "C:\Temp")` and `"${DozeTemp}"` = `C:\Feral\Temp`) = `RUN/home/.local/share/feral-interactive/ShadowOfMordor/VFS/Temp` under boot_check; the probe maps a `C:` argument back to its host file, checks it and copies it into `probe-out/`. **P7 result: yes:** `C:\Temp\`, `C:/Temp/` and `C:\Feral\Temp\` all wrote the 70787-byte dump into VFS/Temp. | re-m1m2.md 2, 10 (X1) | the `EXEC` lines (find, the variable's `+0x38` before/after, `rc`, `server`), the `ROOT` lines after `Teleport`, the `EXEC ... file 'C:\...' -> host '...'` line | `exec CMDLINE` (P5, P7) | A | M1 |
| **K2** | The root-motion slot: `PlayerMovement::vf_11` calls per frame and their thread; does the one-shot extra move (`pm+0x428`, pending `+0x444`) move Talion in his own frame, and does it stop at a wall? **Run 3: yes** (P5 results): 1 call per frame on the game thread; the move is in Talion's frame and slides along a wall. | re-m1m2.md 3, 10 (X2) | `PMCOUNT`, `FIRST P_PMROOT`, the `PMMOVE` and `ROOT` lines (`d` vs `expect`, `consumed by`), the wall shots | `pmcount SECS` (R), `pmmove DX DY DZ` (A) (P5) | R/A | M2 |

### (h) Render size and traversal (`re-resolution.md`, `re-traversal.md`)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **R1** | **The tall eye size:** `exec SetResolution 1832 1920` alone gives a 3D surface of `1832 x round(1832 * bbH / bbW)`; with the 3D desc hook (`RES_3D_DESC`, `0x741ceb`) passing the ScreenHeight cvar it is 1832x1920 while the UI stays 1920x1080 and the textures separate; is the 3D rect letterboxed (`Video.UseWidescreen`)? **P7: pass** (front end): 1832x1031 without, 1832x1920 with the hook, UI 1920x1080, separate textures, no letterbox, back to 1920x1080 clean. | re-resolution.md 1.4, 1.5, 4 | `RES` lines after each mode set: cvars, the device's back buffer/UI/3D sizes, the two textures, the 3D rect (`y0 != 0` = letterbox), rt3D's size; front-end shots | `res`, `restall on|off` (the probe's `P_RES3D` = `RES_3D_DESC`), `exec SetResolution` (P7, at the main menu) | A | M1 |
| T1 | The FSM path of each traversal move (8 falling, 9 transition, 10 landing), the proxy's support and gravity, the attach fields. **P7: smoke test only:** no fall at the teleport (fsm 6, supported, the whole time); 7 = combat on a stock strike; the `head_y` read is broken (**P8: fixed**: the head from `GetNode("Head")` when `nodes` has not run; `head_dy` = above the root). **P8:** `head_dy` 72-73 cm. | re-traversal.md 1.4-1.6, 5 (T1, T3) | `fsm=` in every `STATE` line; `TRAV` lines | `state` (fsm=), `trav SECS` (P7: the teleport's fall only; the full traversal drill is its own run) | R | M2, M6 |

### (i) P8 additions (`re-arms-ik.md`, `re-ui-pointer.md`, `re-traversal.md`, the fight)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **AG1** | **Does `SetCombatScriptedTarget` from our code make an orc fight Talion** (the precondition of CV1, C6, C7, U7, CV2 on an attacker)? **P8: yes** (3 of 3 within 1 s, attacks from 3.7 s on; archers too). | this doc P8 (the static evidence there) | `AGGRO ... (Talion)` (the AI's `+0xb60` = Talion's HOBJECT), then `AGGROWATCH ... combat target TALION` and attacks, `COUNTERPROMPT ... (THE PLAYER)`, `APPLYDAMAGE ... (THE PLAYER)` | `aggro [N]`, `aggro off`, `minhealth V` (P8) | A | M4 |
| IK1 | Which animation states run the engine's arm IK (`Modifier.IK`) on Talion and with which `TargetSocket`; where `IK_LHand`/`IK_RHand` sit against the hands. **P8: none in idle and melee**; the IK nodes stay at the root. | re-arms-ik.md 1.1, 1.2, 7 | `IKSETUP` (record, root/joint/target nodes, the target socket), `IKCHAIN` (state, weight, target in model space), `IKWATCH` (the four nodes, `|IK_RHand - R_Hand|`) | `ikwatch [SECS]` | R | M3 |
| W1 | The sword model's own skeleton: the blade axis and length, the grip node for `C_h`. **P8:** the blade along +Y, the tip 100.7 cm; the grip in the hand open. | re-arms-ik.md 4, 6.2, 7; design-vr.md 3 | `WNODE`/`WSOCKET` (local positions), `WATTACH` (the player's entry: node, offsets), the farthest node | `wnodes [SLOT]` | R | M3, M4 |
| **UP1** | Does R1 (the CInterfaceMgr calls from the input tick) hover and click the menus: the main menu (no player, no guard) and the pause menu (the pad guard in pad mode, off with `padguard off`)? **P8: hover yes (main and pause menu), click yes (the pause menu's QUIT, KBM mode); the pad guard not tested.** | re-ui-pointer.md 2, 3.1, 7 | `MOUSE ...` lines in order (sys skipped, view/gfx reached), the shots (highlight moved, OPTIONS opened), `MOUSE guard ... pad=1 -> 0` | `mouselog`, `pointer`, `click`, `padguard` | A | M2, M8 |
| UP2 | The mapping: `im+0x3210` vs the movie's own mouse, the view offset `GV+0x28` (expected 0), the corners. **P8: 1:1, offset 0**; the GFx stage is 1280x720. | re-ui-pointer.md 4, 9 | `MSTATE` lines at (0,0), (1919,1079), (u,v) = (0.5,0.5) | `mstate` | R | M2 |
| UP3 | Which states swallow the mouse; the system path's gates (window active, interface up) under the harness. **P8: open** (no harness mouse move while logged). | re-ui-pointer.md 1.1, 1.2, 9 | `MOUSE sys` (gates, states, pad mode) for the harness's xdotool moves, `MOUSE view` / `gfx` after ours | `mouselog` | R | M2 |
| UP4 | The element route: which component of the top layer is under the pointer. **P8: works** (main and pause menu: the right item hit). | re-ui-pointer.md 5 | `HITSCAN` lines (stage point, hit per component, the focused one) | `hitscan` | R | M8 |
| T6 | A mod ray gets the surface; `CanWallClimb` / `IsSlippery`. **P8:** the record reads, but the ray hits Talion first. | re-traversal.md 2.2, 2.6, 5 | `SURF` lines (hit, object, surface id, record, the two bools) | `surf [M]` | R | M6 |
| T7 | Gravity off holds Talion; the graph's reaction. **P8:** it holds, unnoticed; `grav on` latches at 0 (write `pc+0x60`). | re-traversal.md 2.4, 2.6, 5 | `TRAV` (fsm, gravity, supported, root y) with `grav off` + `pmmove 0 30 0`, then `grav on` | `grav off|on` | A | stretch |
| T8/T9 | Mounted: `MountInfo`, the beast, the rider's attachment and saddle node. **P8:** smoke test only (not mounted). | re-traversal.md 3.6, 5 | `MOUNT` lines | `mount [SECS]` (P8 at the tower top: not mounted, a smoke test) | R | M7 |

### (j) P9 additions (`re-combat-spawn.md`, `re-bow.md`)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **SP1** | Does the server's debug spawn work from our code (`FUN_029a4830`, a template found by archetype, on the ground ahead, facing Talion)? **P9: pass** (`Orc_Defender` and `Orc_Berserker`, valid at 1 and 3 s; level 0 unless given). | re-combat-spawn.md 1.3, 6 | `SPAWN`/`SPAWNCHECK` lines (a valid Combat/NPC pair, its record), the orc in the shot; `templates.tsv` (each template's archetype) | `templates`, `spawn WHAT [CM]`, `despawn` | A | M4 |
| SP2 | A Nemesis captain from a Faction slot: `Faction.SetTarget`'s argument order; the member bound to the NPC. **P9: pass**, row then column (row 1, column 3 = Ghâm the Hook, level 10; his dump entry changed). | re-combat-spawn.md 1.4, 6 | `SPAWN ... captain` (member, level, template); the name in the shot vs `Faction.Dump` before/after | `exec Faction.SetTarget R C`, `spawncaptain` | A | M4 |
| **AB1** | Does a stock strike at an orc run the AIBlockTable roll (gate, record, roll, chance), and does a won roll notify the orc? **P9: yes where a table exists** (`Infantry`: 0.10 for the light strikes; a won roll notifies, the goal turns relevant); `AI_Defender` and `AI_Berserker` have no table (no roll). | re-combat-spawn.md 2.1, 2.5 | `AIBLOCK gate/reached/roll/notify` per strike; `BLOCKTBL` (the table, the entry the strike's categories match) | `aiblockwatch`, `blocktable [N] [ATTACK]`, `cmd CombatPrimary01` | R | M4 |
| **AB2** | Does a notification make the orc block (defender) or dodge (berserker)? **P9: no** for an infantry (`block_valid`/`dodge_valid` 0, the strike hit); no roll to force on the defender and the berserker, but AB3's direct notification made the defender block. | re-combat-spawn.md 2.2 | after `blockroll force`: `AIBLOCK goal_counter relevant ... -> 1`, `AI block_go`/`dodge_go`, a `*_Block_*`/`*_Dodge_*` start, `HITEV block=1` | `blockroll force` / `off`, `aiwatch` | A | M4 |
| **AB3** | Does our own `FUN_029ac890` (no attack: the VR swing's step 1b) do the same? **P9: the defender blocks** (1 of 3 calls: the one out of its own attack, `block_go` -> `AI_Defender_Block_Into`); the berserker's dodge not shown (`dodge_valid` 0, Talion recoiling). | re-combat-spawn.md 2.6 | `INCOMING ... replan 0 -> 1`, then the AB2 lines before the `dealhit` | `incoming [roll]` | A | M4 |
| SH1 | Does a shield defender block a frontal `dealhit` at damage time, with no AI? **P9: pass** (4 of 4 `dealhit`s and 9 of 9 stock strikes: `block=1`, damage 0). | re-combat-spawn.md 2.3 | `AIBLOCK shield` (HP, attack) and `HITEV block=1` | `dealhit` on the spawned `AI_Defender`, `aiblockwatch` | A | M4 |
| AR1 | The arena: no armed AI within 30 m; the TOD and weather names; `AdvanceTimeOfDay`'s step. **P9: partly**: one step = Day -> Night; 79 weather names; 4 infantry 31-45 m away joined; `CanBeAttacked 0` stopped no attack. | re-combat-spawn.md 3, 6 | `orcs` after the teleport; `TOD` before/after; `WEATHER` names | `tod`, `weathers`, `exec GameServerShell.AdvanceTimeOfDay` | R/A | M4 |
| **BW1** | Which command enters which player weapon mode (aim, vision, Shadow Strike, brand, drain); held or toggled; the Focus gates. **P9: pass**: aim `CombatSecondary03` 124 (held 0.2 s), stealth `CombatSecondary04` 125, vision `CombatSecondary01` 122 (toggle), slow-mo 176; the fire is an attack input, not in this data. | re-bow.md 1.1, 4, 6 | `WMODE` lines (the command via `FUN_00e24570`, HeldDuration, IsToggle, MinEnergy) and `wmodes-N.tsv`; `WMSETUP` | `wmdump` | R | M5 |
| BW3 | Does turning the free-aim ray (`BOW_FA_CAMXF`) steer the aim point, `P+0x214` and the stock arrow? **P9: not run** (`wmhold` picked `PC_Stealth`; the step ran inside the captain's showdown, all presses `active=0`). **P10:** `wmhold PC_WraithRanged` enters the aim (the fix works); `aimray` not run. | re-bow.md 1.4, 6 | `AIMRAY` (calls turned, FA aim point/ray/target, `P+0x214`) and `PROJ new` of the stock shot | `wmhold`, `aimray YAW PITCH [hand]`, `projlog` | A | M5 |
| BW7 | Does the aim turn Talion's body yaw? **P9: not run**; the aim mode's FreeAim data has `EnableAimAtRotateObject` 1. | re-bow.md 6, 7 | Talion's yaw in the `AIMRAY` lines while aiming | `aimray` | R | M5 |

### (k) P10 additions (`design-locomotion.md` 2.10)

| ID | Question | Source | What decides it | Probe | Risk | M |
|---|---|---|---|---|---|---|
| **LR1** | The stock speeds: walk, jog, run, sprint (`FreeRun`); the stick-to-speed curve; the time to 90 % and the stop per band. **P10: partly**: one `Run` band at every stick 0.2-1.0 (222-232 cm/s, 90 % after 0.82 s without a turn clip, stopped after 0.6 s / ~57 cm), `FreeRun` 636-641 cm/s (90 % after 0.58 s); no walk band (KBM mode, `InCombat` all run). | design-locomotion.md 2.1, 2.5 | `SPEED ... end` per band (plateau, 90 % time, stop time and distance), `probe-out/speed-N.tsv` | `speed`, `walk` (the stick along a free heading) | R/A | M2 |
| **LR2** | The console names of the 30 `Client.PlayerMovement` attributes; the player record's values (gravity -20, fall heights, step height, `BaseSpeedMultiplier`); does `BaseSpeedMultiplier` change anything. **P10: pass**: 30 values (gravity -2000 cm/s^2 = `pc+0x58` -20, StepHeight 40, FallMinHeight 100, FallDamageMin/Max 0); no console name by any of the three forms; `pmrec base 1.5` changed nothing (no reader). | 2.2, 2.3 | `PMREC` lines (value, read path, console name), the caches line (`pc+0x58`); a band again after `pmrec set`/`pmrec base` | `pmrec`, `pmrec set ATTR V`/`restore`, `pmrec base V`/`off` | R/A | M2 |
| **LR3** | `LOCO_ROOTDELTA` replaced: the root's speed = asked from the first tick, a stop in one tick, walls, slopes; the FSM staying 6; what the legs play; the turn clips. **P10: pass**: the root at 0.996-1.012 of the asked 200-900 cm/s from tick 1, the stop in one tick, no locks, `dt` from the object timer; the turn clips still play with the stick behind him. | 2.6 | `LOCO` lines (replaced, skips, locks, first tick at 90 %, the stop), TAGS in `SPEED`, `speed-N.tsv` | `loco const`, `loco stick` | A | M2 |
| **LR4** | Which graph value picks the clip family: the stick magnitude or `Game_Velocity`. **P10: the stick** (and `FreeRun`): idle legs at 500/900 cm/s with the stick at rest, `Run` at 0.3 and 1.0, `Run Spr` with `FreeRun`; `Game_Velocity` is not a graph variable. | 2.5 | TAGS and `RawMoveMag`/`Game_Velocity` in `speed-N.tsv` with `loco const 0 500` at the stick 0, 0.3, 1.0 and with `FreeRun` | `loco const` + `walk` | A | M2 |
| **LR5** | Launches: the arc in momentum mode, the FSM path (6 -> 8 -> 10), the landing's root motion. **P10: no liftoff**: `vy` zeroed in the first step while supported; the horizontal 4 m/s glided 3.7 m in momentum mode. | 2.7 | `FLING` lines and `fling-N.tsv` (apex, flight time, FSM and velmode paths, who put root motion back) | `fling` | A | M6 |
| **LR6** | The stock vault over an orc: a sync or not, its record, duration, root path, the orc's reaction, the 180 deg re-face. **P10: not answered**: the evade (orc 3.2 m, Talion in a recoil) was `PC_Dash`, 5.9 m in 0.95 s. | 3.8 | `VAULT ... episode` (names, duration, path, yaw change, the partner's yaw and attacks), `vault-N.tsv` | `vaultlog` + `cmd CombatPrimary04` | R | M4 |
| **LR7** | Does Talion's proxy collide with orcs? **P10: no stop**: Talion slid around the orc (closest 56.5 cm, 0.81 of the asked speed) and passed; the orc moved 33 cm. | 3.8 | `NPCPUSH ... result` (the closest approach; passed or stopped) | `npcpush` | A | M6 |
| **LR8** | A tracker rate on Talion: does the root motion scale with it? **P10: not answered**: a wall stopped the root as the rate was set; the game's delta x0.8 for 0.3 s. | 2.7 | `ANIMRATE ... end` (the root's speed during vs before, the game's rate writes) | `animrate` | A | M2 |
| **LR9** | Does the graph keep free-run parkour (auto-vault, climb) while we own the velocity under `FreeRun`? **P10: not answered**: both wall runs inside the patrol's melee, no FSM 9. | 3.5 | `TRAV`/`VAULT` (FSM 9) and `LOCO ... skipped fsm` in the wall run with `loco stick`, against the stock wall run | `loco stick` + `cmd FreeRun Jump` | A | M6 |
| **LR10** | Shadow Strike's path: a sync with root motion, or a teleport? **P10: not answered**: the aim came up by the exact name (the fix works), the camera at a rock: no target, no strike. | 3.9 | `VAULT ... episode` during the strike (path vs largest step) | `vaultlog` + `wmhold PC_WraithRanged` + `cmd CombatKBM07` | A | M5 |

## 2. The probe runs

Each run is one boot of at most 15 minutes of game time (`--hold 900`), which leaves the slot's remaining minutes for
the harness's start-up, the save checks and the summary. The first ~150 s of every run go to the pre-game window, the
alert and the intro (NOTES.md, attempt 3), and a save load costs another ~60-90 s, so a run has about 8-10 minutes of
world time. The runs are ordered by what they unblock.

### P1 - input injection and the pose/piece/binding dumps (critical)

**Why first:** I1 decides whether anything else can be tested without a human. With in-process input the later runs
can play scripted scenarios (walk, fight, menus) on their own; without it every probe needs the coordinator to type
keys. A1/A2 and P1q/P2q are the cheapest answers in the same world visit, and I2's TSV is needed by every active
word afterwards.

- Commands: `scripts/cmds/probe-P1.cmd` (menus by keys, marks and shots).
- Words (`SOMVR_PROBE_AUTO`), one `--game-env` value:
  `start state on | menu+1 bindings;ui | world+2 mark world;bindings;pieces;nodes;orcs;attacks | world+8 padmode on;bindings | world+11 mark input;cmd Jump ms=120 | world+14 mark pose-move;pose 180;move 0 1 3000 | world+19 orcs | world+22 move 0 0 1;look 0.4 0 800 | world+26 pad A 150 | world+29 pad LY=0.9 2000 | world+33 pad X 150 | world+36 release;nodes;mark input-done`
  (`pose 180` runs *while* the move word drives the player: a still Talion cannot tell a stale pose from
  no motion. The second `nodes` at the end re-reads the skeleton after the fight/move, which shows whether
  the name pointers stay valid.)
- Pass: `VIN press`/`VIN release` lines with `polled=` > 0 (the game read our value), the player's position changing
  in the `move` lines, `POSE-SUMMARY` with 180 frames, `nodes`/`pieces`/`bindings` TSVs written, `STATE` lines for
  menu -> world.
- Fail (and what it means): `polled=0` everywhere -> the game does not read the command records we hold, so the
  injection point is wrong and V3 (`CSDLController::vf_07` on a real pad) is the next option; `cmd: refused` -> the
  save guard is missing (section 4).
- Answers: I1, I2, I3, I4, I5, I6, A1, A2, A3, A10, P1q, U5, partly U1/U2 (menu only).
- The `nodes` TSV of this run is what P3 and the arm-IK work (A5) start from: the whole skeleton with names, the
  arm chains and the head bone.

#### Results, run 3 (2026-10-19 19:04, `logs/boot-check-20261019-190441`)

Not the full P1. Only the read-only dump words, sent live in the world at a forge tower top (seq 2:
`mark p1;bindings;pieces;nodes;orcs;attacks`), with `state on` from the start; no `cmd`, `pad`, `look`, `padmode`,
`pose` or `ui`. The one input word was P5's `move 0 1 4000`. Talion's frame from the root quaternion
(0, 0.5, 0, -0.866): forward (-0.866, 0, 0.5), right (0.5, 0, 0.866); world units are cm.

- **bindings** (`probe-out/bindings-2.tsv`, KBM mode `padmode=0`): 55 of 349 commands bound (39 with a pad input,
  39 with a key/mouse input; keys are Windows VK codes, `mouse:0/1/2` = left/right/middle button, `mouse:5/6` = the
  X/Y axes). 86 active in the world set: the 55 bound ones plus 31 unbound (12 PitchAxis, 13 YawAxis, 243
  VisionModeToggle, 147-150 CombatKBM12-15, 171-180 CombatHidden01-10, 271-276 Friend*, 277-280 EquipSword1-4, ...);
  263 inactive; the swallow byte `+0x29` is 0 for all 349; 184 have no record.
  - Move and camera: 1 Forward W, 2 Reverse S, 4 StrafeLeft A, 5 StrafeRight D, 3 ForwardAxis pad:LY, 6 StrafeAxis
    pad:LX; 22 PitchAccel mouse:6 / pad:RY (scale 0.005 / -1), 23 YawAccel mouse:5 / pad:RX (0.005 / 1); 16 Jump
    Space / A, 18 FreeRun A, 15 Crouch RB, 34 Activate LCtrl / RB, 14 Menu Esc / START, 95 PDA M / BACK.
  - Pad combat: 118 CombatPrimary01 X (+ LMB), 119 CombatPrimary02 Y (no key), 120 CombatPrimary03 B (E), 121
    CombatPrimary04 A (Space); 122-125 CombatSecondary01-04 LB (V), RB (LCtrl), LT (RMB), RT (LShift); 126/127
    CombatTertiary01/02 L3, R3 (Space); 128-131 CombatTertiary03-06 the d-pad (I, K, U, Right); 132-135 the two
    sticks; 160 SpecialAttackModifier RT; SpecialAttack1-4 unbound. With the stock pad layout (X strike, Y counter,
    B stun, A evade) CombatPrimary01-04 = strike, counter, stun, evade (from the layout; `cmd` confirms it).
  - KBM combat: 136-146 CombatKBM01-11 = C, T, R, F, LMB, X, Z, Q, RMB, 2, G (+ MMB); CombatKBM12-20 unbound.
- **pieces**: 17 pieces on the player object, but slot 14 names only two: 0
  `models\player\player_talion\player_talion_pd_Talion_Hair_Bones.` (cut by the probe's 64-byte buffer, most likely
  `.hkt`) and 1 `...\player_talion_pd_ragged_cloak.hkt` (Havok files: hair and cloak); 2-16 are empty strings. The
  head, hood and hair meshes cannot be found by name, and `hidehead`'s default substrings match only piece 0. P3
  needs a hide by piece index (a probe change) with a shot per piece.
- **nodes** (`probe-out/nodes-2.tsv`): 168 nodes and 54 sockets; 168 names readable, 168 stored hashes =
  `somhash(name)`; GetNode finds `Head`, `head` and `HEAD` alike (index 28). Only `SocketHead` of the socket names is
  known (the TSV has the other 53 hashes).
  - Head: node 28 `Head` (parent 24 `Neck`, then 23 `Spine4_joint`) = the parent of socket 2 `SocketHead` (offset
    (-1.98, 0, 0.01), 2 cm above the node); 72.5 cm above the root. The root is at pelvis height: `World_Ground`
    (node 10) and the foot sockets are 104 cm below it.
  - Eyes: 32 `L_Eye_Def` and 40 `R_Eye_Def` (children of `Head`, sockets 0 and 1 sit on them), 6.6 cm apart; their
    midpoint is 11.0 cm forward, 4.1 cm up and 0.7 cm left of `Head`, 180.6 cm above `World_Ground`.
  - The gameplay camera (`cam+0x44c`) is 165 cm behind, 36 cm right of and 5 cm below `Head`.
  - Arms: 25 `L_Shoulder` -> 86 `L_Armu` -> 88 `L_Arml` -> 90 `L_Hand` (fingers 92-107, 97 `L_Weapon`; twist nodes 89
    `L_Bicep`, 91 `L_Forearm`); right 26 -> 108 -> 110 -> 112 `R_Hand` (119 `R_Weapon` with sockets 14-16, 111
    `R_Bicep`, 113 `R_Forearm`). Sheaths: 27 `Scabbard_Joined` (on `Spine4_joint`) -> 131 `scabbard_back`, 130
    `dagger_sheath_back`.
  - Root children: 2 `aimer` (74 cm up, at head height), 3 `attach`, **4/5 `IK_LHand`/`IK_RHand`**, 6/7
    `IK_LFoot`/`IK_RFoot` (all four exactly at the root while standing), 8 `centerofmass`, 9 `Combat_Aimer`, 10
    `World_Ground`, 11 `Camera`, 12 `Aim_Camera`, 13 `FaceCam`. The hand IK targets are a candidate input for the
    arm IK (A5/A7); who writes them is not known.
  - The second `nodes` (after moving) was not run: the name pointers over time are not checked.
- **orcs**: 29 live Combat candidates (48 rejected by the owner-vtable check); the nearest 20 at 53-100 m:
  `AI_Slave_Human`, `AI_Infantry`, `Warg_Brown`, all `parry=0 hitwin=0`, their "attack" an ambient loop
  (`AI_Bip_Campfire_Loop_WarmHands_1`, `AI_Prisoner_Pickaxe_Loop`, ...). Targeting bands 77 entries, hooks 5 live.
  The player's server `Combat` 0x3744a510 and `PlayerCombat` 0x7f2a3857edd0 were found.
- **attacks** (`probe-out/attacks.tsv`: index, rec, hash, name): 10814 `Combat/Attack` records, no duplicate names:
  `AI_` 4806, `PC_` 3477, `DLC2_` 496, `DLC_` 350, `PL_` 155, `DLC1_` 153, `Act1_` 144, `FearCam_` 116, ... The
  player's sword strikes are most likely the 499 `PC_HC_<Lowest|Low|Low2|Medium|High>_L1_D<0-3>_<F|B|L|R>_<swings>`
  behind the 99 `PC_PoolAttacks_HC_*` / `PC_PoolAttacks_KB_EL1_*` pools (names only). The `ATK_*` hooks never fired
  (Talion did not attack): which records the player starts is open (P2).
- From the hooks and `STATE`:
  - Threads (I5): every `FIRST` line is on tid 2974062 `WinMain`. First world frame: `P_PMROOT` -> `P_VALUE2` ->
    `P_ANIM_B` -> `P_CAMPOST_B` within 2.1 ms. Per frame (60 Hz): INPUT, SERVER, VIS, PMROOT, ANIM_B and CAMPOST_B
    1, UI105 3, VALUE 22, VALUE2 4, ISON about 69.
  - **The live player update is `FUN_00a40fe0`** (variant B, sites `0xa41363`/`0xa413c5`): ANIM_B = PMROOT = 13285
    calls over the run, while `FUN_00a416a0`'s sites (`P_ANIM_A`, `P_CAMPOST_A`, the update re-firstperson.md 1.3
    calls the main one) stayed at 0. Every-frame hooks (F1, the IK post) must cover B.
  - Ambient AI loops are `Combat/Attack` records too and fire `APPLYDAMAGE`, `HITEV` and `GAMEHIT` (e.g. HITEV
    `AI_Prisoner_Hostage_Kneel_Loop_2` damage 88.00; slave-abuse kicks through `Combat_DealHit`): C6 must filter on
    the player as target.
  - U5: `ifs` -9 before init, 1 front end, 3 world, 4 leaving the world; `menus` 1 and 3 in the front end, 5, 8, 11,
    15 while the load builds, 0 in the world; load screen `paused=1 modal=1`; pause menu (Escape) `paused=1 modal=0
    menus=0`; `video=1` in the front-end videos. I4: `allow=1/1`, `padmode=0` throughout.
  - U1 (front end only): 20 `UI105` lines, all `this=0x461df60 target=0 mask=0xffffffff ra=0xa124c9`, game thread.
  - I1/I3: `move 0 1 4000`: `VIN press` ForwardAxis 1, StrafeAxis 0; `VIN release ... polled=1446 ticks=241` (6
    reads per tick); Talion ran 616 cm along his forward ((35496.3, -22386.3) -> (34960.5, -22081.8)), state tags
    `Run` while held, `Idl` after. A held analog value from our code drives the player.
- Pass list: `bindings`/`nodes` TSVs written (`pieces` logs only), `STATE` menu -> world yes, `VIN` with
  `polled>0` yes (move only), `POSE-SUMMARY` not run.
- **Still for the full P1:** `cmd Jump`, `pad A/X/LY`, `look` (I1 buttons and camera, I6 with a fired command),
  `padmode on;bindings` (I2 pad mode, I4), `pose 180` while moving (A1, A10), the second `nodes`, the in-frame
  order of device update, player update and targeting (I5), `ui` in the world (U1, U2).

### P2 - combat: our own hits, telegraphs, deaths

**Why second:** C2 is the heart of M4; everything else in combat follows from the attack list and the hit pipeline.

- Commands: `scripts/cmds/probe-P2.cmd` (to the world, then stand still; the fight is driven by the words).
- Words: `start state on | world+2 attacks;orcs | world+6 mark walk;move 0 1 3000 | world+12 orcs | world+14 mark dealhit;dealhit auto | world+18 orcs;dealhit auto | world+22 dealhit auto;dealhit auto | world+28 mark kill;dealhit auto kill | world+34 orcs | world+40 mark counter;autocounter CombatPrimary02 3 | world+90 autocounter off;orcs`
- Pass: `DEALHIT` followed by `HITEV` with a damage value, the orc staggering in the shots, `DEATH` with
  `killer_is_player=1` after the `kill` hit, `COUNTERPROMPT`/`APPLYDAMAGE` pairs with `since_bpw` in the 0.2-0.6 s
  range, `AUTOCOUNTER` lines.
- Answers: C1, C2, C3, C4, C5, C6, C7, C9.
- Note: run it on the dev save, never on a captain the user cares about (a kill is permanent in the Nemesis system).

### P3 - first person: head off, camera at the head, near plane, shadows

- Words: `start state on | world+2 nodes;pieces | world+5 mark shadow-before |
  world+8 hidehead | world+11 mark shadow-after | world+14 near 5 | world+17 mark near5 |
  world+20 near off;showhead | world+24 mark fp;fpcam on fwd=10 up=4 | world+30 move 0 1 2500 |
  world+36 look 0.5 0 1000 | world+42 fpcam off;mark fp-done`
- Shots every 3 s from the harness (`--auto` is not used; the cmd file takes them at the marks).
- Pass: the head pieces gone in the shot but the **shadow** still complete (or not: that is the answer to P2q), the
  near plane clipping the hood, the view sitting at the head with the body visible below, `FPCAM` lines with a level
  horizon, and the game's own camera logic not fighting it (no `STATE` flapping).
- Answers: P2q, P3q, A4, A10; confirms A1's choice of anchor and measures the eye offset from the head bone (A2,
  re-animation.md 9.9).

### P4 - UI surface and the HUD panel

- Needs the prefs seeded with a UI size different from the 3D size (`InterfaceWidth/Height` != `ScreenWidth/Height`;
  once P5 has shown `exec` working, `exec SetResolution W H IW IH` can replace the seeded prefs: `re-m1m2.md` 2.3), so
  the harness runs with
  `--seed-prefs` of a prefs file carrying e.g. 1920x1080 UI and 2560x1440 3D.
- Words: `start state on | menu+2 ui | world+3 ui | world+8 mark hud | world+12 ui`
- Pass: `UI` lines showing two different sizes and two different texture pointers, `UI105` lines naming the caller of
  the Scaleform display with its target and mask.
- Answers: U1, U2 (and the open question in re-ui-effects.md 1.3).

### P5 - console and movement (the M1/M2 gates of `re-m1m2.md`: X1 and X2)

**Why now:** the console executor (K1) is what lets later runs script themselves (`Teleport` to a test place,
`SlowMO.*`, `SetResolution` for P4, `Faction.Dump` for C2), and the root-motion slot (K2) decides how the room-scale
body follow of M2 writes Talion's position (`re-m1m2.md` 3.3). Both are cheap: one world visit, no fight.

- Commands: `scripts/cmds/probe-P5.cmd` (to the world, stand still; then the wall phase by `probe` lines).
- Words (`SOMVR_PROBE_AUTO`): `start state on | world+2 mark console;exec Combat.Debug.Attack 1 |
  world+5 exec Combat.Debug.Attack 0\; Teleport @x+100 @y @z | world+9 exec Faction.Dump @out/somvr-faction-P5.txt |
  world+12 mark pm;pmcount 5 | world+19 pmmove 10 0 0 | world+22 pmmove 0 0 10 | world+25 move 0 1 2000;pmcount 2 |
  world+29 mark pm-done`
  (the `\;` puts both commands into one executor string, so the escape and the multi-command syntax are tested
  once; `@x+100` is the root's world x + 100 cm, i.e. `Teleport` to the current position + 1 m; `pmmove 10 0 0` is
  10 cm to Talion's right, `pmmove 0 0 10` X2's 10 cm forward; the second `pmcount` runs while the walk animation
  drives the root motion.)
- The wall phase (the cmd file, `probe` lines): walk into whatever is ahead (`probe move 0 1 4000`), a shot, then
  `probe pmmove 0 0 30` twice with a shot after each. If shot 15 shows no wall in front of Talion, turn and walk
  again by appending `probe look 0.5 0 800` / `probe move 0 1 4000` live.
- Pass:
  - `CONSOLE G22_* ok` at start; `EXEC ... find 'Combat.Debug.Attack' -> ... variable?` with `u8=0` before and `u8=1`
    after (the cvar's value at `+0x38`, `0x4c44ad8` in `reference/work-gameplay/combat_cvars.tsv`); `find 'Teleport'
    -> ... command` (the repository knows it: `repo->vf_08 != 0`); `rc=0`.
  - after the `Teleport`: the `ROOT seq=N exec` lines with `d=(100 0 0)` (+- the game's snapping to the ground), the
    bool back to `u8=0` from the same string.
  - `EXEC ... file '<RUN>/probe-out/somvr-faction-P5.txt' exists` (an absolute path works). If it says `absent`, look
    (read-only) for `somvr-faction-P5*` under RUN, the Feral user folder and the prefix: that is where the VFS puts
    absolute paths. No relative path on purpose: it could write into the install.
  - `PMCOUNT` with `per_tick` near 1 (hist mostly `1:`), `pm=` the same as `Player+0x2c8`, `offthread=0`, and the
    `FIRST P_PMROOT` line's tid = `FIRST P_INPUT`'s (the game thread); `per_tick` 2+ = several solver steps per frame.
  - `PMMOVE ... consumed by vf_11 call +1` and `ROOT ... d` = `expect` (10 cm in Talion's frame) on the tick after;
    at the wall `d` stops short of `expect` (the move collides) or not (then the body follow must clip `d` with a
    G21 ray first, `re-m1m2.md` 3.3).
- Fail: `exec: the engine ... has vtable` / `G22_RUNSTRING: bytes ... differ` -> not this build or not the engine
  object (nothing was called); `find ... UNKNOWN` for `Teleport` -> the repository lookup is wrong, not the
  command; `server=0` -> the world was not up yet (server commands do nothing then); `PMMOVE ... not consumed` with
  `pending=1` -> vf_11 is not running for the player (another movement state), with `pending=0` -> it runs through
  another path than the hooked slot; `pmmove: ... not at rest` -> the offsets do not match (nothing was written).
- Answers: K1, K2 (`re-m1m2.md` section 10 X1, X2); unblocks the `exec` lines of P4 and C2.
- Note: on the dev save; `Combat.Debug.Attack` is set back by the second exec, the `Teleport` and the `pmmove`s
  move Talion (no save happens in the run: spike1's guard).

#### Results, run 3 (2026-10-19 19:04, `logs/boot-check-20261019-190441`)

The words were sent live (seq 3-13, after P1's dumps) at a forge tower top. The second `pmcount` ran after the walk,
not during it; the `RATE` window of the walk still shows PMROOT 300 per 5 s.

- **K1: yes.** `CONSOLE G22_* ok`. `find 'Combat.Debug.Attack' -> 0x4c44aa0 variable` (`+0x38` = `0x4c44ad8`):
  `u8` 0 -> 1 (seq 3) -> 0 (seq 4). `find 'Teleport' -> 0x46b24a0 command handler=0xa33620`. The `\;` string ran 2
  commands, both known. `Teleport 35500.00 5402.52 -22400.00`: `ROOT tick+1 d=(100.00 0.65 0.00) |d|=100.00` (y = the
  ground snap), the same at tick+2/+3. Every call `rc=0 server=1` on the game thread, 0.002-0.882 ms, cwd = the
  install's `bin/`.
- **`Faction.Dump`: no file.** `find 'Faction.Dump' -> 0x4b58420 command handler=0x22a60f0`, `rc=0`, the file absent
  right after the call. After the run (read-only): no `somvr-faction-P5*` under RUN, `/home/bara` or `/tmp`;
  `~/.local/share/feral-interactive` absent (as it should be); nothing new in the install but the CEF leftovers
  (`install-newer.txt`). Static reason:
  - `0x22a60f0` is a client thunk (`JMP 0x2aeea40`, no server gate) to `FUN_022a6ed0`: argc must be 1 and the path
    non-empty, then it collects the faction members (`*0x46b7310` vtbl+0x78, `FUN_023d6220`) and `FUN_00fbc460`
    writes them at once. Synchronous, no captain target, no console output. `FUN_00fbc460` builds the text in a
    stringstream, opens the file with `FUN_01b86d70(&f, path, "w+t")` (a `fopen_s`: `FUN_01b86cc0` ->
    `FUN_01b86950`, Feral's `_wfopen`) and returns 0 without a message when the open fails. An empty faction list
    would still create the file, so the open failed.
  - The path goes through Feral's Windows layer: `FUN_01af7380` -> `FUN_01af72a0` puts `C:` in front of a path that
    starts with `/` or `\` and has no drive letter (test `FUN_01af6ae0`, string `0x3812114` "C:"), so the probe's
    path became `C:/home/bara/Projects/...` on Feral's virtual C: drive (`CDozeDrive`), resolved against the VFS
    root `*0x4b4d4a8` (`FUN_0200f0b0`) [med]. The drive's mounts (`reference/som-linux.strings.txt` lines
    138550-138582: `C:\Feral\Temp`, `C:\Temp`, `C:\Feral\Users\Default`, `C:\Feral\Program Files`, ... next to
    `${AppPreferences}/VFS/Temp`, `.../VFS/User`, `VFS/Local`) do not include `C:\home`, so its folders do not exist and
    the open fails. A relative path is joined to the Doze current directory `*0x4707938` (most likely the install):
    never use one.
  - Next run: `exec Faction.Dump C:\Temp\somvr-faction-a.txt\; Faction.Dump C:\Feral\Temp\somvr-faction-b.txt`
    and look for `somvr-faction-*` under `RUN/home/.local/share/feral-interactive/ShadowOfMordor/VFS` (where
    `VFS/Temp/feralArguments.cfg` lands) [med: the mount pairs come from strings]. The probe's `file ... exists`
    check only looks at `/` paths.
- **K2: yes.** `PMCOUNT` 5.0 s: ticks=301 vf11_calls=301 `per_tick=1.000` (hist `1:301`), `pm=0x7f2a3857e440` =
  `Player+0x2c8`, `pm_changes=0`, `offthread=0`; 2.0 s after the walk: 121 of 121. `FIRST P_PMROOT` tid 2974062 =
  `FIRST P_INPUT`'s. Over the whole run PMROOT = ANIM_B = 13285: one vf_11 per player update.
  - `pmmove 10 0 0` (right): expect (5.00 0.00 8.66), `ROOT tick+1 d=(5.00 -0.51 8.66) |d|=10.01`, `consumed by
    vf_11 call +1`, pending back to 0. `pmmove 0 0 10` (forward): expect (-8.66 0 5.00), d=(-8.66 -0.55 5.00). x and
    z exact in Talion's frame; the y -0.5 is the ground snap.
  - At the wall (after `move 0 1 4000`): the first `pmmove 0 0 30` gave d=(-25.98 0.00 15.00) = expect (no contact
    yet); the second d=(-9.53 0.00 15.00), |d|=17.77 of 30, while vf_11's own transform still had the full
    (-25.98 0 15.00). The collision comes after vf_11 and slides the move along the wall (the part into it removed).
    So the body follow can write `+0x428` without its own G21 wall ray; ledges and drops are not tested.
- Unblocked: `exec` for P4 (`SetResolution`) and for scripted test places (`Teleport`, `SlowMO.*`); C2's Nemesis
  check waits for a `Faction.Dump` path that works.

### P6 - the XR pipeline (mod/somvr + the Vulkan layer, Monado first)

Not this probe's library: `mod/somvr` and its layer, with the probe loaded alongside for the `state` log and shots.
The questions are X1-X4 and re-stereo.md 8's layout decision. It must come after P1 (so a scripted scenario can hold
the head still while the layer is measured) but can run earlier if the slot is long enough for two boots.

### P7 - the AI's view of a swing, hit-stop, key times, combat items, HUD data, the render size

**Why now:** tonight's static RE (`re-combat-vr.md`, `re-combat-items.md`, `re-hud.md`, `re-resolution.md`,
`re-traversal.md`) left exactly these questions for a boot, and all of them fit one world visit: the combat ones
(CV1-CV3) decide how the VR swing tells the AI about itself, how a heavy hit freezes an orc and whether the telegraph
can be read ahead; the items (CI1-CI8) decide the blade's block/deflect, glow, dagger and sounds; the HUD data (U2, U3,
U7, U8, U10) and R1 are what H11/G19 and the tall eye need next.

- Commands: `scripts/cmds/probe-P7.cmd` (run 4's menu, load and quit steps; everything else is `probe` lines with
  shots, so the run needs no live typing).
- Launch (spike1 for the save guard, the probe next to it; the user's save copy):
  ```
  cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
    --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
    --game-env SOMVR_COMFORT=1 \
    --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p7-world;orcs;ui;res' \
    --cmds scripts/cmds/probe-P7.cmd --hold 900
  ```
- The place: the save loads at a forge tower top with the nearest orcs 53-100 m away (run 3). **Plan: `pick` + `exec
  Teleport @ox+400 @oy+100 @oz`** - `pick` takes the nearest live AI character that is not a slave, human or animal
  (run 3's `AI_Infantry` at the camp below), `@ox/@oy/@oz` expand to its position, so Talion lands 4 m from it, 1 m up
  (the ground snap, run 3). No fixed camp coordinates are needed (none are known), and the same words work anywhere.
  If `pick` finds nobody, the `exec` fails with `@ox: no picked orc` and the combat part logs empty: the dumps of the
  first part still count.
- Order (about 7.5 minutes: ~165 s to the world, ~200 s in it, ~70 s for the quit and the resolution test):
  1. Main menu: `uiscene;ui;res` (U8, U1/U2 front end, R1 baseline).
  2. World, tower top (AUTO `world+3`, then the cmd file 20 s later): `timers;weapons;navdump;prompts;syncinfo
     PC_Bip_StealthTorment_B PC_Bip_StealthTorment_F PC_Bip_SneakInstaKill_Loud_B PC_Bip_Sync_Cntr_F
     PC_Bip_StealthTorment_*`, `attacks full`; `exec Faction.Dump @cout\somvr-faction-P7a.txt`, then
     `C:/Temp/...P7b` and `C:\Feral\Temp\...P7c` in one string (which spelling the VFS takes); the loggers on
     (`stimlog aiwatch keytimes projlog fxlog soundlog`), `sound ui_game_back`; `dagger hand` / `dagger back` with
     shots; the piece sweep `piecehide 0` .. `piecehide 16` with a shot each, `piecehide off`.
  3. `pick;trav 15;exec Teleport @ox+400 @oy+100 @oz`, then `orcs;timers;prompts watch on;exec Combat.Debug.Prompt 1`
     and 30 s of the orcs attacking a still Talion (KEYTIMES/KEYHIT, their stimuli, AI lines, prompts).
  4. Stock attacks: `cmd CombatPrimary01` x3 (the player's stimuli, the AI's block/dodge); swings:
     `swingevent auto` + `dealhit auto` x2, `swingevent auto 400 cur` + `dealhit auto` x2; `autocounter
     CombatPrimary02 3` from here on (Talion has to survive).
  5. Hit-stop: `dealhit auto;hitstop 120` (the design value), `dealhit auto;hitstop 600` with a shot inside the hold,
     `hitstop 1500 rate` (recipe B).
  6. FX: `weaponfx last`, `weaponfx last loop`, `weaponfx stop` with shots (`last` = the last FX name seen on a player
     weapon, else the last one at all).
  7. Projectiles: `projblock 2` (30 s armed), `projreflect 3 own` - only if archers shoot (opportunistic).
  8. `navdump;prompts;orcs;timers;sound last hand`, the loggers off (their tables), `release`.
  9. Quit to the main menu (run 4's keys), then R1: `res;exec SetResolution 1832 1920` (the control: no hook),
     `res;restall on;exec SetResolution 1832 1920` (tall), `res;exec SetResolution 1920 1080;restall off`, `res`, each
     with a shot 5 s later; then QUIT GAME (run 4's keys).
- Pass / fail per question:
  - **CV1** (re-combat-vr.md 1.7): pass = `STIMREG ... caller=start` (or `key`) lines during the stock attacks name
    the player's stimulus records, `rc` >= 0; at least one `SWING ... result` with `goal_counter`, `block_*` or `dodge_*`
    > 0 for the plain swing (step 1 is enough) or only with `cur` (step 2 needed); `HITEV block=1` after a swing+hit =
    the block decided at damage time. Fail: `rc=-1` with `player_off=1` or `mgr_off=1` (stimuli switched off: look at
    `0x4c47090`); all counters 0 for both kinds -> step 3 (`srvatk`, not built); `swingevent: no attack record` -> no
    PC_ record has stimuli (pick one from `attacks-full.tsv`).
  - **CV2** (re-combat-vr.md 2.7): pass = `HITSTOP ... tracker A ms at the start, A during the hold, B now` (the
    animation stood still), the orc's timer frame 0 ms in the `+x s` lines, `its timer is back`, the mid-hold shot
    shows the orc frozen, its next `APPLYDAMAGE` comes >= the hold later; `TIMER` lines classify orcs (`simulation` or
    `default`) and the player (`own` = the slow-mo player timer). Fail: the tracker advances during the hold (the
    animation is not on the object timer: recipe B, `rate`), `timer was no longer ours` (something swapped it: a sync),
    warnings or a crash after the restore (log it, the save is guarded).
  - **CV3** (re-combat-vr.md 3.7): pass = `KEYHIT` measured within 0.017 s of predicted (one frame) and the hand within
    20 cm (`err=`); `KEYTIMES ... tracker NOT FOUND` or `rate=0` = a blend-driven tracker (count from the start, 3.4);
    `err` small only `as world` = SampleNode is world space, not model space (the doc's [med]).
  - **CI1-CI3**: `PROJ new` with speed/range/homing per shot; `PROJBLOCK` then no `PROJWATCH ... THE PLAYER`; after
    `PROJREFLECT` a `PROJHIT` with the archer as the victim. No archer = not answered (say so).
  - **CI4/5**: `FXNAMES` lists the names; a name on a player weapon during the hit streak = the stock glow comes by
    itself (re-combat-items.md 2.2 row 4); `WEAPONFX ... rc=1` and a visible effect in shot 37/38, gone in 39. `rc=0`
    with `clientfx=0` = ClientFX off; with `clientfx=1` = the name is not a loadable FX file.
  - **CI6**: `weapons` names slot 1's record (Acharn?) and the inactive node (`dagger_sheath_back`?); `DAGGER hand:
    ... 0.x cm apart` at `L_Weapon` and the dagger in the left hand in shot 11, back on the sheath in 12.
  - **CI7**: `SYNCINFO ... Sync=` non-zero for the torment records = a paired action (with its `TargetAttack`).
  - **CI8**: `SOUNDNAMES` = the combat event names; `sound ... playing id` non-zero = the event exists in a loaded bank.
  - **U2/U10**: the `UI formats` line (28 expected; the fade bytes). **U3**: `nav-N.tsv` at the tower top and the
    camp, the `IconMovieClip` strings readable (or the encoding is not a C string). **U7**: `PROMPT` lines for the
    counter/drain prompts during the fight; `[COMBAT|PROMPT]` found (or not) in the game's output files of RUN.
    **U8**: `UISCENEVIEW` lines at the main menu (or none: no 3D UI scene there).
  - **K1**: `EXEC ... file 'C:\Temp\somvr-faction-P7a.txt' -> host '...VFS/Temp/...' exists, N bytes; copied to`; the
    `C:/` and `C:\Feral\Temp` variants say which spellings work. Absent everywhere -> Faction.Dump does not write in
    this build (the doc's static reading would be wrong) or the open still fails: look for `somvr-faction-P7*` under
    RUN (read-only).
  - **R1**: the control `RES` shows 3D 1832x1031 (the back buffer's aspect, re-resolution.md 1.2) and `P_RES3D` called
    once per mode set; with `restall on` 3D 1832x1920, UI 1920x1080, `separate` textures, rt3D 1832x1920; no
    `LETTERBOXED`; the back to 1920x1080 restores the run-4 sizes. Fail: `P_RES3D 0 calls` (the mode set did not
    run: SetResolution needs another path), a letterbox (`RES_RECT_LETTERBOX*` needed), a hang or crash at the mode
    set (the end of the run: nothing lost).
  - **P1q**: the shots `20-piece-NN` against `19-pieces-base`: the pieces whose hiding changes the head box (from
    behind: hood, hair, helmet, cloak) - the input for P3 and re-resolution.md 2.3's head bits.
  - **T1** (smoke test): `TRAV fsm=8/..` then `10/..` around the teleport's 1 m drop, `STATE ... fsm=` changes.
- Notes:
  - Active words run only with spike1's guard (`GUARDCHECK ... patched`). The orcs may die from `dealhit` and the
    counters: no `kill` is used and nothing persists (the save guard), but Steam's own stats/achievements are not
    guarded (design-vr.md D16): if the coordinator wants no kills at all, drop step 4's `dealhit`s and step 5 keeps only
    `hitstop` on a picked orc without a hit.
  - The harness's audio is SDL's dummy driver: sounds give playing ids, not audio.
  - `@out` stays a host path (fine for the probe's own files; useless for the game's file calls). Use `@cout` for those.

#### Results, P7 (2026-10-20 00:11, `logs/boot-check-20261020-001117`)

Run as planned: spike1 + the probe from `logs/build-p7` (main `371ac92`), `SOMVR_COMFORT=1`, `probe-P7.cmd`, the user's
save copy; 428 s of game, exit 0 by itself. GameData.sav byte-identical (the guard caught 3 writes), the install
unchanged apart from the moved CEF leftovers. **No crash, no FAIL, no WARN**: probe.log (5435 lines) and somvr.log
(spike1) have none; game.log only the `Previous run didn't quit gracefully` line every run since 10-18 has. 56 shots.
Where: the forge tower top for the dumps, the dagger and the piece sweep; then `pick` (`AI_Infantry`, 38 m) and
`Teleport` to a camp below the tower, where a cave troll was fighting the orcs. **The orcs never fought Talion**: in
the ~110 s there they ran alert and suspicion reactions (`AI_Infantry_ReactToAlert*`, `ReactToSurprised*`,
`ReactToBaseLow_FromSusp`, sheathe/unsheathe), no orc attack and no counter window was aimed at him, the autocounter
pressed 0 times. That limits CV1, C6, C7 and U7.

- **C2, `Combat_DealHit` from our code: yes.** 6 `dealhit auto` (no stock animation; the record = the last player
  attack `PC_HC_Lowest_L1_D0_F_LL_RL_LR_4_OverHeadAng2`; kill=0) at 1.5-7.8 m: each `HITEV damage=10.00 block=0
  crit=0` with Talion as the attacker; the orc's hit reaction `AI_Bip_L100_L2R_F` started in the same frame (4 of 4
  without a hold); the game's own blood FX on Talion's sword (`fx\common\blood\blood_level01_weapontrail.fxp`, site
  `0x273c4fb`, same frame, 6 of 6) and its impact sound `cbt_imp_atk_player`; the HUD hit counter shows `x1` (shots
  34, 35). The game checks no range (the 7.8 m hit landed). Not covered: death, XP, Nemesis credit (no `kill`). C3:
  the recoil direction is not readable from the shots. C4: our hits start the counter (x1); a streak was not tried
  (the hits were 3 s apart).
- **CV1, the AI's view of a swing: not shown.** Stock: 3 `cmd CombatPrimary01` -> 3 player attacks (`ATK noaim`:
  `PC_HC_Lowest_L1_D1_F_LL_RR_3`, `..._D1_F_RR_LL_L2R_4`, `..._D0_F_LL_RL_LR_4_OverHeadAng2`; target 0, the orcs were
  15 m and more away), a `STIM caller=start` each, and their only stimulus is **`WeaponImpactSound`** (`STIMREG` rc
  2044/2045, weight 1, not persistent, `player_off=0 mgr_off=0`); no AI line. `attacks-full.tsv`: the 499 `PC_HC_*`
  records register `WeaponImpactSound` (420), `Weapon_XSwing` (47, the `PC_HC_High_*` tier) or nothing (32).
  Swings: `swingevent auto` at 16.8 and 8.0 m, `swingevent auto 400 cur` at 4.4 and 4.2 m: one `WeaponImpactSound`
  registration each (rc >= 0), every `SWING ... result` `goal_counter=0 block_valid=0 block_go=0 dodge_valid=0
  dodge_go=0`. Over the whole run: `AI_GOAL` 0 calls, the block and dodge checks 6 calls each (one logged, result 0
  at 1.5 m, nobody attacking), `_GO` 0; `HITEV block=0` on all 6 hits. No baseline either (the stock strikes reached
  nobody, no orc was in a fight with Talion). Next: a run where an orc fights Talion (run (a) with a strike in reach),
  `swingevent` with a `Weapon_XSwing` record (`PC_HC_High_*`), then step 3 (`srvatk`, not built).
- **CV2, hit-stop by the object timer: the swap and the restore are clean; the freeze shows only indirectly.**
  `timers`: every live orc has **its own timer** (25, 36 and 31 orcs: all `own`, none on the simulation or the default
  timer; num/den 1/1, 16-17 ms frames, totals = time since its spawn), the player's is `own` too, and the game timer
  is the simulation timer (`0x247475a0`). `dealhit auto;hitstop 120` and `dealhit auto;hitstop 600` (timer): ours has
  num/den 0/1, scale 0, 0 ms frames during the hold, `h+0x68` ours, then `its timer is back` both times. `hitstop
  1500 rate`: 4 trackers to rate 0 (tracker 255 was 1), `its rates are back`. No warning, no stuck orc; the run went
  on 100 s to a clean quit. The effect: after the 120 ms hold the hit reaction started at the hold's end (0.133 s
  after the hit; without a hold, the same frame); **after the 600 ms hold no hit reaction came at all** (that orc's
  next start 3.0 s later was `ReactToSurprised_2`): a long hold can swallow the recoil. Not measured: the animation
  time (the probe's tracker readout is 0 ms throughout: it reads tracker 0xff, not the playing one), "APPLYDAMAGE
  later" (the held orcs were not attacking); shot 35 (inside the 600 ms hold) shows the orc doubled over beside
  Talion, which a still cannot prove frozen. Next: read the playing tracker (`FindTrackerPlaying` of `c+0x17c`)
  during a hold on an attacking orc; keep holds near the design's 120 ms.
- **CV3, key times: yes, counted from the attack start.** 508 `KEYTIMES`, 44 `KEYHIT`. The tracker of the attack's
  animation was found 507 times (`NOT FOUND` once, a knockdown recoil), with **rate 0 every time** (blend-driven,
  re-combat-vr.md 3.4), so `impact in` (now/rate) is never computed. From the start, the table's `APPLYDAMAGE`
  (`Combat+0xcc`) is the impact: for the 30 AI attacks with an impact key (abuse kicks and slaps 833/866/966 ms,
  infight melee 802 and 1231 ms) the measured `APPLYDAMAGE` came 0-18 ms after the table time 29 times (mean 13.9
  ms, the next frame) and 48 ms once; KeyLookup gave the table's ms wherever it found the key. `SampleNode` x the
  object transform put `R_Hand` within 20 cm 29 times of 30 (mean 7.3, max 22.9 cm); taken as world it is 330-470 m
  off, so SampleNode is model space. Talion's own strikes: table 400 ms, measured 0.415/0.416 s, but the hand 112 and
  40 cm off (root motion and layers). Ambient loops with a 0 ms key land one frame after the start.
- **C6, telegraph (orc against orc and troll, not against Talion):** 12 `COUNTERPROMPT`s with windows of 566-898 ms
  (= EPW - BPW of the table); the impacts came 0.55-0.97 s after BPW (`since_bpw`), e.g.
  `AI_Infantry_Infight_Melee_Front` BPW 266, EPW 1164, APPLYDAMAGE 1231 ms. C7: not tested (no window against Talion).
- **CI1-CI3, projectiles: no projectile flew after `projlog on`** (no archer shot near Talion); `projblock 2` and
  `projreflect 3 own` were armed 30 s each: 0 blocked, 0 reflected. Before the loggers (169 updates, 7 hits, the last
  at 213351.7, logs on at 213352.5): thrown spears and the troll's rocks are CProjectiles (`P_PJ_UPDATE` first 1.7 s
  after `AI_Infantry_ThrowProjectile_Troll` began, then `P_PJ_DAMAGE` and in the same frame the HITEV of that attack,
  25 damage on the troll; FX `projectile_spear.fxp`, `troll_rockthrow_projectile.fxp`). Speeds, homing, block and
  deflect: open (needs archers).
- **CI4-CI5, FX:** `fxnames-55.tsv`, 23 names (weapon impact sparks and splinters, troll FX, `projectile_spear`,
  `orcbzk_ranged`, ...). On Talion's sword by themselves: `fx\characters\player\elf\elf_lightchain.fxp` at each stock
  strike (site `0x26fcbd7`, 316-333 ms after the start = the `BCOL` key at 313/320 ms: the swing's light trail) and
  the blood trail at each of our hits (C2). No glow from a streak (only x1 was reached). `weaponfx last` (the blood
  trail): one shot `rc=1` handle 0, loop `rc=1` handle `0x24591160`, `weaponfx stop` -> 0, `clientfx=1`; shots 37-39
  cannot show it (the drawn sword was in front of Talion with the camera behind, and a trail needs motion). The calls
  pass; the look is open.
- **CI6, weapons and the dagger: pass.** Slot 0 `PC_Sword_01` (active), slot 1 **`PC_Dagger_01`** (Acharn), slot 2
  `PC_Bow_01`, all shown and visible (the dagger is on its sheath outside kills, shot 10). The node hashes are
  sockets: sword and dagger active = socket 16 on `R_Weapon` (`0x305e50e3`); inactive = socket 24 on `scabbard_back`
  (`0xdecfcc84`) and socket 23 on `Dagger_Sheath` (`0xb9770fb8`); the bow active = socket 6 on `L_Weapon`, no inactive
  node. `dagger hand` (InactiveNode -> `L_Weapon` `0xfef266e`, then `FUN_0231e2e0`): the dagger left the back (shot
  11) and by the next word its model object was 2.6 cm from L_Weapon's position. The `DAGGER hand` line's 101.8 cm is
  measured in the same frame, before the game moved it (probe nit: measure a tick later). `dagger back`: restored (0
  entries changed by the game), on the back again (shot 12).
- **CI7, brutalize: a sync.** `syncinfo`: 28 records, all with `AnimationData.Sync`. `PC_Bip_StealthTorment_B/F` use
  the **StealthBrand** sync (`PC_Bip_StealthBrand_B/F`: TargetAttack `AI_Bip_PC_StealthBrand_B/F`, InterruptTable
  `Player_Initiated_InstantKill`, AttachSocket "Attach", TeleportDuringInterpolation 1, InitialAlignmentBothRotate 1,
  AlignTranslation 1, CanApproachPlayer 1, KillTargetDamage 0), AOEOnHit `Rune_SBrnd_AoE_Brand`, no stimuli; the
  Hang/Dangle/Sitting/Immune/Resist variants alike, some with `Instakill_Loud`/`Player_AIStepBack`.
  `PC_Bip_SneakInstaKill_Loud_B` -> `PC_Bip_Sneak_InstantKill_Loud_B` (TargetAttack `AI_Bip_PC_Sneak_IK_Loud_B`,
  stimuli `Instakill_Loud` and `Player_StartleSlave`, persistent `Performing_Instakill`, AOEOnHit
  `Rune_BrutalK_3xMorale`); the control `PC_Bip_Sync_Cntr_F` -> its own sync (`AI_Bip_PC_Sync_Cntr_F`). `attacks full`:
  3146 of 10814 records have a Sync, 47 `IsBlock` (the DONE line's "10814 with stimuli" counts the `0` entries too;
  the TSV column is right).
- **CI8, sounds:** `sound ui_game_back` (global) -> playing id 4627; `sound last hand` (`fol_cmn_walk` on our object
  `0x50a70001` at `R_Hand`) -> 13337. Combat events seen: `cbt_imp_atk_player` (our hit), `cbt_imp_atk_biped`,
  `cbt_imp_atk_slave_imp`, `cbt_pris_kd_imp`, `fol_cmn_recoil`, `fol_ply_cbt_swd_twirl`, `fol_cmn_unsheathe`/`sheathe`,
  `dx_cmbt_GenBasic`, `vx_orc*_Enemy*`, and the combat music `mus_combatmode_default` + `mu_mode_combat` on the 2D
  emitter. The `SOUNDNAMES` table is capped at 64 and was full of front-end names before the fight; the combat names
  come from the `SOUND` lines.
- **K1, `Faction.Dump`: yes, with every `C:` spelling.** `C:\Temp\...P7a`, `C:/Temp/...P7b` and
  `C:\Feral\Temp\...P7c` (two commands in one exec string) each wrote the same 70787-byte file into
  `RUN/home/.local/share/feral-interactive/ShadowOfMordor/VFS/Temp/` (copied to `probe-out/`): Latin-1 INI text of one
  faction (`Orc_Faction_SoN`, 27 members): `[FACTIONDATA]`, 3 `[SIMEVENT]`, 25 `[HIERARCHY_POSITION]` (name, title),
  27 `[FACTIONMEMBER]` (CombatTree, Role, Personality, Name, XPLevel, Traits, IsDead, KilledByPlayer, the `SMC_*` model
  parts, ...), `[QUEST_LIST]`. C2's Nemesis check can read it.
- **U1, confirmed in the world:** per frame `0xa124c9` target 0 mask all, `0xa0dc3c` target 1 mask 3, `0xa0dc69`
  target 1 mask 0x38 (front end with the 3D menu scene: `0xa0dab7`/`0xa0db65`), all `this=0x461df60`, game thread.
- **U2:** both formats 28 (`R8G8B8A8_UNORM`); at 1920x1080 one texture, at 1832x1031 and 1832x1920 separate UI and 3D
  textures (R1). The alpha: open (not in this probe).
- **U3, markers:** 292 (tower) and 295 (camp); none shown at the tower, one in the camp (the cave troll's, 81 m,
  `wargmapicon_ui.tex`). `IconTexture` is a readable C string on 265-267 (break 87, ammo 79, flame 38, lock 24, warg
  8, barrows 5, collectibles); `IconMovieClip` only on `TrollHealthBar` (`SPH-HealthBar`), `IconMovieClipPC` on none;
  `ActionID` the same `0x886d5ca8` on all; `RenderOffScreen` 0 on all; the orcs' `InfantryNormal`/`NPC` markers carry
  no icon string. Positions only for the 31-32 markers the probe asked; the `DisplayRange` column is a pointer, not
  the value (probe nit). The hide (G19) is somvr's.
- **U7, prompts:** 0 live entries at the tower and in the camp, no change in 110 s with the watch on;
  `Combat.Debug.Prompt` went 0 -> 1 -> 0 but no `[COMBAT|PROMPT]` line anywhere in RUN (game.log, debug.log, home):
  no combat prompt came up for Talion. Open.
- **U8, the UI scene:** at the main menu `CUISceneMgr` `0x7f104da4e1f0` with a loaded level; the `UISceneCamera` object
  at (-3580.0, 754.4, -1134.9), quat (-0.026, 0, 0, 1) (3 degrees down), **fovY 12.00, fovX 21.17 degrees = 2 atan(16/9
  tan 6)** (from the UI aspect, re-hud.md 2.2), near 0.25, far 100000, DoF on, blend start 39.07 s, duration 6; the
  same after the resolution test (a new level object); rendered by `0x461df60`. The load screen was not covered.
- **U10:** `im+0x2ad8` holds two pointers (`0x7f104d580280`, `0x7f104dd1fc20`) and zeros, at the menu and in the world,
  with no fade running: not a fade value at that offset. Open (dump during `UI.ForceFadeIn`).
- **R1, the tall eye: pass (at the main menu).** Control, `exec SetResolution 1832 1920` without the hook: cvars 1832
  x **1031** (the game writes its own height back), 3D 1832x1031, UI 1920x1080, back buffer 1920x1080, separate
  textures, 3D rect 0,0,1832,1031, rt3D 1832x1031 (flags 0x37d7). `restall on` + the same exec: cvars 1832x1920,
  **3D 1832x1920, UI 1920x1080**, back buffer 1920x1080, separate textures, 3D rect 0,0,1832,1920 (**no letterbox**),
  rt3D 1832x1920. `exec SetResolution 1920 1080` + `restall off`: 1920x1080 everywhere, one texture again. `P_RES3D`
  ran once per mode set (call 1 at boot, then 2, 3, 4; heights 1031 -> 1031, 1031 -> 1920, 1080 -> 1080); each exec
  took 53 ms, no hang. Shot 81: a pillarboxed 1031x1080 column (1832x1920 scaled to the window) holding the menu UI
  and the 3D menu scene, **both squeezed sideways**: the 1920x1080 UI rect is drawn into the column, and the UI scene
  keeps the UI's 16:9 fovX (H3's tangents stayed 0.1869/0.1051 through the tall mode). Cosmetic on the mirror; a menu
  UI scene in an eye needs the eye's projection (H3) or the eye aspect. Not checked: the world at the tall size
  (`cam+0x3a0`/`+0x3fc`, H8 at 1832x1920).
- **P1q, the head pieces: none found - no piece hide showed.** `piecehide 0` .. `16` each returned 0 (piece 0 =
  `..._Talion_Hair_Bones.hkt`, 1 = `..._ragged_cloak.hkt`, 2-16 unnamed), one shot each against `19-pieces-base`
  (camera behind Talion, the white-lit tower top). In none of the 17 shots does any part of Talion disappear: head,
  hair with its tie, cloak, quiver, sword and dagger are in every one; the pixel differences are the idle motion and
  the fog. So there are **no head indices for `SOMVR_HIDE_HEAD`**: `SetPieceHidden` on these 17 pieces of `hChar`'s
  model reaches nothing visible. Most likely they are the base model's (its only names are Havok files) and the
  visible body, head and hair are separate part models attached to it, like the orcs' `SMC_*` parts in the faction
  dump. Next: log the render node's mask `node+0x130` after `piecehide` (did it change at all?), list the player's
  attached child models (`*(playerSm+0x90)`, re-animation.md 4) and their pieces, and sweep those. P2q's shot was not
  taken.
- **T1 (smoke test):** the teleport put the root at y 3989.0, 130 cm above the y asked (3858.9; the root is at pelvis
  height); then Talion slid 114 cm down and about 1 m sideways in 6.1 s while idle (`Gnd Idl`) and stopped at
  (38195.5, 3874.6, -24729.0), with `fsm 6/1`, velocity mode 1, `supported=1` and gravity on the whole time (`pc+0x58`
  = **-20**, not re-traversal.md 2.4's -9.8): no fall state (8, 10) at this teleport. `fsm 7/6` (AliveStateCombat)
  came with the first stock strike and went back to `6/7` 3.8 s later. `head_y=0.0` in every `TRAV` line (the head
  read does not work: probe nit).
- C9: 37 and 31 live Combat candidates in the camp. U5: as run 3.

### P8 - a fight at Talion, the head among the attached models, the arms, the UI pointer, traversal reads

**Why now:** P7 left CV1, C6, C7, U7 and the hit-stop's animation time open for one reason: no orc fought Talion. It
also showed that none of the 17 pieces of hChar's own model is the head. Two static docs of 2026-10-20 ask for runtime
words before mod/somvr builds on them: `re-arms-ik.md` (bb16bee: the G11 write point and the engine's own arm IK) and
`re-ui-pointer.md` (7c7725f: the laser pointer's mouse feed and the pad guard). All of it fits one boot.

**How the fight is started** (static, read for this run in the full listing `reference/work-ui/full.dis.gz`):
- **`aggro`** calls the NPC script binding **`SetCombatScriptedTarget`** without Lua. The name string is `0x38f309a`.
  Its binding `FUN_02405be0` takes three arguments (the NPC, a target character, a bool) and calls **`FUN_0296f870(npc,
  targetCharacter, sticky)`** **[high]**. That function:
  - first calls `FUN_02964f70(npc, target)`. It builds two WBObjRefs to the target, most likely a working-memory fact
    (the AI learns of the target) **[med]**;
  - then writes, at the NPC's AI `*(npc+0xc78)`, a WBObjRef to the target's HOBJECT at `+0xb50..+0xb60` (that is,
    `*(target+8)`, GameBase's `+8`: the probe checks it is the HOBJECT) and the bool at `+0xb68` **[high]**.
- The only reader of `+0xb60` among the AI functions is **`CAITargetSelectCombatScripted::vf_07`** (`0x2a94d70`, vtable
  `0x3ae23d8`). A second path (`0x296c0f1`) clears the target when the bool is 0, so `aggro` passes 1 (sticky)
  **[high]**.
- `ClearCombatScriptedTarget` is `FUN_0296f940(npc)` (`aggro off`).
- RT: whether that target select is in the camp orcs' selector sets (data), and whether they then attack.
- The probe checks the layout before the call: the NPC's vtable `0x3a2b3d0`, `npc+8` = the orc's HOBJECT, the player's
  `CPlayerObj+8` = his HOBJECT. Without that it calls nothing.
- **`minhealth 20`** keeps Talion alive. It writes `SetMinHealth`'s field: binding `FUN_023fa7d0` -> `FUN_0269da10` ->
  `CCharacter+0x9b4`. `CCharacter::vf_37` (`0x268e4d0`) clamps the damage to `max(health - min, 0)` whenever the hit's
  own `Damage.MinHealth` is 0 **[high]**. So he neither dies nor gets healed.
- The fallbacks run in the same fight: P7's stock strikes, now with Talion 3 m from the orc, and `dealhit` (the damage
  pipeline's threat/aggro step 16, re-combat.md 1.2).

- Commands: `scripts/cmds/probe-P8.cmd` (run 4's menu, load and quit steps; the rest `probe` lines with a shot before
  each key step; no live typing). About 8.5 minutes of game time: 461 s of waits plus the shots and probe sends;
  `--hold 720` = the 12-minute cap.
- Launch (as P7, with the hold at 12 minutes):
  ```
  cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
    --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
    --game-env SOMVR_COMFORT=1 \
    --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p8-world;orcs;timers' \
    --cmds scripts/cmds/probe-P8.cmd --hold 720
  ```
- New words (the probe's header has the full syntax):
  - read-only: `attached`, `wnodes [SLOT]`, `ikwatch [SECS]`, `ikcb [SECS]`, `mouselog on|off`, `mstate`, `hitscan`,
    `surf [M]`, `mount [SECS]`, `conlog on|off`;
  - active (spike1's guard): `attachhide ID [node]`, `armpose rot DEG | ik X Y Z`, `aggro [N] [loose] [SUB...]`,
    `minhealth V`, `pointer X Y | u=U v=V | off`, `click [BTN] [FRAMES]`, `padguard off`, `mcmd CMD`, `grav off`,
    `piecehide all`;
  - extended: `hitstop MS ... onbpw`; `exec` logs the console's output of its call (`CONOUT`);
  - undo words (no guard needed): `attachhide off`, `armpose off`, `aggro off`, `minhealth off`, `padguard on`,
    `grav on`.
- Fixes from P7's nits:
  - `trav`'s `head_y` reads the head through `GetNode("Head")` when `nodes` has not run, plus `head_dy` (above the
    root).
  - `dagger hand/back` measures again 3 ticks later: `DAGGER ... (3 ticks later)`.
  - `hitstop` reads the playing tracker: FindTracker of `Combat+0x17c`, as `keytimes` does.
  - `KEYTIMES`/`KEYHIT` name the attack's target and set the parry window (BPW..EPW) against the measured impact.
  - `COUNTERPROMPT`/`APPLYDAMAGE` mark `(THE PLAYER)`, and each counter prompt at Talion logs the prompt diamonds on
    the next tick.
  - `pick` (and `aggro`) also skip trolls and spiders: P7's camp had a cave troll.
- Order (game time from the cmd file's waits; the `world` mark at ~166 s):
  1. **Main menu** (~85 s): `mouselog on;mstate;ui;res`. Then `pointer 136 499;mstate;hitscan`: hover OPTIONS, shot 07.
     Then `pointer 117 266`: hover PLAY, shot 08. No click here, so a failure cannot break the load: PLAY keeps the
     focus either way.
  2. Load as in run 4.
  3. **Tower top** (~186 s):
     - dumps: `nodes;pieces;attached;weapons;wnodes`, then `ikwatch 20;ikcb 20;trav 4;surf 3;mount 3;exec
       AIDebug.ListAIs`.
     - **head sweep**: `piecehide all` (shot 21), `attachhide 0`..`9` (shots 22-*: each child with the children
       attached to it), then `attachhide 0`..`3 node` (the render node's skip byte, shots 23-*), `attachhide off`.
     - **arms**: `armpose rot 60`, `armpose ik 25 10 35`, `armpose ik 30 55 40` (shots 30-32), `armpose off` (33).
     - **gravity**: `trav 10;grav off;pmmove 0 30 0` (shot 34), `grav on` (35).
  4. **The fight** (~241 s):
     - setup: `pick;minhealth 20;exec Teleport @ox+300 @oy+100 @oz` (shot 40), then the loggers: `stimlog aiwatch
       keytimes`, `prompts watch on`, `Combat.Debug.Prompt 1`, `conlog on`, `ikwatch 90;ikcb 90`.
     - **`aggro 3`**: 30 s with shots at 10, 20 and 30 s.
     - CV1: `cmd CombatPrimary01` x3, `swingevent PC_HC_High_L1_D0_F_LR_RR_UD_2 400 cur`, the same without `cur`,
       `swingevent auto 400 cur;dealhit auto`.
     - CV2: `hitstop 120 onbpw` (12 s for a counter prompt at Talion), then `dealhit auto;hitstop 120`.
     - C7: `autocounter CombatPrimary02 3` for 20 s.
  5. **Archers** (~327 s): `aggro off;pick Archer;projlog on`, `aggro 2 Archer;projblock 3` (25 s), `projreflect 3
     own` (25 s), then everything off.
  6. **Pause menu** (~381 s):
     - `mouselog on;mstate;padmode on`, then `pointer 190 495;hitscan` (OPTIONS) and `click`: shots 61/62, the pad
       guard expected to hold.
     - `padguard off;pointer 190 495;click`: shots 63/64, OPTIONS expected to open.
     - `padmode off;padguard on`, Escape back. Then Escape twice (to the world and back into the pause menu), `pointer
       240 248` (hover the first item) and P7's quit: 5 Downs, Return, Yes by click.
  7. **Main menu again** (~429 s), where a failure costs nothing:
     - `pointer 136 499;hitscan;click`: OPTIONS, shots 80/81; Escape back.
     - the corners in `mstate`: (0,0), (1919,1079), u=v=0.5.
     - `pointer 147 557;hitscan;click`: QUIT GAME, shot 84. Return confirms the dialog. The cmd file's last line is
       `quit`, so the harness stops the game if it is still running.
- Pass / fail per question:
  - **AG1 (the fight):**
    - pass = `AGGRO seq=N orc k ... scripted target 0/0 -> 0x.../1 (Talion)` for 1-3 orcs, then within 30 s
      `AGGROWATCH ... combat target TALION` and an attack name. Also `COUNTERPROMPT ... (THE PLAYER)`, `APPLYDAMAGE ...
      (THE PLAYER)`, `KEYTIMES ... target=THE PLAYER`, and Talion's health falling to 20, no lower (the
      `AGGROWATCH` lines).
    - fail:
      - `aggro: the player's CCharacter ... not the layout read`, or `AGGRO ... skip`: the offsets differ, nothing was
        called.
      - `(NOT Talion)`: `FUN_0296f870` returned early (target null or deleted).
      - `(Talion)` but `combat target none` for 30 s: the CombatScripted target select is not in these orcs' sets.
        Then the stock strikes and `dealhit` decide (step 4). If those fail too, the next step is a stimulus
        (`Instakill_Seen`/`CharacterAudibleLoud` records, see `attacks-full.tsv`) or the AI combat manager.
  - **CV1** (re-combat-vr.md 1.7):
    - pass = the stock strikes now have a target (`ATK ... target=0x...`), and `AI block_valid/dodge_valid result=1`
      or `block_go`/`dodge_go` follow them, or `HITEV block=1`.
    - `SWING ... result` counters > 0 for the `Weapon_XSwing` record: with `cur` only = step 2 needed; for both = step
      1 is enough.
    - fail: all counters 0 while an orc fights Talion = step 3 (`srvatk`, not built).
  - **C6**: `KEYHIT ... target=THE PLAYER ... parry window BPW a..EPW b ms: the impact after the window (+x ms after
    EPW)` for each orc attack at Talion. This gives the telegraph lead the VR block needs (re-combat-vr.md 3).
  - **C7**: `AUTOCOUNTER ... -> CombatPrimary02`, then a player attack whose record is a counter (`ATK ...
    PC_..._Cntr_...` or a sync) = the input counter works. Presses but no counter = the window needs more (cancel +
    recoil, re-combat.md 3.6).
  - **U7**: `PROMPTS at a counter prompt at Talion: n live entries` with a `PROMPT ... action=... string=...` line =
    the counter prompt's entry (action, StringId, IconId). 0 entries = the counter prompt is not a diamond entry
    (look at `CONOUT` / `[COMBAT|PROMPT]`).
  - **CV2**:
    - The `HITSTOP` lines name the playing tracker (`animation time on tracker T (anim A)`).
    - pass = `+0 ms during the hold` (the tracker stood still) and the hold's orc `APPLYDAMAGE x s after the hold
      started`, later than its table time by about 120 ms.
    - fail = the tracker advanced (the animation is not on the object timer: recipe B), or `tracker 255 ... not found`
      (no playing attack).
  - **P1q / the head**:
    - pass = one of shots 22-*/23-* (from behind) shows the head, hood or hair gone. That child's `ATTACH k` line names
      its model and pieces: the input for somvr's `SOMVR_HIDE_HEAD`, which then has to work on that model's node
      (re-resolution.md 2.3).
    - `piecehide all` (shot 21): if the body vanishes, the visible body is hChar's model and P7's single pieces were
      only too small to see. If nothing vanishes, the mask did not reach the render node (`PIECEMASK`: the node mask
      still 0 while the model mask is set) or hChar's model is not what is drawn.
  - **A5 (arms)**: pass =
    - `ARMPOSE ... reached; the next player update read R_Hand 0.x cm from ours` (the edit reaches GetNodeTransform);
    - `the sword 0.x cm from object x pose'[node] x offset` (the sword follows) and not near the unedited pose;
    - shots 30-32 with the arm (and the sword) in the new pose, no flicker.
    - fail: `skipped` frames (the pose buffer's count is not the skeleton's), or a large read-back error (another pass
      rewrites the pose after phase 2: then `ARM_OUT_EVAL`).
  - **IK1/A7**: `IKSETUP ...` records with their target socket (`IK_RHand`/`IK_LHand` or not); `IKCHAIN` states during
    the swings; `IKCB add/remove` types 0xa-0xc and 0x11/0x12 on the arm nodes. No setup in the whole fight = the engine
    places no arm by IK in combat.
  - **W1**: `wnodes-N.tsv` and `WNODES ... the farthest node ... cm` = the blade length; `WATTACH` = the grip entry
    (node, offsets) for `C_h`.
  - **UP1-UP4**:
    - main menu: shots 07/08 with the highlight on OPTIONS, then PLAY, and the `MOUSE view move ... (m_GameUI)` / `MOUSE
      gfx move` lines; shot 81 = Options open.
    - pause menu: shot 62 still the menu with `MOUSE guard click ... pad=1 -> 1`; shot 64 = Options with `pad=1 -> 0
      (padguard off ...)`.
    - `MSTATE` corners: the movie's mouse = the pointer (offset 0, no flip).
    - `HITSCAN ... hit=1` on the item under the pointer.
    - fail: `pointer: no local player and not the front end`; no `MOUSE view` after a `pointer` (a menu state
      swallowed it: its number in `MSTATE`); a click without any action (the hit test or the layer refused it).
  - **T6**: `SURF ... record '...' CanWallClimb=0|1` at the tower top (whatever is ahead; a wall drill is its own
    run). **T7**: `TRAV` after `grav off`: the root up 30 cm and staying up (`gravity=0` or `velmode` changes), then
    down after `grav on`; any `fsm=8` (falling) = the graph noticed. **T8/T9**: `MOUNT off` only (no beast here).
  - **CI1-CI3**: as P7, if `PICK ... 'AI_Archer...'` found one. `FAIL ... pick: no live AI character matches` = no
    archer in the camp's bands: open again.
  - **trav fix**: `head_y` non-zero and `head_dy` about 72 cm (run 3: the head 72.5 cm above the root). **dagger fix**:
    `DAGGER ... hand (3 ticks later) ... 2-3 cm apart`.
- Notes:
  - `aggro` and `minhealth` change game state (spike1's guard; nothing persists; `aggro off` and `minhealth off` at the
    end).
  - `armpose` edits the pose every frame while on (the job rewrites it each frame).
  - `attachhide` and `piecehide all` hide pieces of the player's models (shown again by the next word).
  - `grav off` stops gravity until `grav on`.
  - `padguard off` changes the menu items' pad guard until `padguard on`. `padmode on` forces pad mode until
    `padmode off`.
  - `conlog`/`exec` swap the console's output slot (`0x4b5e928`) and restore it; the destructor restores it too.
  - Kills: none intended (no `kill`); `dealhit`s may still kill a weak orc (Steam stats, D16).

#### Results, P8 (2026-10-20 02:21, `logs/boot-check-20261020-022145`)

Run with spike1 + the probe from `logs/build-p8` (main `b3f2ab0`), `SOMVR_COMFORT=1`, `probe-P8.cmd`, the user's save
copy; 499 s of game, ended by the harness's `quit` (exit 143) after the game had quit to the main menu. GameData.sav
byte-identical (the guard caught 6 writes), the install unchanged apart from the moved CEF leftovers. No crash;
somvr.log (spike1) has no WARN/FAIL; probe.log 5778 lines with 7 FAILs, all explained below (`attachhide 7/8/9`: only
7 entries; two `swingevent cur` refused during a recoil; two `hitscan` with no menu open). 65 shots.

**Verdict:** the fight worked (AG1), so C6, CI1-CI3 and the hit-stop's animation time are answered; the arm write point
(A5) passed on screen; the menu mouse (R1) hovers and clicks in KBM mode. Not answered: no head, hood or hair among
the attached models (P1q), no AI reaction to any strike, ours or stock (CV1), no counter by input (C7), the pad guard.

**Surprise: Talion died.** `minhealth off` (seq 50) came while the camp's orcs were still attacking (`aggro off` clears
only the scripted target; they kept Talion as their combat target). 0.2 s later a hit took his last 20 health:
`PC_RecoilAny_PreDeath`, the last-chance QTE (shot 60, not pressed), `PC_DeathLoop`, the Nemesis death scene (shots 62,
64: the orc Bagabûg promoted to captain, "Killed You"), a respawn at a forge tower (shot 70). Only the run's save copy
has the new Nemesis; the user's save is unchanged. The death also shifted the cmd file. The pause-menu steps (seq
51-57) ran during the death screens, with no menu open. The "main menu again" steps (seq 58-64) ran in the world and
then in the pause menu, which the cmd's Escape had opened. They ended with our click on QUIT, the quit dialog and
Return (quit to the main menu). Next fights: keep `minhealth` until Talion has teleported out of the camp.

- **AG1, `aggro` (SetCombatScriptedTarget from our code): pass.**
  - `aggro 3`: three `AI_Infantry` at 2.6, 29.7 and 38.6 m got `scripted target 0/0 -> 0x1ca4e8d0/1 (Talion)`.
  - 1.0 s later all three had `combat target TALION` (alert/unsheathe reactions). The first attack came 3.7 s after the
    call (`AI_Infantry_Melee_Front_1_RR`, `COUNTERPROMPT ... (THE PLAYER)`), the first hit 4.5 s after.
  - Talion's health went 120 -> 99 -> 78 -> 57 -> 36 -> 20 in 10 s (21 per hit). `minhealth 20` then held it at 20
    for 125 s of hits.
  - `aggro 2 Archer` worked the same for two `AI_Archer`s.
  - Over the fight: 74 counter prompts and 70 `APPLYDAMAGE`s aimed at Talion (59 melee, 10 arrows, 1 the pre-death
    blow).
- **C6, the parry window vs the incoming attacks: they match.** 51 `KEYHIT ... target=THE PLAYER`.
  - Infantry melee (`AI_Infantry_Melee_Front_1_LL/_1_RR/_2_LL/_3_LL`, `_Left`, `_Right`): the table's window opens
    **at the attack start** (BPW 0 ms; 1 ms once) and closes at EPW 747, 761, 765, 778 or 800 ms.
  - The table's `APPLYDAMAGE` is 800 ms (802, 811). The measured impact came 0.815-0.817 s after the start (49 of 50;
    the held one 0.950 s, CV2). That is **16-70 ms after the window closed**: +55 ms after a 761 ms window, +69/70
    after 747, +16/17 after 800, +38 after 778.
  - The counter prompt (BPW) fires one tick after the start. `since_bpw` of the 59 melee impacts: 0.799-0.801 s (one
    0.933 s: the hold).
  - Archers (`AI_Archer_Shoot__100PercAccurate`, `_ShootFromLedge_100PercAccurate`): BPW 1566, EPW 2566 ms (1000 ms).
    The release (`APPLYDAMAGE`) is 2633 ms in the table and 2648 ms measured (+82 ms after EPW), 1.066-1.084 s after
    BPW. Spear throws: 666 ms windows.
  - So the VR parry has ~0.75 s from the prompt to the window's end, and the blow lands 1-4 frames after it.
  - CV3 against Talion: the impact time exact. `R_Hand` from `SampleNode` within 3.6-34.1 cm (mean 22.4; P7: 7.3).
- **CV1, do orcs react to our swing event: no. They did not react to the stock strikes either.** Three orcs fought
  Talion at 1.5-6 m.
  - Stock: three `cmd CombatPrimary01` gave `PC_HC_Lowest_L1_D0_F_LL_RR_RL_5` (cut by a recoil before its impact),
    `PC_HC_Lowest_L1_D1_F_LL_RL_L2R_2` and `PL_LedgeKick`, all with a target (orc 2). The last two hit (10 damage,
    `block=0`) and the orc played a hit reaction (`AI_Bip_L100_L2R_F`, `AI_Bip_H300_ST_F`). Stimuli:
    `WeaponImpactSound` twice, none for the kick.
  - Ours: `swingevent PC_HC_High_L1_D0_F_LR_RR_UD_2` (no `cur`) at the picked orc, 4.6 m away: one `Weapon_XSwing`
    registration (rc 1961, `player_off=0`), then `goal_counter=0 block_valid=0 block_go=0 dodge_valid=0 dodge_go=0`
    in 2 s.
  - Both `cur` swings were refused: `the player is in an attack ('PC_Recoil_Light_R2L_B/F')`. Talion was in a recoil;
    the orcs hit him every ~2 s.
  - Over the whole run `AI_GOAL`, `AI_BLOCK_OK/GO` and `AI_DODGE_OK/GO` ran **0 times** (P7: 6 block/dodge checks),
    and every `HITEV` had `block=0`. These `AI_Infantry` never ran their block, dodge or counter AI in this fight, for
    the stock strikes or ours, so a synthetic swing still has no baseline.
  - Stimuli seen: an orc's attack start registers `CharacterAttackSound` at Talion (201 times); every recoil start of
    Talion registers `PlayerHit` at his attacker (41).
  - Next: a shield `AI_Defender` or a captain (they block) for the baseline; `swingevent ... cur` while Talion is not
    recoiling (one attacker); then step 3 (`srvatk`, not built).
- **CV2, hit-stop at 120 ms: the animation stands still and the attack lands late by the hold. The recoil case was not
  tested again.**
  - `hitstop 120 onbpw` started on orc 0's counter prompt at Talion (`AI_Infantry_Melee_Front_1_RR`, 1.8 m). The
    playing tracker (tracker 1, FindTracker of `Combat+0x17c`) stayed at 33 ms through the hold (4 samples, `+0 ms
    during the hold`), the orc's timer frames were 0 ms, and `its timer is back` came after 0.133 s.
  - Its `APPLYDAMAGE` came 0.916 s after the hold started = 0.933 s after BPW instead of 0.800 (+133 ms: the hold
    rounded to 8 frames). No warning; the orc fought on normally.
  - `dealhit auto;hitstop 120`: `dealhit auto` took Talion's current "attack", his recoil `PC_Recoil_Light_R2L_F`, as
    the record. It did 1 damage and caused no hit reaction, with the hold (seq 43) and without it (seq 41). The hold
    froze that orc's tracker 0 at 1666 ms (+0 ms) and restored it.
  - So whether a 120 ms hold keeps the hit reaction stays P7's answer: the reaction started at the hold's end; 600 ms
    lost it.
  - Probe nit: `dealhit auto` must skip `PC_Recoil_*`. The player's recoils are Combat attacks; the stock ones even
    deal 1 damage to the attacker (10 times here).
- **C7, the counter by input: no counter.** `autocounter CombatPrimary02 3` pressed 13 times, each 17-34 ms after a
  window opened (10 melee, 3 arrows; `VIN press ... enabled=1 active=1`, 3-4 ticks, `polled=0`). No player attack
  followed; each time Talion took the hit ~0.78 s later and recoiled.
  - Most likely the wrong command for KBM mode **[med]**. The game was in KBM mode (`padmode=0`), and CombatPrimary02
    has no KBM key (run 3's bindings: pad Y only). The counter prompt over the orc is the right-mouse glyph (shot 48),
    i.e. CombatKBM09 (144, RMB).
  - Talion had recoiled 0.2-2.2 s before each press. That does not explain it: a stock strike started 0.97 s after a
    recoil, and 2 presses came with the FSM out of combat (`fsm 6/7`). A recoil keeps `fsm 7/6` for ~1.4 s.
  - Next: `autocounter CombatKBM09 3`, or `padmode on` with CombatPrimary02 (P9: `autocounter CombatKBM09 3` against a
    spawned captain).
- **U7, the counter prompt: not a prompt-diamond entry.**
  - 74 `PROMPTS at a counter prompt at Talion`: 71 with 0 live entries, 3 with 2 = `UI_DropToHang` (CombatPrimary03,
    a ledge context prompt). `UI_DropToHang` also caused 12 of the 13 `PROMPTS changed` lines (on and off 6 times).
  - The counter prompt is the icon over the attacker's head (shot 48: the RMB glyph in a white flare).
  - `Combat.Debug.Prompt 1` with `conlog on`: no `[COMBAT|PROMPT]` line anywhere in RUN. `CONOUT` had 0 lines for every
    `exec`, `AIDebug.ListAIs` included: the console output slot `0x4b5e928` gets nothing from these commands.
  - Next: read the combat marker entries (re-hud.md 4.4, `FUN_00e05ea0`) instead of the diamonds.
- **P1q, the head among the attached models: it is not there either.**
  - `piecehide all`: 17 of 17 bits set. The mask `0x1ffff` reached the model, the renderable and the handle's node,
    and the render update consumed it 2 ticks later. Nothing vanished (shot 21: head, hair, cloak, quiver, sword and
    dagger all there).
  - `attached`: **7 entries, all weapon models**, 0 one level down, each with one unnamed piece:
    - 0 = the sword item object (`PC_Sword_01`'s entry, socket 24 on `scabbard_back`); 1 and 2 on the same socket;
    - 3 = the dagger item object (socket 23 on `Dagger_Sheath`); 4 and 5 on the same socket;
    - 6 = the bow item object (no node, at the model origin).
  - One shot per hidden child (shots 22-0..6 by piece, 23-0..3 by the render node's `+0x40` skip byte):
    - **child 1 removes the sheathed sword's hilt** (shot 22-1; also 23-1 by the node byte);
    - **child 4 removes the dagger's hilt** (22-4);
    - 0, 2, 3, 5 and 6 change nothing visible;
    - the head, the hair with its tie and the hood/cloak are in every shot.
  - So both hide paths work on an attached model, but there are **no head, hood or hair IDs**: Talion's head and hair
    are neither hChar's 17 pieces nor attached children.
  - Next (re-resolution.md 2.4, re-firstperson.md 3.3), any of:
    - find the render node(s) that draw the head (a deep-buffer hair node, `FUN_007feb00`; the part models of
      `FUN_0236b820`);
    - scale the `Head` node (28) to ~0 in the phase-2 pose through the `armpose` write point (A5 passed; this loses the
      head's shadow too);
    - the near plane (P3q, not tried yet).
- **A5, the phase-2 pose edit: pass.**
  - `armpose rot 60`, `ik 25 10 35`, `ik 30 55 40`, `off` at the tower top: about 210 frames applied per word, 0
    skipped.
  - Every read-back in the next player update had `R_Hand` 0.0 cm from ours (542 logged checks); the IK targets were
    reached exactly.
  - Shots, no flicker:
    - 31: the right hand forward at hip height;
    - 32: the right arm raised to the shoulder;
    - 33: back to idle;
    - 30: the turn about the forearm keeps the hand's position, so it is not visible from behind.
  - The sword check is void: the sword was sheathed on `scabbard_back`, outside the edited subtrees (0.0 cm from both
    poses).
- **IK1/A7, the engine's arm IK: none on Talion.**
  - `ikwatch`, 20 s idle + 90 s of fight (3 strikes, 39 recoils; it ended before the death): 0 IK setups. The 4
    `Modifier.IK` slots of his manager stayed `in_use 0` throughout (5401 chain updates each).
  - `IK_LHand`/`IK_RHand` stayed at the model origin (0, 0, 0) in all 218 samples, 33-72 cm from the hands.
  - `ikcb`: 0 node callbacks added or removed on Talion (1910/1908 on other objects).
  - So the engine places no arm by IK in idle or melee, and G11 has nothing to blend out there. Ledges, climbs and
    grabs are not covered.
- **W1, the sword model:** `PC_Sword_01` has 3 nodes (`root`, `sword_handle` local (0, -23.55, 0), `sword_tip`) and 8
  sockets, all on `sword_handle`.
  - The blade lies along the model's +Y; **the tip is 100.7 cm from the object's origin**.
  - Socket `local` y values: 100.45 (the tip), 38.45 (mid-blade), 2.3 (five across, z -5.6..+5.65: the guard) and
    -32.23 (the pommel end).
  - The attach entry (sheathed): node 131 (socket 24 on `scabbard_back`), offset (-0.02, -14.07, 0.01), no rotation,
    no user offset.
  - The grip entry in the hand (socket 16 on `R_Weapon`) needs `wnodes` with the sword drawn.
- **UP1-UP4, the mouse injection (R1): it hovers and clicks in KBM mode. The pad guard was not tested.**
  - Main menu (`pl=255`, menu state 15): `pointer 136 499` gave `MOUSE view move (m_GameUI) result=0x3` and `MOUSE gfx
    move`, and OPTIONS lit up with its description (shot 07). `pointer 117 266` lit up PLAY (shot 08). The main-menu
    click did not run (that step ran in the world, see above).
  - Pause menu (KBM mode, opened by the cmd's Escape):
    - the pointer left at (136, 499) hovered OPTIONS as the menu opened (`MOUSE guard hover ... pad=0 -> 0`, shot 82);
    - `pointer 147 557` lit up QUIT (shot 83);
    - `click` gave `MOUSE view down/up result=0x1` and `MOUSE guard click pad=0 -> 0`, and **the Quit Game dialog
      opened** (shot 84).
  - The pad guard: seq 51-56 (`padmode on`, hover, click; `padguard off`, click) ran during the death screens, with no
    menu open (`player-ui=1`, view down result 0): **not tested**.
  - UP2, `mstate`: view offset (0, 0). One word later the movie's own mouse equals our pixel position at (0, 0),
    (1919, 1079) and (960, 540): 1:1, no flip. UI 1920x1080; the GFx stage is 1280x720 (hitscan's stage point = pixels
    x 2/3).
  - UP3: no `MOUSE sys` line; the harness moved no mouse while `mouselog` was on.
  - UP4, `hitscan`:
    - main menu: 6 components, hit = component 4 (OPTIONS), focus 0 (PLAY);
    - pause menu: 6 components, hit = 5 (QUIT), focus 4 (OPTIONS, hovered before);
    - the two FAILs (`player UI state 1`) ran in the world.
  - `mcmd` did not run.
- **T6, `surf`: the ray hits Talion first.**
  - From the head along his forward it hit `Flesh_Player` (surface 59251, Talion's own object) at 0 cm; from the
    camera along the view it hit Talion at 135.9 cm.
  - The surface record readout works (`Flesh_Player`, CanWallClimb 0, IsSlippery 0). The normal (0, 0, 0) and the
    fraction 0 are not filled in.
  - Probe nit: skip the player's own body (a filter, or a start point outside his capsule).
- **T7, `grav off`: it holds Talion up, and `grav on` does not release him.**
  - `grav off;pmmove 0 30 0`: the root rose 30.0 cm in one tick and stayed there for 3.6 s. TRAV showed `gravity=0`
    (`pc+0x60`), `fsm 6/1`, `supported=1` and the `Gnd Idl` tags: the graph did not notice.
  - After `grav on` (216 calls had been overridden) `pc+0x60` stayed 0, and Talion hung 30 cm up for 3.5 s more, until
    the fight's Teleport set it back to 1.
  - This fits re-traversal.md 2.4: `PlayerMovement::vf_06` returns the current flag, so a forced 0 latches. A mod has
    to write `pc+0x60 = 1` when it gives gravity back.
- **T8/T9, `mount`:** not mounted, 0 lines (smoke test).
- **The trav fix:** `head_y` 5474.5-5476.0, `head_dy` 71.9-73.5 cm (one -1723 in the Teleport's frame).
- **The dagger fix:** not exercised (P8 has no `dagger` word).
- **CI1-CI3, projectiles: answered.** `pick Archer` found an `AI_Archer` 0.8 m from Talion; then `aggro 2 Archer`.
  - Arrows `Orc_Archer_Proj`: speed 2800 cm/s (48 cm per frame), range 20000 cm, gravity 1 with weight 10 (no visible
    drop over 3.9 m), homing 0, spline 0, immediate 0. They spawn at the attack's `APPLYDAMAGE`.
  - One arrow hit Talion at `PC_RightArm` (an earlier one did 42 damage); one hit an orc in between (`AI_Infantry`
    0x1bb4fd40).
  - Thrown spears `Orc_Spearman_Proj`: 3000 cm/s, range 2000 cm.
  - `projblock 3`: the next arrow (point blank, 0.55 m) got `+0x228 = 1` at its first update and was gone. No
    `PROJHIT`, no damage, no recoil: the next hit on Talion, 1.03 s later, was a melee one.
  - `projreflect 3 own`: a spear was turned at 2.58 m, aimed at the spearman's root + 120 cm (homing, spline and
    target cleared; the shooter = the player's Combat). It flew straight through that point at 30 m/s (the `fly`
    lines stop at its 2000 cm range) without hitting him. 1.75 s later it hit an object 0x1ca48a60 ~52 m away,
    credited to the player (`PROJHIT shooter=... (player)`).
  - So a reflected projectile flies back and counts as ours, but it missed the spearman: he moved, or the thrower is
    on the spear's pass-through list (re-combat-items.md 1.3). A VR deflect needs a lead or homing on the shooter.
- Others:
  - C9: 20 candidates at the tower, 50-51 in the camp.
  - `timers`: in the camp 48 orcs on their own timer and 2 on the simulation timer (the first non-own orcs seen).
  - U1, U2, U8, U10 and R1's baseline at the main menu: as in P7.

Register status after P8 (section 1 updated):

| Status | IDs |
|---|---|
| Answered | AG1, C6, CI1-CI3, A5, A7/IK1, A8, UP2, UP4 |
| Partly | W1 (the blade, not the grip in the hand), UP1 (KBM hover and click; not the pad guard), U7 (not a diamond; the marker entry open), T7 (the latch), CV2 (the animation stands still; the recoil case not tested again) |
| Open | P1q (no head IDs), CV1 (no AI reaction to any strike), C7 (no counter: KBM command?), UP3, T6 (the ray hits Talion), T8/T9, C3-C5, U2's alpha, U10 |

### P9 - test orcs from our code, the block/dodge trigger, a shield defender, the arena light, the bow's modes and aim ray

**Why now:** P8 left CV1 open. No orc ran its block, dodge or counter AI, for the stock strikes or ours, and three
attackers hitting Talion every ~2 s gave no clean baseline. `re-combat-spawn.md` (0f2ea6a) found why and what to do
instead:
- the trigger is not a stimulus but the target's **AIBlockTable roll** at the attacker's attack start (chance 0.5 by
  default); on success `FUN_029ac890` writes an incoming-attack fact, and the counter goal is relevant only while the orc
  is out of its own attacks;
- test orcs on demand: the server's debug spawn `FUN_029a4830`, and a Nemesis captain from the current faction target;
- a shield blocks a frontal hit at damage time with no AI at all;
- a quiet arena 53 m east of the save's tower.

`re-bow.md` (5c7b476) asks for `wmdump` (the weapon-mode commands are data) and the cheapest bow-ray test (the free-aim
camera call). All of it fits one boot.

**The words** (the probe's header has the full syntax):
- read-only: `blocktable [N] [ATTACK|last]`, `aiblockwatch on|off`, `templates [SUB]`, `tod`, `weathers`, `wmdump`;
- active (spike1's guard): `spawn WHAT[,WHAT] [CM] [LEVEL]`, `spawncaptain [CM]`, `incoming [roll] [ATTACK|last]`
  (alias `aiattacked`), `blockroll force`, `wmhold TOKEN[,TOKEN] [MS]`, `aimray YAW PITCH [hand] [SECS]`;
- undo words (no guard needed): `despawn [all]`, `blockroll off`, `aimray off`;
- P8 fixes: `dealhit` takes the picked (or just spawned) orc first, else the nearest as before; `dealhit auto` skips the
  player's `PC_Recoil_*` records (P8: a recoil did 1 damage and no reaction).

Which proposals are in:
- re-combat-spawn.md 5: **all ten**. `aiattacked` is named `incoming` (the old name stays an alias). `spawncaptain`
  takes the current faction target, so the row and column go through the console first (`exec Faction.SetTarget R C`;
  `exec` logs it like any console call). `spawn` and `wmhold` take several names, since the template and mode names are
  only known at run time.
- re-bow.md 6: BW1 `wmdump`, BW3 `aimray`, plus `wmhold` (the stock way into the aim: the aim mode's own command,
  found at run time). BW7 rides along: every `AIMRAY` line carries Talion's yaw.
- Left out of P9:
  - BW2 `wmlog`: seven debug flags and three more hooks. `wmdump`'s commands plus `cmd`/`wmhold` answer which command
    enters which mode.
  - BW4 `bowfire` (our own shot through `FUN_02332c50`): next, once BW3 shows where the stock arrow comes from.
  - BW5 `bowmode` (forcing a mode): `wmhold` takes the stock path first.
  - BW6 `tokens`.

**How a test orc is made** (re-combat-spawn.md 1.2-1.4, read again for this run in the asm at `0x27b49ba..0x27b4b1a`
and `0x27b5028`):
- The template:
  - an exact template name (`FUN_02153910`, the same shape as the attack lookup);
  - else the AI/Templates record that references the archetype record (`AI_Defender` ...) one or two levels down. Short
    names win, and names with Captain, Warchief, Bodyguard, Quest, Slave ... lose. `templates` builds the cache: one
    game-thread scan, cut at 2 s.
  - else the best name containing the stem (`Defender`).
- The spot: CM ahead of Talion in the xz plane. A G21 ray from 2 m above to 6 m below (flags 0x50, filter 0xc, as the
  debug spawn's camera ray); no hit = no spawn. The orc stands at the hit + 10 cm (`DAT_033cc610` = 10.0), turned to
  face Talion.
- The call: `FUN_029a4830(hit object, "DebugSpawnedAI", xf[7], template, 1, 1, 0, member|0, 0)`. Arguments 7-9 go on
  the stack as at `0x27b49cc..0x27b49d5`. A null hit object gets Talion's HOBJECT instead: the function only takes the
  object's world from it (objsys vtbl+0x118).
- The debug path's post steps:
  - the result's HOBJECT class list must hold the NPC class `0x4c52780` (else FAIL: placement refused, or a prop);
  - `npc->vtbl+0x288(npc, FUN_022a2a80(&pos, 0, 0), 1)` (the slot checked: `NPC_SETZONE`);
  - a member: `FUN_02975eb0(npc, fm, 1)`, `FUN_022cdad0(fm, 1)` if `fm+0xa0`, its level `fm+0x340`; otherwise LEVEL or
    the debug spawner's `*0x4c471cc`;
  - `FUN_026ee7c0(Combat, level)`.
  - Left out: the branded bit and the `0x4c471c0` name step.
- The new orc becomes the pick, so `aggro`, `dealhit`, `incoming`, `blocktable` and `hitstop` act on it. `SPAWNCHECK`
  lines 1 and 3 s later: a valid Combat/NPC pair, its record, attack and combat target.
- `spawncaptain`:
  - the faction manager comes from `G->vtbl+0x78` (G = `*0x46b7310`; refused unless the slot points into the game's
    code) and its current target from `FUN_023d3f80`;
  - refused if `fm+0x3a4` bit 1 is set (in the world: the debug spawn refuses too);
  - the template is `FUN_022d71b0(fm)`.
- `despawn`: objsys vtbl+0x58 (`OBJSYS_REMOVE` = `0x8c6f60` -> `0x8cdbd8`). It queues the HOBJECT on the object
  system's removal list and sets its `+0x5c` bit 3, as the game removes objects. Not a kill: no XP, no Nemesis credit,
  no Steam stat.

**The block/dodge trigger, hooked** (re-combat-spawn.md 2.1; asm `0x26eb801..0x26ecd6f`, `0x26ed66e..0x26ed784`):
- `P_AIB_GATE` (`0x26eb818`, `FUN_0229aef0(node, attack)`): only when the target is an NPC and the attacker has an
  owner. Logs passed (not a block, node type in mask 0x883d) or refused.
- `P_AIB_REC` (`0x26ecbc7`, `FUN_026ff330(*(*(npc+0x10)+0x70))`): after the target Combat's `+0x268 == 0` check, i.e.
  the roll path is reached. Logs the record and its AIBlockTable.
- `P_AIB_ROLL` (`0x26ecd50`, `FUN_00c73440(0.0, 1.0)`), the only roll call. Every path jumps to `0x26ecd3f` with the
  chance in xmm2: an entry's BlockChance, or 0.5 when the entry has none or the Entries are not records. The caller saves
  xmm2 at `(%rsp)` across the call, so the hook reads the chance there and returns the roll in the saved xmm0.
  `blockroll force` returns the low bound 0.0: every roll that is made succeeds, and a table with no matching entry still
  makes none.
- `P_AIB_NOTIFY` (`0x26ecd6a`, `FUN_029ac890(*(c+0x70), npc)`).
- `P_GOAL_REL` (CAIGoalCounterMelee vtable `0x3a4e0c0` slot 3 = vf_03 `0x29d6b90`): void, it writes the relevance byte
  `goal+0x10`; logged on change per goal.
- `P_SHIELD` (`0x28177c4`, `FUN_028adf30(shield item, attack, ..)` in the damage context): a PREWRAP, log only (item, HP
  `+0xa8`, attack). The result is the HITEV line's block byte.
- `blocktable` reads the same data as the roll without calling the game. The schema entries are those the asm indexes
  (`*0x4ba0ca0` 56 AIBlockTable, 61 WeaponModeSetup; `*0x4ba6460` 0 Entries; `*0x4ba64e0` 0 Category, 1 BlockChance;
  Combat/Attack 5 = the categories), and the DONE line prints their names. With an attack it names the entry that
  attack would roll and the chance.
- `incoming` is step 1b (re-combat-spawn.md 2.6): `FUN_029ac890(the player's CCharacter, the orc)` from our code, after
  checking the NPC (vtable, working memory `+0xc70`, brain `+0xc78`, `+8` = its HOBJECT). With `roll`, our own roll from
  the orc's table with the attack's categories decides first, keeping the stock odds.

**The arena** (re-combat-spawn.md 3):
- `minhealth 20`, `exec Teleport 40400 3900 -21600`, then `exec Player.CanBeAttacked 0` from there on. Only the
  captain's 20 s counter test turns it on, and it is off again before the captain is removed.
- `orcs`: no armed AI within 30 m is expected.
- The light: `tod` at the tower and at the end. One `exec GameServerShell.AdvanceTimeOfDay` comes after the fights (its
  step size is an open question), so it cannot darken their shots.

**The bow** (re-bow.md 1.1, 1.4):
- `wmdump` walks Combat/PlayerWeaponModes/Modes (`*0x4b567d0`). It writes each mode and each component record field by
  field through the schemas of re-bow.md's schema map: PlayerWeaponMode `*0x4bb2320`, WeaponModePredicate `*0x4bb35a0`,
  Data `*0x4bb33e0`, SlowMo `*0x4bb3040`, FreeAim `*0x4bb2ca0`, VisionMode `*0x4bb3120`, UIMode `*0x4bb2f60`,
  PlayerWeaponModeSetup `*0x4bb21e0`. The command comes from the game's own getter `FUN_00e24570`: the stored value goes
  through `FUN_025aa7c0`, so the raw field is not the id.
- `wmhold Bow,freeaim 7000` presses the first matching mode's command for 7 s: a mode named Bow, else the first with a
  FreeAim component.
- `aimray` post-hooks `P_BOW_CAMXF` (`0xa9a1d0`, `FUN_00a5b8b0(cam, &xf)`). The free-aim update calls it only while
  `FA+0x3d8` is set. The hook turns xf by YAW about world +Y and PITCH about its own X, and with `hand` moves the origin
  to R_Hand (read on the probe tick, one frame old). `AIMRAY` lines twice a second: the calls, the camera's and our
  transform, `FA+0` (aim point), `FA+0x18` (ray), `FA+0xa8` (target), `P+0x214`, Talion's yaw.

- Commands: `scripts/cmds/probe-P9.cmd`: run 4's menu, load and quit steps; the rest are `probe` lines with a shot before
  each key step. About 6.5 minutes of game time: 356 s of waits, 39 shots, 36 probe sends. `--hold 600` = the 10-minute
  cap.
- Launch (as P8, with the hold at 10 minutes):
  ```
  cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
    --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
    --game-env SOMVR_COMFORT=1 \
    --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p9-world;orcs;timers' \
    --cmds scripts/cmds/probe-P9.cmd --hold 600
  ```
- Order (game time from the cmd file's waits; the `world` mark at ~166 s):
  1. Main menu (~88 s): `mark p9-menu` only.
  2. Load as in run 4.
  3. Tower top (~185 s): `tod;weathers;orcs;exec Faction.Dump @cout\somvr-faction-P9a.txt`, `templates`,
     `wmdump;blocktable 3`.
  4. Arena (~193 s): `minhealth 20;exec Teleport 40400 3900 -21600` (shot 20), `exec Player.CanBeAttacked 0;orcs;trav 3`.
  5. Defender (~202 s):
     - `spawn AI_Defender,Defender,Shield 400` (shot 21), then `blocktable 1 last;aiblockwatch on;aiwatch on;aggro 1`
       (shot 22);
     - five `cmd CombatPrimary01` 1.5 s apart (shot 23), `blocktable 1 last`;
     - `blockroll force` and five more strikes (shot 24), `blockroll off`;
     - three times `incoming`, then `dealhit auto` 0.3 s later (shot 25); `incoming roll last;dealhit auto`;
     - `aggro off;despawn` (shot 26).
  6. Berserker (~244 s): `spawn AI_Berserker,Berserker 400` (shot 30), `blocktable 1 last;aggro 1`, `blockroll force`
     with four strikes (shot 31), `blockroll off` and two `incoming` + `dealhit auto` (shot 32), `aggro off;despawn`.
  7. Captain (~269 s):
     - `exec Faction.SetTarget 1 3`, `spawncaptain 400` (shot 40);
     - `blocktable 1 last;aggro 1;exec Player.CanBeAttacked 1`, then `autocounter CombatKBM09 3` for 20 s (shots 41/42;
       KBM mode, P8's C7 next step);
     - `autocounter off;exec Player.CanBeAttacked 0`, `incoming;dealhit auto`;
     - `aggro off;despawn all;exec Faction.Dump @cout\somvr-faction-P9b.txt` (shot 43).
  8. Bow (~309 s): `projlog on;wmhold Bow,freeaim 7000` (shot 50), `aimray 20 5 hand 6` (shot 51), `cmd CombatKBM05`
     (LMB, shot 52), `cmd CombatSecondary04` (pad RT's command, shot 53), `aimray off;projlog off;release`.
  9. Light (~321 s): `tod;exec GameServerShell.AdvanceTimeOfDay`, `tod` (shot 60).
  10. End (~327 s): the loggers off, `despawn all;orcs`, then `minhealth off;release` (shot 61), with CanBeAttacked still
      0 and no spawned orc left. Then run 4's quit.
- Pass / fail per question:
  - **SP1 (an orc from our code):**
    - pass = `SPAWN seq=N template '...' for 'AI_Defender': the best of n templates referencing 'AI_Defender'`, `SPAWN
      ... 0: npc=... record 'AI_Defender' ...; picked`, and `SPAWNCHECK 0 ... a valid orc alive=1 dead=0 ai=1 dist=4.0 m`
      at 1 and 3 s, with the orc in shot 21.
    - The same for `AI_Berserker` (shot 30). `templates.tsv` lists each template's archetype for the next runs.
    - fail:
      - `FAIL ... spawn ...: no template named ...`: the archetype is neither one nor two levels down and no name has the
        stem. Read `templates.tsv`; the next run names a template.
      - `the ray down ... hit nothing`: the arena is not under Talion's forward (shot 20).
      - `FUN_029a4830 returned 0`: placement refused (the nav check of `a5 = 1`): spawn nearer or elsewhere.
      - `NOT a valid Combat/NPC pair` in SPAWNCHECK: the orc is still being built; the 3 s line decides.
  - **SP2 (a Nemesis captain):**
    - pass = `SPAWN ... captain: ... target member ... (in the world 0) level L ... template '...'` and a spawn as SP1
      with `(bound to the faction member)`.
    - `somvr-faction-P9b.txt` vs `-P9a.txt`: that member's entry changed, and the name over his head in shot 40 is the
      dump's name for row 1, column 3 (the argument order, re-combat-spawn.md 6).
    - fail = `no current faction target` (the SetTarget order or arguments) or `already in the world`.
  - **AB1 (the stock roll against a defender):**
    - pass = per stock strike `ATK ... PC_HC_...`, `AIBLOCK gate ... -> passed`, `AIBLOCK reached target=spawn 0
      'AI_Defender' ... AIBlockTable '...'`, `AIBLOCK roll ... chance=c roll=r`, and for r < c `AIBLOCK notify ... (THE
      PLAYER)`.
    - Then `AIBLOCK goal_counter relevant 0 -> 1` and `AI goal_counter` / `block_valid result=1` / `block_go` with an
      `AI_Defender_Block_*` attack in `orcs` or the `AI` lines.
    - Partial passes, each a finding:
      - `-> refused` at the gate: the player's strikes are blocks or the wrong node type;
      - no `reached`: the defender's Combat `+0x268` is set;
      - `reached` but no `roll`: no table entry matches the strike's categories; `blocktable 1 last` names them.
  - **AB2 (forced: does a notification make the orc block / dodge?):**
    - pass = after `blockroll force`, every reached roll gives `-> notify` and the defender blocks (`AI block_go`, an
      `AI_Defender_Block_*` start, `HITEV block=1`).
    - The berserker dodges (`AI dodge_valid/dodge_go`, `AI_Bzrk_Dodge_QT_*`) or does nothing (its pools have no block).
    - fail = notifies but no `AI` line in 2 s = the goal is not relevant (`AIBLOCK goal_counter` stays 0: the orc is in
      its own attack, or its brain does not re-plan).
  - **AB3 (`incoming` without an attack, the VR swing's step 1b):**
    - pass = `INCOMING ... replan 0 -> 1` and then the same AI reaction as AB2, before the `dealhit` 0.3 s later.
    - `incoming roll last` logs our roll from the table.
    - fail = no AI line: the fact alone is not enough (the actions need the attacker in front, CV 2.2: shot 25 shows
      where Talion stands).
  - **SH1 (a dealhit against a shield defender):**
    - pass = `AIBLOCK shield item=... hp=h attack='...'` and `HITEV ... block=1` for the defender while it faces Talion.
    - `block=0` with a shield line = the direction or category test failed (`CannotBlockCategories`); no shield line =
      the hit never reached the shield test (the damage context took another branch).
  - **C7 again (the counter in KBM mode):** pass = `AUTOCOUNTER ... -> CombatKBM09` followed by a player counter attack
    (`ATK ... PC_..._Cntr_...` or a sync) against the captain. Presses with no counter = C7 stays open (pad mode next).
  - **AR1 (the arena and the light):**
    - `orcs` after the teleport: no armed AI within 30 m.
    - `TOD ... Record(...) GDBIndex(i) TOD(name)` before and after one `AdvanceTimeOfDay` gives the step.
    - `WEATHER` names = the inputs for `GameServerShell.ChangeWeather` next time.
  - **BW1 (`wmdump`):** pass = `WMODE i 'name' command id 'Name' HeldDuration=... IsToggle=...` lines with the aim, vision,
    shadow-strike and drain modes and their commands, `wmodes-N.tsv` with every component field, and `WMSETUP` = the
    player's mode list. fail = `no ... Modes category`, or `command -1` everywhere (the getter needs another argument).
  - **BW3 (`aimray`):**
    - pass = `WMHOLD ... mode '...' command ...` and `AIMRAY ... turned: n calls, n turned` with `FA active=1`.
    - The FA ray direction follows our direction, the aim point moves with it, and `P+0x214` equals it.
    - A stock shot (`PROJ new` after `cmd CombatKBM05` or `cmd CombatSecondary04`) spawns near L_Hand and flies along
      the turned ray.
    - `0 calls` = the free-aim update did not run: the held command is not the aim, or the mode needs more (Focus,
      `RequireToken`).
  - **BW7:** Talion's yaw in the `AIMRAY` lines while aiming: it turns toward the aim point or not.
  - **The P8 fixes:** `DEALHIT ... (the picked orc)` for the spawned orcs; no `last player attack 'PC_Recoil_...'` in a
    `dealhit auto` line.
- Notes:
  - `spawn`/`spawncaptain` add orcs to the world, which `despawn` removes. A captain spawned through his member keeps
    name, traits and Nemesis credit (only in the run's save copy, which the guard keeps from being written).
  - `blockroll force` changes every AIBlockTable roll until `blockroll off` or a new player.
  - `incoming` adds a working-memory fact and sets the brain's re-plan byte.
  - `aimray` changes the free-aim ray only.
  - `wmhold` is a command press like `cmd`.
  - `exec Player.CanBeAttacked 0` and `AdvanceTimeOfDay` are console state of this boot.
  - Kills: none intended; a `dealhit` may still kill a weak test orc.

#### Results, P9 (2026-10-20 09:46, `logs/boot-check-20261020-094644`)

Run with spike1 + the probe from `logs/build-p9` (main `f96e10d`), `SOMVR_COMFORT=1`, `probe-P9.cmd`, the user's save
copy; 380 s of game, and the game quit by itself (exit 0) after the cmd's quit. GameData.sav byte-identical (the guard
caught 3 writes), the install unchanged apart from the moved CEF leftovers. No crash; somvr.log (spike1) and probe.log
(2239 lines) have no WARN/FAIL. 40 shots. Analysed offline from the logs and shots.

**Verdict:** test orcs from our code work: a shield defender, a berserker and a named Nemesis captain (SP1, SP2). The
defender's shield blocks every frontal hit, the stock strikes and our `dealhit`, with no AI and no damage (SH1). Our
`incoming` made the defender raise its block (AB3; 1 of 3 calls, the other two came during its own attack). The
counter by input works in KBM mode with `CombatKBM09` (C7). `wmdump` found the bow's aim command: **124
`CombatSecondary03`, held 0.2 s** (BW1). Not answered: a berserker's dodge, any reaction of the captain, the aim ray
(BW3, BW7) and the fire command.

**Surprises:**
- **`AI_Defender` and `AI_Berserker` have no AIBlockTable** (an empty name, 0 entries). The game never rolls for them,
  so neither `blockroll force` nor SOMVR_COMBAT_REACT's copy of the roll can notify them. The `Infantry` table is the
  only one met (6 entries, chances 0-0.25), and a forced notify did not make an infantry block or dodge (AB2).
- **The arena was not quiet.** Four `AI_Infantry` 31-45 m west joined within a minute (shots 26-42). Talion's own
  targeting sent the berserker step's strikes and all four counters at them.
- **`exec Player.CanBeAttacked 0` stopped no attack.** The spawned defender and berserker hit Talion 5 times (21-26
  each), and the patrol's thrown spears hit him too. `minhealth 20` held him at 20 from t=247911 on.
- **The captain's showdown swallowed the bow step.** Ghâm began his Nemesis showdown at t=247978, 30 s after the
  spawn. `despawn all` in the middle of it left the presentation running: its camera and name card are shots 43 and
  50-53. Every command pressed in it logged `active=0 input=0`, so the whole bow step ran inside it. It was gone
  after the time step (`AdvanceTimeOfDay`, ~15 s later; shot 60, night).

- **SP1, an orc from our code: pass, two archetypes.**
  - `templates`: 122 AI/Templates in 0.007 s (`probe-out/templates.tsv`). By archetype: AI_Defender 9, AI_Berserker 6,
    AI_Infantry 12, AI_Spearman 5, AI_Archer 5, AI_Slave_Human 5.
  - `spawn AI_Defender,Defender,Shield 400`: template `Orc_Defender`, the best of 9 (the warchief and cinematic ones
    lost). It stood 3.6 m away. `SPAWNCHECK` was valid at 1 s (`AI_Defender_ReactToSurprised_High`, combat target
    TALION) and at 3 s. Shot 21 shows it with its shield and polearm.
  - `spawn AI_Berserker,Berserker 400`: `Orc_Berserker`, the best of 6, at 1.6 m. Valid at 1 and 3 s
    (`..._ReactToSurprised_Unsheathe`, then `AI_Berserker_CantPath_Taunt_C`). Shot 30 shows it, axe raised.
  - All three orcs stood 1.8-3.0 m from the ray's point (the placement moves them). The two came at level 0 (the
    debug spawner's default): pass LEVEL next time.
  - `despawn` removed each one cleanly (`removal queued rc=0`, gone from `orcs`).
- **SP2, a Nemesis captain: pass. `Faction.SetTarget` takes the row, then the column.**
  - `exec Faction.SetTarget 1 3` (rc 0), then `spawncaptain 400`: member 0x436bf940 (not in the world before), level
    10, template `Orc_Infantry`, bound to the member, record `AI_Infantry`, 4.0 m ahead, valid at 1 and 3 s.
  - The dump's row 1, column 3 is Ghâm: Role_Hook, Thuggish_1, level 10, 21 traits (among them `Block_Immune_Combat`
    and `Counters_Your_Counter_1`). The showdown's card reads "Ghâm the Hook" (shot 43). Shot 61: "Ghâm the Hook,
    Target Located at Ethir Poros".
  - P9b vs P9a: only his entry changed. PresentedToPlayer False -> True, IntelDataLevel 0 -> 1, InteractionScore
    0 -> 65, CurEncounterIndex 65535 -> 1, and three FactionEvents (ActiveSetInterrogationSubject,
    ActiveInitialPlayerThreat, ActiveUnkPresentation). This is in the run's save copy only.
  - He never swung at Talion. In the 30 s before his showdown he threw one spear and otherwise taunted (`CantPath`,
    `CC`) and avoided. Then the intro started (`ShwdnIntro_Thg_Giggler_AN`, `dx_shwdn_GenericRole14_thg1_06`), 0.9 s
    after `CanBeAttacked 0`.
- **AB1, the stock roll: it runs for orcs with a table. The defender and the berserker have none.**
  - Defender, 9 stock strikes (`PC_HC_Lowest_L1_*`, 5 plain + 4 forced): the gate passed (node `PC_PoolAttacks`), then
    `reached ... AIBlockTable ''`, and no roll. `blocktable` shows `AIBlockTable '': 0 of 0 entries` for both
    `AI_Defender` and `AI_Berserker`.
  - The `Infantry` table (AI_Infantry, AI_Slave_Human, the captain): Block_Generic 0, PC_PoolAttacks_KB_EL1 0.25,
    `_EL2` 0.25, `_EL3` 0.10, Ranged_Generic 0, any attack 0.10.
  - Against a patrol `AI_Infantry` the stock strikes rolled at chance 0.100, which is either the EL3 entry or the
    catch-all (the same value; `blocktable` was asked with a recoil, see the nits). The game's rolls were 0.049, 0.810
    and 0.381. A won roll notifies `(THE PLAYER)` with `replan=1`, and the orc's goal turned relevant 266 ms later.
    The counters' sync starts rolled too, at 0.100.
  - The gate refuses the player's recoil and deflect nodes (`PC_Recoil`, `PC_Recoil_HitByRanged`).
  - Whole run: 78 rolls (12 forced), 22 won = 22 notifies; 33 shield tests.
- **AB2, does a notification make the orc block or dodge: not an infantry. The roll cannot notify the defender or
  berserker at all (AB3's direct call made the defender block).**
  - The three forced notifies on the patrol infantry (1.4 m) each gave `goal_counter relevant 0 -> 1`, then
    `dodge_valid result=0` and `block_valid result=0`. Each strike hit (10 damage, `block=0`).
  - `blockroll force` on the defender and the berserker changed nothing (no table, no roll).
- **AB3, `incoming` (`FUN_029ac890` from our code): the defender blocks. The berserker did not dodge.**
  - Defender, 3 calls 2 s apart (1.5-5.2 m), all `replan 0 -> 1`:
    - the first came during its `AI_Defender_Cone` (which hit Talion 0.2 s later), the second during its wall recoil:
      no goal;
    - the third came with the orc out of its attacks: `goal_counter relevant 0 -> 1` 17 ms later, `block_valid
      result=1`, `goal_counter`, then **`block_go` -> `AI_Defender_Block_Into`** (4.2 m, Talion not attacking);
    - the goal ran `block_go` twice more (1.7 and 2.5 s later, at 10 m) and dropped 4.2 s after the call.
  - Berserker, 2 calls: its goal turned relevant once its own move ended (183 ms and 0.98 s later). Then came one
    `dodge_valid result=0`, with Talion recoiling (`player_attack='PC_Recoil_Medium_D2U_B'`), and nothing else. Not
    conclusive.
  - Captain, 1 call during his showdown: nothing.
  - `incoming roll last` on the defender: `chance -1.000 (entry -1 of 0)`, not notified (right: no table, no roll).
  - So the notification works on a defender that is out of its own attack. The roll in front of it never fires for a
    defender or a berserker.
- **SH1, our hit against the shield: pass.**
  - All 4 `dealhit`s on the defender (0.3 s after each `incoming`, from 1.8-9.9 m) gave `HITEV block=1 damage=0.00`,
    with `AIBLOCK shield 'Orc_Defender_Melee_Breakable_Shield' hp=300.0`.
  - So did the 9 stock strikes: `block=1 damage=0.00`, the defender's `AI_Defender_Recoil_Block_Deflect_L` and
    Talion's `PC_Deflected_*` (shots 23, 24).
  - The shield's HP read 300.0 in all 31 lines. The shield test also runs at each strike's start (the same site), not
    only at the impact.
  - Caveat: `dealhit auto`'s record also did 0 damage to the unshielded berserker, so "no damage" rests on the stock
    strikes (0 here against 10 on an infantry).
  - After two of the dealhits the defender bumped into the rock wall (`AI_BIP_Recoil_IntoWall_B`, 10 damage to
    itself).
- **C7, the counter in KBM mode: pass.** `autocounter CombatKBM09 3` pressed 7 times (`active=1`).
  - 4 melee windows of the patrol infantry (0.7-1.8 m) gave 4 counters: `PC_Bip_Sync_Cntr_Knockback_B`,
    `_Knockdown_B3`, `_Knockback_B2` and `_Stun_F2` (node `PC_Counter_KBM`). Each started in the press's frame, 33-56
    ms after its window opened, at the attacker. Each sync's HITEV dealt 0-1 damage.
  - 3 thrown-spear windows (0.8-6.2 m) gave 2 `PC_CounterAttempt_Fail` (no target) and 1 nothing (the spear hit).
  - So P8's miss was the command: in KBM mode the counter is `CombatKBM09` (RMB). Pad mode (`CombatPrimary02`) is
    still untested.
- **AR1, the arena and the light: partly.**
  - After the Teleport: 3 slaves at 5.4-9.0 m and the nearest armed orc at 30.9 m (a pass by 0.9 m). But 4
    `AI_Infantry` at 30.9-44.7 m arrived within a minute.
  - The spot is at a rock wall (`CloseToWalls`/`ApproachingWall` tags). The berserker and the captain played
    `CantPath` taunts.
  - `Player.CanBeAttacked 0` stops neither the spawned orcs' melee nor the thrown spears (above).
  - TOD: `SON_Day02` (GDBIndex 2, Day) at the tower and before the step. One `AdvanceTimeOfDay` gave `SON_Night_01`
    (3, Night): **one step is a whole phase**. It came with a 0.5 s pause, replaced the NPCs around (34 -> 10
    candidates, all new) and showed "New Missions NOW AVAILABLE" (shot 60). For light, use `ChangeWeather`.
  - `weathers`: 79 Weather/Definition records (`probe-out/weathers.tsv`). Day without rain: `DayNoRain`,
    `AnyTimeNoRain`, `TEST_Day_01`..`_03`, `TEST_Day_NoWeather`.
- **BW1, `wmdump`: pass. The aim is 124; 125 enters the stealth mode, it is not a fire.** 50 modes
  (`probe-out/wmodes-6.tsv`, every component field). The player's own list (WMSETUP `Player_Character`): PC_SlowMo,
  PC_WraithPing, PC_WraithRanged_Uprbdy_Mounted, PC_WraithRanged_Uprbdy_Rails, PC_WraithRanged, PC_Stealth,
  PC_GrabDagger.
  - **Bow aim, `PC_WraithRanged` and all its variants: `CombatSecondary03` 124 (RMB / pad LT), held, HeldDuration
    0.2 s.**
    - Not a toggle, no token, no Focus gate. RequiredInventoryItem `CP_BasicWraith`, NativePredicate "BowEquiped",
      weapon slot 2. It exits on damage.
    - FreeAim: MaxTargetDistance 11700, AutoTarget 0.25 s / 0.15 of the screen, ModelNodeTestFirePos "LeftHand",
      EnableAimAtRotateObject 1, SmoothTimeForAim 0.
    - The vision mode `PingMode` (VM_Wraith) is on while aiming.
  - **`PC_Stealth`: `CombatSecondary04` 125 (LShift / pad RT), held, HeldDuration 0.** It takes the dagger slot (1);
    IntoAttack `PC_SheathMeleeToStealth`.
  - Wraith vision, `PC_WraithPing`: **`CombatSecondary01` 122 (V / pad LB), a toggle**, raw input. It is not
    `VisionModeToggle` 243. `PC_Radiance` is 122 held 0.85 s, with Focus >= 1 and the vision on.
  - `PC_SlowMo`: 176 `CombatHidden06` (no binding), a toggle, Focus >= 1, only together with an aim mode (SlowMo record
    `WraithMode`).
  - **The fire command is not in this data.** The shot is an attack in the aim mode's pools (its `Combat.Input`), so
    `wmdump` cannot name it. The in-game hint (shot 20): "Hold [RMB] + tap [Z] to Shadow Strike an enemy" (Z =
    `CombatKBM07` 142).
- **BW3 and BW7, the aim ray: not run.**
  - `wmhold Bow,freeaim 7000` held mode 0, `PC_Stealth` (125), for 7 s. No mode is named Bow, and the `freeaim` test
    is true for every mode: each has a FreeAim array of one element, a null record for most (WMODE's `FreeAim=1` is
    that count). A probe bug.
  - The step also ran inside the captain's showdown: the 3 presses logged `active=0 input=0` (the game polled the 125
    hold 3477 times).
  - `aimray 20 5 hand 6`: 13 `AIMRAY` lines, all with 0 calls, `FA active=0`, `P+0x214` 0. No `PROJ` line after
    `cmd CombatKBM05` or `cmd CombatSecondary04`. Talion's yaw stayed -96.7 deg.
- **The P8 fixes: both work; one new nit.**
  - 7 of 7 `DEALHIT`s took `(the picked orc)`.
  - `dealhit auto` skipped the recoils. With no other player attack, its fallback (the name match) took
    `AI_Warg_Attack_Light_F_Player_1` (score 6), which deals 0 damage to orcs.
- Probe nits for the next run:
  - `wmhold freeaim`: test the element for a non-null record, or name the mode (`wmhold PC_WraithRanged`).
  - `dealhit auto`: fall back to the last `PC_HC_*` strike, or name one.
  - `blocktable 1 last` named only recoils and deflects (`PC_Recoil_Light_ST_F`, `PC_Deflected_LL`, ...), never a
    strike. The same fix also decides EL3 vs the catch-all.
  - The `reached`/`roll` lines name the target by its record holder, which all `AI_Infantry` seem to share: the 4 rolls
    at the counters' starts (targets: the patrol) read `spawn 0 'captain'`. The `notify` lines (matched by NPC) are
    exact.
- Next: the bow alone, before any captain: `wmhold PC_WraithRanged 7000`, `aimray`, `cmd CombatKBM05` (presumably the
  KBM fire), and `CombatSecondary04` in pad mode. The berserker's dodge after `incoming` with Talion standing idle and
  no patrol near (another arena, or the tower top). The captain only after his showdown has ended (no `despawn` during
  it). The counter in pad mode.

Register status after P9 (section 1 updated):

| Status | IDs |
|---|---|
| Answered | SP1, SP2, SH1, C7 (KBM mode), BW1 (the mode commands) |
| Partly | AB1 (the roll runs where a table exists; the defender and the berserker have none), AB2/AB3 (a notification makes the defender block, through `incoming`; an infantry does nothing; no berserker dodge seen), AR1 (the TOD step, the weather names; the arena not quiet, `CanBeAttacked 0` stops nothing), CV1 (a defender blocks after our notification; no infantry reaction) |
| Open | BW3, BW7, the fire command, P1q, UP3, T6, T8/T9, C3-C5, U2's alpha, U10 |

### P10 - locomotion: the stock speeds, the player record, our velocity instead of the root motion, launches, the vault, Talion against an orc

**Why now:** `design-locomotion.md` (8bc9551) settles the user's speed principle (2026-10-20 ~09:30: 1:1, no delay in
gaining speed or stopping) on one hook, `LOCO_ROOTDELTA`: all of Talion's ground speed, starts and stops are animation
(the start clip's root motion is the acceleration), the 30 `Client.PlayerMovement` attributes hold no speed, and in
root-motion mode the solver has no velocity memory, so replacing vf_11's root translation gives speed from the first
tick and a stop in one. Its section 2.10 lists ten questions only a boot answers (LR1-LR10), and its build plan starts
with this run (LO-0) before LO-1 (the Root anchor) and LO-2 (the LOCO core). One boot fits all of them. P9's `wmhold`
bug is fixed on the way.

**The words** (the probe's header has the full syntax):
- read-only: `speed [SECS]`, `pmrec`, `heading clear [M] | open | wall [M] | back | fwd | cam | orc | YAW`,
  `vaultlog [SECS]`;
- active (spike1's guard): `walk MAG [MS]`, `loco const VX VZ SECS [world]`, `loco stick SECS [V]`, `fling VX VY VZ
  [world]`, `animrate R [SECS] [all]`, `npcpush [CM] [V]`, `pmrec set ATTR V`, `pmrec restore`, `pmrec base V`;
- undo words (no guard needed): `walk off`, `loco off`, `animrate off`, `pmrec base off`;
- the P9 fix: `wmhold` takes an exact mode name first, then a substring; `freeaim` = a mode whose FreeAim element is a
  record. P9's `wmhold Bow,freeaim` took mode 0, `PC_Stealth`, because every mode has a FreeAim list of one element,
  a null record for most. P10 names the mode: `wmhold PC_WraithRanged`.

Which proposals are in (design-locomotion.md 4.2): all eight, with these choices:
- `loco const` and `fling` take their components in the frame of the probe's heading (x right, y up, z forward), not
  in world axes (`world` gives those): P9's arena spot faces a rock, and no world axis is known to be free there.
- `heading` and `walk` are new. `heading` finds free ground with 16 rays; `walk` holds the stock stick along it and
  re-aims the stick through the camera's yaw every tick, so a following camera cannot curve the run. The drill's
  `move 0 X` steps of 4.2 became `walk X` along a free heading.
- `npcpush` walks into the orc with loco (2.5 m/s, the VR case) before the design's one-shot `pmmove`-style push.
- `pmrec set`/`restore` exec the console name `pmrec` found, and `pmrec base` writes the cache `pm+0x32c` directly
  (the design's "no reader found").

**How the words work** (design-locomotion.md 2; asm `0xab5e70`, `0xa513a0`, `0x208dd90`, `0x208de50`, `0x20c6ff0`,
`0xab8700`, `0x619a36` read again for this run):
- **`P_ROOTDELTA`**, the hook behind `speed` and `loco` (CALLWRAP on `0xab5ea9`):
  - The original `FUN_00a513a0(body, &delta)` runs first. `delta` = {pos[3] in Talion's frame, quat[4]} on vf_11's
    stack, the caller pre-sets qw = 1.
  - The game's delta is kept for `speed` (the sum and count since the last probe tick).
  - With `loco` on, for the player's body only and only in FSM 6/7, not mounted (`P+0x1d8`), not attached (`pm+0x1a6`)
    and with no sync action (`Combat+0x188` -> `+0x10`), the translation becomes `R(xf.rot)^-1 (v*dt)` with world y = 0.
    The rotation stays the game's (turn clips still turn Talion).
  - `xf` comes from `P_PMROOT` (the slot hook around vf_11 keeps it in a thread-local), else from the root object.
  - `dt` is the player object's timer (`HOBJECT+0x68`, frame seconds at `+0x10`), else the game timer, else the wall
    clock; the LOCO end line counts each source.
  - Counted: skips (FSM, mount/attach, sync), the translation lock `mgr+0x62a` (vf_11 then zeroes our translation unless
    an attack runs, `pm+0x38`), the rotation lock `+0x629`.
- **`speed`**: one row per probe tick in `speed-N.tsv`:
  - the root's horizontal/vertical speed (displacement / wall time between probe ticks), the solver's velocity
    `pm+0xe0..+0xe8`, `pm+0x1a2`, FSM, `FUN_00a36db0` (supported);
  - `RawMoveMag`, `Game_Velocity`, `Game_SmoothedVelocity` and `DeltaYaw` read back with the game's own
    `FUN_0208dd90(mgr, name)` and `FUN_0208de50(mgr, var)` (`mgr = *(PlayerBody+0x3b8)`, as `FUN_00a3c8d0` writes them);
  - the probe's held stick; loco's asked speed; the game's root delta (sum, count, speed) and the one vf_11 went on
    with; the state tags on change.
  - SPEED lines every 0.5 s and on a tag change.
  - The end line: the plateau (the mean over the last second of the hold, or its second half), the first tick whose
    3-tick mean reaches 90 % of it, and the time and distance from the stick's release to two ticks under 5 % (at least
    10 cm/s). The stick is the probe's when it held one in the window, else `RawMoveMag`.
- **`pmrec`**: the record `pm+0x448` through the descriptor array `*0x4c254e0` (`_INIT_1317` stores `0x4c25520` there
  and the count 30 at `0x4c254e8`; each descriptor 0x30 bytes: `+0` name, `+8` hash, `+0x18` state, `+0x28` index):
  - the fast path when `*(int*)(D+0x18) >= 2` (`entry = *(rec+8) + 16 * *(u8*)(D+0x28)`), else the hash search;
  - type 2 floats (with count 3: a vector), 3 ints (ms, shown in s too), 1 bools, 4 strings, 0xa records;
  - the console find of `PlayerMovement.ATTR`, `Client.PlayerMovement.ATTR`, `ATTR`;
  - the caches `pm+0x32c` (BaseSpeedMultiplier), `+0x450`, `+0x454` and the proxy's `pc+0x58`/`+0x60`;
  - `pmrec set ATTR V` keeps the value from before the first set (for `restore`) and logs the record right after the
    exec and 3 ticks later.
- **`heading`**: 16 directions from Talion's facing, each the nearer of two G21 rays from the root at 40 and 100 cm (the
  surf query, flags 0x58), up to M+5 m (`clear`, `open`) or M (`wall`).
  - A hit on Talion's own object, or at the start point (P8: the surf ray hit his `Flesh_Player` at 0 cm), is cast
    again from 45 cm further out, at most 3 times (the HEADING line counts these self hits).
  - Every 3 m a ray down (the spawn's query, flags 0x50) checks the ground; none within 3 m below = a drop.
  - `clear` = the free direction (>= M) nearest the facing, so a run starts without a turn clip; `wall` = the nearest
    obstacle.
- **`walk`**: the stick = `MAG * (sin, cos)(heading - camera yaw)` on StrafeAxis/ForwardAxis, set every tick without a
  new press.
  - Once, 0.6 s in (after 40 cm), the travel direction is compared with the heading. If it is off by more than 50 deg
    and the mirror image about the camera's forward fits, the stick's x is flipped from then on (`loco stick` uses
    the same flag). This guards against a mirrored x in the probe's math.
- **`fling`**:
  - Refused unless FSM 6/7, not mounted or attached.
  - `FUN_020c6ff0(pm, 0)`, then `pm+0xe0` = v and `pc+0x60` = 1.
  - Per tick `fling-N.tsv` and FLING lines (on change, else every 0.2 s): height, root speed, `pm` velocity, velmode,
    FSM, supported, gravity.
  - Liftoff = not supported, FSM 8 or 5 cm up; landing = supported again.
  - 0.4 s after the landing, still momentum in FSM 6/7 = the graph never called it a fall: the probe calls
    `FUN_020c6ff0(pm, 1)` (the same after 1 s without liftoff and after 5 s).
- **`animrate`**: GetCurAnim/GetAnimRate/SetAnimRate (P7's recipe B) on Talion's object, tracker 0xff (or every playing
  one); a rate the game writes back is set again (counted, the first 3 logged); restored where it is still ours.
- **`vaultlog`**:
  - Per tick in `vault-N.tsv`: the player's attack `Combat+0x4d0`, the sync record `*(Combat+0x188)+0x10` and its target
    `+0x1f8`, FSM, velmode, the root and its yaw, and the partner: the sync's target, else the picked orc, else the
    nearest live AI.
  - VAULT lines on a change of FSM, attack or sync record.
  - An episode = while a sync record, FSM 9 or an attack is on; its end line gives duration, names, displacement, path,
    the largest step per tick (a teleport), Talion's yaw change, the partner's yaw change and attacks.
- **`npcpush`**:
  - On the server tick: `spawn`'s path with `AI_Infantry` CM along the heading.
  - 1.2 s later: loco at the orc at V to 1 m past it (at most 3 s), then a one-shot `pm+0x428` push (as `pmmove`) of the
    rest + 60 cm, at most 2 m.
  - The result: the closest approach in each phase, and whether Talion passed the orc (the projection on the push
    direction turned negative).

- Commands: `scripts/cmds/probe-P10.cmd`: run 4's menu, load and quit steps (as P9's); the rest are `probe` lines with a
  shot at each key step. About 7 minutes of game time: 380 s of waits (160 s of them before the world), 34 shots, 35
  probe sends. `--hold 600` = the 10-minute cap.
- Launch (as P9):
  ```
  cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
    --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
    --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
    --game-env SOMVR_COMFORT=1 \
    --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p10-world;orcs;timers' \
    --cmds scripts/cmds/probe-P10.cmd --hold 600
  ```
  No somvr in this boot. With somvr loaded, `SOMVR_FOLLOW` must stay off (`P_PMROOT`, and with it the exact transform
  for `loco`), and so must `SOMVR_INPUT` (the stick words).
- Order (game time from the cmd file's waits; the `world` mark at ~160 s):
  1. Main menu: `mark p10-menu` only. Load as in run 4.
  2. Tower top (~180 s, shot 12): `pmrec;tod;orcs`, then `speed 2` standing still.
  3. Arena (~185 s, shots 20/21):
     - `minhealth 20;exec Teleport 40400 3900 -21600` (minhealth stays on until the quit);
     - `despawn all;orcs;trav 2;heading open 25`.
  4. The stick sweep (~193 s, LR1; shot 22 in the full band):
     - `speed 4;walk B 3000` for B = 0.2, 0.4, 0.6, 0.8, each band after `heading clear` (12-20 m);
     - the full stick: `speed 7;walk 1 3000;sleep 2900;heading back;walk 1 2000` (the reversal).
  5. The sprint (~221 s, shot 23): `heading clear 35;speed 6;cmd FreeRun Jump ms=4000;walk 1 4000`.
  6. BaseSpeedMultiplier (~228 s, LR2): `pmrec set BaseSpeedMultiplier 1.5` + the full band, `pmrec restore`, then
     `pmrec base 1.5` + the full band, `pmrec base off`.
  7. Loco (~239 s, LR3/LR4; shots 30/31), each after `heading clear` and with `speed`:
     - `loco const 0 500 3` and `loco const 0 900 3` (the stick at rest);
     - `loco const 0 500 3` with `walk 0.3 3000`, with `walk 1 3000`, and with `cmd FreeRun Jump ms=3000;walk 1 3000`;
     - `loco stick 5;walk 0.4 2000` (200 cm/s for 2 s, then the stop inside the window).
  8. Launches (~271 s, LR5; shot 40): `speed 3;fling 0 600 0`, then `heading clear 8;speed 3;fling 0 500 400`.
  9. The tracker rate (~279 s, LR8; shot 41): `heading clear 25;speed 7;walk 0.6 6000;sleep 2500;animrate 0.7 3`.
  10. The vault and Shadow Strike (~287 s, LR6/LR10; shots 50-52):
      - `heading clear 8;walk 0.2 800`, then `spawn AI_Infantry 300`;
      - `heading orc;vaultlog 5;walk 0.6 1500;sleep 500;cmd CombatPrimary04`;
      - `heading orc;walk 0.2 400;sleep 600;vaultlog 6;wmhold PC_WraithRanged 3000;sleep 1300;cmd CombatKBM07`;
      - `despawn all`.
  11. Talion against an orc (~306 s, LR7; shot 53): `heading clear 6;npcpush 150 250`, then `despawn all`.
  12. The wall (~313 s, LR9; shots 60/61):
      - stock: `heading wall 12;trav 5;speed 5;vaultlog 5;cmd FreeRun Jump ms=3500;walk 1 3500`;
      - `heading back;walk 0.5 2500`;
      - the same with `loco stick 4` before the run.
  13. End (~328 s, shot 69): `loco off;walk off;animrate off;pmrec base off;pmrec restore;despawn all;orcs`, `release`.
      Then run 4's quit.
- Pass / fail per question:
  - **LR1 (the stock speeds):**
    - pass = per band `SPEED seq=N end: ...; stick B from +t s: plateau P cm/s (...), 90 % after T s; stopped S s and D
      cm after the release; fsm {6}`, with P rising over B (the stick-to-speed curve; walk/jog/run where the TAGS
      change) and the sprint's plateau above the full band's.
    - Read too: T (run 3: the first `Run` tag 0.6 s after the press), the stop distances, the reversal's dip in
      `speed-N.tsv`.
    - `WALK ... end` gives the travel direction against the heading. Off by more than ~30 deg = the run curved or hit
      something (`heading`'s scan line has the free distances).
    - Partial: `fsm {6,7}` (the patrol of P9 joined: combat movement) or TAGS with a turn or stop clip at the start.
    - fail = `walk` refused, or no motion (`plateau 0.0`): the stick did not reach the game.
  - **LR2 (the record):**
    - pass = 30 `PMREC` lines with values; `PMREC 17 'PhysicsCharacterGravity' ... = -20` and the caches line's
      `gravity pc+0x58=-20` (2.3's chain); `FallMinHeight`, `StandCapsuleStepHeight` and `BaseSpeedMultiplier` read.
    - A console name for an attribute, or `none of ...` for all 30: then these are not console variables by those
      names, and `pmrec set` FAILs (the run goes on).
    - BaseSpeedMultiplier: the full band's plateau after `pmrec set` / `pmrec base` against band 1.0. The same = nothing
      reads it (design-locomotion.md 2.2's "no reader"); x1.5 = a reader.
    - fail = `record pm+0x448 = 0` or `schema 0` (the holder is not filled at this point).
  - **LR3 (`LOCO_ROOTDELTA` replaced):**
    - pass = `LOCO ... end`:
      - `replaced` about the vf_11 calls, `skipped fsm 0`;
      - the root's mean speed / asked about 1.0 (`(0.95..1.05)`), and `first at >= 90 % of the asked speed: tick 1` (the
        tick after the start);
      - `LOCO ... stop after the end: the root a, b, c` with a = 0 (or b = 0): the stop in one tick;
      - in `loco stick`, `stop at the stick's release` the same.
    - Read too: `the translation lock was set in n` (n > 0 = the graph's locks zero our motion in those ticks), the FSM
      staying 6, the TAGS in `speed-N.tsv` (what the legs play), `dt from the object timer` (the dt source).
    - A ratio well under 1 with no lock = the solver or the proxy takes speed off (`pm v` in the LOCO lines).
    - fail = `P_ROOTDELTA ... NOT installed`, `replaced 0`, or the root moving at the stock speed.
  - **LR4 (which value picks the clips):** the TAGS and `RawMoveMag`/`Game_Velocity` in the four 500 cm/s windows.
    - Run tags at stick 0 = `Game_Velocity` (our speed) picks the family.
    - Idle tags at stick 0 and walk tags at 0.3 = the stick picks (then the leg feed is the virtual stick, design 2.5).
  - **LR5 (launches):**
    - pass = `FLING ... end (landed)`: `lifted off`, the apex (about 90 cm for 6 m/s up at -20 m/s2; 62 cm for 5 m/s),
      the flight time (0.6 s / 0.5 s), `fsm path 6>8>10>6` (or `6>8>6`), `root-motion mode back by the game` (FSM 10 did
      it) or `by the probe`.
    - `fling-N.tsv` has the arc for the horizontal one: 4 m/s held through the air (momentum mode) and no air drag.
    - fail = `NO liftoff` (the velocity was overwritten before the step: then the write must sit in the hook, not the
      probe tick).
  - **LR6 (the vault):** `VAULT ... episode` around `cmd CombatPrimary04`:
    - the names (an `attack PC_...` vault record, or `sync ...`), the duration, the path, Talion's yaw change (about 180
      deg = the re-face);
    - the partner's yaw change and attacks (its reaction); shot 51.
    - No episode = no vault started (the evade went elsewhere, or the orc was not in range): shot 51 says which.
  - **LR7 (Talion against an orc):** `NPCPUSH ... result`.
    - `stopped at the orc (a collision)` = the proxy collides with orcs (the grab-vault needs our own path past them,
      LO-12).
    - `no collision with the orc` = the root passed through it.
    - `the orc moved n cm` = it was pushed.
  - **LR8 (the tracker rate):** `ANIMRATE ... end`: `x0.70` (within ~10 %) = the root motion scales with the tracker
    rate (LO-10's leg match works); `x1.0` = it does not (the clip time or another tracker drives it).
    - `the game wrote the rate n times` > 0 = the graph sets rates itself: run `all` next time.
  - **LR9 (free run with loco):** the stock wall run against the loco wall run:
    - the stock run shows FSM 9 (`TRAV fsm=9`, a `VAULT ... episode` with `fsm 9`) or a climb at the wall (shot 60);
    - with loco the same FSM 9 appears and `LOCO ... skipped fsm n` > 0 = the graph keeps parkour (it owns the move,
      loco steps aside);
    - no FSM change, the root stopped at the wall = parkour is lost while loco owns the velocity (the free-run latch,
      design 3.5, needs another trigger).
    - If `heading wall 12` FAILs (no obstacle within 12 m), the run went along the last heading: read shot 60.
  - **LR10 (Shadow Strike):** `WMHOLD ... mode 'PC_WraithRanged' (the exact name) command 124` (the fix), then a
    `VAULT ... episode` after `cmd CombatKBM07`.
    - `largest step` of several metres in one tick = a teleport; a long path over many ticks = a sync with root motion.
    - No episode = no strike (no target in the aim, or no Focus): shot 52.
- Notes:
  - `loco`, `walk`, `animrate` and `fling` end by themselves (time, landing, 5 s).
  - `pmrec set` changes a console variable for this boot (`pmrec restore` sets it back).
  - `npcpush` and `spawn` add orcs, which `despawn all` removes. No captain this run.
  - `minhealth 20` stays on to the quit; the save guard holds every write.

#### Results, P10 (2026-10-20 15:46, `logs/boot-check-20261020-154655`)

Run with spike1 + the probe from `logs/build-p10` (main `63b6cff`), `SOMVR_COMFORT=1`, `probe-P10.cmd`, the user's save
copy; 403 s of game, and the game quit by itself (exit 0) after the cmd's quit. GameData.sav byte-identical (the guard
caught 3 writes), the install unchanged apart from the moved CEF leftovers. No crash; somvr.log (spike1) has no
WARN/FAIL, probe.log (2533 lines) one expected FAIL (`pmrec set`, below). 34 shots. KBM mode (`padmode=0`) throughout.
Analysed offline from the logs, `probe-out/speed-N.tsv`/`fling-N.tsv`/`vault-N.tsv` and the shots.

**Verdict:** the 1:1 answer works. `LOCO_ROOTDELTA` replaced moves the root at the asked speed from the first tick
(ratio 0.996-1.012, 200-900 cm/s) and stops it in one tick (LR3). The clip family follows the stick and `FreeRun`, not
the speed (LR4). The stock speeds (LR1): **one band, `Run` at 222-232 cm/s for every stick from 0.2 to 1.0** (no walk
band seen), 90 % after 0.82 s without a turn clip, stopped after 0.6 s and ~57 cm; **`FreeRun` 636-641 cm/s**, 90 %
after 0.58 s. The record (LR2): all 30 attributes read, none is a console variable by those names, and
`BaseSpeedMultiplier` has no reader. Not answered: launches (LR5: no liftoff), the vault (LR6: an evade dash instead),
the tracker rate (LR8: a wall), parkour under loco (LR9) and Shadow Strike (LR10: no target in the aim). LR7: Talion
slides around an orc and passes it, no stop.

**Surprises:**
- **The arena's patrol fought Talion from seq 20 to the end.** Two `AI_Infantry` (0x3d4e2170, 0x3d4c2480) hit him 30
  times from t=269555 (the `loco-500-s10` step) to the quit, a recoil every 2-3 s (`PC_Recoil_*`, FSM 7/6, shots
  40-61). Everything from seq 20 on ran inside that fight: the flings, the tracker rate, the vault, the strike, the
  push and both wall runs. Seq 4-19 were clean.
- **`InCombat` was on all run long**, from 8 s after the load at the tower top (slaves 53 m away, no armed AI near):
  every measured band has it. Whether the stock graph has a walk band out of it (or in pad mode) is open.
- **No stick-to-speed curve.** `RawMoveMag` read exactly the probe's 0.200-1.000, and every band played `Run` at the
  same speed.
- **A launch from the ground loses its vertical speed in its first step** (`vy` 600 -> 0, supported throughout), while
  a horizontal speed written in momentum mode glides on along the ground.
- **The evade at an orc 3.2 m away was `PC_Dash`**, not a vault: 5.9 m in 0.95 s, peak ~12 m/s.

- **LR1, the stock speeds: partly. One band, `Run` ~228 cm/s; the sprint ~640 cm/s; no walk.** Per band (`walk B 3000`
  along `heading clear`, the stick re-aimed through the camera; the probe's `SPEED ... end`; the stop = two ticks under
  5 %):

  | Band | Plateau cm/s | 90 % after | Stop after the release | Notes |
  |---|---|---|---|---|
  | 0.2 | 232.3 | 0.95 s | 0.97 s, 135 cm | a 175 deg turn clip first (the heading behind him); downhill |
  | 0.4 | 229.6 | 0.82 s | 0.60 s, 59 cm | no turn: first motion 0.18 s, 50 % 0.52 s |
  | 0.6 | 223.2 | 0.82 s | 0.60 s, 58 cm | no turn |
  | 0.8 | 225.0 | 0.97 s | not within 1 s (149 cm) | a turn first |
  | 1.0 | 224.2 | 1.08 s | 1.27 s, 200 cm | a 180 deg turn first; the reversal mid-run (below) |
  | 1.0 + `FreeRun` | **636-641** (the probe: 222.2) | 0.58 s (of 635) | a wall | `Spr` from 0.07 s |

  - Tags: `Run` from 0.47-0.88 s in every band (`Gnd Lnd` before it), `Run Spr` with `FreeRun`; FSM 6, root-motion mode
    throughout. The run's speed ripples 200-262 cm/s tick to tick (the stride), the sprint's 570-735 per 0.25 s.
  - The sprint's own `plateau 222.2` is the probe's error: the run met a wall at 17.8 m 3.43 s in (646 -> 2 cm/s in one
    tick while the game's delta still asked 473), inside the last second the plateau is taken from. Its mean over
    2.4-3.3 s is 641 (the game's delta 640).
  - The reversal (`heading back` 2.9 s into the full band): Talion turned ~166 deg in ~0.5 s at full speed with no dip
    (222 cm/s over 2.85-3.4 s).
  - `WALK ... end`: travel within 7 deg of the heading in the clean bands; the reversal 61 deg and the sprint 24 deg off
    (the probe's mirror check flipped the stick during their turns, nits).
- **LR2, the record: pass; no console names; `BaseSpeedMultiplier` has no reader.**
  - `PMREC`: the record `pm+0x448` = `'PlayerMovement'`, 30 attributes, all on the fast path (state 2). Values:
    FallDamageMin/MaxHeight 500/1200, **FallDamageMin/Max 0/0**, FallMinHeight 100, CamLandMinHeight 100,
    CamLandMoveDist -3, CamLandDown/UpTime 150/250 ms, CamLandRollVal 5 (50/150 ms), CamLandPitchVal 5 (50/150 ms),
    PlayerYawInterpolateTime 400 ms, BaseSpeedMultiplier 1, **PhysicsCharacterGravity -2000** (cm/s^2), UsePenCollision
    1, the three PenOriginOffsets (vectors), AimingRadiusScale 1.8, AimingTeleportRange (a range), AimingTeleportExclusiveTags
    `Ral`, `Mount_Warg`, `Mount_Troll`, StandCapsuleHeight 125, Radius 35, **StepHeight 40**, SpectatorSpeedMultiplier1/2
    10/0.1.
  - The caches: `pm+0x32c` 1, `+0x450` 0.4 s, `+0x454` 100 cm; the proxy's **`pc+0x58` = -20** (m/s^2: the record's
    -2000 cm/s^2) with `pc+0x60` 1. 2.3's chain holds.
  - **None of the 30 is known to the console** as `PlayerMovement.X`, `Client.PlayerMovement.X` or `X`, so `pmrec set
    BaseSpeedMultiplier 1.5` FAILed (as allowed).
  - `pmrec base 1.5` (`pm+0x32c` 1 -> 1.5): the full band 229.8 cm/s against 224.2 and 229.3 without it. **Nothing
    reads it** (design-locomotion.md 2.2).
- **LR3, `LOCO_ROOTDELTA` replaced: pass.** Six windows, every `LOCO ... end` with `replaced` = the vf_11 calls,
  `skipped fsm 0`, the translation and rotation locks 0, `dt from the object timer` in every tick, `first at >= 90 %:
  tick 1`:

  | Window | Asked | The root (ratio) | After the end / the release |
  |---|---|---|---|
  | `loco const 0 500 3`, stick 0 | 500 | 499.6 (0.999) | 0, 0, 0 cm/s |
  | `loco const 0 900 3`, stick 0 | 900 | 899.7 (1.000) | 43, 50, 49 (the game's own shuffle; `CombatCam` from 1.6 s) |
  | 500 + `walk 0.3` | 500 | 502.3 (1.005) | 263, 251, 240: the stock Run goes on (the stick held) |
  | 500 + `walk 1` | 500 | 497.8 (0.996), FSM {6,7} | 248, 262, 259 |
  | 500 + `FreeRun` + `walk 1` | 500 | 498.3 (0.997), FSM {6,7} | 619, 661, 637: the stock sprint goes on |
  | `loco stick 5` + `walk 0.4` | 200 | 202.4 (1.012), 2 recoils inside | the release 0, 0, 0; the end 0, 0, 0 |

  - Slopes: the root's vertical speed followed the ground (-80..+70 cm/s) at ratio 1.000. No wall met in the clean
    windows.
  - FSM 6 in the stick-at-rest windows; the yaw unchanged there (no turn clips). With the stick behind Talion the
    stock turn clips still played (163 and 152 deg) while our velocity held.
  - `the translation lock was set in 0` throughout, also over the two recoils in `loco stick`.
  - The line's `an attack ran in N` (= every tick) counts `pm+0x38` non-null, not a running attack (nit).
- **LR4, which value picks the clips: the stick (and `FreeRun`).** With `loco const 0 500/900` and the stick at rest
  the tags stayed `Idl` (Talion glides standing, shot 30) and the game's own root delta was 0; at 0.3 and 1.0 `Run`
  (the game's delta 253 and 236 cm/s under our 500); with `FreeRun` `Run Spr` (the game's delta 639). **`Game_Velocity`
  is not a variable of this graph** (`GRAPH ... Game_Velocity 0`, the column `nan`); `Game_SmoothedVelocity` follows
  our speed (498 / 897) and picks nothing. So the leg feed is the virtual stick (design 2.5).
- **LR5, launches: fail as written (NO liftoff).**
  - `fling 0 600 0`: velmode 1 -> 0, `pm v` (0, 600, 0) written; one tick later `vy` = 0, supported 1, height 0, FSM 6
    (then 7: a hit at 0.83 s). The game put root-motion mode back on the FSM 7 entry.
  - `fling 0 500 400` (world 376, 500, 137 cm/s): `vy` 0 after the first step the same way; **the horizontal 4 m/s held
    in momentum mode along the ground**: 370 cm in 0.98 s (376 -> 331 cm/s, slightly uphill, +16 cm), supported
    throughout, FSM 6. A 5 cm bump was counted as a 0.05 s flight. The probe put root-motion mode back at +0.98 s (the
    graph never did).
  - So the probe tick's write reaches the step (x and z move), but a supported body loses `vy` there: the liftoff
    needs more than the velocity (the write after the solve inside the PM hook, a lift above the 40 cm step height, or
    the stock `Jump` first). 2.7's "a vertical launch alone also works" is wrong for a supported body.
- **LR6, the vault: not answered; the evade was a dash.** `heading orc` (3.2 m) and `walk 0.6 1500` ran the wrong way
  (the mirror flip in a fight), and `cmd CombatPrimary04` came during a patrol recoil: **`PC_Dash`** 0.67 s later
  (attack, FSM 7, root motion, no sync): 592 cm horizontally in 0.95 s (path 609), steps up to 20.7 cm a tick (~12
  m/s), slowing to ~0.7 m/s at the end; Talion's yaw set once at the start (-40 deg), no re-face. The spawned orc
  (`AI_Infantry_Avoid_Left`, 3.2-3.5 m) turned 55 deg. Shot 51: Talion alone.
- **LR7, Talion against an orc: no stop; he slides around it.** `npcpush 150 250`: an `AI_Infantry` 229 cm ahead;
  loco at 250 cm/s toward it: the solver's velocity was turned up to ~57 deg near it (the root down to 136 cm/s),
  **the closest approach 56.5 cm and passed**; the mean 203 cm/s (0.81 of the asked). The one-shot push (20 cm,
  consumed by the next vf_11) passed within 97 cm. The orc moved 32.7 cm (then `AI_BIP_Recoil_IntoWall_B`). The
  probe's verdict `no collision with the orc` means no stop: the capsules deflect (35 cm Talion's radius; 56.5 cm
  between the roots).
- **LR8, the tracker rate: not answered (a wall).** `animrate 0.7 3` on tracker 255 (the game never wrote it back): the
  root stopped dead (1-2 cm/s) in the tick the rate was set, at a rock (`CloseToWalls`; the walk ran 148 deg off after
  a wrong mirror flip, shot 41), so the probe's x0.307 is the wall. The game's own root delta read 196-201 cm/s for the
  first 0.3 s against 249-251 before (x0.79-0.80), then fell as the graph stopped. Between 0.7 and 1.0: inconclusive.
- **LR9, free run with loco: not answered.** Both wall runs were inside the patrol's melee: `heading wall 12` found
  "walls" at 1.2 and 0.5 m (most likely the orcs: the scan does not skip AI objects), no FSM 9 in either, recoils
  instead (`VAULT` episodes of `PC_Recoil_*`); the loco run moved 3.2 of 500 cm/s (blocked). Shots 60/61: two orcs at
  Talion.
- **LR10, Shadow Strike: not answered; the P9 fix works.** `WMHOLD seq=29 mode 'PC_WraithRanged' (the exact name)
  command 124` (`active=1`): the aim came up (shot 52: the bow drawn, the wraith vision, the reticle) and was still up
  in shot 52, ~2 s after a heavy patrol recoil 0.3 s in (`Dagger_RRecoil_Heavy_U2D_B`; with 124 still held). But the camera aimed at a rock (`heading orc`
  turns the stick, not the camera: the orc at -70 deg, the camera at 122 deg): "No Target Selected", and `CombatKBM07`
  was polled 0 times. No strike episode.
- **P9's leftovers** (the aim ray `aimray`, the fire `CombatKBM05`, a berserker's dodge after `incoming`, the counter in
  pad mode): not in this cmd, not run.
- Probe nits for the next run:
  - `LOCO ... an attack ran in N` counts `pm+0x38` non-null; count `FUN_026d4510(*(pm+0x38))` (somvr's `LOCO_ATTACK_Q`).
  - `SPEED`'s plateau is the hold's last second: flag a wall (the root far under the game's delta) and give the best
    1 s mean.
  - `walk`'s mirror check flipped the stick wrongly 6 times (seq 12, 13, 25, 26, 28, 33): it ran during turn clips and
    in FSM 7. Check only after the turn (Talion's yaw within 30 deg of the heading), in FSM 6; never flip in FSM 7.
  - `heading` takes orcs for walls: skip AI objects as it skips Talion's own.
  - `fling`: lift off first (above).
  - The strike: turn the camera to the orc (`heading orc` + a camera yaw), or aim with `aimray`.
  - `Game_Velocity` is not in the graph: drop it or read `Game_VelocityX/Y/Z`.
- Next, a P11 (a plan, no code yet):
  1. **A quiet spot**: P9's arena twice brought the patrol. Pick a teleport target with no armed AI within 60 m
     (`orcs` before every step, abort the active steps when one comes within 20 m), and run the clean steps first.
  2. **LR1's rest**: `padmode on`, then the stick at 0.05/0.1/0.15/0.2/0.3/0.5 (the walk band and the floor of `Run`),
     crouch, the combat strafe; once with `InCombat` off if anything clears it (its source is open).
  3. **LR5 with a liftoff**: the velocity written in the PM hook after vf_10's solve (a new probe site), or `cmd Jump`
     first and the fling at FSM 8, or a lift of the root above the 40 cm step before the write; the arc, the FSM path
     and the landing.
  4. **LR6**: a spawned orc 1.5-2.5 m ahead and facing Talion, Talion idle (no recoil), the stick at it with the fixed
     mirror check, `cmd CombatPrimary04`; `vaultlog`.
  5. **LR8**: `animrate` on a 25 m clear and level heading; judge the game's root delta (`game_speed`), not the root.
  6. **LR9**: a known climbable wall (re-traversal.md's drill spot), outside any fight; the stock and the loco run.
  7. **LR10 and P9's bow**: the camera turned to the orc, `wmhold PC_WraithRanged`, `aimray`, `cmd CombatKBM07`; then
     `cmd CombatKBM05` with `projlog` (the fire); a berserker's dodge after `incoming` with Talion idle; the counter in
     pad mode (`CombatPrimary02`).
  8. The probe nits above. Then the first somvr boot with `SOMVR_LOCO=1 SOMVR_ANCHOR=root` (re-xr-pipeline.md 17.7),
     a separate boot (`0xab5ea9` is exclusive with the probe's `P_ROOTDELTA`).

Register status after P10 (section 1 updated):

| Status | IDs |
|---|---|
| Answered | LR2 (the record; no console names; no `BaseSpeedMultiplier` reader), LR3 (1:1 from the first tick, the stop in one tick), LR4 (the stick and `FreeRun` pick the clips) |
| Partly | LR1 (one `Run` band 222-232 cm/s and `FreeRun` 636-641 in KBM mode with `InCombat`; no walk band, crouch or strafe), LR7 (no stop: a slide around the orc and a 33 cm push), LR10 (the aim by the exact name works; no strike: no target) |
| Open | LR5 (no liftoff from the ground), LR6 (the evade was `PC_Dash`), LR8 (a wall), LR9 (inside a fight), BW3, BW7, the fire command, the berserker's dodge, C7 in pad mode |

## 3. Check data

The probe's own hook and called-function sites, generated from this build with `mod/probe/gen_sites.py` and verified
by `scripts/check_doc_sites.py` (the same lines are in `mod/probe/somvr_probe.c`; `build.sh --check` compares them):

```c
SITE("P_INPUT", K_VSLOT, 0x34798c0, 0x34798c0, "b0 23 90 00 00 00 00 00", 0x9023b0, "80 bf 3c 02 00 00 00 75 07 c3 66 0f 1f 44 00"),
SITE("P_VALUE", K_VSLOT, 0x3479950, 0x3479950, "90 35 90 00 00 00 00 00", 0x903590, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 10 48"),
SITE("P_VALUE2", K_VSLOT, 0x3479958, 0x3479958, "90 39 90 00 00 00 00 00", 0x903990, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 10 48"),
SITE("P_ISON", K_VSLOT, 0x3479968, 0x3479968, "e0 11 90 00 00 00 00 00", 0x9011e0, "48 85 f6 0f 84 57 02 00 00 41 54 55 53 48 8b"),
SITE("P_SERVER", K_VSLOT, 0x3987690, 0x3987690, "50 7c 77 02 00 00 00 00", 0x2777c50, "53 48 89 fb 48 8b 3d 4d 33 ef 01 48 8b 07 ff"),
SITE("P_VIS", K_VSLOT, 0x335cd90, 0x335cd90, "70 2e 70 00 00 00 00 00", 0x702e70, "48 8b 3d 31 81 f6 03 41 54 49 89 f4 55 53 48"),
SITE("P_UI105", K_VSLOT, 0x335cd80, 0x335cd80, "b0 51 70 00 00 00 00 00", 0x7051b0, "41 54 41 89 f4 55 89 d5 53 e8 02 87 02 00 bf"),
SITE("P_ANIM_A", K_CALL, 0xa41a55, 0xa41a45, "03 00 00 83 78 30 03 75 0c 48 8b bb d8 02 00 00 e8 b6 f7 00 00", 0xa51210, "53 48 89 fb c6 87 e0 05 00 00 00 48 8b bf b8"),
SITE("P_ANIM_B", K_CALL, 0xa41363, 0xa41353, "03 00 00 83 78 30 03 75 0c 48 8b bb d8 02 00 00 e8 a8 fe 00 00", 0xa51210, "53 48 89 fb c6 87 e0 05 00 00 00 48 8b bf b8"),
SITE("P_CAMPOST_A", K_CALL, 0xa41ab7, 0xa41aa7, "00 48 8b bb f0 02 00 00 e8 0c 20 09 00 48 89 df e8 84 38 ff ff", 0xa35340, "41 54 66 0f ef d2 55 53 48 89 fb 48 83 c4 80"),
SITE("P_CAMPOST_B", K_CALL, 0xa413c5, 0xa413b5, "00 48 8b bb f0 02 00 00 e8 fe 26 09 00 48 89 df e8 76 3f ff ff", 0xa35340, "41 54 66 0f ef d2 55 53 48 89 fb 48 83 c4 80"),
SITE("P_ATK_OBJ", K_CALL, 0xa8cb19, 0xa8cb09, "ea 4c 89 ee 4c 89 f7 41 51 44 8b 8b d8 04 00 00 e8 72 12 c6 01", 0x26edd90, "4c 89 87 70 01 00 00 44 0f b6 44 24 08 44 89"),
SITE("P_ATK_POS", K_CALL, 0xa8d02b, 0xa8d01b, "58 04 00 00 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 10 0d c6 01", 0x26edd40, "f3 41 0f 10 08 f3 0f 11 8f 50 01 00 00 f3 41"),
SITE("P_ATK_NOPOS", K_CALL, 0xa8d04d, 0xa8d03d, "60 45 89 c8 4c 89 e1 48 89 ea 4c 89 ee 4c 89 f7 e8 5e 09 c6 01", 0x26ed9b0, "48 c7 87 50 01 00 00 00 00 00 00 45 0f b6 c0"),
SITE("P_DEALHIT", K_CALL, 0x26e5468, 0x26e5458, "7e 84 24 a8 00 00 00 f3 0f 10 8c 24 b0 00 00 00 e8 83 fb fe ff", 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55"),
SITE("P_APPLYDMG", K_CALL, 0x26e5af5, 0x26e5ae5, "54 49 89 d4 55 48 89 f5 53 48 89 fb 48 83 ec 50 e8 a6 ee ff ff", 0x26e49a0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 01"),
SITE("P_COUNTER", K_CALL, 0x26fc32b, 0x26fc31b, "66 39 c2 0f 83 bc 09 00 00 29 d0 89 c6 48 89 df e8 60 9c fe ff", 0x26e5f90, "48 83 bf 80 00 00 00 00 0f 84 32 02 00 00 41"),
SITE("P_HITEV", K_CALL, 0x2819a9a, 0x2819a8a, "8b b5 38 ff ff ff 48 83 c4 40 4c 89 e6 4c 89 f7 e8 e1 8a ed ff", 0x26f2580, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("P_DEATH", K_CALL, 0x281985d, 0x281984d, "e8 be ac eb ff 31 c9 48 89 da 48 89 c6 48 89 ef e8 8e c9 ed ff", 0x26f61f0, "80 bf 88 02 00 00 00 0f 85 f3 0e 00 00 55 48"),
SITE("P_PMROOT", K_VSLOT, 0x34fc838, 0x34fc838, "70 5e ab 00 00 00 00 00", 0xab5e70, "55 48 89 fd 66 0f ef db 53 48 89 f3 48 83 ec"),
SITE("P_STIM_START", K_CALL, 0x26ed119, 0x26ed109, "fd ff e9 3a eb ff ff 4c 89 e2 4c 89 ee 48 89 df e8 62 a0 fd ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("P_STIM_KEY", K_CALL, 0x26fd1d7, 0x26fd1c7, "41 39 fe 48 8b 93 c0 04 00 00 48 89 df 48 89 c6 e8 a4 9f fc ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("P_STIM_STOP", K_CALL, 0x26eed28, 0x26eed18, "48 83 7b 70 00 74 0e 4c 89 ea 48 89 ee 48 89 df e8 53 84 fd ff", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("P_REGSTIM", K_CALL, 0x26c747f, 0x26c746f, "00 48 8b 3d 69 8c 58 02 48 8d b4 24 80 00 00 00 e8 9c 73 27 00", 0x293e820, "80 7f 24 00 75 4f 80 3d 63 88 30 02 00 74 51"),
SITE("P_REGSTIM_P", K_CALL, 0x26c76bf, 0x26c76af, "00 48 8b 3d 29 8a 58 02 48 8d b4 24 80 00 00 00 e8 5c 71 27 00", 0x293e820, "80 7f 24 00 75 4f 80 3d 63 88 30 02 00 74 51"),
SITE("P_AI_GOAL", K_VSLOT, 0x3a4e0f8, 0x3a4e0f8, "80 6d 9d 02 00 00 00 00", 0x29d6d80, "53 48 89 fb e8 07 c5 ff ff 48 8b 7b 08 e8 5e"),
SITE("P_AI_BLOCK_OK", K_VSLOT, 0x3a3d828, 0x3a3d828, "10 55 9c 02 00 00 00 00", 0x29c5510, "41 54 49 89 fc 55 53 48 83 ec 70 48 8b 6f 08"),
SITE("P_AI_BLOCK_GO", K_VSLOT, 0x3a3d7e8, 0x3a3d7e8, "d0 61 9c 02 00 00 00 00", 0x29c61d0, "53 48 89 fb 48 83 ec 20 e8 a3 7a ff ff 48 8b"),
SITE("P_AI_DODGE_OK", K_VSLOT, 0x3a3e428, 0x3a3e428, "90 65 9c 02 00 00 00 00", 0x29c6590, "41 57 41 56 41 55 41 54 55 53 48 81 ec 98 01"),
SITE("P_AI_DODGE_GO", K_VSLOT, 0x3a3e3e8, 0x3a3e3e8, "90 7e 9c 02 00 00 00 00", 0x29c7e90, "41 57 41 56 45 31 f6 41 55 45 31 ed 41 54 49"),
SITE("P_KEYTIMES", K_CALL, 0x26eb77a, 0x26eb76a, "8b b7 ff 48 8b b3 a0 04 00 00 4c 89 ff 48 89 ea e8 11 7e b7 ff", 0x2263590, "41 57 41 56 41 55 41 54 49 89 fc 55 48 89 f5"),
SITE("P_PJ_UPDATE", K_VSLOT, 0x3998748, 0x3998748, "20 5d 7c 02 00 00 00 00", 0x27c5d20, "55 53 48 89 fb 48 83 ec 08 f3 0f 10 05 ef ec"),
SITE("P_PJ_DAMAGE", K_CALL, 0x27bf8ba, 0x27bf8aa, "48 8b 54 24 70 4c 8b 8c 24 b0 00 00 00 48 89 de e8 51 95 f0 ff", 0x26c8e10, "41 57 49 89 f7 41 56 41 55 4d 89 cd 41 54 49"),
SITE("P_SND_POST", K_CALL, 0x8c9865, 0x8c9855, "f0 48 89 d7 45 31 c9 6a 00 45 31 c0 31 c9 31 d2 e8 06 e0 b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("P_SND_POST_CB", K_CALL, 0x8c988d, 0x8c987d, "00 45 31 c9 b9 a0 8b 8c 00 ba 05 00 00 00 6a 00 e8 de df b9 00", 0x1467870, "41 57 4d 89 c7 41 56 49 89 ce 41 55 41 89 d5"),
SITE("P_UISCENE", K_CALL, 0xc1ee09, 0xc1edf9, "8b 07 ff 90 d8 00 00 00 48 8d 74 24 50 48 89 c7 e8 42 37 dd ff", 0x9f2550, "55 48 89 fd 53 48 89 f3 48 83 ec 08 48 8b 07"),
SITE("P_FXI_A07FE1", K_CALL, 0xa07fe1, 0xa07fd1, "44 24 10 31 c9 48 89 c2 48 8d 7c 24 30 4c 89 ce e8 8a 1e 00 00", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_A08081", K_CALL, 0xa08081, 0xa08071, "44 24 10 31 c9 48 89 c2 48 8d 7c 24 30 4c 89 ce e8 ea 1d 00 00", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_E1E6D9", K_CALL, 0xe1e6d9, 0xe1e6c9, "49 89 e8 b9 01 00 00 00 4c 89 e6 48 8d 7c 24 10 e8 92 b7 be ff", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_E22DF2", K_CALL, 0xe22df2, 0xe22de2, "00 00 4c 89 e6 4c 8d b4 24 f0 01 00 00 4c 89 f7 e8 79 70 be ff", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26E5E34", K_CALL, 0x26e5e34, 0x26e5e24, "00 00 4c 89 ee 4d 89 f0 31 c9 48 89 c2 48 89 e7 e8 37 40 32 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26E674B", K_CALL, 0x26e674b, 0x26e673b, "00 4c 89 f6 48 8d 7c 24 10 4c 8b 42 08 48 89 c2 e8 20 37 32 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26E6DE8", K_CALL, 0x26e6de8, 0x26e6dd8, "4c 8b 44 24 10 4c 89 fe 44 89 e1 48 8d 7c 24 30 e8 83 30 32 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26EDEAB", K_CALL, 0x26edeab, 0x26ede9b, "67 fe 4c 89 e6 31 c9 48 89 ea 49 89 c0 48 89 e7 e8 c0 bf 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26EEECA", K_CALL, 0x26eeeca, 0x26eeeba, "24 90 01 00 00 4c 89 ce 31 c9 48 89 c2 4c 89 e7 e8 a1 af 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26F34F1", K_CALL, 0x26f34f1, 0x26f34e1, "48 8b 54 24 08 48 89 c6 48 89 ef 4c 8b 01 31 c9 e8 7a 69 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26F3659", K_CALL, 0x26f3659, 0x26f3649, "48 8b 54 24 08 31 c9 49 89 c0 48 89 de 48 89 ef e8 12 68 31 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26FC738", K_CALL, 0x26fc738, 0x26fc728, "4c 8d 6c 24 40 48 89 c2 4d 89 e0 31 c9 4c 89 ef e8 33 d7 30 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_26FCBD7", K_CALL, 0x26fcbd7, 0x26fcbc7, "8b 44 24 08 31 c9 4c 89 ef 48 8b 34 24 48 89 c2 e8 94 d2 30 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_273C276", K_CALL, 0x273c276, 0x273c266, "f8 ff 31 c9 4c 89 e2 4c 89 f6 49 89 c0 48 89 ef e8 f5 db 2c fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_273C4FB", K_CALL, 0x273c4fb, 0x273c4eb, "24 40 4c 89 f6 49 89 c0 31 c9 48 89 ef 4c 89 e2 e8 70 d9 2c fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BD11B", K_CALL, 0x27bd11b, 0x27bd10b, "00 4c 89 e7 31 c9 4c 89 fe 4c 8b 42 08 48 89 c2 e8 50 cd 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BD205", K_CALL, 0x27bd205, 0x27bd1f5, "74 40 4c 89 e7 49 89 d8 31 c9 48 89 c2 4c 89 f6 e8 66 cc 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BD30E", K_CALL, 0x27bd30e, 0x27bd2fe, "5a fe 49 89 c0 4c 89 e7 31 c9 4c 89 ea 48 89 de e8 5d cb 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_FXI_27BDF60", K_CALL, 0x27bdf60, 0x27bdf50, "49 89 c0 48 8d 7c 24 60 31 c9 4c 89 ea 4c 89 e6 e8 0b bf 24 fe", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("P_RES3D", K_CALL, 0x741ceb, 0x741cdb, "c0 f3 44 0f 5e c7 f3 44 0f 58 c1 f3 49 0f 2c c8 e8 f0 df ff ff", 0x73fce0, "44 8b 15 6d 03 ee 03 66 0f ef c0 48 c7 46 3c"),
SITE("P_IK_SETUP_A", K_CALL, 0x21b875e, 0x21b874e, "08 4d 89 6c 24 68 48 8b 53 50 4c 89 e7 4c 89 ee e8 9d d1 fc ff", 0x2185900, "41 55 41 54 55 53 48 89 fb 48 89 d7 48 83 ec"),
SITE("P_IK_SETUP_B", K_CALL, 0x21b8c8a, 0x21b8c7a, "48 08 4c 89 63 68 48 8b 55 50 4c 89 e6 48 89 df e8 71 cc fc ff", 0x2185900, "41 55 41 54 55 53 48 89 fb 48 89 d7 48 83 ec"),
SITE("P_IK_CHAIN", K_CALL, 0x20a51d4, 0x20a51c4, "8d ab d8 34 00 00 4c 8d a3 38 39 00 00 48 89 ef e8 b7 03 0e 00", 0x2185590, "8b 87 0c 01 00 00 85 c0 0f 84 4a 01 00 00 53"),
SITE("P_IEM_ADDCB", K_VSLOT, 0x3484260, 0x3484260, "70 ad 91 00 00 00 00 00", 0x91ad70, "48 85 f6 74 6b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("P_IEM_REMCB", K_VSLOT, 0x3484268, 0x3484268, "f0 ad 91 00 00 00 00 00", 0x91adf0, "48 85 f6 74 5b 41 55 48 89 f7 41 89 d5 41 54"),
SITE("P_TRK_EVAL", K_VSLOT, 0x38df398, 0x38df398, "b0 99 38 02 00 00 00 00", 0x23899b0, "48 8b 97 08 08 00 00 c6 87 48 01 00 00 00 48"),
SITE("P_ATTACH_LISTEN", K_VSLOT, 0x38d8958, 0x38d8958, "60 a6 35 02 00 00 00 00", 0x235a660, "48 83 bf 80 00 00 00 00 0f 84 9a 06 00 00 41"),
SITE("P_LP_GUARD_HOVER", K_CALL, 0xb4e5d8, 0xb4e5c8, "48 89 d3 e8 a0 4f e9 ff 48 85 c0 74 0c 48 89 c7 e8 a3 a8 ee ff", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("P_LP_GUARD_CLICK", K_CALL, 0xb4e62c, 0xb4e61c, "af b5 03 e8 4c 4f e9 ff 48 85 c0 74 0c 48 89 c7 e8 4f a8 ee ff", 0xa38e80, "48 83 ec 08 48 8b 05 d5 06 c7 03 0f b6 b7 31"),
SITE("P_LP_SYS_DOWN", K_VSLOT, 0x34e4230, 0x34e4230, "20 41 9f 00 00 00 00 00", 0x9f4120, "80 3d 9d 54 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("P_LP_SYS_UP", K_VSLOT, 0x34e4238, 0x34e4238, "c0 41 9f 00 00 00 00 00", 0x9f41c0, "80 3d fd 53 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("P_LP_SYS_MOVE", K_VSLOT, 0x34e4240, 0x34e4240, "60 42 9f 00 00 00 00 00", 0x9f4260, "80 3d 5d 53 cb 03 00 74 06 80 7f 10 00 74 06"),
SITE("P_LP_VIEW_DOWN", K_VSLOT, 0x3482248, 0x3482248, "d0 b6 90 00 00 00 00 00", 0x90b6d0, "53 48 63 f6 66 0f ef c0 66 0f ef c9 48 89 fb"),
SITE("P_LP_VIEW_UP", K_VSLOT, 0x3482250, 0x3482250, "70 b7 90 00 00 00 00 00", 0x90b770, "53 48 63 f6 66 0f ef c0 66 0f ef c9 48 89 fb"),
SITE("P_LP_VIEW_MOVE", K_VSLOT, 0x3482258, 0x3482258, "10 b8 90 00 00 00 00 00", 0x90b810, "48 83 ec 28 66 2b 57 2c 66 0f ef c0 66 2b 77"),
SITE("P_LP_GFX_EVENT", K_VSLOT, 0x3763ec0, 0x3763ec0, "d0 a3 24 01 00 00 00 00", 0x124a3d0, "41 54 55 48 89 fd 53 48 89 f3 48 83 ec 20 48"),
SITE("P_GRAV", K_VSLOT, 0x34fc810, 0x34fc810, "a0 58 ab 00 00 00 00 00", 0xab58a0, "41 54 55 53 48 8b 5f 50 48 85 db 0f 84 7f 00"),
SITE("P_AIB_GATE", K_CALL, 0x26eb818, 0x26eb808, "7c 24 38 74 1b 4d 85 ff 74 16 48 89 ee 4c 89 f7 e8 d3 f6 ba ff", 0x229aef0, "48 85 ff 0f 84 57 01 00 00 41 54 48 89 f0 55"),
SITE("P_AIB_REC", K_CALL, 0x26ecbc7, 0x26ecbb7, "00 00 0f 85 69 ec ff ff 49 8b 47 10 48 8b 78 70 e8 64 27 01 00", 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85"),
SITE("P_AIB_ROLL", K_CALL, 0x26ecd50, 0x26ecd40, "0f 10 0d 29 7a c5 00 66 0f ef c0 f3 0f 11 14 24 e8 eb 66 58 fe", 0xc73440, "48 83 ec 18 f3 0f 11 44 24 0c f3 0f 11 4c 24"),
SITE("P_AIB_NOTIFY", K_CALL, 0x26ecd6a, 0x26ecd5a, "0f 2f d0 0f 86 c5 ea ff ff 48 8b 7b 70 4c 89 fe e8 21 fb 2b 00", 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b"),
SITE("P_GOAL_REL", K_VSLOT, 0x3a4e0d8, 0x3a4e0d8, "90 6b 9d 02 00 00 00 00", 0x29d6b90, "41 54 55 53 48 8b 6f 08 48 89 fb 48 8b 85 20"),
SITE("P_SHIELD", K_CALL, 0x28177c4, 0x28177b4, "41 0f 10 87 bc 01 00 00 48 8b 74 24 50 48 89 ef e8 67 67 09 00", 0x28adf30, "f3 0f 10 8f a8 00 00 00 0f 2f 0d 49 a7 a9 00"),
SITE("P_BOW_CAMXF", K_CALL, 0xa9a1d0, 0xa9a1c0, "00 48 8b 03 48 8d 74 24 40 48 8b b8 d0 02 00 00 e8 db 16 fc ff", 0xa5b8b0, "55 48 89 fd 53 48 89 f3 48 83 ec 28 e8 6f 8e"),
SITE("P_ROOTDELTA", K_CALL, 0xab5ea9, 0xab5e99, "44 24 28 00 00 80 3f 48 c7 44 24 20 00 00 00 00 e8 f2 b4 f9 ff", 0xa513a0, "55 48 89 fd 53 48 89 f3 48 83 ec 18 48 8b 47"),
SITE("F_CMD_ON", K_ENTRY, 0xaab660, 0xaab660, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00 00", 0xaab660, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00"),
SITE("F_CMD_OFF", K_ENTRY, 0xaab5d0, 0xaab5d0, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00 00", 0xaab5d0, "80 7f 78 00 75 0a c3 66 0f 1f 84 00 00 00 00"),
SITE("F_CMD_ACTIVE", K_ENTRY, 0xaab900, 0xaab900, "89 f2 48 81 fa 5c 01 00 00 77 1a 48 c1 ea 06 89", 0xaab900, "89 f2 48 81 fa 5c 01 00 00 77 1a 48 c1 ea 06"),
SITE("F_INPUT_MODE", K_ENTRY, 0xa0eeb0, 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f b6", 0xa0eeb0, "41 54 49 89 fc 55 48 89 f5 53 48 8b 7e 08 0f"),
SITE("F_DEALHIT", K_ENTRY, 0x26d4ff0, 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55 48", 0x26d4ff0, "41 57 49 89 f7 41 56 41 89 ce 41 55 41 54 55"),
SITE("F_ATK_BYNAME", K_ENTRY, 0x2166790, 0x2166790, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x2166790, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("F_MODELOF", K_ENTRY, 0x8f3c20, 0x8f3c20, "48 8b 7f 38 48 85 ff 74 0f 48 8b 07 48 8b 40 20", 0x8f3c20, "48 8b 7f 38 48 85 ff 74 0f 48 8b 07 48 8b 40"),
SITE("F_GETNODE", K_ENTRY, 0x91a750, 0x91a750, "41 54 55 48 89 cd 53 48 85 f6 74 34 48 89 f7 48", 0x91a750, "41 54 55 48 89 cd 53 48 85 f6 74 34 48 89 f7"),
SITE("F_NODEXF", K_ENTRY, 0x91c360, 0x91c360, "48 85 f6 0f 84 97 00 00 00 41 55 48 89 f7 49 89", 0x91c360, "48 85 f6 0f 84 97 00 00 00 41 55 48 89 f7 49"),
SITE("F_HASHINDEX", K_ENTRY, 0x91ac20, 0x91ac20, "48 85 f6 74 3b 53 48 89 f7 89 d3 e8 f0 8f fd ff", 0x91ac20, "48 85 f6 74 3b 53 48 89 f7 89 d3 e8 f0 8f fd"),
SITE("F_GETSOCKET", K_ENTRY, 0x91b410, 0x91b410, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 48 89 d3", 0x91b410, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 48 89"),
SITE("F_NUMPIECES", K_ENTRY, 0x91ae60, 0x91ae60, "48 85 f6 74 3b 53 48 89 f7 48 89 d3 e8 af 8d fd", 0x91ae60, "48 85 f6 74 3b 53 48 89 f7 48 89 d3 e8 af 8d"),
SITE("F_GETPIECE", K_ENTRY, 0x91aeb0, 0x91aeb0, "c7 01 ff ff ff ff 48 85 f6 74 4d 55 48 89 f7 48", 0x91aeb0, "c7 01 ff ff ff ff 48 85 f6 74 4d 55 48 89 f7"),
SITE("F_PIECENAME", K_ENTRY, 0x91b510, 0x91b510, "48 85 f6 74 4b 41 54 48 89 f7 49 89 cc 55 44 89", 0x91b510, "48 85 f6 74 4b 41 54 48 89 f7 49 89 cc 55 44"),
SITE("F_HIDEPIECE", K_ENTRY, 0x91b310, 0x91b310, "48 85 f6 74 4b 55 48 89 f7 89 d5 53 0f b6 d9 48", 0x91b310, "48 85 f6 74 4b 55 48 89 f7 89 d5 53 0f b6 d9"),
SITE("F_VIDEOCOVER", K_ENTRY, 0xa10d30, 0xa10d30, "48 81 c7 f0 2a 00 00 e9 f4 3e 21 00 0f 1f 40 00", 0xa10d30, "48 81 c7 f0 2a 00 00 e9 f4 3e 21 00 0f 1f 40"),
SITE("F_BODYSETCAM", K_ENTRY, 0xa4bef0, 0xa4bef0, "48 8b 4f 08 f3 0f 10 06 48 89 f2 f3 0f 10 4e 04", 0xa4bef0, "48 8b 4f 08 f3 0f 10 06 48 89 f2 f3 0f 10 4e"),
SITE("F_CAMRESET", K_ENTRY, 0xa5afb0, 0xa5afb0, "48 8b 87 4c 04 00 00 f3 0f 10 87 54 04 00 00 48", 0xa5afb0, "48 8b 87 4c 04 00 00 f3 0f 10 87 54 04 00 00"),
SITE("F_LOCALPLAYER", K_ENTRY, 0x9e3570, 0x9e3570, "53 48 8d 9f a0 00 00 00 48 8d b7 80 00 00 00 48", 0x9e3570, "53 48 8d 9f a0 00 00 00 48 8d b7 80 00 00 00"),
SITE("F_STIM", K_ENTRY, 0x26c7180, 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5 53", 0x26c7180, "41 57 41 56 41 55 49 89 f5 41 54 55 48 89 d5"),
SITE("F_ATTACKDATA", K_ENTRY, 0xa91300, 0xa91300, "48 8b 35 b9 10 11 04 83 7e 48 01 7e 33 48 85 ff", 0xa91300, "48 8b 35 b9 10 11 04 83 7e 48 01 7e 33 48 85"),
SITE("F_STIM_REMOVE", K_ENTRY, 0x293e150, 0x293e150, "83 fe ff 74 29 80 7f 24 00 75 23 48 8b 47 10 48", 0x293e150, "83 fe ff 74 29 80 7f 24 00 75 23 48 8b 47 10"),
SITE("F_TIMER_CREATE", K_ENTRY, 0x8f8fc0, 0x8f8fc0, "53 48 8b 05 18 1f d7 03 48 89 fb 48 8d b8 70 03", 0x8f8fc0, "53 48 8b 05 18 1f d7 03 48 89 fb 48 8d b8 70"),
SITE("F_TIMER_ATTACH", K_ENTRY, 0x8f8f60, 0x8f8f60, "48 8b 3e 48 85 ff 74 18 48 8b 32 48 85 f6 74 10", 0x8f8f60, "48 8b 3e 48 85 ff 74 18 48 8b 32 48 85 f6 74"),
SITE("F_TIMER_DETACH", K_ENTRY, 0x8f8f90, 0x8f8f90, "48 8b 3e 48 85 ff 74 18 48 83 ec 08 e8 ef cf 07", 0x8f8f90, "48 8b 3e 48 85 ff 74 18 48 83 ec 08 e8 ef cf"),
SITE("F_TIMER_SETOBJ", K_ENTRY, 0x8f9050, 0x8f9050, "48 85 f6 0f 84 4f 01 00 00 53 48 89 f3 48 83 ec", 0x8f9050, "48 85 f6 0f 84 4f 01 00 00 53 48 89 f3 48 83"),
SITE("F_TIMER_GETOBJ", K_ENTRY, 0x8f8ec0, 0x8f8ec0, "48 89 f8 48 85 d2 74 28 48 8b 52 68 48 89 17 48", 0x8f8ec0, "48 89 f8 48 85 d2 74 28 48 8b 52 68 48 89 17"),
SITE("F_TIMER_RELEASE", K_ENTRY, 0x9ec850, 0x9ec850, "48 8b 3f 48 85 ff 74 18 8b 47 08 8d 50 ff 85 c0", 0x9ec850, "48 8b 3f 48 85 ff 74 18 8b 47 08 8d 50 ff 85"),
SITE("F_GETCURANIM", K_ENTRY, 0x91ba10, 0x91ba10, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 0f b6 da", 0x91ba10, "48 85 f6 74 4b 55 48 89 f7 48 89 cd 53 0f b6"),
SITE("F_GETANIMTIME", K_ENTRY, 0x91a520, 0x91a520, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48 89", 0x91a520, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48"),
SITE("F_GETANIMRATE", K_ENTRY, 0x91a580, 0x91a580, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48 89", 0x91a580, "55 48 89 cd 53 48 83 ec 08 48 85 f6 74 32 48"),
SITE("F_SETANIMRATE", K_ENTRY, 0x91bd90, 0x91bd90, "48 85 f6 74 4b 53 48 89 f7 0f b6 da 48 83 ec 10", 0x91bd90, "48 85 f6 74 4b 53 48 89 f7 0f b6 da 48 83 ec"),
SITE("F_KEYLOOKUP", K_ENTRY, 0x91b220, 0x91b220, "41 57 41 56 41 55 41 54 55 53 48 83 ec 08 4c 8b", 0x91b220, "41 57 41 56 41 55 41 54 55 53 48 83 ec 08 4c"),
SITE("F_SAMPLENODE", K_ENTRY, 0x91c410, 0x91c410, "41 56 41 55 41 54 55 53 4c 89 cb 44 8b 74 24 30", 0x91c410, "41 56 41 55 41 54 55 53 4c 89 cb 44 8b 74 24"),
SITE("F_FINDTRACKER", K_ENTRY, 0xc73800, 0xc73800, "41 56 49 89 f6 41 55 41 89 d5 41 54 55 48 89 fd", 0xc73800, "41 56 49 89 f6 41 55 41 89 d5 41 54 55 48 89"),
SITE("F_FX_CSINIT", K_ENTRY, 0xa09e70, 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89 cd", 0xa09e70, "41 55 48 8d 87 60 01 00 00 66 0f ef c0 41 89"),
SITE("F_FX_CREATE", K_ENTRY, 0xd8c7f0, 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40 00", 0xd8c7f0, "80 3d da aa 92 03 00 75 07 31 c0 c3 0f 1f 40"),
SITE("F_FX_CSFREE", K_ENTRY, 0x9ec750, 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00 00", 0x9ec750, "55 53 48 89 fb 48 83 ec 08 80 bf ac 01 00 00"),
SITE("F_FX_RELEASE", K_ENTRY, 0x2aebfd0, 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7 e8", 0x2aebfd0, "48 8b 07 48 85 c0 74 50 53 48 89 fb 48 89 c7"),
SITE("F_ITEM_REATTACH", K_ENTRY, 0x231e2e0, 0x231e2e0, "41 55 49 89 fd 41 54 55 53 48 83 ec 08 0f b6 9f", 0x231e2e0, "41 55 49 89 fd 41 54 55 53 48 83 ec 08 0f b6"),
SITE("F_ITEM_VISIBLE", K_ENTRY, 0x231e700, 0x231e700, "41 57 41 56 41 55 41 54 41 89 f4 55 53 48 89 fb", 0x231e700, "41 57 41 56 41 55 41 54 41 89 f4 55 53 48 89"),
SITE("F_SND_REGOBJ", K_ENTRY, 0x8c97c0, 0x8c97c0, "48 89 d0 48 83 ec 08 89 f7 ba 01 00 00 00 48 89", 0x8c97c0, "48 89 d0 48 83 ec 08 89 f7 ba 01 00 00 00 48"),
SITE("F_SND_SETPOS", K_ENTRY, 0x8c9b80, 0x8c9b80, "41 54 41 89 f4 55 53 48 89 fb 48 83 ec 30 48 8b", 0x8c9b80, "41 54 41 89 f4 55 53 48 89 fb 48 83 ec 30 48"),
SITE("F_SND_POST", K_ENTRY, 0x8c9840, 0x8c9840, "48 83 ec 08 89 f0 83 fe ff 48 c7 c6 ff ff ff ff", 0x8c9840, "48 83 ec 08 89 f0 83 fe ff 48 c7 c6 ff ff ff"),
SITE("F_NAV_POS", K_ENTRY, 0xebc700, 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7 48", 0xebc700, "48 8b 87 50 02 00 00 48 85 c0 74 0f 48 89 c7"),
SITE("F_SUPPORTED", K_ENTRY, 0xa36db0, 0xa36db0, "48 8b 87 20 04 00 00 48 85 c0 74 24 48 8b 40 10", 0xa36db0, "48 8b 87 20 04 00 00 48 85 c0 74 24 48 8b 40"),
SITE("F_AI_SCRIPTTGT", K_ENTRY, 0x296f870, 0x296f870, "48 85 f6 0f 84 b7 00 00 00 41 54 41 89 d4 55 48", 0x296f870, "48 85 f6 0f 84 b7 00 00 00 41 54 41 89 d4 55"),
SITE("F_AI_SCRIPTCLR", K_ENTRY, 0x296f940, 0x296f940, "48 8b 87 78 0c 00 00 48 8b 90 60 0b 00 00 48 85", 0x296f940, "48 8b 87 78 0c 00 00 48 8b 90 60 0b 00 00 48"),
SITE("F_IM_MOVE", K_ENTRY, 0xa0fa20, 0xa0fa20, "41 54 66 0f ef c9 49 89 d4 66 0f ef c0 55 89 f5", 0xa0fa20, "41 54 66 0f ef c9 49 89 d4 66 0f ef c0 55 89"),
SITE("F_IM_DOWN", K_ENTRY, 0xa0f760, 0xa0f760, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53 48", 0xa0f760, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53"),
SITE("F_IM_UP", K_ENTRY, 0xa0f8c0, 0xa0f8c0, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53 48", 0xa0f8c0, "41 55 41 89 f5 41 54 41 89 d4 55 48 89 cd 53"),
SITE("F_VIEW_MSTATE", K_ENTRY, 0x90b960, 0x90b960, "48 83 ec 18 48 8b 7f 18 31 f6 48 8d 4c 24 0c 4c", 0x90b960, "48 83 ec 18 48 8b 7f 18 31 f6 48 8d 4c 24 0c"),
SITE("F_VIEW_TOSTAGE", K_ENTRY, 0xc83ba0, 0xc83ba0, "55 53 48 83 ec 28 48 85 ff 74 18 48 8b 07 89 d5", 0xc83ba0, "55 53 48 83 ec 28 48 85 ff 74 18 48 8b 07 89"),
SITE("F_CLIP_VIEW", K_ENTRY, 0x9783b0, 0x9783b0, "48 8b 47 28 48 8b 78 08 48 8b 07 ff a0 40 01 00", 0x9783b0, "48 8b 47 28 48 8b 78 08 48 8b 07 ff a0 40 01"),
SITE("F_CLIP_HITTEST", K_ENTRY, 0x979db0, 0x979db0, "55 48 89 f8 66 0f ef c0 41 b9 04 00 00 00 f2 0f", 0x979db0, "55 48 89 f8 66 0f ef c0 41 b9 04 00 00 00 f2"),
SITE("F_SURF_FROM_RAY", K_ENTRY, 0x2ae5d90, 0x2ae5d90, "48 8b 47 20 48 85 c0 74 45 0f b7 48 5a 89 ce 85", 0x2ae5d90, "48 8b 47 20 48 85 c0 74 45 0f b7 48 5a 89 ce"),
SITE("F_SPAWN_AI", K_ENTRY, 0x29a4830, 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45 89", 0x29a4830, "48 85 ff 0f 84 b7 07 00 00 41 57 48 89 f8 45"),
SITE("F_TMPL_BYNAME", K_ENTRY, 0x2153910, 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x2153910, "0f b6 07 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("F_ZONE_AT", K_ENTRY, 0x22a2a80, 0x22a2a80, "41 57 41 56 41 55 41 54 49 89 d4 55 53 48 89 fb", 0x22a2a80, "41 57 41 56 41 55 41 54 49 89 d4 55 53 48 89"),
SITE("F_SETLEVEL", K_ENTRY, 0x26ee7c0, 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48 89", 0x26ee7c0, "53 ba 01 00 00 00 48 89 fb e8 82 f7 ff ff 48"),
SITE("F_FM_BIND", K_ENTRY, 0x2975eb0, 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89 d3", 0x2975eb0, "41 56 41 55 49 89 f5 41 54 49 89 fc 55 53 89"),
SITE("F_FM_ACTIVATE", K_ENTRY, 0x22cdad0, 0x22cdad0, "41 55 41 54 41 89 f4 55 48 89 fd 53 48 83 ec 18", 0x22cdad0, "41 55 41 54 41 89 f4 55 48 89 fd 53 48 83 ec"),
SITE("F_FACTION_TARGET", K_ENTRY, 0x23d3f80, 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8 a0 49 00 00", 0x23d3f80, "48 83 ec 08 48 8b b7 e8 00 00 00 e8 a0 49 00"),
SITE("F_FM_TEMPLATE", K_ENTRY, 0x22d71b0, 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03 00 90 66 90", 0x22d71b0, "48 8b 77 60 48 8b 7f 58 e9 e3 39 03 00 90 66"),
SITE("F_CHAR_COMBATREC", K_ENTRY, 0x26ff330, 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85 ff", 0x26ff330, "48 8b 35 09 7f 52 02 83 7e 48 01 7e 2b 48 85"),
SITE("F_AI_FACT", K_ENTRY, 0x29ac890, 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b 6f", 0x29ac890, "41 55 49 89 f5 41 54 55 53 48 83 ec 38 48 8b"),
SITE("F_TOD_NAME", K_ENTRY, 0x25ccd10, 0x25ccd10, "53 0f b6 05 78 8e 66 02 89 fb 84 c0 74 22 39 1d", 0x25ccd10, "53 0f b6 05 78 8e 66 02 89 fb 84 c0 74 22 39"),
SITE("F_WM_CMDID", K_ENTRY, 0xe24570, 0xe24570, "55 53 48 89 fb 48 83 ec 18 48 8b 05 20 f0 d8 03", 0xe24570, "55 53 48 89 fb 48 83 ec 18 48 8b 05 20 f0 d8"),
SITE("F_GRAPH_VAR", K_ENTRY, 0x208dd90, 0x208dd90, "0f b6 06 31 d2 84 c0 74 21 0f 1f 80 00 00 00 00", 0x208dd90, "0f b6 06 31 d2 84 c0 74 21 0f 1f 80 00 00 00"),
SITE("F_GRAPH_GET", K_ENTRY, 0x208de50, 0x208de50, "48 85 f6 74 7b 48 8b 46 18 66 0f ef c0 48 39 87", 0x208de50, "48 85 f6 74 7b 48 8b 46 18 66 0f ef c0 48 39"),
SITE("F_VELMODE", K_ENTRY, 0x20c6ff0, 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40 88", 0x20c6ff0, "80 bf a2 01 00 00 00 74 05 40 84 f6 74 12 40"),
SITE("G22_RUNSTRING", K_VSLOT, 0x344d650, 0x344d640, "20 d3 8b 00 00 00 00 00 30 d3 8b 00 00 00 00 00 b0 d8 8b 00 00 00 00 00 80 d8 8b 00 00 00 00 00 40 d3 8b 00 00 00 00 00", 0x8bd8b0, "53 48 89 f3 e8 77 f8 ff ff 48 89 de 48 89 c7"),
SITE("G22_SERVERGATE", K_VSLOT, 0x344d7c8, 0x344d7c0, "40 e0 8b 00 00 00 00 00 f0 d4 8b 00 00 00 00 00 f0 d9 8b 00 00 00 00 00", 0x8bd4f0, "48 8b 05 e9 d9 da 03 48 83 b8 58 0c 00 00 00"),
SITE("G22_EXEC", K_ENTRY, 0x8b9ac0, 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a 00", 0x8b9ac0, "41 57 41 56 41 55 41 54 55 53 48 81 ec 28 1a"),
SITE("G22_REPO_FIND", K_VSLOT, 0x3910d00, 0x3910d00, "60 5e 45 02 00 00 00 00", 0x2455e60, "41 57 41 56 41 55 41 54 55 31 ed 53 48 83 ec"),
SITE("OBJSYS_MODELHANDLE", K_VSLOT, 0x3453170, 0x3453170, "e0 6a 8c 00 00 00 00 00", 0x8c6ae0, "48 85 f6 74 0b 48 8b 86 e0 00 00 00 c3 0f 1f"),
SITE("SND_POSTGLOBAL", K_VSLOT, 0x346da80, 0x346da80, "30 d7 8e 00 00 00 00 00", 0x8ed730, "80 7f 08 01 75 22 48 85 f6 74 1d 80 3e 00 74"),
SITE("PHYS_RAYCAST", K_VSLOT, 0x3472458, 0x3472458, "d0 4f 8f 00 00 00 00 00", 0x8f4fd0, "48 8b 7f 08 48 85 ff 74 07 e9 02 96 fe ff 66"),
SITE("NPC_SETZONE", K_VSLOT, 0x3a2b658, 0x3a2b658, "b0 39 96 02 00 00 00 00", 0x29639b0, "53 48 89 fb 48 83 ec 10 48 8b 47 10 48 8b 50"),
SITE("OBJSYS_REMOVE", K_VSLOT, 0x34530b0, 0x34530b0, "60 6f 8c 00 00 00 00 00", 0x8c6f60, "48 85 f6 74 1b 48 83 ec 08 48 8b 7f 08 e8 66"),
```

`P_*` are patched (call site or vtable slot), `F_*` are only called and byte-checked, `G22_*` are the console's slots
and functions for `exec`, read and called through their slots, never patched (`G22_RUNSTRING`, `_SERVERGATE` and
`_EXEC` are `re-m1m2.md` section 9's lines; `G22_REPO_FIND` is the repository's find, slot 8 of `0x3910cc0`).
`OBJSYS_MODELHANDLE` (the object system's `vtbl+0x118`, vtable `0x3453058`) and `SND_POSTGLOBAL` (`CSoundMgr::vf_13`)
are two more slots called through (P7). The P7 hooks: `P_STIM_*`/`P_REGSTIM*` (CALLWRAP: the C side calls
`FUN_026c7180` / `RegisterStimulus` with their register arguments), `P_AI_*` (vtable slots of the counter goal and the
block/dodge actions, plain C passing six register arguments through), `P_KEYTIMES`, `P_PJ_DAMAGE`, `P_SND_POST*` and
`P_UISCENE` (PREWRAP, log only: the PJ and sound calls pass arguments on the stack), `P_PJ_UPDATE` (`CProjectile::vf_13`
slot), the 19 `P_FXI_*` (every call of the CLIENTFX_CREATESTRUCT init, one shared C function found by the return
address) and `P_RES3D` (CALLWRAP on the 3D surface desc, re-resolution.md `RES_3D_DESC`).

The P8 hooks:
- `P_IK_SETUP_A`/`_B` and `P_IK_CHAIN` (CALLWRAP on the calls of `FUN_02185900`/`FUN_02185590`, re-arms-ik.md
  `ARM_IK_SETUP_*`/`ARM_IKCHAIN_UPD`).
- `P_IEM_ADDCB`/`_REMCB` (PREWRAP on IEngineModel slots 38/39, log only: AddNodeCallback's seventh argument stays on
  the stack).
- `P_TRK_EVAL` and `P_ATTACH_LISTEN` (vtable slots, plain C, the original first; re-arms-ik.md `ARM_TRK_EVAL`,
  `ARM_ATTACH_LISTENER`).
- `P_LP_GUARD_HOVER`/`_CLICK` (CALLWRAP on the pad guard's two calls, re-ui-pointer.md `LP_GUARD_*`).
- `P_LP_SYS_*`, `P_LP_VIEW_*`, `P_LP_GFX_EVENT` (vtable slots, plain C, re-ui-pointer.md `LP_SYS_*`/`LP_VIEW_*`/
  `LP_GFX_EVENT`).
- `P_GRAV` (PlayerMovement's vtable slot 6, re-traversal.md `TRAV_GRAVITY_WISH`).

The P8 functions: `F_AI_SCRIPTTGT`/`_SCRIPTCLR` (SetCombatScriptedTarget / Clear), `F_IM_MOVE`/`_DOWN`/`_UP`,
`F_VIEW_MSTATE`, `F_VIEW_TOSTAGE`, `F_CLIP_VIEW`/`F_CLIP_HITTEST` (called only through a clip whose vtable slots
120/109 hold them) and `F_SURF_FROM_RAY`. `PHYS_RAYCAST` (IEnginePhysics slot 64) is one more slot called through.

The P9 hooks:
- `P_AIB_GATE`, `P_AIB_REC`, `P_AIB_ROLL`, `P_AIB_NOTIFY` (CALLWRAP on the four calls of the AIBlockTable roll in
  Combat_StartAttack, re-combat-spawn.md `CS_AIBLOCK_GATE`/`_REC`/`_ROLL`/`_NOTIFY`, with 16-byte contexts). The roll's
  C side calls `FUN_00c73440` with the saved xmm0/xmm1, writes the result into the saved xmm0 and reads the chance from
  the saved xmm2.
- `P_GOAL_REL` (CAIGoalCounterMelee vf_03, plain C, the original first; `CS_GOAL_COUNTER_RELEVANT`).
- `P_SHIELD` (PREWRAP, log only, on the damage context's shield test; `CS_SHIELD_TEST`).
- `P_BOW_CAMXF` (CALLWRAP on the free-aim update's camera call, re-bow.md `BOW_FA_CAMXF`).

The P9 functions: `F_SPAWN_AI` (the debug spawn), `F_TMPL_BYNAME` (AI/Templates by name), `F_ZONE_AT` and `F_SETLEVEL`
(the spawn's post steps), `F_FM_BIND`, `F_FM_ACTIVATE`, `F_FACTION_TARGET`, `F_FM_TEMPLATE` (the captain), `F_CHAR_COMBATREC`
(the record the roll reads), `F_AI_FACT` (incoming), `F_TOD_NAME` (tod) and `F_WM_CMDID` (the predicate's command, wmdump).
`NPC_SETZONE` (the NPC's vtbl+0x288) and `OBJSYS_REMOVE` (the object system's vtbl+0x58, the deferred removal) are two
more slots called through. The game services object `*0x46b7310` (slots +0x78, +0xa8) has no fixed vtable to check: the
probe calls a slot only if it points into the game's code.

The P10 hook: `P_ROOTDELTA` (CALLWRAP on `PlayerMovement::vf_11`'s call of `FUN_00a513a0(body, &delta)` at `0xab5ea9`,
design-locomotion.md `LOCO_ROOTDELTA`). The C side calls the original with rdi/rsi, then reads the delta record the
caller keeps at `rsp+0x10..+0x2b` (pos in Talion's frame, quat) and, with `loco` on, rewrites its translation. vf_11
keeps `pm`/`xf` in rbp/rbx and a zero in xmm3 across the call; the wrapper restores all three. The transform comes from
`P_PMROOT` (the slot hook around vf_11 keeps it in a thread-local), else from the root object. When somvr gains its own
`LOCO_ROOTDELTA` (LO-2, behind `SOMVR_LOCO`), `0xab5ea9` must go into build.sh's EXCLUSIVE list.

The P10 functions: `F_GRAPH_VAR` (`FUN_0208dd90(mgr, name)`, the graph variable by name: a hash, then a binary search
of `*(mgr+0x3258)+0x38`), `F_GRAPH_GET` (`FUN_0208de50(mgr, var)`, its float: the slot `*(mgr+0x738) + 0x28*index`,
`+0x10` or `+0x24`, or the slot type's getter), `F_VELMODE` (`FUN_020c6ff0(pm, rootMotion)`, design-locomotion.md
`LOCO_VELMODE`: `pm+0x1a2`, and `pm+0x104..+0x10c` zeroed on the switch to momentum). `pmrec` reads the descriptor array
through its holder `*0x4c254e0` (= `0x4c25520` after `_INIT_1317`, the count 30 at `0x4c254e8`), as `FUN_00ab8700` does:
the fast path when `*(int*)(D+0x18) >= 2`, else the hash search; `heading` casts G21 rays through `PHYS_RAYCAST`.

None of them touches a byte that `mod/spike1` or `mod/somvr` patches (`mod/probe/build.sh --check` proves it), and the
probe never installs the save guard: spike1 keeps `WBOnlineService` slots 219-222, and the probe only *reads* slot 219
to see that the guard is in place before it allows an active word.

P7 adds these writes: `swingevent` registers the attack's stimuli (`FUN_026c7180`; with `cur` `pc+0x4d0/+0x55c/+0x4c0`
for MS, restored) and removes its persistent stimulus (`FUN_0293e150`, `pc+0x168 = 0`); `hitstop` gives one orc its
own timer (CreateTimer/AttachTimer/SetObjectTimer) or sets its trackers' rates to 0, both restored, the timer released;
`projblock` sets one projectile's `+0x228`; `projreflect` rewrites its position, quaternion, homing/spline bytes, target
and (with `own`) shooter; `weaponfx` creates an FX on the active weapon's model object (a loop's handle is released by
`weaponfx stop`); `dagger hand` writes the dagger item's inactive node hash and shown byte and re-attaches it (`back`
restores both); `sound` posts Wwise events (its own game object `0x50a70001`); `piecehide` hides one piece; `restall`
changes the 3D height of the next mode set only.

P8 adds these writes, all undone by the word's `off` (or by the game itself):
- `aggro`: the NPC AI's scripted target (`npc+0xc78` -> `+0xb50..+0xb68`) through `FUN_0296f870`; `aggro off` =
  `FUN_0296f940`, only where it still holds Talion.
- `minhealth`: `CCharacter+0x9b4` of the player, restored by `minhealth off`.
- `armpose`: the right arm's rows of the player tracker's pose buffer, every frame while on (the job rewrites them
  every frame).
- `attachhide`: pieces of an attached child and of the children attached to it (only those the game shows), or the
  render node's skip byte `+0x40` (`node`), shown again by the next `attachhide`/`off`. `piecehide all` does the same
  for hChar's pieces.
- `pointer`/`click`: UI mouse events through CInterfaceMgr (the stored position `im+0x3210`).
- `padguard off`: the pad guard's answer 0.
- `mcmd`: a command ON, then OFF.
- `grav off`: `PlayerMovement::vf_06`'s answer 0.
- `conlog` and every `exec`: the console repository's output slot `0x4b5e928`, put back after the call / at `off` /
  in the destructor.

P9 adds these writes:
- `spawn`/`spawncaptain`: an NPC made by the game's debug spawn with its post steps (and, for a captain, the member
  bound to it); `despawn` queues our spawns' HOBJECTs for the object system's removal.
- `blockroll force`: every AIBlockTable roll returns 0.0 until `blockroll off`.
- `incoming`: one incoming-attack fact in the orc's working memory and its brain's re-plan byte (`FUN_029ac890`).
- `aimray`: the free-aim ray's transform (the caller's local), while on.
- `wmhold`: a command ON, then OFF after MS (as `cmd`).

P10 adds these writes:
- `loco` (and `npcpush`'s walk): the translation of vf_11's root-delta record (the caller's stack local), every vf_11
  call in FSM 6/7 while on; nothing persists after `loco off` or the time.
- `walk`: the StrafeAxis/ForwardAxis values (as `move`), re-aimed every tick, released at MS.
- `fling`: `FUN_020c6ff0(pm, 0)`, `pm+0xe0..+0xe8` (the launch velocity) and the proxy's gravity-on byte `pc+0x60` = 1;
  after the landing `FUN_020c6ff0(pm, 1)` if the game did not put root-motion mode back itself (also on a missing
  liftoff and after 5 s).
- `animrate`: the rate of Talion's main tracker (or every playing one), restored at the end where it is still ours.
- `npcpush`: an orc as `spawn` makes it (left for `despawn`), loco toward it, one `pm+0x428`/`+0x444` push as `pmmove`.
- `pmrec set`: a console variable through `exec` (`pmrec restore` sets the old value back); `pmrec base`:
  `pm+0x32c` (restored by `pmrec base off`).

Data the probe writes (all restored): the near-plane override `0x46b65e4` (`near`), the auto-switch byte `0x46b0d61`
(`padmode`), the player's hidden-piece mask through `SetPieceHidden` (`hidehead`/`showhead`), and `Combat+0x15c` /
`+0x27a` of the player's `Combat` for the duration of one `dealhit` call. Not restored: `pmmove` writes
`PlayerMovement+0x428` (3 floats) and the pending byte `+0x444`, which the game's next `vf_11` consumes and resets
(so it moves Talion once), and whatever an `exec` command does (P5 sets its cvar back itself).

## 4. What the coordinator must change in `scripts/boot_check.sh`

The harness is frozen for the pending run; these are the changes a probe run needs (in the order they matter).
**Done 2026-10-19 on branch `som-bootcheck-probe`** (merge after run 2; test: `scripts/test_boot_check_probe.sh`):
items 1 and 2, with these differences: boot_check sets the three `SOMVR_PROBE_*` paths under RUN itself when a
preload is named `somvr_probe*.so` (a `--game-env` value wins and must be absolute); `probe` waits up to 2 s for the
probe's `CMD queued` line, because the probe reads only the versions it sees (a 100 ms poll: two renames inside one
poll lose the first); the plan prints `!!! probe: NO save guard` without spike1 + `--save-copy`; summary.txt has a
probe block.

1. **Two preload libraries (blocks every active word).** `--preload` takes one `SO` and `GPRE` sets
   `LD_PRELOAD="$PRELOAD"`. The probe must load next to spike1, because spike1 owns the fail-closed save guard and
   two libraries patching the same slots would make the second exit 3. Make `--preload` repeatable and join the paths
   with `:` (the order does not matter; each library gates on its own env variable):
   ```sh
   --preload) PRELOADS+=("${2:?}"); shift 2 ;;
   # ... then, where GPRE is built:
   [ ${#PRELOADS[@]} -gt 0 ] && GPRE=(LD_PRELOAD="$(IFS=:; echo "${PRELOADS[*]}")" SOMVR_LOG=... SOMVR_TRIGGER=...)
   ```
   The ELF64 check and `launch-cmd.txt`'s sha256 line should loop over the list. Without this change a run can only
   use the read-only words (and only with the probe alone, i.e. **no save guard**, which must not happen on a run
   that loads the user's save).
2. **A `probe` command** so a slot can be driven live, like `trigger` is today:
   ```sh
   probe) printf '%s\n' "$*" > "$RUN/probe.cmd.tmp" && mv -f "$RUN/probe.cmd.tmp" "$RUN/probe.cmd"
     log "probe: $* at game time $(gel) s" ;;
   ```
   plus `probe` in the `--cmds` validator's allow-list (`^(shot|key|xdo|mark|wait|trigger|untrigger|quit)$`) and
   `SOMVR_PROBE_CMD=$RUN/probe.cmd` next to the other `--preload` variables. The tmp + rename matters: the probe
   takes a file version only once and ignores a half-written file.
3. **Nothing else is needed.** `--game-env` already accepts `SOMVR_PROBE_LOG`, `SOMVR_PROBE_OUT`, `SOMVR_PROBE_CMD`
   and `SOMVR_PROBE_AUTO` (only `SOMVR_LOG`/`SOMVR_TRIGGER` are refused), and `--save-copy` already puts spike1's
   guard in place. A line in `summary.txt` for `probe.log` (as the `--preload` block does for `somvr.log`) would be
   convenient, not necessary.
4. **Keys, not clicks** (run 1: the results dialog ignored clicks, `Return` worked). The P1/P2 command files use
   keys only; the mouse coordinates of attempt 3 are kept as comments for a fallback.

## 5. Questions no probe here can answer

- **The console/cfg executor** (`design-vr.md` G22): answered 10-19 in `re-m1m2.md` (G22): commands run in-process
  with `(*(void***)eng)[32](eng, "cmd a b; cmd2")`, `eng = *(void**)0x466afa8` (`CWBEngine::vf_32` `0x8bd8b0`), on the
  game thread, no hook. The release build keeps `Teleport`, `SlowMO.*`, `Character.*`, `Faction.Dump`, `SetResolution`;
  `SlowMO.*`/`Character.*` do nothing without a loaded world, and some dumps have their output compiled out
  (`PlayerInput.Bindings.Dump`). The probe word is `exec` (10-19, run P5, K1); P4 can then set the resolution with
  `exec SetResolution` instead of seeded prefs.
- **Achievements and stats** (design-vr.md D16): where to block them at the game's layer is not mapped. Until it is,
  a probe run must not do anything that could unlock an achievement (hence: the dev save, and no story progress).
- **The real headset** (frame rate, comfort): Monado first, the Quest only at milestones.
