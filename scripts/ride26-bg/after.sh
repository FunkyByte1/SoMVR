#!/bin/bash
# after.sh - the follow loop, then the caragor: the popup closed, the sword drawn, the guard on, the trip, the leap
cd /tmp/b26-ride/bg
T0=$(date +%s)
for i in $(seq 1 2400); do S=$(ls -t /tmp/b26-ride/story-*.log 2>/dev/null | head -1)
  [ -n "$S" ] && [ "$(stat -c %Y "$S")" -ge "$T0" ] && grep -q 'waiting for /tmp/b26-ride/bg/ctl/cmd1.txt' "$S" && break; sleep 5; done
for i in $(seq 1 60); do for d in $(ls -dt /home/bara/Projects/flat2vr/ShadowOfMordor/logs/boot-check-2026* | head -3); do
  grep -q t3-5f7724e9 $d/launch-cmd.txt 2>/dev/null && [ "$(stat -c %Y $d/launch-cmd.txt)" -ge "$T0" ] && { echo $d > /tmp/b26-ride/bg/R; break 2; }; done; sleep 5; done
echo "run $(cat /tmp/b26-ride/bg/R)"
WAIT=3 ./follow.sh 75 'ObjectiveGuttered_ALLMaps'
./q.sh "world 30" "tap R a 0.4" "world 20" "wait 1" "word hands test R 10 -10 -15 0 0 0" "word hands squeeze R 1" "word hands test R 30 -25 40 0 -90 0" \
  "word hands squeeze R 0" "word hands test R 30 -10 45 0 -35 0" "word fight guard learn" "wait 2" "word ride guard on" "word ride face on" "word hands test L -25 -35 30 0 0 0" "status" \
  "go -34800 1917 -8452" "waitfor (PC_Counter_Warg|Struggle_Takedown|RIDE t=.* mounted) 60" "pics bg-c1" "wait 1" "pics bg-c2" "wait 2" "pics bg-c3"
echo "queued $(date +%T)"
/tmp/b26-ride/bg/mount7.sh
