#!/bin/bash
# climb41-drive.sh - som-b41-climb4 (build 41, docs/re-xr-pipeline.md 182). PLAN41 words, in order (default
# "air bank beam16 airmantle"):
#   beam16    the 11-05 mark 093135_16 (the patch past the beam's end): Talion at the user's spot, the view at the user's
#             heading (-7), the stick forward 3 s under `climb trace` - the navmesh clip off in both, the context-action
#             clip the game's (`climb ctxclip off`), then SOMVR_WALK_CTXCLIP's (`climb ctxclip on`): the root's way, rise
#   beams     the beam's other two spots (marks 17 and 222603_18) with both clips off: no regression
#   air       the 11-04 mark 222445_15's invisible top (the mantle's landing 32898.2 4309.5 -31593.8): standing on the
#             top 50 cm east of it, the view west and down, `climb drawn probe` at the landing (want: nothing drawn), at
#             the drawn ground west under it (want: drawn); both eyes
#   bank      a normal top: the meadow's low bank (edge38's topa): the right hand resting on its top = the A UP ring, its
#             drawn verdict (want: drawn) and A = the blink up; then `climb drawn force undrawn` (the refusal's way at a
#             real top): the ring red and crossed, A refused, he stays; both eyes of each ring
#   airmantle the invisible top's west edge from the top: backing to it, A held = the climb down onto its face, a grip,
#             a pull up over the lip: the mantle's ring (want: nothing drawn, red) and A refused; `climb drawn off`: A
#             glides onto it (the old way)
# Hands as a player holds them (at the waist pointing ahead, the grip frame's -Z forward); never the turn words for a
# walk (faceyaw then the stick's direction). Attaches to the newest boot of this worktree (PIN_RUN=RUN to pin). Stops at
# once (quit) when the user goes into VR.
CLIMB39_LIB=1 . "$(dirname "$0")/climb39-drive.sh"
PLAN41=${PLAN41:-"air bank beam16 airmantle"}
ST=$R/climb41-steps.txt
PASS() { echo "$(date +%T) PASS $*" | tee -a "$ST"; }
FAIL() { echo "$(date +%T) FAIL $*" | tee -a "$ST"; }
nl() { wc -l < "$L"; }
rootxyz() { root | grep -oE 'root [0-9.-]+,[0-9.-]+,[0-9.-]+' | sed 's/root //'; }
hands() { w "hands test R 22 -55 15 0 0 0" 0.4; w "hands test L -22 -55 15 0 0 0" 0.4; } # at the waist, pointing ahead
a_tap() { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 0.35; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep "${1:-3}"; }
sdir() { local y; y=$(yaw); python3 -c "import math; d=math.radians(float('$1')-float('${y:-0}')); print('%.3f %.3f' % (math.sin(d), math.cos(d)))"; }
handat() { local y v; y=$(yaw); v=$(python3 -c "import math; a=math.radians(float('${y:-0}')); dx,dz=float('$2'),float('$4'); print('%.0f %.0f' % (dx*math.cos(a)-dz*math.sin(a), dx*math.sin(a)+dz*math.cos(a)))")
  w "hands test $1 ${v% *} $3 ${v#* } 0 0 0" 0.5; }
drawnline() { tail -n +"$1" "$L" | grep -E '^CLIMB t=.* drawn: the landing' | tail -1 | sed -E 's/frame=[0-9]+ //' | cut -c1-${2:-420}; }
walk3() { local tag=$1 n0 a b
  a=$(rootxyz); n0=$(nl)
  w "stick 0 0.7 0 3" 3.6; b=$(rootxyz); dd "$tag"
  python3 -c "
a=[float(v) for v in '$a'.split(',')]; b=[float(v) for v in '$b'.split(',')]
import math; print('WALK3 $tag: %.0f cm level, %+.0f cm up (from %s to %s)' % (math.hypot(b[0]-a[0], b[2]-a[2]), b[1]-a[1], '$a', '$b'))
" | tee -a "$ST"
  tail -n +"$n0" "$L" | grep -E '^(LOCO t=.* last 1 s|WALK (lip|edge|slope|navmesh|ctxclip)|UNPIN)' | sed -E 's/frame=[0-9]+ //; s/; follow in our delta.*//' |
    cut -c1-220 | tail -6 | sed 's/^/    /'; }
lvl() { grep -oE "WALK3 $1: [0-9]+ cm level" "$ST" | tail -1 | grep -oE '[0-9]+ cm' | grep -oE '[0-9]+'; }
spot() { local nm=$1 at=$2 yw=$3 cx n0
  echo "=== $nm at $at, the view $yw deg"
  for cx in ${CX:-off on}; do   # the context-action clip the game's / off over a real floor (SOMVR_WALK_CTXCLIP)
    reset; hands; w "climb navmesh on" 0.4; w "climb ctxclip $cx" 0.6
    go "$at"; faceyaw "$yw"; echo "$(date +%T) at: $(rootxyz)"
    n0=$(nl); w "climb trace 4" 0.3; walk3 "$nm-ctx$cx"
    tail -n +"$n0" "$L" | grep -E '^CLIMB trace t=' | sed -E 's/^CLIMB trace //; s/frame=[0-9]+: //; s/ grav [0-9]+ rm [0-9]+ layer [0-9]+//' |
      cut -c1-300 > "$R/trace-$nm-ctx$cx.txt"
    echo "    $(wc -l < "$R/trace-$nm-ctx$cx.txt") trace lines -> trace-$nm-ctx$cx.txt"; head -2 "$R/trace-$nm-ctx$cx.txt" | sed 's/^/    /'
  done; }
beam16() { local a b
  CX="off on" spot m16 "30766.9 4810.3 -30479.0" -7
  a=$(lvl m16-ctxoff); b=$(lvl m16-ctxon)
  [ "${b:-0}" -ge 40 ] && [ "${b:-0}" -ge $(( ${a:-0} + 25 )) ] && PASS "beam16: the clip's way ${a:-?} cm -> SOMVR_WALK_CTXCLIP's ${b:-?} cm in 3 s" ||
    FAIL "beam16: ${a:-?} -> ${b:-?} cm"; }
beams() { CX="on" spot m17 "31122.0 4810.1 -30710.6" 50; CX="on" spot m18 "31237.2 4816.5 -30684.4" 48; }
# the invisible top: Talion on it 50 cm east of the mantle's landing, the view west, pitched down
AIRTOP="32896 4414 -31586"   # (edge38's air spot: the proxy stood on the top at 4312.4; boot 150042's 32950 was off the ridge)
LAND="32898.2 4309.5 -31593.8"
probe_at() { local tag=$1 at=$2 want=$3 k got
  k=$(nl); w "climb drawn probe $at" 2.5; dd "air-$tag"; sleep 1.5
  got=$(drawnline "$k"); echo "    $got" | tee -a "$ST"
  case "$want:$got" in
    undrawn:*"NOTHING DRAWN"*) PASS "air $tag: nothing drawn at $at" ;;
    drawn:*": drawn -"*) PASS "air $tag: drawn at $at" ;;
    *) FAIL "air $tag: want $want at $at" ;;
  esac; }
air() { echo "== air (11-04 mark 222445_15): the invisible top"; reset; hands
  go "$AIRTOP"; echo "$(date +%T) at: $(rootxyz)" | tee -a "$ST"
  python3 -c "import sys; sys.exit(0 if float('$(ry)') > 4300 else 1)" || { FAIL "air: not on the top ($(rootxyz))"; return; }
  faceyaw -90; x "head 0 0 0 pitch -70 s 0.3"; sleep 0.8
  # (boot 151933: the mantle's landing is a ruin wall's drawn top - both eyes and the depth agree: the check lets it be)
  probe_at landing "$LAND" drawn
  probe_at west "32876 4309.5 -31600" drawn
  faceyaw 62; x "head 0 0 0 pitch -65 s 0.3"; sleep 0.8
  probe_at east "32925 4309.5 -31572" drawn
  x "head 0 0 0 s 0.3"; sleep 0.5; }
# a normal top: the meadow's low bank (edge38's MEADOW / topa)
MEADOW="34415 3628 -25894"
bank() { local k y0 y1 g
  echo "== bank: A UP at a drawn top"; reset; w "climb drawn force off" 0.3
  go "$MEADOW"; sleep 6   # (a warm-up trip: boots 150042, 151933 and 161512 had no ring in the first round - the world streaming)
  for g in game forced; do
    go "$MEADOW"; hands; faceyaw 90; sleep 1; y0=$(ry); k=$(nl)
    [ $g = forced ] && w "climb drawn force undrawn" 0.3
    handat R 70 -43 0
    # (boot 150042: the ring 16 s after the arrival)
    if ! waitfor '^CLIMB t=.*top A: the (left|right) hand rests' 45; then FAIL "bank $g: no A UP ring ($(rootxyz))"; continue; fi
    [ $g = forced ] && waitfor '^CLIMB t=.* drawn: the landing .*NOTHING DRAWN' 5
    dd "bank-ring-$g"; echo "    $(drawnline "$k")" | tee -a "$ST"
    # (the headless hand's sway makes the ring come and go - boot 165210's taps 2.4 s on met a gap: A the moment it shows)
    waitfor '^CLIMB t=.*top A: the (left|right) hand rests' 12
    a_tap 2.5; y1=$(ry); dd "bank-after-$g"
    tail -n +"$k" "$L" | grep -E '^CLIMB t=.*(top A|A UP|blink|no blink)' | sed -E 's/frame=[0-9]+ //' | cut -c1-260 | tail -4 | sed 's/^/    /'
    if [ $g = game ]; then
      drawnline "$k" | grep -q ': drawn -' && [ $(( ${y1:-0} - ${y0:-0} )) -gt 80 ] &&
        PASS "bank: the drawn verdict and A = the blink up ($y0 -> $y1)" || FAIL "bank: $y0 -> $y1 ($(drawnline "$k" 160))"
    else
      grep -qE '^CLIMB t=.*A UP: no blink: nothing drawn' <(tail -n +"$k" "$L") && [ $(( ${y1:-0} - ${y0:-0} )) -lt 30 ] &&
        PASS "bank forced: the ring red, A refused, he stays ($y0 -> $y1)" || FAIL "bank forced: $y0 -> $y1"
      w "climb drawn force off" 0.3
    fi
    hands; sleep 0.5
  done; }
# the invisible top's west edge (the mantle's edge 32859 -31602): backing west to it facing east, A held = the climb
# down, the right hand ahead at the face; then a pull up over the lip: the mantle's ring and A
airmantle() { local k y0 y1 s dr
  echo "== airmantle: the mantle onto the invisible top"; reset; hands
  for dr in on off; do
    w "climb drawn $dr" 0.3
    go "$AIRTOP"; faceyaw 90; sleep 0.8; s=$(sdir -90); k=$(nl); w "stick $s 0 8" 0.1
    if ! waitfor '^WALK edge stop t=.*held at a drop' 8; then FAIL "airmantle $dr: no hold at the west edge ($(rootxyz))"; w "stick 0 0 0 0.1" 0.5; continue; fi
    w "stick 0 0 0 0.1" 0.3; w "hands test R 15 -50 40 0 0 0" 0.3; y0=$(ry)
    printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 0.8; printf 'btn R a 0\n' > "$R/xrtest.txt"   # (hold40b's)
    waitfor 'climb down \(A held at the edge\)' 3; sleep 1.0; w "hands squeeze R 1" 1.5
    tail -n +"$k" "$L" | grep -E '^CLIMB t=.*(climb down|grab R|hang|mantle)' | sed -E 's/frame=[0-9]+ //' | cut -c1-240 | tail -5 | sed 's/^/    /'
    climbing || { FAIL "airmantle $dr: not on the face after the climb down ($(rootxyz))"; reset; continue; }
    # the pull in steps: the holding hand down toward the body until the mantle's ring shows (the eyes 25 cm over the lip)
    local yy zz ok=0
    for yy in "-85 35" "-115 30" "-140 25"; do
      zz=${yy#* }; w "hands swing R 15 ${yy% *} $zz 0 0 0 800" 1.2
      waitfor '^CLIMB t=.*mantle: at the top' 1.5 && { ok=1; break; }
    done
    [ $ok = 1 ] || echo "$(date +%T) no mantle ring ($(rootxyz))"
    sleep 1.0; dd "airmantle-ring-$dr"; echo "    $(drawnline "$k")" | tee -a "$ST"
    k2=$(nl); a_tap 2.5; y1=$(ry); dd "airmantle-after-$dr"
    tail -n +"$k2" "$L" | grep -E '^CLIMB t=.*mantle' | sed -E 's/frame=[0-9]+ //' | cut -c1-260 | tail -3 | sed 's/^/    /'
    if [ $dr = on ]; then
      grep -qE '^CLIMB t=.*mantle: A at the top, no way onto it: nothing drawn' <(tail -n +"$k2" "$L") &&
        PASS "airmantle: the mantle onto the invisible top refused ($y0 -> $y1)" || FAIL "airmantle on: no refusal ($y0 -> $y1)"
    else
      grep -qE '^CLIMB t=.*mantle: A \((glide|blink)\)' <(tail -n +"$k2" "$L") &&
        PASS "airmantle off: the old way glides onto it ($y0 -> $y1)" || FAIL "airmantle off: no glide ($y0 -> $y1)"
    fi
    reset; hands
  done
  w "climb drawn on" 0.3; }
# stepoff (the 11-04 mark 222445_15's own sequence): on the ruin wall's top (the mantle's landing), the walk east to its
# far edge (the edge stop holds), A tapped = the light step off over the courtyard 6.8 m down - `jump deep off` (build 40:
# every fall refused, he stays in the air) then `jump deep on` (SOMVR_WALK_GROUND_DEEP: the fall, the floor found on the way)
stepoff() { local dp k y0 y1 s
  echo "== stepoff: the light step off the wall's far edge"
  for dp in off on; do
    reset; hands; w "jump deep $dp" 0.4
    go "$AIRTOP"; sleep 0.5; y0=$(ry)
    python3 -c "import sys; sys.exit(0 if float('${y0:-0}') > 4300 else 1)" || { FAIL "stepoff $dp: not on the top ($(rootxyz))"; continue; }
    faceyaw 85; s=$(sdir 85); k=$(nl); w "stick $s 0 8" 0.1
    if ! waitfor '^WALK edge stop t=.*held at a drop' 8; then FAIL "stepoff $dp: no hold at the east edge ($(rootxyz))"; w "stick 0 0 0 0.1" 0.5; continue; fi
    w "stick 0 0 0 0.1" 0.3; a_tap 4; y1=$(ry); dd "stepoff-$dp"
    tail -n +"$k" "$L" | grep -E '^(WALK (edge stop|ground)|JUMP t=.*(go|refused|landed|end)|JUMP END)' | sed -E 's/frame=[0-9]+ //' |
      cut -c1-240 | tail -6 | sed 's/^/    /'
    echo "STEPOFF $dp: the root $y0 -> $y1 ($(rootxyz))" | tee -a "$ST"
    if [ $dp = on ]; then
      [ "${y1:-9999}" -lt 3900 ] && PASS "stepoff: the fall to the courtyard ($y0 -> $y1)" || FAIL "stepoff on: $y0 -> $y1"
    fi
  done
  w "jump deep on" 0.3; }
# hold40r (build-win9 376's hold40 at a real edge): M5's crevice is spanned by the capsule - 376's walks passed it and
# went ~11 m along the coping (boots 161217, 164934); the one that "passed" held at the coping's far end (30721.9 4811.2
# -31541.8, heading -155, the ground 1057 cm down). From 1 m back of that edge: the stick at -155 = held at the drop, A held
# 0.8 s = the climb down (b40-ledge's rule), the root after
EDGE2="30764.2 4818 -31451.2"   # (1 m back along heading -155: dir = (sin h, cos h); boot 173819 took the other sign and fell)
hold40r() { local k y0 y1 s n0
  echo "== hold40r: A held at a real edge (the coping's far end) = the climb down"; reset; hands
  go "$EDGE2"; sleep 1; s=$(sdir -155); k=$(nl); w "stick $s 0 6" 0.1
  # (boots 180118 / 181952: at heading -163 the lip's bevel blocks the walk before our probe sees the drop - no "held"
  # line; what the player needs there is A held = the climb down all the same: pressed either way)
  if waitfor '^WALK edge stop t=.*held at a drop' 6; then
    echo "$(date +%T) held: $(grep -E '^WALK edge stop t=.*held at a drop' "$L" | tail -1 | grep -oE 'heading [0-9.-]+ deg; the root [0-9.,-]+')" | tee -a "$ST"
  else echo "$(date +%T) not held by the edge stop: the walk stopped at $(rootxyz) (the game's bevel)" | tee -a "$ST"; fi
  w "stick 0 0 0 0.1" 0.3; y0=$(ry); n0=$(cnt 'climb down \(A held at the edge\): lowered')
  printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 0.8; printf 'btn R a 0\n' > "$R/xrtest.txt"
  waitfor 'climb down \(A held at the edge\)' 3; sleep 1.5; y1=$(ry); dd hold40r
  tail -n +"$k" "$L" | grep -E '^(WALK edge stop|CLIMB t=.*(climb down|hang|edge cue))' | sed -E 's/frame=[0-9]+ //' | cut -c1-220 | tail -5 | sed 's/^/    /'
  [ "$(cnt 'climb down \(A held at the edge\): lowered')" -gt "$n0" ] && PASS "hold40r: A held at the edge = the climb down ($y0 -> $y1)" ||
    FAIL "hold40r: no climb down ($y0 -> $y1)"
  reset; }
# beam17 (build 41's SOMVR_WALK_EDGE_STEEP_CM): mark 17's spot, the stick forward 5 s at the user's heading - boot 174915
# walked on down the sloped beam and off its lower end onto rock ~45 cm down and fell 10 m; now the edge stop must hold
# him (no fall, the root not 150 cm under the start)
beam17() { local k a b y0 y1
  echo "=== beam17: mark 17, the walk down the beam to its end"; reset; hands; w "climb navmesh on" 0.3; w "climb ctxclip on" 0.3
  go "31122.0 4810.1 -30710.6"; faceyaw 50; a=$(rootxyz); y0=$(ry); k=$(nl)
  w "climb trace 6" 0.3; w "stick 0 0.7 0 5" 5.6; b=$(rootxyz); y1=$(ry); dd beam17
  tail -n +"$k" "$L" | grep -E '^(WALK (edge stop|ground|lip)|JUMP t=.*(go|end)|CLIMB t=.*edge cue)' | sed -E 's/frame=[0-9]+ //' |
    cut -c1-230 | tail -6 | sed 's/^/    /'
  tail -n +"$k" "$L" | grep -E '^CLIMB trace t=' | sed -E 's/^CLIMB trace //; s/frame=[0-9]+: //' | cut -c1-200 > "$R/trace-beam17.txt"
  echo "BEAM17: $a -> $b" | tee -a "$ST"
  if ! grep -qE '^JUMP t=.*go: the fall from here' <(tail -n +"$k" "$L") && [ $(( ${y0:-0} - ${y1:-0} )) -lt 150 ] &&
     grep -qE '^WALK edge stop t=.*held at a drop' <(tail -n +"$k" "$L"); then
    PASS "beam17: held at the beam's end ($y0 -> $y1; $(grep -E '^WALK edge stop t=.*held at a drop' "$L" | tail -1 | grep -oE 'no floor within [0-9]+ cm'))"
  else FAIL "beam17: $y0 -> $y1"; fi
  w "stick 0 0 0 0.1" 0.5; reset; }
# lip17 (build 41's SOMVR_WALK_EDGE_CONFIRM_CM): boot 180118's own route at mark 17 - along the coping (heading ~64) from
# 1.5 m before the rounded lip it slid off (the descent began at 31597, the fall at 31732): the edge stop must hold him
lip17() { local k y0 y1 s
  echo "=== lip17: the coping's rounded lip (boot 180118's slide)"; reset; hands
  go "31551.4 4818.7 -30574.2"; faceyaw 64; y0=$(ry); s=$(sdir 64); k=$(nl)
  w "climb trace 5" 0.3; w "stick $s 0 4" 4.6; y1=$(ry); dd lip17
  tail -n +"$k" "$L" | grep -E '^(WALK (edge stop|ground)|JUMP t=.*(go|end))' | sed -E 's/frame=[0-9]+ //' | cut -c1-230 | tail -4 | sed 's/^/    /'
  tail -n +"$k" "$L" | grep -E '^CLIMB trace t=' | sed -E 's/^CLIMB trace //; s/frame=[0-9]+: //' | cut -c1-200 > "$R/trace-lip17.txt"
  echo "LIP17: $y0 -> $y1 ($(rootxyz))" | tee -a "$ST"
  if ! grep -qE '^JUMP t=.*go: the fall from here' <(tail -n +"$k" "$L") && [ $(( ${y0:-0} - ${y1:-0} )) -lt 60 ] &&
     grep -qE '^WALK edge stop t=.*held at a drop' <(tail -n +"$k" "$L"); then
    PASS "lip17: held at the rounded lip ($y0 -> $y1; $(grep -E '^WALK edge stop t=.*held at a drop' "$L" | tail -1 | grep -oE 'the root [0-9.,-]+'))"
  else FAIL "lip17: $y0 -> $y1"; fi
  w "stick 0 0 0 0.1" 0.5; reset; }
# hill (som-b41-lip, SOMVR_WALK_EDGE_LINE_CM): normal walks on gentle slopes must not be stopped - from two meadow spots
# the stick 2 s each of four ways (1-9 deg up and down); PASS = no edge stop hold and no fall in any of them
hill() { local at h k a b held fell ok=0 bad=0
  echo "=== hill: gentle downhills are not stopped"
  for at in "34415 3628 -25894" "32500 1878 -24800"; do
    for h in 0 90 180 -90; do
      reset; hands; go "$at"; faceyaw $h >/dev/null; a=$(rootxyz); k=$(nl)
      w "stick 0 0.7 0 2" 2.6; b=$(rootxyz)
      held=$(tail -n +"$k" "$L" | grep -cE '^WALK edge stop t=.*held at a drop')
      fell=$(tail -n +"$k" "$L" | grep -cE '^JUMP t=.*go: the fall from here')
      python3 -c "
import math, sys
a=[float(v) for v in '$a'.split(',')]; b=[float(v) for v in '$b'.split(',')]
lv=math.hypot(b[0]-a[0], b[2]-a[2]); dn=a[1]-b[1]; deg=math.degrees(math.atan2(dn, lv)) if lv > 1 else 0
print('HILL %s heading %s: %.0f cm level, %.0f cm down (%.0f deg), holds $held, falls $fell' % ('$at', '$h', lv, dn, deg))
sys.exit(0)" | tee -a "$ST"
      [ "$held" = 0 ] && [ "$fell" = 0 ] && ok=$((ok + 1)) || bad=$((bad + 1))
    done
  done
  # (boot 184529: no 10-25 deg open hillside is known - the meadows' ways run 1-9 deg; any hold or fall here is a false stop)
  [ $bad = 0 ] && PASS "hill: all $ok meadow walks unstopped (no hold, no fall)" || FAIL "hill: $bad walk(s) held or fell"
  grep -E '^WALK END' "$L" >/dev/null; w "stick 0 0 0 0.1" 0.3; reset; }
[ -n "${CLIMB41_LIB:-}" ] && return 0 # build 42: sourced by edge42-drive.sh for these functions
card
for p in $PLAN41; do
  case $p in air|bank|beam16|beams|airmantle|stepoff|hold40r|beam17|lip17|hill) $p ;; *) echo "unknown plan word $p" ;; esac
done
echo "steps done $(date +%T)"
grep -E ' (PASS|FAIL) ' "$ST" 2>/dev/null
[ "${NOQUIT:-0}" = 1 ] && exit 0
sleep 2; w "endlogs" 3
grep -E '^(WALK END build 4[01]|CLIMB END build 41)' "$L" | tail -4 | cut -c1-300
echo quit >> "$R/cmd"; echo "quit $(date +%T)"
