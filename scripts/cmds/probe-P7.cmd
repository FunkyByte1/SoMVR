# probe-P7.cmd - probe run P7 (docs/runtime-probes.md section 2 P7): the AI's view of a swing (stimlog, aiwatch,
# swingevent), hit-stop by the orc's own timer (timers, hitstop), the attack key times (keytimes), combat items
# (projlog/projblock/projreflect, fxlog/weaponfx, weapons/dagger, syncinfo, soundlog/sound), HUD data (ui, navdump,
# prompts, uiscene), Faction.Dump to a C: path (@cout), the piece sweep (piecehide), the traversal smoke test (trav,
# fsm=) and at the end, back at the main menu, the render size (res, restall, exec SetResolution; re-resolution.md 1.5).
# Fully scripted: run 4's menu, load and quit steps (somvr-run4.cmd, 2026-10-19 21:11), the rest `probe` lines with
# shots. The user's save copy loads at a forge tower top with no orcs near: `pick` takes the nearest live AI character
# that is not a slave, human or animal (run 3: AI_Infantry 53-100 m away) and `exec Teleport @ox+400 @oy+100 @oz` puts
# Talion 4 m from it, 1 m up. spike1 holds the save guard (the probe refuses every active word without it).
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | world+3 mark p7-world;orcs;ui;res' \
#     --cmds scripts/cmds/probe-P7.cmd --hold 900
#
# Read in RUN/probe.log: GUARDCHECK ... patched; HOOK lines (56, all ok); then per step the lines named in the doc's
# pass/fail list (STIM/STIMREG, AI, SWING ... result, KEYTIMES/KEYHIT, TIMER/HITSTOP, PROJ*, FX/FXNAMES/WEAPONFX,
# WEAPON/DAGGER, SYNCINFO/SYNC, SOUND/SOUNDNAMES, UI formats, NAV, PROMPT, UISCENE*, EXEC ... file 'C:\..' -> host,
# TRAV, RES). RUN/probe-out: attacks.tsv, attacks-full.tsv, nav-N.tsv, fxnames-N.tsv, the copied somvr-faction-P7*.
# About 7.5 minutes of game time. Steps 1-6 and 8 are somvr-run4.cmd's (same GUESS/REUSED notes there).
#
# 1. pre-game window with the "Unsupported Linux distribution or version" box: Continue Anyway, then PLAY
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
# 5. main menu: mouse off the menu; the front-end words (U8 UI scene, U1/U2/U10, the R1 baseline)
wait 15
shot 05-main-menu
xdo mousemove 1900 1060
wait 3
shot 06-main-menu-b
mark main-menu
probe mark p7-menu;uiscene;ui;res
wait 2
# 6. load the save: PLAY -> Select Game Mode -> Continue Story Mode; in the world ~55 s after (run 3)
key Return
wait 4
shot 07-game-mode
key Return
mark continue-story
wait 30
shot 08-load-a
wait 30
shot 08-load-b
wait 10
shot 09-settled
mark world
# 7a. tower top (20 s margin over run 3's load; the AUTO world+3 line marks the real arrival): the dumps
wait 20
shot 10-tower-top
probe mark p7-dumps;timers;weapons;navdump;prompts
wait 2
probe syncinfo PC_Bip_StealthTorment_B PC_Bip_StealthTorment_F PC_Bip_SneakInstaKill_Loud_B PC_Bip_Sync_Cntr_F PC_Bip_StealthTorment_*
wait 2
probe attacks full
wait 4
# K1: Faction.Dump to C:\Temp (= RUN/home/.local/share/feral-interactive/ShadowOfMordor/VFS/Temp), then the other spellings
probe mark faction;exec Faction.Dump @cout\somvr-faction-P7a.txt
wait 2
probe exec Faction.Dump C:/Temp/somvr-faction-P7b.txt\; Faction.Dump C:\Feral\Temp\somvr-faction-P7c.txt
wait 2
probe mark logs-on;stimlog on;aiwatch on;keytimes on;projlog on;fxlog on;soundlog on;sound ui_game_back
wait 2
# CI6: the dagger to L_Weapon and back
probe mark dagger;dagger hand
wait 2
shot 11-dagger-hand
probe dagger back
wait 2
shot 12-dagger-back
# P1q: one piece hidden at a time, a shot each (the camera behind Talion: hood, hair, helmet, cloak)
probe mark pieces;piecehide off
wait 1
shot 19-pieces-base
probe piecehide 0
wait 1
shot 20-piece-00
probe piecehide 1
wait 1
shot 20-piece-01
probe piecehide 2
wait 1
shot 20-piece-02
probe piecehide 3
wait 1
shot 20-piece-03
probe piecehide 4
wait 1
shot 20-piece-04
probe piecehide 5
wait 1
shot 20-piece-05
probe piecehide 6
wait 1
shot 20-piece-06
probe piecehide 7
wait 1
shot 20-piece-07
probe piecehide 8
wait 1
shot 20-piece-08
probe piecehide 9
wait 1
shot 20-piece-09
probe piecehide 10
wait 1
shot 20-piece-10
probe piecehide 11
wait 1
shot 20-piece-11
probe piecehide 12
wait 1
shot 20-piece-12
probe piecehide 13
wait 1
shot 20-piece-13
probe piecehide 14
wait 1
shot 20-piece-14
probe piecehide 15
wait 1
shot 20-piece-15
probe piecehide 16
wait 1
shot 20-piece-16
probe piecehide off
wait 1
# 7b. to the orcs: pick the nearest live AI character (not a slave/human/animal) and teleport 4 m from it, 1 m up
probe mark teleport;pick;trav 15;exec Teleport @ox+400 @oy+100 @oz
wait 3
shot 30-teleported
probe orcs;timers;prompts watch on;exec Combat.Debug.Prompt 1
# CV3/C6: the orcs attack a still Talion (KEYTIMES/KEYHIT, their stimuli, AI lines, prompts)
wait 20
shot 31-fight-a
wait 10
shot 32-fight-b
# CV1: the player's stock attacks, then the synthetic swings (step 1, then step 1+2), each followed by a hit
probe mark stock;cmd CombatPrimary01
wait 1
probe cmd CombatPrimary01
wait 1
probe cmd CombatPrimary01
wait 2
shot 33-stock-attacks
probe mark swing;swingevent auto
wait 1
probe dealhit auto
wait 2
probe swingevent auto
wait 1
probe dealhit auto
wait 2
probe swingevent auto 400 cur
wait 1
probe dealhit auto
wait 2
probe swingevent auto 400 cur
wait 1
probe dealhit auto
wait 2
shot 34-swings
# from here Talion counters on his own (C7), so he survives the rest
probe mark counter;autocounter CombatPrimary02 3
wait 8
# CV2: hit-stop - the design's 120 ms, a 600 ms hold with a shot inside it, recipe B (the trackers' rates)
probe mark hitstop;dealhit auto;hitstop 120
wait 3
probe dealhit auto;hitstop 600
shot 35-hitstop-600
wait 3
probe hitstop 1500 rate
shot 36-hitstop-rate
wait 3
# CI4/CI5: our FX on the sword (the last FX name seen on a player weapon, else the last one)
probe mark fx;weaponfx last
wait 2
shot 37-weaponfx
probe weaponfx last loop
wait 3
shot 38-weaponfx-loop
probe weaponfx stop
wait 3
shot 39-weaponfx-stopped
# CI2/CI3: the next projectile near Talion blocked, then the next one turned back (only if archers shoot)
probe mark proj;projblock 2
wait 15
probe projreflect 3 own
wait 15
shot 40-proj
# 7c. the HUD data in the camp, a sound at the hand, the loggers off (their tables)
probe mark hud;navdump;prompts;orcs;timers;sound last hand
wait 3
probe mark logs-off;autocounter off;prompts watch off;exec Combat.Debug.Prompt 0;stimlog off;aiwatch off;keytimes off
wait 1
probe projlog off;fxlog off;soundlog off;proj off;release;mark p7-world-done
wait 2
shot 41-p7-world-done
# 8. quit to the main menu (run 4): pause menu QUIT = 5 Downs, Return, Yes by click
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
# 9. R1 at the main menu (re-resolution.md 1.5): the control without the hook, then tall, then back
probe mark res-control;res;exec SetResolution 1832 1920
wait 5
shot 80-res-control
probe res;mark res-tall;restall on;exec SetResolution 1832 1920
wait 5
shot 81-res-tall
probe res;mark res-back;exec SetResolution 1920 1080;restall off
wait 5
shot 82-res-back
probe res;uiscene;mark p7-done
wait 3
shot 83-main-menu-after
# 10. QUIT GAME (run 4): 5 Downs, Return, Return
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
