# win-fire-climb.cmd - (1) the campfire's perf window, (2) the fog pictures (the sun in the meadow, the fire), (3) the
# climb: fling31-drive.sh's ladder plan driven on this Windows run (scripts/headless_win.sh WIN_CMDS `driver`)
trigger
wait 30
key Return
wait 6
key Return
waitlog 150 ctx=world
waitlog 60 arrived at
wait 8
xrtest btn R a 1
xrtest btn R a 0
xrtest head 0 0 0 yaw 0 pitch 0
wait 2
xrtest dump
wait 2
trigger practice go 30873.5 3900 -32973.7
waitlog 40 arrived at
wait 4
xrtest head 0 0 0 yaw 0 pitch -12
wait 2
trigger perf fire
xrtest perf fire
wait 30
trigger perf after-fire
xrtest perf after-fire
xrtest dump
wait 2
xrtest head 0 0 0 yaw 0 pitch 0
trigger practice back
wait 6
driver fling31-drive.sh PLAN_B31=ladder
