#!/bin/bash
# stealth19b-drive.sh - build 19b's stealth round (handoff/HANDOFF-stealth.md "build 19b", docs/re-xr-pipeline.md 72.6-72.9):
# drives one headless boot (scripts/headless_vr.sh with scripts/headset-build19.env, SOMVR_HANDS_TEST=1, the layer's
# test file). The far unaware orc (practice far: 18 m ahead, its back to Talion) each round. SD_STEPS, in order:
#   range:TYPE[:TEMPLATE]  the notice range of that kind: crouched (`crouch hold`), put behind it at SD_RANGE_CM (default
#                 170 160 150 140 132 125 118 112 106 100 94; `verbs behind`), 3.5 s still at each, until it notices.
#                 TYPE infantry|shield|berserker; TEMPLATE = an AI/Templates name for the spawn (`verbs template`)
#   gold          the grab with build 19's rule (`stealth grab 0`, `stealth decel 0`): the real duck (the test head 50 cm
#                 down), the stick's sneak to the stand-off, then the left hand's real poses one by one, each squeezed
#                 and let go: G1 the hand on its lower back (leaned in 30 cm), G2 the arm out at its back (not leaned),
#                 G4 the stick's crouch (the eyes up again, `crouch hold`), leaned: the hand on its mid back; last G3 the
#                 arm up at its shoulder blade (leaned) - and if that one takes it, the sword's stab
#   gnew:Gn       the new rule (SOMVR_STEALTH_KILL_GRAB_CM 45, the brake): the same sneak, pose Gn squeezed and held,
#                 the sword's stab into its back (the stealth kill while held), let go
#   sneak:TYPE    the brake's proof on that kind: the duck, the stick's sneak (0.6) to the stand-off, at rest, a picture
# Each gold/gnew/sneak round starts SD_PLACE (220) cm behind the far orc (`verbs behind`: within 20 m - boot with
# SOMVR_PRACTICE_FAR_CM=800 in the extra env; the later rounds' far spawns drift off the meadow's line otherwise).
# SD_RUN=RUN drives a running boot. The poses are the left grip's in the head's yaw frame (cm right/up/ahead of the eyes;
# deg about the grip's X, Y, Z: 180 -90 0 = the palm forward, the fingers up, the thumb toward the middle - OpenXR's grip:
# -Z from the pinky to the thumb), each within a real arm's ~70 cm of the left shoulder (18 cm left of, 22 under the eyes).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SD_STEPS:-range:berserker range:shield gold gnew:G1}
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
READY2="hands test R 8 -18 -40 0 0 0"    # the sword hand drawn back by the chest, the blade forward
THRUST="hands swing R 8 -14 60 0 0 0 250"
THRUST_LOW="hands swing R 4 -40 50 -30 0 0 250"   # ... down at the held orc (the blade pitched 30 deg down)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
DRAWN=0
draw_slot() { [ "$DRAWN" = 1 ] && return 0; DRAWN=1; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
sv() { w "stealth status" 1.0; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/sd-steps.txt"; }
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
round() { echo "== $1"; reset; N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); spawn_far "${2:-infantry}"; sv; m "$1-start" 0.1; }
# the sneak's start: SD_PLACE cm behind the far orc (`verbs behind`: the walk from 18 m can meet a rock once the rounds'
# snap turns have turned the far spawn's line; run 060828), standing, facing it
place_behind() { [ "${SD_PLACE:-220}" -gt 0 ] && { w "verbs behind ${SD_PLACE:-220}" 0.8; w "execute face" 1.0; }; }
endr() { sv; x "head 0 0 0 s 0.4" 0.6; w "crouch stand" 0.4; w "verbs template off" 0.3; w "practice remove" 2; }
# the residual bearing to the nearest orc after `execute face` (+ = right)
bearing() { w "execute face" 1.2 >&2; w "execute where" 0.6 >&2; grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1 | grep -oE 'bearing [-+][0-9]+' | grep -oE '[-+][0-9]+'; }
# the test head: DOWN cm under the start, leaned LEAN cm toward the orc (the bearing B: the tracked head turned B too)
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
# a pose: NAME DOWN LEAN x y z (the palm forward, the fingers up)
declare -A PD PL PP
PD[G1]=50; PL[G1]=30; PP[G1]="-10 -6 62"     # the hand on its lower back, leaned in
PD[G2]=50; PL[G2]=0;  PP[G2]="-14 -4 62"     # the arm out at its back, not leaned
PD[G3]=50; PL[G3]=30; PP[G3]="-12 19 55"     # the arm up at its shoulder blade, leaned
PD[G4]=0;  PL[G4]=30; PP[G4]="-10 -37 64"    # the stick's crouch (the eyes up), leaned: the hand on its mid back
pose() { local g=$1 b=$2; [ "${PD[$g]}" = 0 ] && hold; head_at "${PD[$g]}" "${PL[$g]}" "$b"
  w "hands test L ${PP[$g]} 180 -90 0" 0.8; }
unpose() { w "$LREST" 0.4; head_at 50 0 "$1" 0.4; }
squeeze_try() { local g=$1 n0; n0=$(cnt '^VERBS t=.* (squeeze by a stealth target|grab:)'); w "hands squeeze L 1" 0.8
  grep -E '^VERBS t=.* (squeeze by a stealth target|grab:|squeeze: nothing)' "$R/somvr.log" | tail -2 | cut -c1-330 | sed "s/^/$g: /"; }
# the stab at the held orc: straight ahead under the eyes, and if it still lives, down at it (the grab holds it low)
stab() { local k0; k0=$(cnt '^STEALTH kill|^STEALTH t=.* kill'); w "$READY2" 0.4; dp "$1-held"; w "$THRUST" 0.3; dp "$1-stab"
  if [ "$(cnt '^STEALTH kill|^STEALTH t=.* kill')" -le "$k0" ]; then w "$READY2" 0.6; w "$THRUST_LOW" 0.3; dp "$1-stab-low"; fi
  m "$1-stab" 0.01; w "$REST" 0.5; }
for s in $STEPS; do
  case $s in
  range:*)
    IFS=: read -r _ ty tm <<< "$s"
    [ -n "$tm" ] && w "verbs template $tm" 0.3
    round "range-$ty${tm:+-$tm}" "$ty"; hold
    for c in ${SD_RANGE_CM:-170 160 150 140 132 125 118 112 106 100 94}; do noticed && { echo "$(date +%T) noticed"; break; }
      hold; w "verbs behind $c" 0.8; w "execute face" 0.4; echo "$(date +%T) placed $c"; sleep 3.5; done
    noticed && echo "$(date +%T) $ty: noticed ($(grep -E '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log" | tail -1 | grep -oE 'at [0-9]+ cm'))"
    m "range-$ty-end" 0.1; endr ;;
  gold)
    round gold; place_behind; w "stealth grab 0" 0.3; w "stealth decel 0" 0.3; sneak_to 0.6; b=$(bearing); echo "bearing $b"
    for g in G1 G2 G4 G3; do noticed && { echo "$(date +%T) noticed"; break; }
      pose $g "$b"; [ "$g" = G1 ] && dp gold-G1-pose; GR0=$(cnt '^VERBS t=.* grab: the left hand'); squeeze_try "old $g"
      if [ "$(cnt '^VERBS t=.* grab: the left hand')" -gt "$GR0" ]; then stab "gold-$g"; w "hands squeeze L 0" 0.5; break; fi
      w "hands squeeze L 0" 0.6; unpose "$b"; done
    w "stealth grab 45" 0.3; w "stealth decel 2.5" 0.3; m gold-end 0.1; endr ;;
  gnew:*)
    g=${s#gnew:}
    round "gnew-$g"; place_behind; sneak_to 0.6; dp "gnew-$g-rest"; b=$(bearing); echo "bearing $b"
    pose $g "$b"; dp "gnew-$g-pose"; squeeze_try "new $g"; stab "gnew-$g"; w "hands squeeze L 0" 0.5; w "$LREST" 0.4
    sleep 2; m "gnew-$g-end" 0.1; endr ;;
  sneak:*)
    ty=${s#sneak:}
    round "sneak-$ty" "$ty"; place_behind; sneak_to 0.6; dp "sneak-$ty-rest"; m "sneak-$ty-rest" 0.1; sleep 2; noticed && echo "$(date +%T) noticed"; endr ;;
  esac
done
w "stealth status" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
