#!/usr/bin/env bash
# ghidra_query.sh - read-only queries against an analysed persistent Ghidra project (built by ghidra_analyze.sh).
#
#   ghidra_query.sh NAME OUTFILE COMMAND [ARGS...]     e.g.  ghidra_query.sh som-win64 /abs/out.txt decompile 0x140001000
#   ghidra_query.sh NAME OUTFILE @BATCHFILE            one command per line: one JVM start + project open for all
#
# NAME = project under reference/ghidra/ (som-win64 = x64/ShadowOfMordor.exe, som-linux = Feral bin/ShadowOfMordor).
# Commands: info | func ADDR | decompile ADDR [SECS] | disasm ADDR [N] | xrefs ADDR [LIMIT] | strings REGEX [LIMIT]
#           symbols REGEX [LIMIT] | functions REGEX [LIMIT] | classes REGEX [LIMIT] | bytes ADDR [N]
#           (ADDR = 0x hex, a symbol/function name or Namespace::name; see scripts/ghidra/SomQuery.java)
# Opens the project -readOnly -noanalysis: nothing is saved. Takes the per-project lock reference/ghidra/NAME.lock
# (flock: concurrent callers queue; SOM_LOCK_WAIT=SECONDS caps the wait, exit 4), then waits for the heavy-job gate,
# runs nice 19 on 4 CPUs. Refuses while the project's analysis job is still running (exit 3).
# Ghidra's console output goes to logs/ghidra-query-NAME.log; the results go to OUTFILE (absolute path).
set -u
. /home/bara/Projects/flat2vr/ShadowOfMordor/scripts/ghidra_env.sh
NAME=${1:?NAME}; OUT=${2:?OUTFILE}; shift 2
[ $# -gt 0 ] || { echo "missing COMMAND" >&2; exit 2; }
case "$OUT" in /*) ;; *) OUT=$PWD/$OUT ;; esac
[ -e "$PROJECTS/$NAME/$NAME.gpr" ] || { echo "no project $PROJECTS/$NAME/$NAME.gpr" >&2; exit 2; }
[ -e "$PROJECTS/$NAME.pid" ] && { echo "the analysis of $NAME is still running (pid file $PROJECTS/$NAME.pid)" >&2; exit 3; }
som_lock "$NAME" "ghidra_query.sh $*" || exit 4   # queue behind other JVMs on this project
"$GATE" wait /dev/stderr
export GHIDRA_HEADLESS_MAXMEM=${SOM_QUERY_MAXMEM:-8G}
rm -f "$OUT"
nice -n 19 taskset -c "$CPUS" "$HEADLESS" "$PROJECTS/$NAME" "$NAME" -process -noanalysis -readOnly -max-cpu "$MAXCPU" \
  -scriptPath "$SOM/scripts/ghidra" -postScript SomQuery.java "$OUT" "$@" >"$LOGS/ghidra-query-$NAME.log" 2>&1 &
A=$!
"$GATE" watch "$A" "$LOGS/ghidra-query-$NAME.gate.log" 2>/dev/null &   # pauses a long batch if VR starts
W=$!
trap 'kill -s CONT $("$GATE" tree "$A") 2>/dev/null; kill $("$GATE" tree "$A") "$W" 2>/dev/null' TERM INT HUP
while :; do wait "$A"; st=$?; kill -0 "$A" 2>/dev/null || break; done
kill "$W" 2>/dev/null; wait "$W" 2>/dev/null
if [ ! -s "$OUT" ]; then
  echo "query failed (exit $st), see $LOGS/ghidra-query-$NAME.log:" >&2
  grep -E 'ERROR|Exception|SomQuery' "$LOGS/ghidra-query-$NAME.log" | head -n 20 >&2
  exit 1
fi
echo "$OUT"
