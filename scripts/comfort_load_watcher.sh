#!/bin/bash
# comfort_load_watcher.sh T0 - the comfort helper's headless run (build 6 round 2): trigger words at stereo frame counts
# RELATIVE to the arrival's fade-in (somvr.log RATE stereo=; the null compositor's 20 Hz: 20 frames = 1 s). Checks the
# arrival's texture wait under the black (SOMVR_PRACTICE_LOADHOLD), the practice orc with SOMVR_PRACTICE_QUIET on, a
# stick walk with both-eye marks, the trip back and a second trip there; then quits via RUN/cmd.
# T0 = the start time (date +%s); the newest logs/boot-check-* folder made after it is the run. Log: $TMPDIR/cl.log
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
T0=${1:?T0}
LOG=${TMPDIR:-/tmp}/cl.log
echo "watcher start $(date +%T) t0=$T0" > "$LOG"
RUN=""
for i in $(seq 1 300); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | grep -v dryrun | head -3); do
    [ "$(stat -c %Y "$d")" -ge "$T0" ] && [ -f "$d/somvr.log" ] && { RUN=$d; break; }
  done
  [ -n "$RUN" ] && break
  sleep 2
done
[ -n "$RUN" ] || { echo "no run folder" >> "$LOG"; exit 1; }
echo "RUN=$RUN" >> "$LOG"
stereo() { grep -a '^RATE' "$RUN/somvr.log" | tail -1 | sed -n 's/.* stereo=\([0-9]*\).*/\1/p'; }
alive() { ps -eo args | grep -q '[b]oot_check.sh'; }
for i in $(seq 1 400); do grep -aq "the blink there ended" "$RUN/somvr.log" && break; alive || { echo "gone before the area" >> "$LOG"; exit 1; }; sleep 2; done
S0=$(stereo)
echo "arrived $(date +%T) stereo $S0" >> "$LOG"
at() { # FRAMES-AFTER-THE-ARRIVAL WORD...
  local f=$(( S0 + $1 )); shift
  while :; do s=$(stereo); [ -n "$s" ] && [ "$s" -ge "$f" ] && break; alive || { echo "gone at $f" >> "$LOG"; exit 1; }; sleep 0.3; done
  printf '%s' "$*" > "$RUN/somvr.trigger"
  echo "$(date +%T) stereo $s: $*" >> "$LOG"
}
at 40 mark cl-arrival
at 80 practice spawn
at 200 mark cl-orc
at 300 practice status
at 340 practice remove
at 380 stick 0 1 0 8
at 420 mark cl-walk1
at 460 mark cl-walk2
at 500 mark cl-walk3
at 600 practice back
at 800 practice go
at 1000 mark cl-second
at 1060 practice status
while :; do s=$(stereo); [ -n "$s" ] && [ "$s" -ge $(( S0 + 1100 )) ] && break; alive || exit 0; sleep 1; done
echo quit >> "$RUN/cmd"
echo "$(date +%T) quit sent" >> "$LOG"
