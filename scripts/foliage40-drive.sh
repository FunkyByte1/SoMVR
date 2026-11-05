#!/bin/bash
# foliage40-drive.sh - som-b40-foliage (docs/re-xr-pipeline.md 177): the bubble's radius in the user's bushes and the
# open meadow, both eyes, one lossless stereo dump each (the layer's <log>.stereoN.ppm; the name and the frame in
# RUN/fol-steps.txt). One boot per radius (SOMVR_NEAR_FOLIAGE_CM=60,85 = build 39's, 75,95 = build 40's). Boot
# scripts/headless_vr.sh --win with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt,
# WIN_RENDER=vr (the VR preset's vegetation: the grass cards). The places:
#   u*   the bush of the user's 11-05 note (session 092046, mark 092750_10: Talion's root 31276.4 1780.6 -26467.5,
#        crouched): four yaws standing (s) and crouched 90 cm (c), the head 20 cm forward along the view
#   b*   build 36's bush (mark 19's spot, root 31975.8 1829.4 -25357.9): three yaws, standing and crouched 60 cm
#   m*   the practice meadow (the arrival): standing looking down at the feet (pitch -60) and crouched 90 cm: the grass
#        around the player in the open (the radius is the same for the grass)
# BG_RUN=RUN drives a running boot; FOL_WEATHER= empty: the arrival's own weather (else SON_Day01 in every boot).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
R=${BG_RUN:?BG_RUN=RUN}
echo "run $R"
S=$R/fol-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 240); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  echo "$1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')" | tee -a "$S"; }
# the head at yaw Y (deg, counter-clockwise), DY cm up/down, F cm forward along the view; pitch P
pose() { local y=$1 dy=$2 f=$3 p=$4 name=$5 hx hz
  hx=$(awk -v y="$y" -v f="$f" 'BEGIN { printf "%.1f", -f * sin(y * 3.14159265 / 180) }')
  hz=$(awk -v y="$y" -v f="$f" 'BEGIN { printf "%.1f", -f * cos(y * 3.14159265 / 180) }')
  x "head $hx $dy $hz yaw $y pitch $p roll 0" "${PSET:-3}"; dq "$name"; }
go() { local n0; n0=$(grep -c '^PRACTICE t=.*arrived at' "$R/somvr.log")
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(grep -c '^PRACTICE t=.*arrived at' "$R/somvr.log")" -gt "$n0" ] && break; sleep 1; done
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-200 | tee -a "$S"; sleep 6; }
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "front keep 0" 0.3
w "hands test R 25 -75 0 0 180 0" 0.6; w "hands test L -25 -75 0 0 0 0" 0.6   # the hands down at the sides
[ -n "${FOL_WEATHER-SON_Day01}" ] && w "perf console GameServerShell.ChangeWeather ${FOL_WEATHER-SON_Day01}" 12
grep -E 'BUBBLE foliage' "$R/somvr.log" | tail -1 | cut -c1-200 | tee -a "$S"
# 1. the meadow, in the open
pose 0 0 0 -60 m0s; pose 180 0 0 -60 m180s
pose 0 -90 0 -10 m0c; pose 90 -90 0 -10 m90c
# 2. the user's bush (11-05 mark 10)
go 31276 1950 -26468
for y in 0 90 180 270; do pose $y 0 20 -20 u${y}s; pose $y -90 20 -10 u${y}c; done
# 3. build 36's bush (mark 19's spot)
go 31976 2000 -25358
for y in 0 120 240; do pose $y 0 25 -15 b${y}s; pose $y -60 25 -10 b${y}c; done
x "head 0 0 0 yaw 0 pitch 0 roll 0" 1
grep -cE 'BUBBLE (VS|PS|GS) .*bytes\):' "$R/somvr.log" | sed 's/^/bubble edits: /' | tee -a "$S"
grep -E 'BUBBLE .*stays the game' "$R/somvr.log" | head -5
echo "$(date +%T) done"; echo quit >> "$R/cmd"
