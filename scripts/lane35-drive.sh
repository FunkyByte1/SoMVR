#!/bin/bash
# lane35-drive.sh - the VR-safe CPU lane measured (som-b35-lane, docs/re-xr-pipeline.md 163): the game held in the
# practice meadow (or a crowd of 8: LANE_PLACE=crowd) while the lane's loads run one at a time, each in a window named
# by the word `perf NAME` (SOMVR_PERF_RAW's `# perf NAME` marks, the PERF lines, the layer's), a quiet window between
# two loads; the game's processes pinned to GAME_CPUS by lane_watch.py (a protect file, loop off) except in the
# `unpinned` windows; at the end the closed loop on (loop=on) and the deliberate overload (lane_hog) in the lane:
# paused by the watcher. Boot (solo: it measures frame times) with scripts/headset-build26.env + SOMVR_PERF_RAW=
# @RUN@/perf-raw.txt SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600 and WIN_RENDER_CFG (the user session's VR preset), e.g.
#   bash $Q submit --owner som-b35-lane --tree TREE/ShadowOfMordor/mod/somvr --solo --hold 1800 --max-min 45 \
#     --env 'SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600 WIN_RENDER_CFG=/tmp/somvr-lane35/render-vr.cfg' --driver 'bash scripts/lane35-drive.sh'
# Needs: scripts/lane_install.sh install, lane_loads.py prepare. LANE_STEPS (default all, in order): none build whisper
# tts all3 old hog unpinned loop. LANE_WS: a window's seconds (60). BG_RUN=RUN drives a running boot. No sound plays.
# Then: python3 scripts/lane_frames.py RUN (the table).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${BG_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
RTD=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
STEPS=${LANE_STEPS:-build whisper tts all3 old hog unpinned loop}; STEPS=${STEPS//_/ }
WS=${LANE_WS:-60}
T0=$(date +%s)
R=${BG_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
bash "$(dirname "$0")/render_need.sh" "$R" lane35-drive.sh || exit 1   # b37: a Windows run at WIN_RENDER=vr (re-xr-pipeline.md 158.5)
echo "run $R"
PROT=$RTD/somvr-lane/protect.d/lane35-$(basename "$R").conf
JOBS=()
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' "$RTD/vr-session/status.json" 2>/dev/null && return 0
  return 1
}
# our own jobs only: a lane job's whole scope (its build's compilers too) by its cgroup.kill, an old-way job by its pid
stop_jobs() {
  local p cg
  for p in "${JOBS[@]}"; do
    cg=$(cut -d: -f3 "/proc/$p/cgroup" 2>/dev/null)
    case $cg in */somvr-lane.slice/lane-*.scope) echo 1 > "/sys/fs/cgroup$cg/cgroup.kill" 2>/dev/null ;;
      *) kill -TERM "$p" 2>/dev/null ;; esac
  done
  for p in "${JOBS[@]}"; do wait "$p" 2>/dev/null; done
  JOBS=()
}
finish() { stop_jobs; rm -f "$PROT"; }
trap finish EXIT
bail() { echo "$(date +%T) STOP: $1"; stop_jobs; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
[ -s "$R/perf-raw.txt" ] || bail "no $R/perf-raw.txt (SOMVR_PERF_RAW=@RUN@/perf-raw.txt and SOMVR_PERF=on)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
protect() {   # loop on|off, pin yes|no: lane_watch.py's protection of this run
  mkdir -p "$(dirname "$PROT")"
  { echo "raw=$R/perf-raw.txt"; echo "loop=$1"; [ "$2" = yes ] && echo "pin=$R"; } > "$PROT.tmp" && mv -f "$PROT.tmp" "$PROT"
  echo "$(date +%T) protect: loop $1, pin $2"
}
# a named window of SEC seconds (default WS): the words first (both sides' windows), then the time, the VR gate checked
win() {
  w "perf $1" 0.3; x "perf $1" 0.3; echo "$(date +%T) window $1 (${2:-$WS} s)" | tee -a "$R/lane-steps.txt"
  local e=$(( $(date +%s) + ${2:-$WS} ))
  while [ "$(date +%s)" -lt $e ]; do userin && bail "the user is in VR"; sleep 1; done
}
# a lane job in the background (its log RUN/lane-NAME.log), ours to stop
lane() { local n=$1; shift; bash "$HERE/lane_run.sh" "$@" > "$R/lane-$n.log" 2>&1 & JOBS+=($!); echo "$(date +%T) lane job $n pid $!: $*"; }
old() { local n=$1; shift; nice -n 19 "$@" > "$R/lane-$n.log" 2>&1 & JOBS+=($!); echo "$(date +%T) old-way job $n pid $!: nice 19 $*"; }
waitjobs() { stop_jobs; }   # (the window is over: what still runs goes)
L=$HERE/lane_loads.py
[ "${LANE_PLACE:-meadow}" = crowd ] && { w "practice group 8" 6; grep 'group (' "$R/somvr.log" | tail -1 | cut -c1-160; }
# where the game's busiest threads run before any pin (the scheduler's own choice)
python3 -I "$HERE/lane_watch.py" placement "$R" 6 > "$R/lane-placement.txt" 2>&1; head -12 "$R/lane-placement.txt" | cut -c1-200
# the watcher runs (lane_run.sh starts it; here before the first window so the pins are in place)
systemctl --user is-active -q somvr-lane-watch || bash "$HERE/lane_run.sh" true
protect off yes; sleep 4
grep -q 'PIN ' "$SOM/logs/lane/lane-$(date +%Y%m%d).log" 2>/dev/null && tail -2 "$SOM/logs/lane/lane-$(date +%Y%m%d).log" | cut -c1-200
win none-a
n=0
for s in $STEPS; do
  n=$((n + 1))
  case $s in
    build) lane build python3 -I "$L" build --seconds 150; win build 150; waitjobs ;;
    whisper) lane whisper python3 -I "$L" whisper --seconds "$WS"; win whisper "$WS"; waitjobs ;;
    tts) lane tts python3 -I "$L" tts --seconds "$WS"; win tts "$WS"; waitjobs ;;
    all3) lane all3-build python3 -I "$L" build --seconds "$WS"
          lane all3-whisper python3 -I "$L" whisper --seconds "$WS"; lane all3-tts python3 -I "$L" tts --seconds "$WS"
          win all3 "$WS"; waitjobs ;;
    old) old old-whisper python3 -I "$L" whisper --threads 6 --seconds "$WS"; win old-whisper "$WS"; waitjobs ;;
    hog) lane hog python3 -I "$L" hog --threads 4 --seconds 30; win hog 30; waitjobs ;;
    unpinned) protect off no; sleep 14; win none-unpinned
              python3 -I "$HERE/lane_watch.py" placement "$R" 5 > "$R/lane-placement-unpinned.txt" 2>&1
              lane whisper-u python3 -I "$L" whisper --seconds "$WS"; win whisper-unpinned "$WS"; waitjobs
              protect off yes; sleep 4 ;;
    loop) protect on yes; sleep 6
          lane loop-hog python3 -I "$L" hog --threads 4 --seconds 45; win loop-hog 50; waitjobs ;;
    *) echo "unknown step $s" ;;
  esac
  win "none-$n"
done
w "perf end" 0.5; x "perf end" 0.5
rm -f "$PROT"
grep -E 'FREEZE|THAW|PIN|UNPIN|PROTECT' "$SOM/logs/lane/lane-$(date +%Y%m%d).log" | tail -40 > "$R/lane-events.txt"
echo "$(date +%T) done: $(wc -l < "$R/perf-raw.txt") raw lines; lane events in $R/lane-events.txt"
echo quit >> "$R/cmd"
echo PASS
