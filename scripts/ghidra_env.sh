# ghidra_env.sh - sourced by ghidra_analyze.sh / ghidra_query.sh: one place for paths and Ghidra settings.
# Everything Ghidra writes (settings, OSGi script cache, temp files, logs, projects) stays in the workspace.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
GHIDRA_DIR=/usr/lib64/ghidra
HEADLESS=$GHIDRA_DIR/support/analyzeHeadless
GATE=$SOM/scripts/ghidra_gate.sh
PROJECTS=$SOM/reference/ghidra              # one Ghidra project per binary: $PROJECTS/<name>/<name>.gpr
GHOME=$SOM/reference/ghidra-home            # Ghidra's user.home, XDG dirs and java.io.tmpdir
LOGS=$SOM/logs
CPUS=${SOM_GHIDRA_CPUS:-12-15}              # taskset list (12-15 = SMT siblings of cores 4-7)
MAXCPU=${SOM_GHIDRA_MAXCPU:-4}              # analyzeHeadless -max-cpu
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-25-openjdk}
export PATH="/usr/bin:/bin:$PATH"
mkdir -p "$PROJECTS" "$GHOME/home" "$GHOME/tmp" "$GHOME/config" "$GHOME/cache" "$GHOME/data" "$LOGS"
export XDG_CONFIG_HOME=$GHOME/config XDG_CACHE_HOME=$GHOME/cache XDG_DATA_HOME=$GHOME/data
# analyzeHeadless appends GHIDRA_HEADLESS_JAVA_OPTIONS to the JVM arguments; -Xmx comes from GHIDRA_HEADLESS_MAXMEM.
export GHIDRA_HEADLESS_JAVA_OPTIONS="-Duser.home=$GHOME/home -Djava.io.tmpdir=$GHOME/tmp"
unset _JAVA_OPTIONS JAVA_TOOL_OPTIONS JDK_JAVA_OPTIONS GHIDRA_JAVA_OPTIONS

# som_lock NAME [WHO]: take the per-project lock $PROJECTS/NAME.lock on fd 9 (flock), so Ghidra JVMs that use the
# same project queue one after another instead of failing on Ghidra's own project lock (NAME/NAME.lock inside the
# project folder). The lock is held until this shell and every child that inherited fd 9 (the JVM) have exited.
# SOM_LOCK_WAIT=SECONDS caps the wait (default: wait as long as it takes). The file names the current holder.
som_lock() {
  local lock=$PROJECTS/$1.lock
  exec 9>>"$lock" || return 1
  if ! flock -n 9; then
    echo "$(date '+%F %T') waiting for the project lock $lock (held: $(head -c 200 "$lock" 2>/dev/null | tr '\n' ' '))" >&2
    if [ -n "${SOM_LOCK_WAIT:-}" ]; then
      flock -w "$SOM_LOCK_WAIT" 9 || { echo "gave up waiting for $lock after $SOM_LOCK_WAIT s" >&2; return 1; }
    else
      flock 9
    fi
    echo "$(date '+%F %T') got the project lock $lock" >&2
  fi
  printf 'pid=%s since=%s by=%s\n' "$$" "$(date '+%F %T')" "${2:-?}" >"$lock"
}
