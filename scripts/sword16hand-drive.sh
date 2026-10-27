#!/bin/bash
# sword16hand-drive.sh - SOMVR_SWORD_WEIGHT_HAND fixed vs follow (docs/re-xr-pipeline.md 52.10), headless at the practice
# arrival, the same moves in both modes (`hands weighthand fixed|follow`): a 500 ms cut with a dump at its 4th frame, a
# +-15 deg 4 Hz wiggle with a dump (the two hands' follow is the selftest's: their weight is under 3.5 deg). Boot as sword16-drive.sh says
# (S16_WT = a word of the launch command). Dumps: RUN/s16h-steps.txt (scripts/xr_compose.py RUN). Quits at the end, at
# once if the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${S16_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
fastwait() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 20))); do
  [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.05; done; userin && bail "the user is in VR"; return 1; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/s16h-steps.txt"; }
dmid() { ( fastwait "$2" 12 && { printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/s16h-steps.txt"; } ) & }
held() { grep -E '^SLOTS t=.* sword: (done|DENIED|the game put it away: kept|our keep undone)' "$L" | tail -1 |
         grep -qE 'now in the hand|kept in the hand'; }
# 0. the card away, the sword drawn at the right shoulder (the layer's test controllers), then the preload's test hands
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"
t "hand R head 10 -10 -15 0 0 0"; sleep 0.4; t "btn R squeeze 1"; sleep 0.3; t "hand R head 30 -25 40 0 0 0"; t "btn R squeeze 0"; sleep 0.5
held && echo "$(date +%T) the sword is in the hand" || echo "$(date +%T) FAIL no sword in the hand"
grep -E '^CONFIG smooth' "$L" | grep -oE 'the drawn hands [a-z ]*' | head -1
CUT_A="hands test R 45 -15 35 0 -70 0"; CUT="hands swing R -30 -25 45 0 70 0 500"
w "hands test L -25 -35 30 0 0 0"
for mode in fixed follow; do
  echo "== $mode"
  w "hands weighthand $mode" 0.5
  waitfor "^SWORDWT t=.* the drawn hands $([ $mode = follow ] && echo 'follow the blade' || echo fixed) \(trigger word\)" 3 || echo "$(date +%T) no mode line yet"
  w "$CUT_A" 1.5
  dmid "$mode-cut" '^BODY test swing R frame 4 '
  w "$CUT" 2; wait
  w "hands test R 30 -25 40 0 0 0" 1
  w "hands wiggle R 15 4 3" 0.2; waitfor '^BODY test wiggle R: ' 4; sleep 1.2; d "$mode-wiggle"
  waitfor '^BODY test wiggle R end: ' 6; grep -E '^BODY test wiggle R end: ' "$L" | tail -1 | cut -c1-200
  waitfor '^SWORDWT t=.* last 10 s' 12; grep -E '^SWORDWT t=.* last 10 s' "$L" | tail -1 | cut -c1-360
done
echo "steps done $(date +%T)"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
