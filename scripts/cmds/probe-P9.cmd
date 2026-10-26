# probe-P9.cmd - probe run P9 (docs/runtime-probes.md section 2 P9): test orcs from our code (spawn by archetype, a
# Nemesis captain from a Faction slot), the AIBlockTable roll that starts an orc's block/dodge AI (blocktable,
# aiblockwatch, blockroll force), our own incoming-attack notification (incoming), a dealhit against a shield defender,
# the arena light (tod, weathers, AdvanceTimeOfDay), and the bow (wmdump, wmhold, aimray: re-bow.md BW1/BW3).
# Fully scripted: run 4's menu, load and quit steps (somvr-run4.cmd; P8's probe-P8.cmd), the rest `probe` lines with a
# shot before each key step. The user's save copy loads at the forge tower top with no orc within ~50 m (run 3, P7, P8);
# the fights run at re-combat-spawn.md 3's quiet ground 53 m east of it. spike1 holds the save guard (the probe refuses
# every active word without it).
# P8's lesson (Talion died after `minhealth off` while orcs still attacked): `minhealth 20` from the arena on; the only
# `exec Player.CanBeAttacked 1` (the captain's counter test) is followed by `... 0` before any orc is removed, and
# `minhealth off` comes last, with Player.CanBeAttacked still 0 and every spawned orc removed (it is a runtime flag;
# nothing of it is saved, the save guard holds every write).
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p9-world;orcs;timers' \
#     --cmds scripts/cmds/probe-P9.cmd --hold 600
#
# Read in RUN/probe.log: GUARDCHECK ... patched; HOOK lines (80, all ok); FUNC 68 of 68; CONSOLE ... NPC_SETZONE ok,
# OBJSYS_REMOVE ok; then per step the lines named in the doc's pass/fail list (TEMPLATE, SPAWN/SPAWNCHECK, BLOCKTBL,
# AIBLOCK gate/reached/roll/notify/goal_counter/shield, AI, ATK, HITEV block=, INCOMING, DEALHIT, AUTOCOUNTER, DESPAWN,
# TOD, WEATHER, WMODE/WMSETUP, WMHOLD, AIMRAY, PROJ new). RUN/probe-out: templates.tsv, weathers.tsv, wmodes-N.tsv,
# somvr-faction-P9a/b.txt. About 6.5 minutes of game time (356 s of waits plus ~30 s for 39 shots and 36 probe sends);
# --hold 600 = the 10-minute cap.
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
probe mark p9-menu
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
# 7a. tower top (20 s margin over run 3's load): the light, the Nemesis state before (P7's dump is stale: P8 made a new
# captain only in its own copy), the AI templates with their archetypes (one game-thread scan, at most 2 s), the weapon
# modes, the nearest orcs' block tables
wait 20
shot 12-tower-top
probe mark p9-tower;tod;weathers;orcs;exec Faction.Dump @cout\somvr-faction-P9a.txt
wait 2
probe templates
wait 3
probe wmdump;blocktable 3
wait 2
# 7b. the arena (re-combat-spawn.md 3: 8 m NW of the slave group): Talion cannot drop below 20 health and no AI may
# attack him from here on (CanBeAttacked 0); no armed AI within 30 m expected (orcs)
probe mark arena;minhealth 20;exec Teleport 40400 3900 -21600
wait 5
shot 20-arena
probe exec Player.CanBeAttacked 0;orcs;trav 3
wait 3
# 7c. a shield defender 4 m ahead, facing Talion; its table; the loggers; it gets Talion as its scripted target but may
# not attack him (it stands in combat, out of its own attacks: the counter goal's relevance needs that)
probe mark defender;spawn AI_Defender,Defender,Shield 400
wait 4
shot 21-defender
probe blocktable 1 last;aiblockwatch on;aiwatch on;aggro 1
wait 5
shot 22-defender-aggro
# the stock baseline: five strikes 1.5 s apart (ATK, AIBLOCK gate/reached/roll/notify, AI, HITEV block= from the
# shield), then the table against the strike's own record
probe mark stock;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01
wait 8
shot 23-stock
probe blocktable 1 last
wait 1
# every roll forced: five more strikes (each one that has a table entry notifies: AI goal_counter, block_valid/go)
probe mark forced;blockroll force;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01
wait 8
shot 24-forced
probe blockroll off
wait 1
# our own notification (step 1b) with no attack, then a dealhit 0.3 s later (does the shield / a block take it?);
# three times, then once with our own roll from the table (the last strike's record)
probe mark incoming;incoming;sleep 300;dealhit auto;sleep 1700;incoming;sleep 300;dealhit auto;sleep 1700;incoming;sleep 300;dealhit auto
wait 6
shot 25-incoming
probe incoming roll last;sleep 300;dealhit auto
wait 2
probe mark defender-done;aggro off;despawn
wait 2
shot 26-defender-gone
# 7d. a berserker (dodge pools), the same with the roll forced and our notification
probe mark berserker;spawn AI_Berserker,Berserker 400
wait 4
shot 30-berserker
probe blocktable 1 last;aggro 1
wait 5
probe mark bzrk-forced;blockroll force;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01;sleep 1500;cmd CombatPrimary01
wait 7
shot 31-berserker-forced
probe blockroll off;incoming;sleep 300;dealhit auto;sleep 1700;incoming;sleep 300;dealhit auto
wait 4
shot 32-berserker-incoming
probe mark berserker-done;aggro off;despawn
wait 2
# 7e. a Nemesis captain from slot (1, 3) (P7's dump: Gham the Hook, AI_Infantry, Counters_Your_Counter_1; the SPAWN
# line names the member's template), allowed to attack for 20 s with the counter pressed at each parry window
# (KBM mode: CombatKBM09 = RMB, P8's C7 next step); then no attacks again before he is removed
probe mark captain;exec Faction.SetTarget 1 3
wait 1
probe spawncaptain 400
wait 4
shot 40-captain
probe blocktable 1 last;aggro 1;exec Player.CanBeAttacked 1
wait 4
probe mark counter;autocounter CombatKBM09 3
wait 10
shot 41-captain-10s
wait 10
shot 42-captain-20s
probe autocounter off;exec Player.CanBeAttacked 0
wait 2
probe incoming;sleep 300;dealhit auto
wait 2
probe mark captain-done;aggro off;despawn all;exec Faction.Dump @cout\somvr-faction-P9b.txt
wait 3
shot 43-captain-gone
# 7f. the bow (re-bow.md BW1/BW3): the aim mode's own command held 7 s (the first mode named Bow, else the first with
# a FreeAim component), the aim ray turned 20 deg right / 5 deg up from the right hand, a stock shot by LMB (KBM mode),
# then by RT's command; projlog shows the arrows
probe mark bow;projlog on;wmhold Bow,freeaim 7000
wait 2
shot 50-bow-aim
probe aimray 20 5 hand 6
wait 2
shot 51-aimray
probe cmd CombatKBM05
wait 2
shot 52-shot-lmb
probe cmd CombatSecondary04
wait 2
shot 53-shot-rt
probe aimray off;projlog off;release
wait 1
# 7g. the light: one AdvanceTimeOfDay (its step size), after the fights so it cannot spoil their shots
probe mark light;tod;exec GameServerShell.AdvanceTimeOfDay
wait 4
probe tod
wait 1
shot 60-tod
# 7h. the end: loggers off, nothing of ours left in the world; minhealth off last (CanBeAttacked still 0)
probe mark p9-end;aiblockwatch off;aiwatch off;aggro off;blockroll off;despawn all;orcs
wait 1
probe minhealth off;release
wait 2
shot 61-end
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
