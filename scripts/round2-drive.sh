#!/bin/bash
# round2-drive.sh - the combat helper's round 2 boot (build 8 candidates; from build7-drive.sh): the arrival card put
# away by a right grip squeeze, the sword drawn, then COMBAT_PLAN (default r):
#   r: the infantry: the blade rested in it for 3 s (one glancing tick per touch), the guard until 3 parries / blocks
#      (marks and stereo dumps at each DEFENCE clank: SOMVR_DEFENCE_CLANK), lunges until it is down (a mark after the
#      first hit); the shield orc: 3 lunges from the front, then from its side and its back (the hit lines' health);
#      the berserker: the plain guard, then a raised blade at the right and the left (where its blows come from);
#   s: the shield orc alone (front, side, back; longer);
#   p: pictures: the guard at a fresh infantry (with SOMVR_DEFENCE_CLANK_MARKS / SOMVR_COMBAT_HIT_MARKS the mod marks
#      each clank and hit itself), a stereo dump after each clank, then lunges.
# Waits for the newest boot-check run of this worktree (COMBAT_WT, default: the worktree this script is in), then for the
# practice arrival; ends with a quit. Grips: `hands test R x y z rx ry rz` = cm right/up/ahead of the head's yaw frame,
# deg about the OpenXR grip's X Y Z (0 0 0 = the blade straight ahead, 0 -90 0 = the fist axis pointing right, 0 90 0 =
# left, 90 0 0 = up); `hands swing R <the same 6> MS` moves the test grip there in MS ms of the views' time. Marks: `m
# NAME` = RUN/marks/<time>_<n>/eyes.jpg (both eyes), listed in RUN/marks.log. Stops at once (quit) when the user goes
# into VR (somvr-headset active or status.json "user": true).
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${COMBAT_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${COMBAT_PLAN:-r}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/1000/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
down() { grep -q "^PRACTICE t=.* orc is down" "$R/somvr.log"; }
# the swings (the grip from the right shoulder's side to the left and back, an overhead chop), 300 ms each
FORE_A="hands test R 38 -15 22 0 -90 0";  FORE="hands swing R -22 -25 45 0 90 0 300"
BACK_A="hands test R -22 -25 45 0 90 0";  BACK="hands swing R 38 -15 22 0 -90 0 300"
CHOP_A="hands test R 12 15 22 80 0 0";    CHOP="hands swing R 6 -35 45 -30 0 0 300"
GUARD="hands test R 42 -12 35 0 90 0"     # the blade across the chest, 35 cm ahead (the fist axis left)
# the lunges: the arm out (the grip 55-62 cm ahead mid-swing: the tip ~1.45 m, an orc's capsule within ~1.85 m)
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
pair() { w "$FORE_A" 0.5; w "$FORE" 1.0; w "$BACK" 1.0; }
lunge() { w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; }
# face: snap-turn toward the orc of the newest THREAT cue line (its yaw off the view, + = right; 30 deg snaps)
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# dump: a stereo dump with every quad of the frame (the layer's test file; scripts/xr_compose.py draws them)
dump() { x "dump"; }
# waitfor PATTERN SECS: until one more somvr.log line matches (ERE) than when called; 1 = it came
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
chop() { w "$CHOP_A" 0.6; w "$CHOP" 1.0; }
# fastwait PATTERN SECS: waitfor polling every 50 ms (a stereo dump within a frame or two of a clank)
fastwait() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 20))); do
  [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.05; done; userin && bail "the user is in VR"; return 1; }
# 0. the controls card away (A, the layer's test button), the right hand, the sword drawn (two quick squeezes)
m c00-card 0.5
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" && echo "$(date +%T) CARD put away by the grip" || { echo "$(date +%T) CARD not put away by the grip: A"; x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"; w "hands squeeze L 1" 1; m f01-left-fist 0.3; w "hands squeeze L 0" 1; m f02-left-open 0.3
w "hands test R 28 -40 20 0 0 0"
w "hands draw" 2
m c01-drawn
REST="hands test R 10 -22 55 0 0 0"        # the blade straight ahead at the orc's chest (the tip ~1.45 m out)
RAISE_R="hands test R 38 -5 35 90 0 0"     # the blade upright at the right, 35 cm ahead
RAISE_L="hands test R -30 -5 35 90 0 0"    # ... and at the left
if [ "$PLAN" = p ]; then
  # pictures (with SOMVR_DEFENCE_CLANK_MARKS=3 SOMVR_COMBAT_HIT_MARKS=2: the mod marks the clanks and hits itself, the
  # eyes of the next frame): the guard at a fresh infantry, a stereo dump after each clank (the stagger), then lunges
  w "practice spawn infantry" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15
  face; w "$GUARD" 0.5
  for g in 1 2 3; do fastwait '^DEFENCE clank ' 20 && { sleep 0.2; printf 'dump\n' > "$R/xrtest.txt"; echo "$(date +%T) CLANK dump $g"; sleep 2.5; }; done
  for k in $(seq 1 8); do down && break; face; lunge; done
  sleep 3
elif [ "$PLAN" = r ]; then
  w "practice spawn infantry" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15
  face; w "$REST" 3; m r01-rest 0
  grep -E '^COMBAT (glance|t=)' "$R/somvr.log" | tail -3 | cut -c1-220
  w "$GUARD" 0.5
  for g in 1 2 3; do fastwait '^DEFENCE clank ' 14 && { printf 'dump\n' > "$R/xrtest.txt"; m r02-clank-$g 0; }; done
  # the first two hits: a stereo dump within a frame or two (a watcher in the background: the layer's file only, so it
  # never clobbers a trigger word of the lunges)
  ( for h in 1 2; do fastwait '^COMBAT hit tier' 60 && { printf 'dump\n' > "$R/xrtest.txt"; echo "$(date +%T) HIT dump $h"; sleep 1; }; done ) &
  # the reach without a lunge: 4 relaxed pairs (the grip 22-45 cm ahead), facing the orc
  h0=$(grep -c '^COMBAT hit tier' "$R/somvr.log")
  for k in 1 2 3 4; do down && break; face; pair; done
  echo "$(date +%T) REACH relaxed pairs: $(( $(grep -c '^COMBAT hit tier' "$R/somvr.log") - h0 )) hits"; grep -E '^THREAT start' "$R/somvr.log" | tail -2 | grep -oE 'dist=[0-9.]+ m'
  for k in $(seq 1 12); do
    down && break
    face; lunge
  done
  m r05-infantry-end 0.5
  w "practice spawn shield" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15
  for k in 1 2 3; do face; lunge; done
  m r06-shield-front 0
  for side in 1 2 3; do w "stick 1 0 0 1.0" 1.6; face; lunge; lunge; done
  m r07-shield-side 0
  for back in 1 2 3; do w "stick 1 0 0 1.2" 1.8; face; lunge; done
  m r08-shield-round 0
  w "practice spawn berserker" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && { sleep 0.35; dump; m r09-berserker-telegraph 0; }
  # the guard facing it: before each of its attacks a snap turn toward the newest THREAT cue (a player turns to it)
  for g in 1 2 3 4 5 6; do face; w "$GUARD" 0.3; waitfor '^DEFENCE (clank|parry|block|parry-block|hit|dodge) ' 12; done
  w "$RAISE_R" 0.5
  for g in 1 2 3; do waitfor '^DEFENCE (clank|parry|block|parry-block|hit) ' 12; done
  m r10-berserker-raised-right 0
  w "$RAISE_L" 0.5
  for g in 1 2 3; do waitfor '^DEFENCE (clank|parry|block|parry-block|hit) ' 12; done
  m r11-berserker-raised-left 0
  for k in $(seq 1 6); do down && break; face; lunge; done
  m r12-end 0.5
else
  w "practice spawn shield" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15
  for k in 1 2 3 4; do face; lunge; done
  m s01-shield-front 0
  for side in 1 2 3 4 5 6; do w "stick 1 0 0 1.0" 1.6; face; lunge; lunge; done
  m s02-shield-round 0
fi
w "practice remove" 2
echo "steps done $(date +%T)"
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
