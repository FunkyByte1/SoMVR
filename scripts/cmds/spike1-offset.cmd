# spike1-offset.cmd - mod/spike1 (SOMVR_MODE=off30): camera offset on/off at the main menu and in the benchmark.
#   boot_check.sh --steam --seed-prefs logs/boot-check-20261018-003728/preferences.seeded \
#     --preload $SOM/mod/spike1/build/somvr_spike1.so --game-env SOMVR_MODE=off30 --cmds scripts/cmds/spike1-offset.cmd
# Steps 1-6 and the quit are menu-to-benchmark.cmd's (same REUSED/GUESS notes). Added: off/view/off/vp/off shots at
# the main menu (Talion's face, a near object: a 30 cm shift is obvious) and the same in the benchmark, then quit.
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
# 4b. spike1 at the main menu: mouse off the menu so no hover changes, then off / on(view) / off / on(vp) / off
xdo mousemove 1900 1060
wait 2
shot 10-menu-off-a
trigger
wait 1
shot 11-menu-on-view
untrigger
wait 1
shot 12-menu-off-b
trigger vp
wait 1
shot 13-menu-on-vp
untrigger
wait 1
shot 14-menu-off-c
mark menu-offset-done
# 5. OPTIONS (hover, then click). REUSED 136 499
xdo mousemove 136 499
wait 1
shot 06-options-hover
xdo click 1
wait 3
xdo mousemove 1900 1060
wait 1
shot 07-options
# 6. Benchmark (last item of the Options list). REUSED 143 673 - but attempt 3 clicked it after opening and closing
# Video and Advanced Options (Escape); straight from a fresh Options screen is a GUESS (same list, same place expected)
xdo mousemove 143 673
wait 1
xdo click 1
mark benchmark-clicked
wait 3
xdo mousemove 1900 1060
shot 08-bench-loading
# the benchmark scene (attempt 3: on screen 22 s after the click, fps graph top right)
wait 20
shot 09-bench-running
mark benchmark-running
# 7. spike1 in the benchmark (the camera may move: shots close together; the log decides)
shot 20-bench-off-a
trigger
wait 1
shot 21-bench-on-view
trigger vp
wait 1
shot 22-bench-on-vp
untrigger
wait 1
shot 23-bench-off-b
mark bench-offset-done
# 8. results. Run 1 (2026-10-18 15:35): the results were up ~64 s after the click (H1 starts behind them), but mouse
# clicks on Ok were ignored there (attempt 3's worked; cause unknown); Return worked. So keys only
wait 30
wait 20
shot 30-bench-results
key Return
wait 6
shot 31-main-menu-after
# QUIT GAME: hover (REUSED 147 557) + Return, then hover Yes (REUSED 930 642) + Return (run 1: worked)
xdo mousemove 147 557
wait 1
key Return
wait 3
shot 32-quit-confirm
xdo mousemove 930 642
wait 1
key Return
mark quit-return
