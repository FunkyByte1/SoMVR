#!/usr/bin/env bash
# ghidra_analyze.sh - import + Ghidra's default auto-analysis of binaries into PERSISTENT Ghidra projects, run one
# after another in a detached systemd user unit (nice 19, idle I/O, 4 CPUs, OOM-preferred) that the heavy-job gate
# (ghidra_gate.sh) pauses with SIGSTOP and resumes with SIGCONT.
#
#   ghidra_analyze.sh start NAME BINARY [NAME BINARY ...]  start unit som-ghidra running the queue, return at once
#   ghidra_analyze.sh queue NAME BINARY [NAME BINARY ...]  the unit body: run each job in order (a failed job
#                                                          doesn't stop the next), then write reference/analysis-done
#   ghidra_analyze.sh run   NAME BINARY                    one job (also usable from a terminal)
#   ghidra_analyze.sh status [NAME ...]                    unit state, markers, JVM state/CPU time/RSS, log tails
# Env: SOM_GHIDRA_MAXMEM (JVM -Xmx, default 24G), SOM_GHIDRA_OVERWRITE=1 (re-import over an existing project),
#      SOM_GHIDRA_CPUS / SOM_GHIDRA_MAXCPU (ghidra_env.sh), SOM_GHIDRA_UNIT (default som-ghidra).
#
# Per job NAME (all under ShadowOfMordor/, all git-ignored):
#   reference/ghidra/NAME/NAME.gpr     the project (program = basename of BINARY); query it with ghidra_query.sh
#   logs/ghidra-NAME.log               analyzeHeadless console output (appended per run)
#   logs/ghidra-NAME.ghidra.log        Ghidra's own application log
#   logs/ghidra-NAME.gate.log          every gate pause/resume with a timestamp
#   reference/ghidra/NAME.pid          pid of the analyzeHeadless shell while it runs
#   reference/analysis-done-NAME       when the job ends: "exit=<status> finished=<time> ... saved|check <log>"
# reference/analysis-done is written when the whole queue ends (one line per job).
# No wall-clock deadline: analyzeHeadless gets no -analysisTimeoutPerFile, so gate pauses can't kill a job.
# SIGSTOP/SIGCONT/systemd need the real PID namespace: run start/status OUTSIDE the Claude Bash sandbox.
set -u
. "$HOME/Projects/flat2vr/ShadowOfMordor/scripts/ghidra_env.sh"
SELF=$SOM/scripts/ghidra_analyze.sh
UNIT=${SOM_GHIDRA_UNIT:-som-ghidra}
MAXMEM=${SOM_GHIDRA_MAXMEM:-24G}
usage() { sed -n '6,10p' "$SELF" >&2; exit 2; }
checkname() { case "$1" in ''|*[!A-Za-z0-9_.-]*) echo "bad NAME '$1' (use [A-Za-z0-9_.-])" >&2; exit 2 ;; esac; }

cmd=${1:-}; shift || true
case "$cmd" in
  start)
    [ $# -ge 2 ] && [ $(( $# % 2 )) = 0 ] || usage
    args=()
    while [ $# -gt 0 ]; do
      checkname "$1"; b=$(readlink -f "$2")
      [ -f "$b" ] || { echo "no such binary: $2" >&2; exit 2; }
      args+=("$1" "$b"); shift 2
    done
    systemd-run --user --collect --unit="$UNIT" --description="Ghidra analysis queue (Flat2VR ShadowOfMordor)" \
      -p Nice=19 -p CPUAffinity="$CPUS" -p IOSchedulingClass=idle -p OOMScoreAdjust=500 \
      --setenv=SOM_GHIDRA_MAXMEM="$MAXMEM" --setenv=SOM_GHIDRA_OVERWRITE="${SOM_GHIDRA_OVERWRITE:-0}" \
      /bin/bash "$SELF" queue "${args[@]}"
    ;;
  queue)
    [ $# -ge 2 ] && [ $(( $# % 2 )) = 0 ] || usage
    rm -f "$SOM/reference/analysis-done"
    summary=""
    while [ $# -gt 0 ]; do
      /bin/bash "$SELF" run "$1" "$2"
      summary+="$1: $(cat "$SOM/reference/analysis-done-$1" 2>/dev/null || echo "no marker (exit $?)")"$'\n'
      shift 2
    done
    printf '%s' "$summary" >"$SOM/reference/analysis-done"
    ;;
  run)
    NAME=${1:-}; checkname "$NAME"; BIN=$(readlink -f "${2:?BINARY}")
    LOG=$LOGS/ghidra-$NAME.log; GATELOG=$LOGS/ghidra-$NAME.gate.log
    PIDF=$PROJECTS/$NAME.pid; MARK=$SOM/reference/analysis-done-$NAME
    exec >>"$LOG" 2>&1
    echo "=== $(date '+%F %T') job start: name=$NAME binary=$BIN maxmem=$MAXMEM cpus=$CPUS max-cpu=$MAXCPU" \
         "pid=$$ sched/nice=$(ps -o cls=,ni= -p $$ | xargs)"
    echo "sha256 $(sha256sum "$BIN" | cut -d' ' -f1)"
    rm -f "$MARK"
    finish() { echo "exit=$1 finished=$(date '+%F %T') name=$NAME binary=$BIN $2" >"$MARK"; echo "=== $(date '+%F %T') job end: exit=$1 $2"; exit "$1"; }
    over=()
    if [ -e "$PROJECTS/$NAME/$NAME.gpr" ]; then
      [ "${SOM_GHIDRA_OVERWRITE:-0}" = 1 ] || finish 3 "project $PROJECTS/$NAME/$NAME.gpr exists (SOM_GHIDRA_OVERWRITE=1 re-imports)"
      over=(-overwrite)
    fi
    mkdir -p "$PROJECTS/$NAME"
    som_lock "$NAME" "ghidra_analyze.sh run $NAME" || finish 4 "could not take the project lock"
    "$GATE" wait "$GATELOG"
    export GHIDRA_HEADLESS_MAXMEM=$MAXMEM
    nice -n 19 taskset -c "$CPUS" "$HEADLESS" "$PROJECTS/$NAME" "$NAME" -import "$BIN" "${over[@]}" \
      -max-cpu "$MAXCPU" -log "$LOGS/ghidra-$NAME.ghidra.log" -scriptlog "$LOGS/ghidra-$NAME.script.log" &
    A=$!; echo "$A" >"$PIDF"
    "$GATE" watch "$A" "$GATELOG" 2>/dev/null &
    W=$!
    # unit stop: wake the tree so it can die, then terminate it
    trap 'kill -s CONT $("$GATE" tree "$A") 2>/dev/null; kill $("$GATE" tree "$A") 2>/dev/null' TERM INT HUP
    while :; do wait "$A"; st=$?; kill -0 "$A" 2>/dev/null || break; done
    kill "$W" 2>/dev/null; wait "$W" 2>/dev/null
    rm -f "$PIDF"
    if [ "$st" = 0 ] && tail -n 400 "$LOG" | grep -q "REPORT: Save succeeded"; then note=saved; else note="check $LOG"; fi
    finish "$st" "$note"
    ;;
  status)
    systemctl --user show "$UNIT" -p ActiveState -p SubState -p ExecMainPID 2>/dev/null | xargs echo "$UNIT:"
    [ -f "$SOM/reference/analysis-done" ] && sed 's/^/queue done: /' "$SOM/reference/analysis-done"
    [ $# -gt 0 ] || set -- $(cd "$PROJECTS" 2>/dev/null && ls -d */ 2>/dev/null | tr -d /)
    for NAME in "$@"; do
      echo "--- $NAME"
      MARK=$SOM/reference/analysis-done-$NAME; PIDF=$PROJECTS/$NAME.pid
      [ -f "$MARK" ] && echo "marker: $(cat "$MARK")"
      if [ -f "$PIDF" ]; then
        for p in $("$GATE" tree "$(cat "$PIDF")"); do
          [ "$(cat /proc/"$p"/comm 2>/dev/null)" = java ] || continue
          read -r -a f < <(cut -d' ' -f3,14,15,22 /proc/"$p"/stat)
          tck=$(getconf CLK_TCK); up=$(cut -d' ' -f1 /proc/uptime)
          echo "jvm pid=$p state=${f[0]} cpu=$(( (f[1] + f[2]) / tck ))s age=$(awk -v u="$up" -v s="${f[3]}" -v t="$tck" 'BEGIN{printf "%d", u - s/t}')s" \
               "rss=$(awk '/VmRSS/{printf "%.1fG", $2/1048576}' /proc/"$p"/status)" \
               "cpus=$(awk '/Cpus_allowed_list/{print $2}' /proc/"$p"/status) nice=$(ps -o ni= -p "$p" | xargs)"
          # which analyzers are on the stack right now (a paused JVM can't answer: skip it)
          [ "${f[0]}" = T ] || timeout 20 "$JAVA_HOME/bin/jcmd" "$p" Thread.print 2>/dev/null \
            | grep -oE 'ghidra\.[A-Za-z0-9_.]*\.[A-Z][A-Za-z0-9]*Analyzer\b' | grep -v -E '\.(Abstract|Headless)Analyzer' \
            | sort -u | sed 's/.*\./analyzer: /' | xargs echo
        done
      fi
      tail -n 2 "$LOGS/ghidra-$NAME.gate.log" 2>/dev/null | sed 's/^/gate: /'
      grep -v -E '^\s*$' "$LOGS/ghidra-$NAME.log" 2>/dev/null | tail -n 3 | cut -c1-200 | sed 's/^/log: /'
    done
    ;;
  *) usage ;;
esac
