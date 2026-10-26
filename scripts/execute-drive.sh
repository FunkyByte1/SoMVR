#!/bin/bash
# execute-drive.sh - SOMVR_EXECUTE's proof (handoff/HANDOFF-execute.md): one headless boot with build 11's switches +
# SOMVR_EXECUTE=1, the test hands (SOMVR_HANDS_TEST=1), the layer's test file, marks with both eyes. Prologue as
# combat11-drive.sh's. Per round (XD_ROUNDS, default 2): a practice infantry; light slashes back and forth across the
# front (a 70 deg arc in XD_LIGHT_MS, default 600 ms: no heavy hit, 10 damage each; the game's streak lapses 3 s after
# the last hit, so a step in and two slashes every ~2 s) until the game's charge fills (`EXECUTE ready`); the mod's mark of the ready glow (SOMVR_EXECUTE_GLOW
# as booted: round 1 `game` = what the game shows by itself), then `execute glow fx` and a mark of our glow; the right
# trigger held (`btn R trigger 1`), a heavy lunge = the execution (`EXECUTE kill`), the mod's mark of the kill, a mark
# 2.5 s later (the body), the trigger let go. Round 2 on: the fx glow at the ready mark too. XD_LIGHT_MS: the light swing's time (default 1000; longer when a "light"
# swing came out heavy).
# Build 12 (handoff/HANDOFF-build12.md): XD_LHAND=1 the layer's left hand tracked too (`hand L head`, as the hands
# test's: our VR left hand and the wisps where a player's hand is); XD_BOW=1 right after each kill the left trigger
# (the bow) for 3 s while the execution's slow motion still runs (`EXECUTE slow motion over` says whose the timer is);
# XD_FAIR="ROUNDS" (e.g. "1 2", or all) those rounds without the test chase: after each light slash the player's own way to the orc, `execute where` (the distance and
# bearing, nothing moves), `execute face` past 20 deg (= the snap turns), then the stick forward for the distance
# over 1.1 m at 230 cm/s (FAIR lines; XD_FAIR_N slashes a round at most, default 36); the execution as before.
# Round 2 (SOMVR_EXECUTE_LAPSE_S): XD_READY_WAIT="S1 S2 ..." waits S seconds (per round; the last value for the rest)
# after ready before the step in and the trigger: a player's time to see the glow, aim and hold the trigger (in a chase
# round the chase stops at ready; the step in and the heavy swings then go the XD_FAIR way).
# XD_KITE=1: during the wait the player keeps out of the orc's reach instead of standing (`execute where`, `execute face`
# past 20 deg, the stick back 0.7 s while it is within 3 m): the orc's blows on Talion reset the streak (the game's rule).
# XD_NEXT=1: the charge spent on the next orc (the counter is Talion's: a kill does not end it): at ready the orc is
# removed (`practice remove`: no blows during the wait), after the wait a new infantry (2 s) and the test chase to it,
# the execution - the full charge's window measured without the orc's blows (headless the orc out-walks the stick's kite,
# and a new orc circles before it closes in: run 131633).
# XD_GUARD=1: the test guard (`fight guard`, somvr_fight_game.c: a player's block at the orc's telegraph) - round 1
# learns the orc's strikes (`learn`: no blocks), rounds 2 on block them (`path`).
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${XD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
ROUNDS=${XD_ROUNDS:-2}; LMS=${XD_LIGHT_MS:-600}
T0=$(date +%s)
R=${XD_RUN:-}   # XD_RUN=RUN: drive a running boot (a restarted driver)
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
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LUNGE_A="hands test R 40 -8 35 0 -90 0"
SHOW="hands test R 22 -25 40 75 -15 0"   # the blade nearly upright ahead, a little right: in full view (the glow)
drawn() { local l; l=$(tail -800 "$R/somvr.log" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# a swing from LUNGE_A across the front to the left, MS long (300: heavy; LMS: light), then back the slow way
LA="hands test R 30 -10 45 0 -35 0"   # the light slashes' ends: the blade 35 deg right / left of ahead
slash() { w "hands swing R -10 -10 50 0 35 0 $LMS" 0.55; w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.55; }
swing() { w "$LUNGE_A" 0.5; w "hands swing R -20 -18 62 0 90 0 $1" $(( $1 / 1000 + 1 )); w "hands swing R 40 -8 35 0 -90 0 900" 1.0; }
# the test chase (`execute chase on`, SOMVR_HANDS_TEST): the mod turns and walks the player to the nearest orc each frame;
# the nearest orc (`execute face`: the snap turns toward it at once), then walk in to ~1.1 m (the stick: 230 cm/s at most);
# none = a new infantry
approach() { local l d s; w "execute face" 0.4; l=$(grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1)
  case $l in *" 0 orcs within"*) spawn; return 0 ;; esac
  d=$(echo "$l" | grep -oE 'dist [0-9]+' | cut -d' ' -f2); [ -n "$d" ] || return 0
  s=$(awk -v d="$d" 'BEGIN { s = (d - 110) / 200; if (s < 0) s = 0; if (s > 1.5) s = 1.5; printf "%.2f", s }')
  [ "$s" != "0.00" ] && w "stick 0 1 0 $s" 0.3; return 0; }   # (the slash starts while he still walks in)
# XD_FAIR: one light slash, then the step in (the distance from the head; 230 cm/s at full stick)
FK=0
fair() { case " ${XD_FAIR:-} " in *" $rd "*|*" all "*) return 0 ;; esac; return 1; }
fair_step() { local l d b s h0 h1; h0=$(cnt '^COMBAT hit '); FK=$((FK + 1))
  if [ $((FK % 2)) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 $LMS" 0.6; else w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.6; fi
  h1=$(cnt '^COMBAT hit '); fair_in "slash $FK: hits +$((h1 - h0)), the streak $(streak)"; }
fair_in() { local l d b s; w "execute where" 0.25; l=$(grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1)
  case $l in *" 0 orcs within"*) w "execute where" 0.5; l=$(grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1) ;; esac   # (once more: the band can miss a live orc for a frame)
  case $l in *" 0 orcs within"*) echo "FAIR $(date +%T.%N | cut -c1-12) $1: no orc"; spawn; return 0 ;; esac
  d=$(echo "$l" | grep -oE 'dist [0-9]+' | cut -d' ' -f2); b=$(echo "$l" | grep -oE 'bearing [+-]?[0-9]+' | awk '{print $2}')
  echo "FAIR $(date +%T.%N | cut -c1-12) $1, the orc ${d:-?} cm at ${b:-?} deg"
  [ -n "$b" ] && [ "${b#[-+]}" -gt 20 ] && w "execute face" 0.3
  s=$(awk -v d="${d:-0}" 'BEGIN { s = (d - 110) / 230; if (s < 0) s = 0; if (s > 2) s = 2; printf "%.2f", s }')
  [ "$s" != "0.00" ] && w "stick 0 1 0 $s" "$s"; return 0; }
kite() { local t1 l d b; t1=$(( $(date +%s%N) / 1000000 + ${1%.*} * 1000 ))   # (whole seconds)
  while [ "$(( $(date +%s%N) / 1000000 ))" -lt "$t1" ]; do
    w "execute where" 0.25; l=$(grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1)
    d=$(echo "$l" | grep -oE 'dist [0-9]+' | cut -d' ' -f2); b=$(echo "$l" | grep -oE 'bearing [+-]?[0-9]+' | awk '{print $2}')
    [ -n "$b" ] && [ "${b#[-+]}" -gt 20 ] && w "execute face" 0.3
    if [ -n "$d" ] && [ "$d" -gt 0 ] && [ "$d" -lt 300 ]; then echo "KITE $(date +%T.%N | cut -c1-12) the orc $d cm: back"; w "stick 0 -1 0 0.7" 0.7; else sleep 0.3; fi
  done; }
spawn() { w "practice spawn infantry" 3   # (a failed spawn - no ground ahead: Talion walked to an edge, run 133610 - the blink back first)
  if grep -E '^PRACTICE t=.*spawn Infantry' "$R/somvr.log" | tail -1 | grep -q 'FAILED'; then
    echo "$(date +%T) the spawn failed (no ground ahead): back to the practice area"; w "practice go" 6; w "practice spawn infantry" 3
    if grep -E '^PRACTICE t=.*spawn Infantry' "$R/somvr.log" | tail -1 | grep -q 'FAILED'; then   # (still: turned round)
      for i in 1 2 3 4 5 6; do w "turn+" 0.6; done; w "practice spawn infantry" 3; fi; fi; }
streak() { local v; v=$(grep -E '^EXECUTE streak t=' "$R/somvr.log" | tail -1 | awk '{print $4}'); echo "${v:-0}"; }
ready() { [ "$(cnt '^EXECUTE ready t=')" -gt "$RD0" ]; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
[ "${XD_LHAND:-0}" = 1 ] && x "hand L head -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
w "execute status" 1
m x00-start 0.3
for rd in $(seq 1 "$ROUNDS"); do
  echo "$(date +%T) ROUND $rd"
  RD0=$(cnt '^EXECUTE ready t='); K0=$(cnt '^EXECUTE kill t=')
  spawn
  [ "${XD_GUARD:-0}" = 1 ] && { [ "$rd" = 1 ] && w "fight guard learn" 0.5 || w "fight guard path" 0.5; }
  # 1. the streak: light slashes, walking in before each pair (the light stagger steps the orc back ~1.4 m)
  w "$LA" 0.5; fair || w "execute chase on" 0.5
  for k in $(seq 1 90); do
    ready && break
    fair && [ "$k" -gt "${XD_FAIR_N:-36}" ] && break   # (XD_FAIR_N slashes at most)
    fair && { fair_step; continue; }
    H0=$(cnt '^COMBAT hit tier=heavy')
    # one slash at a time, a check after each (the 10th light hit kills an infantry: 100 health)
    if [ $((k % 2)) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 $LMS" 0.6; else w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.6; fi
    # from the 7th hit on: wait for its result before the next slash (the 9th light hit already kills some infantry)
    [ "$(streak)" -ge 7 ] && for j in 1 2 3 4; do ready && break; sleep 0.2; done
    [ $((k % 10)) = 0 ] && approach   # (a check: no orc = a new infantry)
    [ "$(cnt '^COMBAT hit tier=heavy')" -gt "$H0" ] && { LMS=$((LMS + 100)); echo "a heavy hit: the light swing now $LMS ms"; }
  done
  ready || { echo "never ready (round $rd)"; grep -E '^EXECUTE streak' "$R/somvr.log" | tail -5 | cut -c1-200; continue; }
  grep -E '^EXECUTE ready t=' "$R/somvr.log" | tail -1 | cut -c1-250
  RW=$(echo "${XD_READY_WAIT:-0}" | awk -v r="$rd" '{ print (r <= NF) ? $r : $NF }')
  if [ "$RW" != 0 ]; then   # (a chase round: the chase off first; then the player's own step in, as XD_FAIR's)
    fair || w "execute chase off" 0.3
    [ "${XD_GUARD:-0}" = 1 ] && w "fight guard off" 0.3   # (the guard's swing can land the 9th hit: a dead infantry, run 123926)
    [ "${XD_NEXT:-0}" = 1 ] && w "practice remove" 0.5
    echo "$(date +%T) READY_WAIT $RW s (a player sees the glow and aims)"
    if [ "${XD_KITE:-0}" = 1 ] && [ "${XD_NEXT:-0}" != 1 ]; then kite "$RW"; else sleep "$RW"; fi
    if [ "${XD_NEXT:-0}" = 1 ]; then   # (the test chase as the legs to the new orc: the window is measured, not the aim)
      w "practice spawn infantry" 2; w "execute chase on" 0.3
    else
      fair || fair_in "ready (after the wait)"
    fi
  fi
  # 2. at once (the game's streak, and the charge with it, lapses 2.5-3 s after the last hit): the right trigger held,
  # a heavy lunge = the execution (the mod marks the ready glow 0.5 s after ready, and the kill)
  # at once: the right trigger held; the blade up in full view (round in XD_GLOWMARK: a mark of the glow - then the mod's
  # kill mark falls within the marks' 2 s and is skipped), a heavy chop from there = the execution
  # (the test chase walks on until the kill - the orc steps back 1.4 m from each light hit - and stops by itself at the
  # kill: no live orc; the eye trace's frames after the kill carry no test walking)
  fair && fair_in "ready"
  printf '%s\n' "btn R trigger 1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: btn R trigger 1"; sleep 0.25
  w "$SHOW" 0.4
  case " ${XD_GLOWMARK:-1 3} " in *" $rd "*) w "mark x0${rd}a-ready-glow-$([ "$rd" = 1 ] && echo "${XD_GLOW1:-game}" || { [ -n "${XD_FX:-}" ] && echo fx || echo game; })" 0.3 ;; esac
  w "hands swing R -20 -18 62 0 90 0 300" 1.0
  for k in $(seq 1 6); do
    [ "$(cnt '^EXECUTE kill t=')" -gt "$K0" ] && break
    { fair || { [ "$RW" != 0 ] && [ "${XD_NEXT:-0}" != 1 ]; }; } && fair_in "heavy $k"
    swing 300
  done
  if [ "${XD_BOW:-0}" = 1 ]; then   # the bow at once, inside the 0.5 s slow motion (the trigger let go, the left one held)
    printf '%s\n' "btn R trigger 0" "btn L trigger 1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: R trigger 0, L trigger 1"
    sleep 3; x "btn L trigger 0"; sleep 1.5
  fi
  x "btn R trigger 0"; w "execute chase off" 0.5
  grep -E '^EXECUTE kill t=' "$R/somvr.log" | tail -1 | cut -c1-600
  w "$SHOW" 0.5; w "stick 0 -1 0 0.8" 1.5; m "x0${rd}c-after" 0   # two steps back: the body in view, the blade (spent)
  w "$BEHIND" 0.5
  waitfor '^COMBAT death orc' 6; grep -E '^COMBAT death orc' "$R/somvr.log" | tail -1 | cut -c1-330
  w "execute status" 1
  [ "$rd" = 1 ] && [ -n "${XD_FX:-}" ] && w "execute glow fx" 0.5   # XD_FX: round 2 on our ClientFX too
done
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
