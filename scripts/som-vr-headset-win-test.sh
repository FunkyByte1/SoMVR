#!/bin/bash
# som-vr-headset-win-test.sh - the Windows launcher's end-to-end test (docs/re-winport.md 7.10): the launcher's own
# command line (SOMVR_HEADSET_TEST=1: Monado's null compositor standing in for WiVRn, the session's own null sink for
# the sound, a fresh test prefix so the first-run path runs - the prefix made, the save seeded from a copy of the Steam
# save), the unit somvr-headset, the PC window on a headless screen; in the game a bug mark (the trigger word
# `mark`) and the VR menu's Quit (the word `quitgame`) end it the way the player would.
# The game slot as logs/coord/winport/common.md: the guards, mkdir /tmp/somvr-boot.lock, the owner, released here
# (only if it names OWNER). Run it OUTSIDE the Bash sandbox, at most one at a time; about 6 minutes.
#   som-vr-headset-win-test.sh [OWNER] [HARD_SECONDS]   (defaults winport-launch, 480)
# SOMVR_TEST_HAVE_LOCK=1 (build 35): the caller holds slot 1 already as OWNER (game_slot.sh take OWNER solo): no mkdir
#   here and no release (the caller's). SOMVR_HEADSET_TEST_STOPALL=1 passes to the launcher: its solo request and the
#   stop of every slot's test run run in the test too (a test run on another slot is stopped first).
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
SCR=$(cd "$(dirname "$0")" && pwd)
OWNER=${1:-winport-launch}; HARD=${2:-480}
RT=/run/user/$(id -u); LOCK=/tmp/somvr-boot.lock
TPFX=$SOM/logs/win-launch/test-prefix
LOGF=$SOM/logs/som-vr-headset-win.log
say() { echo "[$(date +%T)] $*"; }
systemctl --user is-active -q somvr-headset && { say "STOP: the user's headset session runs"; exit 1; }
grep -q '"user": false' "$RT/vr-session/status.json" 2>/dev/null || { say "STOP: the user is in VR (status.json)"; exit 1; }
for mp in $(pgrep -x monado-service); do   # (build 35: only one on slot 1's runtime folder: other slots' have their own)
  me=$(tr '\0' '\n' < /proc/$mp/environ 2>/dev/null | sed -n 's/^XDG_RUNTIME_DIR=//p'); [ -n "$me" ] || me=$RT
  [ "${me%/}" = "$RT" ] && { say "STOP: a monado-service runs on $RT (pid $mp)"; exit 1; }
done
pgrep -f '[H]and of Fate 2\.' > /dev/null && { say "STOP: Hand of Fate 2 runs"; exit 1; }
journalctl -k -b --no-pager 2>/dev/null | grep -qi 'amdgpu.*\(GPU reset\|VRAM is lost\)' && { say "STOP: an amdgpu reset in the kernel log"; exit 1; }
if [ "${SOMVR_TEST_HAVE_LOCK:-0}" = 1 ]; then   # (the caller's slot: checked, never released here)
  case $(head -1 "$LOCK/owner" 2>/dev/null) in "$OWNER"|"$OWNER "*) say "the game slot is the caller's ($OWNER): kept" ;;
    *) say "STOP: SOMVR_TEST_HAVE_LOCK=1 but slot 1 is not $OWNER's ($(head -1 "$LOCK/owner" 2>/dev/null))"; exit 1 ;; esac
else
  mkdir "$LOCK" 2>/dev/null || { say "STOP: the game slot is taken ($(cat "$LOCK/owner" 2>/dev/null))"; exit 1; }
  echo "$OWNER" > "$LOCK/owner"
  release() { [ "$(cat "$LOCK/owner" 2>/dev/null)" = "$OWNER" ] && rm -rf "$LOCK" && say "the game slot released"; }
  trap release EXIT
  say "the game slot taken ($OWNER)"
fi
case $TPFX in /home/bara/Projects/flat2vr/ShadowOfMordor/logs/win-launch/*) rm -rf "$TPFX" ;; esac
L0=$(wc -l < "$LOGF" 2>/dev/null || echo 0)
SOMVR_HEADSET_TEST=1 SOMVR_HEADSET_TEST_HARD=$HARD "$SCR/som-vr-headset-win.sh" > "$SOM/logs/win-launch/last-test.out" 2>&1 &
LP=$!
RUN=""
for i in $(seq 1 120); do
  RUN=$(tail -n +"$((L0 + 1))" "$LOGF" 2>/dev/null | sed -n 's/.*session unit somvr-headset started (folder \(.*\))$/\1/p' | tail -1)
  [ -n "$RUN" ] && break; kill -0 $LP 2>/dev/null || break; sleep 1
done
[ -n "$RUN" ] || { say "!!! no session folder (the launcher: $(tail -3 "$SOM/logs/win-launch/last-test.out" | tr '\n' ' '))"; wait $LP; exit 1; }
say "session folder $RUN"
waitfor() {   # SECS FILE TEXT
  local end=$(( $(date +%s) + $1 ))
  while [ "$(date +%s)" -lt "$end" ]; do grep -a -q -F -- "$3" "$2" 2>/dev/null && return 0; kill -0 $LP 2>/dev/null || return 1; sleep 1; done
  return 1
}
if waitfor 240 "$RUN/somvr.log" "go to the practice area"; then
  say "in the world (the trip to the practice meadow): a bug mark in 20 s"
  sleep 20; printf 'mark launch-test' > "$RUN/somvr.trigger"
  waitfor 60 "$RUN/marks.log" "| voice:" && say "the mark is served: $(tail -1 "$RUN/marks.log")" || say "!!! no mark served within 60 s"
  sleep 30; printf 'quitgame' > "$RUN/somvr.trigger"; say "the word quitgame (the VR menu's Quit)"
else
  say "!!! no trip to the practice meadow within 240 s: the session runs to its limit"
fi
wait $LP; say "the launcher exit $?"
say "--- the launcher's log"; tail -n +"$((L0 + 1))" "$LOGF" | cut -c1-220
say "--- run.log (key lines)"
grep -a -E 'REFUSED|prefix|save|SAVE|sound|AUDIO|game started|waitlog|MARK|bug mark|QUIT|stopping|the end|exited|crash|DLL|render.cfg' "$RUN/run.log" | cut -c1-220
