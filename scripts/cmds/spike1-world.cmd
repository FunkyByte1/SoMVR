# spike1-world.cmd - run 2: the save guard + comfort pokes, first stage only (the rest is appended live).
#   boot_check.sh --steam --save-copy --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $SOM/mod/spike1/build/somvr_spike1.so --game-env SOMVR_MODE=off30 --game-env SOMVR_COMFORT=1 \
#     --cmds scripts/cmds/spike1-world.cmd
# Stage 1 (this file): to the main menu, then the menu vp A/B again with the comfort pokes (does the smear go?).
# Stage 2 (live, only if somvr.log shows "GUARD on" AND the game's startup profile read as "SAVE read" lines):
# PLAY -> load the world from the copy, view-mode A/B in the world (trigger / untrigger), then the two-eyes test
# (docs/re-stereo.md section 8): `trigger sbs`, wait 2, shot; `trigger sbs0`, wait 2, shot (halves must match); quit
# through the menus with keys.
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
# 4. legal text, Feral logo, intro, then MAIN MENU by itself (no "press a key"). GUESS: 60 s (attempt 3: ~40 s)
wait 30
shot 04-intro
wait 30
shot 05-main-menu
mark main-menu
# 4b. the menu A/B with the comfort pokes on (run 1 without them: the offset face looked smeared)
xdo mousemove 1900 1060
wait 2
shot 10-menu-off-a
trigger vp
wait 2
shot 11-menu-on-vp
untrigger
wait 1
shot 12-menu-off-b
mark stage1-done
