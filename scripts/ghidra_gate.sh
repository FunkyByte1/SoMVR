#!/usr/bin/env bash
# ghidra_gate.sh - the heavy-job gate for Ghidra/JVM work (house rule: no heavy jobs while the user is in VR
# or while a game helper is measuring frame times).
#
# The gate is CLOSED when any of these holds:
#   - /run/user/1000/vr-session/status.json is missing, unreadable, or its "updated" is older than 90 s
#   - status.json has "user": true (or no "user" field at all)
#   - /run/user/1000/vr-session/quiet exists with an mtime less than 120 s old
# "test": true with "user": false alone does NOT close it.
#
# Usage:
#   ghidra_gate.sh check            print "open" or "closed: <reasons>"; exit 0 when open, 1 when closed
#   ghidra_gate.sh wait [LOG]       block until the gate is open (polls every GATE_INTERVAL s)
#   ghidra_gate.sh watch PID [LOG]  while PID lives: every GATE_INTERVAL s SIGSTOP the process tree under PID
#                                   (PID and all descendants) when the gate closes, SIGCONT it when it opens;
#                                   each change is logged with a timestamp. Exits when PID is gone.
#   ghidra_gate.sh tree PID         print PID and its descendants
#
# Must run in the same PID namespace as the job (i.e. NOT inside the Claude Bash sandbox for a job that was
# started outside it). Env overrides (for tests): GATE_STATUS, GATE_QUIET, GATE_INTERVAL.
# If the watcher itself is killed it leaves the tree as it is (a paused tree stays paused). systemd sends
# SIGCONT after SIGTERM when its unit is stopped, so `systemctl --user stop <unit>` works while paused.
set -u

STATUS=${GATE_STATUS:-/run/user/1000/vr-session/status.json}
QUIET=${GATE_QUIET:-/run/user/1000/vr-session/quiet}
INTERVAL=${GATE_INTERVAL:-15}
LOG=/dev/null

log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >>"$LOG"; [ "$LOG" = /dev/stderr ] || printf '%s %s\n' "$(date '+%F %T')" "$*" >&2; }

# Prints nothing when the gate is open, else the reasons it is closed.
gate_reason() {
  python3 -I - "$STATUS" "$QUIET" <<'PY'
import json, os, sys, time
status, quiet = sys.argv[1], sys.argv[2]
now = time.time()
why = []
try:
    with open(status) as f:
        d = json.load(f)
    age = now - float(d.get("updated", 0))
    if age > 90:
        why.append("status.json stale (%.0f s old)" % age)
    if "user" not in d:
        why.append("status.json has no 'user' field")
    elif d["user"] is not False:
        why.append("user is in VR")
except FileNotFoundError:
    why.append("status.json missing")
except Exception as e:
    why.append("status.json unreadable (%s)" % e.__class__.__name__)
try:
    qage = now - os.stat(quiet).st_mtime
    if qage < 120:
        why.append("quiet marker fresh (%.0f s old)" % qage)
except FileNotFoundError:
    pass
except Exception as e:
    why.append("quiet marker unreadable (%s)" % e.__class__.__name__)
print("; ".join(why))
PY
}

# PID and all its descendants, parents before children.
tree() {
  local root=$1
  ps -e -o pid=,ppid= | awk -v root="$root" '
    { kids[$2] = kids[$2] " " $1 }
    END {
      n = 1; q[1] = root; i = 1
      while (i <= n) { p = q[i++]; print p; m = split(kids[p], a, " "); for (j = 1; j <= m; j++) q[++n] = a[j] }
    }'
}

signal_tree() {  # $1 = STOP|CONT, $2 = root pid; prints the pids it signalled
  local sig=$1 root=$2 pids
  pids=$(tree "$root" | tr '\n' ' ')
  if [ "$sig" = CONT ]; then pids=$(printf '%s\n' $pids | tac | tr '\n' ' '); fi   # children first
  # shellcheck disable=SC2086
  kill -s "$sig" $pids 2>/dev/null
  echo "$pids"
}

cmd=${1:-check}
case "$cmd" in
  check)
    r=$(gate_reason)
    if [ -z "$r" ]; then echo open; exit 0; else echo "closed: $r"; exit 1; fi
    ;;
  wait)
    LOG=${2:-/dev/null}
    last=""
    while :; do
      r=$(gate_reason)
      [ -z "$r" ] && { [ -n "$last" ] && log "gate OPEN, starting"; exit 0; }
      [ "$r" != "$last" ] && log "gate CLOSED ($r), waiting to start"
      last=$r
      sleep "$INTERVAL"
    done
    ;;
  watch)
    pid=${2:?watch needs a PID}
    LOG=${3:-/dev/null}
    trap 'log "watcher for $pid exiting (signal), tree left $state"; exit 0' TERM INT HUP
    state=running
    log "watcher started for pid $pid (interval ${INTERVAL}s)"
    while kill -0 "$pid" 2>/dev/null; do
      r=$(gate_reason)
      if [ -n "$r" ]; then
        if [ "$state" = running ]; then
          p=$(signal_tree STOP "$pid"); state=paused
          log "gate CLOSED ($r): SIGSTOP $p"
        else
          signal_tree STOP "$pid" >/dev/null   # catch children started since the pause
        fi
      elif [ "$state" = paused ]; then
        p=$(signal_tree CONT "$pid"); state=running
        log "gate OPEN: SIGCONT $p"
      fi
      sleep "$INTERVAL" & wait $!
    done
    log "pid $pid is gone, watcher done"
    ;;
  tree)
    tree "${2:?tree needs a PID}"
    ;;
  *)
    echo "usage: $0 check | wait [LOG] | watch PID [LOG] | tree PID" >&2; exit 2
    ;;
esac
