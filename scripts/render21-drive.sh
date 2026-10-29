#!/bin/bash
# render21-drive.sh - build 21's render round (handoff/HANDOFF-render.md, docs/re-xr-pipeline.md 86): drives one headless
# boot (scripts/headless_vr.sh with scripts/headset-build21.env, SOMVR_HANDS_TEST=1, SOMVR_FX_TEST=1, the layer's test
# file) to the build 20c session's two places and marks both eyes there (marks/<HHMMSS_n>/eyes.jpg):
#   fire   the campfire of the session's mark 16 (Talion 30873.5 3842.5 -32973.7, the head's yaw -13.5 deg): the game's
#          look, `fx eyesame 1` (the right eye from the left's pose: what still differs is state carried between the
#          two render calls), each ClientFX renderer hidden in turn (`fx hide 1|2|4|8`), SOMVR_FLATFX all
#   sun    the session's mark 12 (Talion 31805.7 4820.2 -30537.7, the head's yaw 22.8 deg, toward the low sun): the
#          game's look, SOMVR_FLATFX all, each part alone, the lens flares' and the flare sprites' draws hidden
#   fire2  the campfire of the session's mark 15 (Talion 32229.7 3709.1 -31856.8, yaw -110.5 deg): the same words
#   torch  run 203003's lit torch (the overcast SON_Day03), lossless both-eye pictures (RD_PPM, default /tmp/b21-render/ppm)
#   camp   the mark-16 campfire with the practice orc out
#   grow   `fx room ${RD_ROOM:-16}` (the request buffer both eyes share, grown); torchB, campB = torch, camp after it
# RD_STEPS (default "torch camp grow torchB campB sun"); RD_WEATHER (default SON_Day01, the session's sunny weather) for the sun; RD_FIX="WORDS;WORDS" = extra marks at the fire with those words (`;` between marks,
# `,` between words). BG_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${RD_STEPS:-torch camp grow torchB campB sun}
T0=$(date +%s)
R=${BG_RUN:-}
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
# a mark; prints its folder
m() { local n0; n0=$(ls "$R/marks" 2>/dev/null | wc -l); sleep "${2:-0.6}"; w "mark $1" 3.5
  echo "$(date +%T) mark $1 -> $(ls -t "$R/marks" 2>/dev/null | head -1)"; }
# a mark whose both-eye picture is also kept lossless (the layer's eyes.ppm, copied before boot_check makes the jpg)
PPM=${RD_PPM:-/tmp/b21-render/ppm}
mp() { local n0 d i; n0=$(ls "$R/marks" 2>/dev/null | wc -l); sleep "${2:-0.6}"; w "mark $1" 0.05
  for i in $(seq 1 100); do d=$(ls -t "$R/marks" 2>/dev/null | head -1); [ "$(ls "$R/marks" | wc -l)" -gt "$n0" ] && \
    [ -s "$R/marks/$d/eyes.ppm" ] && break; sleep 0.1; done
  sleep 0.2; mkdir -p "$PPM"; cp "$R/marks/$d/eyes.ppm" "$PPM/$1.ppm" 2>/dev/null && echo "$(date +%T) mark $1 -> $d (+ $PPM/$1.ppm)" \
    || echo "$(date +%T) mark $1 -> $d (no ppm)"; sleep 2.5; }
# the newest mark's head yaw (deg) and frame rate line
lastyaw() { sed -n 's/^head .* yaw \(-\?[0-9.]*\) deg.*/\1/p' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt" | head -1; }
lastrate() { grep -m1 '^frame rate' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt"; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 -30 0 0 0"
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
w "flatfx state" 0.5
# go to X Y Z and face the head's world yaw YAW (deg) pitched P: the test head's turn found from a mark's yaw
TURN=0
goface() {
  local a0 f0 y d; a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170
  sleep "${7:-3}"; x "head 0 0 0 yaw 0 pitch 0" 1; m "$5-yaw0" 0.2; y=$(lastyaw); echo "yaw now $y (want $4)"
  d=$(python3 -c "d=($4)-($y); d=(d+180)%360-180; print(round(d,1))")
  x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned" 0.2; y2=$(lastyaw); echo "yaw now $y2 after +$d"
  # the wrong way round only if the error grew (run 203003: +53.8 took -67.3 to -6.3, 7 deg off but the right way)
  if python3 -c "import sys; e=lambda a: abs((($4)-(a)+180)%360-180); sys.exit(0 if e($y2) > e($y) else 1)"; then
    d=$(python3 -c "print(-($d))"); x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned2" 0.2; y=$(lastyaw); echo "yaw now $y after $d"
  fi
  TURN=$d
}
for s in $STEPS; do
  case $s in
  fire)
    echo "== fire"; w "flatfx game" 0.3; w "fx hide 0" 0.3; w "fx eyesame 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 fire -12 12
    w "flatfx state" 0.3
    m fire-game 0.5; lastrate; m fire-game2 0.4
    w "fx eyesame 1" 0.5; m fire-eyesame 0.5; m fire-eyesame2 0.4
    w "fx eyesame 0" 0.5
    for h in 1 2 4 8; do w "fx hide $h" 0.5; m "fire-hide$h" 0.5; done; w "fx hide 0" 0.3
    w "fx hide 15" 0.5; m fire-hide15 0.5; w "fx hide 0" 0.3
    w "flatfx all" 0.5; m fire-flatfx 0.5; lastrate
    if [ -n "${RD_FIX:-}" ]; then
      IFS=';' read -ra FX <<< "$RD_FIX"
      for f in "${FX[@]}"; do IFS=',' read -ra WS <<< "$f"; for ww in "${WS[@]}"; do w "$ww" 0.4; done
        m "fire-fix-$(echo "$f" | tr -c 'a-z0-9' '_' | cut -c1-30)" 0.5; done
    fi
    w "flatfx game" 0.3 ;;
  fire2)
    echo "== fire2"; w "flatfx game" 0.3; w "fx hide 0" 0.3; w "fx eyesame 0" 0.3
    goface 32229.7 3760 -31856.8 -110.5 fire2 -6 12
    m fire2-game 0.5; w "fx eyesame 1" 0.5; m fire2-eyesame 0.5; w "fx eyesame 0" 0.5
    for h in 1 2 4 8; do w "fx hide $h" 0.5; m "fire2-hide$h" 0.5; done; w "fx hide 0" 0.3
    w "flatfx state" 0.3 ;;
  torch|torchB)   # the torch on the ruin's wall (run 203003's fire-game view), lit in the overcast SON_Day03; lossless marks
    echo "== $s"; w "flatfx game" 0.3; w "fx hide 0" 0.3; w "fx eyesame 0" 0.3
    [ $s = torch ] && w "fx console GameServerShell.ChangeWeather ${RD_TORCH_WEATHER:-SON_Day03}" 8
    goface 30873.5 3900 -32973.7 -125.8 $s -8 8
    mp $s-game; mp $s-game2 0.3
    w "fx eyesame 1" 0.5; mp $s-same; mp $s-same2 0.3; w "fx eyesame 0" 0.5
    w "fx hide 1" 0.5; mp $s-hide1; w "fx hide 2" 0.5; mp $s-hide2; w "fx hide 0" 0.5
    w "flatfx state" 0.3 ;;
  camp|campB)    # the session's mark-16 campfire with the practice orc out (the session had it out; our earlier runs: unlit)
    echo "== $s"; w "flatfx game" 0.3; w "fx hide 0" 0.3; w "fx eyesame 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 $s -12 3
    w "practice spawn infantry" 10; mp $s-orc; x "head 0 0 0 yaw $TURN pitch -12" 1; mp $s-game
    w "fx eyesame 1" 0.5; mp $s-same; w "fx eyesame 0" 0.5
    w "fx hide 1" 0.5; mp $s-hide1; w "fx hide 2" 0.5; mp $s-hide2; w "fx hide 12" 0.5; mp $s-hide12; w "fx hide 0" 0.5
    w "practice remove" 2; w "flatfx state" 0.3 ;;
  grow)    # SOMVR_RENDER_ROOM_MB live: the renderer's per-frame request buffer grown (from the game's 4 MB)
    echo "== grow"; w "fx room ${RD_ROOM:-16}" 3; w "flatfx state" 0.5 ;;
  sun)
    echo "== sun"; w "flatfx game" 0.3; w "fx hide 0" 0.3
    w "fx console GameServerShell.ChangeWeather ${RD_WEATHER:-SON_Day01}" 8
    goface 31805.7 4880 -30537.7 22.8 sun 4
    m sun-game 0.5; lastrate
    w "flatfx all" 0.5; m sun-flatfx 0.5; lastrate
    for p in flares shafts vignette; do w "flatfx $p" 0.5; m "sun-$p" 0.5; done
    w "flatfx game" 0.3; w "fx hide 8" 0.5; m sun-hide8 0.5; w "fx hide 16" 0.5; m sun-hide16 0.5; w "fx hide 0" 0.3
    w "flatfx all" 0.3 ;;
  esac
done
w "flatfx state" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
