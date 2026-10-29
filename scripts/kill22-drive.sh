#!/bin/bash
# kill22-drive.sh - kill21-drive.sh (build 21's stealth kill) with build 22's steps (docs/re-xr-pipeline.md 93.4: a blade
# the walk carried into its back is no touch): walk0 / walk below; NOQUIT=1 leaves the game running at the end (a driver
# that drives the rest of the boot: scripts/fixes22-drive.sh). Drives one headless boot (scripts/headless_vr.sh with
# scripts/headset-build22.env, SOMVR_HANDS_TEST=1, the layer's test file). Each round: the far orc (practice far, its back
# to Talion), Talion put SD_PLACE (220) cm behind it, the real duck and the test stick's sneak to the stand-off (118 cm),
# then the sword (drawn at the start, its hand at REST behind the hip). SD_STEPS (default "old jab slow thrust out tear
# grab keep keep0"), in order:
#   old     build 20c's rule (`stealth thrust off`): the jab - point first, fast, its tip only just at the orc's back
#   jab     the same jab with build 21's rule: no damage, no kill
#   slow    a slow push (0.37 m/s) on to 55 cm: in its body, no damage, the orc alerted 0.3 s after the touch
#   thrust  at the stand-off the witness (an infantry ~10 m beyond the target, facing Talion), then the thrust: the kill,
#           the blade stuck, the hand held still while the body slumps off it; the witness unaware (+1 s, +3 s)
#   out     the thrust, then drawn straight back at once: out past the wound, the blood
#   tear    the thrust, then the hand 37 cm to the right: torn free
#   grab    build 19b's G2 grab (the left arm out at its back, squeezed), then the thrust: the stab into the held orc
#   neck    standing at the stand-off, leaned in SD_NECK_LEAN (30) cm, a full slash right to left at its neck's height (SD_NECK_Y, 2 cm over the eyes)
#   keep    at the stand-off crouched, then stood up (as the sword's draw does) and the stick on again: held at 118 cm
#   keep0   the same with `stealth keep 0` (build 20c): walked in and noticed (the user's mark 17)
#   walk0   build 21 (`stealth carried off`): the sword held forward (the thrust's end pose: its point ~35 cm past the
#           stand-off) through the sneak - the walk carries it into its back: a touch, the alert (the design consequence)
#   walk    build 22: the same - carried, no touch: it stays unaware 4 s; drawn back, then the thrust: the kill
# Poses: the grips in the head's yaw frame (cm right/up/ahead of the eyes; deg about the grip's X, Y, Z; OpenXR's grip:
# -Z from the pinky to the thumb, the blade along -Z's forward turn: 0 0 0 = the blade straight ahead). SD_RUN=RUN drives
# a running boot. Pictures: scripts/xr_compose.py RUN -f FRAME (frames in RUN/sd-steps.txt).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SD_STEPS:-old jab slow thrust out tear grab keep keep0}
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
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest
# `hands swing` moves along an arc about the shoulder (18 -25 -8): build 19's thrust from 40 cm behind the eyes to 60 cm
# ahead swung round it, and its contact frame could be 60 deg off the blade (runs 20261028-171114, 204131's tear round).
# These poses lie on one ray from the shoulder, d = (-0.201 0.211 0.957) - 12 deg up, 12 deg left - with the blade
# pitched up 12 deg along it (rx 12), so the swings between them are straight thrusts, the point leading (12 deg). At
# the stand-off (crouched: the eyes ~25 cm over the orc's pelvis, its back 87-97 cm ahead) the point (82 cm past the grip)
# enters its back 20-35 cm over its root, its chest (run 211935's level line went in at its pelvis, 221556's 18 deg ray
# passed 12 cm off its upper back)
PRE="hands test R 20 -27 -18 12 0 0"      # on the ray 10 cm behind the shoulder: the point ~61 cm ahead, short of it
READY2="hands swing R 16 -23 0 12 0 0 500" # (from PRE, slowly) 8 cm out on the ray: the point ~77 cm ahead, 10-20 cm short
# of its back (15 cm out touched it when Talion turned: run 215044's thrust round)
THRUST="hands swing R 5 -11 54 12 0 0 180" # a thrust to 65 cm out: 50 cm in 180 ms, the point ~35 cm in
JAB="hands swing R 12 -19 21 12 0 0 80"   # a fast jab to 30 cm out: the point only just at its back
SLOW="hands swing R 5 -11 54 12 0 0 1500"  # the thrust's way at 0.33 m/s: a slow push as deep
PULL="hands swing R 16 -23 0 12 0 0 120" # drawn straight back along the blade, at once (the body falls off it in ~0.45 s)
SIDE="hands swing R 42 -11 54 12 0 0 120"  # the hand 37 cm to the right at the thrust's depth, at once
READY_ARC="hands test R 8 -18 -40 0 0 0"   # build 19's ready pose and arc thrust (the grab round: it reached the held orc,
THRUST_ARC="hands swing R 8 -14 60 0 0 0 250" # run 204131; the ray's thrust passed 67 cm off it, run 215044)
FWD="hands test R 5 -11 54 12 0 0"        # build 22: the thrust's end pose held through the sneak (the point past the stand-off)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
DRAWN=0
draw_slot() { [ "$DRAWN" = 1 ] && return 0; DRAWN=1; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
sv() { w "stealth status" 1.0; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/sd-steps.txt"; }
dq() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$1 (queued $(date +%T.%N | cut -c1-12))" >> "$R/sd-steps.txt"; }
spawn_far() { w "practice far ${1:-infantry}" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED\|refused'; then
    echo "$(date +%T) the far spawn failed: back to the practice area's start"; w "practice back" 6; w "practice go" 7; w "practice far ${1:-infantry}" 3
  fi; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6
draw_slot; w "$REST" 0.6; w "front keep 0" 0.3
noticed() { [ "$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log")" -gt "$N0" ]; }
hold() { w "crouch hold 9000" 0.3; }
NR=0
reset() { NR=$((NR + 1)); [ "$NR" -gt 1 ] && { w "practice back" 6; w "practice go" 7; }; }
round() { echo "== $1"; reset; N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); [ -n "$2" ] && "$2"; spawn_far; sv; m "$1-start" 0.1; }
# the witness: at the stand-off (crouched, behind the target), an infantry SD_WITNESS_CM (1100) ahead - ~10 m beyond the
# target, facing Talion and the kill - kept with the target (`front keep 1`: a new spawn keeps the others). (Spawned 20 m
# ahead at the round's start, it saw Talion standing behind the target and the target turned: run 211935)
witness() { w "front keep 1" 0.3; w "verbs ahead ${SD_WITNESS_CM:-1100}" 0.3; w "practice spawn infantry" 3; w "verbs ahead 350" 0.3; w "front keep 0" 0.3; }
place_behind() { [ "${SD_PLACE:-220}" -gt 0 ] && { w "verbs behind ${SD_PLACE:-220}" 0.8; w "execute face" 1.0; }; }
endr() { sv; x "head 0 0 0 s 0.4" 0.6; w "crouch stand" 0.4; w "verbs template off" 0.3; w "practice remove" 2; w "$REST" 0.4; }
bearing() { w "execute face" 1.2 >&2; w "execute where" 0.6 >&2; grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1 | grep -oE 'bearing [-+][0-9]+' | grep -oE '[-+][0-9]+'; }
head_at() { local dn=$1 ln=$2 b=${3:-0}
  local hx hz; hx=$(awk -v l="$ln" -v b="$b" 'BEGIN{printf "%.1f", l*sin(b*3.14159265/180)}')
  hz=$(awk -v l="$ln" -v b="$b" 'BEGIN{printf "%.1f", -l*cos(b*3.14159265/180)}')
  x "head $hx -$dn $hz yaw $b s ${4:-0.6}" 1.0; }
# the sneak: ducked (the real crouch), the test stick at 0.6 toward it (asked on to 60 cm): the stand-off holds Talion
sneak_to() { local n0 r0 i; n0=$(cnt '^STEALTH t=.* stand-off: orc'); r0=$(cnt '^STEALTH t=.* stand-off: at rest')
  x "head 0 -50 0 s 0.5" 1.2; waitfor '^CROUCH (game|duck) on' 3; w "stealth chase ${1:-0.6} 60" 0.3
  for i in $(seq 1 160); do userin && bail "the user is in VR"; noticed && { echo "$(date +%T) noticed"; break; }
    [ "$(cnt '^STEALTH t=.* stand-off: at rest')" -gt "$r0" ] && break
    [ "$(cnt '^STEALTH t=.* stand-off: orc')" -gt "$n0" ] && [ -z "$S0" ] && S0=$i
    [ -n "$S0" ] && [ $((i - S0)) -gt 12 ] && break; sleep 0.25; done; S0=
  echo "$(date +%T) $(grep -E '^STEALTH t=.* stand-off: ' "$R/somvr.log" | tail -2 | cut -c1-200 | tr '\n' '|')"
  sleep 1.5; w "stealth chase 0" 0.3; sleep 1.0; }
approach() { w "$PRE" 0.4; place_behind; sneak_to 0.6; noticed && echo "$(date +%T) noticed before the blade"; w "execute face" 0.4; w "$READY2" 0.8; }
kills() { cnt '^STEALTH kill'; }
said() { grep -E "$1" "$R/somvr.log" | tail -${2:-3} | cut -c1-260 | sed 's/^/    /'; }
declare -A PD PL PP
PD[G2]=50; PL[G2]=0; PP[G2]="-14 -4 62"     # build 19b's G2: the arm out at its back, not leaned
pose() { local g=$1 b=$2; head_at "${PD[$g]}" "${PL[$g]}" "$b"; w "hands test L ${PP[$g]} 180 -90 0" 0.8; }
for s in $STEPS; do
  k0=$(kills)
  case $s in
  old)
    round old; w "stealth thrust off" 0.3; approach; dp old-ready; w "$JAB" 0.25; dq old-jab; sleep 1.0; w "$READY2" 0.5
    echo "old: kills $(( $(kills) - k0 )) (build 20c's rule: the jab kills)"; said '^STEALTH (kill|t=.* contact)' 2
    w "stealth thrust on" 0.3; sleep 1.5; m old-end 0.1; endr ;;
  jab)
    round jab; approach; dp jab-ready; w "$JAB" 0.25; dq jab-jab; sleep 0.6; w "$READY2" 0.5; sleep 2
    echo "jab: kills $(( $(kills) - k0 )) (build 21: none)"; said '^STEALTH t=.* (touch|alert|aware orc)' 4; m jab-end 0.1; endr ;;
  slow)
    round slow; approach; w "$SLOW" 1.2; dq slow-in; sleep 0.8; w "$READY2" 0.5; sleep 2
    echo "slow: kills $(( $(kills) - k0 )) (none)"; said '^STEALTH t=.* (touch|alert|aware orc)' 4; m slow-end 0.1; endr ;;
  thrust)
    # (the blade drawn back to PRE while the witness comes and Talion faces the target again: a turn swept the ready
    # blade into its shoulder - run 222410)
    round thrust; approach; w "$PRE" 0.4; witness; w "execute face" 0.8; w "$READY2" 0.8; noticed && echo "$(date +%T) noticed with the witness"; dp thrust-ready; w "$THRUST" 0.25; dq thrust-in; sleep 0.25; dq thrust-hold; sleep 1.0; dp thrust-after
    sleep 3; echo "thrust: kills $(( $(kills) - k0 ))"; said '^STEALTH (kill|t=.* (kill by|stick|blood|after the kill))' 8
    w "$READY2" 0.5; m thrust-end 0.1; endr ;;
  out)
    round out; approach; w "$THRUST" 0.3; w "$PULL" 0.1; dq out-out; sleep 1.5
    echo "out: kills $(( $(kills) - k0 ))"; said '^STEALTH t=.* (kill by|stick|blood)' 6; m out-end 0.1; endr ;;
  tear)
    round tear; approach; w "$THRUST" 0.3; w "$SIDE" 0.1; dq tear-side; sleep 1.5
    echo "tear: kills $(( $(kills) - k0 ))"; said '^STEALTH t=.* (kill by|stick|blood)' 6; m tear-end 0.1; endr ;;
  grab)
    round grab; w "$PRE" 0.4; place_behind; sneak_to 0.6; b=$(bearing); echo "bearing $b"; pose G2 "$b"; w "hands squeeze L 1" 0.8
    said '^VERBS t=.* (squeeze by a stealth target|grab:|squeeze: nothing)' 2; w "$READY_ARC" 0.7; dp grab-held
    w "$THRUST_ARC" 0.3; dq grab-stab; sleep 1.2; w "hands squeeze L 0" 0.5; w "$LREST" 0.4
    echo "grab: kills $(( $(kills) - k0 ))"; said '^STEALTH (kill|t=.* (kill by|touch|stick))' 4; sleep 1; m grab-end 0.1; endr ;;
  neck)
    # standing at the stand-off (kept 3 s), a full horizontal slash right to left at its neck's height (sword20's SW:
    # the edge leading, 300 ms)
    round neck; place_behind; sneak_to 0.6; x "head 0 0 -${SD_NECK_LEAN:-30} s 0.3" 0.5; w "execute face" 0.4
    w "hands test R 40 ${SD_NECK_Y:-2} 40 0 -90 90" 0.6; dp neck-ready; w "hands swing R -25 ${SD_NECK_Y:-2} 62 0 90 90 300" 0.3
    dq neck-cut; sleep 1.5; echo "neck: kills $(( $(kills) - k0 ))"; said '^STEALTH (kill|t=.* (kill by|touch|air))' 4
    m neck-end 0.1; endr ;;
  walk|walk0)
    round "$s"; [ "$s" = walk0 ] && w "stealth carried off" 0.3; w "$FWD" 0.6; place_behind; sneak_to 0.6; dp "$s-in"
    sleep 4; noticed && echo "$s: noticed (the walk's blade)" || echo "$s: not noticed after 4 s"
    said '^STEALTH t=.* (carried|touch|alert|aware orc)' 4
    if [ "$s" = walk ] && ! noticed; then # (run 20261029-032839: the orc's idle had moved off the line - faced again)
      for k in 1 2 3; do w "execute face" 0.8; w "$READY2" 0.8; dp "walk-ready$k"; w "$THRUST" 0.25; dq "walk-thrust$k"; sleep 1.5
        [ "$(kills)" -gt "$k0" ] && break; noticed && break; done
      echo "walk: kills $(( $(kills) - k0 ))"; said '^STEALTH (kill|t=.* (kill by|touch|carried))' 4; fi
    [ "$s" = walk0 ] && w "stealth carried on" 0.3; m "$s-end" 0.1; endr ;;
  keep|keep0)
    round "$s"; [ "$s" = keep0 ] && w "stealth keep 0" 0.3; place_behind; sneak_to 0.6
    x "head 0 0 0 s 0.3" 0.5; w "stealth chase 0.6 60" 0.3; sleep 2.2; w "stealth chase 0" 0.3; sleep 2
    noticed && echo "$s: noticed" || echo "$s: not noticed"; w "execute where" 0.6
    said '^(EXECUTE where|STEALTH t=.* (stand-off|aware orc))' 3; [ "$s" = keep0 ] && w "stealth keep 3" 0.3; m "$s-end" 0.1; endr ;;
  esac
done
w "stealth status" 1
[ -n "$NOQUIT" ] && { echo "kill22 done $(date +%T) (NOQUIT)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
