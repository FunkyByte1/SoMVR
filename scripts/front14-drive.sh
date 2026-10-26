#!/bin/bash
# front14-drive.sh - build 14's front-first enemies (SOMVR_FRONT, docs/re-xr-pipeline.md 42), headless, at the practice
# arrival. The head turned away: the layer's test head (`head 0 0 0 yaw 180 s 0.6` in RUN/xrtest.txt: SOMVR_XR_TEST;
# the rig's heading stays); several orcs: the word `front keep 1` (the practice's spawns keep the previous orc) with
# `verbs ahead CM` between the spawns; an archer: `verbs template Orc_Archer`. Boot (sandbox off):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build14.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 [SOMVR_FRONT=log ...]"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
#   this script at the same time (FRONT_PLAN=...).
# Plans (FRONT_PLAN), each the same fight: one orc ahead with the head forward (in view), the head turned 180 deg (the
# orc behind), back; three orcs kept (ahead 3.5 / 5 / 6.5 m) forward, behind, to the side; an archer 12 m ahead,
# forward then behind:
#   fight   (default) as booted (SOMVR_FRONT=log for the baseline, 1 for the veto, + _CAMCOPY=head, _DAMAGE=0.5, ...)
#   short   the one orc only (forward, behind, forward)
#   spawn   no fight (boot it with SOMVR_PRACTICE=0: the save's own place, the ambient spawner on): the head turned 180
#           deg for 150 s, then forward for 60 s; the FRONT arrival / departure lines (orcs new in or gone from the
#           bands within 60 m, with their bearing off the head and off the game's own copy) - run it with
#           SOMVR_FRONT_CAMCOPY=head and =off. FRONT_WAIT=practice: at the practice meadow instead (boot it with
#           SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=off: the ambient spawner on, nothing removed)
# Env: FRONT_PLAN, FRONT_WT, FRONT_WAIT. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FRONT_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${FRONT_PLAN:-fight}
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
ARR="^PRACTICE t=.* arrived"
[ "$PLAN" = spawn ] && [ "${FRONT_WAIT:-}" != practice ] && ARR="^FRONT camcopy"
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "$ARR" "$L" 2>/dev/null && break; sleep 3
done
grep -q "$ARR" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -c "^FRONT $1" "$L"; }
st() { w "front status" 1; grep '^FRONT status t=' "$L" | tail -1 | cut -c1-330
  echo "  lines: veto $(cnt veto) warn $(cnt warn) ready $(cnt ready) grant $(cnt 'grant .*verdict') attack $(cnt attack)" \
       "expired $(cnt expired) THREAT start $(grep -c '^THREAT start' "$L") pre $(grep -c '^THREAT pre' "$L")" \
       "LATE $(grep -c 'LATE' "$L") hurt $(grep -c '^FIGHT hurt\|^HURT' "$L")"; }
turn() { x "head 0 0 0 yaw $1 s 0.6" 1; }
if [ "$PLAN" = spawn ]; then
  sleep 12; m spawn-start
  turn 180
  for k in 1 2 3 4 5; do z 30; st; grep -E '^FRONT (arrival|departure)' "$L" | tail -3 | cut -c1-230; done
  m spawn-behind
  turn 0
  for k in 1 2; do z 30; st; done
  grep -E '^FRONT (arrival|departure)' "$L" | cut -c1-230
  echo "$(date +%T) done"; echo quit >> "$R/cmd"; exit 0
fi
# 1. one orc ahead, the head forward: in view, the game's own turns
w "front keep 0"; w "verbs ahead 350" 0.3; w "practice spawn infantry" 0.3; z 14; m one-front; st
# 2. the head turned 180 deg (the rig's heading stays): the orc behind
turn 180; z 3; m one-behind; z 14; st
turn 0; z 4; st
if [ "$PLAN" = fight ]; then
  # 3. three orcs kept (3.5, 5, 6.5 m ahead): forward, behind, to the side
  w "front keep 1"; w "verbs ahead 500" 0.3; w "practice spawn infantry" 2; w "verbs ahead 650" 0.3
  w "practice spawn shield" 2; w "verbs ahead 350" 0.3; w "practice spawn infantry" 2
  z 16; m three-front; st
  turn 180; z 3; m three-behind; z 14; st
  turn 90; z 3; m three-side; z 10; st
  turn 0; z 6; st
  w "front keep 0"; w "practice remove" 3
  # 4. an archer 12 m ahead (the template: Orc_Archer), forward then behind
  w "verbs template Orc_Archer" 0.3; w "verbs ahead 1200" 0.3; w "practice spawn infantry" 3
  grep '^PRACTICE t=.* spawn' "$L" | tail -1 | cut -c1-200
  z 16; m archer-front; st
  turn 180; z 16; st
  turn 0; z 2
  w "verbs template off" 0.3; w "verbs ahead 350" 0.3; w "practice remove" 2
fi
w "front status" 1
echo "== the run's FRONT summary"
grep -E '^(CONFIG front|FRONT (lockout|gamelog))' "$L" | cut -c1-400
grep -E '^FRONT (warn|ready|grant|attack|expired|prewarned)|^THREAT (pre|start)' "$L" | head -30 | cut -c1-200
grep -E '^FRONT camcopy' "$L" | head -3 | cut -c1-400
grep -E '^FRONT (damage|d12|lockout)' "$L" | head -6 | cut -c1-200
grep -E '^FRONT END' "$L" | cut -c1-900
echo "$(date +%T) done"; echo quit >> "$R/cmd"
