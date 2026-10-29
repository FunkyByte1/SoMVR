#!/bin/bash
# render24-drive.sh - build 24's render round (docs/re-xr-pipeline.md 100): the hurt Talion's frozen world and the
# layer's stale-frame net. fire23b-drive.sh's boot, helpers and staging. Boot with scripts/headset-build24.env,
# SOMVR_HANDS_TEST=1, SOMVR_FX_TEST=1. FX_STEPS (default "hurt"):
#   hurt    the build 20c session's mark-16 campfire, the game's overlays drawn (`flatfx flares,shafts,vignette,rain`),
#           the practice orc's blows until Talion is at low health (25 or less: the game's COverlayFX views; the orc
#           stays - the practice keeps 20 - until after the pictures); `fx eyesame 1` with `eyematch hash` (build 23's match: the eye loss, the layer's net to the
#           loading space) and `eyematch pass` (both eyes, eyesame dumps); stereo marks with the overlays drawn and out
#           (`flatfx all`: SOMVR_HURT_LOW's faint edge alone); `eyedrop 0 N` (the net with each eye its own view);
#           `practice health` at the end
#   d3d     `fx d3d 1` (eyesame) on the meadow's grass at the camp and in the wraith vision while the head turns
# BG_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${FX_STEPS:-hurt}
T0=$(date +%s)
R=${BG_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
# a mark; prints its folder
m() { local n0; n0=$(ls "$R/marks" 2>/dev/null | wc -l); sleep "${2:-0.6}"; w "mark $1" 3.5
  echo "$(date +%T) mark $1 -> $(ls -t "$R/marks" 2>/dev/null | head -1)"; }
# a mark whose both-eye picture is also kept lossless (the layer's eyes.ppm, copied before boot_check makes the jpg)
PPM=${FX_PPM:-/tmp/b24render/ppm}
mp() { local n0 d i; n0=$(ls "$R/marks" 2>/dev/null | wc -l); sleep "${2:-0.6}"; w "mark $1" 0.05
  for i in $(seq 1 100); do d=$(ls -t "$R/marks" 2>/dev/null | head -1); [ "$(ls "$R/marks" | wc -l)" -gt "$n0" ] && \
    [ -s "$R/marks/$d/eyes.ppm" ] && break; sleep 0.1; done
  sleep 0.2; mkdir -p "$PPM"; cp "$R/marks/$d/eyes.ppm" "$PPM/$1.ppm" 2>/dev/null && echo "$(date +%T) mark $1 -> $d (+ $PPM/$1.ppm)" \
    || echo "$(date +%T) mark $1 -> $d (no ppm)"; sleep 2.5; }
# the newest mark's head yaw (deg) and frame rate line
lastyaw() { sed -n 's/^head .* yaw \(-\?[0-9.]*\) deg.*/\1/p' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt" | head -1; }
lastrate() { grep -m1 '^frame rate' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt"; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 -30 0 0 0"
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
w "flatfx state" 0.5
# go to X Y Z and face the head's world yaw YAW (deg) pitched P: the test head's turn found from a mark's yaw
TURN=0
goface() {
  local a0 f0 y d; a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170
  sleep "${7:-3}"; x "head 0 0 0 yaw 0 pitch 0" 1; m "$5-yaw0" 0.2; y=$(lastyaw); echo "yaw now $y (want $4)"
  d=$(python3 -c "d=($4)-($y); d=(d+180)%360-180; print(round(d,1))")
  x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned" 0.2; y2=$(lastyaw); echo "yaw now $y2 after +$d"
  # the wrong way round only if the error grew (run 203003: +53.8 took -67.3 to -6.3, 7 deg off but the right way)
  if python3 -c "import sys; e=lambda a: abs((($4)-(a)+180)%360-180); sys.exit(0 if e($y2) > e($y) else 1)"; then
    d=$(python3 -c "print(-($d))"); x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned2" 0.2; y=$(lastyaw); echo "yaw now $y after $d"
  fi
  TURN=$d
}

dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\ndump 2\ndump 3\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 100); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 3)) ] && break; sleep 0.25; done
  echo "$1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')" | tee -a "$R/fx-steps.txt"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
# the last FIGHT hurt line's health after the blow
hp() { grep '^FIGHT hurt' "$R/somvr.log" | tail -1 | sed -n 's/.*health [0-9.]* -> \([0-9.]*\) .*/\1/p'; }
xl() { grep -cE "$1" "$R/xr-layer.log"; }
for s in $STEPS; do
  case $s in
  hurt)
    echo "== hurt"; w "fx eyesame 0" 0.3; w "eyematch pass" 0.3; w "flatfx state" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    # the game's overlays drawn (build 23's look) for the loss's test: they make the extra views
    w "flatfx flares,shafts,vignette,rain" 0.3; w "practice spawn infantry" 2
    for i in $(seq 1 120); do h=$(hp); [ -n "$h" ] && python3 -c "import sys; sys.exit(0 if $h <= 25 else 1)" && break; sleep 1; done
    echo "$(date +%T) health ${h:-?}"; x "head 0 0 0 yaw $TURN pitch -12" 0.5   # the orc stays (the practice keeps 20)
    # build 23's match with both eyes one camera: the loss, the layer's net
    st0=$(xl 'xr: stale: [0-9]+ presents in a row'); e0=$(xl 'tag says eyes 0x2')
    w "fx eyesame 1" 0.3; w "eyematch hash" 0.3; sleep 8
    echo "$(date +%T) hash: stale lines $(( $(xl 'xr: stale: [0-9]+ presents in a row') - st0 )), 0x2 lines $(( $(xl 'tag says eyes 0x2') - e0 )), switches $(xl 'SOMVR_XR_STALE\)|xr: env: world -> ')"
    w "eyematch state" 0.3
    w "eyematch pass" 0.3; sleep 5; echo "$(date +%T) pass: eyes back lines $(xl 'xr: stale: the eyes back')"
    dq hurt-same-pass; w "eyematch state" 0.3
    w "fx eyesame 0" 1.5; dq hurt-ov-drawn; m hurt-ov-drawn 0.2; lastrate
    w "flatfx all" 1.5; dq hurt-ov-out; m hurt-ov-out 0.2; lastrate
    w "flatfx state" 0.5; w "practice remove" 1
    # the fault itself with each eye its own view: the net, then the world
    st0=$(xl 'xr: stale: [0-9]+ presents in a row'); w "eyedrop 0 40" 0.3
    waitfor '^EYEDROP t=.* over' 60 || echo "$(date +%T) no eyedrop end"; sleep 4
    echo "$(date +%T) eyedrop: stale lines $(( $(xl 'xr: stale: [0-9]+ presents in a row') - st0 )), back $(xl 'xr: stale: the eyes back')"
    dq hurt-after-drop; m hurt-after-drop 0.2
    w "practice health" 2; w "eyematch state" 0.3 ;;
  d3d)
    echo "== d3d"; w "fx eyesame 1" 1
    x "head 0 0 0 yaw $TURN pitch -35" 2; dq d3d-grass
    w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"
    x "head 0 0 0 yaw $TURN pitch 0" 1
    x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 2
    x "head 0 0 0 yaw $TURN pitch 0 s 6" 0.2; x "head 0 0 0 yaw $(python3 -c "print($TURN + 90)") pitch 0 s 6" 0.3
    w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"
    dq d3d-vision-turn; w "fx eyesame 0" 2; dq d3d-vision-stereo; m d3d-vision 0.2
    x "btn L b 0" 0.4; sleep 2 ;;
  esac
done
w "flatfx state" 1
w "eyematch state" 1
w "ktime state" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
