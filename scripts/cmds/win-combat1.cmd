# win-combat1.cmd - the Windows build's first combat (scripts/headless_win.sh WIN_CMDS; with SOMVR_HANDS_TEST=1 and
# SOMVR_XR_DUMP on): the meadow as win-stereo2.cmd, the card put away (the right A), the test hands at rest, the sword
# drawn from the right shoulder (squeeze at the slot, out, let go: kills31-drive.sh's draw_slot), an infantry, the
# test guard (`fight guard learn` then `path`: blocks on the learnt strike paths), then edge cuts across its chest
# (kills31's EDGE: hits) until it dies (a kill: SOMVR_KILLS plays the victim's half). Dumps along the way.
trigger
wait 30
key Return
wait 6
key Return
waitlog 150 ctx=world
waitlog 60 arrived at
wait 6
xrtest btn R a 1
wait 1
xrtest btn R a 0
wait 2
trigger hands test L -22 -35 25 0 0 0
wait 1
trigger hands test R 28 -40 20 0 0 0
wait 1
trigger hands test R 10 -10 -15 0 0 0
wait 1
trigger hands squeeze R 1
wait 1
trigger hands test R 30 -25 40 0 -90 0
wait 1
trigger hands squeeze R 0
wait 1
trigger hands test R 28 -40 20 0 0 0
wait 2
mark drawn
xrtest dump
wait 1
shot 07-drawn
trigger practice remove
wait 2
trigger practice spawn infantry
wait 4
trigger execute face
wait 2
xrtest dump
trigger fight guard frame head
wait 1
trigger fight guard learn
wait 20
trigger execute face
wait 1
trigger fight guard path
wait 20
mark guard
xrtest dump
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
xrtest dump
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 1
xrtest dump
wait 3
xrtest dump
wait 6
shot 08-after
