# probe-P8.cmd - probe run P8 (docs/runtime-probes.md section 2 P8): a real fight (aggro = SetCombatScriptedTarget on the
# picked orcs, minhealth keeps Talion alive) with CV1 (stimlog, aiwatch, swingevent), the parry window against the
# key times (keytimes), the counter prompt (prompts watch, the snap at each counter prompt at Talion), hit-stop at 120 ms
# on the playing tracker (hitstop onbpw, hitstop 120); the head sweep over the player model's attached children
# (attached, piecehide all, attachhide); the arm words (ikwatch, ikcb, wnodes, armpose); the UI pointer (mouselog,
# mstate, pointer, click, padguard, hitscan) in the pause menu and at the main menu; surf, grav and mount; archers for
# the projectile test (pick Archer, projblock, projreflect); trav's head height and the dagger's later measure.
# Fully scripted: run 4's menu, load and quit steps (somvr-run4.cmd, 2026-10-19 21:11; P7's probe-P7.cmd), the rest
# `probe` lines with a shot before each key step. The user's save copy loads at a forge tower top with no orcs near
# (run 3, P7). spike1 holds the save guard (the probe refuses every active word without it).
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p8-world;orcs;timers' \
#     --cmds scripts/cmds/probe-P8.cmd --hold 720
#
# Read in RUN/probe.log: GUARDCHECK ... patched; HOOK lines (73, all ok); FUNC 56 of 56; CONSOLE ... PHYS_RAYCAST ok; then
# per step the lines named in the doc's pass/fail list (AGGRO/AGGROWATCH, COUNTERPROMPT ... (THE PLAYER), KEYTIMES/
# KEYHIT with the parry window, PROMPTS, STIM/STIMREG, AI, SWING, HITSTOP, ATTACH/PIECEMASK, IKSLOT/IKSETUP/IKCHAIN/
# IKWATCH/IKCB, WNODE/WSOCKET/WATTACH, ARMPOSE, MOUSE/MSTATE/HITSCAN/CLICK, SURF, TRAV, PROJ*, CONOUT). RUN/probe-out:
# wnodes-N.tsv. About 8.5 minutes of game time (waits 461 s plus ~50 s of shots and probe sends); --hold 720 = 12 min.
#
# 1. pre-game window with the "Unsupported Linux distribution or version" box: Continue Anyway, then PLAY (run 4)
wait 30
shot 01-pregame-window
xdo mousemove 684 425
wait 1
xdo click 1
wait 3
shot 02-pregame-display-tab
xdo mousemove 858 601
wait 1
xdo click 1
mark play-pressed
# 3. the game's own alert: Return = Continue Anyway
wait 10
shot 03-alert
key Return
mark alert-continue
# 4. legal text, logos, intro: three Escapes 5 s apart from 12 s on
wait 12
shot 04a-intro
key Escape
wait 5
shot 04b-intro
key Escape
wait 5
shot 04c-intro
key Escape
mark intro-skipped
# 5. main menu: mouse off the menu; the front-end words; the pointer hovers only here (no click, so nothing can open:
# OPTIONS at 136,499 then back on PLAY at 117,266, the item Return then activates; no world = no pad guard)
wait 15
shot 05-main-menu
xdo mousemove 1900 1060
wait 3
shot 06-main-menu-b
mark main-menu
probe mark p8-menu;mouselog on;mstate;ui;res
wait 2
probe pointer 136 499;mstate;hitscan
wait 2
shot 07-menu-hover-options
probe pointer 117 266;mstate
wait 2
shot 08-menu-hover-play
probe mouselog off
wait 1
# 6. load the save: PLAY (focused) -> Select Game Mode -> Continue Story Mode; in the world ~55 s after (run 3)
key Return
wait 4
shot 09-game-mode
key Return
mark continue-story
wait 30
shot 10-load-a
wait 30
shot 10-load-b
wait 10
shot 11-settled
mark world
# 7a. tower top (20 s margin over run 3's load): the skeleton, the pieces and the attached children, the weapon model
wait 20
shot 12-tower-top
probe mark p8-dumps;nodes;pieces;attached;weapons;wnodes
wait 3
probe mark ik-idle;ikwatch 20;ikcb 20;trav 4;surf 3;mount 3;exec AIDebug.ListAIs
wait 3
# the head: is the visible body hChar's own model at all (all 17 pieces at once), then each attached child alone
# (pieces; then the first four by the render node's skip byte), a shot each, from behind Talion (P7: piecehide 0-16)
probe mark head;piecehide off;attachhide off
wait 1
shot 20-head-base
probe piecehide all
wait 2
shot 21-head-pieces-all
probe piecehide off;attachhide 0
wait 2
shot 22-attach-0
probe attachhide 1
wait 2
shot 22-attach-1
probe attachhide 2
wait 2
shot 22-attach-2
probe attachhide 3
wait 2
shot 22-attach-3
probe attachhide 4
wait 2
shot 22-attach-4
probe attachhide 5
wait 2
shot 22-attach-5
probe attachhide 6
wait 2
shot 22-attach-6
probe attachhide 7
wait 2
shot 22-attach-7
probe attachhide 8
wait 2
shot 22-attach-8
probe attachhide 9
wait 2
shot 22-attach-9
probe attachhide 0 node
wait 2
shot 23-attach-0-node
probe attachhide 1 node
wait 2
shot 23-attach-1-node
probe attachhide 2 node
wait 2
shot 23-attach-2-node
probe attachhide 3 node
wait 2
shot 23-attach-3-node
probe attachhide off
wait 1
# the right arm in the phase-2 pose: turned about the forearm, then two IK targets (cm in Talion's model space:
# x right, y up, z forward of the root at pelvis height)
probe mark arms;armpose rot 60
wait 3
shot 30-armpose-rot
probe armpose ik 25 10 35
wait 3
shot 31-armpose-ik-low
probe armpose ik 30 55 40
wait 3
shot 32-armpose-ik-high
probe armpose off
wait 2
shot 33-armpose-off
# gravity off: 30 cm up by the one-shot move, held 3 s, then on (T7)
probe mark grav;trav 10;grav off;pmmove 0 30 0
wait 3
shot 34-grav-off
probe grav on
wait 3
shot 35-grav-on
# 7b. the fight: the nearest orc (not a slave, human, animal, troll or spider) picked, Talion 3 m from it, 1 m up; he
# cannot drop below 20 health; the loggers; then three orcs get Talion as their scripted combat target
probe mark fight;pick;minhealth 20;exec Teleport @ox+300 @oy+100 @oz
wait 3
shot 40-teleported
probe stimlog on;aiwatch on;keytimes on;prompts watch on;exec Combat.Debug.Prompt 1;conlog on;ikwatch 90;ikcb 90
wait 1
probe mark aggro;aggro 3;orcs
wait 10
shot 41-aggro-10s
wait 10
shot 42-aggro-20s
wait 10
shot 43-aggro-30s
probe orcs;prompts;timers
wait 2
# CV1: the stock strikes at an orc that fights back (the AI's block/dodge), then the synthetic swings with a
# Weapon_XSwing record (P7: only WeaponImpactSound records were tried)
probe mark stock;cmd CombatPrimary01
wait 1
probe cmd CombatPrimary01
wait 1
probe cmd CombatPrimary01
wait 2
shot 44-stock
probe mark swing;swingevent PC_HC_High_L1_D0_F_LR_RR_UD_2 400 cur
wait 3
probe swingevent PC_HC_High_L1_D0_F_LR_RR_UD_2
wait 3
probe swingevent auto 400 cur;dealhit auto
wait 3
shot 45-swings
# CV2 at 120 ms: armed on the next counter prompt at Talion (an attacking orc: is its APPLYDAMAGE late by the hold?),
# then on a hit orc; the HITSTOP lines carry the playing tracker's time
probe mark hitstop;hitstop 120 onbpw
wait 12
shot 46-hitstop-onbpw
probe dealhit auto;hitstop 120
wait 3
shot 47-hitstop-hit
# C7: the counter by input when a parry window opens
probe mark counter;autocounter CombatPrimary02 3
wait 20
shot 48-counters
probe autocounter off;orcs;timers;prompts
wait 2
# CI1-CI3: archers (by class) get Talion as their target; the next projectile near him blocked, then one turned back
probe mark archers;aggro off;pick Archer;projlog on
wait 1
probe aggro 2 Archer;projblock 3
wait 25
shot 50-archers
probe projreflect 3 own
wait 25
shot 51-reflect
probe mark fight-done;aggro off;proj off;projlog off;autocounter off;prompts watch off;exec Combat.Debug.Prompt 0
wait 1
probe stimlog off;aiwatch off;keytimes off;conlog off;minhealth off;ikwatch off;ikcb off;release
wait 2
shot 52-fight-done
# 8. the pause menu: the pointer in pad mode (the guard: a highlight only), then with padguard off (OPTIONS opens);
# OPTIONS at 190,495 (P7 shot 70-pause)
key Escape
wait 3
shot 60-pause
probe mark pause-pointer;mouselog on;mstate;padmode on
wait 1
probe pointer 190 495;hitscan;mstate
wait 2
shot 61-pause-hover-padmode
probe click
wait 3
shot 62-pause-click-guarded
probe padguard off;pointer 190 495
wait 2
shot 63-pause-hover-unguarded
probe click
wait 3
shot 64-pause-options
probe padmode off;padguard on;mouselog off;mstate
wait 1
key Escape
wait 3
shot 65-pause-back
# back to the world and into the pause menu again (it opens on its first item; the pointer hovers SAURON'S ARMY at
# 240,248 as well: KBM mode, no guard), then run 4's quit: QUIT = 5 Downs, Return, Yes by click
key Escape
wait 3
key Escape
wait 3
probe pointer 240 248
wait 1
shot 70-pause-again
key Down
wait 1
key Down
wait 1
key Down
wait 1
key Down
wait 1
key Down
wait 1
shot 71-on-quit
key Return
wait 3
shot 72-quit-dialog
xdo mousemove 930 641
wait 1
xdo click 1
wait 15
shot 73-main-menu
# 9. the pointer at the main menu (no player, no guard): OPTIONS clicked (Escape closes it, attempt 3), the corners
# in mstate (the mapping), then QUIT GAME (147,557) clicked and the dialog confirmed with Return
probe mark menu-pointer;mouselog on;mstate;pointer 136 499;hitscan
wait 2
shot 80-menu-hover-options
probe click
wait 3
shot 81-menu-options
key Escape
wait 3
shot 82-menu-back
probe pointer 0 0;mstate
wait 1
probe pointer 1919 1079;mstate
wait 1
probe pointer u=0.5 v=0.5;mstate
wait 1
probe pointer 147 557;hitscan
wait 2
shot 83-menu-hover-quit
probe click;mark p8-done
wait 3
shot 84-quit-dialog
key Return
mark quit-sent
wait 15
quit
