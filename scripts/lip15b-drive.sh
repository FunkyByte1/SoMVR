#!/bin/bash
# lip15b-drive.sh - build 15b: the lip the user was stuck under (session 20261026-165456 mark 8, t=792073.5: Talion's
# root 33660.6 2690.9 -27345.8, yaw 135.9; a slab at ~2812 sticking out ~21 cm over the face under it), headless, with
# climb13-drive.sh's way to a wall: `practice go` to the meadow bank's foot (LIP_AT, default 33600 2000 -27310: the
# user's start, 7 m under the slab), the probe's nearest wall, snap turns, the stick to it; then hand over hand up
# (`pull`: the hold 45 cm down in 1 s, the other hand 20 cm up), each grab's line logged, until a hold at the slab
# (its top LIP_TOP_MIN or more, default 2790) or LIP_PULLS (24) pulls; then the pull over it as a player would: the slab
# hand pulled down to the chest (1 s) LIP_OVER times (4), the other hand regrabbing higher each time; then the
# fling over it (the hand down and toward the body fast, released); the root's height after each (`secs`), the eyes
# (`m NAME`) at the slab, after the pulls and after the fling; A at the end (the blink down). Twice: the word `climb
# overhang off` first (build 15: the repro), then `climb overhang on` (the fix). Env: LIP_AT, LIP_WT,
# LIP_PULLS, LIP_TOP_MIN, LIP_OVER. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${LIP_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
AT=${LIP_AT:-33600 2000 -27310}
PULLS=${LIP_PULLS:-24}; TOPMIN=${LIP_TOP_MIN:-2790}; OVER=${LIP_OVER:-4}
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
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
probe() { local n0 i; n0=$(cnt '^CLIMB probe surfaces'); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(cnt '^CLIMB probe surfaces')" -gt "$n0" ] && break; sleep 0.25; done
  grep '^CLIMB probe' "$L" | tail -$([ -n "$1" ] && echo 2 || echo 5) | cut -c1-300; }
near() { grep "^CLIMB probe t=.*at head $1 cm" "$L" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$L" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbing() { grep -E '^CLIMB t=.* (start|end):' "$L" | tail -1 | grep -q ' start:'; }
lastcl() { grep -E '^CLIMB t=' "$L" | tail -${1:-3} | cut -c1-330; }
fl() { grep -E '^CLIMB t=.* (fling|end:|hang:|resume:|grab [LR]|top:|lip |over)' "$L" | tail -${1:-4} | cut -c1-420; }
secs() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -${1:-3} | cut -c1-260; }
root() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -1 | grep -oE 'now [0-9.,-]+' ; }
REACH=50
grab() { local h=$1 y=$2 z=$REACH xs n0 i; [ "$h" = L ] && xs=-20 || xs=20
  for k in $(seq 1 ${3:-4}); do
    n0=$(cnt "^CLIMB t=.* grab $h:")
    w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && return 0; sleep 0.25; done
    lastcl 1
    w "hands squeeze $h 0" 0.5; z=$((z + 12)); w "hands test $h $xs $y $z 0 0 0" 0.5
  done; return 1; }
pull() { local H=$1 O HX OX; [ "$H" = L ] && { O=R; HX=-20; OX=20; } || { O=L; HX=20; OX=-20; }
  w "hands swing $H $HX -45 $REACH 0 0 0 1000" 1.4
  w "hands test $O $OX 20 $REACH 0 0 0" 0.5
  grab $O 20 3 || return 1
  w "hands squeeze $H 0" 0.6
  w "hands test $H $HX 20 $REACH 0 0 0" 0.5; }
toface() {
  w "climb watch" 1
  probe
  NB=$(near -50); [ -n "$NB" ] || NB=$(near +0)
  echo "$(date +%T) nearest climbable: ${NB:-none}"
  [ -n "$NB" ] || { m l01-no-wall 0; return 1; }
  turnto "${NB% *}"
  probe >/dev/null
  PREV=""
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
  w "climb watch off" 0.5
  probe up
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
}
# the hold's top (a grab line's "a lip: its top N" or "top N (found)"), 0 when none
holdtop() { grep -E "^CLIMB t=.* grab $1:" "$L" | tail -1 | grep -oE '(its top|top) [0-9]+' | tail -1 | grep -oE '[0-9]+$' || echo 0; }

m l00-arrival 0.5
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
attempt() { # $1 = off|on: SOMVR_CLIMB_OVERHANG for this climb (the word `climb overhang`, a test aid)
  local OH=$1 H O HX OX SLAB t k
  echo "== attempt overhang $OH"
  w "climb overhang $OH" 0.5
  w "practice go $AT" 1
  waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-200
  sleep 3
  toface || { echo "$(date +%T) no wall"; return 1; }
  m l01-$OH-at-wall 0.3
  grab L 0 || echo "$(date +%T) no grab L"
  H=L; SLAB=""
  for k in $(seq 1 "$PULLS"); do
    climbing || { echo "$(date +%T) not climbing (on a ledge?): $(lastcl 1)"; w "hands squeeze L 0"; w "hands squeeze R 0"
      w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 0.5; grab L 0 5 || break; H=L; }
    t=$(holdtop $H); echo "$(date +%T) pull $k: hold $H top ${t%.*} root $(root)"
    [ "${t%.*}" -ge "$TOPMIN" ] && { SLAB=$H; break; }
    [ "$H" = L ] && O=R || O=L
    if pull $H; then H=$O; else
      # no hold for the other hand (a ledge's top under the next face, out of reach): the holding hand down and
      # toward the body (the push over the ledge's edge: the end over, standing on it; then a new climb from there)
      echo "$(date +%T) the pull failed: the push over: $(lastcl 1)"
      [ "$H" = L ] && HX=-20 || HX=20
      w "hands squeeze $O 0" 0.3; w "hands test $O $([ $O = L ] && echo -20 || echo 20) -30 20 0 0 0" 0.5
      w "hands swing $H $HX -40 10 0 0 0 1200" 2.5
      fl 3
    fi
    t=$(holdtop $H); [ "${t%.*}" -ge "$TOPMIN" ] && { SLAB=$H; echo "$(date +%T) at the slab: $H top $t"; break; }
  done
  fl 6; secs 2
  m l02-$OH-at-slab 0.3
  echo "== the pull over the slab (hand ${SLAB:-none}, overhang $OH)"
  [ -n "$SLAB" ] || SLAB=$H
  for k in $(seq 1 "$OVER"); do
    climbing || break
    [ "$SLAB" = L ] && { O=R; HX=-20; OX=20; } || { O=L; HX=20; OX=-20; }
    w "hands swing $SLAB $HX -30 30 0 0 0 1000" 1.6
    secs 1
    w "hands test $O $OX 35 $REACH 0 0 0" 0.5
    grab $O 35 2 || echo "$(date +%T) no regrab $O"
    w "hands squeeze $SLAB 0" 0.6
    w "hands test $SLAB $HX 20 $REACH 0 0 0" 0.5
    SLAB=$O
    echo "$(date +%T) over $k: root $(root)"; fl 3
  done
  m l03-$OH-after-pulls 0.3
  if climbing; then
    echo "== the fling over"
    [ "$SLAB" = L ] && { O=R; HX=-20; } || { O=L; HX=20; }
    w "hands squeeze $O 0" 0.3; w "hands test $O $([ $O = L ] && echo -20 || echo 20) -30 20 0 0 0" 0.5
    w "hands swing $SLAB $HX -30 50 0 0 0 1000" 1.6
    w "hands swing $SLAB $HX -90 5 0 0 0 400 0.55" 3
    fl 6
  fi
  m l04-$OH-after-fling 0.3
  secs 3
  grep -E '^CLIMB t=.* end: ' "$L" | tail -2 | cut -c1-300
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 4; }
  w "hands squeeze L 0"; w "hands squeeze R 0"
}
attempt off
attempt on
w "hands test off"
grep -E '^CLIMB t=.*(start|end|hang|resume|fling [a-z]+|overhang|lip blocked):' "$L" | cut -c1-220 | tail -30
echo "steps done $(date +%T)"
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
