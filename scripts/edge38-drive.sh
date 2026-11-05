#!/bin/bash
# edge38-drive.sh - live 11-04 (som-live-ledge, SOMVR_WALK_EDGE_STOP): the user's 11-04 Windows session
# (logs/win-session-20261104-195050) at its two edges, the hands held as a player holds them (both controllers at the
# waist, pointing ahead: the grip frame's -Z forward), pin16/climb2-drive.sh's functions (sourced). PLAN_E38 (default
# "m5 m8"):
#   m5   mark 5: the climbed rock top (the mantle's landing 31736 4716 -30559; the session walked off it at heading -151
#        and fell 10 m): the stick into the edge (held: `WALK edge stop`, the root still on the top), A tapped = the light
#        step off (the root down), back up, the stick again, A held 1 s = the ring and the blink (the root down)
#   m8   mark 8: the ledge at 28720 1795 -27097 (the game's keeper held a 19 cm walk, no cue): the stick four ways from
#        the spot (held at any drop over 50 cm: never a fall), then A tapped toward the first edge that held him
# Stops at once (quit) when the user goes into VR. Run with PIN_RUN=<the boot's run> (headless_vr.sh's `windows: RUN`).
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
ST=$R/edge38-steps.txt
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
ry() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2; }
hands() { w "hands test R 22 -55 15 0 0 0" 0.4; w "hands test L -22 -55 15 0 0 0" 0.4; } # at the waist, pointing ahead
a_tap() { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 0.35; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep "${1:-3}"; }
a_hold() { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep "${1:-1.1}"; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep "${2:-4}"; }
lines() { grep -E '^(WALK (edge stop|ground)|JUMP t=.*(press|go:|end:|refused)|CLIMB t=.*(edge cue|drop mark)|PRACTICE t=.*(down|arrived))' "$L" |
  tail -n +$(( $1 + 1 )) | head -${2:-14} | cut -c1-300; }
nl() { wc -l < "$L"; }
# the walk toward heading WANT without turning (faceyaw's turn words spin the headless body: its game moves between our
# ticks at up to 400 cm/s, boots 201212 and 202424): the stick at WANT - the head's yaw (MOVE=head)
sdir() { local y; y=$(yaw); python3 -c "import math; d=math.radians(float('$1')-float('${y:-0}')); print('%.3f %.3f' % (math.sin(d), math.cos(d)))"; }
walkto() { local s; s=$(sdir "$1"); w "stick $s 0 $2" "$2"; sleep 0.5; }
xz() { root | grep -oE 'root [0-9.,-]+' | cut -d' ' -f2; }
moved() { python3 -c "a=[float(v) for v in '$1'.split(',')]; b=[float(v) for v in '$2'.split(',')]; print('%.0f' % ((a[0]-b[0])**2+(a[2]-b[2])**2)**0.5)"; }
M5="31330 4818 -30653"   # 70 cm back from where boots 201212/202424/203208 held at the gap (31264..31291 -30704..-30715, heading -145)
root() { local n0 i; n0=$(cnt '^CLIMB state'); w "climb state" 0.2
  for i in $(seq 1 20); do [ "$(cnt '^CLIMB state')" -gt "$n0" ] && break; sleep 0.2; done
  grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+ yaw [0-9.-]+'; }
held_since() { grep -qE '^WALK edge stop t=.*held at a drop' <(tail -n +$(( $1 + 1 )) "$L"); }
m5run() { local what=$1 y0 y1 k s; go "$M5"; hands; sleep 1; y0=$(ry); s=$(sdir -145); k=$(nl); w "climb trace 6" 0.2; w "stick $s 0 6" 0.1
  if ! waitfor '^WALK edge stop t=.*held at a drop' 6; then FAIL "m5 $what: no hold in 6 s ($(root))"; w "stick 0 0 0 0.1" 0.5; return 1; fi
  echo "$(date +%T) held: $(grep -E '^WALK edge stop t=.*held at a drop' "$L" | tail -1 | grep -oE 'heading [0-9.-]+ deg; the root [0-9.,-]+')" | tee -a "$ST"
  if [ "$what" = tap ]; then a_tap 0.1; else printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.1; printf 'btn R a 0\n' > "$R/xrtest.txt"; fi
  w "stick 0 0 0 0.1" 4; y1=$(ry); dd "m5-$what"; lines "$k" 16
  if [ "$what" = tap ]; then grep -qE '^WALK edge stop t=.*the light step off' <(tail -n +$((k + 1)) "$L") && python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 150 else 1)" &&
      PASS "m5 tap (pushing into the edge): the light step off and the fall ($y0 -> $y1)" || FAIL "m5 tap: $y0 -> $y1"
  else grep -qE '^CLIMB t=.*drop mark: A let go' <(tail -n +$((k + 1)) "$L") && python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 150 else 1)" &&
      PASS "m5 hold (pushing into the edge): the ring and the blink down ($y0 -> $y1)" || FAIL "m5 hold: $y0 -> $y1"; fi; }
m5() { echo "== m5 (mark 5): the gap on the climbed rock top"; x "btn R squeeze 0"; m5run tap; m5run hold
  local k s p1 p2 y0; go "$M5"; hands; sleep 1; y0=$(ry); s=$(sdir -145); k=$(nl); w "stick $s 0 5" 0.1
  if waitfor '^WALK edge stop t=.*held at a drop' 5; then p1=$(xz); sleep 2; p2=$(xz); w "stick 0 0 0 0.1" 0.5
    echo "$(date +%T) stay: held at $p1, after 2 s more of the stick $p2 ($(moved "$p1" "$p2") cm), the root's y $y0 -> $(ry)" | tee -a "$ST"; lastloco 3; fi; }
M8="28720 1795 -27097"
m8() { local y0 y1 yw k held= fell= r; echo "== m8 (mark 8): the small ledge"; x "btn R squeeze 0"
  for yw in -60 -15 30 75 120 165 -150 -105; do go "$M8"; hands; sleep 1; k=$(nl); y0=$(ry); walkto $yw 2; sleep 0.5; y1=$(ry)
    r=walked; held_since "$k" && { r="held (ours)"; [ -n "$held" ] || held=$yw; dd "m8-held$yw"; }
    python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 50 else 1)" && { fell="$fell $yw"; r="$r, FELL"; }
    echo "$(date +%T) heading $yw: $r: $y0 -> $y1 ($(root)) $(lastloco 1)" | tee -a "$ST"; done
  [ -z "$fell" ] && PASS "m8 walks: no fall in 8 ways (ours held at heading '${held:-none}')" || FAIL "m8 walks: fell at '${fell}'"
  [ -n "$held" ] || return 0
  go "$M8"; hands; walkto $held 2; sleep 0.5; k=$(nl); y0=$(ry); a_tap 4; y1=$(ry); dd m8-tap; lines "$k"
  python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 40 else 1)" && PASS "m8 tap: stepped down ($y0 -> $y1)" || FAIL "m8 tap: $y0 -> $y1"; }
BANK="34520 3654 -25896" # the 11-03 session's low bank (marks 31-32): its west side has no edge keeper (151 cm over the meadow)
bank() { local what y0 y1 k s; echo "== bank (11-03 marks 31-32): the low bank's west edge"; x "btn R squeeze 0"
  for what in tap back; do go "$BANK"; hands; sleep 1; y0=$(ry); s=$(sdir -84); k=$(nl); w "climb trace 9" 0.2; w "stick $s 0 9" 0.1
    if ! waitfor '^WALK edge stop t=.*held at a drop' 9; then FAIL "bank $what: no hold in 9 s ($(root))"; w "stick 0 0 0 0.1" 0.5; continue; fi
    echo "$(date +%T) held: $(grep -E '^WALK edge stop t=.*held at a drop' "$L" | tail -1 | grep -oE 'heading [0-9.-]+ deg; the root [0-9.,-]+')" | tee -a "$ST"
    sleep 0.3; dd "bank-cue-$what"; x "head 0 0 0 pitch -65 s 0.3"; sleep 0.5; dd "bank-cue-look-$what"; x "head 0 0 0 s 0.3"; sleep 0.4; echo "$(date +%T) after 1 s more of the stick: $(root)" | tee -a "$ST"
    if [ "$what" = tap ]; then a_tap 0.1; else w "stick 0 -1 0 2" 0.4; a_tap 0.1; fi # back: the stick back + A (1.3 m: short)
    w "stick 0 0 0 0.1" 4; y1=$(ry); dd "bank-$what"; lines "$k" 14
    if [ "$what" = tap ]; then grep -qE '^WALK edge stop t=.*the light step off' <(tail -n +$((k + 1)) "$L") && python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 80 else 1)" &&
        PASS "bank tap (pushing into the edge): the light step off and the fall ($y0 -> $y1)" || FAIL "bank tap: $y0 -> $y1"
    else grep -qE '^WALK edge stop t=.*the light step off' <(tail -n +$((k + 1)) "$L") && python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 80 else 1)" &&
        PASS "bank back (the stick back + A at a 1.3 m drop: short, no hang): the step off ($y0 -> $y1)" || FAIL "bank back: $y0 -> $y1"; fi; done; }
# build 39: the hand at a world offset (east DX, north DZ, cm) from the head, DY under it, as a real hand reaching onto a
# ledge (the grip pointing ahead along the reach): its head-frame numbers from the head's yaw (no turning)
handat() { local y v; y=$(yaw); v=$(python3 -c "import math; a=math.radians(float('${y:-0}')); dx,dz=float('$2'),float('$4'); print('%.0f %.0f' % (dx*math.cos(a)-dz*math.sin(a), dx*math.sin(a)+dz*math.cos(a)))")
  w "hands test $1 ${v% *} $3 ${v#* } 0 0 0" 0.5; }
MEADOW="34415 3628 -25894" # the meadow under the low bank's west face (its top ~3653, 129 cm over the meadow; the face x ~34463)
topa() { local y0 y1 k n0; echo "== topa (build 39): the hand on the low bank's top from the meadow"; x "btn R squeeze 0"; go "$MEADOW"; hands
  sleep 1; y0=$(ry); echo "$(date +%T) at: $(root)"; dd topa-at
  n0=$(cnt '^CLIMB t=.*top A: the (left|right) hand rests'); handat R 70 -43 0; waitfor '^CLIMB t=.*top A: the (left|right) hand rests' 3
  grep -E '^(CLIMB t=.*top A|OBJECTIVE t=.*A UP)' "$L" | tail -2 | cut -c1-260; dd topa-ring
  [ "$(cnt '^CLIMB t=.*top A: the (left|right) hand rests')" -gt "$n0" ] && PASS "topa: the hand on the top: its ring + A UP" || FAIL "topa: no ring"
  k=$(nl); a_tap 3; y1=$(ry); dd topa-up; lines "$k" 10; grep -E '^CLIMB t=.*top A: A pressed' "$L" | tail -1 | cut -c1-220
  python3 -c "import sys; sys.exit(0 if float('$y1') - float('$y0') > 100 else 1)" && PASS "topa: A = the blink up onto the top ($y0 -> $y1)" || FAIL "topa: A: $y0 -> $y1"
  hands; go "$MEADOW"; hands; sleep 1; n0=$(cnt '^CLIMB t=.*top A: the (left|right) hand rests'); handat R 30 0 0; sleep 1.5
  [ "$(cnt '^CLIMB t=.*top A: the (left|right) hand rests')" -eq "$n0" ] && PASS "topa: the hand in the air before the face: no ring" || FAIL "topa: a ring with the hand in the air"; }
# build 39: the climb down at mark 5's crevice (11 m): held at it facing it, the stick back + A = lowered to the hang under
# the lip (the view's turn to the face as snap steps); then the hands may climb; A drops
down5() { local y0 y1 k s n0; echo "== down5 (build 39): backing over the crevice's edge, the stick back + A"; x "btn R squeeze 0"
  go "$M5"; hands; faceyaw 35; sleep 1; s=$(sdir -145); echo "$(date +%T) the stick toward the drop (back): $s" | tee -a "$ST"
  k=$(nl); w "stick $s 0 8" 0.1
  if ! waitfor '^WALK edge stop t=.*held at a drop' 8; then FAIL "down5: no hold"; w "stick 0 0 0 0.1" 0.5; return; fi
  y0=$(ry); n0=$(cnt 'climb down \(the stick back'); a_tap 0.2; w "stick 0 0 0 0.1" 0.5; sleep 5; y1=$(ry)
  dd down5-hang; lines "$k" 12; grep -E '^CLIMB t=.*(climb down|hang|DN|down)' "$L" | tail -4 | cut -c1-260
  [ "$(cnt 'climb down \(the stick back')" -gt "$n0" ] && python3 -c "import sys; sys.exit(0 if float('$y0') - float('$y1') > 80 else 1)" && climbing &&
    PASS "down5: backing to the edge, the stick back + A = lowered to the hang under the lip ($y0 -> $y1)" || FAIL "down5: $y0 -> $y1"
  printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 1.2; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 3; }
# build 39: the 22:09 session's mark 222445_15 - where he stood "on a ceiling that doesn't exist" (the mantle's landing
# 32896 4310 -31586): what the rays and the proxy meet under him
air() { echo "== air (mark 222445_15)"; x "btn R squeeze 0"; go "32896 4414 -31586"; hands; sleep 3; echo "$(date +%T) at: $(root)" | tee -a "$ST"
  dd air-at; w "climb probe edge" 2.5; grep -E '^CLIMB probe edge' "$L" | tail -3 | cut -c1-400 | tee -a "$ST"
  w "climb contacts" 1.5; grep -E '^CLIMB contacts' "$L" | tail -3 | cut -c1-300 | tee -a "$ST"
  sleep 2; echo "$(date +%T) after: $(root)" | tee -a "$ST"; grep -E '^WALK ground' "$L" | tail -2 | cut -c1-300 | tee -a "$ST"; }
# build 39 (SOMVR_WALK_EDGE_HOLD): the stay at the crevice's stop - twice: held, 2 s more of the stick, how far the root went
stay5() { local k s p1 p2 i; echo "== stay5: the stay at the crevice's stop"; x "btn R squeeze 0"
  for i in 1 2; do go "$M5"; hands; sleep 1; s=$(sdir -145); k=$(nl); w "climb trace 6" 0.2; w "stick $s 0 5" 0.1
    if waitfor '^WALK edge stop t=.*held at a drop' 5; then p1=$(xz); sleep 2; p2=$(xz); w "stick 0 0 0 0.1" 0.5
      echo "$(date +%T) stay $i: held at $p1, after 2 s more of the stick $p2 ($(moved "$p1" "$p2") cm); hold lines $(tail -n +$((k + 1)) "$L" | grep -c '^WALK edge hold')" | tee -a "$ST"
      grep -E '^WALK edge hold' "$L" | tail -1 | cut -c1-250; lastloco 2
    else echo "$(date +%T) stay $i: no hold" | tee -a "$ST"; fi; done; }
# the merge with finisher2 (build 37): its hint ring in the same objective layer - a far orc, the execution charged
fin() { local h0; echo "== fin: the finisher's hint ring (a far orc, charged)"; x "btn R squeeze 0"; go "32500 1878 -24800"; hands; sleep 1
  w "hands test R 10 -10 -15 0 0 0" 0.5; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.5
  w "hands squeeze R 0" 0.5; w "hands test R 28 -40 20 0 0 0" 0.4 # the sword drawn from the hip (fin37's draw_slot)
  w "finisher on" 0.2; w "practice remove" 2; w "practice far infantry" 5; w "fight guard path" 0.4
  h0=$(cnt '^FINISHER t=.* hint on'); w "verbs brand charge" 0.4
  if waitfor '^FINISHER t=.* hint on' 10; then sleep 1.5; dd fin-hint; dd fin-hint2
    grep -E '^FINISHER t=.* hint on' "$L" | tail -1 | cut -c1-220; PASS "fin: the finisher's hint on"
  else FAIL "fin: no hint line"; fi
  grep -E "the finisher's hint" "$R/xr-layer.log" | tail -2 | cut -c1-260; w "finisher off" 0.5; w "practice remove" 1; }
card
for p in ${PLAN_E38:-m5 m8}; do $p; done
grep -E '^CONFIG walk edge stop' "$L" | head -1 | cut -c1-200
grep -E ' (PASS|FAIL) ' "$ST" 2>/dev/null
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
