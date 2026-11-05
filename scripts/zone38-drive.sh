#!/bin/bash
# zone38-drive.sh - build 38 (som-live-block): the block zone (SOMVR_DEFENCE_ZONE), the parried orc's reel and the sword
# drawn from the stealth crouch (SOMVR_CROUCH_DRAW=keep), in one headless boot:
#   scripts/headless_vr.sh --win MOD with scripts/headset-build26.env + SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt.
# Z38_STEPS (default all, in order):
#   zone   an infantry: the zone's pictures (both eyes + the layer's dump) in two wind-ups (orange: shown; green: the test
#          blade across it), then `fight guard zone` for Z38_N strikes (the blade's middle put on the zone's centre at its
#          lock): blocked by the zone (DEFENCE contact ... the block zone) vs hit; pictures 0.15 / 0.6 / 1.2 s after the
#          first two blocks (the parried orc's reel)
#   miss   `fight guard zonemiss` for Z38_N strikes (the same 60 cm under the zone): not blocked
#   red    a shield orc, the blade low: pictures at two red wind-ups (no zone for an unblockable blow)
#   crouch (first: the sword still sheathed) the right stick pushed down (the crouch latched, Talion down); the sword
#          drawn from the shoulder: Talion stays down (CROUCH sword ... kept), pictures; the stick down again (up)
# Boot with SOMVR_DEFENCE_ZONE_MARKS=2: the zone's own pictures (marks at its lock and as the blade goes in: the frame
# itself, not a second later as the test file's dump).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${Z38_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${Z38_STEPS:-crouch zone miss red}
N=${Z38_N:-8}
T0=$(date +%s)
R=${Z38_RUN:-}
for i in $(seq 1 ${Z38_WAIT_N:-720}); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
userin() { # the user's own VR only (status.json "user": true): the launcher's tests run the somvr-headset unit too
  grep -q '"user": true' /run/user/1000/vr-session/status.json 2>/dev/null
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 200); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" "${3:-4}"; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
pic() { printf 'dump\n' > "$R/xrtest.txt"; printf '%s' "mark $1" > "$R/somvr.trigger"; echo "$(date +%T) picture $1"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 20))); do
  [ $((i % 20)) = 0 ] && userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.05; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
LOW="hands test R 30 -65 5 0 0 0"         # the arm down at the side: the blade out of every blow's path
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
put_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.4; w "$REST" 0.6; }
mark0() { C0=$(cnt '^DEFENCE contact'); Z0=$(cnt '^DEFENCE contact .*block zone'); H0=$(cnt '^DEFENCE hit'); S0=$(cnt '^DEFENCE zone orc=.* shown');
  B0=$(cnt '^TGUARD .*the blades met: blocked'); U0=$(cnt '^TGUARD .*not blocked'); G0=$(cnt '^TGUARD .* (zone|zonemiss) guard for'); }
phase() { echo "$(date +%T) PHASE $1: zones shown $(( $(cnt '^DEFENCE zone orc=.* shown') - S0 )), guards $(( $(cnt '^TGUARD .* (zone|zonemiss) guard for') - G0 )); contacts $(( $(cnt '^DEFENCE contact') - C0 )) (by the zone $(( $(cnt '^DEFENCE contact .*block zone') - Z0 ))), hits $(( $(cnt '^DEFENCE hit') - H0 )); the guard blocked $(( $(cnt '^TGUARD .*the blades met: blocked') - B0 )), not $(( $(cnt '^TGUARD .*not blocked') - U0 ))"; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1" 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$LOW" 1.5
m z00-start 0.3
if has crouch; then
  drawn && { put_slot; sleep 2; }
  K0=$(cnt '^CROUCH sword .*kept through'); U0=$(cnt '^CROUCH game up'); L0=$(cnt '^CROUCH sword .*let go'); B0=$(cnt '^CROUCH button')
  x "stick R 0 -1" 0.3; x "stick R 0 0" 3
  echo "$(date +%T) CROUCH: buttons $(( $(cnt '^CROUCH button') - B0 )); $(grep -E '^CROUCH (button|game down)' "$R/somvr.log" | tail -2 | cut -c1-140 | tr '\n' '|')"
  m z40-crouched-sheathed 0 2
  U0=$(cnt '^CROUCH game up')
  w "$SLOT" 1.2; w "hands squeeze R 1" 1.2; w "$OUT" 1.2; w "hands squeeze R 0" 1.5
  m z41-crouched-drawn 0 3
  echo "$(date +%T) CROUCH DRAW: kept $(( $(cnt '^CROUCH sword .*kept through') - K0 )), let go $(( $(cnt '^CROUCH sword .*let go') - L0 )), game ups since the draw $(( $(cnt '^CROUCH game up') - U0 )); drawn: $(drawn && echo yes || echo no)"
  grep -E '^(CROUCH|SLOTS .*sword)' "$R/somvr.log" | tail -8 | cut -c1-220
  x "stick R 0 -1" 0.3; x "stick R 0 0" 3
fi
if has zone; then
  w "practice spawn infantry" 2; w "fight guard off" 0.3; mark0
  for k in $(seq 1 ${Z38_LOW_N:-6}); do # the blade low: shown, not blocked (the first 2 marked at the lock: orange);
    # the strikes pass the zone (DEFENCE zone pass) and teach their records' spots (DEFENCE zone learn)
    waitfor '^DEFENCE zone orc=.* shown' 10 || { face; continue; }
    sleep 1.5; face; sleep 1.5
  done
  phase "zone pictures (the blade low: shown, not blocked)"
  w "fight guard zone" 0.3; mark0; rp=0
  for k in $(seq 1 "$N"); do
    waitfor '^TGUARD .* zone guard for' 10 || { face; w "practice spawn infantry" 2; continue; }
    if waitfor '^DEFENCE contact' 2 && [ $rp -lt 2 ]; then # the parried orc's reel: three marks over its 2 s
      rp=$((rp + 1)); m "z2${rp}a-reel" 0 0.5; m "z2${rp}b-reel" 0 0.5; m "z2${rp}c-reel" 0 0.5
    fi
    sleep 1; face
    grep -q "^PRACTICE t=.* orc is down" <(tail -300 "$R/somvr.log") && w "practice spawn infantry" 2
  done
  phase "zone guard (a point of the blade on the zone)"
  grep -E '^DEFENCE (contact|zone orc)' "$R/somvr.log" | tail -4 | cut -c1-330
  grep -E '^COMBAT reel .*after a clank' "$R/somvr.log" | tail -2 | cut -c1-500
fi
if has miss; then
  w "fight guard zonemiss" 0.3; mark0
  for k in $(seq 1 "$N"); do
    waitfor '^TGUARD .* zonemiss guard for' 10 || { face; w "practice spawn infantry" 2; continue; }
    sleep 1.5; face
    grep -q "^PRACTICE t=.* orc is down" <(tail -300 "$R/somvr.log") && w "practice spawn infantry" 2
  done
  phase "zonemiss guard (off the zone: the other side, 45 cm higher)"
  w "fight guard off" 0.3; w "$LOW" 0.5
fi
if has red; then
  w "practice spawn shield" 2; mark0; RD0=$(cnt '^THREAT red orc')
  for k in 1 2; do waitfor '^THREAT red orc' 14 && { sleep 0.2; pic "z3${k}-red-$k"; sleep 1; }; face; done
  echo "$(date +%T) RED: red warnings $(( $(cnt '^THREAT red orc') - RD0 ))"; phase "shield orc (the blade low)"
  w "practice remove" 2
fi
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
