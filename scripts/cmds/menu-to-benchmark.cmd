# menu-to-benchmark.cmd - from the Feral pre-game window to the RUNNING in-game benchmark (Options -> Benchmark).
#   boot_check.sh --steam --seed-prefs <attempt 1's prefs> --cmds scripts/cmds/menu-to-benchmark.cmd [--preload ...]
# (or by hand during a run: cat this file >> RUN/cmd). Runs as one batch from game time 0, a shot at each step.
# Built from logs/boot-check-20261018-003728/cmd.done + run.log + shots (attempt 3, the only run that reached the menu
# and the benchmark; there the operator sent it in small batches and looked at each screen first).
# REUSED = exact coordinate/key of attempt 3 that did what is named. GUESS = timing or order not tried that way.
# Assumes --size 1920x1080 (default) and a pre-game window WITHOUT the "Usage Statistics" prompt (attempt 1's prefs via
# --seed-prefs, as in attempt 3; a fresh profile shows that prompt first and every click below misses).
# Blind and timed: nothing waits for a screen. A slower start (e.g. the preloaded library) shifts everything - read the
# shots. Attempt 3 (game time): pre-game window up by 10 s, alert 8 s after PLAY, main menu ~40 s after the alert,
# benchmark scene ~20 s after its click, results by ~75 s. Comments: whole lines only (an inline "# x" goes to
# xdotool). wait is capped at 30 s per line.

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
# --- end of the batch: the benchmark runs. Append to RUN/cmd now, e.g.
#   trigger                       creates RUN/somvr.trigger for the --preload library
#   shot bench-NAME               attempt 3's scene ran until ~60-75 s after the click
# "Benchmark Results" (Average/Max/Min FPS) were up by ~75 s after the click. Then, all REUSED from attempt 3:
#   xdo mousemove 958 640 / wait 1 / xdo click 1     Ok -> main menu
#   xdo mousemove 147 557 / wait 1 / xdo click 1     QUIT GAME -> "Are you sure"
#   xdo mousemove 930 642 / wait 1 / xdo click 1     Yes -> the game exits 0 by itself
