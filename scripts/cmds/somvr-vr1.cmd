# somvr-vr1.cmd - VR build 1b, the user's headset session (WiVRn): boot_check --headset, the user's save copy, every
# feature on (logs/build-vr1 from main; the launch line below). Stage 1 only: the pre-game window and the alert, then
# the trigger stands from the start (stereo whenever the world renders; menus stay mono quads). The coordinator drives
# the intro/menus live with shots, or the user drives them with the controllers (pad mode: A = select, B = back).
#   boot_check.sh --headset --steam --save-copy --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $BLD/mod/somvr/build/somvr.so --game-env SOMVR_STEREO=B --game-env SOMVR_VIEWS=xr \
#     --game-env SOMVR_COMFORT=1 --game-env VK_LAYER_PATH=$BLD/mod/somvr/vklayer \
#     --game-env VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json \
#     --game-env SOMVR_XR=1 --game-env SOMVR_XR_LOG=@RUN@/xr-layer.log --game-env SOMVR_XR_DUMP=600 \
#     --game-env SOMVR_EYE_SIZE=xr --game-env SOMVR_INPUT=1 --game-env SOMVR_POINTER=1 --game-env SOMVR_HUD=quad \
#     --game-env SOMVR_AUDIO=head --game-env SOMVR_BASE=head --game-env SOMVR_HIDE_HEAD=near --game-env SOMVR_FALLBACK=auto --game-env SOMVR_VIGNETTE=0.4 \
#     --game-env SOMVR_ARMS=1 --game-env SOMVR_COMBAT=1 --game-env SOMVR_BLADE=controller \
#     --cmds scripts/cmds/somvr-vr1.cmd --hold 1700
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
trigger
mark stereo-armed
wait 20
shot 04a-intro
