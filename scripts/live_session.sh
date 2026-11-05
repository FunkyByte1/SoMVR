#!/bin/bash
# live_session.sh - a session's live folder and its daemon (docs/re-xr-pipeline.md 162). Called by som-win-session.sh
# (the user's sessions) and headless_win.sh (tests) when SOMVR_LIVE=on; by hand only for a test.
#
#   live_session.sh dir RUN                the session's live folder: ROOT/<RUN's name> (made: inbox/ say/ patches/
#                                          stt/ - the layer and the DLL need it before the game starts)
#   live_session.sh start RUN [PARENT] [MOD]  ROOT/current -> it, DIR/build.txt (MOD = the build's mod/somvr: the
#                                          layer's and the host's source hashes, live.py fingerprint), starts
#                                          live_daemon.py (its own session, nohup; PARENT = the session's pid: the
#                                          daemon ends when it is gone); prints the daemon's pid
#   live_session.sh preapply RUN MOD DLL   b40 live4 (162.13): before the game starts - the patch staged for the next
#                                          launch (ROOT/next, live_patch.sh stage --next) for exactly this build (MOD's
#                                          logic DLL and fingerprint) becomes the session's logic module: DLL (the
#                                          prefix's somvr_logic.dll) replaced, the build's own kept for Undo; one line out
#   live_session.sh stop RUN               SIGTERM to that daemon only (its pid file, its command line checked), then
#                                          at most 5 s for it to end; DIR/on removed
# SOMVR_LIVE_TTS=on in its environment (the session's switch): the daemon also speaks messages asked to (off by default).
# ROOT: LIVE_ROOT, else logs/live-test for a test run (logs/winport/..., a boot_check run), else logs/live (the user's).
# The daemon runs on the system python3 (-I, standard library); its speech-to-text worker in the lane (live_lane.sh).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
root_of() {
  if [ -n "${LIVE_ROOT:-}" ]; then echo "$LIVE_ROOT"
  else case $1 in */logs/win-session-*) echo "$SOM/logs/live" ;; *) echo "$SOM/logs/live-test" ;; esac; fi
}
cmd=${1:-}; RUN=${2:-}
[ -n "$RUN" ] || { echo "usage: live_session.sh dir|start|stop RUN [PARENT]" >&2; exit 2; }
RUN=$(cd "$RUN" 2>/dev/null && pwd || echo "$RUN")
ROOT=$(root_of "$RUN"); DIR=$ROOT/$(basename "$RUN")
case $cmd in
  dir) mkdir -p "$DIR"/inbox "$DIR"/say "$DIR"/patches "$DIR"/stt && echo "$DIR" ;;
  start)
    mkdir -p "$DIR"/inbox "$DIR"/say "$DIR"/patches "$DIR"/stt || exit 1
    ln -sfn "$DIR" "$ROOT/current"
    PARENT=${3:-0}
    [ -n "${4:-}" ] && [ -d "$4" ] && python3 -I "$HERE/live.py" fingerprint "$4" > "$DIR/build.txt" 2>> "$DIR/daemon.out"
    setsid nohup python3 -I "$HERE/live_daemon.py" --run "$RUN" --live "$DIR" --parent "$PARENT" --tts "${SOMVR_LIVE_TTS:-off}" \
      > "$DIR/daemon.out" 2>&1 < /dev/null &
    pid=$!
    echo "$pid" > "$DIR/daemon.pid"
    for i in $(seq 1 20); do [ -e "$DIR/on" ] && break; sleep 0.1; done
    echo "$pid" ;;
  preapply)
    mkdir -p "$DIR"/inbox "$DIR"/say "$DIR"/patches "$DIR"/stt || exit 1
    LIVE_ROOT=$ROOT python3 -I "$HERE/live.py" patch preapply --mod "${3:?MOD}" --dll "${4:?DLL}" --dir "$DIR" ;;
  stop)
    pid=$(cat "$DIR/daemon.pid" 2>/dev/null)
    if [ -n "$pid" ] && tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q -F -- "live_daemon.py --run $RUN --live $DIR"; then
      kill -TERM "$pid" 2>/dev/null
      for i in $(seq 1 50); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
      kill -0 "$pid" 2>/dev/null && echo "live: the daemon $pid still runs 5 s after SIGTERM" >&2
    fi
    rm -f "$DIR/on" ;;
  *) echo "usage: live_session.sh dir|start|preapply|stop RUN [PARENT | MOD DLL]" >&2; exit 2 ;;
esac
