#!/bin/bash
# bow19-drive.sh - build 19's bow (handoff/HANDOFF-bow.md "build 19", docs/re-xr-pipeline.md 71), headless at the
# practice arrival; bow18-drive.sh's helpers and real bow-hold poses. Boot (sandbox off) with scripts/headset-build19.env:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build19.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time. Pictures: RUN/b19-steps.txt (NAME FRAME), compose with scripts/xr_compose.py.
# B19_STEPS (default: tips grab loop) = any of bow18's (axes string hold tips) and: tipsview (the bow held low, side-on:
#   its upper tip at eye height; TIPS_GRID="ALONG:BACK ..." with `bow stips`, `bow srest ours`), abtn (A with the bow out
#   in a fight: `bow abtn buzz` vs quiet), grab (the trigger pressed outside the quiver and held into it, high, far down
#   the back, an empty grab beside the bow, held 1.5 s), loop (take, carry, nock, draw - also the user's own 17b anchor -,
#   release, a let-down's resting arrow, with pictures), ba (at rest: build 18's look vs build 19's env, three sides).
#   tipslo (the bow held high: the lower end alone, TIPSLO_GRID with `bow stipslo`, TIPSLO_PICK),
#   touch (an arrow brought to the string seen at rest, let go there, drawn again there: `bow srest tips` vs ours).
# B18_STEPS = any of: axes (which hand turns show the bow from its sides: BOW axes lines), string (the bow at rest,
#   nocked and drawn, straight and turned both ways, `bow sdraw off` vs on, then `drawn` - build 18's env), hold (the arrow from the quiver in the hand,
#   `bow ahold old` vs pinch, two hand angles), tips (run 2: our string's ends `bow stips 0|20|35|50` cm past the tip nodes
#   against the game's own string, seen from its visible side; then nocked and drawn at TIPS_PICK)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B19_WT:-${B18_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}}
STEPS=${B19_STEPS:-${B18_STEPS:-tips grab loop}}
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$L"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$L")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
cnt() { grep -cE "$1" "$L"; }
# ex NAME REGEX SECONDS LINE...: the test file's lines, then PASS when a new line matches (counted from before them);
# exw NAME REGEX SECONDS WORD: the same for a trigger word
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2")" -gt "$4" ] && { echo "$(date +%T) PASS $1: $(grep -E "$2" "$L" | tail -1 | cut -c1-150)"; return 0; }
  sleep 0.25; done; echo "$(date +%T) FAIL $1 (no new '$2')"; return 1; }
ex() { local nm=$1 re=$2 sec=$3 n0; shift 3; n0=$(cnt "$re"); T_S=0.05 t "$@"; seen "$nm" "$re" "$sec" "$n0"; }
exw() { local n0; n0=$(cnt "$2"); w "$4" 0.05; seen "$1" "$2" "$3" "$n0"; }
# d NAME: a stereo dump now (both eyes + our hands' layer; compose with scripts/xr_compose.py RUN)
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/b19-steps.txt"; }


REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
rest() { t "$REST_L" "$REST_R"; }
SLOT_L="hand L head -12 -10 -15 90 0 0"
QUIVER="hand R head 12 -10 -15 90 0 0"
BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_NOCK="hand R head -4 -11 47 90 0 0"
DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
HALF="hand R head 0 -10 22 90 0 0 s 0.6"
CARRY="hand R head 14 -22 32 60 0 0 s 0.5"
CARRY2="hand R head 6 -16 34 90 0 0 s 0.4"
bow_out() { t "$SLOT_L"; sleep 0.4; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1; }
bow_back() { t "$REST_R"; t "$SLOT_L"; sleep 0.5; ex "${1:-put back}" '^BOW .* exit: ' 3 "btn L squeeze 0"; sleep 0.6; }
take() { t "$QUIVER"; sleep 0.5; ex "${1:-an arrow taken}" 'quiver: an arrow taken' 3 "btn R trigger 1"; }
isout() { grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter '; }
out_again() { isout || bow_out; }
axes() { grep -E '^BOW axes' "$L" | tail -1 | sed 's/.*the bow/the bow/' | cut -c1-90; }
grep -E '^(WARN|CONFIG bow arrow_hold)' "$L" | head -6 | cut -c1-240
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
m a00-start 0.5
# the bow turned: about the fist's tube (the bow's limbs) by these hand poses; picked in step axes by the shot's axis
TURN_L="hand L head -8 -15 55 90 0 60"
TURN_R="hand L head -8 -15 55 90 0 -60"
if has axes; then
  echo "== axes: the turns"
  bow_out
  for p in "90 0 0" "90 0 60" "90 0 -60" "90 60 0" "90 -60 0"; do t "hand L head -8 -15 55 $p"; sleep 1.3; echo "pose $p: $(axes)"; done
  t "$BOW_STRAIGHT"; sleep 0.5
fi
if has string; then
  echo "== string: at rest, nocked, drawn; straight and turned"
  out_again
  for sd in off on; do
    w "bow sdraw $sd" 0.4
    for p in STRAIGHT L R; do
      case $p in STRAIGHT) P="$BOW_STRAIGHT" ;; L) P="$TURN_L" ;; R) P="$TURN_R" ;; esac
      t "$P" "$REST_R"; sleep 1.2; d "rest-$p-$sd"
    done
  done
  grep -E 'xr: hands: frame .*the string' "$R/xr-layer.log" | tail -2 | cut -c1-120
  w "bow sdraw drawn" 0.4; t "$TURN_L" "$REST_R"; sleep 1.2; d rest-L-drawnmode   # (build 18's env: none of ours at rest)
  take; ex "nocked" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.4; d nocked-STRAIGHT
  t "$TURN_L" "$AT_NOCK"; sleep 1; d nocked-L
  t "$BOW_STRAIGHT" "$HALF"; sleep 1; d half-STRAIGHT
  ex "full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.6; d drawn-STRAIGHT
  t "$TURN_L" "$DRAWN"; sleep 1.2; d drawn-L
  t "$TURN_R" "$DRAWN"; sleep 1.2; d drawn-R
  w "bow sdraw off" 0.4; t "$BOW_STRAIGHT" "$DRAWN"; sleep 1.2; d drawn-STRAIGHT-off; w "bow sdraw drawn" 0.4
  ex "the shot" '^BOW .* release: ' 3 "btn R trigger 0"; sleep 1.5; out_again
fi
if has tips; then
  echo "== tips: our string's ends against the game's"
  out_again; w "bow sdraw on" 0.4
  for k in ${TIPS_GRID:-0:0 20:0 35:0 50:0}; do
    w "bow stips ${k%%:*} ${k##*:}" 0.5
    t "$TURN_L" "$REST_R"; sleep 1.2; d "tips-$k-L"
    t "hand L head -8 -15 55 90 0 30" "$REST_R"; sleep 1.2; d "tips-$k-L30"
    t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2; d "tips-$k-S"
    t "$TURN_R" "$REST_R"; sleep 1.2; d "tips-$k-R"
  done
  w "bow stips ${TIPS_PICK:-35 0}" 0.5
  take; ex "nocked" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.4; d tips-nocked
  ex "full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.6; d tips-drawn
  t "$TURN_L" "$DRAWN"; sleep 1.2; d tips-drawn-L
  t "$TURN_R" "$DRAWN"; sleep 1.2; d tips-drawn-R
  ex "the shot" '^BOW .* release: ' 3 "btn R trigger 0"; sleep 1.5; out_again
  t "$TURN_R" "$REST_R"; sleep 1.2; d tips-rest-R
  t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2; d tips-rest-STRAIGHT
fi
if has tipsview; then
  echo "== tipsview: the bow held low (its upper limb's tip at eye height), side-on, our ends vs the game's limb tips"
  out_again; w "bow sdraw on" 0.4; w "bow srest ${SREST:-ours}" 0.4
  for k in ${TIPS_GRID:-35:0 40:18}; do
    w "bow stips ${k%%:*} ${k##*:}" 0.5
    for yw in 90 -90; do
      t "hand L head -10 -75 40 90 0 $yw" "$REST_R"; sleep 1.2; d "tv-$k-$yw"
    done
    t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2; d "tv-$k-S"
  done
  w "bow stips ${TIPS_PICK:-35 0}" 0.5
fi
if has tipslo; then
  echo "== tipslo: the bow held high (its lower limb's tip at eye height), side-on, the lower end alone (bow stipslo)"
  out_again; w "bow sdraw on" 0.4; w "bow srest ours" 0.4; w "bow stips ${TIPS_PICK:-27 28}" 0.4
  for k in ${TIPSLO_GRID:-30:27}; do
    w "bow stipslo ${k%%:*} ${k##*:}" 0.5
    for yw in 90 -90; do
      t "hand L head -10 55 40 90 0 $yw" "$REST_R"; sleep 1.2; d "tl-$k-$yw"
    done
  done
  w "bow stipslo ${TIPSLO_PICK:-21 23}" 0.5
  t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2; d "tl-pick-S"
  t "$TURN_L" "$REST_R"; sleep 1.2; d "tl-pick-L"
fi
if has ba; then
  echo "== ba: the bow at rest as build 18 left it (drawn, the tip nodes' midpoint, 35 along) vs build 19's env, three sides"
  out_again
  for m in b18 b19; do
    if [ $m = b18 ]; then w "bow sdraw drawn" 0.3; w "bow srest tips" 0.3; w "bow stipslo off" 0.3; w "bow stips 35 0" 0.5
    else w "bow sdraw on" 0.3; w "bow srest ours" 0.3; w "bow stipslo ${TIPSLO_PICK:-21 23}" 0.3; w "bow stips ${TIPS_PICK:-27 28}" 0.5; fi
    for p in STRAIGHT L R; do
      case $p in STRAIGHT) P="$BOW_STRAIGHT" ;; L) P="$TURN_L" ;; R) P="$TURN_R" ;; esac
      t "$P" "$REST_R"; sleep 1.2; d "ba-$m-$p"
    done
  done
fi
if has touch; then
  echo "== touch: the arrow brought to the string seen at rest (33 cm behind the riser), from the side; build 18's zone vs build 19's"
  out_again; w "bow sdraw on" 0.3
  AT_STRING="hand R head -6 -15 22 90 0 0 s 0.7"
  for m in tips ours; do
    w "bow srest $m" 0.5
    take "touch ($m): taken"
    t "$BOW_STRAIGHT" "hand R head 24 -15 22 90 0 0 s 0.6"; sleep 0.8   # beside the string, outside either zone
    n0=$(cnt '^BOW .* nock: '); t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.8; d "touch-$m-at-string"
    seen "touch ($m): the arrow to the string nocks" '^BOW .* nock: ' 2 "$n0"
    grep -E '^BOW .* nock: ' "$L" | tail -1 | cut -c1-200
    ex "touch ($m): let go at the string: it rests" '^BOW .* (let down|quiver: let go)' 3 "btn R trigger 0"
    grep -E '^BOW .* (let down|quiver: let go)' "$L" | tail -1 | cut -c1-200
    d "touch-$m-rest"
    n0=$(cnt '^BOW .* nock: '); t "btn R trigger 1"; seen "touch ($m): the resting arrow drawn again at the string" '^BOW .* nock: ' 2 "$n0"
    t "btn R trigger 0"; sleep 0.6
    bow_back "touch ($m): put back"; out_again
  done
fi
if has abtn; then
  echo "== abtn: A pressed with the bow out in a fight (every whole-fist grab of the 17b session pressed it)"
  w "practice spawn" 1; waitfor "^PRACTICE t=.* spawn check 1" 8; sleep 2
  out_again; t "$BOW_STRAIGHT" "$REST_R"
  for ab in buzz quiet; do
    w "bow abtn $ab" 0.4
    for i in 1 2 3 4 5 6 7 8 9 10 11 12; do t "btn R a 1"; t "btn R a 0"; sleep 0.4; done
    echo "A with abtn $ab: $(grep -cE '^DODGE .*A: refused: the bow is out, a double buzz' "$L") buzzed, $(grep -cE '^DODGE .*A: refused: the bow is out \(quiet' "$L") quiet, $(grep -cE '^DODGE .*A: refused: no fight' "$L") no fight (totals)"
  done
  w "practice remove" 1
fi
if has grab; then
  echo "== grab: the trigger pressed outside the quiver, then held into it (the 17b session's misses)"
  out_again; w "bow sdraw ${SDRAW:-on}" 0.4
  t "$BOW_STRAIGHT" "hand R head 30 12 12 90 0 0"; sleep 0.6
  n0=$(cnt 'quiver: an arrow taken'); t "btn R trigger 1"; sleep 0.5
  t "hand R head 14 -6 -16 90 0 0"; seen "held into the quiver: taken" 'quiver: an arrow taken' 2 "$n0"
  t "btn R trigger 0"; sleep 0.6
  echo "== grab: high over the shoulder (22 cm above the build-18 slot's middle)"
  t "hand R head 15 12 -12 90 0 0"; sleep 0.6
  ex "high over the shoulder: taken" 'quiver: an arrow taken' 2 "btn R trigger 1"
  t "btn R trigger 0"; sleep 0.6
  echo "== grab: far down the back (the 17b session's second miss: +17 right -19 up -41 ahead of the head)"
  t "hand R head 17 -18 -41 90 0 0"; sleep 0.6
  ex "far down the back: taken" 'quiver: an arrow taken' 2 "btn R trigger 1"
  t "btn R trigger 0"; sleep 0.6
  echo "== grab: an empty grab 32 cm from the nock in front (the 17b session's first miss): the no-arrow cue"
  t "$BOW_STRAIGHT" "hand R head 8 -40 40 90 0 0"; sleep 0.6
  ex "an empty grab beside the bow: told" 'no arrow to nock|miss: ' 2 "btn R trigger 1"
  grep -E '^BOW .*(no arrow to nock|miss: )' "$L" | tail -1 | cut -c1-200
  t "btn R trigger 0"; sleep 0.6
  echo "== grab: the trigger held 1.5 s outside, then into the quiver"
  t "hand R head 30 12 12 90 0 0"; sleep 0.4
  n0=$(cnt 'quiver: an arrow taken'); t "btn R trigger 1"; sleep 1.5
  t "hand R head 14 -6 -16 90 0 0"; seen "held 1.5 s, then into the quiver: taken" 'quiver: an arrow taken' 2 "$n0"
  t "btn R trigger 0"; sleep 0.6
  grep -E '^BOW .*quiver: (an arrow taken|let go)' "$L" | tail -4 | cut -c1-200
fi
if has loop; then
  echo "== loop: take over the shoulder, carry, nock, draw, release (pictures at each)"
  out_again; w "bow sdraw ${SDRAW:-on}" 0.4
  t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2; d loop-0-rest
  t "hand R head 16 4 -17 90 0 0"; sleep 0.5
  ex "loop: taken" 'quiver: an arrow taken' 3 "btn R trigger 1"
  n0=$(cnt '^BOW .* nock: ')   # (build 19's zone reaches the string seen at rest: the carry may nock already)
  t "$BOW_STRAIGHT" "$CARRY"; sleep 1.2; d loop-1-carry
  t "$BOW_STRAIGHT" "hand R head 8 -12 22 90 0 0 s 0.5"; sleep 1.2; d loop-2-approach
  t "$BOW_STRAIGHT" "$AT_NOCK"; seen "loop: nocked (on the way or at the nock)" '^BOW .* nock: ' 3 "$n0"; sleep 0.6
  d loop-3-nocked
  t "$BOW_STRAIGHT" "$HALF"; sleep 1.2; d loop-4-half
  ex "loop: full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.8; d loop-5-drawn
  t "$TURN_L" "$DRAWN"; sleep 1.2; d loop-5-drawn-L
  # the user's own full draw in the 17b session (HAND frame 9495: the string hand 20 right, 25 down, 0 ahead of the head;
  # the hands 67 cm apart; both fists upright, BOW axes)
  t "hand L head -18 -32 60 90 0 0" "hand R head 19 -25 0 90 0 0 s 0.8"; sleep 1.2; d loop-5-drawn-real
  t "hand L head -18 -32 60 90 0 0" "hand R head 6 -20 30 90 0 0 s 0.7"; sleep 1.2; d loop-5-half-real
  t "$BOW_STRAIGHT" "$DRAWN"; sleep 0.8
  ex "loop: the shot" '^BOW .* release: ' 3 "btn R trigger 0"; d loop-6-released
  sleep 0.3; d loop-7-after
  t "$BOW_STRAIGHT" "$REST_R"; sleep 1.5; d loop-8-rest-after
  # the arrow let down onto the string (nocked, let go with no draw): where it rests
  take "loop: the next arrow"; ex "loop: nocked again" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.4
  ex "loop: let down" '^BOW .* let down' 3 "btn R trigger 0"; t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2; d loop-9-resting
  t "$TURN_L" "$REST_R"; sleep 1.2; d loop-9-resting-L
fi
if has hold; then
  echo "== hold: the arrow in the hand, old vs pinch"
  out_again
  for ah in old pinch; do
    w "bow ahold $ah" 0.4
    grep -E '^CONFIG bow arrow_hold' "$L" | tail -1 | cut -c1-200
    take "taken ($ah)"
    t "$BOW_STRAIGHT" "$CARRY"; sleep 1.2; d "hold-$ah-a"
    t "$BOW_STRAIGHT" "$CARRY2"; sleep 1.2; d "hold-$ah-b"
    t "hand R head 20 -10 30 90 0 -60 s 0.4"; sleep 1.2; d "hold-$ah-c"
    ex "let go ($ah)" 'quiver: let go before the nock' 3 "btn R trigger 0"; sleep 0.5
  done
fi
echo "steps done $(date +%T)"
grep -E '^BOW END' "$L" | cut -c1-200
grep -E 'xr: hands: .*a string [0-9]+' "$R/xr-layer.log" | tail -1 | grep -o 'an arrow [0-9]*, a string [0-9]*'
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
