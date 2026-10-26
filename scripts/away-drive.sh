#!/bin/bash
# away-drive.sh - the wraith round 2 (handoff/HANDOFF-wraith.md "Round 2", docs/re-bow.md 14.7): Talion hidden while the
# view is held away from him (SOMVR_HP_AWAY), the strike's cost. Boot as wraith-drive.sh says (build 11's switches +
# SOMVR_BOW_STRIKE=dash SOMVR_WISPS=hand SOMVR_PRACTICE_AHEAD_CM=1000 + the test switches + SOMVR_XR_DUMP_MARKED=24: the
# layer dumps the frame after each strike moment the preload marks: before, mid-hold, the dash's start, mid-dash, the
# arrival, the sync, after; `STRIKE t=... dump mark` and `xr: dump mark` lines) and this script at the same time.
# Stops at once (quit) when the user goes into VR. Steps: AW_MODES (below).
# WR_STEPS = any of (in this order):
#   wisps    empty hands, the left hand held up before the eyes: a picture as built, then `wisps test LO HI 0 25 0` per
#            node group of the left arm (which node carries the effect), then `wisps hand` (the fix) and `wisps game`
#   bow      the bow out (the left trigger): `wisps game` / `wisps hand` pictures, the hand drawn too
#   strike   an orc 10 m ahead, the bow aimed at it, X while the game's Shadow Strike prompt is up: `bow strike dash`
#            (WR_STRIKE_MODES, default "stock dash blink": the game's own presentation first), the STRIKE trace lines
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${BOW_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${WR_STEPS:-wisps bow strike}
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
  grep -q '"user": true' /run/user/1000/vr-session/status.json 2>/dev/null && return 0
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
check() { if grep -qE "$2" "$R/somvr.log"; then echo "$(date +%T) PASS $1"; else echo "$(date +%T) FAIL $1"; fi; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
down() { grep -q "^PRACTICE t=.* orc is down" "$R/somvr.log"; }
# turn (snap words) until the nearest orc is within 20 deg of the bow's ray (bo_target's bearing)
face_bow() { local i b y; for i in 1 2 3 4 5 6; do sleep 0.7; b=$(bearing); [ -n "$b" ] || return 0; y=${b% *}; y=${y#+}; y=${y%.*}
  if [ "$y" -gt 20 ]; then w "turn+" 0.5; elif [ "$y" -lt -20 ]; then w "turn-" 0.5; else return 0; fi; done; }
# the poses (head frame, cm): both hands written together (one head sample: their distance is exact). pose A P D [S]:
# a right-handed archer's anchor at the jaw (the controller 7 right, 13 down, level with the eyes: a real controller
# cannot be in front of the face), the bow hand BOW_L (60) cm from it along yaw A (deg, + right) and pitch P (+ up),
# turned the same way; the string hand D cm back from the bow hand toward the anchor (4 = at the nock, 57 = drawn to
# the jaw), over S s
pose() { awk -v a="$1" -v p="$2" -v d="$3" -v sm="${4:-}" -v L="${BOW_L:-60}" 'BEGIN { r = 3.14159265 / 180
  dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 7; ay = -13; az = 0
  bx = ax + L * dx; by = ay + L * dy; bz = az + L * dz; k = L - d
  printf "hand L head %.1f %.1f %.1f %.1f %.1f 0\n", bx, by, bz, p, -a
  printf "hand R head %.1f %.1f %.1f %.1f %.1f 0%s\n", ax + k * dx + 1.5, ay + k * dy, az + k * dz, p, -a, sm != "" ? " s " sm : "" }'; }
setpose() { local IFS=$'\n'; t $(pose "$@"); }
REST="hand L head -22 -40 15 0 0 0
hand R head 22 -40 15 0 0 0"
rest() { local IFS=$'\n'; t $REST; }
A=0; P=0
# the nearest orc's bearing from the ray (bo_target's line), "DY DP" or nothing: its head (BOW_AIM=head, the default:
# the headshot) or its chest (BOW_AIM=chest)
bearing() { local l; l=$(grep -E '^BOW target ' "$R/somvr.log" | tail -1)
  if [ "${BOW_AIM:-head}" = head ]; then echo "$l" | sed -nE 's/.*its head ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1 \2/p'
  else echo "$l" | sed -nE 's/.*its chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1 \2/p'; fi; }
# aim at the orc: N times, A/P = where the target is seen from the string hand, the yaw against the head's (the test
# hands sit in the head's yaw frame, which drifts) - an absolute aim, no accumulated corrections - each after a fresh
# BOW target line (1.1 s: the pose's poll, the 2 Hz line), the draw D held
aimat() { local l w; l=$(grep -E '^BOW target ' "$R/somvr.log" | tail -1); w=${BOW_AIM:-head}
  echo "$l" | sed -nE "s/.*the $w at head yaw ([-+][0-9.]+) pitch ([-+][0-9.]+).*/\1 \2/p"; }
aim() { local k b; for k in $(seq 1 "${2:-3}"); do sleep 1.1; b=$(aimat); [ -n "$b" ] || return 0
  A=${b% *}; P=${b#* }; A=${A#+}; P=${P#+}
  echo "$(date +%T) aim: the ${BOW_AIM:-head} at yaw $A pitch $P (head frame)"; setpose "$A" "$P" "$1"; done; }
# d NAME: a stereo dump now (both eyes + our hands' layer; compose with scripts/xr_compose.py RUN), noted in RUN/bow-steps.txt
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/wr-steps.txt"; }
# shoot [AIMN]: nock, draw over 0.7 s (aimed at the orc AIMN times while drawn), hold, release; marks/dumps with MK=name
shoot() {
  setpose "$A" "$P" 4; sleep 0.3; t "btn R squeeze 1"; waitfor '^BOW .* nock: ' 2 || echo "$(date +%T) no nock line"
  setpose "$A" "$P" 57 0.8; sleep 0.6; waitfor '^BOW .* (armed|full draw)' 2
  [ "${1:-0}" -gt 0 ] && aim 57 "$1"
  [ -n "${MK:-}" ] && { d "$MK-drawn"; m "$MK-drawn" 0; }
  T_S=0.1 t "btn R squeeze 0"; waitfor '^BOW .* (fire:|dropped:|let down:)' 3
  grep -E '^BOW .* (fire:|dropped:|let down:)' "$R/somvr.log" | tail -1 | cut -c1-300
  [ -n "${MK:-}" ] && { d "$MK-released"; m "$MK-released" 0; }
  sleep 0.8; grep -E '^BOW (arrow|hit) ' "$R/somvr.log" | tail -3 | cut -c1-300; }
# aimed: the bow up and aimed at the nearest orc (spawned first when none is near), the string hand at rest by the bow
aimed() {
  A=0; P=-2; setpose "$A" "$P" 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
  [ "${ORC:-0}" = 1 ] || { w "practice spawn infantry" 0.3; waitfor '^PRACTICE t=.* spawn ' 10; ORC=1; }
  sleep 1.5; face_bow; aim 30 2; }
unaim() { T_S=0.3 t "btn L trigger 0"; rest; waitfor '^BOW .* exit: ' 4; sleep 1; }
press() { t "btn $1 $2 1"; sleep 0.5; t "btn $1 $2 0"; sleep 1.2; }
# 0. the controls card away (the right grip, else A), the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
m a00-start 0.5
check "the strike's config" '^CONFIG strike='
check "the away config" '^CONFIG head parts away'
grep -E '^(WARN|BOWSITE .*MISMATCH)' "$R/somvr.log" | head -5
w "bow arrows ${BOW_ARROWS:-40}" 1
# the Shadow Strike's prompt (121) up now: `bow where` says it (STRIKE's own state); 0 = up within ${1:-4} s
prompt_up() { local i; for i in $(seq 1 $((${1:-4} * 3))); do
  userin && bail "the user is in VR"
  printf '%s' "bow where" > "$R/somvr.trigger"; sleep 0.33
  grep -E '^BOW where' "$R/somvr.log" | tail -1 | grep -q 'prompt up$' && return 0; done; return 1; }
# the bow up, a fresh orc 10 m ahead (it charges within ~3 s: press while it is far)
bowup() { w "bow arrows 40" 0.5; A=0; P=-2; setpose "$A" "$P" 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
  w "practice spawn infantry" 0.3; ORC=1; }
again() { unaim; w "practice remove" 2; ORC=0; sleep 1
  # back to the practice spot facing the meadow (a strike leaves him at the orc; `practice go` there is refused)
  w "practice back" 0.5; waitfor '^PRACTICE t=.* the blink back ended' 15; sleep 1
  w "practice go" 0.5; waitfor '^PRACTICE t=.* the blink there ended' 15; sleep 1.5; }
costw() { case $1 in free) w "bow strike free" 0.5 ;; cost) w "bow strike cost" 0.5 ;;
  charge|energy) w "bow strike cost" 0.3; w "bow strike free $1" 0.5 ;; esac; }
# AW_MODES items AWAY:COST:MARKS[:probe] - SOMVR_HP_AWAY's word, the costs (free = both waived, cost = the game's, charge
# / energy = only that one waived), the moments dumped (1 before, 2 mid-hold, 4 the start, 8 mid-dash, 16 the arrival,
# 32 the sync, 64 after); probe: after it, back at the spot with a fresh orc, does the prompt come back (15 s)?
k=0
for item in ${AW_MODES:-hands:free:41 hands:free:82 off:free:24 hide:free:40 hands:charge:0:probe hands:cost:0:probe}; do
  k=$((k + 1)); IFS=: read -r am cost mk probe <<< "$item"
  w "hpaway $am" 0.5; costw "$cost"; w "bow strike marks $mk" 0.5; w "bow strike dash" 0.5
  bowup
  if prompt_up 6; then echo "$(date +%T) strike $k ($item): the prompt is up"; else echo "$(date +%T) strike $k ($item): no prompt (pressing anyway)"; fi
  n0=$(grep -cE '^STRIKE t=.* (start|no move)' "$R/somvr.log")
  T_S=0.3 t "btn L a 1"; t "btn L a 0" # X
  for i in $(seq 1 16); do [ "$(grep -cE '^STRIKE t=.* (start|no move)' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.25; done
  sleep 5 # the sync (3.6 s) and the release; the marks dump the moments
  echo "strike $k $item" >> "$R/wr-steps.txt"
  grep -E '^STRIKE t=.* dump mark' "$R/somvr.log" | tail -4 | cut -c1-200 >> "$R/wr-steps.txt"
  grep -E 'xr: dump mark' "$R/xr-layer.log" | tail -4 | sed -E 's/^[0-9.]+ \[[0-9/]+\] //' | cut -c1-200 | tee -a "$R/wr-steps.txt"
  grep -E '^STRIKE t=.* (press|start|end|no move|the orcs)' "$R/somvr.log" | tail -4 | cut -c1-300
  grep -E '^HEADPARTS t=.* away (begins|ends)' "$R/somvr.log" | tail -2 | cut -c1-300
  again
  if [ "$probe" = probe ]; then
    bowup
    if prompt_up 15; then echo "$(date +%T) COST after strike $k ($cost): the prompt came back" | tee -a "$R/wr-steps.txt"
    else echo "$(date +%T) COST after strike $k ($cost): no prompt for 15 s at the spot" | tee -a "$R/wr-steps.txt"
      grep -E '^BOW target' "$R/somvr.log" | tail -1 | cut -c1-160 | tee -a "$R/wr-steps.txt"
      # X with no prompt: the denied buzz at the press
      T_S=0.3 t "btn L a 1"; t "btn L a 0"; sleep 2.5
      grep -E '^(STRIKE t=.* (press|no move)|HAP L .*src=combat)' "$R/somvr.log" | tail -3 | cut -c1-200
      unaim; w "practice remove" 2; ORC=0; break
    fi
    unaim; w "practice remove" 2; ORC=0; sleep 1
  fi
done
# AW_ARROWS=1: the strike's arrows - shots into the sky down to 1 arrow (no refill), then the prompt and X with 1, then 0
if [ "${AW_ARROWS:-0}" = 1 ]; then
  w "hpaway hands" 0.3; w "bow strike cost" 0.3; w "bow strike marks 0" 0.3
  arrows() { grep -oE 'arrows [0-9]+' "$R/somvr.log" | tail -1 | grep -oE '[0-9]+$'; }
  A=0; P=30; setpose "$A" "$P" 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
  for i in $(seq 1 8); do a=$(arrows); echo "$(date +%T) arrows $a"; [ "${a:-0}" -le 1 ] && break; shoot 0; done
  unaim
  for left in 1 0; do
    A=0; P=-2; setpose "$A" "$P" 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
    w "practice spawn infantry" 0.3; ORC=1
    if prompt_up 6; then echo "$(date +%T) ARROWS $(arrows): the Shadow Strike prompt is up" | tee -a "$R/wr-steps.txt"
    else echo "$(date +%T) ARROWS $(arrows): no Shadow Strike prompt for 6 s" | tee -a "$R/wr-steps.txt"; fi
    n0=$(grep -cE '^STRIKE t=.* (start|no move)' "$R/somvr.log")
    T_S=0.3 t "btn L a 1"; t "btn L a 0"
    for i in $(seq 1 16); do [ "$(grep -cE '^STRIKE t=.* (start|no move)' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.25; done
    sleep 5; grep -E '^STRIKE t=.* (press|start|no move|the orcs)' "$R/somvr.log" | tail -3 | cut -c1-260 | tee -a "$R/wr-steps.txt"
    again
  done
fi
echo "steps done $(date +%T)"
grep -E '^(STRIKE|WISPS) END|^SUMMARY head parts away' "$R/somvr.log" | cut -c1-300
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
