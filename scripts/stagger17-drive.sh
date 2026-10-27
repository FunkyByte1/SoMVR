#!/bin/bash
# stagger17-drive.sh - build 17's stagger checks (handoff/HANDOFF-combat.md "build 17: stagger"; docs/re-xr-pipeline.md
# 58): one headless boot with SOMVR_COMBAT_STAGGER_CM set, the test hands (SOMVR_HANDS_TEST=1), the layer's test file,
# marks with both eyes. Prologue as combat16-drive.sh's. For each cap of S17_CAPS (the word `fight stagger CM`; default
# "0 55": before, after) on fresh practice orcs, every hit and clank logs its `COMBAT reel` line (the carry, the trace,
# the NPC movement's share, the next strike):
#   light    S17_LIGHT flat lunges (light hits) at an infantry, its health back to full before each (`fight health`)
#   medium   S17_MEDIUM edge-leading cuts (the committed cut: medium hits), the same
#   parry    `fight guard learn` (the first cap only: S17_LEARN strikes), then `fight guard path` for S17_PATH strikes:
#            the contact block's clanks (the parry's recoil) and the TGUARD blocked/unblocked counts
#   shield   S17_SHIELD edge-leading cuts at a shield orc's front (the physical shield's glance: its block recoil)
#   glance   S17_GLANCE edge-leading cuts at a shield orc's raised shield (`fight shield glance`: the game's block)
#   berserker S17_BERSERK edge-leading cuts at a berserker
# Marks (both eyes) S17_MARK_S s after the first hit of each kind per cap (the orc at its reel's end).
# S17_STEPS = any of "light medium parry shield glance berserker" (default all).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${S17_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
m() { [ "${S17_MARKS:-1}" = 1 ] || return 0; sleep "${2:-0.3}"; w "mark $1" 4; }
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
STEPS=${S17_STEPS:-light medium parry shield glance berserker}
NL=${S17_LIGHT:-4}; NM=${S17_MEDIUM:-4}; NS=${S17_SHIELD:-3}; NB=${S17_BERSERK:-3}; NG=${S17_GLANCE:-3}; LEARN_N=${S17_LEARN:-10}; PATH_N=${S17_PATH:-8}
MARK_S=${S17_MARK_S:-0.9}; WAIT_S=${S17_WAIT_S:-6}
# one hit with the lunge $1 (lunge | lunge_edge), the orc's health full first; a mark MARK_S s after the hit when $2
hit_once() { local fn=$1 k h0; h0=$(cnt '^COMBAT hit tier'); LUNGE_FN=$fn
  for k in $(seq 1 8); do
    face; w "stick 0 1 0 0.30" 0.5
    if [ "$fn" = lunge_edge ]; then w "hands test R 40 -8 35 0 -90 $RZ" 0.5; w "hands swing R -20 -18 62 0 90 $RZ 300" 0
    else w "$LUNGE_A" 0.5; w "$LUNGE" 0; fi
    for j in $(seq 1 8); do [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ] && break; sleep 0.125; done
    if [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ]; then
      [ -n "${2:-}" ] && { sleep "$MARK_S"; x "dump"; w "mark $2" 0.2; }
      w "$BEHIND" 0.3; LUNGE_FN=lunge; return 0
    fi
    sleep 0.6; w "$BEHIND" 0.4
  done; LUNGE_FN=lunge; return 1; }
reel_last() { grep -E '^COMBAT reel' "$R/somvr.log" | tail -1 | sed -E "s/^COMBAT reel orc=\S+ after //; s/ from where the hit found it//; s/the root's path/path/; s/the NPC movement's steps for it/vf/; s/ \(cm from the hit\/from the head\)//; s/from Talion's root/fromT/g; s/after the hit/after/"; }
spawn_n() { w "practice spawn $1" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5; }
FIRST=1
for cap in ${S17_CAPS:-0 55}; do
  w "fight stagger $cap" 0.5
  if has light; then
    spawn_n infantry
    for k in $(seq 1 "$NL"); do
      w "fight health 125" 0.3
      if hit_once lunge "$([ $k = 1 ] && echo "e1-light-cap$cap")"; then sleep "$WAIT_S"; echo "$(date +%T) LIGHT cap $cap: $(reel_last)"; else echo "LIGHT cap $cap: no hit"; fi
    done
  fi
  if has medium; then
    spawn_n infantry
    for k in $(seq 1 "$NM"); do
      w "fight health 125" 0.3
      if hit_once lunge_edge "$([ $k = 1 ] && echo "e2-medium-cap$cap")"; then sleep "$WAIT_S"; echo "$(date +%T) MEDIUM cap $cap: $(reel_last)"; else echo "MEDIUM cap $cap: no hit"; fi
    done
  fi
  if has parry; then
    ORC=infantry; spawn_n infantry; w "fight guard frame head" 0.5
    strikes() { local n=$1 i=0 miss=0
      while [ $i -lt "$n" ]; do
        if waitfor '^THREAT start .* impact=0\.[0-9]' 8; then i=$((i + 1)); miss=0; sleep 1.2; face
        else miss=$((miss + 1)); face; [ $miss -ge 2 ] && { spawn_n infantry; miss=0; }; fi
      done; }
    if [ $FIRST = 1 ]; then w "fight guard learn" 0.5; strikes "$LEARN_N"; fi
    w "fight guard path" 0.5
    C0=$(cnt '^DEFENCE clank orc'); H0=$(cnt '^DEFENCE hit'); B0=$(cnt '^TGUARD .*the blades met: blocked'); U0=$(cnt '^TGUARD .*not blocked'); RL0=$(cnt '^COMBAT reel .*a clank')
    PM=0
    for k in $(seq 1 "$PATH_N"); do
      w "fight health 125" 0.2; strikes 1
      if [ $PM = 0 ] && [ "$(cnt '^DEFENCE clank orc')" -gt "$C0" ]; then PM=1; sleep 0.3; x "dump"; w "mark e3-parry-cap$cap" 0.2; fi
    done
    w "fight guard off" 0.5; w "$BEHIND" 0.5; sleep "$WAIT_S"
    echo "$(date +%T) PARRY cap $cap: clanks $(( $(cnt '^DEFENCE clank orc') - C0 )), hits on Talion $(( $(cnt '^DEFENCE hit') - H0 )), blocked guards $(( $(cnt '^TGUARD .*the blades met: blocked') - B0 )), unblocked guards $(( $(cnt '^TGUARD .*not blocked') - U0 ))"
    grep -E '^COMBAT reel .*a clank' "$R/somvr.log" | tail -n +$((RL0 + 1)) | head -6 | sed -E "s/^COMBAT reel orc=\S+ after /PARRY cap $cap: /; s/ from where the hit found it//; s/the root's path/path/; s/the NPC movement's steps for it/vf/; s/ \(cm from the hit\/from the head\)//; s/from Talion's root/fromT/g; s/after the hit/after/"
  fi
  if has shield; then
    spawn_n shield; w "fight shield physical" 0.3
    for k in $(seq 1 "$NS"); do
      w "fight health 125" 0.3
      if hit_once lunge_edge "$([ $k = 1 ] && echo "e4-shield-cap$cap")"; then sleep "$WAIT_S"; echo "$(date +%T) SHIELD cap $cap: $(reel_last)"; else echo "SHIELD cap $cap: no hit"; fi
    done
  fi
  if has glance; then   # cuts into a raised shield: the game's block (the angle rule), its block recoil and the clank
    spawn_n shield; w "fight shield glance" 0.3
    for k in $(seq 1 "$NG"); do
      w "fight health 125" 0.3
      if hit_once lunge_edge "$([ $k = 1 ] && echo "e6-glance-cap$cap")"; then sleep "$WAIT_S"; echo "$(date +%T) GLANCE cap $cap: $(reel_last)"; else echo "GLANCE cap $cap: no hit"; fi
    done
    w "fight shield physical" 0.3
  fi
  if has berserker; then
    spawn_n berserker
    for k in $(seq 1 "$NB"); do
      w "fight health 125" 0.3
      if hit_once lunge_edge "$([ $k = 1 ] && echo "e5-berserker-cap$cap")"; then sleep "$WAIT_S"; echo "$(date +%T) BERSERKER cap $cap: $(reel_last)"; else echo "BERSERKER cap $cap: no hit"; fi
    done
  fi
  FIRST=0
done
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
