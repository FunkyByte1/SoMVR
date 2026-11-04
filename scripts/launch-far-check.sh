#!/bin/bash
# launch-far-check.sh - the standing regression check of the practice trip from far away, through the user's own
# launcher (som-b37-practrip, 2026-11-04; docs/re-xr-pipeline.md 27.10). On 11-04 the launcher's tests started 461 m
# from the practice spot (the Steam save after Deck play), the trip there landed before the world's collision had
# streamed in, and Talion fell through the world (win-session-20261104-153611 / -153817 / -160647); the headless boots
# never saw it (their pinned save starts 38 m away).
#   launch-far-check.sh [BUILD] [SAVE]
#     BUILD  a frozen build logs/BUILD (som-win-freeze.sh; default: the launcher's WIN_BUILD)
#     SAVE   pin|far|steam|/FILE (the launcher's SOMVR_HEADSET_TEST_SAVE; default far = logs/test-save/far-461m)
# What it does: SOMVR_HEADSET_TEST=1 som-vr-headset-win.sh (the scripts next to this one) on its own test prefix
# (LF_PREFIX, default logs/win-launch/far-check-prefix; its save replaced by SAVE at each start), waits for the trip's
# outcome (the automatic trip SOMVR_PRACTICE=auto makes), watches 60 s more, quits the player's way (the word quitgame
# = the VR menu's Quit), then judges somvr.log:
#   PASS = no WALK ground fall and no death in the whole run, and either REACHED (the trip there arrived on the meadow's
#   ground, the root 1700..2050; an orc out by the words "practice spawn"; Talion within 100 m of the spot and over
#   1500 at the end) or SAFE-HOME (the spot had no ground - a far Teleport never streams the world in - and the trip
#   went back: "back where he was, in the black", arrived back, Talion within 5 m of where he started at the end).
#   The verdict names which. build-win2..win4 FAIL it from the far save (Talion fell through the world).
# The caller holds the game slot: the solo (game_slot.sh take NAME solo) with slot 1 owned by LF_OWNER (default: any
# owner - the check refuses only when slot 1 is free); the VR gate as som-vr-headset-win-test.sh. About 6 minutes.
# Exit 0 PASS, 1 FAIL, 2 not run. Run it OUTSIDE the Bash sandbox. The raw .ppm/.pgm dumps of the run are deleted after.
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
SCR=$(cd "$(dirname "$0")" && pwd)
BUILD=${1:-}; SAVE=${2:-far}
RT=/run/user/$(id -u); LOCK=/tmp/somvr-boot.lock
LOGF=$SOM/logs/som-vr-headset-win.log
PFX=${LF_PREFIX:-$SOM/logs/win-launch/far-check-prefix}
SPOT_X=32500; SPOT_Z=-24800
say() { echo "[$(date +%T)] $*"; }
systemctl --user is-active -q somvr-headset && { say "NOT RUN: the headset session somvr-headset runs"; exit 2; }
grep -q '"user": false' "$RT/vr-session/status.json" 2>/dev/null || { say "NOT RUN: the user is in VR (status.json)"; exit 2; }
for mp in $(pgrep -x monado-service); do
  me=$(tr '\0' '\n' < /proc/$mp/environ 2>/dev/null | sed -n 's/^XDG_RUNTIME_DIR=//p'); [ -n "$me" ] || me=$RT
  [ "${me%/}" = "$RT" ] && { say "NOT RUN: a monado-service runs on $RT (pid $mp)"; exit 2; }
done
pgrep -f '[H]and of Fate 2\.' > /dev/null && { say "NOT RUN: Hand of Fate 2 runs"; exit 2; }
journalctl -k -b --no-pager 2>/dev/null | grep -qi 'amdgpu.*\(GPU reset\|VRAM is lost\)' && { say "NOT RUN: an amdgpu reset in the kernel log"; exit 2; }
own=$(head -1 "$LOCK/owner" 2>/dev/null)
[ -n "$own" ] || { say "NOT RUN: slot 1 is not held (take the solo first: game_slot.sh take NAME solo)"; exit 2; }
[ -z "${LF_OWNER:-}" ] || case $own in "$LF_OWNER"|"$LF_OWNER "*) ;; *) say "NOT RUN: slot 1 is $own's, not $LF_OWNER's"; exit 2 ;; esac
case $PFX in "$SOM"/logs/win-launch/*) ;; *) say "NOT RUN: LF_PREFIX must be under logs/win-launch/"; exit 2 ;; esac
L0=$(wc -l < "$LOGF" 2>/dev/null || echo 0)
say "the launcher's test: build ${BUILD:-its WIN_BUILD}, save $SAVE, prefix $PFX"
env SOMVR_HEADSET_TEST=1 SOMVR_HEADSET_TEST_HARD=${LF_HARD:-480} SOMVR_HEADSET_TEST_PREFIX="$PFX" \
  SOMVR_HEADSET_TEST_SAVE="$SAVE" ${BUILD:+SOMVR_HEADSET_TEST_BUILD=$BUILD} \
  "$SCR/som-vr-headset-win.sh" > "$SOM/logs/win-launch/last-far-check.out" 2>&1 &
LP=$!
RUN=""
for i in $(seq 1 120); do
  RUN=$(tail -n +"$((L0 + 1))" "$LOGF" 2>/dev/null | sed -n 's/.*session unit somvr-headset started (folder \(.*\))$/\1/p' | tail -1)
  [ -n "$RUN" ] && break; kill -0 $LP 2>/dev/null || break; sleep 1
done
[ -n "$RUN" ] || { say "NOT RUN: no session folder ($(tail -3 "$SOM/logs/win-launch/last-far-check.out" | tr '\n' ' '))"; wait $LP; exit 2; }
say "session folder $RUN"
L=$RUN/somvr.log
waitfor() {   # SECS ERE: 0 = a line of somvr.log matches
  local end=$(( $(date +%s) + $1 ))
  while [ "$(date +%s)" -lt "$end" ]; do grep -a -q -E -- "$2" "$L" 2>/dev/null && return 0; kill -0 $LP 2>/dev/null || return 1; sleep 1; done
  return 1
}
ROOT=""
if waitfor 300 'PRACTICE t=[0-9.]+ go to the practice area'; then
  say "the trip starts: $(grep -a -m1 -E 'go to the practice area' "$L" | cut -c1-120)"
  waitfor 75 'arrived at .*in the practice area|did not arrive|the ground never came' || say "no outcome within 75 s"
  grep -a -q -E 'back where he was, in the black' "$L" && { waitfor 20 'arrived at .*back where Talion was' || say "no arrival back within 20 s"; }
  if grep -a -q -E 'arrived at .*in the practice area' "$L"; then   # the meadow usable: an orc by the trigger word
    sleep 3; printf 'practice spawn' > "$RUN/somvr.trigger"; say "the words 'practice spawn'"
    waitfor 20 'spawn .*(ok: one orc out|FAILED|refused)' || say "no spawn line within 20 s"
  fi
  sleep 60
  ROOT=$(grep -a -E '^ANCHOR frame=[0-9]+ root ' "$L" | tail -1 | sed -n 's/^ANCHOR frame=[0-9]* root \([-0-9.]*,[-0-9.]*,[-0-9.]*\).*/\1/p')
  printf 'quitgame' > "$RUN/somvr.trigger"; say "the word quitgame (the VR menu's Quit); Talion's root then: $ROOT"
else
  say "no trip within 300 s: the session runs to its limit"
fi
wait $LP; say "the launcher exit $?"
# the verdict
TA=$(grep -a -m1 -E 'PRACTICE t=[0-9.]+ arrived at .*in the practice area' "$L" | sed -n 's/^PRACTICE t=\([0-9.]*\) arrived at (\([-0-9.]*\) \([-0-9.]*\) \([-0-9.]*\)).*/\1 \3/p')
say "--- the trip"
grep -a -E 'PRACTICE t=.*(go to the practice area|no ground under Talion|the ground came|arrived at|did not arrive|light:|weather|the blink there ended|spawn|orc)|^WARN|WALK ground|Talion is dead' "$L" |
  grep -a -v -E '^PRACTICE site' | head -40 | cut -c1-230
grep -a -E '^PRACTICE END|^CLIMB END build 36 walk' "$L" | cut -c1-400
grep -a -E 'save|SAVE' "$RUN/run.log" | head -6 | cut -c1-200
V=PASS; WHY=""; HOW=REACHED
SAFE=yes; grep -a -q -E '^WALK ground t=[0-9.]+ no ground under him|Talion is dead' "$L" && { SAFE=no; V=FAIL; WHY="$WHY; a fall or a death"; }
if [ -n "$TA" ]; then
  t=${TA% *}; y=${TA#* }
  awk -v y="$y" 'BEGIN { exit !(y >= 1700 && y <= 2050) }' || { V=FAIL; WHY="$WHY; the arrival not on the meadow's ground (root y $y)"; }
  grep -a -q -E 'PRACTICE t=[0-9.]+ spawn .*ok: one orc out' "$L" || { V=FAIL; WHY="$WHY; no orc spawned on the meadow"; }
  if [ -n "$ROOT" ]; then
    awk -v r="$ROOT" -v sx=$SPOT_X -v sz=$SPOT_Z 'BEGIN { split(r, p, ","); d = sqrt((p[1]-sx)^2 + (p[3]-sz)^2); exit !(d <= 10000 && p[2] > 1500) }' ||
      { V=FAIL; WHY="$WHY; Talion not on the meadow at the end ($ROOT)"; }
  else V=FAIL; WHY="$WHY; no root read before the quit"; fi
elif grep -a -q -E 'back where he was, in the black' "$L"; then
  HOW=SAFE-HOME
  START=$(grep -a -m1 -E 'PRACTICE t=[0-9.]+ back to where Talion was' "$L" | sed -n 's/.*the blink to (\([-0-9.]*\) \([-0-9.]*\) \([-0-9.]*\)).*/\1,\2,\3/p')
  grep -a -q -E 'PRACTICE t=[0-9.]+ arrived at .*back where Talion was' "$L" || { V=FAIL; WHY="$WHY; the trip back did not arrive"; }
  if [ -n "$ROOT" ] && [ -n "$START" ]; then
    awk -v r="$ROOT" -v s="$START" 'BEGIN { split(r, p, ","); split(s, q, ","); exit !(sqrt((p[1]-q[1])^2 + (p[3]-q[3])^2) <= 500 && p[2] > q[2] - 300) }' ||
      { V=FAIL; WHY="$WHY; Talion not back where he started at the end ($ROOT, the start $START)"; }
  else V=FAIL; WHY="$WHY; no root or start read"; fi
else
  HOW=-; V=FAIL; WHY="$WHY; neither an arrival in the practice area nor the trip back home"
fi
grep -a -q -E 'did not arrive' "$L" && ! grep -a -q -E 'the ground never came' "$L" && { V=FAIL; WHY="$WHY; a trip did not arrive (ground there)"; }
find "$RUN" -maxdepth 3 -type f \( -name '*.ppm' -o -name '*.pgm' \) -delete 2>/dev/null
say "VERDICT $V $HOW${WHY:+ (${WHY#; })}; no fall or death in the whole run: $SAFE; WARN lines $(grep -a -c '^WARN' "$L"): $RUN (build ${BUILD:-WIN_BUILD}, save $SAVE)"
[ $V = PASS ]
