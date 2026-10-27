#!/bin/bash
# bite17-drive.sh - build 17's hit feel (docs/re-xr-pipeline.md 59, handoff/HANDOFF-feel.md "build 17: the bite"): one
# headless boot at the practice meadow, the test hands (SOMVR_HANDS_TEST=1), the layer's test file. Boot with build 17's
# env + SOMVR_COMBAT_HITLOG=2 SOMVR_COMBAT_HIT_MARKS=N (the eyes at the frame after each of the first N hits).
# B17_STEPS = any of (in this order):
#   kinds    an infantry; B17_N hits of each kind: cuts (the edge leading), flat slaps, stabs (a thrust along the blade),
#            its health set back before each (`fight health`): the COMBAT hit, BITE, HITFEEL, HITSND, HITFX lines
#   pics     the bite held long for the pictures (`fight bite hold 800 300 75`), slow edge cuts, each marked for the
#            layer's dumps 0.12 s and 0.5 s after (`fight bite dumps 1`; boot with SOMVR_XR_DUMP_MARKED=8):
#            with the FX, without (`fight hitfx off`), with the bite off (both eyes with the layer's quads:
#            scripts/xr_compose.py RUN -f FRAMES)
# Dumps: RUN/b17-steps.txt. Quits at the end, at once if the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B17_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B17_STEPS:-kinds pics}
N=${B17_N:-3}
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
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
fastwait() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 20))); do
  [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.05; done; userin && bail "the user is in VR"; return 1; }
# a dump as soon as a line matching $2 comes (in the background: the swing goes on meanwhile)
dmid() { ( fastwait "$2" 12 && { printf 'dump\n' > "$R/xrtest.txt"; sleep 1.5
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/b17-steps.txt"; } ) & }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(awk -v y="$y" 'BEGIN { y += 0; printf "%d", (y + (y < 0 ? -15 : 15)) / 30 }')
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
RZ=${B17_EDGE_RZ:-90}   # the edge leading: the grip rolled about its blade (the plain lunge sweeps the flat first)
CUT_MS=${B17_CUT_MS:-300}   # the cut's time (the pictures: 900, a 20 Hz frame then turns the hand ~10 deg)
cut_swing() { w "hands test R 40 -8 35 0 -90 $RZ" 0.5; w "hands swing R -20 -18 62 0 90 $RZ $CUT_MS" "$(awk -v m="$CUT_MS" 'BEGIN { print (m < 700 ? 1.0 : m / 1000 + 0.3) }')"; w "hands swing R 40 -8 35 0 -90 $RZ 300" 1.0; }
slap() { w "hands test R 40 -8 35 0 -90 0" 0.5; w "hands swing R -20 -18 62 0 90 0 300" 1.0; w "hands swing R 40 -8 35 0 -90 0 300" 1.0; }
stab() { w "hands test R 12 -18 5 0 0 0" 0.6; w "hands swing R 12 -18 75 0 0 0 150" 0.8; w "hands swing R 12 -18 5 0 0 0 400" 0.8; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
spawn() { w "practice spawn $1" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5; }
# swing $1 (cut|slap|stab) at the orc until a COMBAT hit lands (at most $2 tries); 0 = landed
hit_it() { local k h0; h0=$(cnt '^COMBAT hit tier'); for k in $(seq 1 "${2:-10}"); do
    [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ] && return 0
    face; w "fight health 125" 0.3; w "stick 0 1 0 0.30" 0.5; $1
  done; [ "$(cnt '^COMBAT hit tier')" -gt "$h0" ]; }
last_hit() { grep -E '^COMBAT hit tier' "$L" | tail -1 | sed -E "s/.*tier=([a-z]+)( \([a-z ,]+\))? .*edge=([0-9na]+) .*u=([0-9.]+) along=([0-9.]+).*rec='([^']+)'.*health ([0-9.]+->[0-9.]+).*/\1\2 edge \3 u \4 along \5 rec \6 health \7/"; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade low
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2
drawn || draw_slot
w "$BEHIND" 1.5
grep -E '^CONFIG hitfeel' "$L" | cut -c1-400
if has kinds; then
  spawn infantry; w "$BEHIND" 0.5
  for kind in cut_swing slap stab; do
    for k in $(seq 1 "$N"); do
      b0=$(cnt '^BITE t=.* start'); s0=$(cnt '^HITSND '); f0=$(cnt '^HITFX ')
      if hit_it $kind 10; then
        sleep 0.8
        echo "$(date +%T) KIND $kind $k: $(last_hit) | bites +$(( $(cnt '^BITE t=.* start') - b0 )) HITSND +$(( $(cnt '^HITSND ') - s0 )) HITFX +$(( $(cnt '^HITFX ') - f0 ))"
      else echo "$(date +%T) KIND $kind $k: no hit landed"; spawn infantry; fi
      w "$BEHIND" 0.8
    done
  done
  m e01-kinds 0.3
fi
if has pics; then
  # the bite held long (800 ms, torn free past 75 deg) and a slow cut, so a dump inside the hold shows it; each landed
  # cut marked for the layer's dumps (SOMVR_XR_DUMP_MARKED) 0.12 s after it (in the hold) and 0.5 s after (the FX grown)
  has kinds || { spawn infantry; w "$BEHIND" 0.5; }
  w "fight bite hold 800 300 75" 0.5; CUT_MS=1200
  pic() { local d0; d0=$(grep -c 'dump layers frame [0-9]*: eye 0 pose' "$R/xr-layer.log"); w "fight bite dumps 1" 0.3
    local k; for k in 1 2 3; do hit_it cut_swing 8 || break; echo "$(date +%T) PIC $1: $(last_hit)"
      last_hit | grep -q '^heavy' && break; w "$BEHIND" 0.8; done; sleep 2
    echo "$1 $(grep -o 'dump layers frame [0-9]*: eye 0 pose' "$R/xr-layer.log" | tail -n +$((d0 + 1)) | grep -o 'frame [0-9]*' | cut -d' ' -f2 | tr '\n' ' ')" | tee -a "$R/b17-steps.txt"
    w "$BEHIND" 0.8; }
  pic bite-fx
  w "fight hitfx off" 0.3; pic bite-nofx; w "fight hitfx on" 0.3
  w "fight bite off" 0.3; pic nobite-fx
  w "fight bite on" 0.3; w "fight bite hold 60 100 30" 0.3; CUT_MS=300
  m e02-pics 0.3
fi
echo "== BITE / HITFEEL lines"
grep -E '^BITE t=' "$L" | tail -30 | cut -c1-260
grep -E '^HITFEEL t=' "$L" | tail -20 | cut -c1-260
echo "== HITSND names after our hits (kind, name: count)"
grep -oE "^HITSND t=[0-9.]+ '[^']+'.*our [a-z]+ hit" "$L" | sed -E "s/^HITSND t=[0-9.]+ '([^']+)'.*our ([a-z]+) hit/\2 \1/" | sort | uniq -c | sort -k2,2 -k1,1nr
echo "== HITFX names after our hits"
grep -oE "^HITFX t=[0-9.]+ fx '[^']+' on [a-zA-Z]+.*our [a-z]+ hit" "$L" | sed -E "s/^HITFX t=[0-9.]+ fx '([^']+)' on ([a-zA-Z]+).*our ([a-z]+) hit/\3 \2 \1/" | sort | uniq -c
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
