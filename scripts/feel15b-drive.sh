#!/bin/bash
# feel15b-drive.sh - SOMVR_HANDS_SMOOTH / SOMVR_SWORD_WEIGHT (docs/re-xr-pipeline.md 48, handoff/HANDOFF-feel.md): one
# headless boot with scripts/headset-build16.env + SOMVR_HANDS_SMOOTH=on SOMVR_SWORD_WEIGHT=light, the test hands
# (SOMVR_HANDS_TEST=1) and the layer's test file (SOMVR_XR_TEST=@RUN@/xrtest.txt). Steps:
#   still   the sword drawn, the hand still, no noise: HANDJIT = what our pipeline adds (0), the arms' time = the eyes'
#   noise   `hands noise 0.15 0.5` (deg, mm per axis): HANDJIT raw ~ the injected noise, smoothed cut; a dump + mark
#   swings  with the noise: lunges at a practice infantry until hits land (COMBAT hit: the hits on the drawn blade),
#           SWORDWT (the blade's trail, capped), the swing pulses (HAP)
# Stops at once (quit) when the user goes into VR. FB_WT: the worktree (default: the one this script is in).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
drawn() { local l; l=$(tail -800 "$R/somvr.log" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
HOLD="hands test R 30 -20 35 0 -60 0"      # the sword held out ahead, still
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
jit() { grep -E '^HANDJIT t=' "$R/somvr.log" | tail -1 | cut -c1-700; }
# 0. the controls card away, the left hand forward, the sword drawn
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
drawn && echo "$(date +%T) the sword is in the hand" || echo "$(date +%T) WARN: no sword in the hand"
# 1. still, no noise: two whole HANDJIT windows
w "$HOLD" 1
waitfor '^HANDJIT t=' 12; waitfor '^HANDJIT t=' 12
echo "$(date +%T) STILL (no noise):"; jit
# 2. the noise: two windows, a dump and a mark
w "hands noise 0.15 0.5" 1
waitfor '^HANDJIT t=' 12; waitfor '^HANDJIT t=' 12
echo "$(date +%T) NOISE 0.15 deg 0.5 mm:"; jit
x "dump"; sleep 1.5; m f01-noise-still 0
# 3. swings at a practice infantry (with the noise)
H0=$(cnt '^COMBAT hit '); P0=$(cnt '^HAP R .*src=combat')
w "practice spawn infantry" 2
for i in $(seq 1 10); do
  face; w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$HOLD" 1.0
  [ "$(cnt '^COMBAT hit ')" -ge $((H0 + 3)) ] && break
done
waitfor '^SWORDWT t=' 12
echo "$(date +%T) SWINGS: hits $(( $(cnt '^COMBAT hit ') - H0 )), combat pulses $(( $(cnt '^HAP R .*src=combat') - P0 ))"
grep -E '^COMBAT hit ' "$R/somvr.log" | tail -2 | cut -c1-260
grep -E '^SWORDWT t=' "$R/somvr.log" | tail -2 | cut -c1-300
# 4. the noise off: still again
w "hands noise 0 0" 1; w "$HOLD" 1
waitfor '^HANDJIT t=' 12; waitfor '^HANDJIT t=' 12
echo "$(date +%T) STILL again:"; jit
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
