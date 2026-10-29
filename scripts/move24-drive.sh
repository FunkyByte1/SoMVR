#!/bin/bash
# move24-drive.sh - build 24 (the som-b24-move helper, docs/re-xr-pipeline.md 101): the vault, the overhang, climbing
# down and the jump on the real world's walls and obstacles, headless. climb2/pin16-drive.sh's functions (sourced) and
# jump22-drive.sh's / down22-drive.sh's ways (copied). Plans (PLAN="survey vtry ...", in order):
#   survey  at SPOTS ("name:x y z;..."; `here` = no trip): a both-eye picture, `vault grid` (the vault's finds) and
#           `vault edges` (overhangs, walls, slopes) round him; the finds into $R/move24-finds.txt
#   vtry    the vault at real obstacles (VT "x y z heading;..." = the grid's `from` and heading; default: the survey's
#           best VT_N finds): from 80 cm further back, faced along the heading: (1) the game's own under our moves - the
#           run into it and the sprint into it with the vault off (`jump trace`: the root's rise, the FSM, LOCO's owner);
#           (2) the vault on: walked at it until the ring shows (both eyes), A = over (both eyes during and after), on
#   calib   (first, on open ground) the stick's way against the head's heading: two short walks (OFF, SXS for walkat)
#   ledge   the game's own at a 121-160 cm wall (LT "x y z heading"): the run and the sprint into it, traced
#   jtry    the jump on real ground (JT "x y z heading;..."): a running jump along the heading (onto a ledge, a gap)
#   dtry    down over a real wall's edge (DT "x y z heading;..." = an edge's `stand at` and its heading out): the reach
#           down over the edge and the grip = the glide out and down to hang (down22's reach/downface)
#   otry    an overhang's lip from below (OT "x y z heading;..." = the floor under the lip, the heading toward the wall):
#           at the wall under the lip, A = the jump, the right grip at the apex = the climb's catch on the lip, then the
#           overhang's ring and A = the glide onto the top
# Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
SPOTS=${SPOTS:-"meadow:here;camp:30873 3843 -32974;story:42632 3839 -22529;fortA:23100 2865 450;fortB:24800 3600 3300"}
F=$R/move24-finds.txt
tapA() { local k n0
  userin && bail "the user is in VR"
  n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")
  printf 'btn R a 1\n' > "$R/xrtest.txt"; echo "$(date +%T) xrtest: A tap"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  sleep 0.1
  if [ "${1:-0}" -gt 0 ]; then printf 'btn R a 0\ndump\n' > "$R/xrtest.txt"
    for k in $(seq 2 "$1"); do sleep 0.2; printf 'dump\n' > "$R/xrtest.txt"; done
  else printf 'btn R a 0\n' > "$R/xrtest.txt"; fi
  sleep 1.0; }
tapnow() { local k n0; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.02; done
  sleep 0.08; printf 'btn R a 0\n' > "$R/xrtest.txt"; }
# A pressed until the layer ran it, then let go together with a dump (one write: a dump written alone after the release
# overwrote it before the layer read it - A stayed held, boot 20261029-135222), two more dumps 0.3 s apart
tapdump() { local k n0; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.02; done
  sleep 0.08; printf 'btn R a 0\ndump\n' > "$R/xrtest.txt"; sleep 0.35; printf 'dump\n' > "$R/xrtest.txt"; sleep 0.35; printf 'dump\n' > "$R/xrtest.txt"; }
jl() { grep -E '^(JUMP t=|VAULT t=)' "$L" | tail -${1:-4} | cut -c1-400; }
jend() { grep -E '^JUMP t=.* end:' "$L" | tail -1 | cut -c1-420; }
viewsum() { awk '/^JUMP view/ {n++; for (i=1;i<=NF;i++) if ($i=="step") { s=$(i+1); if (s>m) m=s } } END { printf "view frames %d, the largest eye step a frame %.2f cm\n", n, m }' "$L"; }
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.7 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
# the view turned to heading $1 by the test head's own yaw (the layer's `head` word; fire23's goface): closed loop
faceh2() { local want=$1 y d y2
  x "head 0 0 0 yaw 0 pitch 0" >/dev/null; y=$(yaw); [ -n "$y" ] || y=0
  d=$(python3 -c "d=($want)-($y); d=(d+180)%360-180; print(round(d,1))")
  x "head 0 0 0 yaw $d pitch 0" >/dev/null; y2=$(yaw); [ -n "$y2" ] || y2=0
  if python3 -c "import sys; e=lambda a: abs((($want)-(a)+180)%360-180); sys.exit(0 if e($y2) > e($y) + 2 else 1)"; then
    d=$(python3 -c "print(-($d))"); x "head 0 0 0 yaw $d pitch 0" >/dev/null; y2=$(yaw); fi
  echo "$(date +%T) the head's yaw $y2 (want $want; the test head's yaw $d)"; }
faceyaw() { local want=$1 k y d s d0
  for k in 1 2 3 4 5 6 7 8 9; do
    y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 15 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    if [ -z "$s" ]; then [ "$d" -gt 0 ] && s=+ || s=-
    elif [ "${d#-}" -gt "${d0#-}" ]; then [ $s = + ] && s=- || s=+; fi
    d0=$d; w "turn$s" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
# x y z heading -> the point $2 cm back along the heading
back() { python3 -c "import math; a=math.radians(float('$4')); print('%.0f %.0f %.0f' % (float('$1')-math.sin(a)*$5, float('$2'), float('$3')-math.cos(a)*$5))"; }
# the trace since line n0: the root's rise, the FSMs, LOCO's owners, the horizontal path
trsum() { tail -n +"$1" "$L" | grep -E '^JUMP trace' | awk '{ split($5, r, ","); if (n == 0) { y0 = r[2]; x0 = r[1]; z0 = r[3] }
  n++; if (r[2] - y0 > up) up = r[2] - y0; if (y0 - r[2] > dn) dn = y0 - r[2]; f[$7]++; o[$NF]++; x1 = r[1]; z1 = r[3] }
  END { printf "trace %d ticks: the root rose %.1f cm at most (fell %.1f); along %.0f cm; fsm", n, up, dn, sqrt((x1-x0)^2+(z1-z0)^2);
  for (k in f) printf " %s:%d", k, f[k]; printf "; LOCO"; for (k in o) printf " %s:%d", k, o[k]; printf "\n" }'; }
survey1() { local name=${1%%:*} at=${1#*:} n0
  echo "== survey $name ($at)"
  [ "$at" = here ] || go "$at"
  echo "$(date +%T) at: $(root)"; d "sv-$name"
  w "vault grid ${SV_R:-1500} ${SV_GSTEP:-150}" 0.5; waitfor '^VAULT survey .* grid done' 90
  grep -E '^VAULT survey .* grid done' "$L" | tail -1 | cut -c1-420
  w "vault edges ${SV_R:-1500} ${SV_ESTEP:-100}" 0.5; waitfor '^VAULT survey .* edges done' 120
  grep -E '^VAULT survey .* edges done' "$L" | tail -1 | cut -c1-300; }
survey() { local IFS_=$IFS sp n0
  n0=$(wc -l < "$L")
  IFS=';'; for sp in $SPOTS; do IFS=$IFS_; survey1 "$sp"; IFS=';'; done; IFS=$IFS_
  tail -n +"$n0" "$L" | grep -E '^VAULT (grid|edge) t=' | cut -c1-330 > "$F.new"; cat "$F.new" >> "$F"; rm -f "$F.new"
  echo "finds: $(grep -c 'grid t=.* find:' "$F") vaults, $(grep -c 'grid t=.* near:' "$F") 121-160 cm walls, $(grep -c 'OVERHANG' "$F") overhangs, $(grep -c ' WALL:' "$F") walls"; }
# the survey's best vaults: OVER first, tops 60-115 cm nearest the waist (90), then ONTO; "x y z heading" each
bestv() { grep -E 'grid t=.* find:' "$F" | sed -E 's/.*from ([0-9.-]+) ([0-9.-]+) ([0-9.-]+) heading ([0-9]+): (OVER|ONTO) the ([0-9]+) cm top.*/\5 \6 \1 \2 \3 \4/' |
  awk '{ s = ($1 == "OVER" ? 0 : 100) + ($2 > 90 ? $2 - 90 : 90 - $2); print s, $3, $4, $5, $6 }' | sort -n | head -${1:-3} | cut -d' ' -f2- | paste -sd';'; }
hv() { local k v; for k in 1 2 3; do w "jump state" 0.7 >/dev/null
  v=$(grep -E '^JUMP state' "$L" | tail -1 | grep -oE "heading -?[0-9.]+" | grep -oE -- '-?[0-9.]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; echo 0; }
rxz() { w "climb state" 0.6 >/dev/null; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+' | cut -d' ' -f2 | awk -F, '{print $1, $3}'; }
# the heading (0 = +z, 90 = +x) of the walk from "x0 z0" to "x1 z1"
wdir() { python3 -c "import math; print(round(math.degrees(math.atan2(float('$3')-float('$1'), float('$4')-float('$2')))))"; }
# LOCO walks the stick in the frame of the game view's heading (g_inp.hc: `jump state`'s "the stick's"), x right
hc() { local k v; for k in 1 2 3; do w "jump state" 0.7 >/dev/null
  v=$(grep -E '^JUMP state' "$L" | tail -1 | grep -oE "the stick's -?[0-9.]+" | grep -oE -- '-?[0-9.]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; echo 0; }
# the stick (x y at magnitude $2) that walks along heading $1 now
stickfor() { local h; h=$(hc); python3 -c "import math; a=math.radians(float('$1')-float('$h')); print('%.2f %.2f' % ($2*math.sin(a), $2*math.cos(a)))"; }
# the stick (at magnitude $3) that walks from where he is now at the world point x=$1 z=$2: a short pulse (the stick
# ahead at 0.4 for 0.3 s, ~35 cm) measures the stick's frame from the body's own velocity (`jump trace`), then the
# stick's angle = the target's heading less that one (boot 20261029-130544: the game view's heading read 25 deg off)
stickto() { local h n0 d0 r; n0=$(wc -l < "$L"); w "jump trace 1.2" 0.35 >/dev/null; w "stick 0 0.4 0 0.3" 0.9 >/dev/null
  d0=$(tail -n +"$n0" "$L" | grep -E '^JUMP trace' | awk '{ split($11, v, ","); if (v[1] * v[1] + v[3] * v[3] > 2500) { x = v[1]; z = v[3] } } END { if (x != "") print x, z }')
  r=$(rxz); [ -n "$d0" ] || { h=$(hc); d0=$(python3 -c "import math; a=math.radians(float('$h')); print(math.sin(a), math.cos(a))"); }
  python3 -c "import math; x,z=map(float,'$r'.split()); vx,vz=map(float,'$d0'.split()); a=math.atan2(float('$1')-x, float('$2')-z)-math.atan2(vx, vz); print('%.2f %.2f' % ($3*math.sin(a), $3*math.cos(a)))"; }
# along heading $4 from x=$1 z=$3, $5 cm: "x z"
along() { python3 -c "import math; a=math.radians(float('$4')); print('%.0f %.0f' % (float('$1')+math.sin(a)*float('$5'), float('$3')+math.cos(a)*float('$5')))"; }
calib() { local h a b d1
  echo "== calib: the stick ahead against the game view's heading"
  h=$(hc); a=$(rxz); w "stick 0 1 0 0.6" 1.2; b=$(rxz); d1=$(wdir $a $b)
  echo "$(date +%T) calib: the stick ahead walked heading $d1 from $a to $b, the game view's heading $h"; }
vtry1() { local x=$1 y=$2 z=$3 h=$4 g=${5:-1} fc=${6:-150} st n0 k sk fp
  unclimb
  echo "== vtry at $x $y $z heading $h (the face $fc cm on)"
  st=$(back "$x" "$y" "$z" "$h" 90); fp=$(along "$x" "$y" "$z" "$h" "$fc"); echo "the face at $fp"
  # (1) the game's own under our moves: the run, then the sprint, into it with the vault off
  if [ "$g" = 1 ]; then
    w "vault off" 0.4; go "$st"; faceh2 "$h"; d "vt-at"
    sk=$(stickto $fp 1); echo "the stick $sk"; n0=$(wc -l < "$L"); w "jump trace 3.5" 0.4; w "stick $sk 0 3.0" 3.4
    echo "run into it ($sk): $(trsum "$n0")"; lastloco 1; grep -E '^VAULT t=' "$L" | tail -1 | cut -c1-200; d "vt-run"
    go "$st"; faceh2 "$h"
    sk=$(stickto $fp 1); echo "the stick $sk"; n0=$(wc -l < "$L"); w "jump trace 3.5" 0.4; w "stick $sk 0 3.0" 0.3; x "btn L stick 1"; x "btn L stick 0"; sleep 1.8
    echo "sprint into it ($sk): $(trsum "$n0")"; lastloco 1
  fi
  # (2) the vault walked at: A at the ring's first frame while moving (the player's way)
  w "vault on" 0.4; go "$st"; faceh2 "$h"
  sk=$(stickto $fp 0.5); echo "the stick $sk"; n0=$(cnt '^VAULT t=.* ring:')
  w "jump trace 4" 0.4; w "stick $sk 0 2.5" 0.05
  for k in $(seq 1 60); do [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ] && break; sleep 0.05; done
  if [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ]; then tapdump; VMOV=walking
  else
    # (3) standing at it, looking at it: on the survey's own spot (its plan held there), the view along the heading
    echo "$(date +%T) no ring while walking at it ($sk): standing at the survey's spot"
    go "$x $y $z"; faceh2 "$h"; sleep 0.6; n0=$(cnt '^VAULT t=.* ring:')
    for k in $(seq 1 20); do grep -E '^VAULT t=' "$L" | tail -1 | grep -q ' ring:' && break; sleep 0.1; done
    grep -E '^VAULT t=' "$L" | tail -1 | grep -q ' ring:' || { echo "$(date +%T) no ring standing either"; grep -E '^VAULT t=' "$L" | tail -2 | cut -c1-260; d "vt-noring"; return 1; }
    d "vt-ring"; w "jump trace 4" 0.4; tapdump; VMOV=standing
  fi
  sleep 2.0; echo "the vault ($VMOV):"; grep -E '^VAULT t=.* ring:' "$L" | tail -1 | cut -c1-330; jl 3; jend; viewsum
  d "vt-after"; echo "$(date +%T) after: $(root)"; }
vtry() { local IFS_=$IFS v; VT=${VT:-$(bestv "${VT_N:-3}")}; echo "vault spots: $VT"
  IFS=';'; for v in $VT; do IFS=$IFS_; set -- $v; vtry1 "$1" "$2" "$3" "$4" "$5"; IFS=';'; done; IFS=$IFS_; }
# the game's own at a 121-160 cm wall (a ledge): the run and the sprint into it, traced (LT "x y z heading")
ledge() { local st sk n0 fp; set -- $LT; echo "== ledge at $1 $2 $3 heading $4"; fp=$(along "$1" "$2" "$3" "$4" "${5:-150}")
  unclimb
  st=$(back "$1" "$2" "$3" "$4" 90); w "vault on" 0.3; go "$st"; faceh2 "$4"; d "lg-at"
  sk=$(stickto $fp 1); n0=$(wc -l < "$L"); w "jump trace 3.5" 0.4; w "stick $sk 0 3.0" 3.4
  echo "run into it ($sk): $(trsum "$n0")"; lastloco 1; grep -E '^VAULT t=' "$L" | tail -2 | cut -c1-200
  go "$st"; faceh2 "$4"
  sk=$(stickto $fp 1); n0=$(wc -l < "$L"); w "jump trace 3.5" 0.4; w "stick $sk 0 3.0" 0.3; x "btn L stick 1"; x "btn L stick 0"; sleep 1.8
  echo "sprint into it ($sk): $(trsum "$n0")"; lastloco 1; d "lg-sprint"; }
# never a climb or a hang left over into the next trip: A held 1.4 s (the drop mark's timer, then the fall)
unclimb() { climbing || return 0; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; aon; sleep 1.4; aoff; sleep 2
  climbing && { aon; sleep 1.4; aoff; sleep 2; }; climbing && echo "$(date +%T) STILL climbing"; }
dumpsn() { local k f; for k in $(seq 1 "$3"); do printf 'dump\n' > "$R/xrtest.txt"; sleep "$2"; done; sleep 1
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -"$3" | grep -o '[0-9]*$' | tr '\n' ' ')
  echo "$1 $f"; }
aon() { local n0 k; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  echo "$(date +%T) A down"; }
aoff() { printf 'btn R a 0\n' > "$R/xrtest.txt"; echo "$(date +%T) A up"; sleep 0.6; }
dml() { grep -E '^CLIMB t=.* (drop mark|down|mantle|end:|hang:)' "$L" | tail -${1:-4} | cut -c1-360; }
grabat() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.5
  for k in 1 2 3 4 5; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.25; done
    w "hands squeeze $h 0" 0.4; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.4
  done; return 1; }
# down over a real edge (DT "x y z heading;..." = the survey's `stand at` and the heading out): there, faced out, the
# right hand 115 cm under the eyes 60 cm out over the edge and the grip = the glide out and down to hang; then turned to
# the face, hand over hand down two holds, both open (the hang), A held = the way down
dtry1() { local x=$1 y=$2 z=$3 h=$4 n0 n1 k HOLD O HX OX ox oz
  unclimb
  echo "== dtry at $x $y $z heading $h"
  x "btn R squeeze 0"; w "hands test off"; go "$x $((y + 10)) $z"; faceyaw "$h"; echo "$(date +%T) at: $(root)"; d "dt-at"
  n0=$(cnt '^CLIMB t=.* down:'); n1=$(cnt '^CLIMB t=.* down: the glide.s end')
  w "hands squeeze R 0" 0.3; w "hands test R 0 -115 60 0 0 0" 0.6; w "hands squeeze R 1" 0.05; dumpsn "dt-glide" 0.25 4
  if [ "$(cnt '^CLIMB t=.* down:')" -le "$n0" ]; then sleep 1; echo "$(date +%T) no down line"; fl 2
    w "hands squeeze R 0" 0.6; climbing && { aon; sleep 0.8; aoff; sleep 3; }; return 2; fi
  waitfor 'down: the glide.s end' 4; grep -E '^CLIMB t=.* down:' "$L" | tail -n +$((n0 + 1)) | head -3 | cut -c1-400
  if [ "$(cnt '^CLIMB t=.* down: the glide.s end')" -le "$n1" ]; then
    w "hands squeeze R 0" 0.6; climbing && { aon; sleep 0.8; aoff; sleep 3; }; return 1; fi
  echo "$(date +%T) hanging: $(root)"; d "dt-hang"
  read -r ox oz < <(grep -E '^CLIMB t=.* down: the R hand.s grip' "$L" | tail -1 | grep -oE 'out [-0-9.]+ [-0-9.]+' | cut -d' ' -f2,3)
  [ -n "$ox" ] && faceyaw "$(python3 -c "import math; print(round(math.degrees(math.atan2(-float('$ox'), -float('$oz')))))")"
  d "dt-turned"; HOLD=R
  for k in 1 2; do
    [ "$HOLD" = L ] && { HX=-20; O=R; OX=20; } || { HX=20; O=L; OX=-20; }
    grabat $O -60 35 || { echo "$(date +%T) no grab $O"; break; }
    w "hands squeeze $HOLD 0" 0.4; w "hands test $HOLD $HX -40 10 0 0 0" 0.3
    w "hands swing $O $OX -5 $GZ 0 0 0 1200" 1.6
    echo "$(date +%T) step $k ($O): $(root)"; HOLD=$O
  done
  grep -E '^CLIMB t=.* grab [LR]:' "$L" | tail -2 | cut -c1-220; d "dt-face"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.8; w "hands test off" 0.5
  aon; sleep 1.2; aoff; waitfor 'PRACTICE t=.*arrived|the blink down|the plain fall|end:' 8; dml 3; sleep 2
  echo "$(date +%T) after: $(root)"; }
dtry() { local IFS_=$IFS v; echo "down spots: $DT"; IFS=';'; for v in $DT; do IFS=$IFS_; set -- $v; dtry1 "$1" "$2" "$3" "$4"; IFS=';'; done; IFS=$IFS_; }
# an overhang's lip from below (OT "lipx lipy lipz out drop jump;..." = the survey's lip, its heading out, the drop
# under it, 1 = A's jump first): on the floor 45 cm out from the lip facing the wall, the right hand put on the lip's edge
# (3 cm out, 3 cm under its top, in the head's frame from where he really stands; at the jump's apex with jump 1), the
# grip: the hold; the overhang's ring, A = the glide onto the top (both eyes)
otry1() { local lx=$1 ly=$2 lz=$3 out=$4 drop=$5 jmp=${6:-0} st fy n0 k hl
  unclimb
  st=$(along "$lx" "$ly" "$lz" "$out" 45); fy=$(python3 -c "print(int(float('$ly')-float('$drop')+112))")
  echo "== otry: the lip at $lx $ly $lz out $out (the drop $drop, jump $jmp), from ${st% *} $fy ${st#* }"
  x "btn R squeeze 0"; w "hands squeeze R 0"; w "hands squeeze L 0"; w "hands test off"; go "${st% *} $fy ${st#* }"
  faceyaw "$(( (out + 180) % 360 ))"; echo "$(date +%T) at: $(root)"; d "ot-at"
  hl=$(grep -E '^CLIMB state' "$L" | tail -1 | python3 -c "
import re, sys, math
l = sys.stdin.read(); r = re.search(r'root ([0-9.-]+),([0-9.-]+),([0-9.-]+) yaw [0-9.-]+ \(the head.s ([0-9.-]+)', l)
rx, ry, rz, yh = map(float, r.groups()); a = math.radians(float('$out')); yh = math.radians(yh)
tx, tz, ty = float('$lx') + math.sin(a) * 3, float('$lz') + math.cos(a) * 3, float('$ly') - 3
dx, dz = tx - rx, tz - rz
print('%.0f %.0f %.0f' % (dx * math.cos(yh) - dz * math.sin(yh), ty - (ry + 65) - (60 if '$jmp' == '1' else 0), dx * math.sin(yh) + dz * math.cos(yh)))")
  if [ "$(echo $hl | cut -d' ' -f3)" -gt 52 ]; then
    local sk; sk=$(stickto "$lx" "$lz" 0.35); w "stick $sk 0 0.6" 1.2; root >/dev/null
    hl=$(grep -E '^CLIMB state' "$L" | tail -1 | python3 -c "
import re, sys, math
l = sys.stdin.read(); r = re.search(r'root ([0-9.-]+),([0-9.-]+),([0-9.-]+) yaw [0-9.-]+ \(the head.s ([0-9.-]+)', l)
rx, ry, rz, yh = map(float, r.groups()); a = math.radians(float('$out')); yh = math.radians(yh)
tx, tz, ty = float('$lx') + math.sin(a) * 3, float('$lz') + math.cos(a) * 3, float('$ly') - 3
dx, dz = tx - rx, tz - rz
print('%.0f %.0f %.0f' % (dx * math.cos(yh) - dz * math.sin(yh), ty - (ry + 65) - (60 if '$jmp' == '1' else 0), dx * math.sin(yh) + dz * math.cos(yh)))")
  fi
  echo "$(date +%T) the hand at $hl (right, up, ahead of the head)"
  n0=$(cnt '^CLIMB t=.* grab R:')
  w "hands test R $hl 0 0 0" 0.6
  if [ "$jmp" = 1 ]; then tapnow; sleep 0.12; printf 'hands squeeze R 1' > "$R/somvr.trigger"; sleep 0.4
  else w "hands squeeze R 1" 0.4; fi
  for k in $(seq 1 12); do [ "$(cnt '^CLIMB t=.* grab R:')" -gt "$n0" ] && break; sleep 0.1; done
  sleep 0.8; grep -E '^CLIMB t=.* grab R:' "$L" | tail -1 | grep -oE "edge [01][^;]*" | head -1; jend
  climbing || { echo "$(date +%T) NOT climbing"; w "hands squeeze R 0" 0.5; w "hands test off"; return 1; }
  d "ot-held"; sleep 1
  grep -E '^CLIMB t=.* (mantle|pullover)' "$L" | tail -2 | cut -c1-330
  aon; sleep 0.15; aoff; dumpsn "ot-glide" 0.35 3; sleep 2
  grep -E '^CLIMB t=.* (mantle|end:|A at)' "$L" | tail -4 | cut -c1-360; d "ot-after"
  w "hands squeeze R 0" 0.4; w "hands test off" 0.4; echo "$(date +%T) after: $(root)"; }
otry() { local IFS_=$IFS v; echo "overhang spots: $OT"; IFS=';'; for v in $OT; do IFS=$IFS_; set -- $v; otry1 "$@"; IFS=';'; done; IFS=$IFS_; }
# the jump on real ground (JT "x y z heading mode;..."): mode run = a running jump along the heading; wall = at the face
# ahead, A and the right grip at the apex = the climb's catch
jtry1() { local x=$1 y=$2 z=$3 h=$4 mode=${5:-run} sk n0 k
  unclimb
  echo "== jtry ($mode) at $x $y $z heading $h"
  x "btn R squeeze 0"; w "hands squeeze R 0"; w "hands test off"; go "$x $((y + 10)) $z"; faceyaw "$h"; d "jt-at"
  if [ "$mode" = wall ]; then
    sk=$(stickfor "$h" 0.5); w "stick $sk 0 1.5" 1.8
    w "hands test R 20 75 50 0 0 0" 0.6; n0=$(cnt '^CLIMB t=.* grab R:')
    w "jump trace 2" 0.4; tapnow; sleep 0.12; printf 'hands squeeze R 1' > "$R/somvr.trigger"; sleep 0.1
    for k in $(seq 1 12); do [ "$(cnt '^CLIMB t=.* grab R:')" -gt "$n0" ] && break; sleep 0.1; done
    sleep 0.6; jl 3; jend; fl 3; d "jt-held"
    climbing && echo "$(date +%T) climbing: the catch" || echo "$(date +%T) NOT climbing"
    w "hands squeeze R 0" 0.6; climbing && { aon; sleep 0.3; aoff; sleep 2; }; w "hands test off" 0.4
  else
    sk=$(stickfor "$h" 1); n0=$(wc -l < "$L"); w "jump trace 3" 0.4; w "stick $sk 0 2.5" 0.9; tapdump; sleep 1.3
    jl 3; jend; viewsum; echo "the path: $(trsum "$n0")"; d "jt-after"
  fi; echo "$(date +%T) after: $(root)"; }
jtry() { local IFS_=$IFS v; echo "jump spots: $JT"; IFS=';'; for v in $JT; do IFS=$IFS_; set -- $v; jtry1 "$@"; IFS=';'; done; IFS=$IFS_; }
card
w "climb unstuck on"; w "climb timer on"; w "climb down over"; w "climb ohmantle on"
for p in ${PLAN:-survey vtry}; do case $p in survey) survey ;; calib) calib ;; vtry) vtry ;; ledge) ledge ;; dtry) dtry ;; otry) otry ;; jtry) jtry ;; esac; done
grep -E '^JUMP END' "$L" | tail -1 | cut -c1-700
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
