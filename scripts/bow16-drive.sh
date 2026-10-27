#!/bin/bash
# bow16-drive.sh - build 16's bow (handoff/HANDOFF-bow.md "build 16", docs/re-xr-pipeline.md 51), headless at the
# practice arrival. Boot (sandbox off) with scripts/headset-build16.env's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build16.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (B16_WT = a word of the launch command, default this worktree's name). The hands are
# the layer's test controllers (RUN/xrtest.txt: `hand L|R head X Y Z RX RY RZ [s S]`, `btn ...`), posed the way a real
# hand holds a bow: `90 0 0` = the fist upright (the grip's -Z, its tube, up; +Y back along the arm), not the sword pose
# (`0 0 0`: the tube level and ahead) builds 11-15b were tested with. Stops at once (quit) when the user goes into VR.
# B16_STEPS = any of (in this order; arrowid last):
#   hold    the bow from the left shoulder; `bow hold auto` (builds 11-15b) then `bow hold fist` with the fist upright
#           (pictures auto-straight, fist-straight); then fist canted, low and high (pictures)
#   quiver  an arrow from the right shoulder (picture arrow-in-hand), nocked by touching the string (nocked), drawn
#           (drawn), let go of the trigger = the shot (dumps right after: the arrow in flight); an arrow let go before
#           the nock (gone, no shot); a let-down (the arrow rests on the string) drawn again and shot; the quiver empty
#           (`bow arrows -99`: nothing, a buzz; the arrows given back); a press at the string with no arrow; the bow
#           put back at the left shoulder. PASS/FAIL lines.
#   sound   (boot with SOMVR_BOW_SNDLOG=1) a stock shot (`bow fire 125`: the game's own fire in its aim mode) and one of
#           ours, the game's sound events logged around both (BOWSND lines)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B16_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B16_STEPS:-hold quiver}
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
      echo "$1 $f" | tee -a "$R/b16-steps.txt"; }
REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
rest() { t "$REST_L" "$REST_R"; }
SLOT_L="hand L head -12 -10 -15 90 0 0"      # the bow's slot behind the left shoulder (SOMVR_BOW_DRAW=slot)
QUIVER="hand R head 12 -10 -15 90 0 0"       # the quiver behind the right shoulder (SOMVR_BOW_QUIVER=on)
# the bow held out ahead, the fist upright; canted (rolled 20 deg about the arm), low (the arm 30 deg down), high
BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
BOW_CANT="hand L head -8 -15 55 90 20 0"
BOW_LOW="hand L head -8 -40 48 60 0 0"
BOW_HIGH="hand L head -8 12 48 120 0 0"
# the string hand: at the nock of the straight bow (the nock 4 cm up its tube, 1.7 cm back, 1.2 cm left: about -9 -11 53),
# drawn to the cheek (57 cm), a short draw (14 cm), carrying an arrow ahead of the chest, near the bow but not at its nock
AT_NOCK="hand R head -4 -11 47 90 0 0"
DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
SHORT="hand R head -6 -11 40 90 0 0 s 0.3"
CARRY="hand R head 14 -22 32 60 0 0 s 0.5"
NEARBY="hand R head 10 -14 28 90 0 0 s 0.4"
bow_out() { t "$SLOT_L"; sleep 0.4; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1; }
take() { t "$QUIVER"; sleep 0.5; ex "${1:-an arrow taken}" 'quiver: an arrow taken' 3 "btn R trigger 1"; }
grep -E '^(WARN|CONFIG bow)' "$L" | head -8 | cut -c1-220
# 0. the controls card away (the right grip, else A), the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
m a00-start 0.5
if has hold; then
  echo "== the hold: builds 11-15b's auto, then fist, the fist upright"
  bow_out
  c0=$(cnt '^BOW hold check'); exw "auto measured" '^BOW hold t=.*HOLD=auto' 6 "bow hold auto"
  seen "auto check" '^BOW hold check' 3 "$c0"
  sleep 0.5; d auto-straight; m auto-straight 0.2
  c0=$(cnt '^BOW hold check'); exw "fist measured" '^BOW hold t=.*HOLD=fist' 6 "bow hold fist"
  seen "fist check" '^BOW hold check' 3 "$c0"; seen "the nock on the live string (since the first hold)" '^BOW nock t=' 3 0
  sleep 0.5; d fist-straight; m fist-straight 0.2
  for p in CANT LOW HIGH; do eval "t \"\$BOW_$p\""; sleep 1.2; d "fist-$p"; done
  t "$BOW_STRAIGHT"; sleep 1.2
  grep -E '^BOW axes' "$L" | tail -1 | cut -c1-260
fi
if has quiver; then
  echo "== the quiver: an arrow from the right shoulder, nock, draw, let go of RT"
  grep -q '^BOW .* enter ' "$L" && [ "$(grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -c 'enter ')" = 1 ] || bow_out
  a0=$(grep -E '^BOW t=.* last 1 s' "$L" | tail -1 | grep -o 'arrows [-0-9]*' | grep -o '[-0-9]*$')
  echo "arrows before: $a0"
  take
  t "$CARRY"; sleep 0.8; d arrow-in-hand
  ex "nocked by touching the string" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.4; d nocked
  ex "full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.6; d drawn; m drawn 0.1; w "bow flight 8" 0.3
  f0=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | wc -l); r0=$(cnt '^BOW .* release: ')
  # (the layer reads the test file every 5th frame, 0.25 s headless: a file replaced sooner is lost - one step per
  # 0.35 s; the first shot's arrow drawn at 8 m/s (`bow flight 8`) so the pictures catch it, 70 m/s is 3.5 m a frame)
  T_S=0.35 t "btn R trigger 0" "dump"; for k in 1 2 3; do T_S=0.35 t "dump"; done; w "bow flight 70" 0.3
  # (a dump stalls the headless frames: the arms lose the hands - `ARMS ... nohand` - and a 0.3 s gap ends the bow as
  # lost (run 023259); then out again from the shoulder)
  if grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'exit: lost'; then
    echo "$(date +%T) INFO the dumps' stall ended the bow ($(grep -E '^BOW .* exit: ' "$L" | tail -1 | cut -c1-60)): out again"
    bow_out
  fi
  seen "the release (let go of RT)" '^BOW .* release: ' 1 "$r0"
  sleep 1.5
  grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -n +$((f0 + 1)) | grep -o '[0-9]*$' | sed 's/^/flight /' | tee -a "$R/b16-steps.txt"
  grep -E '^BOW (t=.* (shot|fire|arrow)|.*our arrow)' "$L" | tail -4 | cut -c1-200
  echo "-- let go before the nock"
  sleep 1; n0=$(cnt '^BOW .* release: ')
  take "taken again"
  t "$NEARBY"; sleep 0.6; ex "gone before the nock" 'quiver: let go before the nock' 3 "btn R trigger 0"
  [ "$(cnt '^BOW .* release: ')" -eq "$n0" ] && echo "$(date +%T) PASS no shot" || echo "$(date +%T) FAIL a shot"
  echo "-- a let-down: the arrow rests on the string, drawn again, shot"
  take "taken (3)"
  ex "nocked (3)" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"
  t "$SHORT"; sleep 0.6; ex "let down, resting" 'let down: .*rests on the string' 3 "btn R trigger 0"
  t "$NEARBY"; sleep 0.8; d resting
  t "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.5; ex "the resting arrow nocked again" '^BOW .* nock: ' 3 "btn R trigger 1"
  ex "full draw (3)" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.5
  ex "the second shot" '^BOW .* release: ' 3 "btn R trigger 0"
  sleep 1.2
  echo "-- the quiver empty"
  w "bow arrows -99"; sleep 0.5
  grep -E '^BOW t=.* arrows: ' "$L" | tail -1 | cut -c1-140
  t "$QUIVER"; sleep 0.5; ex "empty: nothing" 'quiver: empty' 3 "btn R trigger 1"
  t "btn R trigger 0"; sleep 0.3
  echo "-- a press at the string with no arrow"
  t "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.5; ex "no arrow to nock" 'no arrow to nock' 3 "btn R trigger 1"; t "btn R trigger 0"
  w "bow arrows 8"; sleep 0.5
  grep -E '^BOW t=.* arrows: ' "$L" | tail -1 | cut -c1-140
  if has sound; then
    echo "-- the sounds: a stock shot (the fire command 125), then ours"
    for f in 125 ours; do
      w "bow fire $f"; sleep 0.5
      take "taken ($f)"; ex "nocked ($f)" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"
      ex "full draw ($f)" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.5
      ex "the release ($f)" '^BOW .* release: ' 3 "btn R trigger 0"; sleep 2.5
    done
    grep -E '^BOW .* fire: ' "$L" | tail -2 | cut -c1-160
    grep -E '^BOWSND' "$L" | tail -30 | cut -c1-160
  fi
  echo "-- the bow put back at the left shoulder"
  t "$REST_R"; t "$SLOT_L"; sleep 0.5; ex "put back" '^BOW .* exit: slot' 3 "btn L squeeze 0"
  rest; sleep 1
fi
if has garrow; then
  echo "== garrow: the game's arrow by our quiver (SOMVR_BOW_GAME_ARROW=on): none taken, in the hand, nocked, drawn, after the shot, resting"
  out_again() { grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'exit: ' && bow_out; }
  grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter ' || bow_out
  HOLD_R="hand R head 14 -22 32 60 0 0"
  w "bow garrow game" 0.6; t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d ga-game-none
  w "bow garrow auto" 0.6; t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d ga-none; out_again
  take "taken (ga)"; t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d ga-hand; out_again
  ex "nocked (ga)" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"; sleep 0.6; d ga-nocked; out_again
  ex "full draw (ga)" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.8; d ga-drawn
  w "bow flight 4" 0.4; r0=$(cnt '^BOW .* release: ')
  T_S=0.3 t "btn R trigger 0"; T_S=0.35 t "dump"; T_S=0.35 t "dump"; sleep 1.5; w "bow flight 70" 0.3
  seen "the release (ga)" '^BOW .* release: ' 1 "$r0"; out_again
  t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d ga-after
  take "taken (ga 2)"; ex "nocked (ga 2)" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"
  t "$SHORT"; sleep 0.6; ex "let down (ga)" 'let down: .*rests on the string' 3 "btn R trigger 0"
  t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d ga-resting
  grep -E '^BOW t=.* game arrow' "$L" | tail -8 | cut -c1-120
fi
if has arrowid; then
  echo "== arrowid: the game's arrow in the right hand (the inventory; the mode's FX skipped one by one, SOMVR_BOW_FX=game)"
  out_again() { grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'exit: ' && bow_out; }
  grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter ' || bow_out
  t "$BOW_STRAIGHT" "hand R head 14 -22 32 60 0 0"; sleep 1.2; d id-none; w "bow inv" 1
  take; t "hand R head 14 -22 32 60 0 0"; sleep 1.2; d id-hand; out_again
  w "bow inv" 1
  t "btn R trigger 0"; sleep 0.5
  for fx in nofxalt nofx none game; do
    t "$SLOT_L"; sleep 0.5; ex "put back ($fx)" '^BOW .* exit: ' 3 "btn L squeeze 0"; sleep 0.8
    w "bow fx $fx" 0.5; bow_out
    t "$BOW_STRAIGHT" "hand R head 14 -22 32 60 0 0"; sleep 1.5; d "id-fx-$fx"; out_again
  done
  grep -E '^BOW inv' "$L" | tail -16 | cut -c1-400
fi
echo "steps done $(date +%T)"
grep -E '^BOW END' "$L" | cut -c1-260
grep -E 'xr: hands: frame .*arrow' "$R/xr-layer.log" | head -4
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
