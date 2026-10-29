#!/bin/bash
# climb21-drive.sh - build 21 (the som-b21-climb helper): the user's build 20c session (logs/boot-check-20261028-193826)
# marks 4/5/11/21. pin16-drive.sh's and climb2-drive.sh's functions (sourced), plans:
#   down    the meadow bank's top at its 4.4 m edge (BANK, the edge at BANK_YAW; the tower top of mark 4 drops him -
#           the trip lands before its collision streams in), facing out: the drop's timer on foot with the move stick held at A's press (SOMVR_CLIMB_UNSTUCK: kept), the hold
#           slowed to 1.2 s for pictures (both eyes every 0.25 s), a fresh tilt = cancel; a tap; then the right hand
#           reaching down 20 cm over the edge, 115 cm under the eyes: `climb down off` (nothing), `climb down over` (the
#           glide over the edge, both eyes during it), turned round to the wall, hand over hand down (holds 60 cm under the eyes), both
#           hands open (the hang), A held with the timer (pictures) = the blink down
#   spire   mark 5's spire by the tower top (TOWER, twice if the first trip falls through; faced at SPIRE_YAW): the right hand on its top (the eyes 48 cm over it), a fast pull
#           down and let go = the fling: `climb unstuck off` (build 20c: up into the air, out of reach) then on (the stop
#           at the wall's reach); A held = the way down after each
#   ladder  mark 11's wall by the meadow's ladder (LADDER_AT, facing LADDER_YAW): hand over hand up to its rounded lip:
#           `climb roundlip off` (build 20c: the holds there no edge, the pullover's top the crest) then on (the real top,
#           the pullover's ring / the mantle); the pulls there, A held = down between
#   ledge   the down plan on the narrow ledge of the user's cliff (LEDGE, its edge at LEDGE_YAW; the face the user
#           climbed in 20c under it)
# Several in one boot (PLAN="down spire ladder"). Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
BANK=${BANK:-31480 2260 -24797}
LEDGE=${LEDGE:-33761.8 2575 -27236.1}
LEDGE_YAW=${LEDGE_YAW:--60}
BANK_YAW=${BANK_YAW:-90}
TOWER=${TOWER:-33704.2 3639 -27376.1}
SPIRE_YAW=${SPIRE_YAW:--153}
LADDER_AT=${LADDER_AT:-31762 2000 -28905}
LADDER_YAW=${LADDER_YAW:--147}
yaw() { w "climb state" 0.6 >/dev/null; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'; }
# snap turns (30 deg) until Talion's yaw is within 15 deg of $1
faceyaw() { local want=$1 k y d s y0 d0
  for k in 1 2 3 4 5 6 7 8 9; do
    y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 15 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    if [ -z "$s" ]; then [ "$d" -gt 0 ] && s=+ || s=-
    elif [ "${d#-}" -gt "${d0#-}" ]; then [ $s = + ] && s=- || s=+; fi
    d0=$d; w "turn$s" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
# n dumps $2 s apart, named $1-k (the frames to $R/climb21-steps.txt)
dumpsn() { local k f; for k in $(seq 1 "$3"); do printf 'dump\n' > "$R/xrtest.txt"; sleep "$2"; done; sleep 1
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -"$3" | grep -o '[0-9]*$' | tr '\n' ' ')
  echo "$1 $f" | tee -a "$R/climb21-steps.txt"; }
aon() { local n0 k; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  echo "$(date +%T) A down"; }
aoff() { printf 'btn R a 0\n' > "$R/xrtest.txt"; echo "$(date +%T) A up"; sleep 0.6; }
dml() { grep -E '^CLIMB t=.* (drop mark|down|round lip|mantle|pullover|fling|end:|hang:)' "$L" | tail -${1:-4} | cut -c1-330; }
xrl() { grep -E 'xr: objective frame .*(timer|landing)' "$R/xr-layer.log" | tail -${1:-3} | cut -c1-220; }
# hand $1 at (y $2 under/over the eyes, z $3 ahead) squeezed until a grab (z 8 cm deeper each try); GZ
grabat() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.5
  for k in 1 2 3 4 5; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.25; done
    w "hands squeeze $h 0" 0.4; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.4
  done; return 1; }
ontop() { go "${SPOT:-$BANK}"; faceyaw "${SPOT_YAW:-$BANK_YAW}"; w "stick 0 1 0 2.5" 3; echo "$(date +%T) at the edge: $(root) (the head's yaw $(yaw))"; }
downplan() { local n0 k HOLD O HX OX
  echo "== down: ${SPOT:-$BANK} (the meadow bank by default), its edge at heading ${SPOT_YAW:-$BANK_YAW}"
  x "btn R squeeze 0"; w "hands test off"; w "climb unstuck on"; w "climb timer on"
  ontop
  echo "-- the timer on foot, the stick held at A's press (the hold 1.2 s for pictures)"
  w "climb dropmark 3000" 0.5; w "stick 0 1 0 6" 0.8
  x "head 0 0 0 pitch -60 s 0.4"; sleep 0.5
  aon; d timer-foot-1; d timer-foot-2; d timer-foot-3
  dml 4; xrl 3
  echo "-- the stick back, then tilted anew: cancel"; sleep 2.5; w "stick 0 1 0 0.6" 1; aoff; dml 3
  x "head 0 0 0 s 0.4"; sleep 0.4
  w "climb dropmark 300" 0.5
  echo "-- a tap (0.1 s): nothing"; aon; sleep 0.1; aoff; dml 2
  echo "-- down off: the right hand reaching down over the edge"
  w "climb down off"; w "hands test R 0 -115 60 0 0 0" 0.6; n0=$(cnt '^CLIMB t=.* down:')
  w "hands squeeze R 1" 1.5; echo "$(date +%T) off: $(root)"; fl 2
  [ "$(cnt '^CLIMB t=.* down:')" -gt "$n0" ] && echo "UNEXPECTED: a down line with down off"
  w "hands squeeze R 0" 0.6; climbing && { echo "(a climb began: A to end it)"; aon; sleep 0.5; aoff; sleep 3; }
  ontop
  echo "-- down over"; w "climb down over"; w "hands test R 0 -115 60 0 0 0" 0.6
  n0=$(cnt 'down: the glide.s end'); w "hands squeeze R 1" 0.05; dumpsn down-glide 0.2 5
  waitfor 'down: the glide.s end' 5; dml 3; echo "$(date +%T) hanging: $(root)"; d down-hold
  echo "-- turned round to the wall (snap turns: the hold keeps him)"; for k in 1 2 3 4 5 6; do w "turn+" 0.7; done
  echo "$(date +%T) the head's yaw $(yaw): $(root)"; d down-turned
  echo "-- hand over hand down"
  HOLD=R
  for k in 1 2; do
    [ "$HOLD" = L ] && { HX=-20; O=R; OX=20; } || { HX=20; O=L; OX=-20; }
    grabat $O -60 35 || { echo "$(date +%T) no grab $O"; break; }
    w "hands squeeze $HOLD 0" 0.4; w "hands test $HOLD $HX -40 10 0 0 0" 0.3
    w "hands swing $O $OX -5 $GZ 0 0 0 1200" 1.6
    echo "$(date +%T) step $k ($O): $(root)"; HOLD=$O
  done
  grep -E '^CLIMB t=.* grab [LR]:' "$L" | tail -5 | cut -c1-200; d down-face
  echo "-- both open: the hang; A held with the timer (1.2 s) = the blink down"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.8; w "hands test off" 0.5; fl 2
  x "head 0 0 0 pitch -60 s 0.4"; sleep 0.5
  w "climb dropmark 3000" 0.5; aon; d timer-hang-1; d timer-hang-2; d timer-hang-3; aoff; x "head 0 0 0 s 0.4"; waitfor 'PRACTICE t=.*arrived|the blink down' 8
  dml 5; sleep 2; echo "$(date +%T) after: $(root)"; w "climb dropmark 300" 0.5; }
spireatt() { local un=$1 n0 y
  echo "-- the spire, unstuck $un"
  x "btn R squeeze 0"; w "hands test off"; w "climb unstuck $un"
  go "$TOWER"; y=$(root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2 | cut -d. -f1)
  [ "${y:-0}" -lt 3600 ] && { echo "(fell off: the top not streamed yet; again)"; go "$TOWER"; }
  y=$(root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2 | cut -d. -f1)
  [ "${y:-0}" -lt 3600 ] && { echo "$(date +%T) not on the tower top ($(root)): skipped"; return 1; }
  faceyaw "$SPIRE_YAW"; probe; echo "nearest: $(near +0)"; w "stick 0 1 0 1.5" 2; echo "$(date +%T) at: $(root)"
  grabat R -45 40 || { echo "$(date +%T) no grab on the spire"; probe; return 1; }
  grep -E '^CLIMB t=.* grab R:' "$L" | tail -1 | cut -c1-260
  n0=$(cnt 'fling (launch|none)'); w "hands swing R 20 -110 $GZ 0 0 0 140" 0.17; w "hands squeeze R 0" 2.5
  grep -E '^CLIMB t=.* fling' "$L" | tail -3 | cut -c1-300; echo "$(date +%T) after: $(root)"; d "spire-$un"
  w "hands test R 20 -20 45 0 0 0" 0.5; w "hands squeeze R 1" 1; grep -E '^CLIMB t=.* (grab R|press R)' "$L" | tail -1 | cut -c1-200
  w "hands squeeze R 0" 0.5; w "hands test off"
  climbing && { aon; sleep 0.6; aoff; waitfor 'PRACTICE t=.*arrived|drop mark: A let go' 8; dml 2; }; }
spireplan() { echo "== spire: mark 5's"; spireatt off; spireatt on; }
ladderatt() { local rl=$1 k
  echo "-- the ladder wall, roundlip $rl"
  x "btn R squeeze 0"; w "hands test off"; w "climb roundlip $rl"
  go "$LADDER_AT"; faceyaw "$LADDER_YAW"; echo "$(date +%T) at: $(root)"
  probe; echo "nearest: $(near +0)"
  UP_REACH=40 upface R 30 2240; echo "upface: top ${TY:-none} by ${TOP:-none}"
  grep -E '^CLIMB t=.* (grab [LR]|pullover|round lip|mantle|through)' "$L" | tail -8 | cut -c1-260
  if climbing; then
    local H=${TOP:-R} HX; [ "$H" = L ] && HX=-20 || HX=20
    w "hands swing $H $HX -60 ${GZ:-40} 0 0 0 2000" 2.6; echo "$(date +%T) pulled: $(root)"
    grep -E '^CLIMB t=.* (pullover|mantle|through|top:)' "$L" | tail -4 | cut -c1-280; d "ladder-$rl"
    grep -q 'pullover: .*armed\|mantle: at the top' <(tail -40 "$L") && echo "(a ring is up)"
    w "hands swing $H $HX -95 ${GZ:-40} 0 0 0 1500" 2.2; grep -E '^CLIMB t=.* (pullover|end:|mantle)' "$L" | tail -3 | cut -c1-260
    climbing && { tapA 0; ams 3; }
    w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.8; w "hands test off"
    climbing && { aon; sleep 0.6; aoff; sleep 2; }
  fi; echo "$(date +%T) after: $(root)"; }
ladderplan() { echo "== ladder: mark 11's wall"; ladderatt off; ladderatt on; }
# from mantle20-drive.sh (its plan functions are not sourced)
tapA() { local k n0
  n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  sleep 0.1; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 1.2; }
ams() { grep -E '^CLIMB t=.* (mantle|drop mark|end:|stand:|hang:)' "$L" | tail -${1:-4} | cut -c1-360; }
upface() { local first=$1 y0=$2 tmin=$3 k HOLD HX O OX REACH=${UP_REACH:-40} GOT TYv
  TOP=; TY=
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
  grabat "$first" "$y0" $REACH || { echo "$(date +%T) no first grab"; return 1; }
  HOLD=$first
  for k in $(seq 1 12); do
    climbing || { echo "$(date +%T) not climbing"; fl 3; return 1; }
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    w "hands swing $HOLD $HX -45 $GZ 0 0 0 1000" 1.5
    GOT=
    for y in 35 10 -15; do grabat $O $y $REACH && { GOT=$y; break; }; done
    [ -n "$GOT" ] || { echo "$(date +%T) no grab $O"; fl 3; return 1; }
    w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX -40 10 0 0 0" 0.4
    TYv=$(grep -E "^CLIMB t=.* grab $O:" "$L" | tail -1 | grep -oE 'edge 1, top [0-9]+' | grep -oE '[0-9]+$')
    echo "$(date +%T) pull $k ($O): $(root) top ${TYv:--}"
    if [ -n "$TYv" ] && [ "$TYv" -ge "$tmin" ]; then TOP=$O; TY=$TYv; break; fi
    HOLD=$O
  done
  [ -n "$TOP" ] || { echo "$(date +%T) no top"; return 1; }; }
card
for p in ${PLAN:-down spire ladder}; do [ "$p" = down ] && downplan; [ "$p" = spire ] && spireplan
  [ "$p" = ladder ] && ladderplan; [ "$p" = ledge ] && SPOT="$LEDGE" SPOT_YAW="$LEDGE_YAW" downplan; done
grep -E '^CLIMB END build 21' "$L" | tail -1 | cut -c1-600
echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
