#!/usr/bin/env bash
# test_boot_check_end.sh - boot_check.sh's end of a session (build 16, docs/re-xr-pipeline.md 53), without the game:
# steam_wait (the Steam wait: Steam's logs and the save files faked), quit_watch (the game's end watched: the layer's
# exit line, a crash, the frames), crash_sign / crash_procs and stop_game's fast stop are cut out of boot_check.sh and
# run against a temporary RUN with stand-in processes (sleep; a copy named FeralLinuxMessage; one that ignores SIGTERM).
#   scripts/test_boot_check_end.sh [BOOT_CHECK]   (default: the boot_check.sh next to this script; about 75 s)
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
BC=${1:-$S/boot_check.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/bc-end-test.XXXXXX") || exit 1
KIDS=()
trap 'for k in "${KIDS[@]}"; do kill -KILL "$k" 2>/dev/null; done; rm -rf -- "$T"' EXIT
fails=0
ok() { echo "ok   $*"; }
no() { echo "FAIL $*"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@"; then ok "$what"; else no "$what"; fi; }

eval "$(sed -n '/^tree() {/p; /^alive() {/p; /^crash_sign() {/,/^}/p; /^crash_procs() {/,/^}/p; /^stop_game() {/,/^}/p;
  /^steam_logs_new() {/,/^}/p; /^steam_wait() {/,/^}/p; /^quit_watch() {/,/^}/p' "$BC")"
for f in tree alive crash_sign crash_procs stop_game steam_logs_new steam_wait quit_watch; do
  declare -F "$f" > /dev/null || { echo "FAIL: $f not found in $BC"; exit 1; }
done
LOG=$T/run.log
log() { printf '%s %s\n' "$(date +%T)" "$*" >> "$LOG"; }
STEAM_UP=0   # pgrep -x steam: 0 = running
pgrep() { if [ "${1:-} ${2:-}" = "-x steam" ]; then return $STEAM_UP; fi; command pgrep "$@"; }
MAIN=$$

# ---- steam_wait
STEAMDIR=$T/steam; SAVEAPP=$T/save
mkdir -p "$STEAMDIR/logs" "$SAVEAPP/remote"
echo save > "$SAVEAPP/remote/GameData.sav"; echo cache > "$SAVEAPP/remotecache.vdf"
GPL=$STEAMDIR/logs/gameprocess_log.txt; CLL=$STEAMDIR/logs/cloud_log.txt
printf '[2026-10-26 23:00:00] AppID 241930 no longer tracking PID 4242, exit code 0\n' > "$GPL"   # an old session's
printf '[2026-10-26 23:00:00] [AppID 241930] Starting sync (up,AC Exit,)\n[2026-10-26 23:00:00] [AppID 241930] YldWriteCacheDirectoryToFile - saved\n' > "$CLL"
GP_OFF=$(stat -c %s "$GPL"); CL_OFF=$(stat -c %s "$CLL")
GAME_PID=4242
secs() { awk -v a="$1" -v b="$2" 'BEGIN { printf "%.1f", b - a }'; }
now() { date +%s.%N; }

# 1. the usual end: tracked, dropped, the exit sync, the files touched once -> done ~3 s after the last change
printf '[x] AppID 241930 adding PID 4242 as a tracked process\n' >> "$GPL"
( sleep 0.7; printf '[x] [AppID 241930] Starting sync (up,AC Exit,)\n' >> "$CLL"; touch "$SAVEAPP/remotecache.vdf"
  printf '[x] [AppID 241930] YldWriteCacheDirectoryToFile - saved\n' >> "$CLL"
  printf '[x] AppID 241930 no longer tracking PID 4242, exit code -1\n' >> "$GPL" ) &
t0=$(now); : > "$LOG"; steam_wait; d=$(secs "$t0" "$(now)"); wait
check "steam_wait: the usual end done in $d s (3..5)" awk -v d="$d" 'BEGIN { exit !(d >= 3 && d <= 5) }'
check "steam_wait: its lines" grep -q "steam: it dropped the game's process" "$LOG"
check "steam_wait: the sync line" grep -q 'steam: its exit sync ended' "$LOG"
check "steam_wait: done line" grep -q 'steam: done after' "$LOG"
check "steam_wait: an old session's lines before the launch do not count (the drop came at ~0.7 s)" \
  bash -c "! grep -q 'dropped the game.s process (after 0.0 s)' '$LOG'"

# 2. the files keep changing for 4 s after the drop -> done 3 s after they settle
GP_OFF=$(stat -c %s "$GPL"); CL_OFF=$(stat -c %s "$CLL")
printf '[x] AppID 241930 adding PID 4242 as a tracked process\n[x] AppID 241930 no longer tracking PID 4242, exit code -1\n' >> "$GPL"
printf '[x] [AppID 241930] Starting sync (up,AC Exit,)\n[x] [AppID 241930] YldWriteCacheDirectoryToFile - saved\n' >> "$CLL"
( for i in 1 2 3 4 5 6 7 8; do sleep 0.5; echo "$i" >> "$SAVEAPP/remotecache.vdf"; done ) &
t0=$(now); : > "$LOG"; steam_wait; d=$(secs "$t0" "$(now)"); wait
check "steam_wait: files changing 4 s -> done in $d s (6.5..8.5)" awk -v d="$d" 'BEGIN { exit !(d >= 6.5 && d <= 8.5) }'

# 3. a new file in remote/ counts as a change
GP_OFF=$(stat -c %s "$GPL"); CL_OFF=$(stat -c %s "$CLL")
printf '[x] AppID 241930 adding PID 4242 as a tracked process\n[x] AppID 241930 no longer tracking PID 4242, exit code -1\n' >> "$GPL"
( sleep 2; echo x > "$SAVEAPP/remote/new.sav" ) &
t0=$(now); : > "$LOG"; steam_wait; d=$(secs "$t0" "$(now)"); wait
check "steam_wait: a new remote/ file at 2 s, no exit sync -> done in $d s (5..7)" awk -v d="$d" 'BEGIN { exit !(d >= 5 && d <= 7) }'
check "steam_wait: no exit sync within 5 s" grep -q 'steam: no exit sync within 5 s' "$LOG"
rm -f "$SAVEAPP/remote/new.sav"

# 4. never tracked (no "adding PID") -> dropped after 3 s; Steam not running -> dropped at once
GP_OFF=$(stat -c %s "$GPL"); CL_OFF=$(stat -c %s "$CLL")
t0=$(now); : > "$LOG"; steam_wait; d=$(secs "$t0" "$(now)")
check "steam_wait: never tracked -> done in $d s (3..4.5)" awk -v d="$d" 'BEGIN { exit !(d >= 3 && d <= 4.5) }'
check "steam_wait: never tracked line" grep -q "steam: it never tracked the game's process (after 3.0 s)" "$LOG"
STEAM_UP=1; t0=$(now); : > "$LOG"; steam_wait; d=$(secs "$t0" "$(now)"); STEAM_UP=0
check "steam_wait: Steam not running -> done in $d s (3..4.5)" awk -v d="$d" 'BEGIN { exit !(d >= 3 && d <= 4.5) }'

# 5. tracked, never dropped -> 30 s, then checked anyway
GP_OFF=$(stat -c %s "$GPL"); CL_OFF=$(stat -c %s "$CLL")
printf '[x] AppID 241930 adding PID 4242 as a tracked process\n' >> "$GPL"
t0=$(now); : > "$LOG"; steam_wait; d=$(secs "$t0" "$(now)")
check "steam_wait: never dropped -> at most 30 s ($d s)" awk -v d="$d" 'BEGIN { exit !(d >= 29.5 && d <= 32) }'
check "steam_wait: the not-done line" grep -q 'steam: !!! not done after 30 s (the game process not dropped' "$LOG"
# 6. a rotated log (smaller than at the launch) is read from its start
GP_OFF=999999
printf '[x] AppID 241930 adding PID 4242 as a tracked process\n[x] AppID 241930 no longer tracking PID 4242, exit code -1\n' > "$GPL"
: > "$LOG"; steam_wait
check "steam_wait: a rotated log read from its start" grep -q "steam: it dropped the game's process (after 0.0 s)" "$LOG"

# ---- quit_watch
RUN=$T/run; mkdir -p "$RUN"; XRLOG=$RUN/xr-layer.log; PRELOAD=/x/somvr.so
QW_EXIT_S=2; QW_CRASH_S=1; QW_FRAMES_S=3
qw_reset() { QW_NEXT=0; QW_EXIT=""; QW_CRASH=""; QW_SEEN=0; QW_NOFR=""; QW_WHY=""; FAST_STOP=0; QUIT=0; : > "$LOG"
  : > "$XRLOG"; : > "$RUN/game.log"; : > "$RUN/somvr.log"; }
rate() { printf 'RATE t=1.0 dt=1.000 H1=0 H2=%s H3=%s H4=0 H6=0 H9=%s AUDIO=0\n' "$1" "$1" "$1" >> "$RUN/somvr.log"; }
qw_run() {   # SECS: quit_watch every 0.5 s until QUIT or SECS; d = the seconds it took (not in a subshell: QUIT)
  local t0 i; t0=$(now)
  for i in $(seq 1 $(($1 * 2))); do quit_watch; [ $QUIT = 1 ] && break; sleep 0.5; done
  d=$(secs "$t0" "$(now)")
}
sleep 600 & GAME_PID=$!; KIDS+=("$GAME_PID"); BW_PID=$GAME_PID

qw_reset; rate 90; printf '1.0 [1/2] xr: phase session\n' > "$XRLOG"
qw_run 4
check "quit_watch: a playing game (frames, no exit line) is left alone ($d s)" [ $QUIT = 0 ]
qw_reset; rate 90; printf '1.0 [1/2] xr: exit: the game is exiting (an exit handler, before the runtime is unloaded): the watcher stopped\n' > "$XRLOG"
qw_run 6
# (quit_watch counts whole seconds of date +%s: an exit line seen late in a second ends it 1..2 s later in real time)
check "quit_watch: alive 2 s after its exit line -> ended ($d s, 1..3.5)" bash -c "[ $QUIT = 1 ] && [ $FAST_STOP = 1 ] && awk -v d=$d 'BEGIN { exit !(d >= 1 && d <= 3.5) }'"
check "quit_watch: the exiting line" grep -q 'the game is exiting: (an exit handler' "$LOG"
check "quit_watch: why" bash -c "[ \"$QW_WHY\" = 'is still alive 2 s after its exit began' ]"
qw_reset; rate 90; printf 'ShadowOfMordor: dumped to "/x/crashes/a.dmp"\nGame crashed with signal 11\n' > "$RUN/game.log"
qw_run 4
check "quit_watch: a crash in game.log -> ended ($d s, 1..2.5)" bash -c "[ $QUIT = 1 ] && [ \"$QW_WHY\" = crashed ] && awk -v d=$d 'BEGIN { exit !(d >= 1 && d <= 2.5) }'"
check "quit_watch: the crash line" grep -q '!!! the game crashed (ShadowOfMordor: dumped to' "$LOG"
qw_reset; rate 90; quit_watch; rate 0
qw_run 6
check "quit_watch: H9=0 for 3 s after frames -> ended ($d s, 2.5..4.5)" bash -c "[ $QUIT = 1 ] && [ \"$QW_WHY\" = 'has shown no frame for 3 s' ] && awk -v d=$d 'BEGIN { exit !(d >= 2.5 && d <= 4.5) }'"
qw_reset; rate 0
qw_run 5
check "quit_watch: no frames before the first frame (the boot) are no sign ($d s)" [ $QUIT = 0 ]
# Feral's message box while the frames go on (its "previous run" reporter at a start) = no crash; once the frames stop = a crash
cp "$(command -v sleep)" "$T/FeralLinuxMessage"
bash -c "\"$T/FeralLinuxMessage\" 600 & wait" & BW_PID=$!; KIDS+=("$BW_PID")
sleep 0.5
qw_reset
for i in 1 2 3 4 5 6 7 8; do rate 90; quit_watch; sleep 0.5; done
check "quit_watch: FeralLinuxMessage with frames going on is no crash" bash -c "[ $QUIT = 0 ] && [ -z \"$QW_CRASH\" ]"
QW_FRAMES_S=60; rate 0
qw_run 8
check "quit_watch: FeralLinuxMessage 3 s after the frames stopped -> a crash, ended ($d s, 3..5.5)" bash -c "[ $QUIT = 1 ] && [ \"$QW_WHY\" = crashed ] && awk -v d=$d 'BEGIN { exit !(d >= 3 && d <= 5.5) }'"
check "quit_watch: the process named" grep -q 'the game crashed (running: FeralLinuxMessa' "$LOG"
for k in $(command pgrep -P "$BW_PID"); do kill -KILL "$k" 2>/dev/null; done; kill -KILL "$BW_PID" 2>/dev/null
kill "$GAME_PID" 2>/dev/null; wait "$GAME_PID" 2>/dev/null

# ---- stop_game: a game that ignores SIGTERM; the fast stop = 3 s, a crash during the 30 s grace = SIGKILL at once
CMD_BW=(bash -c 'trap "" TERM; sleep 600 & wait $!; wait $!')
PRELOAD=""; GAME_END=""
rm -f "$RUN/game.exit"; : > "$RUN/game.log"; : > "$LOG"; FAST_STOP=1
"${CMD_BW[@]}" & BW_PID=$!; GAME_PID=$BW_PID; KIDS+=("$BW_PID"); sleep 0.3
t0=$(now); stop_game 30; d=$(secs "$t0" "$(now)")
check "stop_game: the fast stop ends a SIGTERM-proof game in $d s (3..5)" bash -c "! kill -0 $GAME_PID 2>/dev/null && awk -v d=$d 'BEGIN { exit !(d >= 3 && d <= 5) }'"
check "stop_game: grace 3 s" grep -q "SIGTERM $GAME_PID (grace 3 s)" "$LOG"
rm -f "$RUN/game.exit"; : > "$RUN/game.log"; : > "$LOG"; FAST_STOP=0; GAME_END=""
"${CMD_BW[@]}" & BW_PID=$!; GAME_PID=$BW_PID; KIDS+=("$BW_PID"); sleep 0.3
( sleep 2; printf 'Game crashed with signal 11\n' >> "$RUN/game.log" ) &
t0=$(now); stop_game 30; d=$(secs "$t0" "$(now)"); wait
check "stop_game: a crash 2 s into the grace -> SIGKILL at once ($d s, 2..4)" bash -c "! kill -0 $GAME_PID 2>/dev/null && awk -v d=$d 'BEGIN { exit !(d >= 2 && d <= 4) }'"
check "stop_game: the crash line" grep -q '!!! the game crashed while stopping (Game crashed with signal 11): SIGKILL now' "$LOG"
rm -f "$RUN/game.exit"; printf 'Game crashed with signal 11\n' > "$RUN/game.log"; : > "$LOG"; FAST_STOP=0; GAME_END=""
"${CMD_BW[@]}" & BW_PID=$!; GAME_PID=$BW_PID; KIDS+=("$BW_PID"); sleep 0.3
t0=$(now); stop_game 30; d=$(secs "$t0" "$(now)")
check "stop_game: a game that crashed before the stop (the player's Stop) -> fast ($d s, 3..5)" bash -c "! kill -0 $GAME_PID 2>/dev/null && awk -v d=$d 'BEGIN { exit !(d >= 3 && d <= 5) }'"
check "stop_game: the fast-stop line" grep -q 'the game crashed (Game crashed with signal 11): a fast stop' "$LOG"

echo "$([ $fails = 0 ] && echo ALL OK || echo "$fails FAILED")"
exit $((fails > 0))
