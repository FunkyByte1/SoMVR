#!/bin/bash
# sword20-drive.sh - build 20's sword (docs/re-xr-pipeline.md 79, handoff/HANDOFF-combat.md "build 20"): one headless
# boot with scripts/headset-build20.env (SOMVR_COMBAT_SWING=stroke, SOMVR_HANDS_SURFACE_BLADE=pivot), the test hands
# (SOMVR_HANDS_TEST=1) and the layer's test file (SOMVR_XR_TEST=@RUN@/xrtest.txt). SW_STEPS = any of (in this order;
# default all):
#   wiggle  a practice infantry; the drawn blade held at it and wiggled from the wrist (`hands wiggle R 45 4 2.5 x`: +-45
#           deg at 4 Hz about the grip's +X, the edge leading, the hand still) - first with build 16's rule (`fight swing
#           off`), then with the stroke's (`fight swing on`): the hits by tier in each (COMBAT hit / COMBAT swing lines)
#   real    real swings - an overhead chop, the edge leading (the blade from 70 deg up to 30 deg down in 200 ms, the hand
#           40 cm down and 20 ahead) - until three land: cuts (heavy, k)
#   ground  at the arrival meadow (its Grass): a chop into the ground, then the blade lowered slowly onto it and 30 cm
#           past (it rides up along the ground): SURF blade pivot lines, the meeting's sound and sparks; dumps
#   wall    the stone bank west of the arrival (SW_AT; surface13's way to it): the blade pitched into it, a thrust, a
#           sweep across it, then down at the ground: SURF blade pivot lines (the turn, the clank, the sound, the
#           sparks), the hand never clamped by the blade; dumps of both eyes (RUN/sword-steps.txt: compose with
#           scripts/xr_compose.py RUN -f N) and marks.
# Stops at once (quit) when the user goes into VR. SW_WT: the worktree (default: the one this script is in).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SW_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SW_STEPS:-wiggle real ground wall}
AT=${SW_AT-33350 2000 -27900}
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
check() { if grep -qE "$2" "$R/somvr.log"; then echo "$(date +%T) PASS $1"; else echo "$(date +%T) FAIL $1"; fi; }
# d NAME: a stereo dump now (both eyes + our hands' layer), noted in RUN/surf-steps.txt; md NAME: the dump and a mark
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/surf-steps.txt"; }
md() { d "$1"; m "$1" 0; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
surf() { grep -E '^SURF t=.* (clamp|hold|release|blade|ghost|except)' "$R/somvr.log" | tail -${1:-4} | cut -c1-260; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# climb-drive.sh's probe, near, ahead, turnto
probe() { local n0 i; n0=$(grep -c '^CLIMB probe surfaces' "$R/somvr.log"); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(grep -c '^CLIMB probe surfaces' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.25; done
  grep '^CLIMB probe' "$R/somvr.log" | tail -$([ -n "$1" ] && echo 2 || echo 5) | cut -c1-300; }
near() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# the sword (combat11-drive.sh's)
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1) # (the whole log: 800 lines no longer cover a step)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
lunge() { w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; }

# 0. the controls card away, both test squeezes open, both hands low
x0() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
x0 "btn R squeeze 0"; x0 "btn R squeeze 1"; sleep 1; x0 "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x0 "btn R a 1"; x0 "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
w "hands test L -25 -45 15 0 0 0"; w "$REST" 1
check "the swing's config" '^COMBAT config swing=stroke'
check "the pivot's config" '^CONFIG surface blade=pivot'
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/sword-steps.txt"; }
md() { d "$1"; m "$1" 0; }
tiers() { local a=$1; awk -v a="$a" 'NR > a' "$R/somvr.log" | grep -E '^COMBAT hit tier=' | grep -oE 'tier=[a-z]+' | sort | uniq -c | tr '\n' ' '; }
spawn() { w "practice spawn $1" 2; DN0=$(cnt '^PRACTICE t=.* orc is down'); face; }
down() { [ "$(cnt '^PRACTICE t=.* orc is down')" -gt "$DN0" ]; }
WIG="hands test R 22 -12 38 0 0 0"   # the blade straight ahead at the chest, the tip 138 cm ahead
wigphase() { # $1 = off|on: a fresh orc, then up to 8 rounds of step in + a 2.5 s wiggle, until 4 contacts were dealt
  w "fight swing $1" 1
  spawn infantry
  local L0 C0 k; L0=$(wc -l < "$R/somvr.log"); C0=$(cnt '^COMBAT (hit|glance) ')
  for k in $(seq 1 8); do
    down && spawn infantry
    face; w "stick 0 1 0 0.3" 0.5
    w "$WIG" 0.5; w "hands wiggle R 45 4 2.5 x" 3.0
    [ "$(( $(cnt '^COMBAT (hit|glance) ') - C0 ))" -ge 4 ] && break
  done
  w "$REST" 1
  echo "$(date +%T) WIGGLE swing=$1: contacts $(( $(cnt '^COMBAT (hit|glance) ') - C0 )), hits: $(tiers "$L0")"
  awk -v a="$L0" 'NR > a' "$R/somvr.log" | grep -E '^COMBAT (hit|swing) ' | cut -c1-330 | tail -8
}
if has wiggle || has real; then
  drawn || draw_slot
  sleep 1
  drawn && echo "$(date +%T) the sword is drawn" || echo "$(date +%T) the sword is NOT drawn"
fi
if has wiggle; then
  wigphase off
  md w01-wiggle-old
  wigphase on
  md w02-wiggle-stroke
fi
if has real; then
  w "fight swing on" 1
  spawn infantry
  # a sweep across the front with the edge leading (surface13's lunge, the hand rolled 90 deg: its flat was leading,
  # so build 16 and 20 both made it a slap) and an overhead chop (150 ms)
  SW_A="hands test R 40 -8 35 0 -90 90"; SW="hands swing R -20 -18 62 0 90 90 300"; SW_B="hands swing R 40 -8 35 0 -90 90 300"
  CHOP_A="hands test R 20 5 25 -80 0 0"; CHOP="hands swing R 20 -30 45 40 0 0 150"
  L0=$(wc -l < "$R/somvr.log"); H0=$(cnt '^COMBAT hit ')
  for k in $(seq 1 16); do
    [ "$(( $(cnt '^COMBAT hit ') - H0 ))" -ge 4 ] && break
    down && spawn infantry
    face; w "stick 0 1 0 0.3" 0.5
    if [ $((k % 3)) = 0 ]; then w "$CHOP_A" 0.8; w "$CHOP" 1.0; else w "$SW_A" 0.5; w "$SW" 1.0; w "$SW_B" 1.0; fi
  done
  echo "$(date +%T) REAL swings: hits $(( $(cnt '^COMBAT hit ') - H0 )): $(tiers "$L0")"
  awk -v a="$L0" 'NR > a' "$R/somvr.log" | grep -E '^COMBAT (hit|swing) ' | cut -c1-360 | tail -5
  m r01-real 0.2
  w "$REST" 1
fi
if has ground; then
  drawn || draw_slot
  sleep 1
  # a chop down into the grass (300 ms): from raised to the tip ~27 cm into the ground
  w "hands test R 25 -50 40 -40 0 0" 1.2
  w "hands swing R 25 -120 45 60 0 0 300" 1.5
  surf 3
  md g01-chop
  # lowered slowly (2 s) from the tip just above the ground to ~30 cm past it: the blade turns up, rides on it
  w "hands test R 25 -100 45 50 0 0" 1.2
  w "hands swing R 25 -135 45 50 0 0 2000" 2.6
  surf 3
  md g02-pressed
  w "$REST" 1
  check "the blade at the ground" "^SURF t=.* blade pivot (clank|touch) surf=[A-Za-z_]+ .* n=-?0\.[0-9]+,(0\.[89][0-9]|1\.00),"
  check "a clank at the ground with its sound and sparks" "^SURF t=.* blade pivot meeting \((Grass|grass|dirt|mud|sand|soil)[A-Za-z_]*\) .* playing id [1-9].* sparks rc=1"
  grep -E '^SURF t=.* blade pivot (clank|touch|meeting|release|goes in)' "$R/somvr.log" | tail -6 | cut -c1-300
fi
if has wall; then
  w "stick 0 -1 0 1.5" 2.2
  if [ -n "$AT" ]; then
    w "practice back" 1
    waitfor '^PRACTICE t=.* arrived at' 30 || echo "$(date +%T) no arrival back"
    sleep 2
    w "practice go $AT" 1
    waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
    grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-220
    sleep 3
  fi
  probe
  NB=$(near -50); [ -n "$NB" ] || NB=$(near +0)
  echo "$(date +%T) nearest wall: ${NB:-none}"
  [ -n "$NB" ] && turnto "${NB% *}"
  probe >/dev/null
  PREV=""
  for k in $(seq 1 25); do
    d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
    [ -n "$d" ] || { NB=$(near -50); echo "$(date +%T) nothing ahead; nearest ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"; probe >/dev/null; continue; }
    echo "$(date +%T) ahead $d cm"
    [ "$d" -le 62 ] && break
    if [ "$d" -gt 500 ]; then w "stick 0 1 0 2" 2.4; elif [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
    probe >/dev/null
    [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && { echo "$(date +%T) stopped at $d cm (the wall)"; break; }
    PREV=$d
  done
  D=${d:-60}
  echo "$(date +%T) WALL at $D cm ahead (the view's head)"
  drawn || draw_slot
  sleep 1
  C0=$(cnt '^SURF t=.* clamp hand=R'); G0=$(cnt '^SURF t=.* ghost hand=R on')
  md p00-at-wall
  # the hand in front of the eyes, the blade straight ahead at the stone (the tip would be ~70 cm in): the blade turns
  # along its face; the hand 5 and 10 cm nearer (it turns further); then out again
  for z in 20 25 30; do
    w "hands test R 8 -22 $z 0 0 0" 1.5
    surf 2
    md "p01-blade-at-stone-z$z"
  done
  w "hands test R 8 -22 0 0 0 0" 1.2
  # out of it to the right, then a sweep into it (250 ms, right to left, edge-on: rolled 90): the clank, the sound, sparks
  w "hands test R 35 -20 $((D - 75)) 0 40 90" 1.5
  w "hands swing R 15 -20 $((D - 60)) 0 -55 90 250" 1.2
  surf 3
  md p02-swept-in
  w "hands test R 35 -20 $((D - 75)) 0 40 90" 1.5
  w "hands swing R 15 -20 $((D - 60)) 0 -55 90 250" 1.2
  surf 2
  # down at the ground (away from the stone: turned right): a chop (300 ms) to 60 deg below level, the grip low (the arm
  # stretched: a crouch's reach), held
  w "turn+" 0.8; w "turn+" 0.8; w "turn+" 0.8
  w "hands test R 25 -60 40 -30 0 0" 1.2
  w "hands swing R 25 -110 45 60 0 0 300" 1.5
  surf 3
  md p04-ground
  w "$REST" 1
  check "the blade turned at a surface" '^SURF t=.* blade pivot (clank|touch) '
  check "a clank with a sound" "^SURF t=.* blade pivot meeting .* sound 'cbt_imp_surface_player' playing id [1-9]"
  check "sparks at the contact" '^SURF t=.* blade pivot meeting .* sparks rc=1 on the sword'
  grep -E '^SURF t=.* blade pivot (clank|touch|meeting|release|goes in)' "$R/somvr.log" | tail -10 | cut -c1-300
  C1=$(( $(cnt '^SURF t=.* clamp hand=R') - C0 )); G1=$(( $(cnt '^SURF t=.* ghost hand=R on') - G0 ))
  echo "$(date +%T) the right hand clamped $C1 times, its ghost on $G1 times (the blade pushes no hand: 0 unless the palm met the wall)"
fi
grep -E '^SURF t=.* last 10 s' "$R/somvr.log" | tail -1 | cut -c1-400
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
grep -E '^(SURF|COMBAT) END' "$R/somvr.log" | cut -c1-500
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
