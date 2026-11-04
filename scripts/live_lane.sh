#!/bin/bash
# live_lane.sh - the live loop's way into the VR-safe CPU lane (docs/re-xr-pipeline.md 162; the lane itself is 163).
#
#   live_lane.sh exec NAME CMD...   run CMD in the lane (exec: CMD's pid is this one's)
#   live_lane.sh status             one line: which lane, its state
#   live_lane.sh which              "lane" or "stub" (and why), nothing run
#   live_lane.sh name NAME          the lane job name NAME runs as (live-NAME: live-stt, live-tts, live-build - the names
#                                   lane.conf's LANE_VR_GUARDED / LANE_VR_JOBS match; tests/test_live.py checks them)
#
# The LANE (som-b35-lane, 163) when it is installed - scripts/lane_run.sh here, ~/.config/systemd/user/somvr-lane.slice
# present and loaded: `LANE_NAME=live-NAME LANE_IN_VR=1 lane_run.sh CMD...` - the lane's whole cores, SCHED_IDLE, its
# quota, its watcher (lane_watch.py: paused within ~1 s when the session's frames run late). LANE_IN_VR=1 is only
# lane_run.sh's own start gate: the live workers start with the session, inside VR, and must not wait for it to end.
# Whether a lane job may RUN while the user is in VR is the lane's installed conf (its LANE_IN_VR): 0 = the watcher
# freezes the lane for the whole session - then the notes come without their words until it ends (the daemon posts
# them after 20 s with "the transcription waits", the words as a follow-up note later); `status` says so.
# The STUB otherwise (the lane not installed, or LIVE_LANE_STUB=1): CMD pinned to one physical core (LIVE_LANE_CPUS,
# default 7,15 = core 7's two threads on this Ryzen 7 5800X), nice 19, idle I/O, at most LIVE_LANE_THREADS (2) worker
# threads (OMP / MKL / OpenBLAS / CTranslate2 / onnxruntime), never the GPU. The stub does not pause a job when a frame
# runs late: the lane does. b37: the stub's speech workers (NAME stt / tts: a note's words, an answer's voice) run at
# nice 10 and everything else (a patch's build checks) at nice 19, so a build on the same core no longer makes a note
# wait 10 s (boot 20261104-082636: 9.96 s to transcribe beside a patch check, 0.98 s alone). (In the lane each
# lane_run.sh job is a scope of its own: the slice shares its CPUs between them by their equal weights.)
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
CPUS=${LIVE_LANE_CPUS:-7,15}
THREADS=${LIVE_LANE_THREADS:-2}
cmd=${1:-}; shift || true
[ "$cmd" = name ] && { echo "live-${1:?NAME}"; exit 0; }
lane_ok() {   # the lane is there: its runner, its slice file, the slice loaded
  [ "${LIVE_LANE_STUB:-0}" = 1 ] && { WHY="LIVE_LANE_STUB=1"; return 1; }
  [ -x "$HERE/lane_run.sh" ] || { WHY="no scripts/lane_run.sh in this tree"; return 1; }
  [ -f "$HOME/.config/systemd/user/somvr-lane.slice" ] || { WHY="somvr-lane.slice not installed (lane_install.sh)"; return 1; }
  [ "$(systemctl --user show -p LoadState --value somvr-lane.slice 2>/dev/null)" = loaded ] || { WHY="somvr-lane.slice not loaded"; return 1; }
  return 0
}
conf_in_vr() {   # the installed conf's LANE_IN_VR (the environment wins, as in lane_run.sh)
  local c=$HOME/.config/somvr-lane/lane.conf v
  [ -f "$c" ] || c=$HERE/lane/lane.conf
  v=${LANE_IN_VR_CONF:-$(sed -n 's/^LANE_IN_VR=//p' "$c" 2>/dev/null | tail -1)}
  echo "${v:-0}"
}
WHY=""
if lane_ok; then
  case $cmd in
    exec) name=$1; shift; LANE_NAME="live-$name" LANE_IN_VR=1 exec "$HERE/lane_run.sh" "$@" ;;
    status)
      s=$(python3 -I "$HERE/lane_watch.py" status 2>&1 | head -3 | tr '\n' ' ')
      if [ "$(conf_in_vr)" = 1 ]; then echo "lane (163): in VR the builds run free, live-stt / live-tts only under its late-frame guard (the session's SOMVR_PERF_RAW); $s"
      else echo "lane (163): FROZEN while the user is in VR (its LANE_IN_VR=0): notes come without words until the session ends; $s"; fi
      exit 0 ;;
    which) echo "lane"; exit 0 ;;
  esac
fi
case $cmd in
  exec)
    NICE=19; case ${1:-} in stt|tts) NICE=10 ;; esac
    shift   # NAME (the stub has no queue: the name sets the nice only)
    export OMP_NUM_THREADS=$THREADS MKL_NUM_THREADS=$THREADS OPENBLAS_NUM_THREADS=$THREADS CT2_INTER_THREADS=1
    export ORT_NUM_THREADS=$THREADS LANE_THREADS=$THREADS
    export CUDA_VISIBLE_DEVICES="" HIP_VISIBLE_DEVICES="" ROCR_VISIBLE_DEVICES=""
    exec taskset -c "$CPUS" nice -n "$NICE" ionice -c 3 "$@" ;;
  status)
    echo "stub ($WHY): CPUs $CPUS, nice 10 (stt, tts) / 19 (the rest), idle I/O, $THREADS threads, no pause on late frames" ;;
  which) echo "stub ($WHY)" ;;
  *) echo "usage: live_lane.sh exec NAME CMD... | status | which" >&2; exit 2 ;;
esac
