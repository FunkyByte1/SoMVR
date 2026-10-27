#!/bin/bash
# block17-drive.sh - build 17's blocking checks (handoff/HANDOFF-combat.md "build 17: blocking"): one headless boot,
# the test hands (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Prologue as combat11-drive.sh's.
# B17_STEPS = any of (in this order):
#   bers     a berserker: `fight guard learn` (B17_LEARN_N strikes, the blade behind the hip), then `fight guard path`
#            (B17_PATH_N strikes: the blade swung onto each learnt path at the reaction time): blocked vs not
#   def      a shield orc (its spear sweeps cannot-counter: red with SOMVR_DEFENCE_RED_TYPES): pictures (both eyes + the
#            layer's dump) at two red wind-ups; then B17_DEF_S s with A pressed at each red warning (a watcher on the log,
#            the stick at rest: away from it), B17_LEAN_S s with the test head 45 cm aside at each (a lean), nothing for
#            B17_NONE_S s (the baseline); one A with the stick pushed at the orc (refused: something in the way)
#   probe    `fight dodge probe`: the dodge's rays in 8 directions at 1.5 and 5 m (walls, ledges where they are)
#   hurt     an infantry, the blade held out of the way: a hit lands on Talion; pictures 0.1 / 0.4 s after it (the
#            sword on the hand through his recoil); SWORDWT's off-the-hand angle in the 10 s around it
#   calm     A with no orc around (nothing happens, no buzz)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B17_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B17_STEPS:-bers def hurt probe calm}
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
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1) # (the whole log: 800 lines no longer cover a step)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
# draw at the shoulder: the grip into the slot, a squeeze, out with it held, the grip opened
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
put_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.4; w "$REST" 0.6; }
LOW="hands test R 30 -65 5 0 0 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LEARN_N=${B17_LEARN_N:-8}; PATH_N=${B17_PATH_N:-14}; DEF_S=${B17_DEF_S:-36}; LEAN_S=${B17_LEAN_S:-30}; NONE_S=${B17_NONE_S:-18}
cnt() { grep -cE "$1" "$R/somvr.log"; }
mark0() { C0=$(cnt '^DEFENCE contact'); H0=$(cnt '^DEFENCE hit'); N0=$(cnt '^DEFENCE near miss'); D0=$(cnt '^DEFENCE dodge'); RD0=$(cnt '^THREAT red orc'); G0=$(cnt '^STEP t=.* DODGE dash'); F0=$(cnt '^DODGE t=.* refused'); B0=$(cnt '^TGUARD .*the blades met: blocked'); U0=$(cnt '^TGUARD .*not blocked'); }
phase() { echo "$(date +%T) PHASE $1: contacts $(( $(cnt '^DEFENCE contact') - C0 )), hits $(( $(cnt '^DEFENCE hit') - H0 )), dodges $(( $(cnt '^DEFENCE dodge') - D0 )), near misses $(( $(cnt '^DEFENCE near miss') - N0 )); red warnings $(( $(cnt '^THREAT red orc') - RD0 )); A dodges $(( $(cnt '^STEP t=.* DODGE dash') - G0 )), refused $(( $(cnt '^DODGE t=.* refused') - F0 )); guards blocked $(( $(cnt '^TGUARD .*the blades met: blocked') - B0 )), not $(( $(cnt '^TGUARD .*not blocked') - U0 ))"; }
press_a() { x "btn R a 1"; x "btn R a 0"; }
# n strikes (THREAT starts with a melee impact) while facing the orc; a fresh orc when it is down or gone
strikes() { local n=$1 i=0 miss=0
  while [ $i -lt "$n" ]; do
    if waitfor '^THREAT start .* impact=0\.[0-9]' 8; then i=$((i + 1)); miss=0; sleep 1.2; face
    else miss=$((miss + 1)); face; [ $miss -ge 2 ] && { w "practice spawn $ORC" 1; miss=0; }; fi
  done; }
# a watcher for $2 s: at each new red warning, A (a) or the test head 45 cm aside for 1.2 s (lean); the first A's dash
# dumped (the tunnel in a picture)
react() { local n=0; timeout "$2" tail -n0 -F "$R/somvr.log" 2>/dev/null | grep --line-buffered '^THREAT red orc' | while read -r _; do
    n=$((n + 1))
    case $1 in
      a) printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep "${B17_A_S:-0.45}"; printf 'btn R a 0\n' > "$R/xrtest.txt"
         [ $n = 1 ] && { sleep 0.08; printf 'dump\n' > "$R/xrtest.txt"; } ;;
      lean) printf 'head -45 0 0 s 0.12\n' > "$R/xrtest.txt"; sleep 1.3; printf 'head 0 0 0 s 0.5\n' > "$R/xrtest.txt" ;;
    esac
    echo "$(date +%T) react $1 #$n"
  done; }
fight_for() { local k; for k in $(seq 1 $(($1 / 3))); do face; sleep 2; grep -q "^PRACTICE t=.* orc is down" <(tail -200 "$R/somvr.log") && w "practice spawn $ORC" 1.5; done; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
m e00-start 0.3
w "fight guard frame head" 0.5
if has bers; then
  ORC=berserker; w "practice spawn $ORC" 1.5
  w "fight guard learn" 0.5; mark0
  strikes "$LEARN_N"; phase "berserker learn (the blade behind the hip)"
  w "fight guard path" 0.5; mark0
  for k in $(seq 1 "$PATH_N"); do strikes 1; [ "$k" = 3 ] && { x "dump"; m e01-bers-path 0; }; done
  phase "berserker path (the blade onto each learnt path)"
  grep -E '^DEFENCE (contact|near miss)' "$R/somvr.log" | tail -4 | cut -c1-330
  w "fight guard learn" 0.5; w "$BEHIND" 0.5
fi
if has def; then
  ORC=shield; w "practice spawn $ORC" 1.5; mark0
  for k in 1 2; do waitfor '^THREAT red orc' 12 && { x "dump"; m "e0$((k + 1))-def-red" 0; }; face; done
  phase "defender pictures (no dodge)"
  mark0; react a "$DEF_S" & RP=$!; fight_for "$DEF_S"; wait $RP; phase "defender: A at each red warning"
  mark0; react lean "$LEAN_S" & RP=$!; fight_for "$LEAN_S"; wait $RP; phase "defender: a 45 cm lean at each red warning"
  mark0; fight_for "$NONE_S"; phase "defender: no dodge (the baseline)"
  mark0; waitfor '^THREAT red orc' 12 && { w "stick 0 1 0 1.2" 0.05; press_a; }; sleep 1.5
  phase "defender: A with the stick at the orc"
  grep -E '^(DODGE|STEP t=.* DODGE)' "$R/somvr.log" | tail -6 | cut -c1-300
fi
if has hurt; then
  ORC=infantry; w "practice spawn $ORC" 1.5; w "$GUARD" 0.5; mark0; H1=$(cnt '^DEFENCE hit')
  for k in $(seq 1 10); do
    waitfor '^DEFENCE hit' 6 && { sleep 0.05; x "dump"; m e05-hurt-0.1 0; sleep 0.2; m e06-hurt-0.4 0; break; }
    face
  done
  phase "hurt (the blade held across the chest, still)"
  grep -E '^SWORDWT' "$R/somvr.log" | tail -2 | cut -c1-300
fi
if has probe; then
  w "fight dodge probe" 1.5
  grep -E '^DODGE probe' "$R/somvr.log" | tail -16 | cut -c1-200
fi
if has calm; then
  w "practice remove" 1.5; sleep 4; F1=$(cnt '^DODGE t=.* refused: no fight'); press_a; sleep 0.5
  echo "$(date +%T) CALM: A with no orc: refused as no fight $(( $(cnt '^DODGE t=.* refused: no fight') - F1 ))"
fi
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
