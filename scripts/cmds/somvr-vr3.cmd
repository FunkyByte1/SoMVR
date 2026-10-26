# somvr-vr3.cmd - VR build 3, the user's headset session (WiVRn): boot_check --headset, the user's save copy, VR build
# 1b's features plus the wrist settings menu, the loading sky, the root anchor and the 1:1 locomotion core, the orcs'
# block/dodge roll, the bow, hand-over-hand climbing and the threat warnings; the vignette off (the user's principle:
# comfort aids off by default; the menu's Vignette row turns it on). docs/re-xr-pipeline.md 19 reviews this set.
# The coordinator freezes logs/build-vr3 from main (the launch line below). Stage 1 only: the pre-game window and the
# alert, then the trigger stands from the start (stereo whenever the world renders; menus stay mono quads, the sky
# behind them). The coordinator drives the intro/menus live with shots, or the user drives them with the controllers
# (pad mode: A = select, B = back). SOMVR_SETTINGS is the launcher's file (scripts/som-vr.sh; the menu's changes stay
# for the user's own play); a headless boot gives @RUN@/settings.conf instead.
#   boot_check.sh --headset --steam --save-copy --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $BLD/mod/somvr/build/somvr.so --game-env SOMVR_STEREO=B --game-env SOMVR_VIEWS=xr \
#     --game-env SOMVR_COMFORT=1 --game-env VK_LAYER_PATH=$BLD/mod/somvr/vklayer \
#     --game-env VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json \
#     --game-env SOMVR_XR=1 --game-env SOMVR_XR_LOG=@RUN@/xr-layer.log --game-env SOMVR_XR_DUMP=600 \
#     --game-env SOMVR_EYE_SIZE=xr --game-env SOMVR_INPUT=1 --game-env SOMVR_POINTER=1 --game-env SOMVR_HUD=quad \
#     --game-env SOMVR_AUDIO=head --game-env SOMVR_BASE=head --game-env SOMVR_HIDE_HEAD=near --game-env SOMVR_FALLBACK=auto --game-env SOMVR_VIGNETTE=0 \
#     --game-env SOMVR_ARMS=1 --game-env SOMVR_COMBAT=1 --game-env SOMVR_BLADE=controller \
#     --game-env SOMVR_VRMENU=1 --game-env SOMVR_SETTINGS=$HOME/.local/share/somvr/settings.conf --game-env SOMVR_XR_ENV=on \
#     --game-env SOMVR_ANCHOR=root --game-env SOMVR_LOCO=1 \
#     --game-env SOMVR_COMBAT_REACT=1 --game-env SOMVR_BOW=1 --game-env SOMVR_CLIMB=b --game-env SOMVR_THREAT=1 \
#     --cmds scripts/cmds/somvr-vr3.cmd --hold 1700
# Prerequisites (checked against somvr.c, re-xr-pipeline.md 19.1): ANCHOR=root keeps BASE=head (it replaces only the
# head base's eye point); LOCO, BOW, CLIMB=b, VRMENU need INPUT=1 (LOCO and BOW its injection, LOCO and CLIMB the PM
# hook, which they install); CLIMB=b and FALLBACK=auto need BASE=head; COMBAT_REACT and THREAT need COMBAT=1, THREAT a
# stereo layout (B); XR_ENV=on needs the menu quad (SOMVR_XR_LAYER=quad, the default).
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
