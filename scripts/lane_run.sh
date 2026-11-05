#!/bin/bash
# lane_run.sh CMD [ARGS...] - run one background job (a build, unit tests, a speech model) in the VR-safe CPU lane
# (som-b35-lane, docs/re-xr-pipeline.md 163): a scope in somvr-lane.slice (lane_install.sh: cpu.idle, CPUQuota,
# IOWeight 10, MemoryHigh/Max, no swap), the lane's whole cores only (LANE_CPUS, by affinity), SCHED_IDLE, nice 19,
# the idle IO class, the libraries' thread pools and the build runner (SOMVR_JOBS) sized to the lane, no GPU. The job runs in the foreground; the exit
# code is the job's. Run it OUTSIDE the Bash sandbox (systemd-run needs the user bus).
#   It refuses a real-time caller (keyd's SCHED_FIFO inherited: memory keyd-realtime-inheritance) - exit 3.
#   While the user is in VR (status.json "user": true, or the unit somvr-headset active) and LANE_IN_VR=0 - or
#   LANE_IN_VR=1 but the job's name (LANE_NAME) is not in LANE_VR_JOBS - it waits until the session ends (LANE_WAIT=0:
#   exit 4 instead), and lane_watch.py freezes the lane (that job) if a session starts while it runs. It starts the watcher (the user unit somvr-lane-watch) when none runs (LANE_NO_WATCH=1: not).
# Environment: anything in lane.conf (LANE_CPUS ...), LANE_NAME (the scope's name part, default the command's name).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
RT=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
[ $# -ge 1 ] || { echo "usage: lane_run.sh CMD [ARGS...]"; exit 2; }
say() { printf 'lane_run: %s\n' "$*" >&2; }
# the conf: the installed one (else the repo's); a variable already in the environment wins
CONF=$HOME/.config/somvr-lane/lane.conf; [ -f "$CONF" ] || CONF=$HERE/lane/lane.conf
while IFS='=' read -r k v; do
  [[ $k =~ ^LANE_[A-Z_]+$|^GAME_CPUS$ ]] || continue
  v=${v%\"}; v=${v#\"}   # (a quoted value: the conf is shell syntax)
  [ -n "${!k+x}" ] || export "$k=$v"
done < <(grep -E '^[A-Z_]+=' "$CONF")
# 1. never real-time (a job that inherited keyd's SCHED_FIFO would run above the game and KWin)
CLS=$(ps -o cls= -p $$ | tr -d ' ')
case $CLS in TS|IDL|B) ;; *) say "STOP: this shell runs in scheduling class '$CLS' (real-time, inherited); start the job from a normal shell (memory keyd-realtime-inheritance)"; exit 3 ;; esac
# 2. the slice
[ "$(systemctl --user show -p LoadState --value somvr-lane.slice 2>/dev/null)" = loaded ] && [ -f "$HOME/.config/systemd/user/somvr-lane.slice" ] ||
  { say "STOP: somvr-lane.slice is not installed (scripts/lane_install.sh install)"; exit 3; }
# 3. the user's VR
in_vr() {
  [ -n "${LANE_FAKE_VR:-}" ] && { [ -e "$LANE_FAKE_VR" ]; return; }   # (tests only: this file = the user is in VR)
  systemctl --user is-active -q somvr-headset && return 0
  python3 -I -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1])).get("user") else 1)' "$RT/vr-session/status.json" 2>/dev/null
}
NAME=${LANE_NAME:-$(basename "$1")}; NAME=${NAME//[^A-Za-z0-9_.-]/_}
vr_ok() {   # may this job run while the user is in VR? (LANE_IN_VR=1 and its name in LANE_VR_JOBS: globs, `all`)
  [ "${LANE_IN_VR:-0}" = 1 ] || return 1
  local p pats; read -ra pats <<< "${LANE_VR_JOBS:-all} ${LANE_VR_GUARDED:-}"   # (read: no file-name expansion;
  # a LANE_VR_GUARDED job may start - lane_watch.py holds it while the session's frames are not watched)
  for p in "${pats[@]}"; do [ "$p" = all ] && return 0; [[ $NAME == $p ]] && return 0; done
  return 1
}
if ! vr_ok; then
  W=0
  WHY="LANE_IN_VR=${LANE_IN_VR:-0}"; [ "${LANE_IN_VR:-0}" = 1 ] && WHY="'$NAME' not in LANE_VR_JOBS ($LANE_VR_JOBS)"
  while in_vr; do
    [ "${LANE_WAIT:-1}" = 0 ] && { say "the user is in VR ($WHY): not now (exit 4)"; exit 4; }
    [ $W = 0 ] && say "the user is in VR ($WHY): waiting until the session ends"
    W=1; sleep 5
  done
fi
# 4. the watcher (pauses the lane under a session it protects; keeps every lane thread on the lane's CPUs)
# b37 live3: the shared watcher runs the MAIN checkout's lane_watch.py (it guards the user's session for every caller) -
# never a helper worktree's unmerged copy (a helper's boot on 11-04 20:28 started it from its own tree); LANE_WATCH_PY
# overrides (a test of a new watcher, never while the user may play)
WPY=${LANE_WATCH_PY:-$HOME/Projects/flat2vr/ShadowOfMordor/scripts/lane_watch.py}; [ -f "$WPY" ] || WPY=$HERE/lane_watch.py
if [ "${LANE_NO_WATCH:-0}" != 1 ] && ! systemctl --user is-active -q somvr-lane-watch; then
  systemd-run --user --quiet --collect --unit=somvr-lane-watch --description="SoM VR lane watcher (docs/re-xr-pipeline.md 163)" \
    python3 -I "$WPY" watch >/dev/null 2>&1 ||
    systemctl --user is-active -q somvr-lane-watch || say "note: the watcher did not start (systemctl --user status somvr-lane-watch)"
fi
# 5. the job
N=$(python3 -I -c 'import sys
s=set()
for p in sys.argv[1].split(","):
    a,_,b=p.partition("-"); s.update(range(int(a), int(b or a)+1))
print(len(s))' "$LANE_CPUS")
exec systemd-run --user --scope --quiet --collect --expand-environment=no --slice=somvr-lane.slice --unit="lane-${NAME:0:40}-$$-$(date +%s)" \
  -- chrt --idle 0 taskset -c "$LANE_CPUS" nice -n 19 ionice -c 3 \
  env OMP_NUM_THREADS="$N" OPENBLAS_NUM_THREADS="$N" MKL_NUM_THREADS="$N" CT2_INTER_THREADS=1 ORT_NUM_THREADS="$N" \
      LANE_THREADS="$N" MAKEFLAGS="-j$N" SOMVR_JOBS="$N" CUDA_VISIBLE_DEVICES= HIP_VISIBLE_DEVICES= ROCR_VISIBLE_DEVICES= \
      SOMVR_LANE=1 "$@"
