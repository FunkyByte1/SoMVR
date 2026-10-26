# somvr-run4.cmd - run 4 (docs/re-xr-pipeline.md 4.6.5 check 1): the real library mod/somvr on the desktop, layout B with
# fake views (SOMVR_STEREO=B SOMVR_VIEWS=fake), no Vulkan layer, no runtime. The user's save copy (somvr's guard with the
# "\GameData.sav" fix, 1c284dd), a still camera, stereo only while the trigger file stands. All in this file (run 3's
# menu knowledge: NOTES.md 2026-10-19 19:04).
#   boot_check.sh --steam --save-copy --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $SOM/mod/somvr/build/somvr.so --game-env SOMVR_STEREO=B --game-env SOMVR_VIEWS=fake \
#     --game-env SOMVR_COMFORT=1 --cmds scripts/cmds/somvr-run4.cmd
# Read in somvr.log: HOOK H8 ok jmp 0x7c9de5, seven D3D ... ok, no "WARN layout B without"; after the trigger
# "EYES made 1: 2 textures WxH format F bind 0x28", EYES gen=.. until both vkimage= are non-zero, per frame two
# "H8 ... -> copied" (eye 0 then 1, rt the same, rtflags=0x37d7), never other-target / not-3d-surface / no-texture in
# steady play, H9 eyes=3; at exit END ... eye_copies = 2 x stereo frames, eye_misses=0. Shots: one normal view (eye R +
# HUD), no split, no black frame; RATE frame time within ~0.5 ms of the untriggered seconds.
#
# 1. pre-game window with the "Unsupported Linux distribution or version" box. GUESS: wait 30 (attempt 3 clicked at 49 s)
wait 30
shot 01-pregame-window
# Continue Anyway (in the box). REUSED 684 425
xdo mousemove 684 425
wait 1
xdo click 1
wait 3
shot 02-pregame-display-tab
# 2. PLAY (bottom right of the pre-game window). REUSED 858 601
xdo mousemove 858 601
wait 1
xdo click 1
mark play-pressed
# 3. the game's own alert (FeralLinuxMessage, same unsupported-distro text). REUSED: up ~8 s after PLAY
wait 10
shot 03-alert
# Return = Continue Anyway (focused). REUSED key Return
key Return
mark alert-continue
# 4. legal text, logos, intro: Escape skips the videos (run 2). Run 2 without skipping: the Monolith logo 30 s after the
# alert, the main menu by 60 s (attempt 3: ~40 s). GUESS: three Escapes 5 s apart from 12 s on, a shot before each so
# the shots show what each one hit. If 04b/04c already show the main menu, the next Escape landed there: check 05/06
# for a dialog before stage 2 (no Return is sent here, so nothing is confirmed).
wait 12
shot 04a-intro
key Escape
wait 5
shot 04b-intro
key Escape
wait 5
shot 04c-intro
key Escape
mark intro-skipped
# 5. MAIN MENU with the user's profile: mouse off the menu (no hover), shots, stop.
wait 15
shot 05-main-menu
xdo mousemove 1900 1060
wait 3
shot 06-main-menu-b
mark main-menu
mark stage1-done
# 6. Load the save: PLAY (highlighted) -> Select Game Mode -> Continue Story Mode (first entry). Run 3: in the world
# ~55 s after, at a forge tower top; no input after that (still camera).
key Return
wait 4
shot 07-game-mode
key Return
mark continue-story
wait 30
shot 08-load-a
wait 30
shot 08-load-b
wait 10
shot 09-settled
mark world
# 7. Stereo off/on/off: 10 s each (the eye textures are made at the first stereo frame).
wait 10
shot 40-off-a
trigger
mark stereo-on
wait 2
shot 41-on-2s
wait 8
shot 42-on-10s
untrigger
mark stereo-off
wait 5
shot 43-off-b
# 8. Quit (run 3): pause menu QUIT = 5 Downs, Yes by click; main menu QUIT GAME = 5 Downs, Return, Return.
key Escape
wait 2
shot 70-pause
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
shot 71-on-quit
key Return
wait 3
shot 72-quit-dialog
xdo mousemove 930 641
wait 1
xdo click 1
wait 15
shot 73-main-menu
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
shot 74-on-quit-game
key Return
wait 3
shot 75-quit-dialog
key Return
mark quit-sent
wait 15
