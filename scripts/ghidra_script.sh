#!/usr/bin/env bash
# ghidra_script.sh - run one of our Ghidra scripts (scripts/ghidra/*.java) against an analysed persistent project,
# read-only or WRITING (the script's changes are saved into the project), under the house rules: the per-project
# lock (queue behind other JVMs on the project), the heavy-job gate (wait + watch), nice 19, CPUs 12-15,
# -max-cpu 4, -Xmx at most 24G, -noanalysis (no auto-analysis runs after the script's changes).
#
#   ghidra_script.sh run    NAME ro|rw SCRIPT [ARGS...]   in the foreground (also works inside the Claude sandbox)
#   ghidra_script.sh start  NAME ro|rw SCRIPT [ARGS...]   detached: systemd user unit som-script-NAME, returns at
#                                                         once (run start/status OUTSIDE the Claude sandbox)
#   ghidra_script.sh status NAME                          unit state, done marker, the script's last log lines
# SCRIPT = a file name under scripts/ghidra/ (e.g. SomVtables.java); ARGS go to the script (use absolute paths).
# Per NAME (git-ignored): logs/ghidra-script-NAME.log (console, appended per run), logs/ghidra-script-NAME.gate.log,
#   logs/ghidra-script-NAME.done = "exit=.. finished=.. mode=.. script=.. secs=.. saved|read-only|check <log>"
# Env: SOM_SCRIPT_MAXMEM (JVM -Xmx, default 16G rw / 8G ro, capped at 24G), SOM_LOCK_WAIT (cap on the lock wait, s).
# rw: back the project up first (cp -a reference/ghidra/NAME ...); a saved headless run can't be undone.
set -u
. "$HOME/Projects/flat2vr/ShadowOfMordor/scripts/ghidra_env.sh"
SELF=$SOM/scripts/ghidra_script.sh
usage() { sed -n '7,11p' "$SELF" >&2; exit 2; }
checkname() { case "$1" in ''|*[!A-Za-z0-9_.-]*) echo "bad NAME '$1' (use [A-Za-z0-9_.-])" >&2; exit 2 ;; esac; }

cmd=${1:-}; shift || true
case "$cmd" in
  start|run)
    [ $# -ge 3 ] || usage
    NAME=$1; MODE=$2; SCRIPT=$3; shift 3
    checkname "$NAME"
    case "$MODE" in ro|rw) ;; *) usage ;; esac
    [ -f "$SOM/scripts/ghidra/$SCRIPT" ] || { echo "no script $SOM/scripts/ghidra/$SCRIPT" >&2; exit 2; }
    [ -e "$PROJECTS/$NAME/$NAME.gpr" ] || { echo "no project $PROJECTS/$NAME/$NAME.gpr" >&2; exit 2; }
    [ -e "$PROJECTS/$NAME.pid" ] && { echo "the analysis of $NAME is still running (pid file $PROJECTS/$NAME.pid)" >&2; exit 3; }
    MEM=${SOM_SCRIPT_MAXMEM:-$([ "$MODE" = rw ] && echo 16G || echo 8G)}
    case "$MEM" in [0-9]*G) [ "${MEM%G}" -le 24 ] 2>/dev/null || MEM=24G ;; *) echo "SOM_SCRIPT_MAXMEM must look like 16G" >&2; exit 2 ;; esac
    ;;
esac

case "$cmd" in
  start)
    systemd-run --user --collect --unit="som-script-$NAME" --description="Ghidra script $SCRIPT on $NAME ($MODE, Flat2VR ShadowOfMordor)" \
      -p Nice=19 -p CPUAffinity="$CPUS" -p IOSchedulingClass=idle -p OOMScoreAdjust=500 \
      --setenv=SOM_SCRIPT_MAXMEM="$MEM" ${SOM_LOCK_WAIT:+--setenv=SOM_LOCK_WAIT="$SOM_LOCK_WAIT"} \
      /bin/bash "$SELF" run "$NAME" "$MODE" "$SCRIPT" "$@"
    ;;
  run)
    LOG=$LOGS/ghidra-script-$NAME.log; GATELOG=$LOGS/ghidra-script-$NAME.gate.log; DONE=$LOGS/ghidra-script-$NAME.done
    rm -f "$DONE"
    touch "$LOG"; from=$(( $(wc -l <"$LOG") + 1 ))
    echo "=== $(date '+%F %T') start: $NAME $MODE $SCRIPT $* maxmem=$MEM cpus=$CPUS max-cpu=$MAXCPU pid=$$" \
         "sched/nice=$(ps -o cls=,ni= -p $$ 2>/dev/null | xargs)" >>"$LOG"
    finish() {
      echo "exit=$1 finished=$(date '+%F %T') name=$NAME mode=$MODE script=$SCRIPT secs=$SECONDS $2" | tee "$DONE"
      echo "=== $(date '+%F %T') end: exit=$1 $2" >>"$LOG"
      exit "$1"
    }
    som_lock "$NAME" "ghidra_script.sh $MODE $SCRIPT" 2>>"$LOG" || finish 4 "could not take the project lock"
    "$GATE" wait "$GATELOG"
    SECONDS=0
    ro=(); [ "$MODE" = ro ] && ro=(-readOnly)
    export GHIDRA_HEADLESS_MAXMEM=$MEM
    nice -n 19 taskset -c "$CPUS" "$HEADLESS" "$PROJECTS/$NAME" "$NAME" -process -noanalysis "${ro[@]}" -max-cpu "$MAXCPU" \
      -scriptPath "$SOM/scripts/ghidra" -postScript "$SCRIPT" "$@" >>"$LOG" 2>&1 &
    A=$!
    "$GATE" watch "$A" "$GATELOG" 2>/dev/null &
    W=$!
    trap 'kill -s CONT $("$GATE" tree "$A") 2>/dev/null; kill $("$GATE" tree "$A") "$W" 2>/dev/null' TERM INT HUP
    while :; do wait "$A"; st=$?; kill -0 "$A" 2>/dev/null || break; done
    kill "$W" 2>/dev/null; wait "$W" 2>/dev/null
    new=$(tail -n +"$from" "$LOG")
    if [ "$st" != 0 ] || grep -qE 'SCRIPT ERROR|Exception in|REPORT SCRIPT ERROR|Script.*failed' <<<"$new"; then note="check $LOG"
    elif [ "$MODE" = ro ]; then note=read-only
    elif grep -q 'REPORT: Save succeeded' <<<"$new"; then note=saved
    elif grep -q 'REPORT: Save' <<<"$new"; then note="check $LOG (save)"
    else note="nothing to save"; fi
    finish "$st" "$note"
    ;;
  status)
    NAME=${1:?NAME}
    systemctl --user show "som-script-$NAME" -p ActiveState -p SubState -p ExecMainPID 2>/dev/null | xargs echo "som-script-$NAME:"
    [ -f "$LOGS/ghidra-script-$NAME.done" ] && echo "done: $(cat "$LOGS/ghidra-script-$NAME.done")"
    head -c 300 "$PROJECTS/$NAME.lock" 2>/dev/null | sed 's/^/last lock holder: /'
    tail -n 2 "$LOGS/ghidra-script-$NAME.gate.log" 2>/dev/null | sed 's/^/gate: /'
    grep -vE '^\s*$' "$LOGS/ghidra-script-$NAME.log" 2>/dev/null | tail -n 4 | cut -c1-200 | sed 's/^/log: /'
    ;;
  *) usage ;;
esac
