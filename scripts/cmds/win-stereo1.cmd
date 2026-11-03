# win-stereo1.cmd - the first stereo boot of the Windows build (scripts/headless_win.sh WIN_CMDS): stereo armed (the
# SOMVR_TRIGGER file, as somvr-vr6.cmd), the main menu, Play -> Continue Story Mode (a copy of the user's Linux save:
# it loads on the GOG build), the world. (The world's `SESSION ui ... ctx=world` line does not come on Windows yet:
# the pause flag read stays 1 - waits by time.)
trigger
wait 30
shot 01-menu
key Return
wait 6
shot 02-game-mode
key Return
wait 45
shot 03-world
wait 30
shot 04-world
xrtest head 0 0 0 yaw 60 s 1
wait 10
shot 05-yaw60
xrtest head 0 0 0 s 1
wait 30
shot 06-world
