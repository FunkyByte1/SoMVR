#!/bin/bash
# lane.sh - the VR-safe CPU lane's front door (som-b37-lane2, docs/re-xr-pipeline.md 163) in the form the live loop's
# scripts/live_lane.sh calls (162):
#   lane.sh run NAME -- CMD [ARGS...]   CMD in the lane (scripts/lane_run.sh with LANE_NAME=NAME; exec: CMD's exit code,
#                                       3 = refused (real-time caller, no slice), 4 = the user is in VR and the lane may
#                                       not run then with LANE_WAIT=0)
#   lane.sh status                      one line: installed or not, the CPUs, the quota, in VR or not, frozen or not,
#                                       the jobs in it, whether a job would run now
# Run it OUTSIDE the Bash sandbox (systemd-run needs the user bus). Everything else is lane_run.sh's (its environment).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
case ${1:-} in
  run)
    shift; NAME=${1:?usage: lane.sh run NAME -- CMD...}; shift
    [ "${1:-}" = -- ] && shift
    [ $# -ge 1 ] || { echo "usage: lane.sh run NAME -- CMD..." >&2; exit 2; }
    LANE_NAME=$NAME exec bash "$HERE/lane_run.sh" "$@" ;;
  status)
    exec python3 -I "$HERE/lane_watch.py" status --line ;;
  *) echo "usage: lane.sh run NAME -- CMD... | lane.sh status" >&2; exit 2 ;;
esac
