#!/bin/bash
# combat16-drive.sh - build 16's combat checks (handoff/HANDOFF-combat.md "build 16"): one headless boot, the test hands
# (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Prologue as combat11-drive.sh's.
# C16_STEPS = any of (in this order):
#   shield   a fresh shield orc per record of C16_RECS (the word `fight rec light|heavy NAME`), lunges from its front
#            until one lands: the COMBAT hit line's shield (its HP before -> after) and health per record family
#   reach    an infantry strikes a still Talion (the blade behind the hip, out of every path) C16_REACH_N times: the
#            THREAT reach lines (how close each strike type comes to his root and to the head)
#   passive  a shield orc: its shield broken from the front (C16_BREAK_REC), C16_PASSIVE_HITS more lunges while it
#            reels (the REACT rolls), then the blade behind the hip for C16_PASSIVE_S s: does it strike again
#            (the break: up to 3 fresh shield orcs, stepped at while idle, until a knockdown break, the user's case)
#   C16_RANGES: "ATTACK MIN MAX;..." for `fight range` before the reach step (an attack's StrikeRange)
#   broken   shield orcs broken by the High strike, then set to C16_BROKEN health values: strikes vs block starts
#   ways     the shield orc: a cut into the shield, low, high, from behind, after the stun (C16_WAYS, C16_WAYS_SHIELD)
#   C16_HEAD: "X Y Z" the test head's offset (tracking space, -Z ahead) during the reach step
#   lowhp    an infantry set to C16_LOWHP health values (`fight health`), C16_LOWHP_S x 0.4 s each: strikes vs keyless moves
#   blocks   a shield orc, C16_BLOCKS_N lunges at its front with the records as they are (the build's mapping;
#            C16_BLOCKS_WORDS: "WORD;WORD" first, e.g. "fight shield glance;fight class cut")
#   C16_ORC_MARKS: marks (both eyes) at the steps' moments (default on)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${C16_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${C16_STEPS:-shield reach passive}
RECS=${C16_RECS:-PC_HC_Lowest_L1_D0_F_LR_RL_LR_7 PC_HC_Low_L2_D0_F_LR_RL_LR_2_RevGrip PC_HC_Medium_L1_D0_F_RR_LL_LR_2 PC_HC_High_L1_D0_F_LL_LL_02 PC_HC_Low_L2_D0_F_RL_LR_ST_9_Thrust}
BREAK_REC=${C16_BREAK_REC:-PC_HC_High_L1_D0_F_LL_LL_02}
REACH_N=${C16_REACH_N:-14}; PASSIVE_HITS=${C16_PASSIVE_HITS:-3}; PASSIVE_S=${C16_PASSIVE_S:-25}; BLOCKS_N=${C16_BLOCKS_N:-6}
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
m() { [ "${C16_ORC_MARKS:-1}" = 1 ] || return 0; sleep "${2:-0.3}"; w "mark $1" 4; }
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
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
lunge() { w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; }
RZ=${C16_EDGE_RZ:-90}   # the edge leading: the grip rolled about its blade (the plain lunge sweeps the flat first)
lunge_edge() { w "hands test R 40 -8 35 0 -90 $RZ" 0.5; w "hands swing R -20 -18 62 0 90 $RZ 300" 1.0; w "hands swing R 40 -8 35 0 -90 $RZ 300" 1.0; }
LUNGE_FN=lunge
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1) # (the whole log: 800 lines no longer cover a step)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# lunge at the practice orc until a COMBAT hit lands on it (at most $1 tries); 0 = landed
hit_it() { local k h0; h0=$(cnt '^COMBAT hit tier'); for k in $(seq 1 "${1:-12}"); do
    [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ] && return 0
    face; w "stick 0 1 0 0.30" 0.5; $LUNGE_FN
  done; [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ]; }
last_hit() { grep -E '^COMBAT hit tier' "$R/somvr.log" | tail -1 | sed -E "s/.*tier=([a-z]+) .*rec='([^']+)'.*dead=([0-9]->[0-9]) health ([0-9.]+->[0-9.]+)( NO DAMAGE)?(, its shield [0-9.]+->[0-9.]+)?.*the contact ([-+0-9]+) deg.*/\1 \2 dead \3 health \4\5\6 side \7/"; }
spawn() { w "practice spawn $1" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
m e00-start 0.3
if has shield; then
  for rec in $RECS; do
    w "fight rec light $rec" 0.3; w "fight rec heavy $rec" 0.3; w "fight rec stab $rec" 0.3
    spawn shield
    if hit_it 14; then echo "$(date +%T) SHIELD $rec: $(last_hit)"; else echo "$(date +%T) SHIELD $rec: no hit landed"; fi
    w "$BEHIND" 1.5
  done
  for k in light heavy stab; do w "fight rec $k default" 0.3; done
  m e01-shield 0.3
fi
if has reach; then
  IFS=';' read -r -a RG <<< "${C16_RANGES:-}"; for rg in "${RG[@]}"; do [ -n "$rg" ] && w "fight range $rg" 0.5; done
  [ -n "${C16_HEAD:-}" ] && { x "head $C16_HEAD s 0.4"; sleep 1; }   # the player's head off Talion's root (the test head)
  spawn infantry
  w "$BEHIND" 0.5
  R0=$(cnt '^THREAT reach'); i=0; miss=0
  while [ $i -lt "$REACH_N" ]; do
    if waitfor '^THREAT reach' 8; then i=$((i + 1)); miss=0; [ "${C16_REACH_FACE:-1}" = 1 ] && face
      if [ $i = 3 ] || [ $i = 7 ]; then   # both eyes and the layer's dump at a strike's start
        waitfor '^THREAT start .* impact=0\.' 8 && { x "dump"; m "e02-reach$i" 0; }
      fi
    else miss=$((miss + 1)); face; [ $miss -ge 2 ] && { spawn infantry; miss=0; }; fi
  done
  [ -n "${C16_HEAD:-}" ] && { x "head 0 0 0 s 0.4"; sleep 0.6; }
  echo "$(date +%T) REACH $(( $(cnt '^THREAT reach') - R0 )) lines"
  grep -E '^THREAT reach' "$R/somvr.log" | tail -n "$REACH_N" | sed -E "s/^THREAT reach orc=\S+ attack '([^']+)' \(impact ([0-9.]+) s, ([a-z ]+)\): Talion's root ([0-9]+) cm at the start, ([0-9]+) at its impact, closest ([0-9]+) at ([0-9.]+) s; the head ([0-9]+) \/ ([0-9]+) \/ closest ([0-9]+) .*/\1 root \4->\5 min \6 head \8->\9 min \10/"
fi
if has passive; then
  # the user's mark 8: a shield orc broken while idle (the knockdown 'Recoil_F_KD_ShieldBroken'), hit C16_PASSIVE_HITS
  # times while it reels (the REACT rolls in its stagger), its health set (`fight health`), then the blade behind the
  # hip for C16_PASSIVE_S x 0.4 s: its strikes vs its block starts. C16_PASSIVE_CASES = "HITS:HEALTH ..." (one fresh
  # orc each; default "0:60 2:60 2:5")
  w "fight shield game" 0.3; w "fight class speed" 0.3
  for cs in ${C16_PASSIVE_CASES:-0:60 2:60 2:5}; do
    ph=${cs%%:*}; phv=${cs##*:}
    w "fight rec light $BREAK_REC" 0.3; w "fight rec heavy $BREAK_REC" 0.3
    for k in 1 2 3; do
      spawn shield; sleep 1.0; face; w "stick 0 1 0 ${C16_PASSIVE_STEP:-0.8}" 0.3; lunge
      hit_it 6 && echo "$(date +%T) PASSIVE $cs break $k: $(last_hit)"
      sleep 1; grep -E '^COMBAT reaction' "$R/somvr.log" | tail -1 | cut -c1-160
      grep -E '^COMBAT hit tier' "$R/somvr.log" | tail -1 | grep -q 'shield 300.0->0.0' && break
    done
    for k in light heavy; do w "fight rec $k default" 0.3; done
    for k in $(seq 1 "$ph"); do hit_it 4 && echo "$(date +%T) PASSIVE $cs hit $k: $(last_hit)"; done
    sleep 0.5; w "fight health $phv" 0.3; w "$BEHIND" 0.5
    S0=$(cnt '^THREAT start .* impact=0\.'); B0=$(cnt "^VERBS .* attack 'AI_[A-Za-z]+_Block_Into'"); G0=$(cnt '^FRONT grant'); N0=$(cnt '^FRONT attack .*no attack start')
    for k in $(seq 1 "$PASSIVE_S"); do face; sleep 0.4; done
    m "e04-passive-${cs/:/h}" 0
    echo "$(date +%T) PASSIVE $cs: strikes $(( $(cnt '^THREAT start .* impact=0\.') - S0 )), block starts $(( $(cnt "^VERBS .* attack 'AI_[A-Za-z]+_Block_Into'") - B0 )), grants $(( $(cnt '^FRONT grant') - G0 )), grants unused 6 s $(( $(cnt '^FRONT attack .*no attack start') - N0 ))"
  done
fi
if has broken; then
  # a shield orc broken by the High strike (the heavy tier's record only; its light hits stay the Lowest), then HITS
  # edge-leading cuts (the class CLASS: speed = the High strikes, cut = the Medium ones) and its health set to HEALTH
  # (`fight health`; - = as the hits left it), a fresh orc per case of C16_BROKEN ("CLASS:HITS:HEALTH ..."), the blade
  # behind the hip for C16_BROKEN_S x 0.4 s: its strikes vs its block starts (the user's mark 8 orc: three High hits
  # after the break, 5 of 125 left)
  w "fight shield game" 0.3
  for cs in ${C16_BROKEN:-speed:0:6 speed:0:15 speed:0:60}; do
    bc=${cs%%:*}; rest=${cs#*:}; bh=${rest%%:*}; hv=${rest#*:}
    w "fight class speed" 0.3; w "fight rec heavy $BREAK_REC" 0.3
    R0=$(cnt "^COMBAT reaction .*ShieldBroken"); ok=0
    for k in $(seq 1 3); do
      spawn shield
      for j in $(seq 1 6); do
        face; w "stick 0 1 0 0.30" 0.5; lunge_edge
        [ "$(cnt "^COMBAT reaction .*ShieldBroken")" -gt "$R0" ] && { ok=1; break; }
        grep -q "^PRACTICE t=.* orc is down" <(tail -200 "$R/somvr.log") && break
      done
      [ $ok = 1 ] && break
    done
    w "fight rec heavy default" 0.3; w "fight class $bc" 0.3
    echo "$(date +%T) BROKEN $cs: $(grep -E '^COMBAT reaction .*ShieldBroken' "$R/somvr.log" | tail -1 | grep -o "'AI_Defender[A-Za-z_]*ShieldBroken'")"
    # the user's flurry: up to HITS quick forward cuts (no back swing), stepping in while none lands
    sleep 2.5; H0=$(cnt '^COMBAT hit tier')
    for k in $(seq 1 $((bh * 3))); do
      [ "$(( $(cnt '^COMBAT hit tier') - H0 ))" -ge "$bh" ] && break
      face; [ $((k % 2)) = 1 ] && w "stick 0 1 0 0.35" 0.4
      w "hands test R 40 -8 35 0 -90 $RZ" 0.35; w "hands swing R -20 -18 62 0 90 $RZ 250" 0.45; w "$BEHIND" 0.3
    done
    grep -E '^COMBAT hit tier' "$R/somvr.log" | tail -n +$((H0 + 1)) | sed -E "s/.*tier=([a-z]+)[^']*rec='([^']+)'.*health ([0-9.]+->[0-9.]+).*/BROKEN $cs hit: \1 \2 \3/" 
    [ "$hv" != "-" ] && w "fight health $hv" 0.3
    w "$BEHIND" 0.5
    S0=$(cnt '^THREAT start .* impact=0\.'); B0=$(cnt "^VERBS .* attack 'AI_[A-Za-z]+_Block_Into'"); N0=$(cnt '^FRONT attack .*no attack start'); P0=$(cnt '^SLOTS .*the game put it away')
    for k in $(seq 1 "${C16_BROKEN_S:-40}"); do face; sleep 0.4; done
    m "e07-broken-${cs//:/-}" 0
    echo "$(date +%T) BROKEN $cs: strikes $(( $(cnt '^THREAT start .* impact=0\.') - S0 )), block starts $(( $(cnt "^VERBS .* attack 'AI_[A-Za-z]+_Block_Into'") - B0 )), grants unused 6 s $(( $(cnt '^FRONT attack .*no attack start') - N0 )), the game's sheathes $(( $(cnt '^SLOTS .*the game put it away') - P0 ))"
    grep -q "^PRACTICE t=.* orc is down" <(tail -400 "$R/somvr.log") && echo "BROKEN $cs: the orc is down"
  done
fi
if has ways; then
  # what a player can do against a raised shield (build 16 round 2), the build's own config (cut, glance): a cut into
  # the shield, one low at the legs, one high over it, one from behind (Talion put 130 cm behind the orc), one after
  # the stun (the left palm on its head, a quick left trigger: the VR stun); a fresh shield orc each
  w "fight class cut" 0.3; w "fight shield ${C16_WAYS_SHIELD:-glance}" 0.3
  wcut() { # wcut Y_FROM Y_TO: an edge-leading cut across at that height (cm from the eyes)
    w "hands test R 40 $1 38 0 -90 $RZ" 0.35; w "hands swing R -20 $2 62 0 90 $RZ 250" 0.45; w "$BEHIND" 0.3; }
  for wy in ${C16_WAYS:-front low high behind stun}; do
    case $wy in *@*) w "fight shield ${wy#*@}" 0.3; wy=${wy%@*} ;; esac   # CASE@MODE: that case under that shield mode
    spawn shield; sleep 1.5; H0=$(cnt '^COMBAT hit tier')
    for k in 1 2 3 4 5 6; do
      [ "$(cnt '^COMBAT hit tier')" -gt "$H0" ] && break
      face
      case $wy in
        behind) w "verbs behind 130" 1.2; face; wcut -8 -18 ;;
        stun) w "stick 0 1 0 0.30" 0.4; w "verbs aim head" 0.6   # a quick left trigger (<= 0.35 s): the stun, not the hold
              printf '%s\n' "btn L trigger 1" > "$R/xrtest.txt"; sleep 0.15; printf '%s\n' "btn L trigger 0" > "$R/xrtest.txt"
              sleep 0.4; w "verbs aim off" 0.2; wcut -8 -18 ;;
        back) w "stick 0 1 0 0.30" 0.4   # a backhand, left to right: into the shield's side
              w "hands test R -40 -30 38 0 90 $RZ" 0.35; w "hands swing R 30 -35 62 0 -90 $RZ 250" 0.45; w "$BEHIND" 0.3 ;;
        chop) w "stick 0 1 0 0.30" 0.4   # straight down at the shield (left of the middle)
              w "hands test R -15 25 35 -70 0 0" 0.35; w "hands swing R -20 -60 55 40 0 0 250" 0.45; w "$BEHIND" 0.3 ;;
        low) w "stick 0 1 0 0.30" 0.4; wcut -95 -100 ;;
        high) w "stick 0 1 0 0.30" 0.4; wcut 12 8 ;;
        *) w "stick 0 1 0 0.30" 0.4; wcut -30 -35 ;;
      esac
    done
    grep -E '^COMBAT hit tier' "$R/somvr.log" | tail -n +$((H0 + 1)) | head -2 | sed -E "s/.*tier=([a-z]+)( \([a-z ]+\))?[^']*rec='([^']+)'.*health ([0-9.]+->[0-9.]+)( NO DAMAGE)?(, its shield [0-9.]+->[0-9.]+)?.*the contact ([-+0-9]+) deg off the orc's facing\)(.*)/WAYS $wy: \1\2 \3 health \4\5\6 side \7\8/" | cut -c1-420
    grep -E "^VERBS .*(Stun|stun)" <(tail -300 "$R/somvr.log") | tail -2 | cut -c1-160
    [ "$wy" = front ] && { x "dump"; m e08-ways-front 0; }
  done
fi
if has lowhp; then
  # an infantry at a full, a third, an eighth and a twenty-fifth of its health (`fight health`), C16_LOWHP_S s each,
  # the blade behind the hip: its strikes vs its block starts (the user's mark 8: a 5-health shield orc only blocked)
  spawn infantry; w "$BEHIND" 0.5
  for hv in ${C16_LOWHP:-125 40 15 5}; do
    w "fight health $hv" 0.3
    S0=$(cnt '^THREAT start .* impact=0\.'); K0=$(cnt '^THREAT start .*keyless'); G0=$(cnt '^FRONT grant')
    for k in $(seq 1 "${C16_LOWHP_S:-14}"); do face; sleep 0.4; done
    echo "$(date +%T) LOWHP health $hv: strikes $(( $(cnt '^THREAT start .* impact=0\.') - S0 )), keyless moves $(( $(cnt '^THREAT start .*keyless') - K0 )), grants $(( $(cnt '^FRONT grant') - G0 ))"
    [ "$hv" = 5 ] && m e06-lowhp 0
    grep -q "^PRACTICE t=.* orc is down" <(tail -300 "$R/somvr.log") && { echo "LOWHP: the orc is down"; break; }
  done
fi
if has blocks; then
  [ -n "${C16_BLOCKS_WORDS:-}" ] && { IFS=';' read -r -a BW <<< "$C16_BLOCKS_WORDS"; for bw in "${BW[@]}"; do w "$bw" 0.3; done; }
  spawn shield
  LUNGE_FN=lunge_edge
  for k in $(seq 1 "$BLOCKS_N"); do hit_it 4 && echo "$(date +%T) BLOCKS $k: $(last_hit)"; [ $k = 2 ] && { x "dump"; m e05-blocks 0; }; done
  LUNGE_FN=lunge
  grep -E '^COMBAT shield' "$R/somvr.log" | tail -4 | cut -c1-260
fi
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
