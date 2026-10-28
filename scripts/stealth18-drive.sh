#!/bin/bash
# stealth18-drive.sh - SOMVR_STEALTH_KILL's proof (handoff/HANDOFF-stealth.md, docs/re-xr-pipeline.md 67): drives one
# headless boot (scripts/headless_vr.sh with scripts/headset-build18.env, SOMVR_HANDS_TEST=1, the layer's test file).
# Prologue as verbs13-drive.sh's. SD_STEPS (default "back slow witness front aware"), in order:
#   sneak    the far unaware orc (practice far: 18 m ahead, its back to Talion), crouched (crouch hold), walked up with
#            the stick (execute where/face), the sword thrust into its back: the stealth kill; marks before / at / after
#   back     the far orc, crouched, Talion put 1.3 m behind it (verbs behind), the thrust into its back
#   slow     the same standing (no crouch): slow counts (SOMVR_STEALTH_KILL_STANCE=either)
#   witness  the far orc first (front keep 1: it stays), then a patrol orc 10 m ahead (verbs ahead/template), Talion
#            put behind that one (the far orc 7 m behind him, its back to him): the other's awareness 1 s / 3 s after
#   dagger   Acharn drawn from the left hip (the slot), the back round with the left hand's thrust
#   front    a patrol orc 2 m ahead (it faces Talion), the thrust into its front: the ordinary hit
#   aware    the practice infantry (it fights Talion), Talion put behind it, the thrust into its back: the ordinary hit
# SD_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SD_STEPS:-sneak back slow dagger witness front aware}
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
THRUST="hands swing R 8 -14 60 0 0 0 250"   # the orc 1.3 m off (run 195528: at 15 cm ahead it touched the orc and turned it)
patrol() { w "verbs ahead $1" 0.3; w "verbs template Orc_Infantry_Patrol" 0.3; spawn infantry; w "verbs template off" 0.3; }
far() { w "practice far" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED\|refused'; then
    echo "$(date +%T) the far spawn failed: back to the practice area"; w "practice go" 6; w "practice far" 3
  fi; }
dist() { w "execute where" 0.4 >&2; grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1 | grep -E 'the nearest 0x' | grep -oE 'dist [0-9]+' | cut -d' ' -f2; }
back_round() { # $1 name, $2 crouch 1|0, $3 the orc (far|patrol)
  if [ "${3:-far}" = patrol ]; then patrol 1000; else far; fi; sv
  [ "$2" = 1 ] && w "crouch hold 9000" 0.6
  w "verbs behind 130" 0.8; w "execute face" 0.6; sv; dp "$1-approach"; m "$1-approach" 0.1
  w "$READY" 0.8; w "$THRUST" 0.3; dp "$1-kill"; m "$1-stab" 0.01; w "$REST" 0.5; sleep 2.5; dp "$1-after"; m "$1-after" 0.1; sv
  w "crouch stand" 0.4; w "practice remove" 2
}
# the arrival's Controls card put away first (the layer's right grip; else its A): while it shows, combat counts "the VR
# menu open" and has no blade (runs 191459, 192656, 193846: 5133 of 5275 frames)
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
grep -E 'card: put away' "$R/xr-layer.log" | tail -1 | cut -c1-120
LREST="hands test L -25 -35 30 0 0 0"   # the left hand at rest
w "$LREST" 0.6
draw_slot; w "$REST" 0.6; w "front keep 0" 0.3
for s in $STEPS; do
  case $s in
  sneak)
    echo "== sneak: the far orc (18 m, its back to Talion), crouched, walked up, the thrust into its back"
    far; sv; m sneak-start 0.1
    for k in $(seq 1 14); do
      w "crouch hold 4000" 0.3
      d=$(dist); echo "$(date +%T) dist ${d:-none}"
      [ -n "$d" ] && [ "$d" -lt 200 ] && break
      [ -n "$d" ] && w "execute face" 0.3
      if [ -n "$d" ] && [ "$d" -lt 450 ]; then w "stick 0 0.2 0 0.6" 0.8; else w "stick 0 0.6 0 2" 2.2; fi
    done
    w "crouch hold 9000" 0.4; w "execute face" 0.4; sv; dp sneak-approach; m sneak-approach 0.1
    w "$READY" 0.8; w "$THRUST" 0.3; dp sneak-kill; m sneak-stab 0.01; w "$REST" 0.5; sleep 2.5; dp sneak-after; m sneak-after 0.1; sv
    w "crouch stand" 0.4; w "practice remove" 2 ;;
  back) echo "== back: crouched behind an unaware patrol orc, the thrust into its back"; back_round back 1 ;;
  slow) echo "== slow: standing, slow, behind an unaware patrol orc"; back_round slow 0 ;;
  witness)
    echo "== witness: a patrol orc 30 m off, then the crouched back round on a nearer one"
    w "front keep 1" 0.3; far; back_round witness 1 patrol; w "front keep 0" 0.3; w "practice remove" 2 ;;
  dagger)
    echo "== dagger: Acharn from the left hip, crouched behind an unaware patrol orc, the left thrust into its back"
    w "hands test L -18 -62 -5 0 0 0" 0.5; w "hands squeeze L 0" 1.2; w "hands squeeze L 1" 1.2
    w "hands test L -12 -20 32 0 0 0" 0.8; w "hands squeeze L 0" 0.6
    far; sv; w "crouch hold 9000" 0.6
    w "verbs behind 130" 0.8; w "execute face" 0.6; sv; m dagger-approach 0.1
    w "hands test L -8 -18 15 0 0 0" 0.8; w "hands swing L -8 -12 75 0 0 0 250" 0.3; dp dagger-kill; m dagger-stab 0.01; w "hands test L -12 -20 32 0 0 0" 0.5; sleep 3.2
    m dagger-after 0.1; sv; w "crouch stand" 0.4; w "practice remove" 2; w "$LREST" 0.5 ;;
  front)
    echo "== front: a patrol orc 2 m ahead, facing Talion: the thrust into its front"
    patrol 200; w "execute face" 0.4; sv; m "front-approach" 0.1
    w "$READY" 0.8; w "$THRUST" 0.45; m "front-stab" 0.05; w "$REST" 0.5; sleep 1.5; sv; w "practice remove" 2 ;;
  aware)
    echo "== aware: the practice infantry (it fights Talion), Talion put behind it, the thrust into its back"
    w "verbs template off" 0.3; w "verbs ahead 350" 0.3; spawn infantry; sleep 3; w "crouch hold 9000" 0.6
    w "verbs behind 130" 0.8; w "execute face" 0.4; sv; m "aware-approach" 0.1
    w "$READY" 0.8; w "$THRUST" 0.45; m "aware-stab" 0.05; w "$REST" 0.5; sleep 1.5; sv; w "crouch stand" 0.4; w "practice remove" 2 ;;
  esac
done
w "verbs template off" 0.3; w "verbs ahead 350" 0.3
w "stealth status" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
