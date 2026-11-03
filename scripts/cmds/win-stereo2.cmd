# win-stereo2.cmd - the Windows build into the world in stereo (scripts/headless_win.sh WIN_CMDS; with
# SOMVR_XR_DUMP=300,700,1100,2000: 700 falls inside the 20 s turn - stereo frames run at the null compositor's 20 Hz): stereo armed, Play -> Continue Story Mode (a copy of the user's Linux save), the world
# (`SESSION ui ... ctx=world`, IN_PAUSE_FLAG fixed), SOMVR_PRACTICE=auto's trip to the meadow, then the scripted head
# turned 60 deg right for 20 s (the views follow it) and back
trigger
wait 30
shot 01-menu
key Return
wait 6
shot 02-game-mode
key Return
waitlog 150 ctx=world
mark world
wait 15
shot 03-world
waitlog 60 PRACTICE
wait 20
shot 04-practice
xrtest head 0 0 0 yaw 60 s 1
wait 10
shot 05-yaw60
wait 10
xrtest head 0 0 0 s 1
wait 40
shot 06-later
