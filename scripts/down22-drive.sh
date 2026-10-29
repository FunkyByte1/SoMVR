#!/bin/bash
# down22-drive.sh - build 22 (som-b22-fixes, docs/re-xr-pipeline.md 93.5): SOMVR_CLIMB_DOWN_OVER on real walls - on foot,
# the right hand reaching down 20 cm over an edge, 115 cm under the eyes, and the grip: the glide out and down to hang
# from that hand (or the refusal's why), then hand over hand down the face (holds 60 cm under the eyes) and A held = the
# way down. pin16/climb2/climb21-drive.sh's functions (sourced or copied). Spots (DN_SPOTS, default "ladder bank plat"):
#   ladder  the top of the wall by the meadow's ladder (the user's build 20c mark 11; its top 2263-2268, 40 cm in from
#           the edge, facing out at LAD_YAW)
#   bank    the meadow bank's 4.4 m edge (run 222900: its face slopes out under the lip - the wall check's refusal)
#   plat    the platform of build 18's mark 5 (its top 2335, its ruin wall): faced at PLAT_YAWS in turn until an edge
# `climb down off` first at the ladder (build 21: nothing but the usual grab). Stops at once (quit) when the user goes
# into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
LAD=${LAD:-31692 2285 -29022}
LAD_YAW=${LAD_YAW:-33}
BANK=${BANK:-31640.8 2270 -25039.8}  # run 222900's spot at the bank's edge (the lip 47 cm out along 0.90,-0.43)
BANK_YAW=${BANK_YAW:-115}
PLAT=${PLAT:-34052.5 2445 -27055}
PLAT_YAWS=${PLAT_YAWS:-180 0 90 -90}  # (run 20261029-003708: its edge at 180)
yaw() { w "climb state" 0.6 >/dev/null; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'; }
faceyaw() { local want=$1 k y d s d0
  for k in 1 2 3 4 5 6 7 8 9; do
    y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 15 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    if [ -z "$s" ]; then [ "$d" -gt 0 ] && s=+ || s=-
    elif [ "${d#-}" -gt "${d0#-}" ]; then [ $s = + ] && s=- || s=+; fi
    d0=$d; w "turn$s" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
dumpsn() { local k f; for k in $(seq 1 "$3"); do printf 'dump\n' > "$R/xrtest.txt"; sleep "$2"; done; sleep 1
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -"$3" | grep -o '[0-9]*$' | tr '\n' ' ')
  echo "$1 $f" | tee -a "$R/down22-steps.txt"; }
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
# the reach down over the edge and the grip; 0 = the glide began, 1 = refused (the why), 2 = nothing (no down line)
reach() { local n0 n1; n0=$(cnt '^CLIMB t=.* down:'); n1=$(cnt '^CLIMB t=.* down: the glide.s end')
  w "hands squeeze R 0" 0.3; w "hands test R 0 -115 60 0 0 0" 0.6; w "hands squeeze R 1" 0.05; dumpsn "$1-glide" 0.2 4
  [ "$(cnt '^CLIMB t=.* down:')" -gt "$n0" ] || { sleep 1; echo "$(date +%T) $1: no down line"; fl 2; return 2; }
  waitfor 'down: the glide.s end' 4; grep -E '^CLIMB t=.* down:' "$L" | tail -n +$((n0 + 1)) | head -3 | cut -c1-400
  [ "$(cnt '^CLIMB t=.* down: the glide.s end')" -gt "$n1" ] && return 0; return 1; }
# after the glide's end: turned round to the wall, hand over hand down (2 holds), both open (the hang), A held = down
downface() { local k HOLD O HX OX ox oz
  echo "$(date +%T) $1 hanging: $(root)"; d "$1-hang"
  # turned to the face: the down line's out vector reversed (run 20261029-003708: six snap turns left the hands 40 deg off it)
  read -r ox oz < <(grep -E '^CLIMB t=.* down: the R hand.s grip' "$L" | tail -1 | grep -oE 'out [-0-9.]+ [-0-9.]+' | cut -d' ' -f2,3)
  faceyaw "$(python3 -c "import math; print(round(math.degrees(math.atan2(-float('$ox'), -float('$oz')))))")"; d "$1-turned"
  HOLD=R
  for k in 1 2; do
    [ "$HOLD" = L ] && { HX=-20; O=R; OX=20; } || { HX=20; O=L; OX=-20; }
    grabat $O -60 35 || { echo "$(date +%T) $1: no grab $O"; break; }
    w "hands squeeze $HOLD 0" 0.4; w "hands test $HOLD $HX -40 10 0 0 0" 0.3
    w "hands swing $O $OX -5 $GZ 0 0 0 1200" 1.6
    echo "$(date +%T) $1 step $k ($O): $(root)"; HOLD=$O
  done
  grep -E '^CLIMB t=.* grab [LR]:' "$L" | tail -3 | cut -c1-220; d "$1-face"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.8; w "hands test off" 0.5
  aon; sleep 1.2; aoff; waitfor 'PRACTICE t=.*arrived|the blink down|the plain fall|end:' 8; dml 3; sleep 2
  echo "$(date +%T) $1 after: $(root)"; }
off_try() { local n0; echo "-- down off (build 21's shipped state) at the ladder top"; w "climb down off"
  n0=$(cnt '^CLIMB t=.* down:'); w "hands test R 0 -115 60 0 0 0" 0.6; w "hands squeeze R 1" 1.5; fl 2
  [ "$(cnt '^CLIMB t=.* down:')" -gt "$n0" ] && echo "UNEXPECTED: a down line with down off"
  w "hands squeeze R 0" 0.6; climbing && { aon; sleep 0.5; aoff; sleep 3; }; w "climb down over"; }
spot() { local name=$1 at=$2 yw=$3 r
  echo "== $name: $at, facing $yw"
  x "btn R squeeze 0"; w "hands test off"; go "$at"; faceyaw "$yw"
  echo "$(date +%T) at: $(root)"; d "$name-at"
  reach "$name"; r=$?
  if [ $r = 0 ]; then downface "$name"; return 0; fi
  w "hands squeeze R 0" 0.6; climbing && { aon; sleep 0.5; aoff; sleep 3; }; return $r; }
card
w "climb unstuck on"; w "climb timer on"; w "climb down over"
for s in ${DN_SPOTS:-ladder bank plat}; do
  case $s in
  ladder) go "$LAD"; faceyaw "$LAD_YAW"; off_try; spot ladder "$LAD" "$LAD_YAW" ;;
  bank) spot bank "$BANK" "$BANK_YAW" ;;
  plat) for yw in $PLAT_YAWS; do spot plat "$PLAT" "$yw"; r=$?; [ $r = 0 ] && break; done ;;
  esac
done
grep -E '^CLIMB END build 21' "$L" | tail -1 | cut -c1-600
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
