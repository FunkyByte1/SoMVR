# win-quitpath.cmd - the coordinator's quitpath.cmd (logs/coord) for the Windows build (final.sh FINAL_WIN=1, boot 1;
# winport-drivers 11-03): the same keys, but 4 Downs on the main menu - the GOG build's has 5 rows (PLAY, SAVE
# SLOTS, MARKETPLACE, OPTIONS, QUIT TO DESKTOP; 5 Downs wrap to PLAY: Continue Story Mode again). The pause menu's
# 5 Downs to its quit are the same on both.
trigger
mark stereo-armed
waitlog 150 SESSION ui ifs=1 menus=15
mark main-menu
wait 2
shot 05-main-menu
key Return
wait 4
shot 06-game-mode
key Return
mark continue-story
waitlog 30 SESSION ui ifs=3 menus=0 paused=1
waitlog 150 SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world
mark world
wait 25
shot 08-world
key Escape
wait 3
shot q1-pause
key Down
wait 1
key Down
wait 1
key Down
wait 1
key Down
wait 1
key Down
wait 1
shot q2-on-quit
key Return
wait 3
shot q3-quit-dialog
key Return
waitlog 60 SESSION ui ifs=1 menus=15
wait 3
shot q4-main-menu
key Down
wait 1
key Down
wait 1
key Down
wait 1
key Down
wait 1
shot q5-on-quit-game
key Return
wait 3
shot q6-quit-game-dialog
mark confirm
key Return
