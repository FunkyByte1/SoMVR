#!/bin/bash
# live40-drive.sh - som-b40-live4 (docs/re-xr-pipeline.md 162.13, 162.14) in a headless Windows game: the B + Y chord
# PRESSED (no hold) makes a note; a lone B makes none; a note answered by live_say within 40 s - no "nobody answering"
# line; a note left without an answer - after 40 s the daemon says "Noted. No live answer right now; your notes are
# saved for later." once, daemon.json's coordinator_absent set, live_status shows it, a live_say clears it. Boot as
# live37-drive.sh says (WIN_MARK_VOICE=NOTE.wav: each mark's note). LV_WT = the worktree's name; LV_RUN=RUN.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${LV_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
T0=$(date +%s); R=${LV_RUN:-}
for i in $(seq 1 "${LV_FIND:-720}"); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -L -c %Y "$d")" -ge "$T0" ] && R=$(readlink -f "$d") && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/live40-steps.txt
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$S"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -aE "$2"; }
waitfor() { local n0=$1 re=$2 f=$3 s=${4:-20} i; for i in $(seq 1 $((s * 4))); do since "$n0" "$re" "$f" | grep -q . && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$S"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$S"; }
for i in $(seq 1 120); do grep -q '^SOMVR_LIVE_DIR=' "$R/env.txt" 2>/dev/null && break; sleep 1; done
LD=$(sed -n 's/^SOMVR_LIVE_DIR=//p' "$R/env.txt" | tail -1)
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 6
x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
for i in $(seq 1 120); do grep -q 'the acknowledgements made' "$LD/live.log" && break; sleep 0.5; done
grep -a 'SESSION config mark' "$L" | cut -c1-150 | tee -a "$S"
marks() { ls "$R/marks" 2>/dev/null | wc -l; }
chord() { x "btn L b 1" "btn R b 1"; sleep 0.15; x "btn L b 0" "btn R b 0"; }   # B and Y pressed together, let go at once
# 1. a lone B: no mark
m0=$(marks); x "btn R b 1"; sleep 0.3; x "btn R b 0"; sleep 2
[ "$(marks)" = "$m0" ] && pass "a lone B pressed: no mark" || fail "a lone B made a mark"
# 2. the chord pressed (0.15 s), answered within 40 s: no absent line
nl=$(lines); chord
waitfor "$nl" '^MARK #[0-9]+ \(the chord B \+ Y' "$L" 4 && pass "the chord pressed 0.15 s: $(since "$nl" '^MARK #[0-9]+ \(the chord' | head -1 | cut -c1-90)" \
  || fail "the chord pressed: no mark ($(since "$nl" 'MARK chord' | tail -1 | cut -c1-120))"
for i in $(seq 1 80); do [ "$(ls "$LD/inbox" | grep -c json)" -ge 1 ] && break; sleep 0.25; done
"$HERE/live_say.sh" --dir "$LD" "Got your note - looking at it now." | tee -a "$S"
sleep 45
grep -aq 'absent: .*playing' "$LD/live.log" && fail "the absent line after an answered note" || pass "a note answered within 40 s: no absent line in 45 s"
# 3. the chord again, no answer: after 40 s the absent line, once; daemon.json and live_status
nl=$(lines); chord
waitfor "$nl" '^MARK #[0-9]+ \(the chord B \+ Y' "$L" 4 && pass "the second chord press: a mark" || fail "the second chord: no mark"
for i in $(seq 1 200); do grep -aq 'absent: .*playing' "$LD/live.log" && break; sleep 0.5; done
al=$(grep -a 'absent: ' "$LD/live.log" | tail -1 | sed 's/^[^ ]* //')
ack=$(grep -a "ack .*playing" "$LD/live.log" | tail -1 | cut -c1-19)
case $al in *playing*) pass "nobody answering: $al (the note's ack at $ack)" ;; *) fail "no absent line: ${al:-none}" ;; esac
for i in $(seq 1 40); do [ -s "$LD/ack/absent.spoken" ] && break; sleep 0.25; done
grep -q ' spoken: on somvr_live_tts_' "$LD/ack/absent.spoken" 2>/dev/null && pass "said on the run's sink: $(cut -c21-130 "$LD/ack/absent.spoken")" || fail "not said ($(cat "$LD/ack/absent.spoken" 2>/dev/null))"
sleep 2.5
st=$("$HERE/live_status.sh" --dir "$LD" | grep coordinator)
case $st in *ABSENT*) pass "live_status: $(echo $st | cut -c1-140)" ;; *) fail "live_status: $st" ;; esac
"$HERE/live_say.sh" --dir "$LD" "Back now - I have your notes." > /dev/null; sleep 3
"$HERE/live_status.sh" --dir "$LD" | grep -q 'coordinator: answering' && pass "a live_say cleared it" || fail "still absent after a live_say"
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; grep -E 'PASS|FAIL' "$S" | cut -c1-220
