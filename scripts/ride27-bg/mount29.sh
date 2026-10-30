#!/bin/bash
# mount27.sh - som-b27-ride2 (build 27, re-xr-pipeline.md 109): after each counter of Big Game's caragor leap walk to it
# until the game offers the mount, the left hand (at rest: 39 cm out, ahead-left) closes = the mount, then through the
# domination's struggle as its class wants (the RIDE struggle line): a hold = the hand kept closed; a press count / a
# quick-time = the hand closed anew 4 times a second. Mounted: Seat mode's checks (pictures, the run, a turn, the leap,
# the bite, the run's click, the other mounted commands), the mission's objective while riding, then B = off.
R=$(cat /tmp/b27-ride2/bg/R); L=$R/somvr.log; DQ=/tmp/b27-ride2/bg/dumps.txt
w() { printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T.%N | cut -c1-12) word: $1"; sleep "${2:-0.8}"; }
t() { { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep 0.3; }
cnt() { grep -acE "$1" "$L"; }
since() { tail -c +"$B0" "$L"; }
up() { since | grep -aqE "^RIDE t=.* the game offers the mount"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "dump $1 $f" | tee -a $DQ; }
MOUNTED=0
for round in 1 2 3 4 5; do
  n0=$(cnt 'DEFENCE countered.*JumpAttack')
  for i in $(seq 1 480); do [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] && break; sleep 0.25; done
  B0=$(stat -c %s "$L"); echo "$(date +%T) countered (round $round)"; sleep 3.1
  for k in $(seq 1 10); do up && break; w "stick 0 1 0 0.4" 0.05; for q in $(seq 1 7); do up && break 2; sleep 0.1; done; done
  up || { echo "no offer"; continue; }
  echo "$(date +%T.%N | cut -c1-12) the offer"; w "stick 0 0 0 0.1" 0.05; w "hands squeeze L 1" 0.3
  # the domination: the struggle's line names its class
  for i in $(seq 1 100); do since | grep -aqE "^RIDE t=.* the struggle \(|PC_Warg_Domination_Fail|^RIDE t=.* mounted" && break; sleep 0.1; done
  SL=$(since | grep -a -m1 '^RIDE t=.* the struggle (' | cut -c1-300); echo "struggle: $SL"
  [ $round = 1 ] && d "struggle-$round"
  if echo "$SL" | grep -qE 'a press count|a quick-time'; then
    for i in $(seq 1 60); do since | grep -aqE "^RIDE t=.* the struggle .* ended|^RIDE t=.* mounted" && break
      w "hands squeeze L 0" 0.1; w "hands squeeze L 1" 0.1; done
  fi
  for i in $(seq 1 100); do since | grep -aqE "^RIDE t=.* mounted|PC_Warg_Domination_Fail" && break; sleep 0.1; done
  since | grep -a -E "^RIDE t=" | head -12 | cut -c1-260
  since | grep -aq '^RIDE t=.* mounted' && { MOUNTED=1; break; }
  w "hands squeeze L 0" 0.2
  echo "NOT mounted (round $round)"
done
[ $MOUNTED = 1 ] || { echo "never mounted"; echo quit >> "$R/cmd"; exit 1; }
w "ride face off" 0.2
for i in $(seq 1 40); do since | grep -aq "^RIDE t=.* seated on" && break; sleep 0.25; done; sleep 1.5
w "hands squeeze L 0" 0.3
since | grep -a -E '^RIDE t=' | head -16 | cut -c1-300
t "head 0 0 0 yaw 0 pitch -10 s 0.3"; d ride-seat; t "head 0 0 0 yaw 0 pitch -40 s 0.3"; d ride-seat-down; t "head 0 0 0 yaw 0 pitch -10 s 0.3"
# the run, the leap (A) mid-run, the bite (RT), a turn, the charge (the stick's click)
w "stick 0 1 0 6.0" 1.5; d ride-run; echo "$(date +%T.%N | cut -c1-12) A"; t "btn R a 1"; sleep 0.25; d ride-leap; t "btn R a 0"; sleep 0.6; d ride-leap-b; sleep 2.5
w "stick 0 1 0 3.0" 0.8; t "btn R trigger 1"; sleep 0.3; d ride-bite; t "btn R trigger 0"; sleep 2.0
w "stick -0.8 0.6 0 3.0" 1.2; d ride-turn; sleep 2.0
w "stick 0 1 0 3.0" 0.5; t "btn L stick 1"; sleep 0.3; t "btn L stick 0"; sleep 2.5
w "status" 0.3; w "ride status" 0.5
since | grep -a -E "^RIDE (t=.*(the rider's|the ridden|press)|motion)" | tail -24 | cut -c1-260
# off: B held
P0=$(stat -c %s "$L"); t "btn R b 1"; sleep 0.6; t "btn R b 0"
for i in $(seq 1 40); do since | grep -aq "^RIDE t=.* off: standing" && break; sleep 0.25; done; sleep 1; d ride-off-b
tail -c +"$P0" "$L" | grep -a -E '^(RIDE (t=|press)|BODYMOMENT .*(released|blink)|VIN press cmd=123)' | head -10 | cut -c1-260
echo done
echo quit >> "$R/cmd"; echo "$(date +%T) quit asked"
