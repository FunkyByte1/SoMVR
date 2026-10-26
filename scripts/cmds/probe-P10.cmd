# probe-P10.cmd - probe run P10 (docs/runtime-probes.md section 2 P10; design-locomotion.md 2.10 and 4.2): the stock
# locomotion measured (speed per tick, the stick sweep along a free heading, the sprint), the player record and its
# console names (pmrec; BaseSpeedMultiplier through the console and the cache), our velocity instead of the root
# motion (loco const / stick through P_ROOTDELTA), launches (fling), a tracker rate on Talion (animrate), the stock
# vault over an orc and Shadow Strike (vaultlog), Talion's proxy against an orc (npcpush), and free-run into a wall,
# stock vs loco.
# Fully scripted: run 4's menu, load and quit steps (as probe-P9.cmd), the rest `probe` lines with a shot at each key
# step. The user's save copy loads at the forge tower top; the drill runs at P9's arena (re-combat-spawn.md 3, 53 m
# east). spike1 holds the save guard (the probe refuses every active word without it).
# P9's lessons: `minhealth 20` from the arena on and never turned off (the game quits with it; a runtime flag, the save
# guard holds every write); `despawn all` before the drill and after each orc step; no captain at all (P9: `despawn all`
# in a captain's showdown left the presentation running and swallowed the next steps); `exec Player.CanBeAttacked 0`
# is left out (it stopped no attack in P9). Every run first asks `heading` for free ground (16 rays with ground checks):
# P9's arena spot faces a rock wall, and the patrol of P9 came from the west within a minute, so the stick sweep is
# the first thing done there.
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p10-world;orcs;timers' \
#     --cmds scripts/cmds/probe-P10.cmd --hold 600
#
# Read in RUN/probe.log: GUARDCHECK ... patched; HOOK lines (81, P_ROOTDELTA ok); FUNC 71 of 71; CONSOLE ... ok; then
# per step the lines of the doc's pass/fail list (PMREC, HEADING, WALK, SPEED ... end, LOCO, FLING, ANIMRATE, VAULT,
# NPCPUSH, PMMOVE, TRAV, SPAWN/SPAWNCHECK, WMHOLD, DESPAWN). RUN/probe-out: speed-N.tsv (one per window), fling-N.tsv,
# vault-N.tsv.
# About 7 minutes of game time (see the doc for the count); --hold 600 = the 10-minute cap.
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
# 5. main menu: mouse off the menu (no hover)
wait 15
shot 05-main-menu
xdo mousemove 1900 1060
wait 3
shot 06-main-menu-b
mark main-menu
probe mark p10-menu
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
# 7a. tower top: the player record, its 30 attributes and their console names (pmrec), the light, the orcs; then 2 s
# standing still (the graph's values at rest, the idle root)
wait 20
shot 12-tower-top
probe mark p10-tower;pmrec;tod;orcs
wait 2
probe speed 2
wait 3
# 7b. the arena (P9's spot, 8 m NW of the slave group): Talion cannot drop below 20 health from here to the quit; no orc
# of ours from before; the freest direction for the first band (the spot faces a rock)
probe mark arena;minhealth 20;exec Teleport 40400 3900 -21600
wait 5
shot 20-arena
probe despawn all;orcs;trav 2;heading open 25
wait 3
shot 21-heading
# 7c. the stick sweep (LR1): the stock stick along the heading (re-aimed through the camera every tick) for 3 s, then
# 1 s to stop; each band first asks for free ground nearest Talion's facing (no turn at the start); the full band ends
# in a reversal (heading back after 2.9 s, 2 s the other way)
probe mark band-0.2;speed 4;walk 0.2 3000
wait 5
probe mark band-0.4;heading clear 12;speed 4;walk 0.4 3000
wait 5
probe mark band-0.6;heading clear 15;speed 4;walk 0.6 3000
wait 5
probe mark band-0.8;heading clear 20;speed 4;walk 0.8 3000
wait 5
probe mark band-1.0;heading clear 25;speed 7;walk 1 3000;sleep 2900;heading back;walk 1 2000
wait 2
shot 22-run
wait 6
# 7d. the sprint: FreeRun and Jump held (both are pad A's; in KBM mode Space = Jump holds the free run) with the full
# stick
probe mark sprint;heading clear 35;speed 6;cmd FreeRun Jump ms=4000;walk 1 4000
wait 2
shot 23-sprint
wait 5
# 7e. BaseSpeedMultiplier (LR2): through its console name (if the find knows one), then straight into its cache
# pm+0x32c; the full band again each time (a speed change = something reads it)
probe mark basespeed-cvar;pmrec set BaseSpeedMultiplier 1.5;heading clear 25;speed 4;walk 1 3000
wait 5
probe pmrec restore;mark basespeed-cache;pmrec base 1.5;heading clear 25;speed 4;walk 1 3000
wait 5
probe pmrec base off
wait 1
# 7f. our velocity instead of the root motion (LR3, LR4): 500 and 900 cm/s along the heading with the stick at rest,
# 500 with the stick at 0.3, at 1.0 and with FreeRun (which clips play?), then 1:1 from the stick at 500 cm/s (0.4 for
# 2 s: 200 cm/s, then the stop inside the window)
probe mark loco-500;heading clear 20;speed 4;loco const 0 500 3
wait 2
shot 30-loco-500
wait 3
probe mark loco-900;heading clear 32;speed 4;loco const 0 900 3
wait 5
probe mark loco-500-s03;heading clear 20;speed 4;loco const 0 500 3;walk 0.3 3000
wait 5
probe mark loco-500-s10;heading clear 20;speed 4;loco const 0 500 3;walk 1 3000
wait 2
shot 31-loco-run
wait 3
probe mark loco-500-free;heading clear 20;speed 4;loco const 0 500 3;cmd FreeRun Jump ms=3000;walk 1 3000
wait 5
probe mark loco-stick;heading clear 12;speed 6;loco stick 5;walk 0.4 2000
wait 7
# 7g. launches (LR5): straight up at 6 m/s (90 cm at the proxy's -20 m/s2), then 5 m/s up and 4 m/s along the heading
probe mark fling-up;speed 3;fling 0 600 0
wait 1
shot 40-fling-up
wait 3
probe mark fling-fwd;heading clear 8;speed 3;fling 0 500 400
wait 4
# 7h. a tracker rate on Talion (LR8): the main tracker at 0.7 for 3 s in the middle of a 6 s run at 0.6
probe mark animrate;heading clear 25;speed 7;walk 0.6 6000;sleep 2500;animrate 0.7 3
wait 4
shot 41-animrate
wait 4
# 7i. the stock vault over an orc (LR6) and Shadow Strike at it (LR10): an infantry 3 m ahead of Talion (turned to free
# ground first); the evade (CombatPrimary04: pad A / Space) while running at it, under vaultlog; then the aim (124 by
# the exact mode name: P9's wmhold fix) and Z (CombatKBM07, the hint's key); the orc removed after
probe mark vault;heading clear 8;walk 0.2 800
wait 2
probe spawn AI_Infantry 300
wait 3
shot 50-vault-orc
probe heading orc;vaultlog 5;walk 0.6 1500;sleep 500;cmd CombatPrimary04
wait 2
shot 51-vault
wait 4
probe mark strike;heading orc;walk 0.2 400;sleep 600;vaultlog 6;wmhold PC_WraithRanged 3000;sleep 1300;cmd CombatKBM07
wait 3
shot 52-strike
wait 4
probe despawn all
wait 1
# 7j. Talion's proxy against an orc (LR7): an infantry 1.5 m along the heading, loco at it at 2.5 m/s to 1 m past it,
# then a one-shot push of up to 2 m at it
probe mark npcpush;heading clear 6;npcpush 150 250
wait 4
shot 53-npcpush
wait 2
probe despawn all
wait 1
# 7k. free-run into the nearest wall (LR9): the stock run first, then back off and the same with loco owning the
# velocity (the probe's stick 1:1 at 500 cm/s); trav and vaultlog show a climb or vault (FSM 9)
probe mark wall-stock;heading wall 12;trav 5;speed 5;vaultlog 5;cmd FreeRun Jump ms=3500;walk 1 3500
wait 2
shot 60-wall-stock
wait 4
probe mark wall-back;heading back;walk 0.5 2500
wait 3
probe mark wall-loco;heading wall 12;trav 5;speed 5;vaultlog 5;loco stick 4;cmd FreeRun Jump ms=3500;walk 1 3500
wait 2
shot 61-wall-loco
wait 4
# 7l. the end: nothing of ours left on (minhealth stays: the quit follows)
probe mark p10-end;loco off;walk off;animrate off;pmrec base off;pmrec restore;despawn all;orcs
wait 1
probe release
wait 2
shot 69-end
# 8. quit (run 4): pause menu QUIT = 5 Downs, Return, Yes by click; main menu QUIT GAME = 5 Downs, Return, Return
key Escape
wait 2
shot 70-pause
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
shot 74-on-quit-game
key Return
wait 3
shot 75-quit-dialog
key Return
mark quit-sent
wait 15
quit
