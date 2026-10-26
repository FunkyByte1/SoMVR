#!/bin/bash
# vrhands-drive.sh - build 10's VR hands, the watch on the wrist and the wrist panel off the palm in one headless boot
# (docs/re-xr-pipeline.md 33, handoff/HANDOFF-vrhands.md). Boot (outside the sandbox) with the layer's test file:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' logs/build-vr9/headset.env | tr '\n' ' ')
#     SOMVR_VRHANDS=1 SOMVR_XR_WATCH=wrist SOMVR_XR_PANEL=palm SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt
#     SOMVR_EYE_SIZE=2015x2217" setsid scripts/headless_vr.sh <worktree>/ShadowOfMordor/mod/somvr
# and this script at the same time (VRH_WT = a word of the launch command: the worktree's name). Every step ends in a
# stereo dump (`dump`: the eyes, the HUD, the panel, our hands' layer); look at them with
#   python3 scripts/xr_compose.py RUN (RUN/compose/frameN.png: both eyes as the compositor shows them).
# VRH_STEPS = any of: hold palms fist point turn menu watch spin sword two (default all, in that order: the sword last,
# a drawn sword keeps the right hand a fist). The test file's turns (run 181440): L 0 90 0 = the back of the left hand
# toward the eyes (the watch), L 0 -90 0 = its palm (the wrist panel's gesture); R mirrored.
# Prints the dump frame of each step (RUN/vrh-steps.txt) and quits. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${VRH_WT:-t3-89de93b7}
STEPS=${VRH_STEPS:-hold palms fist point turn menu watch spin sword two}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
# d NAME: a dump now (the next stereo frame), its number noted with the step's name
d() { local n0; n0=$(grep -c 'dump layers frame [0-9]*: hands' "$R/xr-layer.log" 2>/dev/null); t "dump"; sleep 1.4
      local f; f=$(grep -o 'dump layers frame [0-9]*: hands' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*')
      [ "$(grep -c 'dump layers frame [0-9]*: hands' "$R/xr-layer.log")" -gt "$n0" ] || f="$f (no new hands dump)"
      echo "$1 $f" | tee -a "$R/vrh-steps.txt"; }
both() { t "hand L head $1" "hand R head $2"; sleep "${3:-0.6}"; }
grep -q 'xr: hands: ready' "$R/xr-layer.log" && echo "PASS the layer made our hands" || echo "FAIL no 'xr: hands: ready'"
for s in $STEPS; do
  case $s in
  hold)  # the controllers held naturally (the grip's -Z 40 deg up), open, relaxed
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 1.5; d hold-open
    both "-12 -22 30 40 0 0" "12 -22 30 40 0 0" 1; d hold-near ;;
  palms)  # the backs, then the palms toward the eyes (the wrist panel may open on the palms: shut it again)
    both "-10 -12 30 0 90 0" "10 -12 30 0 -90 0" 1; d backs
    both "-10 -12 30 0 -90 0" "10 -12 30 0 90 0" 1; d palms
    both "-10 -12 30 90 -90 0" "10 -12 30 90 90 0" 1; d palms-up
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 1.5 ;;
  fist)
    both "-15 -25 35 40 0 0" "15 -25 35 40 0 0"; t "btn L squeeze 1" "btn R squeeze 1"; sleep 1; d fists
    t "btn L trigger 1" "btn R trigger 1"; sleep 1; d fists-trigger
    t "btn L trigger 0" "btn R trigger 0" "btn L squeeze 0" "btn R squeeze 0"; sleep 1 ;;
  point)  # the grip squeezed, the trigger untouched: the index points
    both "-15 -25 35 40 0 0" "12 -18 30 10 0 0"; t "btn R squeeze 1"; sleep 1; d point-right
    both "-15 -25 35 40 0 0" "8 -14 24 10 50 0"; sleep 1; d point-side
    t "btn R squeeze 0"; sleep 0.5 ;;
  turn)  # one hand through a roll: the wrist's fade from every side, nothing cut
    for a in 0 60 120 180 240 300; do both "-20 -30 35 40 0 0" "10 -14 30 40 0 $a" 0.8; d "roll-$a"; done ;;
  sword)  # the quick draw (the hands test's two squeezes), the fist round the hilt from four sides
    both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 0.5; w "hands draw" 3
    both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 1; d sword-held
    both "-20 -30 35 40 0 0" "15 -15 35 40 0 -90" 1; d sword-right
    both "-20 -30 35 40 0 0" "10 -10 30 40 90 -90" 1; d sword-palm
    both "-20 -30 35 40 0 0" "15 -10 40 -20 0 0" 1; d sword-up ;;
  two)  # the left hand on the hilt behind the right (SOMVR_TWOHAND), squeezed
    both "6 -27 30 40 0 0" "8 -18 35 40 0 0" 1; t "btn L squeeze 1"; sleep 1.5; d two-hands
    t "btn L squeeze 0"; sleep 1; both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 0.5 ;;
  menu)  # the wrist panel (the left palm toward the eyes), the right fingertip on it
    o=$(grep -c 'vrmenu: opened by' "$R/xr-layer.log")
    t "hand R head 15 -30 35 40 0 0" "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 2
    [ "$(grep -c 'vrmenu: opened by' "$R/xr-layer.log")" -gt "$o" ] || { t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 1; }
    grep 'vrmenu: opened by' "$R/xr-layer.log" | tail -1 | cut -c1-160
    d menu-open
    t "hand R touch panel 0.5 0.45 4 s 0.3"; sleep 1; d menu-finger-4cm
    t "hand R touch panel 0.5 0.45 0.3 s 0.3"; sleep 1; d menu-finger-touch
    t "hand R touch panel 0.3 0.25 1.5 s 0.3"; sleep 1; d menu-finger-row
    t "hand L head 2 -12 30 -20 -90 50 s 0.4"; sleep 1; d menu-wrist-tilted
    t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1.5 ;;
  watch)  # the left wrist raised, its back toward the eyes (the watch), from a few angles
    both "-6 -14 30 0 90 0" "18 -30 35 40 0 0" 1.2; d watch-raised
    both "-6 -14 30 0 90 -60" "18 -30 35 40 0 0" 1; d watch-turned-60
    both "-6 -14 30 0 90 60" "18 -30 35 40 0 0" 1; d watch-turned+60
    both "-6 -14 30 40 90 0" "18 -30 35 40 0 0" 1; d watch-tilt40
    both "-12 -18 34 0 60 0" "18 -30 35 40 0 0" 1; d watch-side ;;
  read)  # the left wrist raised to read the watch: the forearm across the chest (the elbow left), the back of the
         # hand up and tilted toward the eyes (L -140 0 90 = 40 deg toward them); then 25 deg less and more
    both "-5 -22 30 -140 0 90" "18 -30 35 40 0 0" 1.2; d read
    both "-5 -22 30 -165 0 90" "18 -30 35 40 0 0" 1; d read-flat
    both "-5 -22 30 -115 0 90" "18 -30 35 40 0 0" 1; d read-steep
    both "-5 -22 30 -140 0 90" "10 -12 30 10 0 0" 1; d read-pointing ;;
  roll)  # the forearm rolled slowly (5 s) from reading the watch to its back away: shown/hidden logged, no flashes
    both "-5 -22 30 -140 0 90" "18 -30 35 40 0 0" 1
    t "hand L head -5 -22 30 -10 0 90 s 5"; sleep 1.0; d roll-a; sleep 0.8; d roll-b; sleep 0.8; d roll-c; sleep 2.5
    grep 'minimap: the watch face' "$R/xr-layer.log" | tail -4 | cut -c1-160 ;;
  spin)  # the wrist turned slowly (5 s) from the back to the palm and back: the watch's frames logged (no flashes)
    both "-6 -14 30 0 90 0" "18 -30 35 40 0 0" 1
    t "hand L head -6 -14 30 0 0 0 s 5"; sleep 1.0; d spin-a; sleep 0.8; d spin-b; sleep 0.8; d spin-c; sleep 2.5
    t "hand L head -6 -14 30 0 90 0 s 5"; sleep 1.0; d spin-d; sleep 0.8; d spin-e; sleep 2.5
    grep 'minimap: the watch face' "$R/xr-layer.log" | tail -6 | cut -c1-160 ;;
  esac
done
grep -E 'xr: hands|xr: dump layers frame [0-9]*: hands' "$R/xr-layer.log" | head -5 | cut -c1-220
grep -E '^VRHANDS|^CONFIG vrhands' "$R/somvr.log" | tail -4 | cut -c1-220
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (xr-layer.log.stereoN / handsN / vrmenuN), steps in $R/vrh-steps.txt"
