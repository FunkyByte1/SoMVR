# probe-P5.cmd - probe run P5 (docs/runtime-probes.md section 2): console and movement - the engine's console
# executor in-process (exec: a cvar read back, Teleport +1 m, Faction.Dump to an absolute path) and PlayerMovement's
# root-motion slot (pmcount, the one-shot extra move pmmove, once in the open and again at a wall). re-m1m2.md
# section 10, X1 and X2. Run it only on the dev save and only with spike1's save guard in place (--save-copy and both
# preloads): exec and pmmove are active words.
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs <the prefs of the P1 run> \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | world+2 mark console;exec Combat.Debug.Attack 1 | world+5 exec Combat.Debug.Attack 0\; Teleport @x+100 @y @z | world+9 exec Faction.Dump @out/somvr-faction-P5.txt | world+12 mark pm;pmcount 5 | world+19 pmmove 10 0 0 | world+22 pmmove 0 0 10 | world+25 move 0 1 2000;pmcount 2 | world+29 mark pm-done' \
#     --cmds scripts/cmds/probe-P5.cmd --hold 900
#
# In the words: `\;` is a ';' inside one exec string (the probe's own ';' separates probe words), @x/@y/@z the
# player root's coordinates (here world x + 100 cm), @out the probe-out folder under RUN. pmmove is in cm in
# Talion's frame (x right, y up, z forward). Read the EXEC, ROOT, PMCOUNT and PMMOVE lines of RUN/probe.log
# (the pass/fail list is in the doc's P5).
# boot_check.sh sets SOMVR_PROBE_CMD/_LOG/_OUT under RUN itself (see probe-P1.cmd's header, also for the --dry-run
# check). The wall phase at the end is sent with `probe` lines from this file; if shot 15 shows no wall in front of
# Talion, append `probe look 0.5 0 800` and `probe move 0 1 4000` to RUN/cmd, then the two `probe pmmove 0 0 30`.
# Steps 1-6 are probe-P1.cmd's (same GUESS/REUSED notes).
wait 30
shot 01-pregame-window
key Return
wait 2
shot 02-pregame-after-return
key Tab
wait 1
key Return
mark play-pressed
wait 10
shot 03-alert
key Return
mark alert-continue
wait 30
shot 04-intro
wait 30
shot 05-main-menu
mark main-menu
wait 3
key Return
wait 3
shot 06-after-play
key Return
wait 3
mark loading
wait 30
shot 07-loading
wait 30
shot 08-world-maybe
wait 20
shot 09-world
mark world-reached
# the probe's own words run from here (world+2 .. world+29). Shots around each phase.
wait 5
shot 10-console
wait 4
shot 11-after-teleport
wait 6
shot 12-pm-phase
wait 10
shot 13-after-pmmove
wait 8
shot 14-after-walk
mark wall-phase
# the wall: walk into whatever is ahead, then the extra move forward into it (does Talion stop at the wall?)
probe mark wall-walk;move 0 1 4000
wait 6
shot 15-at-wall
probe mark wall-pmmove-1;pmmove 0 0 30
wait 2
shot 16-wall-pmmove-1
probe mark wall-pmmove-2;pmmove 0 0 30
wait 2
shot 17-wall-pmmove-2
mark p5-done
wait 5
shot 18-settled
key Escape
wait 3
shot 19-pause-menu
mark quit-start
wait 5
quit
