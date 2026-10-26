#!/bin/bash
# climb10-drive.sh - the climb helper's words for build 10 (climb-drive.sh's way to a wall and its hand-over-hand pulls):
#   the hang (SOMVR_CLIMB_LETGO=hang): after the 2nd pull both hands open (Talion stays; the per-second lines say hang,
#   the root still), the same hand squeezes again (resume); after the 3rd pull both open again, then A (the layer's test
#   button: `btn R a 1`): the drop and the fall; then the climb from the foot again, to the top (push, over);
#   the wall-top bug (build 9, marks 170126_20 / 170147_21): CLIMB_AT="X Y Z" first takes Talion there (practice back,
#   practice go: the teleport lands on the ground); on the top `climb watch` (a CLIMB state line per change of the state
#   tags and each second), standing, the rig turned (LOCO's standing turn), the stick along and off; then (CLIMB_LEDGE=1)
#   `climb loyaw game` (LOCO leaves the yaw to the game) and `climb ledges off` (CL_CTXACT: no context action for
#   Talion), the turn and the stick again.
# Env: CLIMB_AT (default: the pillar of build 9's bug, 33350 2000 -27900), CLIMB_PULLS (most pull pairs, default 9),
# CLIMB_HANG (1: the hang and the drop, default 1), CLIMB_LEDGE (1: the ledge tests after the top, default 1),
# CLIMB_INTO (1: at the top no mantle: the hands reach up and in onto the pillar over the narrow ledge, as the user did,
# and pull the body up and in over it; then both open (hang) and A: Talion lands on the ledge), CLIMB_WT. Stops at once
# (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CLIMB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
AT=${CLIMB_AT-33350 2000 -27900}
PULLS=${CLIMB_PULLS:-9}
HANG=${CLIMB_HANG:-1}
LEDGE=${CLIMB_LEDGE:-1}
INTO=${CLIMB_INTO:-0}
PULL_MS=1000
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
probe() { local n0 i; n0=$(grep -c '^CLIMB probe surfaces' "$R/somvr.log"); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(grep -c '^CLIMB probe surfaces' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.25; done
  grep '^CLIMB probe' "$R/somvr.log" | tail -$([ -n "$1" ] && echo 2 || echo 5) | cut -c1-420; }
near() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbing() { grep -E '^CLIMB t=.* (start|end):' "$R/somvr.log" | tail -1 | grep -q ' start:'; }
headnow() { grep -E '^CLIMB t=.*(last 1 s|top:)' "$R/somvr.log" | tail -1 | grep -oE 'head [0-9.]+' | tail -1 | cut -d' ' -f2; }
topnow() { grep -E '^CLIMB t=.* grab [LR]:.*edge 1, top ' "$R/somvr.log" | tail -1 | grep -oE 'top [0-9.]+' | cut -d' ' -f2; }
lastcl() { grep -E '^CLIMB t=' "$R/somvr.log" | tail -${1:-3} | cut -c1-330; }
st() { grep -E '^CLIMB state' "$R/somvr.log" | tail -${1:-1} | cut -c1-560; }
lo() { grep -E '^LOCO t=.*last 1 s' "$R/somvr.log" | tail -${1:-2} | sed -E 's/follow in our.*yaw ours/... yaw ours/; s/dt timer.*between/... between/' | cut -c1-300; }

# 0. the controls card away, both test squeezes open; to the wall's foot
m k00-arrival 0.5
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
if [ -n "$AT" ]; then
  w "practice back" 1
  waitfor '^PRACTICE t=.* arrived at' 30 || echo "$(date +%T) no arrival back"
  sleep 2
  w "practice go $AT" 1
  waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-220
  sleep 3
fi
w "climb watch" 1
# 1. the nearest climbable wall, faced; 2. walked to (climb-drive.sh's way)
probe
NB=$(near -50); [ -n "$NB" ] || NB=$(near +0)
echo "$(date +%T) nearest climbable: ${NB:-none}"
[ -n "$NB" ] || { m k01-no-wall 0; echo quit >> "$R/cmd"; exit 1; }
turnto "${NB% *}"
probe
for k in $(seq 1 25); do
  d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
  [ -n "$d" ] || { NB=$(near -50); echo "$(date +%T) nothing ahead; nearest ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"; probe >/dev/null; continue; }
  echo "$(date +%T) ahead $d cm"
  [ "$d" -le 62 ] && break
  if [ "$d" -gt 500 ]; then w "stick 0 1 0 2" 2.4; elif [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
  probe >/dev/null
  [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && { echo "$(date +%T) stopped at $d cm (the wall)"; break; }
  PREV=$d
done
probe up
m k01-at-wall 0.3
REACH=50
w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
grab() { local h=$1 y=$2 z=$REACH xs n0 i; [ "$h" = L ] && xs=-20 || xs=20
  for k in $(seq 1 ${3:-4}); do
    n0=$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log")
    w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done
    lastcl 1
    w "hands squeeze $h 0" 0.5; z=$((z + 12)); w "hands test $h $xs $y $z 0 0 0" 0.5
  done; return 1; }
grabtop() { local h=$1 xs rz n0 i z=55 y; [ "$h" = L ] && { xs=-20; rz=-90; } || { xs=20; rz=90; }
  for y in 30 0 -30 -60; do
    w "hands squeeze $h 0" 0.4; w "hands test $h $xs $y $z 0 0 $rz" 0.5
    n0=$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log")" -gt "$n0" ] && { TOPZ=$z; return 0; }; sleep 0.25; done
    lastcl 1
  done; return 1; }
NTOP0=$(grep -cE '^CLIMB t=.* top: ' "$R/somvr.log")
attop() { [ "$(grep -cE '^CLIMB t=.* top: ' "$R/somvr.log")" -gt "$NTOP0" ] ||
  grep -E "^CLIMB t=.* grab $1:" "$R/somvr.log" | tail -1 | grep -q 'edge 1'; }
grab L 0 || { echo "$(date +%T) no grab L"; lastcl 4; }
lastcl 3
HOLD=L; HANGED=0; DROPPED=0; TOP=""
for k in $(seq 1 $((PULLS * 2))); do
  climbing || { echo "$(date +%T) not climbing: $(lastcl 1)"; break; }
  [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
  w "hands swing $HOLD $HX -45 $REACH 0 0 0 $PULL_MS" 1.4
  climbing || { echo "$(date +%T) ended in the pull: $(lastcl 2)"; break; }
  if [ "$HANG" = 1 ] && [ $HANGED = 0 ] && [ $k = 2 ]; then
    # the hang: the only hold opens; 4 s; the same hand squeezes again: resume
    w "hands squeeze $HOLD 0" 0.3
    waitfor '^CLIMB t=.* hang: ' 3 && echo "$(date +%T) HANG: $(grep -E '^CLIMB t=.* hang: ' "$R/somvr.log" | tail -1 | cut -c1-260)"
    m k02-hanging 0.3; sleep 3
    grep -E '^CLIMB t=.*last 1 s' "$R/somvr.log" | tail -3 | cut -c1-200
    st 1
    w "hands squeeze $HOLD 1" 0.6
    waitfor '^CLIMB t=.* resume: ' 3 && echo "$(date +%T) RESUME: $(grep -E '^CLIMB t=.* resume: ' "$R/somvr.log" | tail -1 | cut -c1-200)"
    HANGED=1
    continue
  fi
  if [ "$HANG" = 1 ] && [ $DROPPED = 0 ] && [ $k = 3 ]; then
    # both open, then A: the drop (the fall); then the climb again from the foot
    w "hands squeeze $HOLD 0" 0.5
    waitfor '^CLIMB t=.* hang: ' 3 && echo "$(date +%T) HANG 2"
    sleep 1.5
    x "btn R a 1"; x "btn R a 0"
    waitfor '^CLIMB t=.* end: drop' 3 && echo "$(date +%T) DROP: $(grep -E '^CLIMB t=.* end: ' "$R/somvr.log" | tail -1 | cut -c1-220)"
    m k03-dropped 1.2
    DROPPED=1
    w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
    grab L 0 || { echo "$(date +%T) no grab L after the drop"; lastcl 4; break; }
    HOLD=L
    continue
  fi
  attop "$HOLD" && { TOP=$HOLD; break; }
  w "hands test $O $OX 20 $REACH 0 0 0" 0.5
  if ! grab $O 20 2; then
    if grabtop $O; then TOP=$O; echo "$(date +%T) the top held by $O (z $TOPZ)"; else echo "$(date +%T) no grab $O"; break; fi
  fi
  w "hands squeeze $HOLD 0" 0.6
  w "hands test $HOLD $HX $([ -n "$TOP" ] && echo -40 || echo 20) $REACH 0 0 0" 0.5
  HOLD=$O
  lastcl 2
  attop $O && TOP=$O
  [ -n "$TOP" ] && break
done
m k05-high 0
if [ -n "$TOP" ] && [ "$INTO" = 1 ]; then
  # up and in: the free hand onto the face above the ledge (80 cm ahead, then farther), pulled down and toward the body
  H=$TOP
  for rd in 1 2 3 4; do
    climbing || break
    [ "$H" = L ] && { O=R; OX=20; HX=-20; } || { O=L; OX=-20; HX=20; }
    GOT=""
    for z in 75 90 105; do
      w "hands squeeze $O 0" 0.3; w "hands test $O $OX 30 $z 0 0 0" 0.4
      n0=$(grep -cE "^CLIMB t=.* grab $O:" "$R/somvr.log"); w "hands squeeze $O 1" 0.8
      [ "$(grep -cE "^CLIMB t=.* grab $O:" "$R/somvr.log")" -gt "$n0" ] && { GOT=$z; break; }
    done
    echo "$(date +%T) into $rd: $O ${GOT:+held at z $GOT}${GOT:-found nothing}"; lastcl 1
    [ -n "$GOT" ] || break
    w "hands squeeze $H 0" 0.4; w "hands test $H $HX -40 10 0 0 0" 0.3
    w "hands swing $O $OX -35 30 0 0 0 1500" 2.0
    st 1; lastcl 2
    H=$O
  done
  m k05c-into 0.3
  TOP=""
fi
if [ -n "$TOP" ]; then
  # the mantle (climb-drive.sh plan t): the top hand pushes down, then toward the body; the other onto the top
  [ "$TOP" = L ] && { TX=-20; OT=R; OX=20; } || { TX=20; OT=L; OX=-20; }
  echo "$(date +%T) the top held by $TOP: $(grep -E "^CLIMB t=.* grab $TOP:" "$R/somvr.log" | tail -1 | grep -oE 'edge [01][^;]*')"
  w "hands squeeze $OT 0" 0.3; w "hands test $OT $OX -40 10 0 0 0" 0.5
  w "hands swing $TOP $TX -90 40 0 0 0 1800" 2.4
  lastcl 3; m k05b-pushed 0
  TYH=-90
  for stg in 1 2 3; do
    climbing || break
    w "hands swing $TOP $TX $TYH -40 0 0 0 2000" 2.6; lastcl 2
    climbing || break
    [ $stg = 3 ] && break
    TY=$(topnow); HY=$(headnow); GOT=""
    [ "$OT" = L ] && OR=-90 || OR=90
    for dy in 10 -5 -20; do
      REL=$(awk "BEGIN{printf \"%d\", $TY - $HY + $dy}")
      w "hands squeeze $OT 0" 0.3; w "hands test $OT $OX $REL 40 0 0 $OR" 0.4
      n0=$(grep -cE "^CLIMB t=.* grab $OT:" "$R/somvr.log"); w "hands squeeze $OT 1" 1.0
      [ "$(grep -cE "^CLIMB t=.* grab $OT:" "$R/somvr.log")" -gt "$n0" ] && { GOT=$REL; break; }
    done
    echo "$(date +%T) stage $stg: top $TY head $HY: the $OT hand ${GOT:+held at $GOT}${GOT:-found nothing}"
    [ -n "$GOT" ] || { lastcl 1; break; }
    w "hands squeeze $TOP 0" 0.5; w "hands test $TOP $TX -40 10 0 0 0" 0.4
    X=$TOP; TOP=$OT; OT=$X; X=$TX; TX=$OX; OX=$X; TYH=$((GOT - 10))
  done
  m k05d-over 0.8
  grep -E '^CLIMB t=.* end: ' "$R/somvr.log" | tail -1 | cut -c1-220
fi
# both hands open (hang, or nothing if over); A if still on
w "hands squeeze L 0"; w "hands squeeze R 0" 1.5
climbing && { echo "$(date +%T) still on (hang): A"; x "btn R a 1"; x "btn R a 0"; sleep 1.5; }
lastcl 3
w "hands test off"
st 2; lo 2
m k06-on-top 0.5
if [ "$LEDGE" = 1 ]; then
  # the top: standing 5 s, the rig turned 120 deg (LOCO's standing turn), the stick right/left/ahead
  sleep 5; st 1; lo 2
  for k in 1 2 3 4; do w "turn+" 0.7; done
  sleep 4; st 2; lo 3
  m k07-turned 0.3
  for s in "1 0" "-1 0"; do w "stick $s 0 2" 3.2; st 1; lo 3; done
  m k08-walked 0.3
  # LOCO leaves the yaw to the game; turn and stick again
  w "climb loyaw game" 0.5
  for k in 1 2 3 4; do w "turn-" 0.7; done
  sleep 4; st 1; lo 3
  for s in "1 0" "-1 0"; do w "stick $s 0 2" 3.2; st 1; lo 3; done
  w "climb loyaw ours" 0.5
  m k09-loyaw 0.3
  # no context action for Talion: does the game let go of him? stand, turn, stick off
  w "climb ledges off" 1
  sleep 3; st 2; lo 2
  for k in 1 2 3 4; do w "turn+" 0.7; done
  sleep 4; st 1; lo 3
  for s in "1 0" "-1 0" "0 1" "0 -1"; do w "stick $s 0 2" 3.2; st 1; lo 3; done
  m k10-ledges-off 0.3
  w "climb ledges game" 0.5
fi
w "climb watch off" 1
grep -E '^CLIMB t=.*(start|end|hang|resume):' "$R/somvr.log" | cut -c1-240
echo "steps done $(date +%T)"
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
