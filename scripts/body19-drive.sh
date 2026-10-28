#!/bin/bash
# body19-drive.sh - build 19b's orc bodies out of the player's face (docs/re-xr-pipeline.md 74, handoff/HANDOFF-combat.md
# "build 19b: the bodies out of the face"): one headless boot at the practice arrival, the test hands
# (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Needs SOMVR_FIGHT_BODY_CM set (0 = measured only)
# in the boot's env; the phases switch the hold with the word `fight body CM`. Prologue as answer19-drive.sh's.
# Boot (sandbox off):
#   HEADLESS_HOLD=1200 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build19.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/...
#   this script at the same time.
# B19_KINDS = the orcs, each in turn (default: berserker shield infantry); for each, B19_MODES (default "0 55": the hold
#   off, then at 55 cm): `fight guard learn` (the blade behind the hip: every blow lands) B19_LAND_S s, then
#   `fight guard path` (the test guard onto each learnt path: blocks) B19_BLOCK_S s; the berserker also B19_PROV_S s of
#   edge cuts every ~4 s (its paired headbutt after our cut lands); each phase's line: strikes, hits, blocks, the bodies
# B19_CROWD = 1: then the group (`practice group 4`) for B19_CROWD_S s per mode, the blade behind the hip
# Marks: the first B19_MARKS moves per mode whose body comes within B19_MARK_OFF cm (off; 45) / B19_MARK_ON cm (on; the
# mode's cm + 7), each at its nearest.
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B19_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
KINDS=${B19_KINDS:-berserker shield infantry}
MODES=${B19_MODES:-0 55}
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every blow's path
RZ=90
EDGE_O="hands swing R 40 -8 35 0 -90 $RZ 120"
EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
DN0=0; ORC=""
spawn() { ORC=$1; w "practice spawn $1" 2; DN0=$(cnt '^PRACTICE t=.* orc is down'); face; }
alive_or() { [ "$(cnt '^PRACTICE t=.* orc is down')" -gt "$DN0" ] && spawn "$ORC"; return 0; }
mark0() { S0=$(cnt '^THREAT start .* impact=[0-9]'); H0=$(cnt '^DEFENCE hit'); C0=$(cnt '^DEFENCE contact'); B0=$(cnt '^TGUARD .*the blades met: blocked'); U0=$(cnt '^TGUARD .*not blocked'); NB0=$(cnt '^FIGHT body near'); SY0=$(cnt '^HOLD talion start'); }
phase() { local nl; nl=$(grep -E '^FIGHT body near' "$L" | tail -n +$((NB0 + 1)))
  echo "$(date +%T) PHASE $1: strikes $(( $(cnt '^THREAT start .* impact=[0-9]') - S0 )), hits $(( $(cnt '^DEFENCE hit') - H0 )), contacts $(( $(cnt '^DEFENCE contact') - C0 )), guards blocked $(( $(cnt '^TGUARD .*the blades met: blocked') - B0 )) not $(( $(cnt '^TGUARD .*not blocked') - U0 )), syncs on Talion $(( $(cnt '^HOLD talion start') - SY0 )); body lines (under 70 cm) $(printf '%s\n' "$nl" | grep -oE ': its [a-z ]+ [0-9]+ cm from the eyes' | awk '{v=$(NF-4); n++; if (v<50) u++; if (min==""||v+0<min+0) min=v} END {printf "%d, under 50 cm %d, nearest %s cm", n, u, min}')"
  printf '%s\n' "$nl" | grep -oE "move '[^']+'( \(a sync\))?: its [a-z ]+ [0-9]+ cm" | sed -E 's/ [0-9]+ cm$//' | sort | uniq -c | sort -rn | head -8; }
fight_for() { local k; for k in $(seq 1 $(( $1 / 3 ))); do face; z 2; alive_or; [ $((k % 5)) = 0 ] && w "holdtalion health 120" 0.2; done; }
provoke_for() { local k; for k in $(seq 1 $(( $1 / 4 ))); do face; alive_or
    w "$EDGE_O" 0.14; w "$EDGE" 0.32; w "$EDGE_B" 0.32; w "$BEHIND" 0.3; z 2
    [ $((k % 3)) = 0 ] && w "holdtalion health 120" 0.2; done; }
# 0. the controls card away, the left hand down, the sword drawn, the blade behind the hip
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
w "fight guard frame head" 0.5
w "fight body status" 0.3
m b00-start 0.3
for K in $KINDS; do
  spawn "$K"
  for MD in $MODES; do
    w "fight body $MD" 0.3; w "fight guard frame head" 0.3   # (the learnt paths forgotten: each mode learns its own)
    if [ "$MD" = 0 ]; then w "fight body marks ${B19_MARKS:-2} ${B19_MARK_OFF:-45}" 0.3; else w "fight body marks ${B19_MARKS:-2} ${B19_MARK_ON:-$((MD + 7))}" 0.3; fi
    w "fight guard learn" 0.3; w "$BEHIND" 0.3; mark0
    fight_for "${B19_LAND_S:-30}"; phase "$K body=$MD land (the blade behind the hip)"
    w "fight guard path" 0.3; mark0
    fight_for "${B19_BLOCK_S:-30}"; phase "$K body=$MD block (the test guard on the learnt paths)"
    w "fight guard learn" 0.3; w "$BEHIND" 0.3
    if [ "$K" = berserker ]; then mark0; provoke_for "${B19_PROV_S:-36}"; phase "$K body=$MD provoked (edge cuts: its headbutt)"; fi
  done
  w "practice remove" 3; z 8
done
if [ "${B19_CROWD:-1}" = 1 ]; then
  for MD in $MODES; do
    w "fight body $MD" 0.3
    w "practice group 4" 3; ORC=infantry; DN0=$(cnt '^PRACTICE t=.* orc is down')
    w "fight guard learn" 0.3; w "$BEHIND" 0.3; mark0
    for k in $(seq 1 $(( ${B19_CROWD_S:-60} / 3 ))); do face; z 2; [ $((k % 5)) = 0 ] && w "holdtalion health 120" 0.2; done
    phase "crowd body=$MD (the blade behind the hip)"
    w "practice remove" 3; z 10
  done
fi
w "fight body status" 0.5
echo "== the run's summary"
grep -E '^CONFIG fight body' "$L" | cut -c1-300
w "endlogs" 2
grep -E '^FIGHT END (body|space)' "$L" | cut -c1-400
echo "$(date +%T) done"; echo quit >> "$R/cmd"
