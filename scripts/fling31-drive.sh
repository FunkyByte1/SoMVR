#!/bin/bash
# fling31-drive.sh - build 31 (som-b31-climb, docs/re-xr-pipeline.md 138): the fling down and the sword while hanging,
# on real walls with scripted hands held as a real hand holds a hold (the grip's -Z pinky -> thumb along the wall) -
# pin16/climb2-drive.sh's functions (sourced), down22-drive.sh's ways (copied). PLAN (default "ladder fort"):
#   ladder  the meadow ladder's wall (3.2 m, its top 2265): from its top the left hand reaching down over the edge =
#           the glide down to hang by it (facing out, the meadow 1.3 m under the feet); `climb flingdown off` (build
#           30): the left hand thrown up and let go = the hang; back up, on: the same throw = the fling down onto the
#           meadow (landed). Back up, an unaware infantry 1 m out on the meadow below (`above far 100`, `practice far`),
#           down to hang again; `climb sword off` (build 30): the right hand's press at its shoulder slot draws
#           nothing; on: it draws; a chop down at the orc = a COMBAT hit
#   fort    the Sea Fort camp's east wall (9 m, plumb; its lip 25522 3547 2000, out +x): down over it, turned to the
#           face; two holds down hand over hand slowly; a slow push and let go (both open) = the hang (too slow); the
#           end of a pull up (the hand already rising at the let-go) = the hang (the end of a pull up); flingdown off: a
#           hard throw hangs; on: a throw caught by the other hand, a throw to rest, throws to the bottom (landed)
# Every throw with `climb trace` (Talion's root each tick) and both eyes' dumps (fling31-steps.txt). Stops at once
# (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
ST=$R/fling31-steps.txt
LAD=${LAD:-31692 2285 -29022}
LAD_YAW=${LAD_YAW:-33}
FORT=${FORT:-25500 3649 2000}
FORT_YAW=${FORT_YAW:-90}
SLOT_R="hands test R 10 -10 -15 0 0 0"    # the sword's slot over the right shoulder (SOMVR_SLOTS' 10,-10,-15)
OUT_R="hands test R 30 -25 40 0 -90 0"    # out of it, the blade turned along the forearm
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.7 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; sleep 0.5; done; }
faceyaw() { local want=$1 k y d s d0
  for k in $(seq 1 ${YAW_TRIES:-16}); do
    y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le ${YAW_TOL:-20} ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    if [ -z "$s" ]; then [ "$d" -gt 0 ] && s=+ || s=-
    elif [ "${d#-}" -gt "${d0#-}" ]; then [ $s = + ] && s=- || s=+; fi
    d0=$d; w "turn$s" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
dumpsn() { local k f; for k in $(seq 1 "$3"); do printf 'dump\n' > "$R/xrtest.txt"; sleep "$2"; done; sleep 1
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | grep -o '[0-9]*$' | uniq | tail -"$3" | tr '\n' ' ')
  echo "$1 $f" | tee -a "$ST"; }
dd() { local f; printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$ST"; }
fll() { grep -E '^CLIMB t=.* (fling|end:|hang:|resume:|grab [LR]:|start:|down:)' "$L" | tail -${1:-6} |
  sed -E 's/frame=[0-9]+ //' | cut -c1-360; }
ry() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2; }
grabat() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.5
  for k in 1 2 3 4 5; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.25; done
    w "hands squeeze $h 0" 0.4; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.4
  done; return 1; }
# the path of the last throw: Talion's root y each player tick from the trace (t y), into the steps file
path() { local n=$1; grep -E '^CLIMB trace t=.*: root' "$L" | tail -n +$((n + 1)) | head -80 |
  sed -E 's/^CLIMB trace t=([0-9.]+) frame=[0-9]+: root ([-0-9.]+),([-0-9.]+),([-0-9.]+) .*/\1 \2 \3 \4/' > "$R/path-$2.txt"
  echo "$(date +%T) $2: path $(wc -l < "$R/path-$2.txt") ticks (path-$2.txt): $(awk 'NR==1{y0=$3} {y=$3} END{printf "y %.1f -> %.1f", y0, y}' "$R/path-$2.txt")"; }
# a throw by the holding hand H: up from its pose to Y over MS, let go at OPEN (CATCH: the other hand grips then)
throw() { local H=$1 Y=$2 MS=$3 OPEN=$4 CATCH=$5 name=$6 xs n0; [ "$H" = L ] && xs=-20 || xs=20
  n0=$(cnt '^CLIMB trace t=.*: root'); w "climb trace 5" 0.6
  w "hands swing $H $xs $Y ${GZ:-35} 0 0 0 $MS $OPEN${CATCH:+ $CATCH}" "$(python3 -c "print($MS*$OPEN/1000+0.2)")"
  dumpsn "$name" 0.3 5; sleep 2
  fll 5; path "$n0" "$name"; }
unclimb() { climbing && { w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.5; aon; sleep 1.2; aoff; sleep 2; }; }
aon() { local n0 k; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done; }
aoff() { printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 0.6; }
# from a top facing out: hand H reaching down over the edge = the glide down to hang by it; 0 = hanging
over() { local H=$1 n1 z; n1=$(cnt '^CLIMB t=.* down: the glide.s end')
  for z in ${OVER_Z:-60 45 75}; do
    w "hands squeeze $H 0" 0.5; w "hands test $H 0 -115 $z 0 0 0" 0.7; w "hands squeeze $H 1" 0.4
    waitfor 'down: the glide.s end' 4 && break; fll 1
  done; dumpsn "$2-hang" 0.3 2; fll 3
  [ "$(cnt '^CLIMB t=.* down: the glide.s end')" -gt "$n1" ] && climbing && { echo "$(date +%T) $2: hanging $(root)"; return 0; }
  echo "$(date +%T) $2: no hang by the glide"; return 1; }
orcat() { grep -E '^PRACTICE t=.*spawn check [12]' "$L" | tail -1 | grep -oE 'at \([-0-9. ]+\)' | tr -d '()' | awk '{print $2,$3,$4}'; }
rootxyz() { root | grep -oE 'root [0-9.,-]+' | cut -d' ' -f2 | tr ',' ' '; }
# turn to the orc (its logged root) and chop along its way: the hand 45 % of the way (at most 55 cm out), the blade at it
aimcut() { local ox oy oz tx ty tz yw d dy hy hz rx hd hx
  read -r ox oy oz < <(orcat); read -r tx ty tz < <(rootxyz)
  [ -n "$ox" ] && [ -n "$tx" ] || { echo "$(date +%T) $1: no orc or no root"; return 1; }
  read -r yw d dy < <(python3 -c "import math; dx=$ox-$tx; dz=$oz-$tz; print(round(math.degrees(math.atan2(dx,dz))), round(math.hypot(dx,dz)), round(($oy+30)-($ty+70)))")
  echo "$(date +%T) $1: the orc $d cm away (level) at yaw $yw, its chest $dy cm from the eyes" | tee -a "$ST"
  if [ "${YAW_TOL:-12}" -ge 90 ]; then hd=${HEAD_YAW:-$(yaw)}; else YAW_TOL=${YAW_TOL:-12} faceyaw "$yw" >/dev/null; hd=$(yaw); fi
  # the chop's plane shifted sideways by what the turn left (the orc's offset in the head's yaw frame: right +)
  read -r hx hy hz rx < <(python3 -c "import math; a=math.radians($yw-${hd:-$yw}); d=$d; ty=$dy; lat=d*math.sin(a); ah=d*math.cos(a); hz=min(55,ah*0.45); hy=max(-100,min(0,-25+(ty+25)*0.45)); print(round(max(-40,min(45,lat))),round(hy),round(hz),round(max(-10,min(85,math.degrees(math.atan2(hy-ty,max(ah-hz,5)))))))")
  echo "$(date +%T) $1: the head at $hd: the chop at x $hx, to $hy $hz, the blade $rx deg down" | tee -a "$ST"
  w "hands test R $hx 5 25 -80 0 0" 0.45
  w "hands swing R $hx $hy $hz $rx 0 0 ${CHOP_MS:-160}" 0.3; dumpsn "$1" 0.25 3; sleep 0.5
  grep -E '^COMBAT (hit|swing)' "$L" | tail -3 | cut -c1-300; }
PASS() { echo "$(date +%T) PASS $*" | tee -a "$ST"; }
FAIL() { echo "$(date +%T) FAIL $*" | tee -a "$ST"; }
ladder() { local n0 h0 k
  echo "== ladder: the fling down to the meadow, then the sword at an unaware orc below"
  for on in off on; do
    x "btn R squeeze 0"; w "hands test off"; go "$LAD"; faceyaw "$LAD_YAW"; echo "$(date +%T) at: $(root)"
    over L "lad-$on" || continue; dd "lad-$on-hang"
    w "climb flingdown $on" 0.3
    w "climb jump 25" 0.3; w "hands test L -20 -45 30 0 0 0" 0.6 # (the pose moved while holding: a jump, ignored)
    w "climb jump 60" 0.3; GZ=30; n0=$(cnt '^CLIMB t=.* fling down landed'); n1=$(cnt '^CLIMB t=.* fling down launch')
    throw L 45 300 0.75 "" "lad-throw-$on"
    if [ "$on" = off ]; then climbing && [ "$(cnt '^CLIMB t=.* fling down launch')" -eq "$n1" ] &&
        PASS "ladder off (build 30): the throw = the hang" || FAIL "ladder off: not the hang"
    else [ "$(cnt '^CLIMB t=.* fling down landed')" -gt "$n0" ] && ! climbing && PASS "ladder: the fling down landed on the meadow" ||
        FAIL "ladder: no landing"; fi
    dd "lad-$on-after"; echo "$(date +%T) after: $(root)"; w "hands test off" 0.3; unclimb
  done
  echo "-- the sword while hanging"
  x "btn R squeeze 0"; w "hands test off"; go "$LAD"; faceyaw "$LAD_YAW"
  over L lad-sw || { FAIL "sword: no hang"; return 1; }
  w "climb sword off" 0.5; n0=$(cnt '^SLOTS t=.* sword: drawn')
  w "$SLOT_R" 0.7; w "hands squeeze R 1" 0.9; w "hands squeeze R 0" 0.5; w "$OUT_R" 0.5
  [ "$(cnt '^SLOTS t=.* sword: drawn')" -gt "$n0" ] && FAIL "sword off: drawn" || PASS "sword off (build 30): the slot's press while hanging draws nothing"
  w "climb sword on" 0.5; n0=$(cnt 'drawn while climbing')
  w "$SLOT_R" 0.7; w "hands squeeze R 1" 0.9; w "$OUT_R" 0.6; w "hands squeeze R 0" 0.5
  [ "$(cnt 'drawn while climbing')" -gt "$n0" ] && PASS "sword on: drawn while hanging" || FAIL "sword on: not drawn"
  grep -E '^SLOTS t=' "$L" | tail -3 | cut -c1-260; dd lad-sword
  h0=$(cnt '^COMBAT hit tier=')
  w "above far ${FAR_CM:-150}" 0.6; w "practice far infantry" 1.8
  grep -E '^PRACTICE t=.*spawn far' "$L" | tail -1 | grep -oE 'at \([-0-9. ]+\) [0-9]+ cm ahead'
  for k in 1 2 3; do
    climbing || break
    aimcut "lad-cut$k"
    [ "$(cnt '^COMBAT hit tier=')" -gt "$h0" ] && { PASS "sword: a hit on the orc while hanging (cut $k)"; break; }
  done
  [ "$(cnt '^COMBAT hit tier=')" -gt "$h0" ] || FAIL "sword: no hit"
  grep -E '^COMBAT hit tier=' "$L" | tail -1 | cut -c1-300
  echo "$(date +%T) after the cuts: $(root)"; fll 3; w "hands test off" 0.3; unclimb; w "practice remove" 1; }
fort() { local k y0 n0
  echo "== fort: the 9 m wall"
  w "climb jump 60" 0.3; w "climb flingdown on" 0.3
  x "btn R squeeze 0"; w "hands test off"; go "$FORT"; faceyaw "$FORT_YAW"; echo "$(date +%T) at: $(root)"; dd fort-top
  OVER_Z="42 30 55" over R fort || { FAIL "fort: no hang"; return 1; }
  read -r ox oz < <(grep -E '^CLIMB t=.* down: the R hand.s grip' "$L" | tail -1 | grep -oE 'out [-0-9.]+ [-0-9.]+' | cut -d' ' -f2,3)
  faceyaw "$(python3 -c "import math; print(round(math.degrees(math.atan2(-float('${ox:-1}'), -float('${oz:-0}')))))")"; dd fort-face
  echo "-- slowly hand over hand (two holds), then a slow push and let go"
  HOLD=R
  for k in 1 2; do
    [ "$HOLD" = L ] && { O=R; OX=20; HX=-20; } || { O=L; OX=-20; HX=20; }
    grabat $O -60 35 || { echo "no grab $O"; break; }
    w "hands squeeze $HOLD 0" 0.4; w "hands test $HOLD $HX -40 10 0 0 0" 0.3
    w "hands swing $O $OX -5 $GZ 0 0 0 1200" 1.6; echo "$(date +%T) slow $k ($O): $(root)"; HOLD=$O
  done
  [ "$HOLD" = L ] && { O=R; OX=20; } || { O=L; OX=-20; }
  w "hands test $O $OX -40 10 0 0 0" 0.3 # the other hand off the wall
  n0=$(cnt '^CLIMB t=.* fling (down )?launch'); w "hands swing $HOLD $([ $HOLD = L ] && echo -20 || echo 20) 55 $GZ 0 0 0 800 0.6" 2.2
  fll 3; [ "$(cnt '^CLIMB t=.* fling (down )?launch')" -eq "$n0" ] && climbing && PASS "fort: slow hand over hand and a slow let-go: no fling" ||
    FAIL "fort: a slow descent flung"
  echo "-- the end of a pull up (the hand rising at the let-go)"
  grabat R 30 35 || echo "no grab R"; w "hands squeeze L 0" 0.3; w "hands test L -20 -40 10 0 0 0" 0.3
  w "hands swing R 20 -50 $GZ 0 0 0 500" 0.9
  n0=$(cnt '^CLIMB t=.* fling (down )?launch'); w "hands swing R 20 -12 $GZ 0 0 0 150 0.85" 1.5; fll 3
  [ "$(cnt '^CLIMB t=.* fling (down )?launch')" -eq "$n0" ] && grep -E '^CLIMB t=.* fling none R' "$L" | tail -1 | grep -q 'end of a pull up' &&
    PASS "fort: the end of a pull up = the hang" || FAIL "fort: the end of a pull up flung or not judged so"
  echo "-- flingdown off (build 30): a hard throw"
  w "climb flingdown off" 0.3; grabat R -40 35 || echo "no grab R"; w "hands squeeze L 0" 0.3; w "hands test L -20 -70 10 0 0 0" 0.3
  y0=$(ry); throw R 60 400 0.6 "" fort-off
  climbing && ! grep -E '^CLIMB t=.* fling down launch' "$L" | tail -1 | grep -q . && PASS "fort off: the throw = the hang" || FAIL "fort off"
  w "climb flingdown on" 0.3
  echo "-- a throw caught by the left hand"
  grabat R -40 35 || echo "no grab R"; w "hands squeeze L 0" 0.3; w "hands test L -20 -70 $GZ 0 0 0" 0.3
  n0=$(cnt '^CLIMB t=.* fling down catch'); throw R 60 400 0.6 1.0 fort-catch
  [ "$(cnt '^CLIMB t=.* fling down catch')" -gt "$n0" ] && PASS "fort: the fling down caught by the left hand" || FAIL "fort: no catch"
  echo "-- a throw to rest"
  w "hands squeeze L 0" 0.3; w "hands test L -20 -70 10 0 0 0" 0.3; grabat R -40 35 || echo "no grab R"; w "hands squeeze L 0" 0.3
  w "hands test L -20 -70 10 0 0 0" 0.3
  n0=$(cnt '^CLIMB t=.* fling down hang \(at rest\)'); throw R 60 400 0.6 "" fort-rest
  [ "$(cnt '^CLIMB t=.* fling down hang \(at rest\)')" -gt "$n0" ] && PASS "fort: the fling down to rest (the hang)" || FAIL "fort: not at rest"
  echo "-- hard throws to the bottom"
  for k in 1 2 3 4 5; do
    climbing || break
    grabat R -60 35 || { echo "no grab R"; break; }; w "hands squeeze L 0" 0.3; w "hands test L -20 -70 10 0 0 0" 0.3
    n0=$(cnt '^CLIMB t=.* fling down landed'); throw R 60 300 0.6 "" "fort-hard$k"
    [ "$(cnt '^CLIMB t=.* fling down landed')" -gt "$n0" ] && { PASS "fort: landed at the foot of the wall (throw $k)"; break; }
  done
  climbing && FAIL "fort: still on the wall after the throws"; dd fort-foot; echo "$(date +%T) at the foot: $(root)"
  w "hands test off" 0.3; unclimb; }
# build 31 boot 3: the user's own 7 m wall (the build 30 session's descent at 31005 -30450: 1 m a push, nothing in the way)
WALL=${WALL:-30992 3700 -30443}
WALL_YAW=${WALL_YAW:--150}
wall() { local k n0 HOLD O OX HX y0
  echo "== wall: up by hand, then the throws down"
  w "climb jump 60" 0.5; w "climb flingdown on" 0.5; w "climb sword on" 0.5
  x "btn R squeeze 0"; w "hands test off"; go "$WALL"; YAW_TOL=12 faceyaw "$WALL_YAW"; echo "$(date +%T) at the foot: $(root)"; dd wall-foot
  HOLD=
  for k in 1 2 3 4 5 6 7; do
    [ "$HOLD" = L ] && { O=R; OX=20; HX=-20; } || { O=L; OX=-20; HX=20; }
    grabat $O 40 35 || { echo "$(date +%T) up $k: no grab $O"; break; }
    [ -n "$HOLD" ] && { w "hands squeeze $HOLD 0" 0.5; w "hands test $HOLD $HX -40 10 0 0 0" 0.5; }
    w "hands swing $O $OX -40 $GZ 0 0 0 800" 1.3; echo "$(date +%T) up $k ($O): $(root)"; HOLD=$O
  done
  fll 4; dd wall-up
  # the right hand to hold for the throws (the left off the wall, open)
  [ "$HOLD" = R ] || { grabat R -40 35; w "hands squeeze L 0" 0.5; }
  w "hands test L -20 -70 10 0 0 0" 0.5
  echo "-- flingdown off (build 30): a hard throw"
  w "climb flingdown off" 0.5; grabat R -40 35 || echo "no grab R"; w "hands squeeze L 0" 0.5; w "hands test L -20 -70 10 0 0 0" 0.5
  n0=$(cnt '^CLIMB t=.* fling down launch'); throw R 60 400 0.6 "" wall-off
  climbing && [ "$(cnt '^CLIMB t=.* fling down launch')" -eq "$n0" ] && PASS "wall off: the throw = the hang (no flight)" || FAIL "wall off"
  w "climb flingdown on" 0.5
  echo "-- a throw caught by the left hand"
  grabat R -40 35 || echo "no grab R"; w "hands squeeze L 0" 0.5; w "hands test L -20 -70 $GZ 0 0 0" 0.5
  n0=$(cnt '^CLIMB t=.* fling down catch'); throw R 60 400 0.6 1.0 wall-catch
  [ "$(cnt '^CLIMB t=.* fling down catch')" -gt "$n0" ] && PASS "wall: the fling down caught by the left hand" || FAIL "wall: no catch"
  echo "-- a throw to rest"
  grabat R -40 35 || echo "no grab R"; w "hands squeeze L 0" 0.5; w "hands test L -20 -70 10 0 0 0" 0.5
  n0=$(cnt '^CLIMB t=.* fling down hang \(at rest\)'); throw R 60 400 0.6 "" wall-rest
  [ "$(cnt '^CLIMB t=.* fling down hang \(at rest\)')" -gt "$n0" ] && PASS "wall: the fling down to rest (the hang)" || FAIL "wall: not at rest"
  echo "-- hard throws to the bottom"
  for k in 1 2 3 4; do
    climbing || break
    grabat R -60 35 || { echo "no grab R"; break; }; w "hands squeeze L 0" 0.5; w "hands test L -20 -70 10 0 0 0" 0.5
    n0=$(cnt '^CLIMB t=.* fling down landed'); throw R 60 300 0.6 "" "wall-hard$k"
    [ "$(cnt '^CLIMB t=.* fling down landed')" -gt "$n0" ] && { PASS "wall: landed at the foot (throw $k)"; break; }
  done
  climbing && FAIL "wall: still on the wall after the throws"; dd wall-landed; echo "$(date +%T) at the foot: $(root)"
  # (build 31 suite: the sword part moved to swordlow - this one spawned the orc before the hang and left the sword drawn)
  w "hands test off" 0.5; climbing && unclimb; }
# boot 4: the sword low on the user's wall - hanging and drawn first, then the far orc (it comes to him), the cut at once
# the sword on the shoulder before a test (a plan before may have left it drawn: the suite's run 155156) - on foot, the
# slot's press puts it back
sheathed() { local l; l=$(grep -E '^SLOTS t=.* sword: (done; now|draw asked; now|put-back asked; now|the game put it away)' "$L" | tail -1)
  [ -z "$l" ] && return 0; ! printf '%s\n' "$l" | grep -qE 'now in the hand|kept in the hand'; }
swordlow() { local k n0 h0
  echo "== swordlow: hanging low, the sword drawn, then the orc"
  w "climb sword on" 0.5; x "btn R squeeze 0"; w "hands test off"; go "$WALL"
  if ! sheathed; then echo "$(date +%T) the sword is in the hand: back to the shoulder first (on foot)"
    w "$SLOT_R" 0.7; w "hands squeeze R 1" 0.9; w "$OUT_R" 0.6; w "hands squeeze R 0" 0.5; w "hands test off" 0.5
    sheathed && echo "$(date +%T) sheathed" || echo "$(date +%T) still in the hand?"; fi
  YAW_TOL=12 faceyaw "$WALL_YAW"
  grabat L -20 35 || { FAIL "swordlow: no grab"; return 1; }
  w "hands swing L -20 -50 $GZ 0 0 0 500" 1.0; echo "$(date +%T) hanging low: $(root)"; dd swl-hang
  n0=$(cnt 'drawn while climbing')
  w "$SLOT_R" 0.7; w "hands squeeze R 1" 0.9; w "$OUT_R" 0.6; w "hands squeeze R 0" 0.5
  [ "$(cnt 'drawn while climbing')" -gt "$n0" ] && PASS "swordlow: the sword drawn while hanging" || FAIL "swordlow: not drawn"
  grep -E '^SLOTS t=.*(drawn while climbing|while climbing)' "$L" | tail -2 | cut -c1-220; dd swl-sword
  h0=$(cnt '^COMBAT hit tier=')
  YAW_TOL=15 faceyaw "${ORC_YAW:-117}" # where the game put the orc at this spot in boots 3-5 (112, 117, 119)
  HEAD_YAW=$(yaw)
  w "hands test R 20 5 25 -80 0 0" 0.5 # the blade raised before the orc comes
  local e0 c1; e0=$(cnt '^CLIMB t=.* end:'); c1=$(cnt '^PRACTICE t=.*spawn check 1')
  w "above far 100" 0.5; w "practice far infantry" 0.3
  for k in $(seq 1 12); do [ "$(cnt '^PRACTICE t=.*spawn check 1')" -gt "$c1" ] && break; sleep 0.25; done
  grep -E '^PRACTICE t=.*spawn (far|check 1)' "$L" | tail -2 | cut -c1-220
  for k in 1 2 3; do
    [ "$(cnt '^CLIMB t=.* end:')" -gt "$e0" ] && { echo "$(date +%T) the climb ended: $(grep -E '^CLIMB t=.* end:' "$L" | tail -1 | cut -c1-160)"; break; }
    YAW_TOL=90 aimcut "swl-cut$k"
    if [ "$(cnt '^COMBAT hit tier=')" -gt "$h0" ]; then
      # while hanging = the hit's line before any end of this climb
      hl=$(grep -nE '^COMBAT hit tier=' "$L" | tail -1 | cut -d: -f1); el=$(grep -nE '^CLIMB t=.* end:' "$L" | sed -n "$((e0 + 1))p" | cut -d: -f1)
      if [ -z "$el" ] || [ "$hl" -lt "$el" ]; then PASS "sword: a hit on the orc while hanging (cut $k; the climb on at the hit)"
      else FAIL "sword: the hit came after the hang had ended (line $hl > $el)"; fi; break; fi
  done
  [ "$(cnt '^COMBAT hit tier=')" -gt "$h0" ] || FAIL "sword: no hit"
  grep -E '^COMBAT hit tier=' "$L" | tail -2 | cut -c1-300; fll 3; echo "$(date +%T) after: $(root)"
  w "hands test off" 0.5; unclimb; w "practice remove" 1; }
card
w "climb unstuck on"; w "climb down over"
for p in ${PLAN_B31:-ladder fort}; do $p; done
grep -E '^CLIMB END build 31' "$L" | tail -1 | cut -c1-700
grep -E '^(PASS|FAIL)' "$ST" 2>/dev/null; grep -E ' (PASS|FAIL) ' "$ST" | cut -c1-200
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
