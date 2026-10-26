#!/bin/bash
# combat11-drive.sh - build 11's open combat checks (handoff/HANDOFF-open11.md round 1): one headless boot, the test
# hands (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Prologue as combat10-drive.sh's.
# C11_STEPS = any of (in this order):
#   learn   an infantry strikes with the blade behind the hip (out of every path; the guards' rest pose too): `fight guard learn` learns each strike's
#           path (the TGUARD learnt lines); the hits land
#   path    `fight guard path`: at each strike's reaction time (impact - 0.45 s, the telegraph) the blade swings onto its
#           learnt path (C11_PATH_N strikes): how many the contact judge blocks
#   miss    `fight guard miss`: the same guard mirrored to the other side (C11_MISS_N strikes): it should not block
#   hold    the sword just held out, still (`fight guard learn`): across the chest, pointing at the orc, vertical ahead
#           (C11_HOLD_N strikes each): the DEFENCE contact / hit lines per pose
#   C11_ORC: the practice orc (infantry, berserker, shield; default infantry); C11_HOLD1: one hold pose instead of three
#   kills   SOMVR_COMBAT_DEATH's variants, a kill each (lunges at a fresh infantry): C11_DEATHS (words after
#           `fight death`: kills separated by ',', the words of one by ';'; default "game,Light_UpToDown,Light_Straight")
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${C11_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${C11_STEPS:-learn path miss hold kills}
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
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
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
LOW="hands test R 30 -65 5 0 0 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
ORC=${C11_ORC:-infantry}   # the practice orc: infantry, berserker, shield
PATH_N=${C11_PATH_N:-20}; MISS_N=${C11_MISS_N:-12}; HOLD_N=${C11_HOLD_N:-10}; LEARN_N=${C11_LEARN_N:-12}
DEATHS=${C11_DEATHS:-game,Light_UpToDown,Light_Straight}
cnt() { grep -cE "$1" "$R/somvr.log"; }
# n strikes (THREAT starts with a melee impact) while facing the orc; a fresh infantry when it is down or gone
strikes() { local n=$1 i=0 miss=0
  while [ $i -lt "$n" ]; do
    if waitfor '^THREAT start .* impact=0\.[0-9]' 8; then i=$((i + 1)); miss=0; sleep 1.2; face
    else miss=$((miss + 1)); face; [ $miss -ge 2 ] && { w "practice spawn $ORC" 1; miss=0; }; fi
  done; }
phase() { local tag=$1; echo "$(date +%T) PHASE $tag: contacts $(( $(cnt '^DEFENCE contact') - C0 )), hits $(( $(cnt '^DEFENCE hit') - H0 )), near misses $(( $(cnt '^DEFENCE near miss') - N0 )), blocked guards $(( $(cnt '^TGUARD .*the blades met: blocked') - B0 )), unblocked guards $(( $(cnt '^TGUARD .*not blocked') - U0 ))"; }
mark0() { C0=$(cnt '^DEFENCE contact'); H0=$(cnt '^DEFENCE hit'); N0=$(cnt '^DEFENCE near miss'); B0=$(cnt '^TGUARD .*the blades met: blocked'); U0=$(cnt '^TGUARD .*not blocked'); }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
m e00-start 0.3
w "practice spawn $ORC" 1
if has learn; then
  w "fight guard learn" 0.5; mark0
  strikes "$LEARN_N"; phase learn
  grep -E '^TGUARD .*(learnt|blade in the right grip)' "$R/somvr.log" | tail -8 | cut -c1-220
fi
if has path; then
  w "fight guard path" 0.5; mark0; P=0
  for k in $(seq 1 "$PATH_N"); do
    strikes 1
    [ $P -lt 3 ] && grep -q '^TGUARD .*path guard for' "$R/somvr.log" && { P=$((P + 1)); m e0$P-path 0; }
  done
  phase path
  grep -E '^TGUARD .*(check|guard for)' "$R/somvr.log" | tail -6 | cut -c1-300
fi
if has miss; then
  w "fight guard miss" 0.5; mark0
  strikes "$MISS_N"; m e05-miss 0; phase miss
fi
if has hold; then
  w "fight guard learn" 0.5
  HOLDS=("42 -12 35 0 90 0" "30 -25 45 0 0 0" "10 -40 40 90 0 0"); [ -n "${C11_HOLD1:-}" ] && HOLDS=("$C11_HOLD1")
  for pose in "${HOLDS[@]}"; do
    w "hands test R $pose" 0.5; mark0
    strikes "$HOLD_N"; m "e06-hold-${pose// /_}" 0; phase "hold $pose"
  done
  w "$BEHIND" 0.5
fi
if has kills; then
  w "fight guard off" 0.5
  IFS=, read -r -a DL <<< "$DEATHS"
  for dv in "${DL[@]}"; do
    IFS=';' read -r -a DW <<< "$dv"; for dw in "${DW[@]}"; do w "fight death $dw" 0.5; done
    K0=$(cnt '^COMBAT death orc')
    w "practice spawn $ORC" 1.5   # (a spawn removes the last one first)
    for k in $(seq 1 16); do
      [ "$(cnt '^COMBAT death orc')" -gt "$K0" ] && break
      grep -q '^COMBAT hit .*dead=0->1' <(tail -200 "$R/somvr.log") && { sleep 4.5; continue; }
      face; w "stick 0 1 0 0.35" 0.5; lunge
    done
    waitfor '^COMBAT death orc' 6
    grep -E '^COMBAT death orc' "$R/somvr.log" | tail -1 | cut -c1-330
    dt=${dv// /_}; m "e07-death-${dt//;/+}" 0.3
  done
fi
grep -E '^TGUARD .*(guard for .*:|END)' "$R/somvr.log" | tail -3 | cut -c1-200
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
