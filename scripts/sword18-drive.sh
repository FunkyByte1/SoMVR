#!/bin/bash
# sword18-drive.sh - build 18's sword in the fist and the bite (docs/re-xr-pipeline.md 66, handoff/HANDOFF-feel.md "build
# 18"): one headless boot at the practice meadow with build 18's env + the test hands (SOMVR_HANDS_TEST=1), the layer's
# test file; boot with SOMVR_XR_DUMP_MARKED=8 for the bite's pictures. Each picture is taken twice where a switch has a
# live word: build 17's way and build 18's (the fist: the layer's `handsize 1.2 0|3.4`; the hands with the weight and
# the bite: `hands weighthand unlock|lock`; the left hand's place: `hands twohold 8 24|10.5 13`).
# S18_STEPS = any of (in this order):
#   fist   the sword held still, one hand, three views (ahead, across, the edge up), build 17's fist / build 18's
#   two    both hands on the hilt, the left controller 23 cm behind the right grip (the session's mark 1): build 17's
#          fist and hold / build 18's
#   swing  a +-15 deg 4 Hz wiggle (the heavy weight's trail), dumps mid-wiggle: unlocked (build 17: the handle turns in
#          the fist) / locked
#   bite   the bite held long for the pictures (`fight bite hold 800 300 75`, a 1.2 s cut), the dumps 0.12 s and 0.5 s
#          after: unlocked / locked; then build 18's own numbers (90 / 110 / 35, drag) back
#   cuts   S18_N fast landed cuts (300 ms) on an infantry at build 18's bite: the BITE timeline, the haptics
# Dumps: RUN/s18-steps.txt. Quits at the end, at once if the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${S18_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${S18_STEPS:-fist two swing bite cuts}
N=${S18_N:-3}
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
[ -f "$R/s18-steps.override" ] && STEPS=$(cat "$R/s18-steps.override") && echo "steps (RUN/s18-steps.override): $STEPS"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
fastwait() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 20))); do
  [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.05; done; userin && bail "the user is in VR"; return 1; }
# d NAME: a stereo dump now (both eyes + the layer's quads), its frame noted
d() { T_S=0.1 t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/s18-steps.txt"; }
fist() { t "handsize 1.2 $1"; sleep 0.6; grep -E 'hands: the hand model at size' "$R/xr-layer.log" | tail -1 | grep -oE 'grip [0-9.]+ cm|the fist round[^)]*\)' | tr '\n' ' '; echo; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(awk -v y="$y" 'BEGIN { y += 0; printf "%d", (y + (y < 0 ? -15 : 15)) / 30 }')
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
RZ=90                                       # the edge leading (bite17-drive.sh)
CUT_MS=300
cut_swing() { w "hands test R 40 -8 35 0 -90 $RZ" 0.5; w "hands swing R -20 -18 62 0 90 $RZ $CUT_MS" "$(awk -v m="$CUT_MS" 'BEGIN { print (m < 700 ? 1.0 : m / 1000 + 0.3) }')"; w "hands swing R 40 -8 35 0 -90 $RZ 300" 1.0; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
spawn() { w "practice spawn $1" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5; }
hit_it() { local k h0; h0=$(cnt '^COMBAT hit tier'); for k in $(seq 1 "${2:-10}"); do
    [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ] && return 0
    face; w "fight health 125" 0.3; w "stick 0 1 0 0.30" 0.5; $1
  done; [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ]; }
last_hit() { grep -E '^COMBAT hit tier' "$L" | tail -1 | grep -oE 'tier=[a-z]+|speed=[0-9.]+|u=[0-9.]+' | tr '\n' ' '; }
# 0. the controls card away, the left hand forward, the sword drawn
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
drawn && echo "$(date +%T) the sword is in the hand" || echo "$(date +%T) FAIL no sword in the hand"
grep -E '^CONFIG hitfeel' "$L" | cut -c1-420
grep -E '^CONFIG body' "$L" | grep -oE 'twohand=[^)]*\)' | head -1
grep -E 'hands: the hand model:' "$R/xr-layer.log" | head -1 | cut -c1-500

if has fist; then
  echo "== the fist on the handle, one hand, still: build 17's fist (0) / build 18's (3.4 cm)"
  w "hands test L -25 -45 10 0 0 0"
  for v in "ahead 0 0 0" "across 0 -90 0" "edge 0 -90 90"; do
    set -- $v
    w "hands test R 12 -22 32 $2 $3 $4" 1.2
    fist 0; d "fist-$1-b17"
    fist 3.4; d "fist-$1-b18"
  done
fi

if has two; then
  echo "== both hands on the hilt, the left controller 23 cm behind the right grip (the blade across, pointing right)"
  w "hands test R 0 -25 35 0 -90 0" 0.8
  n=$(cnt '^TWOHAND t=.* grab'); w "hands test L -23 -25 35 0 -90 0" 0.6; t "btn L trigger 1"; sleep 1.2
  SX=-1
  if [ "$(cnt '^TWOHAND t=.* grab')" -le "$n" ]; then  # the blade the other way: the hilt to the right
    t "btn L trigger 0"; sleep 0.6; SX=1; w "hands test L 23 -25 35 0 -90 0" 0.6; t "btn L trigger 1"; sleep 1.2; fi
  [ "$(cnt '^TWOHAND t=.* grab')" -gt "$n" ] && echo "$(date +%T) PASS two hands: $(grep -E '^TWOHAND t=.* grab' "$L" | tail -1 | grep -oE 'the left on the hilt [0-9.]+ cm behind')" ||
    echo "$(date +%T) FAIL no TWOHAND grab: $(grep -E '^TWOHAND t=' "$L" | tail -1 | cut -c1-200)"
  w "hands twohold 8 24" 0.8; fist 0; d two-b17
  w "hands twohold 10.5 13" 0.8; fist 3.4; d two-b18
  w "hands test L $((SX * 11)) -25 35 0 -90 0" 0.8; d two-close-b18
  t "btn L trigger 0"; sleep 0.8
  w "hands test L -25 -45 10 0 0 0"
fi

if has swing; then
  echo "== the heavy weight's trail: a +-15 deg 4 Hz wiggle, dumps mid-wiggle (unlocked = build 17 / locked)"
  for mode in unlock lock; do
    w "hands weighthand $mode" 0.6; w "hands weighthand fixed" 0.4
    w "hands test R 12 -22 32 0 -90 0" 1.0
    w "hands wiggle R 15 4 3" 0.2; waitfor '^BODY test wiggle R: ' 4; sleep 0.9; d "wiggle-$mode-1"; sleep 0.37; d "wiggle-$mode-2"
    waitfor '^BODY test wiggle R end: ' 6
    grep -E '^BODY test wiggle R end: ' "$L" | tail -1 | cut -c1-240
  done
fi

if has bite || has cuts; then spawn infantry; w "$BEHIND" 0.5; fi
if has bite; then
  echo "== the bite held long for the pictures (800 ms / 300 / 75 deg, a 1.2 s cut): unlocked = build 17 / locked"
  w "fight bite hold 800 300 75" 0.5; CUT_MS=1200
  pic() { local d0 k; d0=$(grep -c 'dump layers frame [0-9]*: eye 0 pose' "$R/xr-layer.log"); w "fight bite dumps 1" 0.3
    for k in 1 2 3; do hit_it cut_swing 8 || break; echo "$(date +%T) PIC $1: $(last_hit)"
      grep -E '^BITE t=.* start' "$L" | tail -1 | cut -c1-120; last_hit | grep -q 'tier=heavy' && break; w "$BEHIND" 0.8; done; sleep 2
    echo "$1 $(grep -o 'dump layers frame [0-9]*: eye 0 pose' "$R/xr-layer.log" | tail -n +$((d0 + 1)) | grep -o 'frame [0-9]*' | cut -d' ' -f2 | tr '\n' ' ')" | tee -a "$R/s18-steps.txt"
    w "$BEHIND" 0.8; }
  w "hands weighthand unlock" 0.4; pic bite-unlocked
  w "hands weighthand lock" 0.4; pic bite-locked
  CUT_MS=300
fi
if has cuts; then
  echo "== build 18's bite (90 ms / 110 / 35 deg, drag, thud) on $N fast cuts (300 ms)"
  w "fight bite hold 90 110 35" 0.5; w "hands weighthand lock" 0.3
  for k in $(seq 1 "$N"); do
    b0=$(cnt '^BITE t=.* start')
    if hit_it cut_swing 10; then sleep 0.8; echo "$(date +%T) CUT $k: $(last_hit) | bites +$(( $(cnt '^BITE t=.* start') - b0 ))"
    else echo "$(date +%T) CUT $k: no hit landed"; spawn infantry; fi
    w "$BEHIND" 0.8
  done
fi
echo "== BITE lines"
grep -E '^BITE t=' "$L" | tail -24 | cut -c1-230
echo "== the bites' haptics (the layer's haptic lines within 0.3 s of each start)"
for ts in $(grep -E '^BITE t=[0-9.]+ start' "$L" | tail -6 | grep -oE 't=[0-9.]+' | cut -d= -f2); do
  echo "bite $ts:"; awk -v t0="$ts" '/xr: haptic [LR] /{ t = $1 + 0; if (t >= t0 - 0.02 && t <= t0 + 0.3) print "  " substr($0, 1, 120) }' "$R/xr-layer.log"
done
echo "== SWORDWT"
grep -E '^SWORDWT t=' "$L" | tail -6 | cut -c1-330
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
