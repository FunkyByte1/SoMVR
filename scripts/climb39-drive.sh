#!/bin/bash
# climb39-drive.sh - som-b37-climb2 (build 39, docs/re-xr-pipeline.md 172): the 11-04 22:09 session's walls
# (logs/win-session-20261104-220933). PLAN39 words, in order (default "corner bumps walkup dome"):
#   corner  marks 222115_11 / 222139_12: Talion at the first face's foot (the user's climb start), faced to it, a hold
#           at the chest pulled up 3 times (off the ground), the head put 42 cm to the left (the user's room offset:
#           along the first face, toward the second after the turn), the hold let go (the hang), then the move stick
#           right 9 s: the slide along the face and round the outside corner (SOMVR_CLIMB_SIDE). Both eyes every 1.5 s
#           (the head turned to the second face after the corner). A_B39=1: again with `climb headkeep off` (the head's
#           keep off: the head into the second face, HEADROCK's fades).
#   bumps   mark 222911_23: the rough wall's foot (the user's own start), hand over hand at the chest (wall37's way)
#           MOVES39 pulls; A_B39=1: again with `climb thrkeep off` (each through ends 10 cm on at the bumps' contact).
#   walkup  mark 222603_18: Talion at the user's spot on the coping, the stick forward along the user's heading: the
#           walk's lines (WALK lip / UNPIN), a probe of the beam ahead (rays), both eyes.
#   dome    the coordinator's mark 223924_11 (the dome): Talion at the user's spot, faced to the dome, `climb probe`
#           (what the rays meet: distance and kind), a grip at the dome's face at the chest and over the head.
# Attaches to the newest boot of this worktree (PIN_RUN=RUN to pin). Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
PLAN39=${PLAN39:-"corner bumps walkup dome"}
MOVES39=${MOVES39:-24}; WY=${WY:--25}; PY=${PY:--80}
ST=$R/climb39-steps.txt
aon() { printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 0.3; }
aoff() { printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 0.6; }
dd() { local f; printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$ST"; }
root() { local n0 k; n0=$(grep -c '^CLIMB state' "$L"); w "climb state" 0.25
  for k in $(seq 1 25); do [ "$(grep -c '^CLIMB state' "$L")" -gt "$n0" ] && break; sleep 0.1; done
  grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+ yaw [0-9.-]+'; }
ry() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2 | cut -d. -f1; }
unclimb() { climbing || return 0; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; aon; sleep 1.4; aoff; sleep 2
  climbing && { aon; sleep 1.4; aoff; sleep 2; }; climbing && echo "$(date +%T) STILL climbing"; }
grabat() { local h=$1 y=$2 z=$3 xs=${4:-} n0 i k; [ -n "$xs" ] || { [ "$h" = L ] && xs=-20 || xs=20; }
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.4
  for k in 1 2 3 4; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.2
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.2; done
    w "hands squeeze $h 0" 0.3; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.3
  done; return 1; }
lines() { tail -n +"$1" "$L" | grep -E "${2:-^CLIMB t=.* (side|keep|through|corner)}" | sed -E 's/^CLIMB t=[0-9.]+ frame=[0-9]+ //' |
  cut -c1-${3:-230} | tail -${4:-12}; }
tothewall() { local k a
  for k in 1 2 3 4 5 6; do probe; a=$(ahead +0); echo "$(date +%T) the face ahead: ${a:-none} cm"
    [ -n "$a" ] && [ "$a" -le 45 ] && return 0; w "stick 0 0.5 0 0.5" 0.9; done; return 1; }
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.6 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
faceyaw() { local want=$1 k y d
  for k in 1 2 3 4 5 6 7 8 9 10 11 12; do
    sleep 0.4; y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 16 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    [ "$d" -gt 0 ] && w "turn+" 0.9 >/dev/null || w "turn-" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
reset() { unclimb; x "btn R squeeze 0"; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.3
  x "head 0 0 0 yaw 0 pitch 0 s 0.3"; w "stick 0 0 0 0" 0.3; }
# hand over hand at the chest, n pulls (wall37's): the rise in cm
climbup() { local n=$1 tag=$2 k HOLD=R HX O OX y0 y1 rise total=0 stalls=0 n0 thr
  w "hands test L -20 -60 20 0 0 0" 0.3
  grabat R "$WY" 40 || { echo "$(date +%T) $tag: no first hold at the chest"; return 1; }
  for k in $(seq 1 "$n"); do
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    climbing || { echo "$(date +%T) $tag move $k: not climbing"; break; }
    n0=$(wc -l < "$L"); y0=$(ry)
    w "hands swing $HOLD $HX $PY ${GZ:-40} 0 0 0 1000" 1.4
    y1=$(ry); rise=$(( ${y1:-0} - ${y0:-0} )); total=$((total + rise))
    thr=$(tail -n +"$n0" "$L" | grep -cE '^CLIMB t=.* through: the pull blocked')
    printf '%s %s move %d (%s): %+d cm (the root %s), throughs %d, kept %d\n' "$(date +%T)" "$tag" "$k" "$HOLD" "$rise" "$y1" "$thr" \
      "$(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* through kept')" | tee -a "$ST"
    [ "$rise" -lt 15 ] && stalls=$((stalls + 1))
    [ "$k" -lt "$n" ] || break
    grabat $O "$WY" "${GZ:-40}" || grabat $O "$WY" 30 || { echo "$(date +%T) $tag move $k: no hold for $O at the chest"; break; }
    w "hands squeeze $HOLD 0" 0.3; w "hands test $HOLD $HX -60 15 0 0 0" 0.2; HOLD=$O
  done
  CLIMBED=$total; LASTHOLD=$HOLD
  echo "CLIMBUP $tag: $total cm up in $k pulls, stalls (under 15 cm) $stalls; $(root)" | tee -a "$ST"; }

corner() { local tag=$1 n0 i
  echo "=== corner ($tag)"; reset
  go "33855.2 2014.3 -27219.6"; faceyaw -150; tothewall || echo "$(date +%T) no face within 45 cm"   # (the face's foot:
  # the user's climb start at 2799 is mid-wall - the trip cannot land there; the same face and corner, 8 m lower)
  echo "$(date +%T) at the foot: $(root)"
  climbup 3 "corner-$tag" || return 1
  x "head -42 0 0 yaw 0 pitch 0 s 0.5"; sleep 1
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.4
  sleep 1; echo "$(date +%T) hanging, the head 42 cm left: $(root)"; dd "corner-$tag-start"
  n0=$(wc -l < "$L")
  w "stick 1 0 0 10" 0.1
  for i in 1 2 3 4 5 6; do sleep 0.3; dd "corner-$tag-t$i"; done
  x "head -42 0 0 yaw -80 pitch 0 s 0.5"; sleep 0.6   # (the head turned to the second face, as the user's after the corner: -80 = right in the head word)
  for i in 7 8 9; do dd "corner-$tag-t$i"; done
  w "stick 0 0 0 0" 1
  echo "$(date +%T) after the slide: $(root)"
  lines "$n0" '^(CLIMB t=.* (side|keep|corner)|HEADROCK t=)' 240 20 | sed 's/^/    /'
  echo "CORNER $tag: slides $(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* side:'), corner turns $(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* side corner'), keep episodes $(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* keep: the head'), headrock fades $(tail -n +"$n0" "$L" | grep -c '^HEADROCK t=.* the view fades')" | tee -a "$ST"
  x "head 0 0 0 yaw 0 pitch 0 s 0.3"; reset; }

bumps() { local tag=$1
  echo "=== bumps ($tag)"; reset
  go "33901.7 1939.2 -25928.9"; faceyaw 91; tothewall || echo "$(date +%T) no face within 45 cm"
  echo "$(date +%T) at the foot: $(root)"; dd "bumps-$tag-foot"
  local n0; n0=$(wc -l < "$L")
  climbup "$MOVES39" "bumps-$tag"
  dd "bumps-$tag-end"
  echo "BUMPS $tag: $CLIMBED cm; throughs $(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* through: the pull'), kept $(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* through kept'), outs $(tail -n +"$n0" "$L" | grep -c '^CLIMB t=.* through over')" | tee -a "$ST"
  reset; }

walkup() { local n0
  echo "=== walkup (mark 222603_18)"; reset
  go "31237.2 4816.5 -30684.4"; faceyaw 48; echo "$(date +%T) at: $(root)"; dd "walkup-at"
  probe; grep '^CLIMB probe t=' "$L" | tail -3 | cut -c1-400
  w "climb probe edge" 1.2; grep '^CLIMB probe' "$L" | tail -2 | cut -c1-400
  n0=$(wc -l < "$L")
  w "stick 0 0.7 0 3" 3.5; echo "$(date +%T) after 3 s forward: $(root)"; dd "walkup-fwd"
  lines "$n0" '^(WALK|UNPIN|JUMP [a-z]+ t)' 260 10 | sed 's/^/    /'
  faceyaw 24; n0=$(wc -l < "$L")
  w "stick 0 0.7 0 3" 3.5; echo "$(date +%T) after 3 s forward (24 deg): $(root)"; dd "walkup-fwd2"
  lines "$n0" '^(WALK|UNPIN|JUMP [a-z]+ t)' 260 10 | sed 's/^/    /'; reset; }

dome() { local n0
  echo "=== dome (mark 223924_11)"; reset
  go "35061.4 6113.7 -22037.9"; faceyaw 111; echo "$(date +%T) at: $(root)"; dd "dome-at"
  probe; grep '^CLIMB probe t=' "$L" | tail -3 | cut -c1-400
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -2 | cut -c1-400
  n0=$(wc -l < "$L")
  w "stick 0 0.6 0 3" 3.5; echo "$(date +%T) after 3 s into the dome: $(root)"; dd "dome-walk"
  lines "$n0" '^(LOCO t=.* last 1 s|UNPIN|WALK)' 200 5 | sed 's/^/    /'
  probe; grep '^CLIMB probe t=' "$L" | tail -3 | cut -c1-400
  n0=$(wc -l < "$L")
  grabat R -10 40 && echo "$(date +%T) the dome held at the chest" || echo "$(date +%T) no hold at the chest"
  lines "$n0" '^CLIMB t=.* (grab|press)' 300 4 | sed 's/^/    /'
  climbing && { climbup 6 dome; dd "dome-climb"; }
  reset; }

card
for p in $PLAN39; do
  case $p in
    corner) corner on; [ "${A_B39:-0}" = 1 ] && { w "climb headkeep off" 0.5; corner off; w "climb headkeep on" 0.5; } ;;
    bumps) bumps on; [ "${A_B39:-0}" = 1 ] && { w "climb thrkeep off" 0.5; bumps off; w "climb thrkeep on" 0.5; } ;;
    walkup) walkup ;;
    dome) dome ;;
  esac
done
grep -E '^CLIMB END build 39' "$L" | tail -1 | cut -c1-400
echo "steps done $(date +%T)"; [ "${NOQUIT:-0}" = 1 ] && exit 0
sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
