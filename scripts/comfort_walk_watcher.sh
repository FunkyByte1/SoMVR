#!/bin/bash
# watcher for the comfort helper's headless run: trigger words at stereo frame counts (somvr.log RATE stereo=)
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
T0=$1
LOG=/tmp/claude-1000/w1.log
echo "watcher start $(date +%T) t0=$T0" > $LOG
RUN=""
for i in $(seq 1 300); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | grep -v dryrun | head -3); do
    [ "$(stat -c %Y "$d")" -ge "$T0" ] && [ -f "$d/somvr.log" ] && { RUN=$d; break; }
  done
  [ -n "$RUN" ] && break
  sleep 2
done
[ -n "$RUN" ] || { echo "no run folder" >> $LOG; exit 1; }
echo "RUN=$RUN" >> $LOG
stereo() { grep '^RATE' "$RUN/somvr.log" | tail -1 | sed -n 's/.* stereo=\([0-9]*\).*/\1/p'; }
alive() { ps -eo args | grep -q '[b]oot_check.sh'; }
for i in $(seq 1 400); do grep -q "the blink there ended" "$RUN/somvr.log" && break; alive || { echo "gone before the area" >> $LOG; exit 1; }; sleep 2; done
echo "arrived $(date +%T) stereo $(stereo)" >> $LOG
at() { # FRAME WORD...
  local f=$1; shift
  while :; do s=$(stereo); [ -n "$s" ] && [ "$s" -ge "$f" ] && break; alive || { echo "gone at $f" >> $LOG; exit 1; }; sleep 0.3; done
  printf '%s' "$*" > "$RUN/somvr.trigger"
  echo "$(date +%T) stereo $s: $*" >> $LOG
}
at 1140 stick 0 1 0 12
at 1420 stick 0 0 1 4
at 1700 turn+
at 1720 turn-
at 1800 stick 0.3 1 0.4 15
at 2150 practice spawn shield
at 2260 practice status
at 2420 practice back
while :; do s=$(stereo); [ -n "$s" ] && [ "$s" -ge 2600 ] && break; alive || exit 0; sleep 1; done
echo quit >> "$RUN/cmd"
echo "$(date +%T) quit sent" >> $LOG
