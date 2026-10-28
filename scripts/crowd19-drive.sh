#!/bin/bash
# crowd19-drive.sh - build 19's first crowd (docs/re-xr-pipeline.md 70, handoff/HANDOFF-combat-design.md item 5): the
# practice page's group (`practice group`: 4 orcs around Talion) against the crowd's pace (SOMVR_CROWD_PACE_MS); one
# headless boot at the practice arrival, the test hands (SOMVR_HANDS_TEST=1), the layer's test file, marks with both
# eyes. The attack timeline is the mod's own `CROWD` lines (grants, starts, impacts with their gaps and bearings).
# Boot (sandbox off):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build18.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 [SOMVR_CROWD_LOGONLY=1]"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
#   this script at the same time.
# CROWD19_STEPS = any of (in this order; default all):
#   look    the group spawned; marks looking ahead, left-behind and right-behind (where they stand)
#   still   the blade behind the hip (out of every blow's path), the head ahead, `fight guard learn` (CROWD19_STILL_S,
#           60): the pace's timeline with the blows landing (every blow's path learnt for the guard)
#   guard   `fight guard path` (CROWD19_GUARD_S, 60), the head turned to the last cue now and then (a player facing the
#           attacker): blocks and parries in a crowd
#   dodge   `fight dodge auto 350` (A 350 ms after each red warning; CROWD19_DODGE_S, 40): dodges in a crowd
#   behind  the head turned 180 deg (CROWD19_BEHIND_S, 25): the warnings from behind with the pace
# Env: CROWD19_STEPS, CROWD19_N (3|4), CROWD19_WT. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CROWD19_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${CROWD19_STEPS:-look still guard dodge behind}
T0=$(date +%s)
R=${CROWD19_RUN:-} # CROWD19_RUN=RUN: drive a running boot (build 19: tether19-drive.sh chains drivers)
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
turn() { x "head 0 0 0 yaw $1 s ${2:-0.6}" 1; }
cnt() { grep -cE "$1" "$L"; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# the per-phase deltas: line counts at the phase's start (ph) and its end (pe NAME)
K=("CROWD grant" "CROWD start" "CROWD impact" "CROWD impact .*UNDER THE PACE" "CROWD .*OVERLAP" "CROWD wait" "CROWD pass"
   "CROWD cancelled" "FRONT warn t" "DEFENCE contact" "DEFENCE hit" "DEFENCE dodge" "TGUARD .*the blades met: blocked"
   "TGUARD .*not blocked")
N=("grants" "starts" "impacts" "under" "overlaps" "waits" "pass" "cancelled" "warnings" "contacts" "hits" "dodges"
   "tg-blocked" "tg-unblocked")
declare -a C0
ph() { local i; for i in "${!K[@]}"; do C0[$i]=$(cnt "^${K[$i]}"); done; PT=$(date +%s); }
pe() { local i s="" c; for i in "${!K[@]}"; do c=$(cnt "^${K[$i]}"); s="$s ${N[$i]}=$((c - C0[$i]))"; done
  echo "PHASE $1 ($(( $(date +%s) - PT )) s):$s"; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every blow's path
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# the wrist menu (practice-menu-drive.sh's): t LINE... = one step of the layer's test file; press U V = the right
# fingertip through the panel; the Practice tab 0.441 0.042, Spawn group 0.727 0.411 (somvr_vm_rect: 272+100, 292+24)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; return 1; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
G0=$(cnt '^PRACTICE t=.* group \(the menu\): [34] of [34] orcs out')
if [ "${CROWD19_MENU:-1}" = 1 ] && menu_open; then
  t "dump"; sleep 1.5; press 0.441 0.042; sleep 1.5; press 0.727 0.411; sleep 4
  m c00-menu 0
  menu_shut
fi
if [ "$(cnt '^PRACTICE t=.* group \(the menu\): [34] of [34] orcs out')" -gt "$G0" ]; then
  echo "PASS the group from the menu's Spawn group"; GROUP_MENU=1
else
  echo "FAIL the menu's Spawn group (the word instead)"; GROUP_MENU=0
fi
# 0. the controls card away, the left hand forward, the sword drawn, the blade behind the hip
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
w "fight guard frame head" 0.5
# 1. the group (the menu's, else the word's)
[ "$GROUP_MENU" = 1 ] || w "practice group ${CROWD19_N:-4}" 3
grep -E '^PRACTICE t=.* group' "$L" | tail -5 | cut -c1-230
grep -qE '^PRACTICE t=.* group \((the trigger word|the menu)\): [34] of [34] orcs out' "$L" || bail "no group"
if has look; then
  x "dump" 1.5; m c01-group-ahead 0
  turn -125 1; z 1; x "dump" 1.5; m c02-group-left-behind 0
  turn 125 1; z 1; x "dump" 1.5; m c03-group-right-behind 0
  turn 0 1; z 1
fi
if has still; then
  w "fight guard learn" 0.5
  ph; z $(( ${CROWD19_STILL_S:-60} - 20 )); x "dump" 1; m c04-still 0; z 18; pe still
  grep -E '^CROWD (impact|start)' "$L" | tail -8 | cut -c1-260
fi
if has guard; then
  w "fight guard path" 0.5
  ph; e=$(( $(date +%s) + ${CROWD19_GUARD_S:-60} )); k=0
  while [ "$(date +%s)" -lt "$e" ]; do z 6; face; k=$((k + 1)); [ $k -eq 3 ] && { x "dump" 1; m c05-guard 0; }; done
  pe guard
fi
if has dodge; then
  w "fight dodge auto 350" 0.5
  ph; z "${CROWD19_DODGE_S:-40}"; m c06-dodge 0; pe dodge
  w "fight dodge auto 0" 0.5
fi
if has behind; then
  w "fight guard learn" 0.5
  ph; turn 180 1; z 3; x "dump" 1; m c07-behind 0; z "${CROWD19_BEHIND_S:-25}"; pe behind
  turn 0 1
fi
w "front status" 1
w "practice remove" 3
echo "== the run's summary"
grep -E '^CONFIG (front|crowd)' "$L" | cut -c1-300
grep -E '^CROWD END|^FRONT END' "$L" | cut -c1-1500
echo "$(date +%T) done"; [ -n "${CROWD19_NOQUIT:-}" ] || echo quit >> "$R/cmd" # CROWD19_NOQUIT=1: leave it running
