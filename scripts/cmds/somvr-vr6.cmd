# somvr-vr6.cmd - VR build 6's start sequence: from the launch straight to the main menu's Continue and the story, no
# clicks. The user after headset session 2: "it still went to the intermediate launcher with the prompt warning about
# compatibility. Can you make it skip that completely?" and "make it skip all the launch movies? I just had to sit there
# waiting 30 seconds for all the company logos". Needs the build-6 launch (scripts/som-vr-headset.sh,
# scripts/headless_vr.sh): --game-arg --no-feral-options (no pre-game window) and the run's own prefs with
# GameOptionsDialogShouldShow 0, SkipOSWarnings 1, SpecificationAlerts\LnxDistributionUnsupported 1 (no
# unsupported-distro box, neither the window's nor FeralLinuxMessage's) and DisableSplashVideos 1 (the game's own
# "+SkipSplashScreens 1": the legal screens), docs/re-launcher.md 1-2 and 8; and SOMVR_INPUT=1 (somvr.so's `SESSION ui`
# lines: the interface state machines, a video; what waitlog and skipvideos read).
# Run 1 (logs/boot-check-20261023-123345, nothing pressed): ~25 s black loading (a spinner), then five videos
# (`video=1`: the Feral logo, the middleware logos, the title) ~28 s, the main menu (`ifs=1 menus=15`) at 53 s; the
# first Return opens the game mode (the same state), the second loads the story (`ifs=3`, paused while loading, ~40 s),
# then the world (`ctx=world`); SOMVR_PRACTICE=auto takes Talion to the practice area.
# Run 2 (logs/boot-check-20261023-160221, `skipvideos ... Escape`): Escape as each video started skipped none (the five
# at +24 s .. +55 s, 4.0 4.0 4.6 4.4 13.4 s; the menu at +55 s): the splash movies take a skip only after the loading
# has finished (docs/re-launcher.md 8). So no key here: SOMVR_SKIP_SPLASH=1 (somvr.so) never queues them; without it
# they play (~31 s) and this waits. Nothing is pressed before the main menu (no stray Escape, no quit prompt).
# Run 3 (logs/boot-check-20261023-165133, SOMVR_SKIP_SPLASH=1): no movie; the main menu at +31 s, the world +83 s, the
# practice meadow +90 s (run 2: +55 / +102 / +110 s).
trigger
mark stereo-armed
# the boot movies (none with SOMVR_SKIP_SPLASH=1), until the main menu
waitlog 150 SESSION ui ifs=1 menus=15
mark main-menu
wait 2
shot 05-main-menu
# Continue (the first item), then the game mode's first item (the story)
key Return
wait 4
shot 06-game-mode
key Return
mark continue-story
# the load: the world state for a moment, then paused while it loads, then the world
waitlog 30 SESSION ui ifs=3 menus=0 paused=1
waitlog 150 SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world
mark world
wait 10
shot 08-world
