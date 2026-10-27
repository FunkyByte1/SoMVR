#!/usr/bin/env bash
# test_som_vr_headset_gate.sh - scripts/som-vr-headset.sh's start gate (build 16, docs/re-xr-pipeline.md 53) without
# a session: start_gate, session_ending, unit_state/unit_running, note/tell/fail are cut out of the launcher and run with
# a fake `systemctl` (the unit's state from a file) and a fake notify-send. Never touches the real somvr-headset unit.
# Checks: no session = go on at once; a session playing or starting = refused ("already running", no wait); a session
# ending (boot_check's --ending-file, or the unit deactivating) = the notification, the old window's next-start note,
# a wait until the unit ends, then go on; one that does not end = refused after the wait; a dry run never waits.
#   scripts/test_som_vr_headset_gate.sh [LAUNCHER]   (default: the som-vr-headset.sh next to this script; about 15 s)
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
L=${1:-$S/som-vr-headset.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/headset-gate-test.XXXXXX") || exit 1
trap 'rm -rf -- "$T"' EXIT
fails=0
ok() { echo "ok   $*"; }
no() { echo "FAIL $*"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@"; then ok "$what"; else no "$what"; fi; }

FN=$(sed -n '/^note() {/p; /^tell() {/,/^}/p; /^fail() {/p; /^unit_state() {/p; /^unit_running() {/p;
  /^session_ending() {/,/^}/p; /^start_gate() {/,/^}/p' "$L")
for f in note tell fail unit_state unit_running session_ending start_gate; do
  grep -q "^$f() {" <<< "$FN" || { echo "FAIL: $f not found in $L"; exit 1; }
done
# one case: STATE (the unit's) [ENDING=1] [DRY=1] [FLIP=SECS:STATE]; prints the elapsed seconds and WENT_ON when the
# gate let the start go on
run_case() {
  local state=$1; shift
  rm -rf "$T/priv"; mkdir -p "$T/priv"; : > "$T/log"; : > "$T/notify"; echo "$state" > "$T/state"
  ( UNIT=somvr-headset PRIV=$T/priv LOGF=$T/log DRY=0 WAIT_END=4 ENDING=0 FLIP=""
    for kv in "$@"; do eval "$kv"; done
    systemctl() { [ "${1:-}" = --user ] && [ "${2:-}" = is-active ] && [ "${3:-}" = somvr-headset ] || return 9
      cat "$T/state"; [ "$(cat "$T/state")" = active ]; }
    notify-send() { echo "NOTIFY $*" >> "$T/notify"; }
    eval "$FN"
    [ "$ENDING" = 1 ] && echo "ending 1 /x" > "$PRIV/ending"
    [ -n "$FLIP" ] && ( sleep "${FLIP%%:*}"; echo "${FLIP#*:}" > "$T/state" ) &
    t0=$(date +%s.%N)
    start_gate 2> /dev/null
    awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN { printf "%.1f ", b - a }'
    echo WENT_ON )
}
secs_in() { awk -v d="${1%% *}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(d >= lo && d <= hi) }'; }

o=$(run_case inactive)
check "no session: go on at once ($o)" bash -c "[[ '$o' == *WENT_ON ]]" && secs_in "$o" 0 0.5
check "no session: no notification" bash -c "[ ! -s '$T/notify' ]"
o=$(run_case active)
check "a session playing: refused, no wait ('$o')" bash -c "[ -z '$o' ]"
check "a session playing: 'already running'" grep -q 'NOTIFY .*Shadow of Mordor VR is already running' "$T/notify"
o=$(run_case active ENDING=1 FLIP=2:inactive)
check "a session ending (its ending file): went on after its end ($o)" bash -c "[[ '$o' == *WENT_ON ]]" && secs_in "$o" 1.8 3.5
check "a session ending: the notification" grep -q 'NOTIFY .*starting in a moment The last session is closing - the new one starts by itself in a moment' "$T/notify"
check "a session ending: the old window's note" test -s "$T/priv/next-start"
check "a session ending: the log line" grep -q 'the last session closed after [0-9]* s: starting' "$T/log"
o=$(run_case deactivating FLIP=2:inactive)
check "the unit deactivating (the Stop): went on after its end ($o)" bash -c "[[ '$o' == *WENT_ON ]]" && secs_in "$o" 1.8 3.5
o=$(run_case active ENDING=1)
check "an ending session that never ends: refused after the wait ('$o')" bash -c "[ -z '$o' ]"
check "never ends: the message" grep -q 'did not start The last session did not close within 5 minutes' "$T/notify"
o=$(run_case active ENDING=1 DRY=1)
check "a dry run during an ending session: no wait, no start ('$o')" bash -c "[ -z '$o' ]"
check "a dry run: its note" grep -q 'dry run: the last session is closing: a real start would wait for it, then start' "$T/log"
check "a dry run: no next-start note" test ! -e "$T/priv/next-start"
o=$(run_case activating)
check "a session starting (activating): refused ('$o')" bash -c "[ -z '$o' ]"

echo "$([ $fails = 0 ] && echo ALL OK || echo "$fails FAILED")"
exit $((fails > 0))
