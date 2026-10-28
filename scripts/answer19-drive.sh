#!/bin/bash
# answer19-drive.sh - build 19's two combat checks (docs/re-xr-pipeline.md 70.7-70.8, handoff/HANDOFF-combat.md "build
# 19: the attack from behind, the infantry's riposte"): one headless boot at the practice arrival, the test hands
# (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Prologue as crowd19-drive.sh's.
# Boot (sandbox off):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build19.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/...
#   this script at the same time.
# A19_STEPS = any of (in this order; default all):
#   inf     an infantry: `fight guard learn` (A19_LEARN_N strikes), then `fight guard path` (A19_INF_S s): each contact
#           block a parry (the opening, 1 s); `parry auto A19_AUTO_MS` (400: after the test guard's own return to its
#           rest, which cancelled a cut at 100 ms in run 014712): the mod's test hand answers each opening
#           with a fast swing out to the right and the edge cut across, no step (a watcher on the log came 1.2-1.6 s
#           late in run 012117, and a slow pose change into the orc only touched it)
#   base    the group (`practice group 4`), `front answer 0` (the hold off: the baseline), the blade behind the hip:
#           after each berserker blow that hits Talion (`CROWD landed`) the head turns to it and cuts it (the
#           provocation: out to the right, across and back); then the head turns 150 deg away (its answer from out of
#           view; every A19_AWAY_EVERY-th time, default 1; A19_ONESWEEP=1: one sweep, then away at once; the first
#           A19_DUMPS (0) turns: the layer dumped A19_DUMP_S after the turn - a warned answer's red edge) (A19_BASE_S)
#   hold    the same with `front answer 3000` (the hold on) (A19_HOLD_S)
# Env: A19_STEPS, A19_WT. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${A19_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${A19_STEPS:-inf base hold}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
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
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$L"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$L")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
DN0=0
spawn() { w "practice spawn $1" 2; DN0=$(cnt '^PRACTICE t=.* orc is down'); face; }
alive_or() { [ "$(cnt '^PRACTICE t=.* orc is down')" -gt "$DN0" ] && spawn "$1"; return 0; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every blow's path
RZ=90
EDGE_O="hands swing R 40 -8 35 0 -90 $RZ 120"   # out to the right fast (a slow pose change into an orc is a touch)
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# 0. the controls card away, the left hand down, the sword drawn, the blade behind the hip
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
w "fight guard frame head" 0.5
m a00-start 0.3
if has inf; then
  spawn infantry
  w "fight guard learn" 0.5
  n=0; for k in $(seq 1 $(( ${A19_LEARN_N:-6} * 2 ))); do [ $n -ge "${A19_LEARN_N:-6}" ] && break
    waitfor '^THREAT start .* impact=0\.[0-9]' 8 && n=$((n + 1)); face; alive_or infantry; w "holdtalion health 120" 0.2; done
  O0=$(cnt '^PARRY open .*by a parry'); RP0=$(cnt '^COMBAT hit tier=[a-z]+ \(the riposte'); MK=0
  w "fight guard path" 0.5
  w "parry auto ${A19_AUTO_MS:-400}" 0.5   # the counter-cut in the mod: out to the right fast, then the edge cut across
  for k in $(seq 1 $(( ${A19_INF_S:-60} / 3 ))); do face; sleep 2
    alive_or infantry
    [ $((k % 6)) = 0 ] && w "holdtalion health 120" 0.2
    if [ "$MK" = 0 ] && [ "$(cnt '^COMBAT hit tier=[a-z]+ \(the riposte')" -gt "$RP0" ]; then MK=1; x "dump" 0.5; m a01-inf-riposte 0; fi
  done
  w "parry auto 0" 0.5
  echo "PHASE inf: parries $(( $(cnt '^PARRY open .*by a parry') - O0 )), ripostes $(( $(cnt '^COMBAT hit tier=[a-z]+ \(the riposte') - RP0 ))"
  grep -E '^PARRY (parry|open)|^COMBAT (hit|glance) ' "$L" | tail -12 | cut -c1-260
  w "fight guard learn" 0.5; w "$BEHIND" 0.5
  w "practice remove" 3; z 12   # its removal is queued while it fights (run 014712: 10 s; its old grant's blows meanwhile)
fi
HY=0   # the test head's yaw (deg, the tracking space)
hy() { HY=$1; printf 'head 0 0 0 yaw %s s %s\n' "$1" "${2:-0.25}" > "$R/xrtest.txt"; }
# the provocation watcher: each berserker blow that hit Talion -> the head to it, the edge cut; every second one the
# head 150 deg away right after the cut (its answer from out of view); back to it 2.5 s later
provoke() { # $1 = seconds
  local k=0
  timeout "$1" tail -n0 -F "$L" 2>/dev/null | grep --line-buffered -E '^CROWD landed ' | while read -r l; do
    y=$(printf '%s' "$l" | grep -oE 'yaw=[+-]?[0-9]+' | head -1 | cut -d= -f2); y=${y#+}; [ -n "$y" ] || continue
    k=$((k + 1)); ny=$(( HY + y )); hy "$ny" 0.15; HY=$ny; sleep 0.25
    printf '%s' "$EDGE_O" > "$R/somvr.trigger"; sleep 0.14; printf '%s' "$EDGE" > "$R/somvr.trigger"; sleep 0.32
    [ "${A19_ONESWEEP:-0}" = 1 ] || { printf '%s' "$EDGE_B" > "$R/somvr.trigger"; sleep 0.32; } # and back across
    echo "$(date +%T) provoked $k (turned $y deg)"
    if [ "${A19_AWAY_EVERY:-1}" -gt 0 ] && [ $((k % ${A19_AWAY_EVERY:-1})) = 0 ]; then ay=$(( HY + 150 )); hy "$ay" 0.25; sleep ${A19_DUMP_S:-0.45}
      if [ "$(ls "$R"/a19-edge-dumped.* 2>/dev/null | wc -l)" -lt "${A19_DUMPS:-0}" ]; then # the first A19_DUMPS turns away:
        printf 'dump\n' > "$R/xrtest.txt"; touch "$R/a19-edge-dumped.$k"  # the layer dumped (a warned answer: its red edge)
        echo "$(date +%T) dump after provocation $k (the red edge if it was warned)"; fi
      sleep 2; hy "$HY" 0.4; sleep 0.5; fi
    printf '%s' "$BEHIND" > "$R/somvr.trigger"
  done
}
crowd() { # $1 = name, $2 = answer ms, $3 = seconds
  w "front answer $2" 0.5
  w "practice group 4" 3
  grep -qE '^PRACTICE t=.* group \((the trigger word|the menu)\): [34] of [34] orcs out' <(tail -400 "$L") || echo "WARN no group line"
  w "fight guard learn" 0.5
  local s0 a0 o0 u0 p0 h0 c0
  s0=$(cnt '^CROWD start '); a0=$(cnt '^CROWD start .*ITS ANSWER'); u0=$(cnt '^CROWD start .*NO GRANT'); o0=$(cnt '^CROWD .*OVERLAP')
  p0=$(cnt '^CROWD provoked '); h0=$(cnt '^CROWD landed '); c0=$(cnt '^THREAT answer ')
  provoke "$3" &
  local PV=$! j=0 mk=0
  while kill -0 $PV 2>/dev/null; do z 5; j=$((j + 1)); [ $((j % 4)) = 0 ] && w "holdtalion health 120" 0.2
    if [ "$mk" = 0 ] && [ "$(cnt '^THREAT answer .*red edge')" -gt "$c0" ]; then mk=1; x "dump" 0.3; m "a02-$1-answer-edge" 0; fi
  done
  wait $PV
  echo "PHASE $1 (answer $2 ms): berserker blows that hit $(( $(cnt '^CROWD landed ') - h0 )), provoked $(( $(cnt '^CROWD provoked ') - p0 )), starts $(( $(cnt '^CROWD start ') - s0 )), answers in the hold $(( $(cnt '^CROWD start .*ITS ANSWER') - a0 )), ungranted $(( $(cnt '^CROWD start .*NO GRANT') - u0 )), overlaps $(( $(cnt '^CROWD .*OVERLAP') - o0 )), answer cues $(( $(cnt '^THREAT answer ') - c0 ))"
  grep -E '^CROWD (start .*(NO GRANT|ITS ANSWER)|answer t|provoked)|^THREAT answer' "$L" | tail -10 | cut -c1-330
  hy 0 0.4; HY=0; sleep 0.6
  w "practice remove" 3
}
has base && crowd base 0 "${A19_BASE_S:-90}"
has hold && crowd hold 3000 "${A19_HOLD_S:-120}"
w "front status" 1
w "parry status" 0.5
echo "== the run's summary"
grep -E '^CONFIG crowd' "$L" | cut -c1-300
grep -E '^CROWD END|^PARRY END' "$L" | cut -c1-1500
w "endlogs" 2
echo "$(date +%T) done"; echo quit >> "$R/cmd"
