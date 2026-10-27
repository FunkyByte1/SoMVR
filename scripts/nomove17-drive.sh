#!/bin/bash
# nomove17-drive.sh - build 17's "no game moves of the player" checks (handoff/HANDOFF-combat.md "build 17: no game
# moves of the player", docs/re-xr-pipeline.md 64): one headless boot, the test hands (SOMVR_HANDS_TEST=1), the layer's
# test file, marks with both eyes. Prologue as combat16-drive.sh's.
# N17_STEPS = any of (in this order):
#   berserker  for each of N17_MODES (default "off nopin on": the word `holdtalion off`; `holdtalion on` + `nopin` = the
#              ticks ours, the game's outside move measured only; `on` + `pin` = the switch as shipped): a fresh
#              berserker, edge-leading cuts (the Medium strike) until the game starts its paired headbutt on Talion
#              ('PC_OrcBzrk_Headbutt'): the HOLD / LOCO owner / DEFENCE hit / FIGHT hurt / ANCHOR lines, a mark (both
#              eyes) just after its impact; N17_BZ_N headbutts per mode (default 1)
#   infantry   `holdtalion on`, an infantry strikes a still Talion (the blade behind the hip) N17_HITS times
#   defender   the same with a shield orc (its radial and cone bashes)
# A mark just after the first hit on Talion of each step.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${N17_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${N17_STEPS:-berserker infantry defender}
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
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
RZ=90   # the edge leading: the committed cut (SOMVR_COMBAT_CLASS=cut: the game's Medium strike)
lunge_edge() { w "hands test R 40 -8 35 0 -90 $RZ" 0.5; w "hands swing R -20 -18 62 0 90 $RZ 300" 1.0; w "hands swing R 40 -8 35 0 -90 $RZ 300" 1.0; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
spawn() { w "practice spawn $1" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5; }
# the lines of one game move: the HOLD lines, LOCO's owner changes, the DEFENCE hit and the hurt since log line $1
since() { tail -n +"$1" "$R/somvr.log" | grep -E "^(HOLD talion|LOCO t=.* owner|DEFENCE hit|FIGHT hurt|ANCHOR |THREAT (reach|end) .*Headbutt)" | cut -c1-700; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
if has berserker; then
  for mode in ${N17_MODES:-off nopin on}; do
    case $mode in off) w "holdtalion off" 0.5 ;; nopin) w "holdtalion on" 0.3; w "holdtalion nopin" 0.5 ;;
      *) w "holdtalion on" 0.3; w "holdtalion pin" 0.5 ;; esac
    spawn berserker
    got=0
    for k in $(seq 1 ${N17_BZ_TRIES:-14}); do
      L0=$(($(wc -l < "$R/somvr.log") + 1)); B0=$(cnt "Talion attack 'PC_OrcBzrk_Headbutt'"); H0=$(cnt '^DEFENCE hit .*Headbutt')
      w "holdtalion health 120" 0.2   # (the practice keeps him at 20 or more: a blow's damage seen above it)
      face; w "stick 0 1 0 0.30" 0.5
      w "hands test R 40 -8 35 0 -90 $RZ" 0.5; w "hands swing R -20 -18 62 0 90 $RZ 300" 1.0
      # the headbutt started: the blade out of the way at once (no hit of ours into its half)
      if [ "$(cnt "Talion attack 'PC_OrcBzrk_Headbutt'")" -gt "$B0" ]; then w "$BEHIND" 0.2; else w "hands swing R 40 -8 35 0 -90 $RZ 300" 1.0; fi
      if [ "$(cnt "Talion attack 'PC_OrcBzrk_Headbutt'")" -gt "$B0" ] || waitfor "Talion attack 'PC_OrcBzrk_Headbutt'" 1; then
        w "$BEHIND" 0.1
        got=$((got + 1))
        { [ "$(cnt '^DEFENCE hit .*Headbutt')" -gt "$H0" ] || waitfor "^DEFENCE hit .*Headbutt" 3; } && { sleep 0.15; x "dump"; w "mark e1-headbutt-$mode-$got" 0.2; }
        sleep 3
        echo "$(date +%T) HEADBUTT $mode $got:"; since "$L0"
        [ "$got" -ge "${N17_BZ_N:-1}" ] && break
        w "fight health 200" 0.3
      fi
    done
    [ "$got" = 0 ] && echo "$(date +%T) HEADBUTT $mode: none in ${N17_BZ_TRIES:-14} cuts"
    w "$BEHIND" 0.5
  done
fi
hits() {   # $1 = the orc, $2 = the step name: the blade behind the hip, N17_HITS hits on Talion
  local n=0 miss=0 L0 D0
  w "holdtalion on" 0.3; w "holdtalion pin" 0.3
  spawn "$1"; w "$BEHIND" 0.5
  L0=$(($(wc -l < "$R/somvr.log") + 1))
  while [ $n -lt "${N17_HITS:-5}" ]; do
    D0=$(cnt '^DEFENCE hit ')
    if waitfor '^DEFENCE hit ' 10; then n=$((n + 1)); miss=0
      [ $n = 1 ] && { sleep 0.1; x "dump"; w "mark e2-$2-hit" 0.2; }
      face
    else miss=$((miss + 1)); face; [ $miss -ge 2 ] && { spawn "$1"; miss=0; }; fi
  done
  echo "$(date +%T) HITS $2:"; tail -n +"$L0" "$R/somvr.log" | grep -E "^(DEFENCE hit|FIGHT hurt|HOLD talion|LOCO t=.* owner)" | cut -c1-330
}
has infantry && hits infantry infantry
has defender && hits shield defender
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
