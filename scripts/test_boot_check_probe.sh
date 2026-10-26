#!/usr/bin/env bash
# test_boot_check_probe.sh - boot_check.sh's `probe` command and its --cmds check, without the game: probe_send,
# cmds_bad and genv are cut out of boot_check.sh and run against a temporary RUN, with the REAL probe's command-file
# reader (mod/probe/somvr_probe.c: poll_cmd_file + flush_log every 100 ms, as its writer thread does) built into a small
# driver. The argument refusals are checked through boot_check.sh itself (each call fails its argument checks, i.e.
# before the run folder, the guards or anything else).
#   scripts/test_boot_check_probe.sh [BOOT_CHECK]   (default: the boot_check.sh next to this script)
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
BC=${1:-$S/boot_check.sh}
PSRC=$(cd "$S/../mod/probe" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/bc-probe-test.XXXXXX") || exit 1
DRV=""
trap '[ -n "$DRV" ] && kill "$DRV" 2>/dev/null; rm -rf -- "$T"' EXIT
fails=0
ok() { echo "ok   $*"; }
no() { echo "FAIL $*"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@"; then ok "$what"; else no "$what"; fi; }

# ---- the probe's reader
cat > "$T/drv.c" <<'EOF'
#define SOMVR_SELFTEST
#include "somvr_probe.c"
int main(int argc, char **argv)   /* CMDFILE LOG SECS: what writer_main does with the command file, nothing else */
{
	if (argc < 4)
		return 2;
	snprintf(g_cmd_path, sizeof g_cmd_path, "%s", argv[1]);
	snprintf(g_log_path, sizeof g_log_path, "%s", argv[2]);
	g_fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (g_fd < 0)
		return 1;
	double end = mono() + atof(argv[3]);
	while (mono() < end) {
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		poll_cmd_file();
		flush_log();
	}
	return 0;
}
EOF
gcc -O1 -no-pie -fno-pie -std=gnu11 -w -I "$PSRC" -o "$T/drv" "$T/drv.c" -lpthread -lm || { echo "FAIL: the driver does not build"; exit 1; }

# ---- the functions under test, from boot_check.sh
eval "$(sed -n '/^genv() {/,/^}/p; /^cmds_bad() {/,/^}/p; /^probe_send() {/,/^}/p' "$BC")"
for f in genv cmds_bad probe_send; do declare -F "$f" > /dev/null || { echo "FAIL: no $f() in $BC"; exit 1; }; done
RUN=$T/run; mkdir -p "$RUN"; LOG=$RUN/run.log; : > "$LOG"
log() { printf '%s\n' "$*" >> "$LOG"; }
gel() { echo 0; }
send() { local w; read -r -a w <<< "$1"; probe_send "${w[@]:1}"; }   # as the command loop does: read -a, then run_cmd

# ---- 1. probe preloaded, the reader running: back-to-back commands all arrive, once each, in order
PROBE=/x/somvr_probe.so; SPIKE1=/x/somvr_spike1.so; GEXTRA=()
PROBE_CMDF=$(genv SOMVR_PROBE_CMD) || PROBE_CMDF=$RUN/probe.cmd
PROBE_LOGF=$(genv SOMVR_PROBE_LOG) || PROBE_LOGF=$RUN/probe.log
"$T/drv" "$PROBE_CMDF" "$PROBE_LOGF" 60 & DRV=$!
cd "$T" || exit 1   # files here: a glob in the words would expand to them
long=$(printf 'mark %0600d' 0)
t0=$(date +%s%N)
send "probe state on"
send "probe orcs;mark   two   spaces"
send "probe orcs;mark   two   spaces"
send "probe hidehead *"
send "	probe move 0 1 3000;look 0.4 0 800  "
send "probe"
send "probe $long"
t1=$(date +%s%N)
exp=$(printf '%s\n' "state on" "orcs;mark two spaces" "orcs;mark two spaces" "hidehead *" "move 0 1 3000;look 0.4 0 800")
got=$(sed -n "s/^CMD queued seq=[0-9]* '\(.*\)' t=.*/\1/p" "$PROBE_LOGF")
check "the probe queued the 5 lines once each, in order (blanks collapsed, '*' literal)" [ "$got" = "$exp" ]
[ "$got" = "$exp" ] || { echo "  expected:"; printf '    %s\n' "$exp"; echo "  got:"; printf '    %s\n' "$got"; }
check "run.log: 5 'queued by the probe (seq 1..5)'" [ "$(grep -o 'queued by the probe (seq [0-9]*)' "$LOG" | grep -o '[0-9]*' | tr '\n' ' ')" = "1 2 3 4 5 " ]
check "run.log: the empty probe line is not sent" grep -q '^probe: no words$' "$LOG"
check "run.log: the 605-byte line is not sent" grep -q '^probe: NOT sent, 605 bytes' "$LOG"
check "no tmp file left in RUN" [ ! -e "$PROBE_CMDF.tmp" ]
check "probe.cmd holds the last line + newline" [ "$(cat "$PROBE_CMDF")" = "move 0 1 3000;look 0.4 0 800" ]
check "probe.cmd ends with a newline" [ "$(tail -c 1 "$PROBE_CMDF" | od -An -c | tr -d ' ')" = '\n' ]
echo "     (7 commands, 5 sent and acknowledged, in $(( (t1 - t0) / 1000000 )) ms)"

# ---- 2. the same without the wait (raw renames 1 ms apart): what the wait prevents
n0=$(grep -c '^CMD queued' "$PROBE_LOGF")
for w in raw-a raw-b raw-c; do printf '%s\n' "$w" > "$RUN/probe.cmd.tmp" && mv -f "$RUN/probe.cmd.tmp" "$RUN/probe.cmd"; sleep 0.001; done
sleep 0.5
echo "     info: 3 raw renames 1 ms apart, no wait: the probe queued $(( $(grep -c '^CMD queued' "$PROBE_LOGF") - n0 )) of them ($(grep -o "'raw-.'" "$PROBE_LOGF" | tr '\n' ' '))"

# ---- 3. the reader stopped (an inert probe): the command is written, the harness gives up after ~2 s
kill "$DRV" 2>/dev/null; wait "$DRV" 2>/dev/null; DRV=""
t0=$(date +%s%N); send "probe orcs"; t1=$(date +%s%N); ms=$(( (t1 - t0) / 1000000 ))
check "inert probe: '!!! no \"CMD queued\"' logged after ${ms} ms (1900-3500)" \
  bash -c '[ "$1" -ge 1900 ] && [ "$1" -le 3500 ] && grep -q "^probe: orcs -> .*!!! no \"CMD queued\"" "$2"' _ "$ms" "$LOG"
check "inert probe: the file is still written" [ "$(cat "$PROBE_CMDF")" = orcs ]

# ---- 4. no probe library: written, no wait, said so
PROBE=""
t0=$(date +%s%N); send "probe pieces"; t1=$(date +%s%N); ms=$(( (t1 - t0) / 1000000 ))
check "no probe library: 'nobody reads it', no wait (${ms} ms < 500)" \
  bash -c '[ "$1" -lt 500 ] && grep -q "^probe: pieces -> .*(no probe library in --preload: nobody reads it)$" "$2"' _ "$ms" "$LOG"

# ---- 5. --game-env SOMVR_PROBE_CMD / _LOG: the probe command writes and looks there
GEXTRA=(SOMVR_COMFORT=1 SOMVR_PROBE_CMD=/wrong SOMVR_PROBE_CMD="$T/alt.cmd" SOMVR_PROBE_LOG="$T/alt.log")
PROBE=/x/somvr_probe.so
PROBE_CMDF=$(genv SOMVR_PROBE_CMD) || PROBE_CMDF=$RUN/probe.cmd
PROBE_LOGF=$(genv SOMVR_PROBE_LOG) || PROBE_LOGF=$RUN/probe.log
check "genv: the last --game-env value wins" [ "$PROBE_CMDF" = "$T/alt.cmd" ]
if genv SOMVR_PROBE_OUT > /dev/null; then no "genv: status 1 for a key not given"; else ok "genv: status 1 for a key not given"; fi
"$T/drv" "$PROBE_CMDF" "$PROBE_LOGF" 30 & DRV=$!
send "probe nodes"
check "override: written to and acknowledged in the --game-env paths" grep -q "^probe: nodes -> $T/alt.cmd at game time 0 s: queued by the probe (seq 1)$" "$LOG"
kill "$DRV" 2>/dev/null; wait "$DRV" 2>/dev/null; DRV=""

# ---- 6. the --cmds check
printf '%s\n' '# a comment' '' 'probe orcs' '   probe   mark x' 'probe' "probe $long" 'shot x' 'bogus 1' 'probe hidehead *' 'quit' > "$T/t.cmd"
got=$(cmds_bad "$T/t.cmd")
exp=$(printf '%s\n' '  line 5 (probe: no words or over 511 bytes): probe' "  line 6 (probe: no words or over 511 bytes): probe $long" '  line 8: bogus 1')
check "cmds_bad: flags exactly lines 5 (no words), 6 (too long), 8 (unknown)" [ "$got" = "$exp" ]
[ "$got" = "$exp" ] || printf '    %s\n' "$got" | cut -c1-120
for c in probe-P1 probe-P2 spike1-world spike1-offset menu-to-benchmark; do
  check "cmds_bad: scripts/cmds/$c.cmd passes" [ -z "$(cmds_bad "$S/cmds/$c.cmd")" ]
done

# ---- 7. the real script refuses bad arguments before anything else (exit 2, the message)
refuse() {   # $1 = a part of the expected message, then the arguments
  local want=$1 out st; shift
  out=$("$BC" --dry-run "$@" 2>&1); st=$?   # --dry-run: should a check ever let it through, nothing is started
  check "boot_check $(printf '%q ' "$@" | cut -c1-70): exit 2, '$want'" bash -c '[ "$1" = 2 ] && [[ $2 == *"$3"* ]]' _ "$st" "$out" "$want"
  [ "$st" = 2 ] || printf '    %s\n' "$out" | head -3
}
printf 'probe\n' > "$T/empty-probe.cmd"
refuse "line 1 (probe: no words" --cmds "$T/empty-probe.cmd"
refuse "--preload needs an absolute path: rel.so" --preload rel.so
refuse "':' or a blank in /a:b.so" --preload /a:b.so
refuse "--preload: no readable file /nonexistent/somvr_probe.so" --preload /nonexistent/somvr_probe.so
refuse "--preload: $T/t.cmd is not an ELF64" --preload "$T/t.cmd"
refuse "SOMVR_PROBE_LOG needs an absolute path" --game-env SOMVR_PROBE_LOG=probe.log
refuse "SOMVR_PROBE_OUT needs an absolute path" --game-env SOMVR_PROBE_OUT=/
refuse "SOMVR_LOG is set by the harness" --game-env SOMVR_LOG=/x

echo "$([ $fails = 0 ] && echo PASS || echo "FAILED: $fails")"
[ $fails = 0 ]
