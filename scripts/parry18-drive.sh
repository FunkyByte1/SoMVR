#!/bin/bash
# parry18-drive.sh - build 18's parry checks (handoff/HANDOFF-combat.md "build 18: the parry", docs/re-xr-pipeline.md
# 65): one headless boot, the test hands (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Prologue as
# block17-drive.sh's. P18_STEPS = any of (in this order):
#   base   a shield orc: edge cuts (lunges) until it blocks; its own block stops them (the 17b session's turtle)
#   shove  the same orc: the left hand pushed into its shield (a 150 ms swing, P18_SHOVE_N times), each followed by an
#          edge cut inside the opening: through its block / the riposte into its shield (the break)
#   word   a fresh shield orc: `parry open` then an edge cut (the riposte rule alone: into the shield = broken)
#   dodge  (build 19, SOMVR_PARRY_DODGE) a shield orc: at each red warning the test head 45 cm aside for 1.2 s (a lean:
#          the head out of the blow), then a step in and an edge cut at each opening (`PARRY dodge`), P18_DODGE_S s
#   inf    an infantry: `fight guard learn` (P18_LEARN_N strikes), then `fight guard path` (P18_PATH_N strikes): each
#          contact block a parry (the streak +1, the opening); a watcher answers each opening with a step and an edge cut
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${P18_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${P18_STEPS:-base shove word dodge inf}
T0=$(date +%s)
R=${P18_RUN:-} # P18_RUN=RUN: drive a running boot (build 19: tether19-drive.sh chains drivers)
for i in $(seq 1 120); do
  [ -n "$R" ] && break
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
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# the orc's distance from Talion (FRONT's per-frame score line), cm
odist() { tail -300 "$R/somvr.log" | grep -E '^FRONT score' | tail -1 | grep -oE ' dist=[0-9.]+' | head -1 | awk -F= '{printf "%d", $2 * 100}'; }
# walk toward the orc while a fresh FRONT line says it is farther (no fresh line: no orc in the fight, no walk)
near() { local k d; for k in $(seq 1 8); do d=$(odist); [ -z "$d" ] && return 1; [ "$d" -le "${1:-120}" ] && return 0; face; w "stick 0 1 0 0.30" 0.5; done; return 1; }
# a fresh orc of kind $1 when the last one is down (the count of "orc is down" lines since its spawn)
DN0=0
spawn() { w "practice spawn $1" 2; DN0=$(cnt '^PRACTICE t=.* orc is down'); face; }
alive_or() { [ "$(cnt '^PRACTICE t=.* orc is down')" -gt "$DN0" ] && spawn "$1"; return 0; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
BEHIND="hands test R 22 -35 -30 0 180 0"
LREST="hands test L -25 -35 30 0 0 0"
LBACK="hands test L -12 -22 20 0 0 0"                 # the left hand drawn back by the chest
LSHOVE="hands swing L 0 -18 ${P18_SHOVE_CM:-75} 0 0 0 150"  # the push: 55 cm forward in 150 ms (3.7 m/s)
RZ=90
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
mark0() { P0=$(cnt '^PARRY parry'); O0=$(cnt '^PARRY open'); S0=$(cnt '^PARRY shove'); RP0=$(cnt '^COMBAT hit tier=[a-z]+ \(the riposte'); OP0=$(cnt '^COMBAT hit tier=[a-z]+ \(open\)');
  BR0=$(cnt '^PARRY riposte .*BROKEN'); BL0=$(cnt '^REACT blocked'); TH0=$(cnt '^REACT blocking .*hits through'); C0=$(cnt '^DEFENCE contact'); ST0=$(cnt '^EXECUTE streak'); LA0=$(cnt '^COMBAT hit .*health [0-9.]+->[0-9.]+ ?[,(]'); }
phase() { echo "$(date +%T) PHASE $1: contacts $(( $(cnt '^DEFENCE contact') - C0 )), parries $(( $(cnt '^PARRY parry') - P0 )), opens $(( $(cnt '^PARRY open') - O0 )), shoves $(( $(cnt '^PARRY shove') - S0 )); hits in an opening $(( $(cnt '^COMBAT hit tier=[a-z]+ \(open\)') - OP0 )), ripostes $(( $(cnt '^COMBAT hit tier=[a-z]+ \(the riposte') - RP0 )), shields broken $(( $(cnt '^PARRY riposte .*BROKEN') - BR0 )); its block stopped $(( $(cnt '^REACT blocked') - BL0 )), hit through $(( $(cnt '^REACT blocking .*hits through') - TH0 )); streak lines $(( $(cnt '^EXECUTE streak') - ST0 ))"; }
# 0. the controls card away, the left hand forward, the sword drawn
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
m p00-start 0.3
if has base; then
  spawn shield; mark0
  for k in $(seq 1 "${P18_BASE_N:-6}"); do near 115; face; ecut; sleep 0.6; done
  phase "shield orc: edge cuts, no opening (the baseline)"
  grep -E '^(REACT blocked|COMBAT hit tier)' "$R/somvr.log" | tail -4 | cut -c1-240
fi
if has shove; then
  spawn shield; mark0
  for k in $(seq 1 "${P18_SHOVE_N:-6}"); do
    alive_or shield
    near 105; face; w "$EDGE_A" 0.4; w "$LBACK" 0.4
    echo "$(date +%T) the orc at $(odist) cm"; w "$LSHOVE" 0.25
    if waitfor '^PARRY shove' 1; then w "$EDGE" 0.6; w "$EDGE_B" 0.4; [ "$k" = 1 ] && { x "dump"; m p01-shove 0; }; else echo "$(date +%T) no shove"; sleep 0.5; fi
    [ "$k" = 2 ] && grep -q '^PARRY riposte .*BROKEN' "$R/somvr.log" && { x "dump"; m p02-broken 0; }
    w "$LREST" 0.8; sleep 0.8
  done
  phase "shield orc: a shove, then an edge cut in the opening"
  grep -E '^PARRY (shove|riposte|open|push refused)' "$R/somvr.log" | tail -8 | cut -c1-330
fi
if has word; then
  spawn shield; mark0
  for k in $(seq 1 "${P18_WORD_N:-4}"); do
    alive_or shield
    near 105; face; w "$EDGE_A" 0.4; w "parry open" 0.4; w "$EDGE" 0.6; w "$EDGE_B" 0.4
    grep -q '^PARRY riposte .*BROKEN' <(tail -200 "$R/somvr.log") && { x "dump"; m "p03-word-broken-$k" 0; }
    sleep 1.2
  done
  phase "shield orc: 'parry open', then an edge cut"
  grep -E '^PARRY riposte|^COMBAT hit tier=[a-z]+ \(the riposte' "$R/somvr.log" | tail -4 | cut -c1-300
fi
if has dodge; then
  spawn shield; mark0; D0=$(cnt '^PARRY dodge'); DG0=$(cnt '^DEFENCE dodge')
  timeout "${P18_DODGE_S:-50}" tail -n0 -F "$R/somvr.log" 2>/dev/null | grep --line-buffered -E '^THREAT red orc|^PARRY dodge' | while read -r l; do
    case $l in
      THREAT*) printf 'head -45 0 0 s 0.12\n' > "$R/xrtest.txt"; sleep 1.3; printf 'head 0 0 0 s 0.5\n' > "$R/xrtest.txt"; echo "$(date +%T) lean" ;;
      PARRY*) printf '%s' "stick 0 1 0 0.60" > "$R/somvr.trigger"; sleep 0.3
              printf '%s' "$EDGE_A" > "$R/somvr.trigger"; sleep 0.25; printf '%s' "$EDGE" > "$R/somvr.trigger"; sleep 0.5
              printf '%s' "$EDGE_B" > "$R/somvr.trigger"; echo "$(date +%T) the cut after the dodge" ;;
    esac
  done &
  RP=$!
  for k in $(seq 1 $(( ${P18_DODGE_S:-50} / 3 ))); do face; sleep 2
    alive_or shield
    [ "$k" = 6 ] && { x "dump"; m p05-dodge 0; }
  done
  wait $RP
  echo "$(date +%T) dodges $(( $(cnt '^DEFENCE dodge') - DG0 )), dodge openings $(( $(cnt '^PARRY dodge') - D0 ))"
  phase "shield orc: a lean at each red blow, a step and a cut at each opening"
  grep -E '^PARRY (dodge|riposte)|^COMBAT hit tier=[a-z]+ \((open|the riposte)' "$R/somvr.log" | tail -5 | cut -c1-300
fi
if has inf; then
  spawn infantry
  w "fight guard frame head" 0.5; w "fight guard learn" 0.5
  n=0; for k in $(seq 1 $(( ${P18_LEARN_N:-6} * 2 ))); do [ $n -ge "${P18_LEARN_N:-6}" ] && break; waitfor '^THREAT start .* impact=0\.[0-9]' 8 && n=$((n + 1)); face; alive_or infantry; done
  mark0; w "fight guard path" 0.5
  timeout "${P18_INF_S:-60}" tail -n0 -F "$R/somvr.log" 2>/dev/null | grep --line-buffered '^PARRY open' | while read -r _; do
    printf '%s' "stick 0 1 0 0.60" > "$R/somvr.trigger"; sleep 0.25
    printf '%s' "$EDGE_A" > "$R/somvr.trigger"; sleep 0.2; printf '%s' "$EDGE" > "$R/somvr.trigger"; sleep 0.5
    echo "$(date +%T) riposte"
  done &
  RP=$!
  for k in $(seq 1 $(( ${P18_INF_S:-60} / 3 ))); do face; sleep 2
    alive_or infantry
    [ "$k" = 4 ] && { x "dump"; m p04-inf 0; }
  done
  wait $RP
  phase "infantry: the test guard on the learnt paths (parries), a step and a cut at each opening"
  grep -E '^PARRY parry|^EXECUTE streak' "$R/somvr.log" | tail -6 | cut -c1-260
  w "fight guard learn" 0.5; w "$BEHIND" 0.5
fi
w "parry status" 0.5
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
[ -n "${P18_NOQUIT:-}" ] || echo quit >> "$R/cmd" # P18_NOQUIT=1: leave it running
echo "quit $(date +%T)"
