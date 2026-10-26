#!/bin/bash
# threat14-drive.sh - build 14's front-first round 2 (docs/re-xr-pipeline.md 42.11): THREAT's in-view test by bearing,
# one warning per attack (SOMVR_FRONT_ONECUE), bigger fights; headless at the practice arrival, as front14-drive.sh
# (the layer's test head `head 0 0 0 yaw DEG s SEC` in RUN/xrtest.txt; `front keep 1` keeps the practice's spawns).
# Boot (sandbox off):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build14.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 [SOMVR_FRONT_MAX_WARN=2 ...]"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
#   this script at the same time (THREAT14_PLAN=...).
# Plans (THREAT14_PLAN):
#   all    (default) feet, then crowd
#   feet   one orc spawned 2 m ahead: it fights at Talion's feet with the head forward (in view: the glow, no edge
#          warning, no THREAT warn line), then the head turned 180 deg (behind: the FRONT warning, then one warning per
#          attack: `THREAT quiet`, `FRONT cues`), back
#   crowd  five orcs kept (infantry and shields 2-8 m ahead): forward, a head sweep (yaw 0 -> +-120 -> 180 over 3 s
#          turns, 4 s apart), behind, to the side, forward; the per-phase deltas of grants, warnings, quiet pulses
# Env: THREAT14_PLAN, THREAT14_WT. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${THREAT14_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${THREAT14_PLAN:-all}
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
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
turn() { x "head 0 0 0 yaw $1 s ${2:-0.6}" 1; }
# the per-phase deltas: line counts at the phase's start (ph) and its end (pe NAME)
K=("THREAT warn" "THREAT warn .*LATE" "THREAT cue .*cue=edge" "THREAT cue .*cue=glow" "THREAT quiet" "THREAT pre"
   "FRONT cues" "FRONT grant .*verdict=in-view" "FRONT grant .*verdict=ready" "FRONT warn t" "FRONT expired"
   "FRONT veto .*prio" "THREAT start")
N=("warn" "LATE" "edge-cue" "glow-cue" "quiet" "pre" "cues" "grant-inview" "grant-ready" "fwarn" "expired" "prio"
   "starts")
declare -a C0
ph() { local i; for i in "${!K[@]}"; do C0[$i]=$(grep -c "^${K[$i]}" "$L"); done; PT=$(date +%s); }
pe() { local i s="" c; for i in "${!K[@]}"; do c=$(grep -c "^${K[$i]}" "$L"); s="$s ${N[$i]}=$((c - C0[$i]))"; done
  echo "PHASE $1 ($(( $(date +%s) - PT )) s):$s"; }
st() { w "front status" 1; grep '^FRONT status t=' "$L" | tail -1 | cut -c1-330; }
if [ "$PLAN" = all ] || [ "$PLAN" = feet ]; then
  # 1. one orc at Talion's feet, the head forward: in view by the bearing (no edge warning)
  w "front keep 0"; w "verbs ahead 200" 0.3; w "practice spawn infantry" 0.3; z 6
  ph; z 16; m feet-front; pe feet-front
  grep -E '^THREAT (warn|cue)' "$L" | tail -4 | cut -c1-200
  # 2. the head turned 180 deg: behind, one warning per attack
  ph; turn 180; z 3; m feet-behind; z 15; pe feet-behind
  grep -E '^FRONT cues' "$L" | tail -3 | cut -c1-330
  ph; turn 0; z 6; pe feet-back
  st
fi
if [ "$PLAN" = all ] || [ "$PLAN" = crowd ]; then
  # 3. five orcs kept: forward, a sweep, behind, the side, forward
  w "front keep 1"
  [ "$PLAN" = crowd ] && { w "verbs ahead 200" 0.3; w "practice spawn infantry" 2; }
  w "verbs ahead 450" 0.3; w "practice spawn shield" 2; w "verbs ahead 600" 0.3; w "practice spawn infantry" 2
  w "verbs ahead 750" 0.3; w "practice spawn shield" 2; w "verbs ahead 300" 0.3; w "practice spawn infantry" 2
  grep -c '^PRACTICE t=.* spawn' "$L"
  ph; z 15; m crowd-front; pe crowd-front
  ph; for y in 60 120 180 -120 -60 0 90 180 -90 0; do turn $y 3; z 3; done; m crowd-sweep; pe crowd-sweep
  ph; turn 180; z 3; m crowd-behind; z 15; pe crowd-behind
  ph; turn 90; z 3; m crowd-side; z 12; pe crowd-side
  ph; turn 0; z 10; pe crowd-back
  st
  grep '^FRONT status orc=' "$L" | tail -6 | cut -c1-200
  w "front keep 0"; w "practice remove" 3
fi
w "front status" 1
echo "== the run's summary"
grep -E '^CONFIG (front|threat)' "$L" | cut -c1-260
grep -E '^FRONT END' "$L" | cut -c1-1200
echo "$(date +%T) done"; echo quit >> "$R/cmd"
