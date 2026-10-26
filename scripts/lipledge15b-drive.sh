#!/bin/bash
# lipledge15b-drive.sh - build 15b: the user's lip (session 165456 mark 8) from the ledge under it: `practice go` onto
# the narrow ledge (top 2562) at the user's stuck spot (LEDGE_AT, default 33660 2575 -27345: the slab at ~2812 sticks
# out ~21 cm over the face behind it), face the nearest wall (the probe, snap turns; no walking), then twice - the word
# `climb overhang off` (build 15: the repro), then `climb overhang on` (the fix): a hand on the slab (75 cm over the
# head, 35 ahead), pulled down to the waist (1.5 s), the other hand on it, pulled again, three times; the push over
# (the hand down and toward the body); the root's height after each pull, the eyes (`mark`). A between. Attaches to
# a running boot with LIP_RUN=RUNDIR (else waits for one of this worktree's). Stops at once (quit) when the user goes
# into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${LIP_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
AT=${LEDGE_AT:-33660 2575 -27345}
T0=$(date +%s)
R=${LIP_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
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
[ -n "$LIP_RUN" ] || sleep 8
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
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbing() { grep -E '^CLIMB t=.* (start|end):' "$L" | tail -1 | grep -q ' start:'; }
lastcl() { grep -E '^CLIMB t=' "$L" | tail -${1:-3} | cut -c1-330; }
fl() { grep -E '^CLIMB t=.* (fling|end:|hang:|resume:|grab [LR]|top:|overhang|retop|lip )' "$L" | tail -${1:-4} | cut -c1-360; }
secs() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -${1:-3} | cut -c1-250; }
rooty() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -1 | grep -oE 'now [0-9.,-]+' ; }
# hand H onto the slab: up UP, ahead Z (tries a little higher / nearer)
onslab() { local h=$1 xs n0 i up z; [ "$h" = L ] && xs=-20 || xs=20
  for t in "75 35" "85 30" "65 40" "90 45" "70 25"; do
    up=${t% *}; z=${t#* }
    w "hands squeeze $h 0" 0.3; w "hands test $h $xs $up $z 0 0 0" 0.5
    n0=$(cnt "^CLIMB t=.* grab $h:")
    w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { fl 1; return 0; }; sleep 0.25; done
  done; return 1; }
attempt() { local OH=$1 H O HX k
  echo "== attempt overhang $OH"
  w "hands squeeze L 0"; w "hands squeeze R 0"
  w "climb overhang $OH" 0.5
  w "practice go $AT" 1
  waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-200
  sleep 3
  probe
  NB=$(near +0); [ -n "$NB" ] || NB=$(near -50)
  echo "$(date +%T) nearest climbable: ${NB:-none}"
  [ -n "$NB" ] && turnto "${NB% *}"
  probe up
  m e01-$OH-on-ledge 0.3
  H=R
  onslab $H || { echo "$(date +%T) no hold on the slab"; return 1; }
  for k in 1 2 3; do
    [ "$H" = L ] && { O=R; HX=-20; } || { O=L; HX=20; }
    w "hands swing $H $HX -15 25 0 0 0 1500" 2.2
    echo "$(date +%T) pull $k ($H): root $(rooty)"; secs 2; fl 3
    climbing || break
    onslab $O || echo "$(date +%T) no second hold"
    w "hands squeeze $H 0" 0.5
    H=$O
  done
  m e02-$OH-after-pulls 0.3
  if climbing; then # the push over: the hand down and toward the body
    [ "$H" = L ] && HX=-20 || HX=20
    w "hands swing $H $HX -40 5 0 0 0 1200" 2.5
    secs 2; fl 4
  fi
  m e03-$OH-after-push 0.3
  grep -E '^CLIMB t=.* end: ' "$L" | tail -1 | cut -c1-300
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 4; }
}
x "btn R squeeze 0"
attempt off
attempt on
w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
grep -E '^CLIMB t=.*(start|end|hang|resume|overhang|retop|top):' "$L" | cut -c1-220 | tail -24
grep -E '^CLIMB t=.*(overhang|retop)' "$L" | wc -l
echo "steps done $(date +%T)"
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
