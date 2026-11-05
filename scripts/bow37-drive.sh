#!/bin/bash
# bow37-drive.sh - som-b37-bow (docs/re-xr-pipeline.md 174): the arrow beside the riser at the player's own cants, and
# the bow from the climb's hands-free hang at a target orc. Boot with scripts/headset-build26.env (+ the som-b37-bow
# block), SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt, SOMVR_BOW_HITLOG=1. B37_STEPS (default "riser roll hang"):
#   riser  from the eyes (no head moves), `bow side` 1.2 / 1.6 / 2.0 / 2.4 cm: the upright draw (abil27c's real-hand pose,
#          as bow33's), then the user's own -26 deg draw - how far beside the limbs' line the arrow clears the riser
#   roll   the player's own held draws (the 11-04 22:09 session, `replay_input ... bow --poses`: the bow canted 4, 26,
#          57, -26 and 86 deg), each drawn twice: SOMVR_BOW_SIDE=roll + SOMVR_BOW_FIT=on, then `bow side fixed` +
#          `bow fit off` (builds 16-36); both eyes at the user's head pitch
#   hang   the user's wall 1 (wall37-drive.sh's foot and heading), hand over hand 3 holds up, both let go = the hang; the
#          view snap-turned from the wall, the bow out and drawn, then an orc put 11 m ahead; aimed by
#          bo_target's line (bow20-drive.sh's aim), released: the hit, Talion's root and gravity unchanged; put away at
#          the shoulder = the hang again; a grip on the wall climbs on
# B37_RUN=RUN drives a running boot. Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 PIN_RUN=${B37_RUN:-} . "$(dirname "$0")/climb2-drive.sh"
STEPS=${B37_STEPS:-riser roll hang}
X=$R/xr-layer.log; ST=$R/b37-steps.txt
# every word waited for (the Windows build reads the trigger file every ~1-2 s: a word written sooner after the last is
# lost - som-b37-finisher2's boot 210251): written, then its TRIGGER line (3 s at most), then the pause asked
w() { userin && bail "the user is in VR"; local a0 i; a0=$(grep -c '^TRIGGER cmd=' "$L"); printf '%s' "$1" > "$R/somvr.trigger"
  echo "$(date +%T) word: $1"; for i in $(seq 1 30); do [ "$(grep -c '^TRIGGER cmd=' "$L")" -gt "$a0" ] && break; sleep 0.1; done
  sleep "${2:-0.8}"; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
xx() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
       mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
dmp() { xx "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
        echo "dump $1 $f" | tee -a "$ST"; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$ST"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$ST"; }
lines() { wc -l < "$L"; }
sincel() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
bowline() { grep -E '^BOW t=.* last 1 s' "$L" | tail -1 | sed -E 's/^BOW t=([0-9.]+) frame=[0-9]+ last 1 s: //' | cut -c1-${1:-400}; }
card
w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
grep -E '^CONFIG bow (side|fit|hang)' "$L" | cut -c1-200

BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
NOCK='^BOW t=.* (nock|draw|enter)'
bow_out() { xx "$BOW_SLOT" "$R_REST" "btn L squeeze 0" "btn L a 0" "btn L b 0" "btn R trigger 0"; sleep 0.3
  xx "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8; xx "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1; }
bow_away() { xx "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; xx "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1; }
isout() { grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter; }
# draw LPOSE RPOSE: the bow hand there; the string hand onto the line a quarter of the way from the bow to RPOSE (inside
# the nock's zone), the trigger, then drawn to RPOSE in 1.2 s (the side settles while the draw is short of 20 cm)
draw() { local lp=$1 rp=$2 np
  np=$(awk -v l="$lp" -v r="$rp" 'BEGIN { split(l, a, " "); split(r, b, " ")
    printf "hand R head %.1f %.1f %.1f %s %s %s", a[4] + 0.25 * (b[4] - a[4]), a[5] + 0.25 * (b[5] - a[5]), a[6] + 0.25 * (b[6] - a[6]), b[7], b[8], b[9] }')
  xx "$lp" "$R_REST" "btn L squeeze 1" "btn R trigger 0"; sleep 0.6
  xx "$lp" "$np" "btn L squeeze 1"; sleep 0.4
  xx "$lp" "$np" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  xx "$lp" "$rp s 1.2" "btn L squeeze 1" "btn R trigger 1"; sleep 1.6; }
letdown() { local lp=$1; xx "$lp" "$AT_STRING s 0.5" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8
  xx "$lp" "$AT_STRING" "btn L squeeze 1" "btn R trigger 0"; sleep 0.8; }

if has riser; then
  # (no head moves: the headless head eased back drifted up to 18 cm in boot 021658, the views not comparable)
  echo "== riser: from the eyes, the arrow 1.2 / 1.6 / 2.0 / 2.4 cm beside the limbs' line - upright, then the user's -26 deg draw"
  isout || bow_out
  w "bow side fixed" 0.3
  draw "$BOW_STRAIGHT" "$DRAWN"
  for s in 1.2 1.6 2.0 2.4; do w "bow side $s" 1.0; dmp "riser-$s-eye"; done
  letdown "$BOW_STRAIGHT"
  w "bow side roll" 0.3; w "bow side 1.2" 0.3
  P26L="hand L head -6.6 -33.3 64.8 72.5 22.9 25.8"; P26R="hand R head 29.0 -40.0 -22.3 26.3 49.2 79.5"
  draw "$P26L" "$P26R"; xx "head 0 0 0 yaw 0 pitch -16 s 0.3"; sleep 0.6
  for s in 1.2 1.6 2.0 2.4; do w "bow side $s" 1.0; dmp "riser26-$s"; done
  echo "   $(bowline 200 | grep -oE "the arrow's side .*")"
  xx "head 0 0 0 yaw 0 pitch 0 s 0.3"; sleep 0.4; letdown "$P26L"
  w "bow side ${B37_SIDE_CM:-1.2}" 0.3
fi

if has roll; then
  echo "== roll: the player's own draws (11-04 22:09: the bow canted 4, 26, 57, -26, 86 deg), the fit + the side by the cant vs builds 16-36"
  isout || bow_out
  # cant | the bow hand | the string hand | the head's pitch (replay_input FILE bow --poses)
  POSES="4|hand L head -8.1 -22.3 66.0 81.0 -0.3 -10.7|hand R head 10.0 -21.2 10.3 104.0 22.1 15.4|-6
26|hand L head -0.6 -26.0 71.3 70.1 -24.4 -10.1|hand R head 13.0 -21.1 -9.2 95.0 39.8 30.5|-4
57|hand L head -6.5 14.4 68.8 103.3 -66.5 -37.6|hand R head 22.4 -26.1 -29.0 122.6 14.8 21.5|17
-26|hand L head -6.6 -33.3 64.8 72.5 22.9 25.8|hand R head 29.0 -40.0 -22.3 26.3 49.2 79.5|-16
86|hand L head -8.6 -29.9 67.4 -77.8 -83.3 151.3|hand R head 27.0 -40.5 -16.7 52.4 46.3 60.4|-14"
  while IFS='|' read -r c lp rp hp; do
    for mode in new old; do
      if [ "$mode" = new ]; then w "bow side roll" 0.2; w "bow fit on" 0.2; else w "bow side fixed" 0.2; w "bow fit off" 0.2; fi
      xx "head 0 0 0 yaw 0 pitch 0 s 0.2"; sleep 0.3
      n0=$(lines); draw "$lp" "$rp"
      xx "head 0 0 0 yaw 0 pitch $hp s 0.3"; sleep 0.8
      echo "   cant $c $mode: $(bowline | grep -oE "draw [0-9.]+ cm|the arrow's side .*" | tr '\n' ' ') $(sincel "$n0" "the arrow's side:" | tail -1 | cut -c1-140)"
      dmp "roll$c-$mode"
      xx "head 0 0 0 yaw 0 pitch 0 s 0.2"; sleep 0.3
      letdown "$lp"
    done
  done <<< "$POSES"
  w "bow side roll" 0.2; w "bow fit on" 0.2
  bow_away
fi

# ---- the hang (wall37-drive.sh's wall 1 and its climbing words) --------------------------------------------------------
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.6 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
tyaw() { grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'yaw [0-9.-]+' | head -1 | grep -oE '[0-9.-]+$'; }
croot() { w "climb state" 0.6 >/dev/null; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+' | cut -c6-; }
faceyaw() { local want=$1 k y d
  for k in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
    sleep 0.4; y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 16 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    [ "$d" -gt 0 ] && w "turn+" 0.9 >/dev/null || w "turn-" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
tothewall() { local k a
  for k in 1 2 3 4 5 6; do probe; a=$(ahead +0); echo "$(date +%T) the face ahead: ${a:-none} cm"
    [ -n "$a" ] && [ "$a" -le 45 ] && return 0; w "stick 0 0.5 0 0.5" 0.9; done; return 1; }
grabat() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.4
  for k in 1 2 3 4; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.2
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.2; done
    w "hands squeeze $h 0" 0.3; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.3
  done; return 1; }
ry() { croot | cut -d, -f2 | cut -d. -f1; }
if has hang; then
  echo "== hang: the bow from the climb's hands-free hang at an orc"
  isout && bow_away
  xx "hand L off" "hand R off" "btn L squeeze 0" "btn R squeeze 0" "btn R trigger 0"
  WALL=${B37_WALL:-"31757.2 2054.4 -28935.9@-161"}
  go "${WALL%@*}"; faceyaw "${WALL#*@}"; tothewall || echo "$(date +%T) no face within 45 cm"
  w "hands test L -20 -60 20 0 0 0" 0.3
  grabat R -25 40 || fail "no first hold"
  HOLD=R; y0=$(ry)
  for k in 1 2 3; do
    [ "$HOLD" = L ] && { HX=-20; O=R; } || { HX=20; O=L; }
    w "hands swing $HOLD $HX -80 ${GZ:-40} 0 0 0 1000" 1.4
    grabat $O -25 "${GZ:-40}" || grabat $O -25 30 || { echo "$(date +%T) move $k: no hold for $O"; break; }
    w "hands squeeze $HOLD 0" 0.3; w "hands test $HOLD $HX -60 15 0 0 0" 0.2; HOLD=$O
  done
  echo "$(date +%T) climbed $(( $(ry) - ${y0:-0} )) cm: $(croot)"
  n0=$(lines); w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.6
  h=$(sincel "$n0" '^CLIMB t=.* hang: no hand holds' | tail -1)
  [ -n "$h" ] && pass "the hang: $(echo "$h" | sed -E 's/^CLIMB t=[0-9.]+ frame=[0-9]+ //' | cut -c1-120)" || fail "no hang: $(sincel "$n0" '^CLIMB t=' | tail -2 | cut -c1-200)"
  w "hands test off" 0.5; w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3
  ROOT0=$(croot); TY=$(tyaw); echo "$(date +%T) hanging at $ROOT0, Talion's yaw $TY"
  # (boot 021658: an orc put first saw Talion in 3 s and hit him 10 s later - a hit drops the climb, SOMVR_CLIMB_HIT=drop;
  # so: the view turned from the wall, the bow out and drawn, then the orc 11 m ahead of the head, aimed and shot at once)
  BACK=$(python3 -c "print(round((float('$TY')+180+180)%360-180,1))"); faceyaw "$BACK"; HY=$(yaw)
  w "bow arrows 30" 0.5; dmp hang-turned
  n0=$(lines); bow_out
  l=$(sincel "$n0" 'the bow out in the hang'); [ -n "$l" ] && pass "out in the hang: $(echo "$l" | cut -c1-160)" || fail "no bow in the hang: $(sincel "$n0" '^BOW' | head -3 | cut -c1-200)"
  # bow20-drive.sh's aim: the anchor by the jaw, the bow hand 60 cm out along yaw A pitch P (head frame)
  pose() { awk -v a="$1" -v p="$2" -v d="${3:-57}" -v sm="${4:-}" 'BEGIN { r = 3.14159265 / 180; L = 60
    dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 16; ay = -22; az = 0
    printf "hand L head %.1f %.1f %.1f 90 0 0\n", ax + L * dx - 1.2, ay + L * dy, az + L * dz
    printf "hand R head %.1f %.1f %.1f 90 0 0%s\n", ax + (L - d) * dx, ay + (L - d) * dy, az + (L - d) * dz, sm != "" ? " s " sm : "" }'; }
  setpose() { local IFS=$'\n'; TS=${TS:-0.4} xx $(pose "$@") "btn L squeeze 1" "btn R trigger ${TRIG:-1}"; }
  aimat() { grep -E '^BOW target ' "$L" | tail -1 | sed -nE "s/.*the ${1:-head} at head yaw ([-+][0-9.]+) pitch ([-+][0-9.]+).*/\1 \2/p"; }
  offray() { grep -E '^BOW target ' "$L" | tail -1 | sed -nE "s/.*its ${1:-head} ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1 \2/p"; }
  meets() { grep -E '^BOW t=.* aim: the line meets' "$L" | tail -1 | sed -E 's/.*the line meets (an orc.s head|an orc.s body|the world|nothing).*/\1/'; }
  A=0; P=-8
  TRIG=0 setpose 0 -8 4; sleep 0.4; TRIG=1 setpose 0 -8 4; waitfor "$NOCK" 2; TRIG=1 setpose 0 -8 57 0.6; sleep 1.0
  echo "$(date +%T) drawn in the hang: $(bowline 120)"
  SPOT=$(python3 -c "import math; x,y,z=map(float,'$ROOT0'.split(',')); a=math.radians(float('${HY:-$BACK}')); print('%.0f %.0f %.0f'%(x+1100*math.sin(a), y, z+1100*math.cos(a)))")
  nt=$(cnt '^BOW target '); w "practice put $SPOT infantry" 0.2; waitfor '^PRACTICE t=.*spawn put' 4
  grep -E '^PRACTICE t=.*spawn put' "$L" | tail -1 | cut -c1-200
  for k in 1 2 3 4 5 6 7 8 9 10; do [ "$(cnt '^BOW target ')" -gt "$((nt + 1))" ] && break; sleep 0.3; done
  # (boots 022841-024534: an aware orc walks and the scripted aim lags it - the line met the world at the release three
  # times; the hands now aimed at its chest once, then the test word `bow aimhead` lays this shot's ray from the nock onto
  # its head: the shot's origin is still the nock where Talion hangs, its aim the test's)
  b=$(aimat chest); [ -n "$b" ] && { A=${b% *}; P=${b#* }; setpose "$A" "$P" 57; sleep 0.6; }
  w "bow aimhead" 0.3
  echo "$(date +%T) aimed yaw $A pitch $P: the line meets $(meets); $(bowline 260)"
  dmp hang-drawn
  R1=$(croot); n0=$(lines); nh=$(cnt '^BOW hit t=')
  TRIG=0 TS=0.1 setpose "$A" "$P" 57; sleep 2.0
  dmp hang-shot
  sincel "$n0" '^BOW (t=.* (release|fire|shot|our shot|aimhead|game arrow: flying)|hit t=)|^PRACTICE t=.*(dead|kill)|^CLIMB t=.* end:' | head -8 | cut -c1-230
  if [ "$(cnt '^BOW hit t=')" -gt "$nh" ]; then pass "the hit from the hang: $(grep -E '^BOW hit t=' "$L" | tail -1 | cut -c1-220)"
  else fail "no hit line from the hang"; fi
  w "practice remove" 0.5
  R2=$(croot); echo "$(date +%T) Talion's root drawn $R1, after the shot $R2 (hanging since $ROOT0)"
  # (the game's aim moves Talion's root ~30 cm out from the wall: boots 022841, 024534; the view is held instead - the
  # view's base must not move)
  vh=$(grep -E "^BOW t=.* the view's hold: " "$L" | tail -1); echo "   ${vh:-no view-hold line}" | cut -c1-200
  vb=$(echo "$vh" | grep -oE 'at most [0-9.]+' | grep -oE '[0-9.]+$')
  [ -n "$vb" ] && python3 -c "import sys; sys.exit(0 if float('$vb') < 2.0 else 1)" \
    && pass "the view still while the bow is out in the hang: its base moved $vb cm at most ($(echo "$vh" | grep -oE "Talion's root [0-9.]+ cm"))" \
    || fail "the view moved while aiming in the hang: ${vh:-no line}"
  grep -E '^CLIMB t=.* (last 1 s|end:)' "$L" | tail -2 | cut -c1-160
  n0=$(lines); letdown "$(pose "$A" "$P" 57 | head -1)"; bow_away
  l=$(sincel "$n0" 'the bow in the hang ends'); [ -n "$l" ] && pass "put away: $(echo "$l" | cut -c1-160)" || fail "no end line: $(sincel "$n0" '^BOW' | tail -3 | cut -c1-200)"
  # a grip on the wall climbs on
  xx "hand L off" "hand R off" "btn L squeeze 0" "btn R trigger 0"
  faceyaw "$TY"; n0=$(lines)
  grabat R -25 40; l=$(sincel "$n0" '^CLIMB t=.* resume:')
  [ -n "$l" ] && pass "a grip climbs on: $(echo "$l" | sed -E 's/^CLIMB t=[0-9.]+ frame=[0-9]+ //' | cut -c1-120)" || fail "no resume: $(sincel "$n0" '^CLIMB t=' | tail -2 | cut -c1-200)"
  w "hands squeeze R 0" 0.4; w "hands test off" 0.3; w "practice remove" 1
fi
echo "steps done $(date +%T)"; grep -cE ' (PASS|FAIL) ' "$ST" 2>/dev/null; [ "${NOQUIT:-0}" = 1 ] && exit 0
sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
