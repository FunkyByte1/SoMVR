#!/bin/bash
# move33-drive.sh - build 33 (som-b33-move, docs/re-xr-pipeline.md 143): the user's 11-03 session's places (run
# logs/boot-check-20261103-093210, marks 22, 24-26) with `climb trace` (Talion's root, the proxy and its contacts each
# tick) - pin16/climb2-drive.sh's functions (sourced). PLAN_B33 (default "lip coping arch ledge"):
#   lip     mark 24: on the long wall's coping at 31215 4710 -30690, walking east (heading 70) into the spot the session's
#           walk stopped at (31236 4816 -30685: asked 197 cm, the body 2 cm); then back west and east again
#   coping  mark 25: at 31400 4716 -30640, walking east along the coping (heading 81)
#   arch    mark 26: standing in the archway's opening (32060 4216 -30470), the far wall's face 75 cm off at heading
#           -132: the right hand on it at the eyes' height, a pull down (the body up 60 cm) - where the session's pull put
#           the capsule 30 cm into the rock
#   ledge   mark 22: on the wall's top at 30944 4708 -30659 (heading 18, the edge ~75 cm ahead): the right hand pressed
#           out at the chest as the user did (126-141 cm over the floor), then reached down over the edge
# Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
ST=$R/move33-steps.txt
PASS() { echo "$(date +%T) PASS $*" | tee -a "$ST"; }
FAIL() { echo "$(date +%T) FAIL $*" | tee -a "$ST"; }
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.7 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; sleep 0.5; done; }
faceyaw() { local want=$1 k y d s d0
  for k in $(seq 1 ${YAW_TRIES:-16}); do
    y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le ${YAW_TOL:-10} ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    if [ -z "$s" ]; then [ "$d" -gt 0 ] && s=+ || s=-
    elif [ "${d#-}" -gt "${d0#-}" ]; then [ $s = + ] && s=- || s=+; fi
    d0=$d; w "turn$s" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
dd() { local f; printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | grep -o '[0-9]*$' | tail -1); echo "$1 $f" | tee -a "$ST"; }
# a walk forward along the head for S seconds with the trace on: the root's way, the contacts at the stops
walk() { local name=$1 s=${2:-2.5} n0 r0 r1; n0=$(cnt '^CLIMB trace t=.*: root'); r0=$(root)
  w "climb trace $((${s%.*} + 2))" 0.5; w "stick 0 1 0 $s" "$s"; sleep 1.2; r1=$(root)
  grep -E '^CLIMB trace t=.*: root' "$L" | tail -n +$((n0 + 1)) > "$R/trace-$name.txt"
  echo "$(date +%T) $name: $r0 -> $r1 ($(wc -l < "$R/trace-$name.txt") ticks)" | tee -a "$ST"
  awk '{for(i=1;i<=NF;i++) if($i=="moved") m=$(i+1); if (m=="0.0,0.0,0.0") s++} END{print "  ticks that did not move: " s+0}' "$R/trace-$name.txt"
  grep -oE 'moved [-0-9.,]+ asked [-0-9.,]+.*contacts [0-9]+:.*' "$R/trace-$name.txt" | awk 'NR%8==1' | head -8 | cut -c1-260
  grep -E '^LOCO t=.*last 1 s' "$L" | tail -2 | sed -E 's/.*asked ([0-9.]+) cm, the body ([0-9.]+) cm.*/  LOCO asked \1 cm, the body \2 cm/'; }
lip() { echo "== lip (mark 24)"; x "btn R squeeze 0"; w "hands test off"; go "31215 4710 -30690"; faceyaw 70; dd lip-at
  walk lip-e1 2.5; dd lip-stop1; w "vault edges 150" 1.5; grep -E '^(VAULT|JUMP).*(WALL|OVERHANG|edge)' "$L" | tail -6 | cut -c1-220
  faceyaw 250; walk lip-w 1.5; faceyaw 70; walk lip-e2 3; dd lip-stop2; }
coping() { echo "== coping (mark 25)"; x "btn R squeeze 0"; w "hands test off"; go "31400 4716 -30640"; faceyaw 81; dd cop-at
  walk cop-e1 3; dd cop-stop; faceyaw 261; walk cop-w 2; }
arch() { local n0; echo "== arch (mark 26)"; x "btn R squeeze 0"; w "hands test off"; go "32060 4216 -30470"; echo "$(date +%T) at: $(root)"
  faceyaw -132; dd arch-at; w "climb trace 6" 0.5
  n0=$(cnt '^CLIMB t=.* grab R:'); w "hands squeeze R 0" 0.5; w "hands test R 20 0 60 0 0 0" 0.7; w "hands squeeze R 1" 1.0
  [ "$(cnt '^CLIMB t=.* grab R:')" -gt "$n0" ] && grep -E '^CLIMB t=.* grab R:' "$L" | tail -1 | cut -c1-260 || echo "$(date +%T) arch: no grab"
  w "hands swing R 20 -60 60 0 0 0 1200" 1.8; dd arch-pull; echo "$(date +%T) arch after the pull: $(root)"
  grep -E '^(UNPIN|HEADROCK).*' "$L" | tail -3 | cut -c1-240
  grep -E '^CLIMB trace t=.*: root' "$L" | tail -40 > "$R/trace-arch.txt"; grep -oE 'contacts [0-9]+:.*' "$R/trace-arch.txt" | awk 'NR%5==1' | head -8 | cut -c1-200
  w "hands squeeze R 0" 0.5; w "hands test off" 0.5; climbing && { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.3; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 2; }; }
ledge() { local n0; echo "== ledge (mark 22)"; x "btn R squeeze 0"; w "hands test off"; go "30944 4708 -30659"; faceyaw 18; dd ledge-at
  n0=$(cnt '^CLIMB t=.* (press R|down:)'); w "hands test R 20 -40 55 0 0 0" 0.7; w "hands squeeze R 1" 1.2
  grep -E '^CLIMB t=.* (press R|down:|grab R)' "$L" | tail -n +$((n0 + 1)) | head -3 | cut -c1-260; w "hands squeeze R 0" 0.5
  climbing && { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.3; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 2; }
  w "stick 0 1 0 1.5" 2; echo "$(date +%T) walked to the edge: $(root)"; dd ledge-edge
  n0=$(cnt '^CLIMB t=.* (press R|down:)'); w "hands test R 20 -40 55 0 0 0" 0.7; w "hands squeeze R 1" 1.2
  grep -E '^CLIMB t=.* (press R|down:|grab R)' "$L" | tail -n +$((n0 + 1)) | head -3 | cut -c1-260; w "hands squeeze R 0" 0.5
  w "hands test off" 0.5; climbing && { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.3; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 2; }; }
# build 33 proof: the lip steps (the walk passes where boot 112305's stopped)
lipp() { local n0 x; echo "== lip proof (mark 24)"; x "btn R squeeze 0"; w "hands test off"; go "31215 4710 -30690"; faceyaw 70
  n0=$(cnt '^WALK lip t=.*stepped'); walk lipp-e1 3; dd lipp-after
  x=$(root | grep -oE 'root [0-9.]+' | cut -d' ' -f2); grep -E '^WALK lip' "$L" | tail -2 | cut -c1-300
  [ "$(cnt '^WALK lip t=.*stepped')" -gt "$n0" ] && python3 -c "import sys; sys.exit(0 if float('${x:-0}') > 31245 else 1)" &&
    PASS "lip: stepped onto the coping's lip, the walk on past it (x ${x})" || FAIL "lip: still stopped (x ${x})"
  faceyaw 250; walk lipp-w 3; grep -E '^WALK lip' "$L" | tail -1 | cut -c1-300
  # boot 151958's way: north along the bevel (held, its normal beside the walk)
  go "31215 4710 -30690"; faceyaw 5; n0=$(cnt '^WALK lip t=.*the step over'); walk lipp-n 3
  grep -E '^(WALK lip|JUMP t=.*(step over|landed))' "$L" | tail -3 | cut -c1-260
  [ "$(cnt '^WALK lip t=.*the step over')" -gt "$n0" ] && PASS "lip north: the step over the bevel" || echo "$(date +%T) lip north: no step (see the walk)"; }
copp() { local n0; echo "== coping proof (mark 25)"; x "btn R squeeze 0"; w "hands test off"; go "32003 4718 -30577"; faceyaw 233
  n0=$(cnt '^WALK lip t=.*stepped'); walk copp-w 3; grep -E '^WALK lip' "$L" | tail -2 | cut -c1-300
  [ "$(cnt '^WALK lip t=.*stepped')" -gt "$n0" ] && PASS "coping: stepped over its lip" || echo "$(date +%T) coping: no lip step (see the walk)"; }
# the way down by a hand reached out at the chest (a plumb 11 m face, from `vault edges`: the lip at 30859 4708 -31386, out 90)
ledge2() { local n0 n1; echo "== ledge2: the reach-out way down (mark 22)"; x "btn R squeeze 0"; w "hands test off"; go "30826 4708 -31386"
  faceyaw 90; echo "$(date +%T) at: $(root)"; dd ledge2-at
  n0=$(cnt '^CLIMB t=.* down: the R hand.s grip'); n1=$(grep -c 'down: the glide.s end' "$L")
  w "hands squeeze R 0" 0.5; w "hands test R 15 -40 60 0 0 0" 1.5; dd ledge2-reach # the chest (137 cm over the floor), past the lip
  w "hands squeeze R 1" 0.3; waitfor 'down: the glide.s end' 5; dd ledge2-hang
  grep -E '^CLIMB t=.* (down:|press R)' "$L" | tail -3 | sed -E 's/frame=[0-9]+ //' | cut -c1-330
  [ "$(cnt '^CLIMB t=.* down: the R hand.s grip.*DOWN_REACH')" -gt "$n0" ] && climbing &&
    PASS "ledge2: the chest-high grip past the edge took him down to hang from the lip" || FAIL "ledge2: no way down by the reach"
  echo "$(date +%T) hanging: $(root)"; w "hands squeeze R 0" 0.5; w "hands test off" 0.5
  climbing && { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.5; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 3; }; }
# the archway's face (mark 26): up it by hand from the ground below (the face's toward -0.5,-0.87: heading -150)
archup() { local k HOLD O OX HX g0; echo "== archup (mark 26)"; x "btn R squeeze 0"; w "hands test off"; go "32060 3710 -30470"
  faceyaw -150; g0=$(cnt '^CLIMB t=.* rock guard'); HOLD=
  for k in 1 2 3 4 5 6 7 8; do
    [ "$HOLD" = L ] && { O=R; OX=20; HX=-20; } || { O=L; OX=-20; HX=20; }
    grabat $O 40 35 || { echo "$(date +%T) up $k: no grab $O"; break; }
    [ -n "$HOLD" ] && { w "hands squeeze $HOLD 0" 0.5; w "hands test $HOLD $HX -40 10 0 0 0" 0.5; }
    w "hands swing $O $OX -40 $GZ 0 0 0 800" 1.3; echo "$(date +%T) up $k ($O): $(root)"; HOLD=$O
  done; dd archup-top
  grep -E '^(CLIMB t=.* rock guard|UNPIN|HEADROCK)' "$L" | tail -4 | cut -c1-260
  echo "$(date +%T) archup: rock guard undos $(( $(cnt '^CLIMB t=.* rock guard') - g0 ))"
  w "hands squeeze L 0" 0.4; w "hands squeeze R 0" 0.4; w "hands test off" 0.5
  climbing && { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.5; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 3; }; }
grabat() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.5
  for k in 1 2 3 4 5; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.25; done
    w "hands squeeze $h 0" 0.4; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.4
  done; return 1; }
# what stops the walk past mark 24's bevel (boots 150416, 153822: the walk refused even 20 cm up, only the floor touching)
probe33() { local yw; echo "== probe the stop past the lip"; x "btn R squeeze 0"; w "hands test off"; go "31215 4710 -30690"
  faceyaw 70; walk probe-in 2.5; echo "$(date +%T) at the stop: $(root)"
  for yw in 70 90 45; do faceyaw $yw; w "climb probe edge" 2.5
    grep -E '^CLIMB probe' "$L" | tail -8 | cut -c1-400; done
  dd probe-stop; }
# build 36 (som-b36-move): the low bank of the 11-03 Windows session's marks 31-32 (34520 3654 -25896: stuck at its edge,
# A cut off by "a ceiling"; then walking on air at its height 7 m west, 33810 -25824)
bank36() { local n0 yw; echo "== bank36 (marks 31-32)"; x "btn R squeeze 0"; w "hands test off"; go "34520 3654 -25896"
  echo "$(date +%T) at: $(root)"; faceyaw -150; dd bank-at
  w "climb probe edge" 2.5; grep -E '^CLIMB probe edge' "$L" | tail -5 | cut -c1-500
  walk bank-out 3; dd bank-out
  w "climb contacts" 1.5; grep -E '^CLIMB contacts' "$L" | tail -4 | cut -c1-300
  n0=$(cnt '^JUMP t=.*end:'); printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 0.25; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 1.5
  grep -E '^JUMP t=.*(go:|ceiling|end:)' "$L" | tail -3 | cut -c1-260
  for yw in -84 -84; do faceyaw $yw; walk "bank-west" 3; dd bank-west; w "climb probe edge" 2.5
    grep -E '^CLIMB probe edge' "$L" | tail -5 | cut -c1-500; done
  echo "$(date +%T) after: $(root)"; }
# build 36 (som-b36-move, re-xr-pipeline.md 143 "build 36"; the build-36 env): mark 31's low bank (34520 3654 -25896,
# heading -150): the walk pushed into its edge (the game's keeper: the walk held; the low edge's cue), A held 0.9 s with
# the stick still pushing = the drop's ring on the meadow below, let go = the blink down (SOMVR_CLIMB_EDGE_LOW); mark
# 32's spot on the sky (33810 3654 -25824): set there - on nothing he falls (the game's, or SOMVR_WALK_GROUND's fall);
# then from the bank west and round as the user went, with the ground guard watching
ry() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2; }
b36() { local n0 n1 n2 n3 y0 y1 r0 r1 yw; echo "== b36 (marks 31, 32)"; x "btn R squeeze 0"; w "hands test off"
  go "34520 3654 -25896"; echo "$(date +%T) at: $(root)"; faceyaw -150; dd b36-bank
  w "climb probe edge" 2.5; grep -E '^CLIMB probe edge' "$L" | tail -3 | cut -c1-400
  n0=$(cnt '^CLIMB t=.*edge cue'); n1=$(cnt '^CLIMB t=.*drop mark: A held at a low edge'); n2=$(cnt '^PRACTICE t=.*arrived at')
  r0=$(root); y0=$(ry); w "stick 0 1 0 5" 2.0
  printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.0; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 4; r1=$(root); y1=$(ry)
  grep -E '^(CLIMB t=.*(edge cue|drop mark)|JUMP t=.*(press|go:|ceiling)|PRACTICE t=)' "$L" | tail -9 | cut -c1-300
  lastloco 2; echo "$(date +%T) the low edge: $r0 -> $r1"; dd b36-below
  if [ "$(cnt '^CLIMB t=.*drop mark: A held at a low edge')" -gt "$n1" ] && [ "$(cnt '^PRACTICE t=.*arrived at')" -gt "$n2" ] &&
     python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 80 else 1)" 2>/dev/null; then
    PASS "b36 low edge (mark 31): the cue $(( $(cnt '^CLIMB t=.*edge cue') - n0 )), A held = the ring below, let go = the blink down ($y0 -> $y1)"
  else FAIL "b36 low edge (mark 31): $y0 -> $y1"; fi
  n3=$(cnt '^(WALK ground|JUMP t=.*the fall from here)'); go "33810 3654 -25824"; y0=$(ry); sleep 3; y1=$(ry)
  echo "$(date +%T) the sky spot (mark 32): $y0 -> $y1"; dd b36-sky
  grep -E '^(WALK ground|JUMP t=.*(the fall from here|end:))' "$L" | tail -4 | cut -c1-400
  python3 -c "import sys; sys.exit(0 if float('$y1') < 3700 else 1)" 2>/dev/null && PASS "b36 the sky spot: he is on the ground ($y1; guard lines $(( $(cnt '^(WALK ground|JUMP t=.*the fall from here)') - n3 )))" || FAIL "b36 the sky spot: still at $y1"
  go "34520 3654 -25896"; n3=$(cnt '^(WALK ground|JUMP t=.*the fall from here)')
  for yw in -84 0 84; do faceyaw $yw; walk "b36-round$yw" 3; echo "  the root now $(root)"; done
  echo "$(date +%T) round: guard lines $(( $(cnt '^(WALK ground|JUMP t=.*the fall from here)') - n3 ))"
  grep -E '^(WALK ground|JUMP t=.*the fall from here)' "$L" | tail -3 | cut -c1-400; dd b36-round; }
card
for p in ${PLAN_B33:-lip coping arch ledge}; do $p; done
grep -E '^CLIMB END build 33' "$L" | tail -1 | cut -c1-600
grep -E ' (PASS|FAIL) ' "$ST" 2>/dev/null
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
