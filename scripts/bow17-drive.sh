#!/bin/bash
# bow17-drive.sh - build 17's bow round (handoff/HANDOFF-bow.md "build 17", docs/re-xr-pipeline.md 61), headless at the
# practice arrival; bow16-drive.sh's helpers and real bow-hold poses (`90 0 0` = the fist upright). Boot (sandbox off)
# with scripts/headset-build17.env's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build17.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (B17_WT = a word of the launch command, default this worktree's name). Stops at once
# (quit) when the user goes into VR. Pictures: RUN/b17-steps.txt (NAME FRAME), compose with scripts/xr_compose.py.
# B17_STEPS = any of (in this order):
#   zone     the nock's zone: an arrow carried 20 cm beside the nock - no nock with `bow zone point` (build 16), nocked
#            with `bow zone string` (picture), drawn and shot; nocked 21 cm beside and let go at once: resting, no shot;
#            carried 13 cm in front of the bow: no nock
#   auto     the VR menu's Arrows: Auto (`bow quiver off`, the menu17 row): RT closed 20 cm off the nock draws an arrow
#            there (the zone), drawn and shot; back to the quiver
#   cue      the empty hand at the string: `bow cue buzz` (NOARROW, the double buzz's HAP lines), `bow cue take` (taken
#            and nocked there, shot); the quiver empty: three knocks; take with none left: nothing
#   reticle  SOMVR_BOW_RETICLE=hide: the game's crosshair sets turned into hides (RETICLE lines), pictures hidden vs
#            `reticle game` (the bow out again for each)
#   hands    the hands at `handsize 1`, 1.15 and 1.3 (the layer's test line): the bow held, then the sword drawn and held
#   front    (run 2) an arrow carried in front of the bow from the side, never through the zone: no nock
#   sizes    (run 2) the hands at 1, 1.2 and 1.25: the bow held, the sword held (the same poses as hands)
#   vision   the wraith vision's look timed (the layer's `lumlog`): Y held 2.5 s with the record's ramps, then with
#            `vision ramp 150 150`, then the shortest press
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B17_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B17_STEPS:-zone auto cue reticle hands vision}
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
      echo "$1 $f" | tee -a "$R/b17-steps.txt"; }

REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
rest() { t "$REST_L" "$REST_R"; }
SLOT_L="hand L head -12 -10 -15 90 0 0"
QUIVER="hand R head 12 -10 -15 90 0 0"
BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_NOCK="hand R head -4 -11 47 90 0 0"
DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
NEAR20="hand R head 9 -12 44 90 0 0 s 0.4"     # an arrow 20 cm beside/behind the nock (build 16: no nock)
BESIDE="hand R head 12 -11 53 90 0 0 s 0.4"    # 21 cm beside it, level with it: no pull
FRONT="hand R head -9 -11 67 90 0 0 s 0.4"     # 13 cm in front of the bow
EMPTYAT="hand R head 5 -11 45 90 0 0 s 0.3"    # the empty hand pressed 15-18 cm off the nock
CARRY="hand R head 14 -22 32 60 0 0 s 0.5"
bow_out() { t "$SLOT_L"; sleep 0.4; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1; }
bow_back() { t "$REST_R"; t "$SLOT_L"; sleep 0.5; ex "${1:-put back}" '^BOW .* exit: ' 3 "btn L squeeze 0"; sleep 0.6; }
take() { t "$QUIVER"; sleep 0.5; ex "${1:-an arrow taken}" 'quiver: an arrow taken' 3 "btn R trigger 1"; }
isout() { grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter '; }
out_again() { isout || bow_out; }
grep -E '^(WARN|CONFIG bow (nock_zone|noarrow|reticle)|CONFIG vision)' "$L" | head -8 | cut -c1-220
grep -E 'xr: hands: the hand model' "$R/xr-layer.log" | tail -1 | cut -c1-200
# 0. the controls card away (the right grip, else A), the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
m a00-start 0.5
if has zone; then
  echo "== zone: the nock's zone"
  bow_out
  w "bow zone point" 0.5; take
  n0=$(cnt '^BOW .* nock: '); t "$BOW_STRAIGHT" "$NEAR20"; sleep 1.5
  [ "$(cnt '^BOW .* nock: ')" -eq "$n0" ] && echo "$(date +%T) PASS point: no nock 20 cm off" || echo "$(date +%T) FAIL point: nocked"
  exw "string: nocked there" 'nock: .*the zone' 3 "bow zone string"; sleep 0.4; d zone-nocked
  ex "full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.5
  ex "the shot" '^BOW .* release: ' 3 "btn R trigger 0"; sleep 1.5; out_again
  echo "-- nocked beside the bow and let go at once: resting, no shot"
  take "taken (2)"; ex "nocked beside" 'nock: .*the zone' 3 "$BOW_STRAIGHT" "$BESIDE"
  r0=$(cnt '^BOW .* release: ')
  ex "let go beside: resting" 'let down: .*rests on the string' 3 "btn R trigger 0"
  [ "$(cnt '^BOW .* release: ')" -eq "$r0" ] && echo "$(date +%T) PASS no shot" || echo "$(date +%T) FAIL a shot"
  t "$CARRY"; sleep 0.8; d zone-resting
  echo "-- the resting arrow drawn again from beside, shot"
  t "$BOW_STRAIGHT" "$BESIDE"; sleep 0.6; ex "drawn again" 'nock: .*the zone' 3 "btn R trigger 1"
  ex "full draw (2)" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.5; ex "the shot (2)" '^BOW .* release: ' 3 "btn R trigger 0"
  sleep 1.5; out_again
  echo "-- carried in front of the bow: no nock"
  take "taken (3)"; n0=$(cnt '^BOW .* nock: '); t "$BOW_STRAIGHT" "$FRONT"; sleep 1.5
  [ "$(cnt '^BOW .* nock: ')" -eq "$n0" ] && echo "$(date +%T) PASS front: no nock" || echo "$(date +%T) FAIL front: nocked"
  ex "let go in front: gone" 'quiver: let go before the nock' 3 "btn R trigger 0"
  grep -E '^BOW t=.* (nock|release|let down|quiver)' "$L" | tail -10 | cut -c1-200
fi
if has auto; then
  echo "== auto: the Arrows row's Auto with the zone"
  isout && bow_back "put back (auto)"
  w "bow quiver off" 0.5; bow_out; sleep 0.5
  t "$BOW_STRAIGHT" "$NEAR20"; sleep 0.8
  ex "auto: drawn 20 cm off the nock" 'nock: .*the zone' 3 "btn R trigger 1"
  ex "full draw (auto)" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.5; ex "the shot (auto)" '^BOW .* release: ' 3 "btn R trigger 0"
  sleep 1.5; isout && bow_back "put back (auto 2)"
  exw "the quiver again" 'quiver off -> on' 3 "bow quiver on"; bow_out
fi
if has cue; then
  echo "== cue: the empty hand at the string"
  out_again
  w "bow cue buzz" 0.4; t "$BOW_STRAIGHT" "$EMPTYAT"; sleep 0.6
  h0=$(grep -c '^HAP R .*amp=0.65' "$L")
  ex "buzz: no arrow" 'no arrow to nock .*double buzz' 3 "btn R trigger 1"; sleep 0.5; t "btn R trigger 0"; sleep 0.4
  echo "$(date +%T) INFO the double's HAP lines: $(( $(grep -c '^HAP R .*amp=0.65' "$L") - h0 ))"
  grep -E '^HAP (R|L) ' "$L" | tail -4 | cut -c1-100
  w "bow cue take" 0.4; sleep 0.3
  ex "take: nocked at the string" 'one taken and nocked there' 3 "btn R trigger 1"; sleep 0.3; d cue-take-nocked
  ex "full draw (take)" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.5; ex "the shot (take)" '^BOW .* release: ' 3 "btn R trigger 0"
  sleep 1.5; out_again
  echo "-- the quiver empty"
  w "bow arrows -99"; sleep 0.5
  t "$QUIVER"; sleep 0.5; h0=$(grep -c '^HAP R .*amp=0.70' "$L")
  ex "empty: three knocks" 'quiver: empty' 3 "btn R trigger 1"; sleep 0.5; t "btn R trigger 0"; sleep 0.3
  echo "$(date +%T) INFO the triple's HAP lines: $(( $(grep -c '^HAP R .*amp=0.70' "$L") - h0 ))"
  t "$BOW_STRAIGHT" "$EMPTYAT"; sleep 0.6; ex "take, none left: empty" 'quiver: empty' 3 "btn R trigger 1"; t "btn R trigger 0"
  w "bow arrows 8"; w "bow cue buzz" 0.4
fi
if has reticle; then
  echo "== reticle: the game's crosshair"
  out_again; sleep 1
  seen "the crosshair's sets hidden" 'RETICLE t=.*hidden \(SOMVR_BOW_RETICLE=hide' 2 0
  t "$BOW_STRAIGHT" "$CARRY"; sleep 1; d reticle-hide
  bow_back "put back (reticle)"; w "reticle game" 0.4; bow_out; sleep 1
  t "$BOW_STRAIGHT" "$CARRY"; sleep 1; d reticle-game
  bow_back "put back (reticle 2)"; w "reticle hide" 0.4; w "reticle state" 0.4
  grep -E '^RETICLE' "$L" | tail -6 | cut -c1-200
fi
if has hands; then
  echo "== hands: the sizes, the bow held, the sword held"
  out_again
  HOLD_R="hand R head 6 -20 40 90 0 0"
  for sz in 1 1.15 1.3; do t "handsize $sz"; t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d "hands-bow-$sz"; done
  bow_back "put back (hands)"; rest; sleep 0.5
  t "hand R head 10 -10 -15 0 0 0"; sleep 0.6; t "btn R squeeze 1"; sleep 0.8
  t "hand R head 18 -22 34 0 0 0 s 0.6"; sleep 1.5
  grep -E '^SLOTS .*sword' "$L" | tail -2 | cut -c1-160
  for sz in 1 1.15 1.3; do
    t "handsize $sz"; t "hand R head 14 -20 32 0 0 0"; sleep 1.2; d "hands-sword-$sz"
    t "hand R head 10 -16 28 0 -60 0"; sleep 1.2; d "hands-sword2-$sz"
  done
  t "handsize 1.15"; grep -E 'xr: hands: the hand model at size' "$R/xr-layer.log" | tail -3 | cut -c1-200
  t "hand R head 10 -10 -15 0 0 0 s 0.6"; sleep 1; t "btn R squeeze 0"; sleep 0.8; rest
fi
if has front; then
  echo "== front: carried in front of the bow from the side"
  out_again; take "taken (front)"
  t "$BOW_STRAIGHT" "hand R head 45 -11 72 90 0 0 s 0.5"; sleep 0.9
  n0=$(cnt '^BOW .* nock: '); t "$BOW_STRAIGHT" "hand R head -9 -11 67 90 0 0 s 0.6"; sleep 1.5
  [ "$(cnt '^BOW .* nock: ')" -eq "$n0" ] && echo "$(date +%T) PASS front: no nock" || echo "$(date +%T) FAIL front: nocked"
  d front-carried
  ex "let go in front: gone" 'quiver: let go before the nock' 3 "btn R trigger 0"
fi
if has sizes; then
  echo "== sizes: the hands at 1, 1.2, 1.25"
  out_again
  HOLD_R="hand R head 6 -20 40 90 0 0"
  for sz in 1 1.2 1.25; do t "handsize $sz"; t "$BOW_STRAIGHT" "$HOLD_R"; sleep 1.2; d "sizes-bow-$sz"; done
  bow_back "put back (sizes)"; rest; sleep 0.5
  t "hand R head 10 -10 -15 0 0 0"; sleep 0.6; t "btn R squeeze 1"; sleep 0.8
  t "hand R head 18 -22 34 0 0 0 s 0.6"; sleep 1.5
  for sz in 1 1.2 1.25; do
    t "handsize $sz"; t "hand R head 14 -20 32 0 0 0"; sleep 1.2; d "sizes-sword-$sz"
    t "hand R head 10 -16 28 0 -60 0"; sleep 1.2; d "sizes-sword2-$sz"
  done
  t "hand R head 10 -10 -15 0 0 0 s 0.6"; sleep 1; t "btn R squeeze 0"; sleep 0.8; rest
fi
if has vision; then
  echo "== vision: the look's ramps timed"
  isout && bow_back "put back (vision)"
  rest; sleep 1
  w "vision ramp" 1
  grep -E '^VISION t=.*the vision record' "$L" | tail -2 | cut -c1-220
  for pass in game fast; do
    [ "$pass" = fast ] && { w "vision ramp 150 150" 1; grep -E '^VISION t=.*the vision record' "$L" | tail -1 | cut -c1-220; }
    echo "VISIONPASS $pass $(date +%s.%N)" >> "$R/b17-steps.txt"
    t "lumlog 140"; w "vision y 2500" 0.2; sleep 7.5
    grep -E '^VISION t=' "$L" | tail -6 | cut -c1-160
  done
  echo "VISIONPASS short $(date +%s.%N)" >> "$R/b17-steps.txt"
  t "lumlog 100"; w "vision y 1" 0.2; sleep 5.5
  grep -E '^VISION t=' "$L" | tail -5 | cut -c1-160
fi
echo "steps done $(date +%T)"
grep -E '^(BOW END|RETICLE END|VISION END)' "$L" | cut -c1-260
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
