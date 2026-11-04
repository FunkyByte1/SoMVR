# win-vr6.cmd - somvr-vr6.cmd for the Windows build (headless_vr.sh --win's default; headless_win.sh's command mode):
# stereo armed, the main menu's Continue (Play -> Continue Story Mode: the run's copy of the save), the world, then the
# drivers take over (SOMVR_PRACTICE=auto blinks to the meadow by itself). The GOG build shows no launcher, no movies
# with the prefix's settings; its main menu answers a few seconds after `menus=15` (winport-base's runs: 30 s from the
# launch).
trigger
mark stereo-armed
waitlog 150 SESSION ui ifs=1 menus=15
mark main-menu
wait 6
shot 05-main-menu
key Return
wait 6
shot 06-game-mode
key Return
mark continue-story
waitlog 150 ctx=world
mark world
wait 10
shot 08-world
