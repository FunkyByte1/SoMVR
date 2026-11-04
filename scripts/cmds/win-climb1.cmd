# win-climb1.cmd - the climb on the Windows build: fling31-drive.sh's ladder plan (the glide down over the edge, the
# hang, the fling down to the meadow) driven on this run (scripts/headless_win.sh WIN_CMDS `driver`; docs/re-winport.md 7.4)
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
wait 4
driver fling31-drive.sh PLAN_B31=ladder~wall~swordlow
