#!/bin/bash
# stealth19-drive.sh - the walk-up for the stealth kill (handoff/HANDOFF-stealth19.md, docs/re-xr-pipeline.md 72): drives one
# headless boot (scripts/headless_vr.sh with scripts/headset-build19.env, SOMVR_HANDS_TEST=1, the layer's test file).
# The far unaware orc (practice far: 18 m ahead, its back to Talion) each round; `stealth watch HZ` (SD_WATCH, 5) logs
# its awareness and Talion's state. SD_STEPS (default "sneak dsneak stand front run"), in order:
#   dump    the AI records (STLREC lines: the player's and the nearest orc's senses)
#   placec  crouched, put behind it at 125, 118, 112, 106, 100, 94 cm (verbs behind), 3.5 s still at each, until it notices
#   places  the same standing, 170 .. 100 cm
#   sneak   the proof: crouched, the test stick (0.6, ~140 cm/s) straight at its back from 18 m, asked on to 60 cm: the
#           stand-off holds Talion, 3 s still, then the sword's thrust into its back (the stealth kill); pictures
#   dsneak  the same with Acharn from the left hip
#   grab    the sneak, then the left hand reached up at its head and squeezed (SOMVR_VERBS' grab), then the sword's thrust
#   slow    the sneak's walk at stick 0.3, no blade
#   stand   standing: walked straight at it (stick 0.6) to 60 cm: it notices (the game's rule)
#   front   crouched, straight at a patrol orc 6 m ahead that faces Talion: it notices (no stand-off in front)
#   run     standing, stick 1.0 to 60 cm
# SD_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SD_STEPS:-sneak dsneak stand front run}
T0=$(date +%s)
R=${SD_RUN:-}
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
k() { echo "key $1" >> "$R/cmd"; echo "$(date +%T) key $1"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hand L head -22 -40 15 0 0 0"     # the layer's left hand at the hip (cm right/up/ahead of the head, deg)
LREACH="hand L head -3 -3 50 0 90 0"     # ... reached out ahead at an orc's head, the palm turned forward
LFACE="hand L head -3 -8 24 0 90 0"      # ... pulled in to the face (24 cm ahead of the eyes)
spawn() { w "practice spawn ${1:-infantry}" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED'; then
    echo "$(date +%T) the spawn failed: back to the practice area"; w "practice go" 6; w "practice spawn ${1:-infantry}" 3
  fi; }
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
DRAWN=0   # (the slot's squeeze toggles: draw once)
draw_slot() { [ "$DRAWN" = 1 ] && return 0; DRAWN=1; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
sv() { w "stealth status" 1.0; }
# a layer dump (the composed pictures: scripts/xr_compose.py RUN -f FRAME); the frame noted in RUN/sd-steps.txt
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/sd-steps.txt"; }
READY="hands test R 8 -18 -25 0 0 0"       # the sword hand drawn back by the chest, the blade forward, its tip short of
THRUST="hands swing R 8 -14 60 0 0 0 250"
READY2="hands test R 8 -18 -40 0 0 0"      # build 19: at the stand-off (118 cm) READY's tip touched its back: 15 cm further back   # the orc 1.3 m off (run 195528: at 15 cm ahead it touched the orc and turned it)
patrol() { w "verbs ahead $1" 0.3; w "verbs template Orc_Infantry_Patrol" 0.3; spawn infantry; w "verbs template off" 0.3; }
far() { w "practice far" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED\|refused'; then
    echo "$(date +%T) the far spawn failed: back to the practice area's start"; w "practice back" 6; w "practice go" 7; w "practice far" 3
  fi; }
dist() { w "execute where" 0.4 >&2; grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1 | grep -E 'the nearest 0x' | grep -oE 'dist [0-9]+' | cut -d' ' -f2; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
grep -E 'card: put away' "$R/xr-layer.log" | tail -1 | cut -c1-120
LREST="hands test L -25 -35 30 0 0 0"   # the left hand at rest
w "$LREST" 0.6
draw_slot; w "$REST" 0.6; w "front keep 0" 0.3
noticed() { [ "$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log")" -gt "$N0" ]; }
hold() { w "crouch hold 9000" 0.3; }
# the test stick toward the nearest orc within 40 m (stealth chase): $1 stick, $2 stop cm; waits for the stop or a notice
chase() { local n0 i; n0=$(cnt '^STEALTH t=.* chase: stopped'); w "stealth chase $1 $2" 0.3
  for i in $(seq 1 120); do userin && bail "the user is in VR"; [ "$(cnt '^STEALTH t=.* chase: stopped')" -gt "$n0" ] && return 0
    noticed && { echo "$(date +%T) noticed"; w "stealth chase 0" 0.3; return 1; }; [ $((i % 12)) = 0 ] && [ "$3" = 1 ] && hold; sleep 0.25; done
  w "stealth chase 0" 0.3; return 1; }
NR=0   # rounds after the first start from the practice area's start again (back, go): the same facing, the far spawn's ground
reset() { NR=$((NR + 1)); [ "$NR" -gt 1 ] && { w "practice back" 6; w "practice go" 7; }; }
round() { echo "== $1"; reset; N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); far; sv; m "$1-start" 0.1; }
endr() { sv; w "crouch stand" 0.4; w "practice remove" 2; }
# put Talion behind the far orc at each distance in turn ($1 crouched 1|0, then the cm list), 4 s still at each
place() { local c cr=$1; shift; [ "$cr" = 1 ] && hold
  for c in "$@"; do noticed && { echo "$(date +%T) noticed"; return 1; }
    [ "$cr" = 1 ] && hold; w "verbs behind $c" 0.8; w "execute face" 0.4; echo "$(date +%T) placed $c"; sleep 3.5; done
  noticed && { echo "$(date +%T) noticed"; return 1; }; return 0; }
w "stealth watch ${SD_WATCH:-5}" 0.5
# the proof's approach: crouched, the test stick at a sneaking pace straight at the far orc's back, asked to go on to
# 60 cm (into it): the stand-off must hold Talion outside its notice range; then 3 s still, then the blade
sneak_to() { local n0 i; n0=$(cnt '^STEALTH t=.* stand-off: '); hold; w "stealth chase $1 60" 0.3
  for i in $(seq 1 160); do userin && bail "the user is in VR"; [ "$(cnt '^STEALTH t=.* stand-off: ')" -gt "$n0" ] && break
    noticed && { echo "$(date +%T) noticed"; break; }; [ $((i % 12)) = 0 ] && hold; sleep 0.25; done
  echo "$(date +%T) $(grep -E '^STEALTH t=.* stand-off: ' "$R/somvr.log" | tail -1 | cut -c1-160)"
  sleep 1.5; hold; w "stealth chase 0" 0.3; sleep 2.5; }
for s in $STEPS; do
  case $s in
  dump) far; w "stealth dump" 4; w "practice remove" 2 ;;
  placec) round placec; place 1 125 118 112 106 100 94; m placec-end 0.1; endr ;;
  places) round places; place 0 170 155 140 130 120 110 100; m places-end 0.1; endr ;;
  sneak)
    round sneak; sneak_to 0.6; noticed && echo "$(date +%T) noticed before the blade"; hold; w "execute face" 0.3; w "$READY2" 0.5; dp sneak-approach
    w "$THRUST" 0.3; dp sneak-kill; m sneak-stab 0.01; w "$REST" 0.5; sleep 2.5; dp sneak-after; m sneak-after 0.1; endr ;;
  dsneak)
    round dsneak
    w "hands test L -18 -62 -5 0 0 0" 0.5; w "hands squeeze L 0" 1.2; w "hands squeeze L 1" 1.2
    w "hands test L -12 -20 32 0 0 0" 0.8; w "hands squeeze L 0" 0.6
    sneak_to 0.6; noticed && echo "$(date +%T) noticed before the blade"; w "execute face" 0.3; m dsneak-approach 0.1
    w "hands test L -8 -18 15 0 0 0" 0.5; w "hands swing L -8 -12 80 0 0 0 250" 0.3; dp dsneak-kill; m dsneak-stab 0.01; w "hands test L -12 -20 32 0 0 0" 0.5; sleep 3
    endr; w "$LREST" 0.5 ;;
  grab)
    round grab; sneak_to 0.6; noticed && echo "$(date +%T) noticed before the grab"; hold; w "execute face" 0.3; w "$READY2" 0.4
    w "hands test L -3 25 45 0 90 0" 0.6; w "hands squeeze L 1" 1.0; dp grab-held
    w "$THRUST" 0.3; dp grab-kill; m grab-stab 0.01; w "$REST" 0.5; w "hands squeeze L 0" 0.5; w "$LREST" 0.5
    sleep 2; endr ;;
  slow) round slow; sneak_to 0.3; noticed && echo "$(date +%T) noticed"; m slow-end 0.1; endr ;;
  stand) round stand; chase 0.6 60 0; w "stealth chase 0" 0.3; sleep 3; noticed && echo "$(date +%T) noticed (standing: the game's rule)"; m stand-end 0.1; endr ;;
  run) round run; chase 1.0 60 0; w "stealth chase 0" 0.3; sleep 3; noticed && echo "$(date +%T) noticed (running)"; m run-end 0.1; endr ;;
  front)
    echo "== front"; reset; N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); patrol 600; w "execute face" 0.3; sv
    hold; chase 0.6 60 1; w "stealth chase 0" 0.3; sleep 3; noticed && echo "$(date +%T) noticed (in front: the game's rule)"; m front-end 0.1; endr ;;
  esac
done
w "stealth watch off" 0.3
w "stealth status" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
