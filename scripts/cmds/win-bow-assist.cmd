# win-bow-assist.cmd - win-bow.cmd for bow20-drive.sh auto~aim with its beside step's 5 deg cone set by the `bow assist`
# word (winport-combat, 11-03): the headset envs since build 20 have SOMVR_BOW_ASSIST_DEG=0 (no cone, no assist line).
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
