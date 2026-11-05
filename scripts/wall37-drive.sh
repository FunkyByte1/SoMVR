#!/bin/bash
# wall37-drive.sh - live patch som-live-wall (11-04, HANDOFF-climb.md "Live patch 11-04"): the user's two rock walls of
# session win-session-20261104-195050 (marks 2 and 4: "stuck on this wall"), climbed the way they climbed them - every
# hold at the CHEST (WY cm to the eyes, default -25) pulled to the WAIST (PY, default -80) in 1 s, hand over hand.
# Build 37 held Talion there (the capsule's top caught under the face's bumps, the holds under the eyes: no through,
# the pullover's false "no way up" buzz); SOMVR_CLIMB_THROUGH_LOW lets such a pull go through. Per move one line: the
# rise, the through / no-way lines; both eyes dumped every DUMP_EVERY moves (the view never in the rock).
#   WALLS="name:x y z[@yaw];..." (the foot to start from, the heading to face; default the two walls' feet: wall 1 at the
#   user's own spot and heading - a probe's nearest face took the A run 1 m west, where the face has no bumps), MOVES (12),
#   NOQUIT=1 (leave the game running for another driver), A_B=1: each wall again
#   with `climb throughlow off` (needs a build with that word; this branch's has it).
# Attaches to the newest boot of this worktree (PIN_RUN=RUN to pin). Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
WALLS=${WALLS:-"wall1:31757.2 2054.4 -28935.9@-161;wall2:32050.8 2505.3 -29482.9"}
MOVES=${MOVES:-12}; WY=${WY:--25}; PY=${PY:--80}; DUMP_EVERY=${DUMP_EVERY:-3}
ST=$R/wall37-steps.txt
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
since() { tail -n +"$1" "$L" | grep -E '^CLIMB t=.* (through|no way up|top:|hang:|end:|fling (launch|apex)|mantle:|pullover: no way|blocked)' |
  sed -E 's/^CLIMB t=[0-9.]+ frame=[0-9]+ //' | cut -c1-${2:-200} | tail -${3:-4}; }
facewall() { local nb; probe; nb=$(near +0); [ -n "$nb" ] || nb=$(near -50); echo "$(date +%T) the nearest face: ${nb:-none}"
  [ -n "$nb" ] && turnto "${nb% *}"; }
tothewall() { local k a
  for k in 1 2 3 4 5 6; do probe; a=$(ahead +0); echo "$(date +%T) the face ahead: ${a:-none} cm"
    [ -n "$a" ] && [ "$a" -le 45 ] && return 0; w "stick 0 0.5 0 0.5" 0.9; done; return 1; }
# the head's yaw (deg) and a turn to $1 (the snap/smooth turn words), as climb27b-drive.sh
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.6 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
faceyaw() { local want=$1 k y d
  for k in 1 2 3 4 5 6 7 8 9 10 11 12; do
    sleep 0.4; y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 16 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    [ "$d" -gt 0 ] && w "turn+" 0.9 >/dev/null || w "turn-" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
# one wall: the foot, faced (the user's own heading when given: "x y z@yaw"), hand over hand at the chest
wall() { local nm=$1 at=$2 tag=$3 k HOLD HX O OX y0 y1 rise n0 nw0 th0 stalls=0 total=0 thr nw
  echo "=== $nm ($tag) from $at"
  unclimb; x "btn R squeeze 0"; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.3
  local fy=; case $at in *@*) fy=${at#*@}; at=${at%@*} ;; esac
  go "$at"
  if [ -n "$fy" ]; then faceyaw "$fy"; tothewall || echo "$(date +%T) no face within 45 cm"
  else facewall; tothewall || { facewall; tothewall; } || echo "$(date +%T) no face within 45 cm"; fi
  y0=$(ry); echo "$(date +%T) at the foot: $(root)"; dd "$nm-$tag-foot"
  nw0=$(cnt 'no way up here'); th0=$(cnt '^CLIMB t=.* through: the pull blocked')
  w "hands test L -20 -60 20 0 0 0" 0.3
  grabat R "$WY" 40 || { echo "$(date +%T) $nm: no first hold at the chest"; return 1; }
  HOLD=R
  for k in $(seq 1 "$MOVES"); do
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    climbing || { echo "$(date +%T) $nm move $k: not climbing"; break; }
    n0=$(wc -l < "$L"); y0=$(ry)
    w "hands swing $HOLD $HX $PY ${GZ:-40} 0 0 0 1000" 1.4      # the pull: the chest to the waist in 1 s
    y1=$(ry); rise=$(( ${y1:-0} - ${y0:-0} )); total=$((total + rise))
    thr=$(tail -n +"$n0" "$L" | grep -cE '^CLIMB t=.* through: the pull blocked'); nw=$(tail -n +"$n0" "$L" | grep -c 'no way up here')
    printf '%s %s move %d (%s): %+d cm (the root %s), through %d, no-way buzz %d\n' "$(date +%T)" "$nm" "$k" "$HOLD" "$rise" "$y1" "$thr" "$nw" | tee -a "$ST"
    [ "$rise" -lt 15 ] && { stalls=$((stalls + 1)); since "$n0" 240 4 | sed 's/^/    /'; }
    [ $((k % DUMP_EVERY)) = 0 ] && dd "$nm-$tag-move$k"
    grabat $O "$WY" "${GZ:-40}" || grabat $O "$WY" 30 || { echo "$(date +%T) $nm move $k: no hold for $O at the chest"; since "$n0" 240 4; break; }
    w "hands squeeze $HOLD 0" 0.3; w "hands test $HOLD $HX -60 15 0 0 0" 0.2; HOLD=$O
  done
  dd "$nm-$tag-end"
  echo "WALL $nm ($tag): $total cm up in $k moves, stalls (under 15 cm) $stalls, throughs $(( $(cnt '^CLIMB t=.* through: the pull blocked') - th0 )), no-way buzzes $(( $(cnt 'no way up here') - nw0 )); $(root)" | tee -a "$ST"
  unclimb; }
card
IFS_=$IFS; IFS=';'; for s in $WALLS; do IFS=$IFS_
  wall "${s%%:*}" "${s#*:}" on
  if [ "${A_B:-0}" = 1 ]; then w "climb throughlow off" 0.5; wall "${s%%:*}" "${s#*:}" off; w "climb throughlow on" 0.5; fi
  IFS=';'; done; IFS=$IFS_
grep -E '^CLIMB END build 16 round 2' "$L" | tail -1 | cut -c1-300
echo "steps done $(date +%T)"; [ "${NOQUIT:-0}" = 1 ] && exit 0   # (NOQUIT=1: another driver goes on in this boot)
sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
