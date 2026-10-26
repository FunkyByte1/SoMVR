#!/bin/bash
# handsdepth-drive.sh - build 11: the world hides our VR hands (SOMVR_XR_HANDS_DEPTH=world; docs/re-xr-pipeline.md
# 33.8, handoff/HANDOFF-vrhands.md "Round 2"). Boot (outside the sandbox, the slot taken) with build 10's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build10.env | tr '\n' ' ')
#     SOMVR_XR_HANDS_DEPTH=world SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
#     setsid scripts/headless_vr.sh <snapshot>/somvr
# and this script at the same time (HD_WT = a word of the launch command, default this worktree's name). Every step
# ends in a stereo dump (`dump`: the eyes, our hands' layer and, with the world's depth, RUN/xr-layer.log.worldN.pgm:
# 16-bit mm, both eyes); RUN/hd-steps.txt names them. Pictures: python3 scripts/xr_compose.py RUN.
# HD_STEPS = any of: hold reach ground wall orc aimground wallwalk aimwall aimorc sword menu (default: hold aimground
# wallwalk aimorc sword menu; wallwalk walks Talion up to the stone wall left of the meadow). The aim steps dump the scene, then put a hand where scripts/handsdepth_aim.py finds the world's surface
# (the ground ahead and below, the wall on the left, the nearest thing in the middle: the orc) and push it through it
# in steps (cm along the eye's ray). Stops at once (quit) when the user goes into VR.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${HD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${HD_STEPS:-hold aimground wallwalk aimorc sword menu}
AIM=${HD_AIM:-$(cd "$(dirname "$0")" && pwd)/handsdepth_aim.py}
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
# d NAME: a dump now (the next stereo frame), its number and the world's distances at the hands noted
d() { local n0 f; n0=$(grep -c 'dump layers frame [0-9]*: hands' "$R/xr-layer.log" 2>/dev/null); t "dump"; sleep 1.6
      f=$(grep -o 'dump layers frame [0-9]*: hands' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*')
      [ "$(grep -c 'dump layers frame [0-9]*: hands' "$R/xr-layer.log")" -gt "$n0" ] || f="$f (no new hands dump)"
      local z; z=$(grep "dump layers frame $f: the world's depth" "$R/xr-layer.log" | tail -1 | sed 's/.*; eye 0/eye 0/')
      echo "$1 $f ${z:-(no world depth)}" | tee -a "$R/hd-steps.txt"; }
both() { t "hand L head $1" "hand R head $2"; sleep "${3:-0.6}"; }
lastf() { grep -o "dump layers frame [0-9]*: the world's depth" "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*'; }
# push NAME WHAT HAND ROT OFF...: a scene dump, then HAND's grip on WHAT's surface point OFF cm along the ray, dumped
push() { local name=$1 what=$2 hand=$3 rot=$4 o p f; shift 4
  d "$name-scene"; f=$(lastf)
  for o in "$@"; do
    p=$(python3 "$AIM" "$R" "$f" "$what" "$o") || { echo "$name: nothing at $what in frame $f"; return 1; }
    echo "$name $o cm: grip at ${p% *} (the surface ${p##* } m away)" | tee -a "$R/hd-steps.txt"
    t "hand $hand head ${p% *} $rot"; sleep 0.5; d "$name$o"
  done; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
grep -q 'xr: hands: ready' "$R/xr-layer.log" && echo "PASS the layer made our hands" || echo "FAIL no 'xr: hands: ready'"
# the arrival card away (the right grip squeezed)
t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"; sleep 0.5
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; sleep 0.5; t "btn R a 0"; sleep 0.5; }
for s in $STEPS; do
  case $s in
  hold)  # the controllers held naturally: the world's depth maps the meadow (nothing near the hands)
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 1.5; d hold
    both "-12 -22 30 40 0 0" "12 -22 30 40 0 0" 1; d hold-near ;;
  reach)  # the right hand straight ahead at chest height, ever farther (into the grass and what stands there)
    for z in 40 80 120 170 230; do both "-17 -30 38 40 0 0" "12 -30 $z 20 0 0" 1; d "reach-$z"; done ;;
  ground)  # the right hand down and ahead into the ground (within the eyes' lower edge: about 49 deg down)
    for p in "10 -130 120" "10 -160 140" "10 -190 165" "10 -220 190" "10 -250 215"; do
      both "-17 -30 38 40 0 0" "$p -30 0 0" 1; d "ground-${p// /,}"; done ;;
  wall)  # the left hand out to the left (33 deg) ever farther: the stone wall left of the meadow
    for p in "-90 -25 140" "-150 -25 230" "-210 -25 320" "-270 -25 410" "-330 -25 500"; do
      both "$p 0 60 0" "17 -30 38 40 0 0" 1; d "wall-${p// /,}"; done ;;
  orc)  # an infantry orc in front: the right hand pushed into it
    w "practice spawn infantry" 1.0
    waitfor '^THREAT start ' 20 || echo "no THREAT start"
    face; sleep 3
    for z in 60 100 140 180 220 270; do both "-17 -30 38 40 0 0" "10 -25 $z 0 0 0" 0.5; d "orc-$z"; done
    w "practice remove" 1 ;;
  aimground)  # the right hand, fingers down, into the ground ahead (the lower middle of the left eye)
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 1
    push ground ground R "-60 0 0" -12 -6 -2 2 6 ;;
  wallwalk)  # turn toward the stone wall on the left and walk up to it, then push the right hand into it
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 1
    w "turn-" 1.2; w "turn-" 1.2
    for i in 1 2 3 4 5 6 7 8 9 10; do
      d "walk-$i"; f=$(lastf); z=$(python3 "$AIM" "$R" "$f" px:0.5,0.5 0 | awk '{print $4}')
      echo "walk $i: the world straight ahead ${z:-?} m"
      [ -n "$z" ] && awk -v z="$z" 'BEGIN{exit !(z < 1.0)}' && break
      if [ -n "$z" ] && awk -v z="$z" 'BEGIN{exit !(z < 2.0)}'; then w "stick 0 0.5 0 0.6" 1.4; else w "stick 0 1 0 1.0" 1.8; fi
    done
    push wallR px:0.5,0.5 R "0 0 0" -10 -5 -2 1 4
    push wallL px:0.45,0.6 L "0 0 0" -6 0 4 ;;
  aimwall)  # the left hand into the wall on the left
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 1
    push wall wall L "0 70 0" -8 -2 4 ;;
  aimorc)  # an infantry orc in front: the right hand pushed into it (the nearest thing in the middle)
    w "practice spawn infantry" 1.0
    waitfor '^THREAT start ' 20 || echo "no THREAT start"
    face; sleep 2
    both "-17 -30 38 40 0 0" "17 -30 38 40 0 0" 0.5
    H=${HD_ORC_HAND:-R}  # (L after the sword step: the right hand holds the sword)
    push orc near $H "0 0 0" -8 -3 2 6
    push orc2 near $H "0 0 0" -4 0 4
    # the hands held still ahead while the orc comes and strikes: whatever of it passes them hides them there
    t "hand L head -15 -15 80 0 0 0" "hand R head 15 -20 100 0 0 0"; sleep 0.5
    for i in 1 2 3 4 5 6; do d "orc-still-$i"; sleep 0.4; done
    w "practice remove" 1 ;;
  sword)  # the quick draw, the fist round the hilt from four sides (the game's sword in the depth)
    both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 0.5; w "hands draw" 3
    both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 1; d sword-held
    both "-20 -30 35 40 0 0" "15 -15 35 40 0 -90" 1; d sword-right
    both "-20 -30 35 40 0 0" "10 -10 30 40 90 -90" 1; d sword-palm
    both "-20 -30 35 40 0 0" "15 -10 40 -20 0 0" 1; d sword-up
    both "6 -27 30 40 0 0" "8 -18 35 40 0 0" 1; t "btn L squeeze 1"; sleep 1.5; d two-hands
    t "btn L squeeze 0"; sleep 1 ;;
  menu)  # the wrist panel (the left palm toward the eyes), the right fingertip on it: unchanged by the world
    t "hand R head 15 -30 35 40 0 0" "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 2
    grep -q 'vrmenu: opened by' "$R/xr-layer.log" || { t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 1; }
    d menu-open
    t "hand R touch panel 0.5 0.45 0.3 s 0.3"; sleep 1; d menu-finger-touch
    t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1.5 ;;
  esac
done
grep -E "xr: hands: (the world|frame [0-9]*: the world)|xr: hands: GPU" "$R/xr-layer.log" | head -12 | cut -c1-240
grep -E '^VRHANDS (depth|t=)' "$R/somvr.log" | tail -4 | cut -c1-260
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R, steps in $R/hd-steps.txt"
