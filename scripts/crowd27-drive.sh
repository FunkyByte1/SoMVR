#!/bin/bash
# crowd27-drive.sh - build 27's combat round (som-b27-combat; docs/re-xr-pipeline.md 108): the crowd of 8 in the practice
# meadow (`practice group 8`: 4 infantry, 2 shields, 2 berserkers round Talion) against FRONT's turns and the crowd's
# pace, then the vault over an orc (SOMVR_WRAITH_STEP_VAULT_CM: the head on a shield orc, the right stick forward and
# held = the gold ring behind it, let go = the blink over it) and a cut at its back after the player's own half turn.
# One headless boot at the practice arrival (crowd19-drive.sh's boot line with scripts/headset-build26.env), the test
# hands (SOMVR_HANDS_TEST=1), the layer's test file; marks and dumps for both-eye pictures (scripts/xr_compose.py).
# CROWD27_STEPS = any of (in this order; default all):
#   look    the group spawned; dumps looking ahead, left-behind and right-behind
#   still   the blade behind the hip (out of every blow's path), `fight guard learn` (CROWD27_STILL_S 40): the timeline
#   guard   `fight guard path` (CROWD27_GUARD_S 50), the head turned to the last cue now and then: blocks and parries
#   dodge   `fight dodge auto 350` (CROWD27_DODGE_S 30)
#   behind  the head turned 180 deg (CROWD27_BEHIND_S 20): the warnings from behind
#   vault   the group removed, one shield orc: CROWD27_VAULTS (3) vaults over it, each with a cut at its back
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CROWD27_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${CROWD27_STEPS:-look still guard dodge behind vault}
N=${CROWD27_N:-8}
T0=$(date +%s)
R=${CROWD27_RUN:-}
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
YAW=0
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
d() { x "dump"; sleep 0.6; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/c27-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
turn() { YAW=$1; x "head 0 0 0 yaw $1 s ${2:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
K=("CROWD grant" "CROWD start" "CROWD impact" "CROWD impact .*UNDER THE PACE" "CROWD .*OVERLAP" "CROWD wait"
   "CROWD cancelled" "FRONT warn t" "DEFENCE contact" "DEFENCE hit" "DEFENCE dodge" "PARRY parry" "COMBAT hit")
NM=("grants" "starts" "impacts" "under" "overlaps" "waits" "cancelled" "warnings" "contacts" "hits" "dodges" "parries"
    "our-hits")
declare -a C0
ph() { local i; for i in "${!K[@]}"; do C0[$i]=$(cnt "^${K[$i]}"); done; PT=$(date +%s); }
pe() { local i s="" c; for i in "${!K[@]}"; do c=$(cnt "^${K[$i]}"); s="$s ${NM[$i]}=$((c - C0[$i]))"; done
  echo "PHASE $1 ($(( $(date +%s) - PT )) s):$s"; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
BEHIND="hands test R 22 -35 -30 0 180 0"
RZ=90
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade behind the hip
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
w "fight guard frame head" 0.5
grep -E '^(WARN|CONFIG wraith step)' "$L" | head -6 | cut -c1-300
if has look || has still || has guard || has dodge || has behind; then
  w "practice group $N" 4
  grep -E '^PRACTICE t=.* group' "$L" | tail -$((N + 1)) | cut -c1-200
  grep -qE "^PRACTICE t=.* group \(the trigger word\): [0-9] of $N orcs out" "$L" || bail "no group"
fi
if has look; then
  d c01-group-ahead; m c01-group-ahead 0
  turn -125 1; z 1; d c02-group-left-behind; turn 125 1; z 1; d c03-group-right-behind; turn 0 1; z 1
fi
if has still; then
  w "fight guard learn" 0.5
  ph; z $(( ${CROWD27_STILL_S:-40} - 12 )); d c04-still; m c04-still 0; z 8; pe still
fi
if has guard; then
  w "fight guard path" 0.5
  ph; e=$(( $(date +%s) + ${CROWD27_GUARD_S:-50} )); k=0
  while [ "$(date +%s)" -lt "$e" ]; do z 5; face; k=$((k + 1)); [ $k -eq 3 ] && d c05-guard; [ $k -eq 6 ] && d c05b-guard; done
  pe guard
fi
if has dodge; then
  w "fight dodge auto 350" 0.5
  ph; z "${CROWD27_DODGE_S:-30}"; d c06-dodge; pe dodge
  w "fight dodge auto 0" 0.5
fi
if has behind; then
  w "fight guard learn" 0.5
  ph; turn 180 1; z 3; d c07-behind; z "${CROWD27_BEHIND_S:-20}"; pe behind
  turn 0 1
fi
if has look || has still || has guard || has dodge || has behind; then
  w "front status" 1
  w "practice remove" 3
  grep -E '^CROWD END|^FRONT END' "$L" | tail -2 | cut -c1-200
fi
# the vault over a shield orc: its bearing from the head's ray (the gaze strike's test word) -> OB_Y OB_P OB_D
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p')
  OB_D=$(echo "$l" | sed -nE 's/.* at ([0-9.]+) m: .*/\1/p'); [ -n "$OB_Y" ]; }
if has vault; then
  w "fight guard off" 0.5
  turn 0 1
  w "practice spawn shield" 3
  w "$REST" 1
  VN=${CROWD27_VAULTS:-3}; V=0; TRY=0
  while [ $V -lt "$VN" ] && [ $TRY -lt $((VN * 4)) ]; do
    TRY=$((TRY + 1))
    orc0 || { echo "no orc 0"; z 2; continue; }
    echo "$(date +%T) orc 0: $OB_Y deg right, $OB_P deg up, $OB_D m"
    if awk "BEGIN{exit !($OB_D > 3.2)}"; then  # walk toward it (the stick) until it is within reach of the vault
      YAW=$(awk "BEGIN{printf \"%.0f\", $YAW + $OB_Y}"); x "head 0 0 0 yaw $YAW s 0.3"; w "stick 0 1 0 0.5" 1.2; continue
    fi
    YAW=$(awk "BEGIN{printf \"%.0f\", $YAW + $OB_Y}"); P=$(awk "BEGIN{printf \"%.0f\", $OB_P}")
    W0=$(cnt '^WSTEP t=.* arrived'); V0=$(cnt '^WSTEP t=.* vault: the orc')
    x "head 0 0 0 yaw $YAW pitch $P s 0.25"
    TS=0.3 x "head 0 0 0 yaw $YAW pitch $P" "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0" "stick R 0 0.9"; sleep 0.6; d "v$V-aim"
    grep -E '^WSTEP t=.* aim:' "$L" | tail -1 | cut -c1-330
    x "head 0 0 0 yaw $YAW pitch $P" "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0" "stick R 0 0"; sleep 0.3
    waitfor '^WSTEP t=.* arrived' "$W0" 4 || { echo "no arrival"; grep -E '^WSTEP t=' "$L" | tail -3 | cut -c1-300; z 2; continue; }
    [ "$(cnt '^WSTEP t=.* vault: the orc')" -gt "$V0" ] || { sleep 0.3; echo "not a vault"; z 2; continue; }
    # the player's half turn (the right stick's snap turns: the facing is the player's), then the cut at its back
    H0=$(cnt '^COMBAT hit tier')
    TS=0.3 x "head 0 0 0 yaw $((YAW + 180)) pitch -10 s 0.3"; YAW=$((YAW + 180))
    ecut; d "v$V-turned"; sleep 0.5
    grep -E '^WSTEP t=.* (blink onto|arrived|vault:)' "$L" | tail -3 | cut -c1-330
    if [ "$(cnt '^COMBAT hit tier')" -gt "$H0" ]; then grep -E '^(COMBAT hit tier|COMBAT shield|PARRY (open|riposte))' "$L" | tail -4 | cut -c1-330
    else echo "the cut met nothing"; fi
    V=$((V + 1)); z 3
  done
  echo "VAULTS $V of $VN ($TRY tries)"
  w "practice remove" 3
fi
echo "== the run's summary"
grep -E '^CONFIG (front|crowd|wraith)' "$L" | cut -c1-300
grep -E '^CROWD END|^FRONT END|^WSTEP END' "$L" | cut -c1-1500
echo "$(date +%T) done"; [ -n "${CROWD27_NOQUIT:-}" ] || echo quit >> "$R/cmd"
