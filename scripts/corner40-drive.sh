#!/bin/bash
# corner40-drive.sh - som-b40-fixes (docs/re-xr-pipeline.md 175.5): the walk-up corner of the 11-04 session (mark
# 222603_18: on the long wall's coping, the sloped stone beam rising from its corner; the walk stalls there and the game
# takes it back between our ticks). At the user's spot (31237.2 4816.5 -30684.4), for each heading of C40_YAWS (default
# "74 48": the session's stalled walk `dir 0.96 0.28`, then the user's head), the Windows build's write watch on Talion's
# root (`climb writes 4`: the debug registers, 175.5) while the stick walks him 3 s into the beam; LOCO / WALK / UNPIN
# lines, `climb contacts`, `climb probe`. Lines: RUN/c40-steps.txt. Boot with scripts/headset-build26.env,
# SOMVR_HANDS_TEST=1. C40_RUN=RUN drives a running boot. Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 PIN_RUN=${C40_RUN:-} . "$(dirname "$0")/climb2-drive.sh"
ST=$R/c40-steps.txt
# every word waited for (the Windows build reads the trigger file every ~1-2 s: a word written sooner is lost)
w() { userin && bail "the user is in VR"; local a0 i; a0=$(grep -c '^TRIGGER cmd=' "$L"); printf '%s' "$1" > "$R/somvr.trigger"
  echo "$(date +%T) word: $1"; for i in $(seq 1 30); do [ "$(grep -c '^TRIGGER cmd=' "$L")" -gt "$a0" ] && break; sleep 0.1; done
  sleep "${2:-0.8}"; }
lines() { wc -l < "$L"; }
sincel() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.6 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
faceyaw() { local want=$1 k y d
  for k in $(seq 1 14); do
    sleep 0.4; y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 16 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    [ "$d" -gt 0 ] && w "turn+" 0.9 >/dev/null || w "turn-" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
card
w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
SPOT=${C40_SPOT:-"31237.2 4816.5 -30684.4"}
for Y in ${C40_YAWS:-74 48}; do
  echo "== heading $Y"
  go "$SPOT"; faceyaw "$Y"; echo "$(date +%T) at $(root)"
  n0=$(lines); w "climb writes 4" 0.3; w "stick 0 1 0 3" 5.0
  note "heading $Y: walked to $(root)"
  sincel "$n0" '^CLIMB writes' | cut -c1-300 | tee -a "$ST"
  sincel "$n0" '^(LOCO t=.*last 1 s|WALK |UNPIN t=|JUMP t=.* go)' | cut -c1-260 | head -10 | tee -a "$ST"
  contacts 8 | tee -a "$ST"
  probe; grep -E '^CLIMB probe' "$L" | tail -4 | cut -c1-300 | tee -a "$ST"
done
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
