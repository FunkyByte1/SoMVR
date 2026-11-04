#!/bin/bash
# bubble36-drive.sh - build 36's bubble (docs/re-xr-pipeline.md 166): the head in foliage and among the floating bugs, in
# both eyes, one lossless stereo dump each (the layer's <log>.stereoN.ppm; the name and the frame in RUN/bub-steps.txt),
# and the frame costs while the head is in a bush (two of the layer's 10 s `xr: perf` windows + somvr's PERF lines, the
# times in RUN/bub-steps.txt). One boot with SOMVR_NEAR_FOLIAGE_CM=off SOMVR_NEAR_BUGS_CM=off (the game's), one with the
# env block's (20,35 / 75,100). Boot scripts/headless_vr.sh --win with scripts/headset-build26.env, SOMVR_HANDS_TEST=1,
# SOMVR_PERF=1, SOMVR_XR_TEST=@RUN@/xrtest.txt (frame costs: a solo slot). The places (the user's 11-03 session marks):
#   m*   the practice meadow (the arrival): crouched 110 cm in its grass, yaw 0 / 90
#   b*   the bush by the practice area's rock (mark 19's spot): six yaws, the head 25 cm forward along the view (leaning
#        in), standing (b<yaw>) and crouched 60 cm (c<yaw>); then 25 s held at c0 / b0 (the perf windows)
#   g*   mark 23's meadow with the mosquitoes: four yaws standing, two dumps each 2 s apart (the bugs move)
# BG_RUN=RUN drives a running boot; BUB_WEATHER= empty: the arrival's own weather (else SON_Day01 in every boot).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
T0=$(date +%s)
R=${BG_RUN:-}
for i in $(seq 1 720); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
S=$R/bub-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 200); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
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
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-200 | tee -a "$S"; sleep 5; }
hold() { echo "hold $1 from $(date +%s.%N) for $2 s" | tee -a "$S"; sleep "$2"; echo "hold $1 to $(date +%s.%N)" >> "$S"; }
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 -30 0 0 0" 0.6; w "hands test R 22 -35 -30 0 180 0" 0.6; w "front keep 0" 0.3
w "hands test R 25 -75 0 0 180 0" 0.6; w "hands test L -25 -75 0 0 0 0" 0.6   # the hands down at the sides
[ -n "${BUB_WEATHER-SON_Day01}" ] && w "perf console GameServerShell.ChangeWeather ${BUB_WEATHER-SON_Day01}" 12
grep -E '^BUBBLE|BUBBLE ' "$R/somvr.log" 2>/dev/null | head -3
echo "mode foliage=${BUB_MODE:-?}" >> "$S"
# 1. the meadow's grass, crouched
pose 0 -110 0 -10 m0
pose 90 -110 0 -10 m90
# 2. the bush (mark 19's spot: Talion's root 31975.8 1829.4 -25357.9)
go 31976 2000 -25358
for y in 0 60 120 180 240 300; do pose $y 0 25 -15 b$y; pose $y -60 25 -10 c$y; done
x "head 0 -60 -25 yaw 0 pitch -10 roll 0" 3; hold c0 25
x "head 0 0 -25 yaw 0 pitch -15 roll 0" 3; hold b0 25
# 3. mark 23's mosquitoes (Talion's root 31803.9 1948.5 -27775.4)
go 31804 2100 -27775
for y in 0 90 180 270; do pose $y 0 0 0 g${y}a; sleep 2; dq g${y}b; done
x "head 0 0 0 yaw 0 pitch 0 roll 0" 1
grep -cE 'BUBBLE (VS|PS|GS) .*bytes\):' "$R/somvr.log" | sed 's/^/bubble edits: /'
grep -E 'BUBBLE .*stays the game' "$R/somvr.log" | head -5
echo "$(date +%T) done"; echo quit >> "$R/cmd"
