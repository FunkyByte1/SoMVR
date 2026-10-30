#!/bin/bash
# above25-drive.sh - build 25 (the som-b25-above helper, docs/re-xr-pipeline.md 102): kills and brands from above by the
# hands, headless. climb2/pin16-drive.sh's functions (sourced) and move24-drive.sh's ways (copied). STEPS (in order):
#   ledge  hanging under the wall's lip (down over the edge, the right hand) with an orc spawned on the top: the game's
#          ledge kill asked (`above probe`, the ledge lines); if it offers it, the stock X with the trace
#   drop   on the wall top 45 cm from the lip facing out, an unaware orc spawned 2 m ahead (on the ground below, its back
#          to the wall: `above far 200`, `practice far`): `above probe`; the cue; the sword drawn, the ready pose (the
#          blade down and out at him), the plunge (50 cm down and 29 out in 180 ms, point first) = the press, the black,
#          the seat, back in, the end (dumps at each); if nothing started: the stock X for the game's own way
#   brand  the same with the brand granted (`beasts grant PSS_Brand`, a test aid): the open left hand out on the line to
#          his head + the left trigger
# SPOT="x y z heading" (the stand point 45 cm in from a lip, its heading out; default the ruin wall by the mark-16
# campfire, re-xr-pipeline.md 101.4). Pictures: scripts/xr_compose.py RUN -f FRAME (frames in RUN/above25-steps.txt).
# Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
STEPS=${STEPS:-drop}
SPOTS=${SPOTS:-ladder:31692 2275 -29022 33;bank:31640.8 2260 -25039.8 115}
SPOT=${SPOT:-31692 2275 -29022 33}
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.7 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
faceyaw() { local want=$1 k y d s d0
  for k in 1 2 3 4 5 6 7 8 9; do
    y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 15 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    if [ -z "$s" ]; then [ "$d" -gt 0 ] && s=+ || s=-
    elif [ "${d#-}" -gt "${d0#-}" ]; then [ $s = + ] && s=- || s=+; fi
    d0=$d; w "turn$s" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
aon() { local n0 k; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done; }
aoff() { printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 0.6; }
unclimb() { climbing || return 0; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; aon; sleep 1.4; aoff; sleep 2
  climbing && { aon; sleep 1.4; aoff; sleep 2; }; climbing && echo "$(date +%T) STILL climbing"; }
pd() { userin && bail "the user is in VR"; printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2; local f
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/above25-steps.txt"; }
pdn() { local k f; for k in $(seq 1 "$3"); do printf 'dump\n' > "$R/xrtest.txt"; sleep "$2"; done; sleep 1
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -"$3" | grep -o '[0-9]*$' | tr '\n' ' ')
  echo "$1 $f" | tee -a "$R/above25-steps.txt"; }
al() { grep -E '^ABOVE' "$L" | grep -v '^ABOVE trace' | tail -${1:-4} | cut -c1-460; }
aprobe() { w "above probe" 1.5; grep -E '^ABOVE probe' "$L" | tail -${1:-3} | cut -c1-1200; }
faceorc() { local r o h
  r=$(root | grep -oE 'root [-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d' ' -f2)
  o=$(grep -E '^PRACTICE t=.*spawn check 1' "$L" | tail -1 | grep -oE 'at \([-0-9. ]+\)' | tr -d '()' | cut -d' ' -f2-)
  [ -n "$r" ] && [ -n "$o" ] || return 1
  read -r h pt < <(python3 -c "import math; r=[float(v) for v in '$r'.split(',')]; o=[float(v) for v in '$o'.split()]
print(round(math.degrees(math.atan2(o[0]-r[0], o[2]-r[2]))), round(-math.degrees(math.atan2(r[1]+77-o[1]-30, math.hypot(o[0]-r[0], o[2]-r[2])))))")
  echo "$(date +%T) the orc at $o, Talion's root $r: the heading $h, the look down $pt deg"; faceyaw "$h"
  # a player looks at the orc he means to drop on (the game's own selection takes its target from the view)
  [ "${LOOK:-1}" = 1 ] && x "head 0 0 0 yaw 0 pitch $pt s 0.4"; }
offered() { grep -E "^ABOVE t=.*: the game offers $1" "$L" | tail -1 | grep -q .; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
# the draw once: the same reach to the slot puts a drawn sword back (boot 20261029-190728's second spot: sheathed)
DRAWN=0
draw() { [ "$DRAWN" = 1 ] && return 0; DRAWN=1; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# the plunge: the grip's -Z (pinky -> thumb, the blade's way) 60 deg under the horizontal ahead (rx -60): the wrist bent
# down as a real overhand stab holds it; 50 cm down and 29 cm out along the blade in 180 ms (2.8 m/s, point first)
READY="hands test R 20 -5 25 -60 0 0"
PLUNGE="hands swing R 20 -55 54 -60 0 0 180"
SLOWDOWN="hands swing R 20 -55 54 -60 0 0 1100"  # the plunge's way over 1.1 s: a lowered sword (boot -223941: 550 ms peaked at 2.08 in one frame)
setup() { set -- $SPOT; unclimb; x "btn R squeeze 0"; go "$1 $(( $2 + 10 )) $3"; faceyaw "$4"; echo "$(date +%T) at: $(root)"; }
# the far orc ahead; where the game really put it (the spawn check: the AI's own placement can move it off the spot -
# boot 20261029-160531: 2.4 m and 5 m up a slope at the ruin wall)
spawn() { w "above far ${1:-200}" 0.5; w "practice far infantry" 3
  grep -E '^PRACTICE t=.*spawn far' "$L" | tail -1 | grep -oE 'at \([-0-9. ]+\) [0-9]+ cm ahead'
  grep -E '^PRACTICE t=.*spawn check 1' "$L" | tail -1 | grep -oE '[0-9.]+ m from Talion at \([-0-9. ]+\)'; }
dropstep() { local n0 sp IFS_=$IFS
  echo "== drop"; IFS=';'; for sp in $SPOTS; do IFS=$IFS_; SPOT=${sp#*:}; echo "-- spot ${sp%%:*} ($SPOT)"
    setup; draw; w "$REST" 0.5; spawn 200; sleep 1; faceorc; w "crouch hold 10000" 1.5; aprobe 2; al 3
    offered "the drop kill" && break; w "crouch stand" 0.4; IFS=';'; done; IFS=$IFS_
  pd "drop-cue"
  if [ "${SLOWFIRST:-1}" = 1 ]; then # the same way at 1.0 m/s: a lowered sword, not the plunge - nothing may start
    w "crouch hold 10000" 0.3; w "$READY" 0.9; w "$SLOWDOWN" 1.5; al 2; w "$READY" 0.9; fi
  n0=$(cnt '^ABOVE t=.*press:'); w "crouch hold 10000" 0.3; w "above trace 9" 0.3; w "$READY" 0.9; pd "drop-ready"
  w "$PLUNGE" 0.2; pdn "drop-black" 0.25 4; sleep 0.5; pdn "drop-in" 0.3 4; sleep 2; pdn "drop-after" 0.8 3; sleep 2; al 14
  pd "drop-end"; grep -E '^BODYMOMENT t=.*(released|the return)' "$L" | tail -3 | cut -c1-300
  if [ "$(cnt '^ABOVE t=.*press:')" -le "$n0" ] || grep -E '^ABOVE t=.*blink' "$L" | tail -1 | grep -q 'took nothing'; then
    echo "$(date +%T) the hand's press did not start it: the stock X while offered (the node log's window)"
    al 2; w "$REST" 0.5; w "crouch hold 10000" 0.6; w "verbs brand nodelog 4" 0.3; w "verbs press 118 150" 0.3; sleep 5; grep -E '^(VERBS t=.*Talion attack|BODYMOMENT t=.*(hold|release|return|blink))' "$L" | tail -10 | cut -c1-300
  fi
  w "crouch stand" 0.4; w "above status" 0.6; al 1; echo "$(date +%T) after: $(root)"; }
brandstep() { local n0
  echo "== brand"; setup; w "$REST" 0.5; w "beasts grant PSS_Brand" 1; spawn 200; sleep 1.5; aprobe 2; al 3
  offered "the brand from above" || { echo "$(date +%T) no brand from above standing: crouched"; w "crouch hold 9000" 1.5; aprobe 2; al 3; }
  # the open left hand out down on the line to his head (60 deg under the horizontal ahead, the palm ~55 cm out)
  w "hands squeeze L 0" 0.3; w "hands test L -5 -48 28 0 0 0" 0.9; pd "brand-aim"
  n0=$(cnt '^ABOVE t=.*press:'); w "above trace 9" 0.3; w "hands trigger L 1" 0.4; w "hands trigger L 0" 0.1
  pdn "brand-black" 0.25 4; sleep 0.5; pdn "brand-in" 0.3 4; sleep 2; pdn "brand-after" 0.8 3; sleep 2; al 14
  [ "$(cnt '^ABOVE t=.*press:')" -le "$n0" ] && { echo "$(date +%T) no brand press: the stock B tap"; w "verbs press 120 150" 0.3; sleep 5
    grep -E '^VERBS t=.*Talion attack' "$L" | tail -5 | cut -c1-300; }
  w "crouch stand" 0.4; w "hands test L -25 -35 30 0 0 0" 0.4; w "above status" 0.6; al 1; }
# the ledge: on the top facing out, down over the edge (the right hand) to hang under the lip, turned to the wall; then
# an orc spawned on the top LEDGE_IN cm past Talion (its back to the edge: the spawn's ray finds the top; spawned first on
# the top 1 m off, it saw Talion and fought - boot 20261029-201145); the game asked (the probe, the ledge lines);
# offered: `above start ledge` (the game's own ledge kill started on him, the view held); not: the stock X, node log
LEDGES=${LEDGES:-bank:31640.8 2260 -25039.8 115;ladder:31692 2275 -29022 33}
ledge1() { local h n0; set -- $1
  echo "-- ledge at $*"; unclimb; x "btn R squeeze 0"; w "hands test off"; go "$1 $(( ${2%.*} + 10 )) $3"
  # LEDGE_FIRST=1: the orc spawned on the top first, crouched, LEDGE_IN (150) cm in - past the game's ~108 cm notice range
  # (boot 20261029-201145's 100 cm: seen at once); boot -204408's spawn from the hang went down the meadow (his body
  # faced out)
  if [ "${LEDGE_FIRST:-1}" = 1 ]; then faceyaw "$(( ($4 + 180) % 360 ))"; w "crouch hold 10000" 1.2; spawn ${LEDGE_IN:-150}
    sleep 1; aprobe 1; fi
  faceyaw "$4"; echo "$(date +%T) at: $(root)"
  w "hands squeeze R 0" 0.3; w "hands test R 0 -115 60 0 0 0" 0.6; w "hands squeeze R 1" 0.05; waitfor 'down: the glide.s end' 6
  grep -E '^CLIMB t=.* down:' "$L" | tail -1 | cut -c1-260; sleep 1; climbing || { echo "$(date +%T) not hanging"; return 1; }
  h=$(( ($4 + 180) % 360 )); faceyaw "$h"; echo "$(date +%T) hanging: $(root)"
  [ "${LEDGE_FIRST:-1}" = 1 ] || spawn ${LEDGE_IN:-150}; sleep 1.5; x "head 0 0 0 yaw 0 pitch 45 s 0.4"; sleep 0.6; pd "ledge-hang"
  aprobe 3; al 3
  if offered "the ledge kill"; then echo "$(date +%T) the ledge kill offered: started by the test word"
    n0=$(wc -l < "$L"); w "above trace 8" 0.3; w "above start ledge" 0.2; pdn "ledge-kill" 0.4 5; sleep 3; pd "ledge-after"
    tail -n +"$n0" "$L" | grep -E '^(ABOVE t=|VERBS t=.*Talion attack|CLIMB t=.* (end|hang|resume|start)|BODYMOMENT t=.*(hold|release|return))' | head -16 | cut -c1-300
  else echo "$(date +%T) no ledge kill offered: the stock X with the node log"
    w "verbs brand nodelog 3" 0.3; w "verbs press 118 150" 0.3; sleep 3
    grep -E "^VERBS brand node.*LedgeHang|^VERBS brand gate|^VERBS t=.*Talion attack" "$L" | tail -5 | cut -c1-260; fi
  x "head 0 0 0 yaw 0 pitch 0 s 0.3"; w "hands squeeze R 0" 0.4; unclimb; w "hands test off" 0.4; }
ledgestep() { local IFS_=$IFS sp; echo "== ledge"; IFS=';'; for sp in $LEDGES; do IFS=$IFS_; ledge1 "${sp#*:}"
  offered "the ledge kill" && break; IFS=';'; done; IFS=$IFS_; }
card
for s in $STEPS; do case $s in drop) dropstep ;; brand) brandstep ;; ledge) ledgestep ;; esac; done
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
