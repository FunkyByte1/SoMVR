#!/bin/bash
# combat-drive.sh - the combat helper's live words and both-eye marks for one headless boot (SOMVR_HANDS_TEST=1,
# SOMVR_MARK=1, SOMVR_XR_TEST=@RUN@/xrtest.txt): the practice orc fought with scripted swings (`hands swing`, mod
# somvr_body_game.c), a held guard against its attacks, a walk round the shield orc. Waits for the newest boot-check run
# of this worktree (COMBAT_WT, default: the worktree this script is in), then for the practice arrival; ends with a
# quit. Grips: `hands test R x y z rx ry rz` = cm right/up/ahead of the head's yaw frame, deg about the OpenXR grip's
# X Y Z (0 -90 0 = the fist axis pointing right, 0 90 0 = left, 90 0 0 = up); `hands swing R <the same 6> MS` moves
# the test grip there in MS ms of the views' time. Marks: `m NAME` = RUN/marks/<time>_<n>/eyes.jpg (both eyes), listed
# in RUN/marks.log. COMBAT_PLAN=a|b|c|d|e picks the steps (a: infantry, guard, shield; b: berserker, more guard; c: a
# parry's aftermath; d: with SOMVR_DEFENCE_PARRY=recoil, marks on the telegraph, blocks and hits, the three orc types; e: d facing the orc, with stereo dumps for scripts/xr_compose.py).
# Stops at once (quit) when the user goes into VR (somvr-headset active or status.json "user": true).
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${COMBAT_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${COMBAT_PLAN:-a}
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
# 0. the controls card away (A, the layer's test button), the right hand, the sword drawn (two quick squeezes)
x "btn R a 1"; x "btn R a 0"
w "hands test R 28 -40 20 0 0 0"
w "hands draw" 2
m c01-drawn
if [ "$PLAN" = a ]; then
  # 1. the infantry: swings until it is down (at most 14 pairs), marks of the first hits
  w "practice spawn infantry" 5
  m c02-infantry
  for k in $(seq 1 14); do
    down && break
    pair
    [ $k = 1 ] && m c03-after-fore-back 0
    [ $k = 2 ] && { w "$FORE_A" 0.5; w "$FORE" 0.35; m c04-mid-hit 0; }
    [ $k = 4 ] && { chop; m c05-after-chop 0; }
    [ $k = 6 ] && { w "$GUARD" 1; for g in 1 2 3; do m c06-guard-$g 1.2; done; }
  done
  m c07-infantry-end 1
  # 2. the guard against a fresh infantry: the blade held across for 12 s (its attacks: telegraphs, blocks/parries)
  w "practice spawn infantry" 5
  w "$GUARD" 1
  for g in 1 2 3 4 5 6; do m c08-guard2-$g 1.5; done
  # 3. the shield orc: from the front, then from its side after a strafe (stick right), then more
  w "practice spawn shield" 5
  m c09-shield 0
  for k in 1 2 3; do pair; done
  w "stick 1 0 0 1.5" 2.5
  m c10-shield-side 0
  for k in 1 2 3; do pair; done
  w "stick 1 0 0 1.5" 2.5
  for k in 1 2 3; do pair; chop; done
  m c11-shield-end 1
elif [ "$PLAN" = d ]; then
  # SOMVR_DEFENCE_PARRY=recoil: the telegraph in view, the guard (blocks, parries as the orc's recoil), lunges to hit
  w "practice spawn infantry" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && m d02-telegraph 0.1
  w "$GUARD" 0.5
  for g in 1 2 3; do waitfor '^DEFENCE (parry|block|parry-block) ' 12 && m d03-block-$g 0; done
  for k in $(seq 1 10); do
    down && break
    lunge
    [ "$(grep -c '^COMBAT hit' "$R/somvr.log")" -gt 0 ] && [ -z "${D04:-}" ] && { D04=1; m d04-after-hit 0; }
  done
  m d05-infantry-end 0.5
  # the shield orc: lunges from the front (its shield), then strafes to its side (stick right) and lunges again
  w "practice spawn shield" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && m d06-shield-telegraph 0.1
  for k in 1 2 3; do lunge; done
  m d07-shield-front 0
  for side in 1 2 3; do w "stick 1 0 0 1.2" 2; lunge; lunge; done
  m d08-shield-side 0
  for k in 1 2 3; do down && break; w "stick 1 0 0 0.8" 1.5; lunge; chop; done
  # the berserker
  w "practice spawn berserker" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && m d09-berserker-telegraph 0.1
  w "$GUARD" 0.5
  waitfor '^DEFENCE (parry|block|parry-block) ' 12 && m d10-berserker-block 0
  for k in $(seq 1 8); do down && break; lunge; done
  m d11-end 0.5
elif [ "$PLAN" = e ]; then
  # as d, facing the orc before each lunge, with stereo dumps (xr_compose.py) of the telegraph, a block and a hit
  w "practice spawn infantry" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && { sleep 0.35; dump; m e02-telegraph 0; }
  w "$GUARD" 0.5
  for g in 1 2; do waitfor '^DEFENCE (parry|block|parry-block) ' 12 && { dump; m e03-block-$g 0; }; done
  for k in $(seq 1 12); do
    down && break
    face; lunge
    [ "$(grep -c '^COMBAT hit' "$R/somvr.log")" -gt 0 ] && [ -z "${E05:-}" ] && { E05=1; dump; m e05-after-hit 0; }
  done
  m e06-infantry-end 0.5
  w "practice spawn shield" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && { sleep 0.35; dump; m e07-shield-telegraph 0; }
  for k in 1 2 3; do face; lunge; done
  m e08-shield-front 0
  for side in 1 2 3; do w "stick 1 0 0 1.0" 1.6; face; lunge; lunge; done
  m e09-shield-side 0
  w "practice spawn berserker" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && { sleep 0.35; dump; m e10-berserker-telegraph 0; }
  w "$GUARD" 0.5
  waitfor '^DEFENCE (parry|block|parry-block) ' 12 && { dump; m e11-berserker-block 0; }
  for k in $(seq 1 10); do down && break; face; lunge; done
  m e12-end 0.5
elif [ "$PLAN" = c ]; then
  # the parry's aftermath: the guard until a parry (the stock counter), marks through its sync, then swings at the orc
  w "practice spawn infantry" 4
  w "$GUARD" 1
  n0=$(grep -c '^DEFENCE parry' "$R/somvr.log")
  for i in $(seq 1 60); do [ "$(grep -c '^DEFENCE parry' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.5; done
  m p01-parry 0
  m p02-counter 0.5
  for i in $(seq 1 20); do grep -q '^FALLBACK sync record .* stale' "$R/somvr.log" && break; sleep 0.5; done
  m p03-after 0.5
  w "practice status" 1
  for k in $(seq 1 5); do pair; done
  m p04-swings-after 0
  w "practice status" 1
  w "$GUARD" 1
  for g in 1 2 3; do m p05-guard-$g 2; done
  for k in $(seq 1 4); do down && break; pair; chop; done
  w "practice spawn shield" 5
  m p06-shield 0
  for k in 1 2 3; do pair; done
  w "stick 1 0 0 1.5" 2.5
  for k in 1 2 3; do pair; done
  m p07-shield-side 0
else
  # the berserker, then the guard against it
  w "practice spawn berserker" 5
  m c02-berserker
  for k in $(seq 1 10); do down && break; pair; [ $k = 2 ] && m c03-berserker-hit 0; done
  w "$GUARD" 1
  for g in 1 2 3 4 5 6; do m c04-guard-$g 1.5; done
fi
w "practice remove" 2
echo "steps done $(date +%T)"
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
