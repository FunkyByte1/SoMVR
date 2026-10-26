# somvr-run5.cmd - run 5 (docs/run5-monado-plan.md): the first OpenXR runtime test. mod/somvr layout B with real XR views
# (SOMVR_STEREO=B SOMVR_VIEWS=xr) and VK_LAYER_SOMVR_openxr in the game, Monado headless (simulated HMD + null
# compositor, started by the operator BEFORE this run, stopped after it). Copy of somvr-run4.cmd with longer load waits
# (the null compositor paces the session at 20 Hz) and a 40 s stereo window. All in this file.
#   (bash, outside the sandbox; R5 = logs/run5-<ts> holds Monado's files, made by the operator block in the plan,
#   section 2; boot_check replaces @RUN@ with the run folder)
#   boot_check.sh --steam --save-copy --seed-prefs $SOM/logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $SOM/mod/somvr/build/somvr.so --game-env SOMVR_STEREO=B --game-env SOMVR_VIEWS=xr \
#     --game-env SOMVR_COMFORT=1 --game-env VK_LAYER_PATH=$SOM/mod/somvr/vklayer \
#     --game-env VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr \
#     --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json --game-env SOMVR_XR=1 \
#     --game-env SOMVR_XR_LOG=@RUN@/xr-layer.log --game-env SOMVR_XR_DUMP=60,300,700 \
#     --game-env XR_LOADER_DEBUG=info --game-env VK_LOADER_DEBUG=layer --cmds $SOM/scripts/cmds/somvr-run5.cmd
# Read: the plan's section 4 (layer log RUN/xr-layer.log, somvr.log, $R5/monado.log). Shots: the desktop mirror only
# (eye R + HUD, as run 4); the null compositor shows nothing. The XR images: RUN/xr-layer.log.stereo{60,300,700}.ppm
# (both eyes side by side, as released to Monado; the layer's SOMVR_XR_DUMP).
#
# 1. pre-game window with the "Unsupported Linux distribution or version" box. REUSED (run 4): wait 30
wait 30
shot 01-pregame-window
# Continue Anyway (in the box). REUSED 684 425
xdo mousemove 684 425
wait 1
xdo click 1
wait 3
shot 02-pregame-display-tab
# 2. PLAY (bottom right of the pre-game window). REUSED 858 601. The layer's XR init (xrCreateInstance on Monado) runs
# inside the game's vkCreateInstance after this.
xdo mousemove 858 601
wait 1
xdo click 1
mark play-pressed
# 3. the game's own alert (FeralLinuxMessage). REUSED: up ~8 s after PLAY
wait 10
shot 03-alert
key Return
mark alert-continue
# 4. legal text, logos, intro: three Escapes 5 s apart from 12 s on (run 4). From the first present on, the XR session
# runs and xrWaitFrame paces the game at the null compositor's rate (20 Hz expected).
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
# 5. MAIN MENU: mouse off the menu, shots.
wait 15
shot 05-main-menu
xdo mousemove 1900 1060
wait 3
shot 06-main-menu-b
mark main-menu
mark stage1-done
# 6. Load the save: PLAY -> Select Game Mode -> Continue Story Mode (first entry). Run 4: in the world 71 s after; here
# 100 s (GUESS: the load at 20 fps may take longer). No input after that (the simulated head wobbles by itself).
key Return
wait 4
shot 07-game-mode
key Return
mark continue-story
wait 30
shot 08-load-a
wait 30
shot 08-load-b
wait 30
shot 08-load-c
wait 10
shot 09-settled
mark world
# 7. Stereo off / on 40 s / off. At 20 Hz ~800 stereo frames: the layer logs stereo frames 1-3 and every 600th, somvr's
# burst covers the first 300 calls of each hook after the trigger (~15 s of H4).
wait 10
shot 40-off-a
trigger
mark stereo-on
wait 2
shot 41-on-2s
wait 8
shot 42-on-10s
# optional operator step (plan 2.4): `monado-ctl -c` once in the next 10 s (a runtime recentre: the layer logs
# "reference space 2 changes (recentre)", somvr a RIG line "recentre=1 (2 so far)")
mark recenter-window
wait 10
shot 43-on-20s
# somvr.c's one-shot trigger words (boot_check `trigger recenter|turn+|turn-`), sent while the file stands: each acts
# once (somvr.log "TRIGGER cmd=recenter" + a RIG line recentre=1, then "TRIGGER cmd=turn+" + RIG snap=1); stereo stays
# on. 2 s apart: somvr polls the file every 250 ms and sees one change per poll.
trigger recenter
mark recenter-word
wait 2
trigger turn+
mark turn-word
wait 8
shot 44-on-30s
wait 10
shot 45-on-40s
untrigger
mark stereo-off
wait 5
shot 46-off-b
# 8. Quit (run 3/4): pause menu QUIT = 5 Downs, Yes by click; main menu QUIT GAME = 5 Downs, Return, Return. Waits a
# little longer than run 4 (20 fps).
key Escape
wait 3
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
wait 20
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
wait 20
