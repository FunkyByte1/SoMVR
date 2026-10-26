# probe-P2.cmd - probe run P2 (docs/runtime-probes.md section 2): combat - our own hits through Combat_DealHit,
# telegraph timing, a kill and the Nemesis credit. Run it only on the dev save (a kill is permanent in the Nemesis
# system) and only with spike1's save guard in place (--save-copy and both preloads).
#
#   cd /home/bara/Projects/flat2vr/ShadowOfMordor && scripts/boot_check.sh --steam --save-copy \
#     --seed-prefs <the prefs of the P1 run> \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/spike1/build/somvr_spike1.so \
#     --preload /home/bara/Projects/flat2vr/ShadowOfMordor/mod/probe/build/somvr_probe.so \
#     --game-env SOMVR_COMFORT=1 \
#     --game-env SOMVR_PROBE_AUTO='start state on | world+2 attacks;orcs | world+6 mark walk;move 0 1 3000 | world+12 orcs | world+14 mark dealhit;dealhit auto | world+18 orcs;dealhit auto | world+22 dealhit auto;dealhit auto | world+28 mark kill;dealhit auto kill | world+34 orcs | world+40 mark counter;autocounter CombatPrimary02 3 | world+90 autocounter off;orcs' \
#     --cmds scripts/cmds/probe-P2.cmd --hold 900
#
# The attack record: `dealhit auto` picks the best name match from the game's own records (and prefers the last
# attack the player started, which the ATK hook logs). After the P1 run has produced probe-out/attacks.tsv, replace
# `auto` with a real name, e.g. `dealhit PlayerLightAttack1`.
# boot_check.sh sets SOMVR_PROBE_CMD/_LOG/_OUT under RUN itself (see probe-P1.cmd's header, also for the --dry-run
# check); a word outside the plan during the run: append `probe WORDS` to RUN/cmd (e.g. `probe orcs`).
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
# the probe's own words run from here (world+2 .. world+90). Shots around each phase.
wait 6
shot 10-attacks-dumped
wait 8
shot 11-after-walk
wait 4
shot 12-before-hit
wait 4
shot 13-hit-1
wait 4
shot 14-hit-2
wait 6
shot 15-hit-3-4
wait 6
shot 16-kill
wait 6
shot 17-after-kill
mark counter-phase
# the counter phase: the orcs attack, autocounter presses the counter command when a parry window opens.
# Stand still (no move words): the shots show whether a counter animation plays.
wait 10
shot 18-counter-1
wait 10
shot 19-counter-2
wait 10
shot 20-counter-3
wait 10
shot 21-counter-4
wait 10
shot 22-counter-5
mark p2-done
wait 10
shot 23-settled
key Escape
wait 3
shot 24-pause-menu
mark quit-start
wait 5
quit
