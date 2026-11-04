# buildplan.sh - sourced by build.sh, vklayer/build.sh and win/build.sh (docs/re-xr-pipeline.md 161): their commands
# become records of a plan that buildplan.py runs in parallel, printed in the plan's order (the serial script's log).
#   plan_open DIR      a new plan (DIR: the timing history), or - SOMVR_PLAN set by the calling script (build.sh calls
#                      vklayer/build.sh) - the caller's plan, appended to
#   gcc ARGS / clang ARGS   a compiler line, as written: compiled per source with the object cache, then linked
#   run [-k] [-d PATH]... [-o PATH]... [--] ARGV...   a command, after whatever makes its program and the files it names
#   say TEXT           a line of the log, in its place (an echo at plan time would print before everything)
#   built PATH [SUFFIX]   "built PATH (sha16)SUFFIX" once PATH is made
#   plan_go            run the plan and end the script with its exit code (a plan opened by a caller: just exit 0)
# Every command of a check goes through these: a plain command runs at plan time, before anything is built (only for
# what needs no build: wintables.py --out, a file the plan reads). A multi-line check is a step: a function step_NAME in
# the script, planned as `run -d WHAT_IT_READS -- SCRIPT --step NAME` (the script runs it and exits).
BP_PY=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/buildplan.py
BP_FILE= BP_OWN=0 BP_STATE=
plan_open() {
  BP_STATE=$1
  # a test's run line at the line start without `run` would run at plan time: before its build (or the old program)
  local bad
  bad=$(grep -nE '^"\$\{?[A-Z]+\}?(/[A-Za-z0-9_.]+)*/tests/[A-Za-z0-9_]+"' "${BASH_SOURCE[1]}" || true)
  [ -z "$bad" ] || { echo "FAIL: ${BASH_SOURCE[1]}: a test's run line without run (buildplan.sh):"; echo "$bad"; exit 1; }
  if [ -n "${SOMVR_PLAN:-}" ] && [ -f "$SOMVR_PLAN" ]; then
    BP_FILE=$SOMVR_PLAN BP_OWN=0
  else
    BP_FILE=$(mktemp "${TMPDIR:-/tmp}/somvr-plan.XXXXXXXX")
    BP_OWN=1
    export SOMVR_PLAN=$BP_FILE
    trap 'rm -f -- "$BP_FILE"' EXIT
  fi
}
bp_rec() { [ -n "$BP_FILE" ] || { echo "buildplan: $1 before plan_open"; exit 2; }; printf '%s\0' R "$1" "$PWD" "$(($# - 1))" "${@:2}" >> "$BP_FILE"; }
gcc() { bp_rec gcc gcc "$@"; }
clang() { bp_rec gcc clang "$@"; }
run() { bp_rec run "$@"; }
say() { bp_rec echo "$*"; }
built() { bp_rec built "$@"; }
plan_go() {
  [ "$BP_OWN" = 1 ] || exit 0
  local rc=0
  python3 -I "$BP_PY" "$BP_FILE" --state "$BP_STATE" || rc=$?
  exit $rc
}
