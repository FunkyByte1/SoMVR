# win-perf1.cmd - the Windows build's frame cost (perf27-drive.sh's places; scripts/headless_win.sh WIN_CMDS with
# SOMVR_PERF=on SOMVR_XR_PERF=on SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600 SOMVR_HANDS_TEST=1 SOMVR_XR_FOG=radial): each place
# held for a named window (`perf NAME` to both sides: the DLL's PERF lines, the layer's `xr: perf NAME:` lines and its
# per-thread CPU from /proc schedstat), the window logged when the next starts; a dump after each (sun, fog, fire)
trigger
wait 30
key Return
wait 6
key Return
waitlog 150 ctx=world
waitlog 60 arrived at
wait 8
xrtest btn R a 1
wait 1
xrtest btn R a 0
wait 2
xrtest head 0 0 0 yaw 0 pitch 0
wait 2
trigger perf meadow
xrtest perf meadow
wait 30
trigger perf meadow-turn
xrtest perf meadow-turn | head 0 0 0 yaw 60 pitch 0 s 4
wait 9
xrtest head 0 0 0 yaw 0 pitch 0 s 4
wait 5
trigger perf after-meadow
xrtest perf after-meadow
xrtest dump
wait 2
trigger practice group 8
wait 6
trigger perf crowd
xrtest perf crowd
wait 30
trigger perf after-crowd
xrtest perf after-crowd
xrtest dump
wait 2
trigger practice remove
wait 3
trigger practice go 30873.5 3900 -32973.7
waitlog 40 arrived at
wait 4
xrtest head 0 0 0 yaw 0 pitch -12
wait 2
trigger perf fire
xrtest perf fire
wait 30
trigger perf end
xrtest perf end
xrtest dump
wait 4
