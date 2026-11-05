#!/bin/bash
# nurn41-drive.sh - som-b41-nurn (GAME-STUDY 1.8: the user's Núrn story path in VR, one mission per boot): the driver of
# scripts/ladder_boot.sh's boot from a rung (logs/test-save/ladder/<NN-name>/, or a run's save). In order:
#   1. unless NU_START=0: ladder-drive.sh's `start` (the rung's marker, B = the card, A = start, the video skipped, the world)
#   2. NU_MISSION=FILE...: each nurn41/FILE (a mission's director: lib.sh's VR acts and aids), sourced in turn
#   3. the queue: RUN/ctl/cmdN.txt one after another (N = 1, 2, ...), each file's lines run as lib.sh shell (a director
#      for a step the first ones did not reach); `end` leaves the queue; 20 idle minutes leave it too
#   4. NU_END=1 (default): the end handling (videos, paused pages, windows); the prefix's save -> RUN/saves/nu-end.sav;
#      then quit (NU_NOQUIT=1: the boot left running; the default with NU_START=0, a driver started by hand on a run)
# Everything the director does is logged as `NU act` (a VR player's own: the test hands' poses and buttons) or `NU aid`
# (the harness's stand-in for a person's travel or skill). Run by ladder_boot.sh (LADDER_RUN, LADDER_RUNG), e.g.
#   scripts/ladder_boot.sh 12 nurn41-drive.sh NU_MISSION=m12.sh
# Stops at once (quit) when the user goes into VR.
R=${LADDER_RUN:-${ST_RUN:-}}; [ -d "$R" ] || { echo "no run (LADDER_RUN)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/nurn41/lib.sh"
echo "$(date +%T) nurn41: run $R, rung ${LADDER_RUNG:-none}, missions ${NU_MISSION:-none}"
for i in $(seq 1 150); do userin && bail "the user is in VR"; gone && bail "the game is gone"
  [ "$(cnt '^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world')" -ge 2 ] && break; sleep 3; done
echo "$(date +%T) in the world: $(state)"; sleep 4
hands0
L0=$(lines)
if [ "${NU_START:-1}" != 0 ]; then
  LADDER_STEPS=start LADDER_NOQUIT=1 LADDER_NOPREP=${LADDER_NOPREP:-} bash "$HERE/ladder-drive.sh" 2>&1 | grep -vE ' (word|tap) ' | tail -40
  inworld || { sleep 5; inworld || echo "$(date +%T) after the start: $(state)"; }
fi
for m in ${NU_MISSION:-}; do
  echo "$(date +%T) ==== mission director $m"; . "$HERE/nurn41/$m"; echo "$(date +%T) ==== $m done ($(state))"
done
CTL=${NU_CTL_DIR:-$R/ctl}; mkdir -p "$CTL"; NUK=0
[ "${NU_CTL:-1}" = 0 ] && echo end > "$CTL/cmd1.txt"   # NU_CTL=0: no queue after the directors
while :; do
  NUK=$((NUK + 1)); f=$CTL/cmd$NUK.txt; echo "$(date +%T) waiting for $f"
  for i in $(seq 1 400); do [ -s "$f" ] && break; userin && bail "the user is in VR"; gone && bail "the game is gone"; sleep 3; done
  [ -s "$f" ] || { echo "$(date +%T) no $f in 20 min: the end"; break; }
  sleep 0.3; grep -qx 'end' "$f" && { echo "$(date +%T) end"; break; }
  n0=$(lines); echo "$(date +%T) ---- cmd$NUK: $(head -c 300 "$f" | tr '\n' ';')"
  . "$f"
  echo "$(date +%T) ---- cmd$NUK done ($(state)); since:"; moments "$n0" "$NU_RE" 25
  echo "WARN lines: $(since "$n0" '^WARN' | wc -l)"
done
[ "${NU_END:-1}" = 1 ] && endphase 6
saveas nu-end
echo "$(date +%T) WARN lines in the run: $(cnt '^WARN')"; grep -aE '^WARN' "$L" | head -5 | cut -c1-200
NQ=${NU_NOQUIT:-$([ "${NU_START:-1}" = 0 ] && echo 1 || echo 0)}   # a takeover (NU_START=0) leaves the game running
[ "$NQ" = 1 ] && { echo "the boot left running (NU_NOQUIT=1)"; exit 0; }
w "endlogs" 2; sleep 2; echo quit >> "$R/cmd"; echo "$(date +%T) quit"
