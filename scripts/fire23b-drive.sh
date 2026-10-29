#!/bin/bash
# fire23-drive.sh - build 23's fire round (docs/re-xr-pipeline.md 96): fire22-drive.sh's boot, helpers and steps, plus the
# D3D11 capture (`fx d3d N`, SOMVR_FX_TEST): eye 1's draws against eye 0's, the DCAP lines. Boot with
# scripts/headset-build23.env, SOMVR_HANDS_TEST=1, SOMVR_FX_TEST=1, SOMVR_PRACTICE_DAY=0 (fires light).
# FX_STEPS (default "cap vcap"), fire22's steps and:
#   cap     the build 20c session's mark-16 campfire (lit, the practice orc out), `fx eyesame 1`: `fx d3d 2`, then with
#           `fx sync 1` `fx d3d 1`; lossless dumps of each; a stereo mark
#   vcap    the wraith vision (Y held) 60 deg up, `fx eyesame 1`: `fx d3d 2`, then with `fx sync 1` `fx d3d 1`
#   fix3    fix's A/B with the orc removed before it hurts Talion; FIX_TORCH=1 adds the ruin's torch (yaw -125.8; run
#           070912's turn missed it)
#   fix     the campfire and the vision with FIX_WORDS (the words of a fix under test) against FIX_OFF_WORDS: eyesame and
#           stereo dumps; the vision also with FIX2_WORDS; FIX_CAP=1 adds a capture with the fix on
# BG_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${FX_STEPS:-cap vcap}
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
PPM=${FX_PPM:-/tmp/b23/render/ppm}
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

dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\ndump 2\ndump 3\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 100); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 3)) ] && break; sleep 0.25; done
  echo "$1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')" | tee -a "$R/fx-steps.txt"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
for s in $STEPS; do
  case $s in
  torch)   # the torch on the ruin's wall, lit in the overcast SON_Day03 (boot with SOMVR_PRACTICE_DAY=0: the trip keeps it)
    echo "== torch"; w "fx hide 0" 0.3; w "fx eyesame 0" 0.3; w "curframe game" 0.3
    goface 30873.5 3900 -32973.7 -125.8 torch -8 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 12
    dq torch-game; m torch-game 0.2; lastrate
    w "fx eyesame 1" 1; w "fx trace 1" 1.5; dq torch-same; w "curframe state" 0.3
    w "curframe eye" 3; dq torch-same-eye; w "curframe state" 0.3
    w "fx eyesame 0" 1; dq torch-eye; m torch-eye 0.2; lastrate
    w "curframe game" 3; dq torch-game2; w "curframe state" 0.3 ;;
  camp)    # the session's mark-16 campfire with the practice orc out (overcast SON_Day03; SOMVR_PRACTICE_DAY=0)
    echo "== camp"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 10; x "head 0 0 0 yaw $TURN pitch -12" 2
    w "fx eyesame 1" 1; dq camp-same; w "curframe state" 0.3
    w "curframe eye" 3; dq camp-same-eye; w "curframe state" 0.3
    w "fx sync 1" 3; dq camp-same-eye-sync; w "curframe state" 0.3
    w "curframe game" 3; dq camp-same-sync; w "curframe state" 0.3
    w "fx sync 0" 1; w "fx eyesame 0" 1
    w "curframe eye" 3; dq camp-eye; m camp-eye 0.2; lastrate
    w "curframe game" 3; dq camp-game; m camp-game 0.2; lastrate
    w "practice remove" 2; w "curframe state" 0.3 ;;
  vb)      # the campfire: the dynamic vertex buffer's ranges re-hashed at each present (the counts in the state lines)
    echo "== vb"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 10; x "head 0 0 0 yaw $TURN pitch -12" 2
    w "curframe state" 0.3; dq vb-game; m vb-game 0.2; w "curframe state" 0.3
    w "fx eyesame 1" 2; dq vb-same; w "curframe state" 0.3
    w "fx vbgrow 4" 3; dq vb-same-grow; w "curframe state" 0.3
    w "curframe eye" 3; dq vb-same-grow-eye; w "curframe state" 0.3
    w "fx eyesame 0" 2; dq vb-grow-eye; m vb-grow-eye 0.2; lastrate
    w "curframe game" 2; dq vb-grow; m vb-grow 0.2; lastrate
    w "practice remove" 2; w "curframe state" 0.3 ;;
  bb)      # the campfire: the HDR state's blend factor (+0xc) zeroed for both eyes (`fx bloomblend 0`), eyesame and stereo
    echo "== bb"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 10; x "head 0 0 0 yaw $TURN pitch -12" 2
    w "fx eyesame 1" 2; dq bb-same
    w "fx bloomblend 0" 3; dq bb-same-bb0
    w "curframe eye" 3; dq bb-same-bb0-eye
    w "fx eyesame 0" 2; dq bb-bb0-eye; m bb-bb0-eye 0.2
    w "fx bloomblend game" 1; w "curframe game" 2; dq bb-game; m bb-game 0.2
    w "practice remove" 2; w "flatfx state" 0.3; w "curframe state" 0.3 ;;
  vbg)     # the campfire: the custom draws' four dynamic vertex buffers made 8x bigger (`fx vbgrow 8`), eyesame before/after
    echo "== vbg"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 10; x "head 0 0 0 yaw $TURN pitch -12" 2
    w "fx eyesame 1" 2; dq vbg-same; w "curframe state" 0.3
    w "fx vbgrow 8" 4; dq vbg-same-grow; w "curframe state" 0.3; sleep 2; dq vbg-same-grow2
    w "fx eyesame 0" 2; dq vbg-grow; m vbg-grow 0.2; lastrate
    w "practice remove" 2; w "flatfx state" 0.3; w "curframe state" 0.3 ;;
  cap)     # the campfire, eyesame: the D3D11 capture without and with `fx sync 1`
    echo "== cap"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 10; x "head 0 0 0 yaw $TURN pitch -12" 2
    w "fx eyesame 1" 2; dq cap-same
    w "fx d3d 2" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"
    dq cap-same2
    w "fx sync 1" 3; dq cap-same-sync
    w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"
    w "fx sync 0" 1; w "fx eyesame 0" 2; dq cap-stereo; m cap-stereo 0.2; lastrate
    w "practice remove" 2 ;;
  vcap)    # the wraith vision 60 deg up, eyesame: the D3D11 capture without and with `fx sync 1`
    echo "== vcap"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    x "head 0 0 0 yaw 0 pitch 60" 2
    x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 2
    w "fx eyesame 1" 2; dq vcap-same
    w "fx d3d 2" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"
    w "fx sync 1" 3; dq vcap-same-sync
    w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"
    w "fx sync 0" 1; w "fx eyesame 0" 2; dq vcap-stereo; m vcap-stereo 0.2
    x "btn L b 0" 0.4; sleep 2 ;;
  fix3)    # the k_fTime A/B (FIX_OFF_WORDS / FIX_WORDS): the campfire (the orc spawned for the camp, removed before it
           # hurts Talion - a hurt Talion loses eye 0's image in this spot), the ruin's torch, the vision; eyesame + stereo
    echo "== fix3"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 16; w "practice remove" 1; w "practice health" 1
    x "head 0 0 0 yaw $TURN pitch -12" 2; sleep ${FIX_WEATHER_WAIT:-30}; m fix3-camp-lit 0.2; lastrate
    for p in camp; do
      w "fx eyesame 1" 2; for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-$p-same-off
      for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-$p-same-on; sleep 1; dq fix3-$p-same-on2
      w "fx eyesame 0" 2; dq fix3-$p-stereo-on; m fix3-$p-on 0.2; lastrate
      for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-$p-stereo-off; m fix3-$p-off 0.2
      for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done
    done
    [ -n "${FIX_CAP:-}" ] && { w "fx eyesame 1" 2; w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"; w "fx eyesame 0" 1; }
    d=$(python3 -c "print(round(-125.8 - (-13.5) + $TURN, 1))"); [ -n "${FIX_TORCH:-}" ] && { x "head 0 0 0 yaw $d pitch -8" 2; m fix3-torch-turned 0.2; }
    for p in ${FIX_TORCH:+torch}; do
      w "fx eyesame 1" 2; for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-$p-same-off
      for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-$p-same-on; sleep 1; dq fix3-$p-same-on2
      w "fx eyesame 0" 2; dq fix3-$p-stereo-on; m fix3-$p-on 0.2
      for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-$p-stereo-off; m fix3-$p-off 0.2
      for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done
    done
    x "head 0 0 0 yaw 0 pitch 60" 2
    x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 2
    w "fx eyesame 1" 2; for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-vision-same-off
    for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-vision-same-on; sleep 1; dq fix3-vision-same-on2
    for fw in $FIX2_WORDS; do w "${fw//_/ }" 1; done; sleep 3; dq fix3-vision-same-on2w; sleep 1; dq fix3-vision-same-on2w2
    w "fx eyesame 0" 2; dq fix3-vision-stereo-on2w; m fix3-vision-on2w 0.2
    for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 2
    dq fix3-vision-stereo-on; m fix3-vision-on 0.2; lastrate
    for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix3-vision-stereo-off; m fix3-vision-off 0.2
    for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done
    x "btn L b 0" 0.4; sleep 2 ;;
  fix)     # FIX_OFF_WORDS / FIX_WORDS (words, _ for a space): the campfire and the vision, eyesame and stereo, off and on;
           # the vision also with FIX2_WORDS (a second "on"); FIX_CAP=1: a capture with the fix on in both places
    echo "== fix"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    goface 30873.5 3900 -32973.7 -13.5 camp -12 3
    w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 2
    w "practice spawn infantry" 10; x "head 0 0 0 yaw $TURN pitch -12" 2
    sleep ${FIX_WEATHER_WAIT:-45}; m fix-camp-lit 0.2   # the weather's change takes ~a minute; the fire burns after it
    w "fx eyesame 1" 2; for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix-camp-same-off
    for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix-camp-same-on; sleep 1; dq fix-camp-same-on2
    [ -n "${FIX_CAP:-}" ] && { w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"; }
    w "fx eyesame 0" 2; dq fix-camp-stereo-on; m fix-camp-on 0.2; lastrate
    for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix-camp-stereo-off; m fix-camp-off 0.2; lastrate
    w "practice remove" 2
    x "head 0 0 0 yaw 0 pitch 60" 2
    x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 2
    w "fx eyesame 1" 2; dq fix-vision-same-off
    for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 3; dq fix-vision-same-on; sleep 1; dq fix-vision-same-on2
    [ -n "${FIX_CAP:-}" ] && { w "fx d3d 1" 0.2; waitfor 'DCAP t=.* the capture is over' 60 || echo "$(date +%T) no capture end"; }
    for fw in $FIX2_WORDS; do w "${fw//_/ }" 1; done; sleep 3; dq fix-vision-same-on2w; sleep 1; dq fix-vision-same-on2w2
    w "fx eyesame 0" 2; dq fix-vision-stereo-on2w; m fix-vision-on2w 0.2
    for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; for fw in $FIX_WORDS; do w "${fw//_/ }" 1; done; sleep 2
    dq fix-vision-stereo-on; m fix-vision-on 0.2; lastrate
    for fw in $FIX_OFF_WORDS; do w "${fw//_/ }" 1; done; sleep 2; dq fix-vision-stereo-off; m fix-vision-off 0.2; lastrate
    x "btn L b 0" 0.4; sleep 2 ;;
  vision)
    echo "== vision"; w "fx eyesame 0" 0.3; w "curframe game" 0.3; w "fx sync 0" 0.3
    x "head 0 0 0 yaw 0 pitch 60" 2
    x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 2
    w "fx eyesame 1" 2; dq vision-same; w "curframe state" 0.3
    w "fx sync 1" 3; dq vision-same-sync; w "curframe state" 0.3
    w "curframe eye" 3; dq vision-same-eye-sync; w "curframe state" 0.3
    w "fx sync 0" 3; dq vision-same-eye; w "curframe state" 0.3
    w "fx eyesame 0" 3; dq vision-eye; m vision-eye 0.2
    w "curframe game" 3; dq vision-game; m vision-game 0.2
    x "btn L b 0" 0.4; sleep 2
    w "curframe state" 0.3 ;;
  esac
done
w "flatfx state" 1
w "curframe state" 1
w "ktime state" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
