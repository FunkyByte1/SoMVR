# spike1-run3.cmd - run 3 (docs/re-stereo.md 9.5): layout B (sbsB/sbsB0), velocity blur off (COMFORT) with the
# `blur` A/B, the EXPO (eye adaptation) and AUDIO (listener) log hooks. Stage 1 only: boot, the alert, the intro videos
# skipped, shots at the main menu, stop. The coordinator drives stage 2 live (planned below).
#   boot_check.sh --steam --save-copy --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $SOM/mod/spike1/build/somvr_spike1.so --game-env SOMVR_MODE=off30 --game-env SOMVR_COMFORT=1 \
#     --cmds scripts/cmds/spike1-run3.cmd
# --save-copy with the save-name fix (592bdb2): the guard now serves the game's "\GameData.sav" from RUN/saves, so the
# main menu should show the user's profile (no Gamma screen; probably a CONTINUE entry; layout unknown: 05/06 show it).
#
# Stage 2 (live). Preconditions in somvr.log: "GUARD on", "SAVE read name=\GameData.sav ... -> N" with N > 0,
# "HOOK EXPO_HDR ok" and "HOOK AUDIO_LISTENER ok", COMFORT velblur55/velblur58 was=1 now=0 lines.
# boot_check.sh's `trigger` accepts sbsB, sbsB0 and blur since 10-19 (the library reads the file's first word, exact).
#   1. Load the save: the profile's entry (CONTINUE or PLAY -> Story Mode -> the save; Return confirms, clicks only
#      highlight), Escape through whatever plays, then in the world NO input at all (a still camera: the A/B needs it),
#      xdo mousemove 1900 1060.
#   2. Each step: the command, wait 2, shot (EXPO logs every call while a trigger stands and 2 s after: test 1):
#        untrigger        -> 40-off-a       reference
#        trigger view     -> 41-view        30 cm at H1, velocity blur OFF: haze gone? (test 4)
#        trigger vp       -> 42-vp          30 cm at H2, velocity blur off
#        trigger blur     -> 43-blur        the view 30 cm again with the game's velocity-blur bytes back: haze back?
#        trigger sbsB     -> 44-sbsB        layout B, eyes -/+3.2 cm, eye R on screen: brightness as 40, no seam
#        trigger sbsB0    -> 45-sbsB0       layout B, offset 0: must match 40 / 46
#        untrigger        -> 46-off-b
#   3. Quit: Escape (pause), QUIT, "Quit Game", Yes (930,641), Return. Run 2's exact keys (logs/boot-check-20261019-
#      085223/cmd.done): key Escape; wait 2; key Down; key Return; wait 3; xdo mousemove 930 641; xdo click 1; wait 15;
#      key Return; wait 15; then at the main menu Down to QUIT GAME (4x with the empty profile; one more if CONTINUE
#      was added), key Return, wait 3, key Return.
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
