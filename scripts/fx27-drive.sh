#!/bin/bash
# fx27-drive.sh - build 27's effects audit (som-b27-fx; docs/re-xr-pipeline.md 125): the states a player meets, each with
# both eyes dumped (scripts/xr_compose.py) and, where it matters, `fx eyesame 1` dumps (both eyes one pose: anything that
# still differs is a per-eye effect; scripts/fx27-eyediff.py); the game's screen-space effects of each moment are in the
# log by themselves (SOMVR_FX_CENSUS: FXSEEN / FXCENSUS lines). One headless boot at the practice arrival with
# scripts/headset-build26.env, SOMVR_HANDS_TEST=1, SOMVR_FX_TEST=1 and the layer's test file (abil27d-drive.sh's boot).
# FX27_STEPS = any of (in this order; default: base vision bow strike hurt rain death):
#   base    the meadow ahead, level, and 30 deg up (the sun's side)
#   vision  Y held (the wraith vision): the eyes as it comes on, held, 60 deg up; Y let go: as it goes
#   bow     an orc 10 m ahead, the bow out, an arrow drawn at him (the game's aim: its slow motion while the Focus lasts),
#           held 4 s, let go
#   strike  Y held, the orc marked by the gaze, A = the game's Shadow Strike: the eyes through the strike and its kill
#   hurt    Talion at 20 of his health (the game's low-health overlay - out since build 24), then healed
#   rain    the night's rain (GameServerShell.ChangeWeather SON_Night_Rain01) 25 s on
#   death   Talion mortal at 5 health, an orc fighting him: the eyes through the death (greying, the fall) and the
#           Nemesis screens after it
#   exec    (not in the default) the execution: the sword drawn, an orc, light slashes with the test chase until the
#           game's charge is full (`EXECUTE ready`), the right trigger held, a heavy chop: the eyes through the kill's
#           slow motion (FX_HIDEEYE="E MASK": `fx hideeye E` + `fx hide MASK` for the step - one eye without a renderer)
#   near    (not in the default) the FX near the eyes: an orc's blows land on Talion (his blood on his body), then our
#           slashes on the orc at arm's length (his blood); `fx vprobe 60` (the CPU emitters within 3 m: their
#           vertices); eye 0 drawn without the CPU particles (`fx hideeye 0`, `fx hide 1`), then without the GPU
#           particles (2), then without the sprites (4): a dump right after each blow
#   nearv   (not in the default) SOMVR_FX_NEAR's proof: looking down at Talion's hip (the dagger's own FX ~46 cm from the
#           eyes), the fold at a test radius of 30,60 cm in eye 0 only (`fxnear 30,60`, `fxnear eye 0`): the FXNEAR lines'
#           distances and the dump's two eyes; then back to 15,30 (eye 0 only stays for a chained driver's kill)
#   nearb   (not in the default) SOMVR_FX_NEAR on real blood: our slashes on an orc at arm's length with the fold at a
#           test radius of 150,200 cm in eye 0 only (his blood and the sparks within 1.5 m: gone in eye 0, the game's in
#           eye 1 - a dump after each hit), then 15,30 again (eye 0 only stays for a chained driver)
#   torch   (not in the default) SOMVR_FX_NEAR on a flame one can see: the ruin's torch at the build 20c campfire spot
#           (fire23b-drive.sh's: lit in the overcast SON_Day03 - boot with SOMVR_PRACTICE_DAY=0), dumped as the game draws
#           it, with the fold at a test radius of 800,1000 cm in eye 0 only (the flame gone in eye 0, the game's in eye 1),
#           then at 15,30 (nothing near: both eyes the game's)
# FX27_RUN=RUN drives a running boot; FX27_NOQUIT=1 leaves the game running at the end (a chain of drivers).
# FX_EXTRA_WORDS: words sent once after the arrival (e.g. a fix's switch). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FX27_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${FX27_STEPS:-base vision bow strike hurt rain death}
T0=$(date +%s)
R=${FX27_RUN:-}
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
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
# d NAME: one stereo frame dumped; NAME FRAME into RUN/fx27-steps.txt (the census's time with it)
d() { local n0 i f; userin && bail "the user is in VR"; n0=$(grep -c 'dump layers frame' "$R/xr-layer.log")
  echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 40); do [ "$(grep -c 'dump layers frame' "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.1; done
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f $(date +%T.%N | cut -c1-12)" | tee -a "$R/fx27-steps.txt"; }
# ds NAME: the same with both eyes one pose (`fx eyesame 1`)
ds() { w "fx eyesame 1" 1.2; d "$1"; w "fx eyesame 0" 0.6; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
lines() { wc -l < "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
look() { x "head 0 0 0 yaw $1 pitch ${2:-0} s 0.3"; }
LREST="hands test L -22 -35 25 0 0 0"
REST="hands test R 22 -35 -30 0 180 0"
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST"; w "$REST" 1
w "bow arrows 40" 0.5; w "flatfx state" 0.5
[ -n "${FX_EXTRA_WORDS:-}" ] && { IFS=';' read -ra EW <<< "$FX_EXTRA_WORDS"; for e in "${EW[@]}"; do w "$e" 0.6; done; }
grep -E '^(WARN|CONFIG (fx_census|flatfx))' "$L" | head -6 | cut -c1-260
# the bow (the layer's test hands; abil27d-drive.sh's)
BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
bow_out() { w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
  x "$BOW_SLOT" "$R_REST"; sleep 0.3; x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1
  grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | cut -c1-160; }
bow_away() { x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 0.5
  x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; x "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1
  w "$LREST" 0.3; w "$REST" 0.5; }
draw() { x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.4
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  x "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.0; }
release() { TS=0.1 x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 0"; }
spawn_far() { w "practice far infantry" 3
  if grep -E '^PRACTICE t=.*spawn ' "$L" | tail -1 | grep -q 'FAILED\|refused'; then
    w "practice back" 6; w "practice go" 7; w "practice far infantry" 3; fi; }
# the gaze's nearest orc (strike18-drive.sh's): its bearing from the head's ray
HY=0; HP=0
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p'); [ -n "$OB_Y" ]; }
lookorc() { orc0 || return 1; HY=$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}')
  HP=$(awk -v a="$HP" -v b="$OB_P" 'BEGIN{v=a+b; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}'); look "$HY" "$HP"; }

if has base; then
  echo "== base"; look 0 0; sleep 1; d base-level; ds base-level-same; look 0 30; sleep 1; d base-up30; look 0 0
fi
if has vision; then
  echo "== vision: Y held, then let go"
  n0=$(lines); x "btn L b 1" 0.05; d vis-on-1; d vis-on-2; z 1; d vis-on-held
  ds vis-held-same; look 0 60; sleep 1; d vis-up60; ds vis-up60-same; look 0 0; sleep 0.5
  x "btn L b 0" 0.05; d vis-off-1; d vis-off-2; z 2; d vis-off-after
  since "$n0" '^(VISION t=|FXSEEN)' | head -12 | cut -c1-200
fi
if has bow; then
  echo "== bow: an orc 10 m ahead, an arrow drawn at him (the game's aim and its slow motion), held 4 s"
  w "practice remove" 1.2; spawn_far; look 0 0; bow_out
  n0=$(lines); draw; d bow-drawn; z 2; d bow-held2; ds bow-held-same; z 1; d bow-held4
  since "$n0" '^(FXSEEN|BOW t=.* (aim|focus|slow))' | head -10 | cut -c1-200
  release; sleep 0.3; d bow-released; z 2; d bow-after; bow_away
fi
if has strike; then
  echo "== strike: Y held, the orc marked by the gaze, A"
  w "practice remove" 1.2; spawn_far; HY=0; HP=0; look 0 0
  v0=$(cnt '^VISION t=.* the flag 0 -> 1'); x "btn L b 1" 0.3; waitfor '^VISION t=.* the flag 0 -> 1' "$v0" 3; sleep 0.5
  for i in 1 2 3; do lookorc && break; sleep 0.5; done
  sleep 0.8; d strike-marked
  n0=$(lines); TS=0.3 x "btn L b 1" "btn R a 1"; TS=0.2 x "btn L b 1" "btn R a 0"
  d strike-1; d strike-2; d strike-3; z 1; d strike-4; ds strike-same; z 2; d strike-after
  since "$n0" '^(STRIKE t=.* (start|no move|land|dash)|FXSEEN)' | head -12 | cut -c1-200
  x "btn L b 0" 0.5; w "practice remove" 1.2; HY=0; HP=0; look 0 0
fi
if has hurt; then
  echo "== hurt: Talion at 20 of his health"
  n0=$(lines); w "practice hurt 20" 2; d hurt-20; ds hurt-20-same; w "practice health" 2; d hurt-healed
  since "$n0" '^(FXSEEN|FIGHT low|PRACTICE t=.* (hurt|health))' | head -8 | cut -c1-200
fi
if has rain; then
  echo "== rain: night with rain"
  n0=$(lines); w "fx console GameServerShell.ChangeWeather SON_Night_Rain01" 1; z 25
  d rain-level; ds rain-same; look 0 40; sleep 1; d rain-up40; look 0 0
  since "$n0" '^(FXSEEN|FLATFX t=.* console)' | head -8 | cut -c1-200
fi
if has death; then
  echo "== death: mortal at 5, an orc fights him"
  n0=$(lines); w "practice remove" 1.2; w "practice mortal on" 0.5; w "practice hurt 5" 1
  w "practice spawn infantry" 3; w "fight guard path" 0.3
  DT=$(date +%s); dead=0
  while [ $(( $(date +%s) - DT )) -lt 120 ]; do
    since "$n0" '^SCREENS t=.* the death: dying' >/dev/null && { dead=1; break; }; userin && bail "the user is in VR"; sleep 0.5
  done
  echo "dying=$dead after $(( $(date +%s) - DT )) s"
  if [ $dead = 1 ]; then
    d death-0; d death-1; d death-2; ds death-same; for k in 3 4 5 6 7 8; do z 2; d "death-$k"; done
    for k in 1 2 3; do z 4; d "death-after-$k"; done
  fi
  since "$n0" '^(SCREENS t=|FXSEEN)' | head -16 | cut -c1-200
fi
if has exec; then
  echo "== exec: light slashes until the charge is full, the right trigger held, a heavy chop"
  LA="hands test R 30 -10 45 0 -35 0"; SHOW="hands test R 22 -25 40 75 -15 0"; LUNGE_A="hands test R 40 -8 35 0 -90 0"
  LMS=${FX_LMS:-1000}
  # the sword drawn unless it is (execute-drive.sh's test of the SLOTS lines)
  sl=$(tail -3000 "$L" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $sl in *"kept on the back no more"*|*"now in the hand"*|*"kept in the hand"*) echo "the sword in the hand" ;;
    *) w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6; w "hands squeeze R 0" 0.5 ;; esac
  [ -n "${FX_HIDEEYE:-}" ] && { set -- $FX_HIDEEYE; w "fx hideeye $1" 0.4; w "fx hide $2" 0.4; }
  w "practice remove" 1.2; w "practice spawn infantry" 3
  RD0=$(cnt '^EXECUTE ready t='); K0=$(cnt '^EXECUTE kill t='); n0=$(lines)
  w "$LA" 0.5; w "execute chase on" 0.5
  for k in $(seq 1 70); do
    [ "$(cnt '^EXECUTE ready t=')" -gt "$RD0" ] && break
    if [ $((k % 2)) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 $LMS" 0.6; else w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.6; fi
    [ $((k % 14)) = 0 ] && since "$n0" '^COMBAT death orc' >/dev/null && { w "practice spawn infantry" 2.5; n0=$(lines); }
  done
  if [ "$(cnt '^EXECUTE ready t=')" -gt "$RD0" ]; then
    grep -E '^EXECUTE ready t=' "$L" | tail -1 | cut -c1-200
    x "btn R trigger 1" 0.25; w "$SHOW" 0.4; d exec-ready
    for k in 1 2 3 4 5 6; do
      [ "$(cnt '^EXECUTE kill t=')" -gt "$K0" ] && break
      w "$LUNGE_A" 0.5; w "hands swing R -20 -18 62 0 90 0 300" 0.3; d "exec-chop$k"; w "hands swing R 40 -8 35 0 -90 0 900" 0.6
    done
    d exec-kill-1; d exec-kill-2; d exec-kill-3; z 1; d exec-kill-4; ds exec-kill-same; z 2; d exec-after
    grep -E '^EXECUTE kill t=' "$L" | tail -1 | cut -c1-300
  else
    echo "never ready"; grep -E '^EXECUTE streak' "$L" | tail -3 | cut -c1-200
  fi
  x "btn R trigger 0" 0.3; w "execute chase off" 0.5; w "fx hide 0" 0.3; w "fx hideeye -1" 0.3; w "practice remove" 1; w "$REST" 0.5
fi
if has torch; then
  echo "== torch: the ruin's torch, the fold at 800,1000 in eye 0"
  # render24-drive.sh's goface: go to X Y Z, face the head's world yaw YAW (a mark's yaw), pitched P
  m() { sleep "${2:-0.6}"; w "mark $1" 3.5; }
  lastyaw() { sed -n 's/^head .* yaw \(-\?[0-9.]*\) deg.*/\1/p' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt" | head -1; }
  goface() { local a0 f0 y d y2; a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
    w "practice go $1 $2 $3" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
    sleep 3; x "head 0 0 0 yaw 0 pitch 0" 1; m "$5-yaw0" 0.2; y=$(lastyaw); echo "yaw now $y (want $4)"
    d=$(python3 -c "d=($4)-($y); d=(d+180)%360-180; print(round(d,1))")
    x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned" 0.2; y2=$(lastyaw); echo "yaw now $y2 after +$d"
    if python3 -c "import sys; e=lambda a: abs((($4)-(a)+180)%360-180); sys.exit(0 if e($y2) > e($y) else 1)"; then
      d=$(python3 -c "print(-($d))"); x "head 0 0 0 yaw $d pitch $6" 1.2; fi; }
  w "practice remove" 1; goface 30873.5 3900 -32973.7 -125.8 torch -8
  w "fx console GameServerShell.ChangeWeather ${FX_TORCH_WEATHER:-SON_Day03}" 14
  w "fxnear eye -1" 0.3; w "fxnear 15,30" 0.3; d torch-game; sleep 0.5; d torch-game-b
  n0=$(lines); w "fxnear 800,1000" 0.3; w "fxnear eye 0" 1.0; d torch-eye0-folded; sleep 0.5; d torch-eye0-folded-b
  w "fxnear state" 0.5; since "$n0" '^FXNEAR t=' | head -4 | cut -c1-240
  w "fxnear 15,30" 1.0; d torch-15-30; w "fxnear state" 0.5
fi
if has nearb; then
  echo "== nearb: our slashes on an orc, the fold at 150,200 in eye 0"
  w "practice remove" 1.2; look 0 0
  sl=$(tail -3000 "$L" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $sl in *"kept on the back no more"*|*"now in the hand"*|*"kept in the hand"*) : ;;
    *) w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6; w "hands squeeze R 0" 0.5 ;; esac
  n0=$(lines); w "fxnear 150,200" 0.4; w "fxnear eye 0" 0.4; w "practice spawn infantry" 3
  w "hands test R 30 -10 45 0 -35 0" 0.5; w "execute chase on" 0.5; nd=0
  for k in $(seq 1 16); do
    c0=$(cnt '^COMBAT hit '); if [ $((k % 2)) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 1000" 0.3; else w "hands swing R 30 -10 45 0 -35 0 1000" 0.3; fi
    waitfor '^COMBAT hit ' "$c0" 1 && { nd=$((nd + 1)); d "nearb-hit-$k"; sleep 0.15; d "nearb-hit-$k-b"; }
    [ $nd -ge 5 ] && break
  done
  w "fxnear state" 0.5; since "$n0" '^FXNEAR t=' | head -4 | cut -c1-240
  w "execute chase off" 0.4; w "fxnear 15,30" 0.4; w "practice remove" 1; w "$REST" 0.5; look 0 0
fi
if has nearv; then
  echo "== nearv: the dagger's FX at the hip folded in eye 0 at a test radius 30,60"
  w "fxnear state" 0.5; look 0 -60; sleep 1.5; d nearv-down-game
  n0=$(lines); w "fxnear 30,60" 0.4; w "fxnear eye 0" 0.4; sleep 2; d nearv-down-eye0-30-60; w "fxnear state" 0.5
  since "$n0" '^FXNEAR' | head -6 | cut -c1-260
  w "fxnear 15,30" 0.4; look 0 0; sleep 1; w "fxnear state" 0.5
fi
if has vprobe; then
  echo "== vprobe: the CPU emitters' vertices (3 draws each, 60 lines), looking ahead, down, then with the vision"
  look 0 0; w "fx vprobe 60" 0.4; z 3; look 0 -60; z 3; x "btn L b 1" 0.3; z 3; x "btn L b 0" 0.3; look 0 0; z 2
  w "bow strike gaze status" 0.5; grep -E '^STRIKE t=.* gaze: (on|off|status)|^STRIKE gaze' "$L" | tail -2 | cut -c1-200
fi
if has near; then
  echo "== near: blows on Talion, then ours on the orc; eye 0 without a renderer at a time"
  w "practice remove" 1.2; look 0 -10; w "fx vprobe 60" 0.4; w "fx hideeye 0" 0.4
  w "practice spawn infantry" 3
  for m in 1 2 4; do
    w "fx hide $m" 0.4
    for k in 1 2; do
      h0=$(cnt '^FIGHT hurt'); waitfor '^FIGHT hurt' "$h0" 25 && { d "near-hurt-hide$m-$k"; sleep 0.4; d "near-hurt-hide$m-$k-b"; } || echo "no blow"
    done
  done
  w "fx hide 1" 0.4
  sl=$(tail -3000 "$L" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $sl in *"kept on the back no more"*|*"now in the hand"*|*"kept in the hand"*) : ;;
    *) w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6; w "hands squeeze R 0" 0.5 ;; esac
  w "hands test R 30 -10 45 0 -35 0" 0.5; w "execute chase on" 0.5
  for k in $(seq 1 12); do
    c0=$(cnt '^COMBAT hit '); if [ $((k % 2)) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 1000" 0.4; else w "hands swing R 30 -10 45 0 -35 0 1000" 0.4; fi
    waitfor '^COMBAT hit ' "$c0" 1 && { d "near-ourhit-$k"; }
  done
  w "execute chase off" 0.4; w "fx hide 0" 0.3; w "fx hideeye -1" 0.3; w "practice remove" 1; w "$REST" 0.5; look 0 0
fi
w "flatfx state" 1
[ -n "${FX27_NOQUIT:-}" ] && { echo "$(date +%T) done (the game left running)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
