#!/bin/bash
# mount7.sh - after the next counter of the leap: (`ride face on` turns the view to the stunned caragor) walk to it until
# the game offers the mount, then the left hand (at rest: 39 cm out, ahead-left) closes = the mount; then the ride
R=$(cat /tmp/b26-ride/bg/R); L=$R/somvr.log
w() { printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T.%N | cut -c1-12) word: $1"; sleep "${2:-0.8}"; }
t() { { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep 0.3; }
cnt() { grep -acE "$1" "$L"; }
up() { tail -c +"$B0" "$L" | grep -aqE "^RIDE t=.* the game offers the mount"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "dump $1 $f" | tee -a /tmp/b26-ride/bg/dumps.txt; }
dq() { t "dump"; echo "dump-asked $1 $(date +%T.%N | cut -c1-12)" | tee -a /tmp/b26-ride/bg/dumps.txt; }
for round in 1 2 3; do
  n0=$(cnt 'DEFENCE countered.*JumpAttack'); g0=$(cnt '^RIDE guard t=.* the blade across')
  for i in $(seq 1 1200); do [ "$(cnt '^RIDE guard t=.* the blade across')" -gt "$g0" ] && break; sleep 0.1; done
  [ $round = 1 ] && dq "leap-guard-$round"
  for i in $(seq 1 480); do [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] && break; sleep 0.25; done
  B0=$(stat -c %s "$L"); echo "$(date +%T) countered (round $round)"; [ $round = 1 ] && { sleep 0.3; dq "counter-$round"; }; sleep 3.1
  for k in $(seq 1 10); do up && break; w "stick 0 1 0 0.4" 0.05; for q in $(seq 1 7); do up && break 2; sleep 0.1; done; done
  up || { echo "no offer"; continue; }
  echo "$(date +%T.%N | cut -c1-12) the offer"; w "stick 0 0 0 0.1" 0.05; dq "offer-$round"; sleep 0.2; w "hands squeeze L 1" 0.3
  for i in $(seq 1 40); do tail -c +"$B0" "$L" | grep -aq '^RIDE t=.* mounted' && break; sleep 0.25; done
  w "hands squeeze L 0" 0.2
  tail -c +"$B0" "$L" | grep -a -E '^(RIDE t=|VERBS t=.*prompt|BEASTS t=.* Talion: attack)' | head -8 | cut -c1-220
  tail -c +"$B0" "$L" | grep -aq '^RIDE t=.* mounted' && break
  echo "NOT mounted (round $round)"
done
tail -c +"$B0" "$L" | grep -aq '^RIDE t=.* mounted' || exit 1
w "ride face off" 0.2
for i in $(seq 1 40); do tail -c +"$B0" "$L" | grep -aq "^RIDE t=.* seated on" && break; sleep 0.25; done; sleep 1.5
tail -c +"$B0" "$L" | grep -a -E '^(RIDE|BODYMOMENT .*(released|blink))' | head -10 | cut -c1-250
t "head 0 0 0 yaw 0 pitch -10 s 0.3"; d ride-seat; t "head 0 0 0 yaw -60 pitch -25 s 0.3"; d ride-seat-left; t "head 0 0 0 yaw 0 pitch -10 s 0.3"
w "stick 0 1 0 3.0" 1.5; d ride-run; sleep 1.5
w "stick -0.7 0.7 0 2.5" 1.0; d ride-turn; sleep 0.5; d ride-turn-b; sleep 0.8
w "stick 0 1 0 2.0" 0.3; t "btn R a 1"; d ride-leap; t "btn R a 0"; d ride-leap-b; sleep 1
t "btn R trigger 1"; d ride-bite; t "btn R trigger 0"; d ride-bite-b; sleep 1.5
for c in 184 186 187 188; do w "ride press $c 300" 1.6; done
w "status" 0.3; w "ride status" 0.5
t "btn R b 1"; d ride-off; t "btn R b 0"
for i in $(seq 1 40); do tail -c +"$B0" "$L" | grep -aq "^RIDE t=.* off: standing" && break; sleep 0.25; done; sleep 1; d ride-off-b
tail -c +"$B0" "$L" | grep -a -E '^(RIDE (t=|press|motion|status)|BODYMOMENT .*(released|blink))' | tail -16 | cut -c1-250
echo done
