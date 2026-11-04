# win-bow.cmd - (2) the fog pictures (the meadow's sun, both eyes) and (4) the bow: bow20-drive.sh driven on this
# Windows run (scripts/headless_win.sh WIN_CMDS `driver`; boot with SOMVR_PRACTICE_FAR_CM=1000 SOMVR_BOW_HITLOG=1
# SOMVR_HANDS_TEST=1 SOMVR_XR_DUMP=99999 for dumps on demand)
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
xrtest head 0 0 0 yaw -60 pitch 5 s 1
wait 2
xrtest dump
xrtest head 0 0 0 yaw 0 pitch 0 s 1
wait 2
driver bow20-drive.sh B20_STEPS=auto~aim
