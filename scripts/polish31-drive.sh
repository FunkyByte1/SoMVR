#!/bin/bash
# polish31-drive.sh - build 31's polish round (docs/re-xr-pipeline.md 136, som-b31-polish). Boot with
# scripts/headset-build26.env (+ the som-b31-polish block), SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt and
# SOMVR_XR_DUMP_LOW=3. In the practice meadow after the arrival, P31_STEPS (default all of them, in this order):
#   low    `practice hurt 20`: low health's red ring and its heartbeat (the layer's dumps at the ring's first frame and
#          two lubs), then `practice health`: off
#   bow    the bow out (the layer's test hands), an arrow drawn to the cheek (no cap), then 40 cm past the ear (the cap
#          holds the arrow), let down, the bow away
#   place  `practice place`: the sword hand aimed at the ground ahead (the gold ring, a dump), the trigger: an unaware
#          orc placed there, a dump, its watch
#   palm   the sword drawn, the left palm toward the eyes while the sword hand swings: the gesture waits, then opens
#   test   the panel's Test tab, Next, the Practice tab, the Test tab again: the page kept
#   fight  an infantry orc: its blows at a still Talion, the body judge's lines; a dump at a wind-up (a prompt?)
# P31_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${P31_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${P31_STEPS:-low bow place palm test fight}
T0=$(date +%s)
R=${P31_RUN:-}
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
waitfor() { local n0 i; n0=$(cnt "$1" "${3:-$L}"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1" "${3:-$L}")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/p31-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/p31-steps.txt"; }
d() { x "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/p31-steps.txt"; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
grep -E '^(WARN|CONFIG (hurt|vrmenu gate|defence body))' "$L" | cut -c1-240

if has low; then
  echo "== low: low health's ring"
  for hp in ${P31_HP:-20}; do
    n0=$(lines); w "practice hurt $hp" 1
    [ -n "$(since "$n0" 'red ring on|the ring: how low')" ] || waitfor 'red ring on' 6
    pass "health $hp: $(since "$n0" '^FIGHT low' | tail -1 | cut -c1-200)"
    sleep 4
  done
  echo "layer: $(grep -E "low health's ring (on|off)|low health's ring at frame" "$X" | tail -5 | cut -c1-200)"
  n0=$(lines); w "practice health" 1.5
  sleep 1; [ -n "$(since "$n0" 'red ring off')" ] && pass "the ring off" || fail "the ring stays"
fi

BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
OVER="hand R head 14 -8 -42 90 0 0"
if has bow; then
  echo "== bow: drawn to the cheek, then past the ear"
  w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
  x "$BOW_SLOT" "$R_REST"; sleep 0.3; x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1
  grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | cut -c1-160
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.4
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  n0=$(lines); x "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.5
  [ -z "$(since "$n0" 'the drawn arrow held')" ] && pass "drawn to the cheek: no cap ($(since "$n0" '^BOW t=.* last 1 s' | tail -1 | grep -o 'draw [0-9.]* cm' | head -1))" \
    || fail "capped at the cheek: $(since "$n0" 'the drawn arrow held' | tail -1 | cut -c1-200)"
  d bow-cheek
  n0=$(lines); x "$BOW_STRAIGHT" "$OVER s 0.6" "btn L squeeze 1" "btn R trigger 1"; sleep 1.5
  l=$(since "$n0" 'the drawn arrow held in front' | tail -1)
  [ -n "$l" ] && pass "past the ear: $(echo "$l" | cut -c1-260)" || fail "no cap past the ear"
  d bow-over
  x "$BOW_STRAIGHT" "$AT_STRING s 0.5" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 0"; sleep 0.8
  x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; x "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1
  x "hand L off" "hand R off"; sleep 1
fi

if has place; then
  echo "== place: Practice > Place orc"
  w "practice remove" 1.5
  for rx in -20 20 -35; do
    x "hand R head 18 -25 30 $rx 0 0"; sleep 0.5
    n0=$(lines); w "practice place" 1.2
    l=$(since "$n0" 'place: the spot' | tail -1)
    [ -n "$l" ] && { pass "aimed (RX $rx): $(echo "$l" | cut -c1-200)"; break; }
    echo "RX $rx: $(since "$n0" '^PRACTICE t=.* place' | tail -2 | cut -c1-200 | tr '\n' ' ')"; w "practice place" 0.8
  done
  d place-ring
  n0=$(lines); x "hand R head 18 -25 30 $rx 0 0" "btn R trigger 1"; sleep 1.5; x "hand R head 18 -25 30 $rx 0 0" "btn R trigger 0"
  l=$(since "$n0" '^PRACTICE t=.* spawn placed' | tail -1)
  echo "$l" | grep -q ' ok: ' && pass "placed: $(echo "$l" | cut -c1-260)" || fail "not placed: $l $(since "$n0" 'place:' | tail -1 | cut -c1-200)"
  sleep 4; d place-orc
  echo "watch: $(since "$n0" 'far orc \+' | tail -2 | cut -c1-200 | tr '\n' ' ')"
  w "practice remove" 1.5
  x "hand R off"; sleep 0.5
fi

SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"; REST="hands test R 28 -40 20 0 0 0"
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"
opens() { grep -c 'vrmenu: opened by' "$X" 2>/dev/null; }
if has palm; then
  echo "== palm: the left palm up while the sword hand swings"
  w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test L -25 -45 15 0 0 0"; w "$REST" 1
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.8
  grep -E '^SLOTS .*sword: ' "$L" | tail -1 | cut -c1-160
  w "hands test L off" 0.3
  x "$PALM_AWAY"; sleep 0.8
  o=$(opens); nx=$(lines "$X"); n0=$(lines)
  w "hands test R 40 -8 35 0 -90 90" 0.3
  TS=0.1 x "$PALM s 0.3"
  for k in 1 2 3; do w "hands swing R -20 -18 62 0 90 90 300" 0.45; w "hands swing R 40 -8 35 0 -90 90 300" 0.45; done
  ts=$(date +%s.%N)
  sleep 2.5
  te=$(grep 'vrmenu: opened by the palm' "$X" | tail -1)
  echo "gate: $(since "$n0" '^VRMENU gate' | head -4 | cut -c1-120 | tr '\n' ' ')"
  echo "layer: $(since "$nx" 'palm gesture (waits|held off)|opened by' "$X" | head -4 | cut -c1-160 | tr '\n' ' ')"
  [ "$(opens)" -gt "$o" ] && [ -n "$(since "$nx" 'palm gesture waits' "$X")" ] && [ -z "$(since "$nx" 'palm gesture held off' "$X")" ] \
    && pass "the palm waited for the swings, then opened: $(echo "$te" | cut -c1-120)" || fail "the palm: opens $(( $(opens) - o ))"
  d palm-open
fi

if has test; then
  echo "== test: the Test page keeps its page"
  [ "$(cnt 'vrmenu: opened by|vrmenu: closed' "$X")" -gt 0 ] && grep -E 'vrmenu: (opened|closed)' "$X" | tail -1 | grep -q opened || { x "$PALM s 0.3"; sleep 1.5; }
  w "hands test R off" 0.3
  press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
            x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
  nx=$(lines "$X")
  press 0.90 0.042; press 0.82 0.927; d test-page2
  press 0.418 0.042; press 0.90 0.042; d test-again
  echo "clicks: $(since "$nx" 'vrmenu: click' "$X" | cut -c1-120 | tr '\n' ' ')"
  since "$nx" 'click Test button: the test page \(page 2 of' "$X" | grep -q . && pass "the Test tab again at page 2" || fail "the Test page went back"
  x "hand R off" "$PALM_AWAY"; sleep 1; x "hand L off"; sleep 1
fi

if has fight; then
  echo "== fight: an infantry orc's blows at a still Talion, the body judge"
  w "hands test R 28 -40 20 0 0 0" 0.5
  n0=$(lines); w "practice spawn infantry" 2
  waitfor '^CROWD start' 15 && { sleep 0.45; d fight-windup; }
  sleep 12
  echo "body: $(since "$n0" '^DEFENCE body' | cut -c1-230)"
  echo "verdicts: $(since "$n0" '^DEFENCE (hit|dodge|block)' | cut -c1-140)"
  [ -n "$(since "$n0" '^DEFENCE body')" ] && pass "the body judge: $(since "$n0" '^DEFENCE body' | wc -l) attacks" || fail "no body lines"
  w "practice remove" 1.5; w "practice health" 1
fi
echo "== done $(date +%T)"; cat "$R/p31-steps.txt"
echo quit >> "$R/cmd"
