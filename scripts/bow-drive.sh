#!/bin/bash
# bow-drive.sh - build 11's bow (SOMVR_BOW=1 on build 10's switches; handoff/HANDOFF-bow.md, docs/re-bow.md 12) in one
# headless boot. Boot (outside the sandbox, from the main checkout, a build snapshot whose path names this worktree):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' <wt>/ShadowOfMordor/scripts/headset-build10.env |
#     tr '\n' ' ') SOMVR_BOW=1 SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
#     setsid scripts/headless_vr.sh <snapshot>/somvr
# and this script at the same time (BOW_WT = a word of the launch command, default this worktree's name). It waits for
# the newest boot-check run whose launch-cmd.txt names it, then for the practice arrival, and ends with a quit. Stops at
# once (quit) when the user goes into VR. The hands are the layer's test hands (RUN/xrtest.txt: `hand L|R head x y z rx
# ry rz [s S]` = cm right/up/ahead of the head's yaw frame, deg about the OpenXR grip's X Y Z, moved over S s; `btn L|R
# trigger|squeeze|a|b|menu V`), so the bow reads them through the real input path. Every picture is a bug mark (both
# eyes, RUN/marks/).
# BOW_STEPS = any of (in this order):
#   summon  the left trigger held: the bow forms (the game's aim mode); nock, draw, release into the meadow; let go
#   orc     an infantry spawned ahead (SOMVR_PRACTICE_AHEAD_CM), the bow aimed at it: shots until it is down (at most 6)
#   stock   the same with the stock fire command (`bow fire 125`: tapped once the game's aim runs), then `bow fire ours`
#   snap    a quick draw and release under the aim's 0.25 s (the fire waits), a let-down (no shot)
#   strike  the bow aimed at an orc, X (the left lower button): the Shadow Strike (only with SOMVR_BOW_STRIKE)
#   empty   shots until the arrows run out: the "denied" pulse, no shot
#   fx      the wraith's FX A/B (needs SOMVR_BOW_FX at boot): `bow fx game|nofx|nofxalt|none`, an aim and a draw each
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BOW_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${BOW_STEPS:-summon orc snap}
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
      echo "$1 $f" | tee -a "$R/bow-steps.txt"; }
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
# 0. the controls card away (the right grip, else A), the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
m a00-start 0.5
check "the bow's config" '^CONFIG bow=1'
grep -E '^(WARN|BOWSITE .*MISMATCH)' "$R/somvr.log" | head -5
w "bow attacks" 1; w "bow arrows ${BOW_ARROWS:-40}" 1
grep -E '^BOW (arrows|attacks)' "$R/somvr.log" | tail -3 | cut -c1-400
if has summon; then
  setpose 0 0 30; sleep 0.5          # the string hand away from the nock (no draw)
  t "btn L trigger 1"
  sleep 1.5; check "enter (the left trigger)" '^BOW .* enter \(by the button\)' 
  waitfor "^BOW .* last 1 s: .*the game's aim on" 4 && echo "$(date +%T) PASS the game's aim on" || echo "$(date +%T) FAIL the game's aim not on"
  d b01-summoned; m b01-bow-summoned 0
  setpose 0 0 8; sleep 1.5; check "the hover tick" '^BOW .* hover: ' 
  MK=b02 shoot
  # a drawn picture at full draw, the arrow kept (a let-down: the grip opened at the nock)
  setpose 0 0 4; sleep 0.3; t "btn R squeeze 1"; setpose 0 0 57 0.8; sleep 1.2
  d b04-drawn; m b04-drawn 0
  setpose 0 0 8 0.6; sleep 0.8; t "btn R squeeze 0"; sleep 0.6
  check "let-down (no arrow)" '^BOW .* let down'
  T_S=0.3 t "btn L trigger 0"; rest
  waitfor '^BOW .* exit: ' 4 && echo "$(date +%T) PASS exit" || echo "$(date +%T) FAIL no exit"
  m b03-bow-gone 0.5
fi
# orc_round TAG N: the bow up, an infantry spawned (SOMVR_PRACTICE_AHEAD_CM: 15 m for this test), N shots aimed at it
orc_round() {
  A=0; P=-2; setpose "$A" "$P" 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
  # drawn before the orc comes (it charges within ~3 s): nock, draw, then the spawn, one correction, the release
  setpose "$A" "$P" 4; sleep 0.4; t "btn R squeeze 1"; setpose "$A" "$P" 57 0.6; sleep 0.8
  w "practice spawn infantry" 0.3
  waitfor '^PRACTICE t=.* spawn ' 10
  [ -n "${ORCHP:-}" ] && { sleep 0.5; w "bow orchp $ORCHP" 0.5; }
  d "$1-01-drawn"; aim 57 2
  T_S=0.1 t "btn R squeeze 0"; waitfor '^BOW .* (fire:|dropped:)' 3
  grep -E '^BOW .* (fire:|dropped:)' "$R/somvr.log" | tail -1 | cut -c1-300
  d "$1-02-released"; m "$1-02-released" 0
  sleep 1; grep -E '^BOW (arrow|hit) ' "$R/somvr.log" | tail -2 | cut -c1-300
  for k in $(seq 2 "$2"); do
    down && break
    face_bow
    MK= shoot 2
    [ -z "${HIT:-}" ] && grep -qE '^BOW hit check .*(DEAD|gone|-> [0-9.]+ *(;|$))' "$R/somvr.log" && { HIT=1; m "$1-03-hit" 0; }
  done
  down && echo "$(date +%T) PASS $1: the orc is down" || echo "$(date +%T) FAIL $1: the orc is not down"
  d "$1-04-after"; m "$1-04-after" 0.5
  T_S=0.3 t "btn L trigger 0"; rest
  w "practice remove" 2
}
if has fx; then
  # the wraith's FX A/B (SOMVR_BOW_FX set at boot): per mode one aim: the open string hand by the bow, then drawn
  for fm in game nofx nofxalt none; do
    w "bow fx $fm" 0.6
    setpose 0 0 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1.5
    setpose 0 0 8; sleep 1; d "fx-$fm-aim"
    setpose 0 0 4; sleep 0.3; t "btn R squeeze 1"; setpose 0 0 57 0.8; sleep 1.2; d "fx-$fm-drawn"
    setpose 0 0 8 0.6; sleep 0.8; t "btn R squeeze 0"; sleep 0.4
    T_S=0.3 t "btn L trigger 0"; rest; waitfor '^BOW .* exit: ' 4; sleep 1
  done
fi
if has stock; then
  w "bow fire 125" 1
  HIT= orc_round s 4
  w "bow fire ours" 1
fi
if has orc; then
  HIT= orc_round c 6
fi
if has snap; then
  A=0; P=0
  setpose 0 0 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1.5
  # a quick nock-draw-release (0.15 s)
  setpose 0 0 3; sleep 0.3; T_S=0.1 t "btn R squeeze 1"; T_S=0.1 setpose 0 0 33 0.15; sleep 0.3
  t "btn R squeeze 0"; waitfor '^BOW .* (fire:|dropped:|let down:)' 3
  grep -E '^BOW .* (fire:|dropped:|let down:)' "$R/somvr.log" | tail -1 | cut -c1-200
  sleep 1
  # the let-down: nock, a 10 cm pull, open
  setpose 0 0 3; sleep 0.4; t "btn R squeeze 1"; setpose 0 0 13 0.3; sleep 0.4; t "btn R squeeze 0"
  waitfor '^BOW .* let down' 3 && echo "$(date +%T) PASS let-down" || echo "$(date +%T) FAIL no let-down"
  T_S=0.3 t "btn L trigger 0"; rest
  waitfor '^BOW .* exit: ' 4
fi
if has strike; then
  w "practice spawn infantry" 1.0
  waitfor '^THREAT start ' 15
  face
  A=0; P=0; setpose 0 0 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
  b=$(bearing); [ -n "$b" ] && { A=${b% *}; P=${b#* }; setpose "$A" "$P" 30; }
  d d01-strike-aim; m d01-strike-aim 0
  t "btn L a 1"; sleep 0.3; t "btn L a 0"
  waitfor '^BOW .* strike' 4
  grep -E '^BOW .* strike' "$R/somvr.log" | tail -2 | cut -c1-220
  d d02-strike; m d02-strike 0.2
  sleep 2
  T_S=0.3 t "btn L trigger 0"; rest
  w "practice remove" 2
fi
if has empty; then
  A=0; P=10; setpose 0 10 30; t "btn L trigger 1"; waitfor '^BOW .* enter ' 3; sleep 1
  for k in $(seq 1 30); do
    MK= shoot
    grep -qE '^BOW .* dropped: our shot refused \(no arrows' "$R/somvr.log" && { echo "$(date +%T) PASS no arrows: refused"; break; }
  done
  T_S=0.3 t "btn L trigger 0"; rest
fi
grep -cE 'cmd=125|CombatSecondary04' "$R/somvr.log" | sed 's/^/stealth command lines: /'
echo "steps done $(date +%T)"
grep -E '^BOW (END|.*exit:|.*fire:|.*dropped:)' "$R/somvr.log" | tail -12 | cut -c1-220
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
