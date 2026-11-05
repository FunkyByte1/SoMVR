# win-ladder-new.cmd - som-b37-ladder (docs/re-xr-pipeline.md 172): a NEW GAME for the story ladder, in the ladder's own
# prefix (logs/winport/prefix-ladder, its save removed first: scripts/ladder_boot.sh new), never a slot's or the user's.
# Stereo armed, the main menu, then pictures only: the menu's way to a new game is driven by the lines appended to RUN/cmd
# (the ladder's new-game driver, or by hand while the boot holds).
trigger
mark stereo-armed
waitlog 150 SESSION ui ifs=1 menus=15
mark main-menu
wait 8
shot nm-01-main-menu
