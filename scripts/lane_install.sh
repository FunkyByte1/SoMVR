#!/bin/bash
# lane_install.sh install|check|uninstall [--force] - the VR-safe CPU lane's systemd user slice (som-b35-lane,
# docs/re-xr-pipeline.md 163; /home/bara/Projects/flat2vr/SYSTEM-CHANGES.md). Run it OUTSIDE the Bash sandbox.
#   install    ~/.config/somvr-lane/lane.conf (from scripts/lane/lane.conf; an existing one is kept unless --force) and
#              ~/.config/systemd/user/somvr-lane.slice made from it, systemctl --user daemon-reload, then check
#   check      read only: the files, the slice's properties as systemd has them, its cgroup's cpu.idle/cpu.max/memory,
#              the watcher (unit somvr-lane-watch), frozen or not, the jobs in the lane
#   uninstall  refuses while a job is in the lane (--force: stops them); stops the watcher and the slice, removes both
#              files and the folder, daemon-reload, then shows that nothing is left
# Nothing here needs root. The lane's CPUs: systemd's AllowedCPUs needs the cpuset controller delegated to
# user@.service (root; not done): the slice file carries the line for the day it is, and lane_run.sh / lane_watch.py
# pin the lane's jobs by their affinity meanwhile.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
CFG=$HOME/.config/somvr-lane; CONF=$CFG/lane.conf
UD=$HOME/.config/systemd/user; SLICE=$UD/somvr-lane.slice
MODE=${1:-check}; FORCE=0; [ "${2:-}" = --force ] && FORCE=1
say() { printf 'lane_install: %s\n' "$*"; }

conf() {   # the installed conf, else the repo's; the environment wins (as lane_run.sh)
  local f=$CONF; [ -f "$f" ] || f=$HERE/lane/lane.conf
  # shellcheck disable=SC1090
  . "$f"
  LANE_CPUS=${LANE_CPUS:?}; LANE_QUOTA=${LANE_QUOTA:-200%}; LANE_QUOTA_PERIOD=${LANE_QUOTA_PERIOD:-10ms}
  LANE_MEMORY_HIGH=${LANE_MEMORY_HIGH:-6G}; LANE_MEMORY_MAX=${LANE_MEMORY_MAX:-10G}
}
lane_jobs() {   # the PIDs in the lane's subtree
  local cg; cg=$(systemctl --user show -p ControlGroup --value somvr-lane.slice 2>/dev/null)
  [ -n "$cg" ] && [ -d "/sys/fs/cgroup$cg" ] || return 0
  find "/sys/fs/cgroup$cg" -name cgroup.procs -exec cat {} + 2>/dev/null
}

check() {
  local ok=1 cg
  [ -f "$CONF" ] && say "conf: $CONF" || { say "conf: none ($CONF)"; ok=0; }
  [ -f "$SLICE" ] && say "slice file: $SLICE" || { say "slice file: none ($SLICE)"; ok=0; }
  systemctl --user show somvr-lane.slice -p LoadState -p ActiveState -p CPUWeight -p CPUQuotaPerSecUSec \
    -p CPUQuotaPeriodUSec -p IOWeight -p MemoryHigh -p MemoryMax -p MemorySwapMax -p TasksMax -p AllowedCPUs \
    2>/dev/null | sed 's/^/  /'
  cg=$(systemctl --user show -p ControlGroup --value somvr-lane.slice 2>/dev/null)
  if [ -n "$cg" ] && [ -d "/sys/fs/cgroup$cg" ]; then
    local d=/sys/fs/cgroup$cg
    say "cgroup $d: cpu.idle $(cat "$d/cpu.idle" 2>/dev/null) cpu.max '$(cat "$d/cpu.max" 2>/dev/null)'" \
        "memory.high $(cat "$d/memory.high" 2>/dev/null) memory.max $(cat "$d/memory.max" 2>/dev/null)" \
        "frozen $(grep -o 'frozen [01]' "$d/cgroup.events" 2>/dev/null | cut -d' ' -f2)"
  else
    say "cgroup: not made yet (systemd makes it at the first job: scripts/lane_run.sh)"
  fi
  local ctl; ctl=$(cat "/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/cgroup.subtree_control" 2>/dev/null)
  case " $ctl " in *" cpuset "*) say "cpuset: delegated (AllowedCPUs works)" ;;
    *) say "cpuset: not delegated to the user manager ($ctl): the lane's CPUs by affinity (lane_run.sh, lane_watch.py)" ;; esac
  say "watcher: $(systemctl --user is-active somvr-lane-watch 2>/dev/null)"
  local j; j=$(lane_jobs | tr '\n' ' '); say "jobs in the lane: ${j:-none}"
  [ $ok = 1 ]
}

case $MODE in
  install)
    conf
    mkdir -p "$CFG" "$UD" || exit 1
    if [ -f "$CONF" ] && [ $FORCE = 0 ]; then say "kept the installed $CONF (--force: replace it with the repo's)"
    else cp "$HERE/lane/lane.conf" "$CONF" && say "wrote $CONF"; conf; fi
    cat > "$SLICE" <<EOF
# somvr-lane.slice - the VR-safe CPU lane (Shadow of Mordor VR, som-b35-lane; docs/re-xr-pipeline.md 163).
# Made by ShadowOfMordor/scripts/lane_install.sh from $CONF; undo: lane_install.sh uninstall.
[Unit]
Description=SoM VR lane: background jobs beside a VR session (builds, tests, speech models)

[Slice]
# below every other task (cpu.idle); at most $LANE_QUOTA of a CPU, in short periods
CPUWeight=idle
CPUQuota=$LANE_QUOTA
CPUQuotaPeriodSec=$LANE_QUOTA_PERIOD
IOWeight=10
MemoryHigh=$LANE_MEMORY_HIGH
MemoryMax=$LANE_MEMORY_MAX
MemorySwapMax=0
TasksMax=1024
# needs the cpuset controller delegated to user@.service (root); lane_run.sh pins by affinity meanwhile
AllowedCPUs=$LANE_CPUS
EOF
    say "wrote $SLICE"
    systemctl --user daemon-reload || exit 1
    check ;;
  check) check ;;
  uninstall)
    J=$(lane_jobs | tr '\n' ' ')
    if [ -n "${J// /}" ] && [ $FORCE = 0 ]; then say "STOP: jobs in the lane ($J); wait for them or --force"; exit 1; fi
    if systemctl --user is-active -q somvr-lane-watch; then
      systemctl --user stop somvr-lane-watch && say "stopped the watcher (it gives back any pinned game threads)"
    fi
    systemctl --user stop somvr-lane.slice 2>/dev/null && say "stopped somvr-lane.slice"
    # (somvr.slice is the lane's implicit parent: stop it only if nothing else is in it)
    P=$(systemctl --user show -p ControlGroup --value somvr.slice 2>/dev/null)
    if [ -n "$P" ] && [ -d "/sys/fs/cgroup$P" ] && [ -z "$(find "/sys/fs/cgroup$P" -name cgroup.procs -exec cat {} + 2>/dev/null)" ]; then
      systemctl --user stop somvr.slice 2>/dev/null
    fi
    [ -f "$SLICE" ] && rm -f "$SLICE" && say "removed $SLICE"
    [ -f "$CONF" ] && rm -f "$CONF" && say "removed $CONF"
    [ -d "$CFG" ] && rmdir "$CFG" 2>/dev/null
    rm -rf "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/somvr-lane"
    systemctl --user daemon-reload
    systemctl --user reset-failed somvr-lane-watch 2>/dev/null
    say "left: slice file $([ -e "$SLICE" ] && echo yes || echo no), conf $([ -e "$CFG" ] && echo yes || echo no)," \
        "unit $(systemctl --user show -p LoadState --value somvr-lane.slice 2>/dev/null)" ;;
  *) echo "usage: lane_install.sh install|check|uninstall [--force]"; exit 2 ;;
esac
