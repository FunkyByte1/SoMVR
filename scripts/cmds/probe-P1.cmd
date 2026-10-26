# probe-P1.cmd - probe run P1 (docs/runtime-probes.md section 2): input injection, the pose/piece/binding dumps.
# Keys only (run 1: the results dialog ignored mouse clicks, Return worked); the attempt-3 mouse coordinates are
# kept as comments for a fallback. The probe's own words come from SOMVR_PROBE_AUTO, so this file only walks the
# menus, marks the phases and takes shots. Extra words during the run: append `probe WORDS` lines to RUN/cmd.
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | menu+1 bindings;ui | world+2 mark world;bindings;pieces;nodes;orcs;attacks | world+8 padmode on;bindings | world+11 mark input;cmd Jump ms=120 | world+14 mark pose-move;pose 180;move 0 1 3000 | world+19 orcs | world+22 move 0 0 1;look 0.4 0 800 | world+26 pad A 150 | world+29 pad LY=0.9 2000 | world+33 pad X 150 | world+36 release;nodes;mark input-done' \
#     --cmds scripts/cmds/probe-P1.cmd --hold 900
#
# boot_check.sh (branch som-bootcheck-probe, docs/runtime-probes.md section 4) joins the two --preload paths into
# LD_PRELOAD and gives the probe SOMVR_PROBE_CMD=RUN/probe.cmd, SOMVR_PROBE_LOG=RUN/probe.log and
# SOMVR_PROBE_OUT=RUN/probe-out by itself (RUN is only known once it runs: do not pass them with --game-env).
# Check the plan first with --dry-run added: the "launch: game only" line must show both libraries and the three
# paths, and there must be no "!!! probe: NO save guard" line. A boot_check.sh that takes one --preload only cannot
# run this file: loading the probe alone means NO save guard (SOMVR_PROBE_NOGUARD_OK=1 is only for a run that never
# loads the user's save, and this one does).
# 1. the pre-game window ("Unsupported Linux distribution or version" box). GUESS: Return activates Continue Anyway
wait 30
shot 01-pregame-window
key Return
wait 2
shot 02-pregame-after-return
# if shot 02 still shows the box: REUSED attempt 3's clicks (append live)
#   xdo mousemove 684 425 / xdo click 1
# 2. PLAY in the pre-game window. GUESS: Tab walks to it, Return presses it
key Tab
wait 1
shot 03-pregame-tab
key Return
mark play-pressed
# REUSED fallback for PLAY: xdo mousemove 858 601 / xdo click 1
# 3. the game's own alert (FeralLinuxMessage, same text). REUSED: up ~8 s after PLAY, Return = Continue Anyway
wait 10
shot 04-alert
key Return
mark alert-continue
# 4. legal text, Feral logo, intro, then the MAIN MENU by itself (~40 s after the alert)
wait 30
shot 05-intro
wait 30
shot 06-main-menu
mark main-menu
# the probe writes bindings/ui for the menu state here (SOMVR_PROBE_AUTO: menu+1)
wait 3
# 5. PLAY -> the save slots -> load. The menu order is PLAY, SAVE SLOTS, WBPlay, MARKETPLACE, OPTIONS, QUIT GAME.
# GUESS: the first item is focused, Return opens it; the shots after every key show where the highlight is.
key Return
wait 3
shot 07-after-play
key Return
wait 3
shot 08-after-play-2
mark loading
# 6. the world loads (~60-90 s). The probe fires its `world` words by itself once a live player exists.
wait 30
shot 09-loading
wait 30
shot 10-world-maybe
wait 20
shot 11-world
mark world-reached
# 7. the probe's dumps and the pose run (world+2 .. world+13)
wait 10
shot 12-after-dumps
# 8. input injection (world+14 .. world+34): a shot every few seconds shows whether the player really moved
wait 5
shot 13-padmode
wait 5
shot 14-jump
wait 5
shot 15-move-1
wait 5
shot 16-move-2
wait 5
shot 17-look
wait 5
shot 18-pad-a
wait 5
shot 19-pad-stick
wait 8
shot 20-input-done
mark input-shots-done
# 9. a second look at the state after everything (the probe keeps logging STATE changes)
wait 20
shot 21-settled
mark p1-done
# 10. quit through the menus (ESC -> the pause menu; Return confirms). Keys only.
key Escape
wait 3
shot 22-pause-menu
mark quit-start
wait 5
quit
