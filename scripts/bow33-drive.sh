#!/bin/bash
# bow33-drive.sh - build 33's bow round (docs/re-xr-pipeline.md 142, som-b33-bow). Boot with scripts/headset-build26.env
# (+ the som-b33-bow block), SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt. The layer's test hands hold the bow as a
# real hand does (abil27c's poses: the fist upright, the bow's limbs along it). B33_STEPS (default all, in order):
#   strike  the bow out, X tapped 0.2 s, X with Y (the bug mark's chord), X held 0.6 s: only the held one strikes
#   tips    the bow held ahead and turned: dumps of the string's ends at the limb tips (from behind up and down, and
#           the front: the head 45 cm back, both ends in view; at rest and drawn)
#   face    an arrow drawn to the cheek: dumps of the string by the face
#   aim     drawn, a 3 deg tremor on the bow hand: the aim's frame steps with `bow twist 0`, then 4 Hz; both hands down
#           15 cm together: the aim's turn
#   take    `bow take grip`: the left grip closed ahead of the body takes the bow, let go puts it away; back to shoulder
# B33_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B33_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B33_STEPS:-tips face aim take strike}
T0=$(date +%s)
R=${B33_RUN:-}
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
L=$R/somvr.log; X=$R/xr-layer.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$L}" 2>/dev/null; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -E "$2"; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/b33-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/b33-steps.txt"; }
d() { x "dump" "$@"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "dump $DN $f" | tee -a "$R/b33-steps.txt"; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
grep -E '^(WARN|CONFIG (strike press|bow take))' "$L" | cut -c1-220
w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5

BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
bow_out() { x "$BOW_SLOT" "$R_REST" "btn L squeeze 0" "btn L a 0" "btn L b 0" "btn R trigger 0"; sleep 0.3; x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1; }
bow_away() { x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; x "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1; }
draw() { x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.4
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  x "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.3; }
letdown() { x "$BOW_STRAIGHT" "$AT_STRING s 0.5" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 0"; sleep 0.8; }

if has strike; then
  echo "== strike: X tapped, X with Y, X held"
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter && bow_away
  w "practice far" 3; bow_out
  n0=$(lines); TS=0.05 x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn L a 1"; sleep 0.15; x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn L a 0"; sleep 0.6
  l=$(since "$n0" '^STRIKE t=.* X (let go|with)'); [ -n "$l" ] && [ -z "$(since "$n0" '^STRIKE t=.* press [0-9]+: X')" ] \
    && pass "a tap: $(echo "$l" | cut -c1-160)" || fail "a tap: $(since "$n0" '^STRIKE' | head -2 | cut -c1-160)"
  n0=$(lines); x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn L a 1" "btn L b 1"; sleep 0.7; x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn L a 0" "btn L b 0"; sleep 0.6
  l=$(since "$n0" '^STRIKE t=.* X with B or Y'); [ -n "$l" ] && [ -z "$(since "$n0" '^STRIKE t=.* press [0-9]+: X')" ] \
    && pass "the chord: $(echo "$l" | cut -c1-160)" || fail "the chord: $(since "$n0" '^STRIKE' | head -2 | cut -c1-160)"
  n0=$(lines); x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn L a 1"; sleep 0.7; x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn L a 0"; sleep 1.5
  l=$(since "$n0" '^STRIKE t=.* press [0-9]+: X'); [ -n "$l" ] && pass "held: $(echo "$l" | cut -c1-200)" || fail "held: no strike"
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | cut -c1-120
  w "practice remove" 1; w "practice back" 1; sleep 1
  [ "$(grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -c exit)" = 1 ] || bow_away
fi

if has tips; then
  echo "== tips: the string's ends against the limbs' tips (scripts/bow_tips.py on the dumps; \`bow slog\`)"
  bow_out
  x "hand L head -5 -22 60 90 0 0" "$R_REST" "btn L squeeze 1"; sleep 0.8
  [ -n "${B33_UP:-}" ] && w "bow stips $B33_UP" 0.4; [ -n "${B33_LO:-}" ] && w "bow stipslo $B33_LO" 0.4
  x "head 0 0 0 yaw 0 pitch 32 s 0.3"; sleep 0.8; w "bow slog 40" 0.2; DN=tips-up d
  x "head 0 0 0 yaw 0 pitch -48 s 0.4"; sleep 0.8; w "bow slog 40" 0.2; DN=tips-down d
  # (the front: the head 45 cm back, the whole bow and both ends in the eyes - the hands stay where they were put)
  x "head 0 0 45 yaw 0 pitch -7 s 0.4"; sleep 0.8; w "bow slog 40" 0.2; DN=front-rest d
  x "head 0 0 0 yaw 0 pitch 0 s 0.3"; sleep 0.5
  x "hand L head -5 -22 60 90 0 0" "$AT_STRING" "btn L squeeze 1"; sleep 0.4
  x "hand L head -5 -22 60 90 0 0" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  x "hand L head -5 -22 60 90 0 0" "hand R head 3 -14 -3 90 0 0 s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.3
  x "head 0 0 0 yaw 0 pitch 32 s 0.3"; sleep 0.8; w "bow slog 40" 0.2; DN=drawn-up d
  x "head 0 0 0 yaw 0 pitch -48 s 0.4"; sleep 0.8; w "bow slog 40" 0.2; DN=drawn-down d
  x "head 0 0 0 yaw 0 pitch 0 s 0.3"; sleep 0.3; DN=drawn-front d
  x "head 0 0 45 yaw 0 pitch -7 s 0.4"; sleep 0.8; w "bow slog 40" 0.2; DN=front-drawn d
  x "head 0 0 0 yaw 0 pitch 0 s 0.3"; sleep 0.3
  letdown
fi

if has face; then
  echo "== face: the string drawn to the cheek"
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter || bow_out
  draw; DN=face-cheek d
  x "$BOW_STRAIGHT" "hand R head 6 -6 -8 90 0 0 s 0.4" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8; DN=face-ear d
  grep -E "xr: hands: frame .* the string" "$X" | tail -2 | cut -c1-160
fi

if has aim; then
  echo "== aim: a tremor on the bow hand, the filter off then on; both hands down"
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter || bow_out
  draw
  x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "hand L shake 0 3"; sleep 0.5
  w "bow twist 0" 3.2; off=$(grep -E '^BOW t=.* bow steps' "$L" | tail -2 | head -1); aoff=$(grep -E '^BOW t=.* aim steps' "$L" | tail -2 | head -1)
  w "bow twist 4" 3.2; on=$(grep -E '^BOW t=.* bow steps' "$L" | tail -2 | head -1); aon=$(grep -E '^BOW t=.* aim steps' "$L" | tail -2 | head -1)
  echo "off: $(echo "$off" | cut -c1-200)"; echo "on:  $(echo "$on" | cut -c1-200)"
  echo "aim off: $(echo "$aoff" | cut -c14-120)"; echo "aim on:  $(echo "$aon" | cut -c14-120)"
  a=$(echo "$off" | grep -oE '[0-9.]+ shown' | grep -oE '^[0-9.]+'); b=$(echo "$on" | grep -oE '[0-9.]+ shown' | grep -oE '^[0-9.]+')
  [ -n "$a" ] && [ -n "$b" ] && python3 -c "import sys; sys.exit(0 if $b < 0.7 * $a else 1)" && pass "the tremor's bow steps shown $a -> $b deg rms" || fail "the tremor: $a -> $b"
  x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "hand L shake 0 0"; sleep 0.8
  t0=$(grep -E '^BOW target t=' "$L" | tail -1 | grep -oE '\(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | head -1)
  x "hand L head -8 -30 55 90 0 0 s 0.6" "hand R head 3 -25 -3 90 0 0 s 0.6" "btn L squeeze 1" "btn R trigger 1"; sleep 1.5
  t1=$(grep -E '^BOW target t=' "$L" | tail -1 | grep -oE '\(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | head -1)
  echo "the ray before $t0, after both hands 15 cm down $t1" | tee -a "$R/b33-steps.txt"
  letdown
fi

if has take; then
  echo "== take: Bow from Grip"
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter && bow_away
  w "bow take grip" 1
  n0=$(lines); x "hand L head -15 -25 35 90 0 0" "$R_REST" "btn L squeeze 0" "btn R trigger 0"; sleep 0.4; x "hand L head -15 -25 35 90 0 0" "$R_REST" "btn L squeeze 1"; sleep 1
  l=$(since "$n0" 'closed away from the shoulder'); [ -n "$l" ] && pass "taken by the grip: $(echo "$l" | cut -c1-160)" || fail "not taken: $(since "$n0" '^BOW' | head -3 | cut -c1-160)"
  DN=take-grip d
  n0=$(lines); x "hand L head -15 -25 35 90 0 0" "$R_REST" "btn L squeeze 0"; sleep 1
  [ -n "$(since "$n0" '^BOW t=.* exit')" ] && pass "let go: put away" || fail "let go: $(since "$n0" '^BOW' | head -2 | cut -c1-160)"
  w "bow take shoulder" 1
  n0=$(lines); x "hand L head -15 -25 35 90 0 0" "$R_REST" "btn L squeeze 1"; sleep 1; x "hand L head -15 -25 35 90 0 0" "$R_REST" "btn L squeeze 0"; sleep 0.5
  [ -z "$(since "$n0" 'the bow out')" ] && pass "shoulder again: the grip ahead takes nothing" || fail "shoulder: the bow came out"
fi
x "hand L off" "hand R off"
echo "== done $(date +%T)"; cat "$R/b33-steps.txt"
echo quit >> "$R/cmd"
