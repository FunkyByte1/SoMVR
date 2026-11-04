#!/bin/bash
# grass33-drive.sh - build 33's grass scout (docs/re-xr-pipeline.md 148): the practice meadow's grass in both eyes at a
# set of head poses, one lossless stereo dump each (the layer's <log>.stereoN.ppm with its `xr: dump layers` poses; the
# name and the frame in RUN/fx-steps.txt), for a boot with SOMVR_XR_GRASS unset (the game's grass) and one with
# SOMVR_XR_GRASS=vr. Boot scripts/headless_vr.sh --win with scripts/headset-build26.env, SOMVR_HANDS_TEST=1,
# SOMVR_XR_TEST=@RUN@/xrtest.txt (and SOMVR_XR_GRASS=vr for the second). The poses (test yaw 0 = the arrival's heading):
#   p20 p20b      pitch -20 twice (the noise floor: the wind and the rig between two dumps of one pose)
#   p45 p20c      pitch -45, then -20 again (the grass's bend follows the head's pitch in the game's shader)
#   p65           pitch -65 (the feet: the bend's largest)
#   sl sr         the head 8 cm left / right at pitch -35 (the cards' layers against each other)
#   cr0 cr35      crouched 70 cm at pitch -5, yaw 0 and 35 (grass above the eye - 20 cm near the view's axis is pushed
#                 down by the game's shader)
# The grass cards need the VR preset's vegetation (3): the Windows slots' prefixes hold 0 (Low: no cards) - set it in the
# slot's render.cfg for the boot (re-xr-pipeline.md 148.6). BG_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
bash "$(dirname "$0")/render_need.sh" "$R" grass33-drive.sh || exit 1   # b37: a Windows run at WIN_RENDER=vr (re-xr-pipeline.md 158.5)
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
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  echo "$1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')" | tee -a "$R/fx-steps.txt"; }
pose() { x "head $1 $2 $3 yaw $4 pitch $5 roll 0" "${PSET:-3}"; dq "$6"; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 -30 0 0 0"
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
# the hands down at the sides, out of the pictures
w "hands test R 25 -75 0 0 180 0" 0.6; w "hands test L -25 -75 0 0 0 0" 0.6
# the same weather in every boot (SOMVR_PERF's console word; GD_WEATHER= empty: the arrival's own)
[ -n "${GD_WEATHER-SON_Day01}" ] && w "perf console GameServerShell.ChangeWeather ${GD_WEATHER-SON_Day01}" 12
x "head 0 0 0 yaw 0 pitch -20" 4
pose 0 0 0 0 -20 p20
pose 0 0 0 0 -20 p20b
pose 0 0 0 0 -45 p45
pose 0 0 0 0 -20 p20c
pose 0 0 0 0 -65 p65
pose -8 0 0 0 -35 sl
pose 8 0 0 0 -35 sr
pose 0 -70 0 0 -5 cr0
pose 0 -70 0 35 -5 cr35
x "head 0 0 0 yaw 0 pitch 0" 1
grep -E '^GRASS|GRASS ' "$R/somvr.log" 2>/dev/null | head -12
echo "$(date +%T) done"; echo quit >> "$R/cmd"
