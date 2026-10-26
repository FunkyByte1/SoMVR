#!/bin/bash
# combat10-drive.sh - build 10's combat round (handoff/HANDOFF-combat.md round 3): one headless boot, the test hands
# (SOMVR_HANDS_TEST=1), the layer's test file (SOMVR_XR_TEST=@RUN@/xrtest.txt), bug marks with both eyes. Model:
# build7-drive.sh's plan e. Run it next to scripts/headless_vr.sh (both outside the sandbox); it waits for the newest
# boot-check run whose launch-cmd.txt names this worktree (C10_WT, default: the worktree this script is in), then for
# the practice arrival, and ends with a quit. Stops at once (quit) when the user goes into VR.
# C10_STEPS = any of (in this order):
#   draws   the draw at the right shoulder (the grip into the slot, a squeeze), put back there, three times; the quick
#           draw (`hands draw`) and its put-back: SOMVR_ARMS_LW's `ARMS watch` lines (the hands through each draw)
#   auto    an infantry spawned with the sword sheathed, a walk at it (the stick): the game's draw refused / put back
#           (SOMVR_SLOTS_GAMEDRAW=refuse), then the player's draw at the shoulder in the fight
#   guard   the blade held out across the chest for the infantry's attacks (the block's judge), then dropped (hits on
#           Talion: SOMVR_HURT), marks at the wind-up, the block and the hit
#   swings  lunges at the infantry (facing it), the reaction lines (`COMBAT reaction`: how far it went)
#   hits    slashes at an infantry as it starts each strike (the reactions: SOMVR_COMBAT_KNOCKBACK)
#   timed   a block placed late into the strike's path (SOMVR_DEFENCE_BLOCK=contact: the blade moved into the blow's
#           line in its last 0.25 s), marks at the clank and the recoil
#   bers    the berserker: the guard, its blows, lunges
# Grips: `hands test R x y z rx ry rz` = cm right/up/ahead of the head's yaw frame, deg about the OpenXR grip's X Y Z;
# `hands swing R <the same 6> MS`. The sword slot: 10 right, -10 up, -15 ahead of the head (SOMVR_SLOTS defaults).
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${C10_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${C10_STEPS:-draws auto guard swings timed bers}
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
dump() { x "dump"; }
down() { grep -q "^PRACTICE t=.* orc is down" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
GUARD="hands test R 42 -12 35 0 90 0"     # the blade across the chest, 35 cm ahead (the fist axis left)
LOW="hands test R 30 -65 5 0 0 0"         # the arm down at the side: the blade out of every blow's path
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
lunge() { w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; }
# the sword in the player's hand: the last SLOTS line that tells (a draw done, the attach keep, the kept-on-the-back move)
drawn() { local l; l=$(tail -800 "$R/somvr.log" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
# draw at the shoulder: the grip into the slot, a squeeze, out with it held, the grip opened
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
put_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.4; w "$REST" 0.6; }
# 0. the controls card away (the right grip, else A), the left hand forward
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
m a00-start 0.3
if has draws; then
  for k in 1 2 3; do
    draw_slot; m a0$k-drawn-shoulder 0.2
    drawn && echo "$(date +%T) PASS draw $k" || echo "$(date +%T) FAIL draw $k"
    sleep 3.2
    put_slot; drawn && echo "$(date +%T) FAIL put-back $k" || echo "$(date +%T) PASS put-back $k"
    sleep 3.2
  done
  w "$OUT" 0.5; w "hands draw" 1.5; m a04-quick-draw 0.2
  drawn && echo "$(date +%T) PASS quick draw" || echo "$(date +%T) FAIL quick draw"
  sleep 3.2; put_slot; sleep 3.2
fi
if has auto; then
  drawn && put_slot
  w "practice spawn infantry" 1.0
  waitfor '^THREAT start ' 15
  face
  w "stick 0 1 0 1.2" 1.6
  m b01-combat-sheathed 0.2
  sleep 2
  drawn && echo "$(date +%T) FAIL the sword is out (not drawn by the player)" || echo "$(date +%T) PASS the sword stays sheathed in combat"
  grep -E "^SLOTS .*(UNSHEATH|undraw|drew it|changed by the game)" "$R/somvr.log" | tail -5
  draw_slot; m b02-drawn-in-combat 0.2
  drawn && echo "$(date +%T) PASS draw in combat" || echo "$(date +%T) FAIL draw in combat"
fi
if has guard; then
  drawn || draw_slot
  w "practice spawn infantry" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && { sleep 0.35; dump; m c01-windup 0; }
  w "$GUARD" 0.5
  for g in 1 2 3; do waitfor '^DEFENCE (parry|block|parry-block|hit) ' 12 && { dump; m c0$((g + 1))-guard-$g 0; }; done
  w "$LOW" 0.5
  for g in 1 2; do waitfor '^FIGHT hurt ' 12 && { dump; m c0$((g + 4))-hurt-$g 0; }; done
  w "$GUARD" 0.5
fi
if has swings; then
  drawn || draw_slot
  for k in $(seq 1 12); do
    down && break
    face; w "stick 0 1 0 0.35" 0.5; lunge   # a short step in first (the infantry fights at 1.6-1.7 m: the tip's limit)
    [ "$(grep -c '^COMBAT hit' "$R/somvr.log")" -gt 0 ] && [ -z "${D01:-}" ] && { D01=1; dump; m d01-after-hit 0; }
    [ "$(grep -c '^COMBAT reaction' "$R/somvr.log")" -gt 0 ] && [ -z "${D02:-}" ] && { D02=1; m d02-reaction 0; }
  done
  m d03-infantry-end 0.5
fi
if has hits; then
  # our hits on an infantry for its reactions (SOMVR_COMBAT_KNOCKBACK): the orc starts a strike (in its reach), we turn
  # to it and slash at once (our blade lands in its wind-up, 0.3 s of its 0.8 s)
  drawn || draw_slot
  w "practice spawn infantry" 0.5
  for k in $(seq 1 14); do
    down && break
    waitfor '^THREAT start .* impact=0\.[0-9]' 8 || { face; w "stick 0 1 0 0.35" 0.5; }
    face; lunge
    [ "$(grep -c '^COMBAT hit tier' "$R/somvr.log")" -gt 0 ] && [ -z "${H01:-}" ] && { H01=1; m h01-after-hit 0; }
  done
  m h02-hits-end 0.5
fi
if has timed; then
  # guard poses held through one attack each (the contact judge: which ones meet the infantry's blade; DEFENCE path
  # lines tell where its weapon passed): across the chest, a vertical blade ahead (middle, left, right), across high/low
  drawn || draw_slot
  w "practice spawn infantry" 0.5
  k=0
  for pose in "42 -12 35 0 90 0" "10 -45 40 90 0 0" "-25 -45 40 90 0 0" "35 -45 40 90 0 0" "42 15 35 0 90 0" "42 -50 40 0 90 0" \
              "10 -45 40 90 0 0" "-25 -45 40 90 0 0"; do
    k=$((k + 1))
    down && break
    waitfor '^THREAT start .* impact=0\.[0-9]' 15 || continue
    face
    w "hands test R $pose" 0.2
    waitfor '^DEFENCE (contact|block|hit|dodge|parry) ' 3 && m e0$k-pose 0
    sleep 0.8
  done
  m e09-timed-end 0.5
fi
if has bers; then
  drawn || draw_slot
  w "practice spawn berserker" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 15 && { sleep 0.35; dump; m f01-berserker-windup 0; }
  w "$GUARD" 0.5
  for g in 1 2; do waitfor '^DEFENCE (parry|block|parry-block|hit) ' 12 && { dump; m f0$((g + 1))-berserker-$g 0; }; done
  for k in $(seq 1 10); do down && break; face; w "stick 0 1 0 0.35" 0.5; lunge; done
  m f04-end 0.5
fi
w "practice remove" 2
echo "steps done $(date +%T)"
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
