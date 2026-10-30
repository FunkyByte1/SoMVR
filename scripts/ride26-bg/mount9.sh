#!/bin/bash
# mount8.sh BTN... - per round one struggle button: the counter, the walk to the offer, the left hand closes (the mount's
# press), then through the domination's struggle that button tapped (~4 a second) until it ends; mounted = stop
R=$(cat /tmp/b26-ride/bg/R); L=$R/somvr.log
w() { printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T.%N | cut -c1-12) word: $1"; sleep "${2:-0.8}"; }
t() { { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${TS:-0.3}"; }
cnt() { grep -acE "$1" "$L"; }
up() { tail -c +"$B0" "$L" | grep -aqE "^RIDE t=.* the game offers the mount"; }
dq() { t "dump"; echo "dump-asked $1 $(date +%T.%N | cut -c1-12)" | tee -a /tmp/b26-ride/bg/dumps.txt; }
for btn in "$@"; do
  n0=$(cnt 'DEFENCE countered.*JumpAttack')
  for i in $(seq 1 600); do [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] && break; sleep 0.25; done
  B0=$(stat -c %s "$L"); echo "$(date +%T) countered (button $btn)"; sleep 3.0
  w "verbs behind 200" 0.1; for q in $(seq 1 30); do up && break; sleep 0.1; done
  for k in $(seq 1 6); do up && break; w "stick 0 1 0 0.3" 0.05; for q in $(seq 1 7); do up && break 2; sleep 0.1; done; done
  up || { echo "no offer"; continue; }
  w "stick 0 0 0 0.1" 0.05; w "hands squeeze L 1" 0.1; w "verbs press 123 7000" 0.2
  for i in $(seq 1 40); do tail -c +"$B0" "$L" | grep -aq 'ctx=struggle' && break; sleep 0.1; done
  echo "$(date +%T.%N | cut -c1-12) the struggle: $(tail -c +"$B0" "$L" | grep -ac 'ctx=struggle') (123 held 7 s, the left hand closed)"; dq "struggle-hold"
  for i in $(seq 1 80); do tail -c +"$B0" "$L" | grep -aqE "^RIDE t=.* mounted|PC_Warg_Domination_Fail" && break; sleep 0.1; done
  w "hands squeeze L 0" 0.05
  sleep 1
  tail -c +"$B0" "$L" | grep -a -E "^(RIDE t=|BEASTS t=.* Talion: attack 'PC_Warg_Dom|VIN press)" | head -10 | cut -c1-200
  tail -c +"$B0" "$L" | grep -aq '^RIDE t=.* mounted' && { echo "MOUNTED with $btn"; break; }
  echo "not mounted with $btn"
done
