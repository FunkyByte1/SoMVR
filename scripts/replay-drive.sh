#!/bin/bash
# replay-drive.sh - b35 (docs/re-xr-pipeline.md 160.3): a recorded moment of a session (SOMVR_INPUT_REC's
# <log>.inrec, or a bug mark's own marks/<id>/input.inrec) replayed into a headless game - the player's head, both
# controllers and every control frame by frame in real time (SOMVR_INPUT_REPLAY: the game's own input path, as the
# drivers' `hands test` words), from REPLAY_BEFORE s before the mark to REPLAY_AFTER s after it.
#   replay-drive.sh SESSION_RUN MARK_ID|N|T0-T1
# with a boot of scripts/headless_vr.sh [--win] MOD and SOMVR_EXTRA holding the env file's switches (SOMVR_INPUT_REC=on:
# the replay is recorded too, for the compare) + SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt
# SOMVR_INPUT_REPLAY=@RUN@/replay.inrec. The setup before the replay, from the recording's 4 Hz state at T0: Talion
# taken to his root there (`practice go X Y Z`: the practice trip), each live orc of the nearest 6 put where it stood
# (`practice put X Y Z infantry`: facing Talion; `front keep 1` keeps them all), the sword drawn if it was;
# REPLAY_SETUP=FILE instead sources FILE's `setup` function (the same words as the source boot's own setup: a scripted
# test's exact start). Then `replay start`; at each of the source's marks inside the window a `mark` here too; at the
# end the compare (scripts/inrec.py compare: the logic's poses against the source's through the remap, the hits on
# both sides) and the pictures side by side (RUN/replay/mark-*.jpg: the session's mark picture | the replay's).
# What matches: the player's motion (head, hands, controls) to the frame. What cannot: the orcs (their AI is not
# deterministic; the practice spawns are infantry wherever they stood), the world's state, the game's own timing.
# Env: REPLAY_RUN=RUN (drive a running boot), REPLAY_BEFORE (15), REPLAY_AFTER (3), REPLAY_SETUP, REPLAY_NO_ORCS=1,
# REPLAY_WT. Quits the game at the end; at once when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${REPLAY_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
SRC=${1:?replay-drive.sh SESSION_RUN MARK_ID|N|T0-T1}
WHAT=${2:?replay-drive.sh SESSION_RUN MARK_ID|N|T0-T1}
BEFORE=${REPLAY_BEFORE:-15}; AFTER=${REPLAY_AFTER:-3}
PY="python3 -I $HERE/inrec.py"
# the source recording and the window
REC=$SRC
[ -d "$SRC" ] && REC=$SRC/somvr.log.inrec
MK=""
case $WHAT in
  *-*) T0=${WHAT%-*}; T1=${WHAT#*-} ;;
  *)
    if [ -d "$SRC/marks" ]; then MK=$(ls -d "$SRC"/marks/*_"$WHAT" "$SRC/marks/$WHAT" 2>/dev/null | head -1); fi
    TM=""
    [ -n "$MK" ] && TM=$(sed -n 2p "$MK/state.txt" 2>/dev/null | grep -oE 't=[0-9.]+' | cut -d= -f2)
    [ -z "$TM" ] && TM=$($PY marks "$REC" 2>/dev/null | awk -v n="$WHAT" '$2 == "#" n || $2 == "#"n { sub("t=", "", $3); print $3 }' | head -1)
    [ -n "$TM" ] || { echo "no mark $WHAT in $SRC"; exit 1; }
    [ -f "$REC" ] || { [ -n "$MK" ] && [ -f "$MK/input.inrec" ] && REC=$MK/input.inrec; }
    T0=$(awk -v t="$TM" -v b="$BEFORE" 'BEGIN { printf "%.3f", t - b }'); T1=$(awk -v t="$TM" -v a="$AFTER" 'BEGIN { printf "%.3f", t + a }') ;;
esac
[ -f "$REC" ] || { echo "no recording $REC (SOMVR_INPUT_REC=on in that session?)"; exit 1; }
echo "source $REC window t=$T0..$T1${MK:+ (mark $(basename "$MK"))}"
# the run
T00=$(date +%s)
R=${REPLAY_RUN:-}
for i in $(seq 1 $(( ${REPLAY_RUN_WAIT:-3600} / 5 ))); do   # (a boot may wait for a game slot first)
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T00" ] && R=$d && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
last() { grep -E "$1" "$L" | tail -"${2:-1}" | cut -c1-"${3:-300}"; }
grep -q "SOMVR_INPUT_REPLAY" "$R/launch-cmd.txt" || bail "the boot has no SOMVR_INPUT_REPLAY=@RUN@/replay.inrec"
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 6
$PY cut "$REC" "$T0" "$T1" -o "$R/replay.inrec" || bail "no frames in the window"
eval "$($PY state "$REC" "$T0")"
echo "the source at t=$T0: Talion $ROOT yaw $YAW, health $HEALTH, flags $FLAGS, $NORCS orcs alive near"
# the controls card away (the first right squeeze), the hands low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
if [ -n "${REPLAY_SETUP:-}" ]; then
  REP_SOURCE_ONLY=1 . "$REPLAY_SETUP"
  setup
else
  w "practice go $ROOT" 2; waitfor "^PRACTICE t=.* arrived" 20 || echo "$(date +%T) no arrival at $ROOT"
  sleep 2
  if [ -z "${REPLAY_NO_ORCS:-}" ] && [ "${NORCS:-0}" -gt 0 ]; then
    w "front keep 1" 0.4
    for k in $(seq 0 $((NORCS - 1))); do eval "O=\$ORC_$k"; set -- $O
      w "practice put $1 $2 $3 infantry" 2.5; last '^PRACTICE t=.* spawn' 1 220; done
  fi
  if [ $((FLAGS & 2)) -ne 0 ]; then   # the sword was in the hand: drawn from its slot by the test hand first
    w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6; w "hands squeeze R 0" 0.5
  fi
fi
w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "hands trigger L -1" 0.2; w "hands trigger R -1" 0.2
R0=$(cnt '^REPLAY t=.* start')
w "replay start" 0.3
waitfor '^REPLAY t=.* start' 10 || bail "the replay did not start: $(grep -E 'TRIGGER .*replay' "$L" | tail -1)"
last '^REPLAY t=.* start' 1 400
RT0=$(last '^REPLAY t=.* start' | grep -oE '^REPLAY t=[0-9.]+' | cut -d= -f2)
# the source's marks inside the window: a mark here at the same moment of the replay
$PY marks "$REC" | awk -v a="$T0" -v b="$T1" '{ t = $3; sub("t=", "", t); if (t + 0 >= a + 0 && t + 0 <= b + 0) print $2, t, $4 }' > "$R/replay-marks.txt"
START=$(date +%s.%N)
while read -r num tm name; do
  at=$(awk -v t="$tm" -v a="$T0" 'BEGIN { printf "%.3f", t - a }')
  while :; do now=$(awk -v s="$START" -v n="$(date +%s.%N)" 'BEGIN { printf "%.3f", n - s }')
    awk -v n="$now" -v a="$at" 'BEGIN { exit !(n + 0 >= a - 0.1) }' && break; userin && bail "the user is in VR"; sleep 0.05; done
  w "mark replay-of-$name" 0.2
  echo "$name" >> "$R/replay-marked.txt"
done < "$R/replay-marks.txt"
DUR=$(awk -v a="$T0" -v b="$T1" 'BEGIN { printf "%d", b - a + 3 }')
waitfor '^REPLAY t=.* end' "$DUR" || { w "replay stop" 0.5; waitfor '^REPLAY t=.* end' 5; }
last '^REPLAY t=.* end' 1 400
sleep 2
echo quit >> "$R/cmd"
# after the game: the compare and the pictures
for i in $(seq 1 60); do [ -f "$R/game.exit" ] || [ -f "$R/cmd.done" ] && break; sleep 3; done
sleep 5
echo "== compare"
$PY compare "$R/replay.inrec" "$L.inrec" -o "$R/replay-compare.tsv" 2>&1 | tee "$R/replay-compare.txt"
echo "== the hits and grabs in the source's window and in the replay"
mkdir -p "$R/replay"
if [ -d "$SRC/marks" ]; then
  i=0
  for m in $(ls -d "$R"/marks/*replay-of* "$R"/marks/* 2>/dev/null | sort -u); do
    [ -f "$m/want" ] || continue
    nm=$(sed -n 1p "$m/want" | sed 's/.*replay-of-//')
    [ -n "$nm" ] && [ -d "$SRC/marks/$nm" ] || continue
    a=$(ls "$SRC/marks/$nm"/seen.jpg "$SRC/marks/$nm"/eyes.jpg 2>/dev/null | head -1)
    for k in $(seq 1 30); do [ -f "$m/seen.jpg" ] && break; sleep 3; done   # (boot_check's mark_compose.sh after the game)
    b=$(ls "$m"/seen.jpg "$m"/eyes.jpg 2>/dev/null | head -1)
    [ -n "$a" ] && [ -n "$b" ] && python3 -I - "$a" "$b" "$R/replay/mark-$nm.jpg" <<'EOF' && echo "picture $R/replay/mark-$nm.jpg"
import sys
from PIL import Image
a, b = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB')
h = min(a.height, b.height)
a, b = a.resize((a.width * h // a.height, h)), b.resize((b.width * h // b.height, h))
o = Image.new('RGB', (a.width + b.width + 8, h), (255, 255, 255))
o.paste(a, (0, 0)), o.paste(b, (a.width + 8, 0))
o.save(sys.argv[3], quality=88)
EOF
  done
fi
echo "done $(date +%T)"
