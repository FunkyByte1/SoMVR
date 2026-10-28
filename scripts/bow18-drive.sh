#!/bin/bash
# bow18-drive.sh - build 18's bow visuals (handoff/HANDOFF-bow.md "build 18", docs/re-xr-pipeline.md 61.10), headless at
# the practice arrival; bow17-drive.sh's helpers and real bow-hold poses. Boot (sandbox off) with
# scripts/headset-build18.env's switches (SOMVR_BOW_STRING_DRAW=on, SOMVR_BOW_ARROW_HOLD=pinch):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build18.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time. Pictures: RUN/b18-steps.txt (NAME FRAME), compose with scripts/xr_compose.py.
# B18_STEPS = any of: axes (which hand turns show the bow from its sides: BOW axes lines), string (the bow at rest,
#   nocked and drawn, straight and turned both ways, `bow sdraw off` vs on, then `drawn` - build 18's env), hold (the arrow from the quiver in the hand,
#   `bow ahold old` vs pinch, two hand angles), tips (run 2: our string's ends `bow stips 0|20|35|50` cm past the tip nodes
#   against the game's own string, seen from its visible side; then nocked and drawn at TIPS_PICK)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B18_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B18_STEPS:-axes string hold}
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
      echo "$1 $f" | tee -a "$R/b18-steps.txt"; }


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
