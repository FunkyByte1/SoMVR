#!/bin/bash
# ghost-drive.sh - the white see-through glove near the left eye in the ivy by the stone wall (handoff/HANDOFF-ghost.md,
# docs/re-xr-pipeline.md 36). Boot (outside the sandbox, the slot taken) with a build's switches and SOMVR_HANDS_TEST=1
# SOMVR_XR_TEST=@RUN@/xrtest.txt (see scripts/handsdepth-drive.sh's header), and this script at the same time (GH_WT = a
# word of the launch command, default this worktree's name). It walks Talion from the practice meadow's arrival to the
# ivy on the stone wall (turn- twice, then the stick until the head is within 60 cm of GH_AT, default run 013938's spot
# x,z = 31784,-25314: by the head position of the AUDIO lines, so it works without the world's depth, e.g. on build 10)
# and dumps both eyes on the way (RUN/hd-steps.txt names them; pictures: python3 scripts/xr_compose.py RUN -f N).
# GH_STEPS (default: meadow walk bush handsdown wordtest): pose / sword / bow / orc = SOMVR_HP_POSE off and on (the word
# hppose) where Talion stands, with the sword drawn, with the bow out, an orc in front; autoskip = each node SOMVR_HP_NEARLOG logged after the walk
# began hidden alone (the word hpskip) and dumped (GH_SKIPMAX, default 12); wordtest = GH_WORDS (';'-separated trigger words, e.g.
# "ghost off;ghost on") each followed by a dump named after it. Then it holds GH_HOLD seconds (default 120: words and
# dumps by hand: `echo dump > RUN/xrtest.txt`; touch RUN/ghost.done ends the hold) and quits. Stops at once (quit) when
# the user goes into VR.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${GH_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${GH_STEPS:-meadow walk bush handsdown wordtest}
AT=${GH_AT:-31784,-25314}
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
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
# d NAME: a dump now (the next stereo frame), its number and the head position noted
d() { local n0 f p; n0=$(grep -c 'dump layers frame [0-9]*: hands' "$R/xr-layer.log" 2>/dev/null); t "dump"; sleep 1.6
      f=$(grep -o 'dump layers frame [0-9]*: hands' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*')
      [ "$(grep -c 'dump layers frame [0-9]*: hands' "$R/xr-layer.log")" -gt "$n0" ] || f="$f (no new hands dump)"
      p=$(headpos)
      echo "$1 $f head ${p:-?}" | tee -a "$R/hd-steps.txt"; }
both() { t "hand L head $1" "hand R head $2"; sleep "${3:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# the head's world position (cm) and facing, from the newest AUDIO line: "x,y,z fx,fy,fz"
headpos() { grep -E '^AUDIO n=.*head pos=' "$R/somvr.log" | tail -1 | sed -E 's/.*head pos=([^ ]+) front=([^ ]+).*/\1 \2/'; }
# the horizontal distance (cm) from the head to GH_AT
dist() { headpos | awk -v at="$AT" '{split($1,p,","); split(at,a,","); dx=p[1]-a[1]; dz=p[3]-a[2]; printf "%.0f\n", sqrt(dx*dx+dz*dz)}'; }
grep -q 'xr: hands: ready' "$R/xr-layer.log" && echo "PASS the layer made our hands" || echo "FAIL no 'xr: hands: ready'"
t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"; sleep 0.5
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; sleep 0.5; t "btn R a 0"; sleep 0.5; }
HOLD="-17 -30 38 40 0 0"; HOLDR="17 -30 38 40 0 0"
for s in $STEPS; do
  case $s in
  meadow)  # the hands held naturally in the open meadow (no ivy): nothing white
    both "$HOLD" "$HOLDR" 1.5; d meadow ;;
  walk)  # turn toward the stone wall on the left and walk to the ivy (run 013938's spot), dumps on the way
    both "$HOLD" "$HOLDR" 1
    NW=$(grep -c '^HEADPARTS near: node' "$R/somvr.log")  # the near-log's lines before the walk
    for k in $(seq 1 ${GH_TURNS:-2}); do w "turn-" 1.2; done  # GH_TURNS=0: already facing the wall
    last=""; stuck=0
    for i in $(seq 1 16); do
      z=$(dist); echo "walk $i: $z cm to the ivy spot ($(headpos))"
      [ -n "$z" ] && [ "$z" -lt 60 ] && break
      if [ -n "$z" ] && [ -n "$last" ] && [ $(( last - z )) -lt 15 ]; then  # (run 034003: the first stick was lost)
        stuck=$((stuck + 1)); [ $stuck -ge 3 ] && echo "walk: no progress" && break
      else stuck=0; fi
      last=$z
      if [ -n "$z" ] && [ "$z" -lt 250 ]; then w "stick 0 0.5 0 0.6" 1.6; else w "stick 0 1 0 1.0" 2.0; fi
      [ $((i % 2)) -eq 0 ] && d "walk-$i"
    done
    sleep 1.5; echo "walk: at $(dist) cm from the spot ($(headpos))" | tee -a "$R/hd-steps.txt" ;;
  bush)  # the hands held as in the meadow, in the ivy: the white glove (run 013938 1347) or not
    both "$HOLD" "$HOLDR" 1.5; d bush-1; sleep 3; d bush-2 ;;
  handsdown)  # our hands low and wide (out of the middle): what the game draws there on its own
    both "-40 -70 20 40 0 0" "40 -70 20 40 0 0" 1.5; d bush-handsdown
    both "$HOLD" "$HOLDR" 1 ;;
  pose)  # SOMVR_HP_POSE off, then on again, a dump each (the white glove of the wraith in the ivy: off shows it)
    w "hppose off" 1.5; d pose-off; w "hppose on" 1.5; d pose-on ;;
  sword)  # the quick draw (handsdepth-drive.sh's sword step), the fist round the hilt; SOMVR_HP_POSE off and on
    both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 0.5; w "hands draw" 3
    both "-20 -30 35 40 0 0" "15 -20 35 40 0 0" 1; d sword-on
    w "hppose off" 1.5; d sword-pose-off; w "hppose on" 1.5; d sword-pose-on
    both "$HOLD" "$HOLDR" 1 ;;
  bow)  # the bow out (build 11's SOMVR_BOW=1: the left trigger held, bow-drive.sh's pose 0 0 30), SOMVR_HP_POSE on, off,
    # on (the white glove round the bow hand: HANDOFF-bow.md round 1); the trigger let go
    t "hand L head 7.0 -13.0 60.0 0.0 0.0 0" "hand R head 8.5 -13.0 30.0 0.0 0.0 0"; sleep 0.5
    t "btn L trigger 1"; sleep 2.5
    grep -E "^BOW .* enter" "$R/somvr.log" | tail -1 | cut -c1-160
    d bow-on; w "hppose off" 1.5; d bow-pose-off; w "hppose on" 1.5; d bow-pose-on
    t "btn L trigger 0"; sleep 2; both "$HOLD" "$HOLDR" 1 ;;
  orc)  # an infantry orc in front (the practice word), faced: SOMVR_HP_POSE on, off, on (an orc has its own transforms)
    w "practice spawn infantry" 1.0
    waitfor '^THREAT start ' 20 || echo "no THREAT start"
    face; sleep 2
    d orc-on; w "hppose off" 1.5; d orc-pose-off; w "hppose on" 1.5; d orc-pose-on
    w "practice remove" 1.5 ;;
  autoskip)  # each candidate hidden alone (hpskip) and dumped: Talion's model's renderables (the word hpscan), the
    # near-log's nodes logged after the walk began (GH_SKIPFROM=0: all of them); the body node too (hidden already: its
    # "who" lines for comparison)
    w "hpscan" 1.5
    MAIN=$(grep -oE '^HEADPARTS t=.* nodes main=0x[0-9a-f]+' "$R/somvr.log" | tail -1 | grep -oE '0x[0-9a-f]+$')
    for nd in $( { grep -E '^HEADPARTS scan: model\+' "$R/somvr.log" | grep -oE 'node 0x[0-9a-f]+' | awk '{print $2}'
                   grep -E '^HEADPARTS near: node' "$R/somvr.log" | tail -n +$(( ${GH_SKIPFROM:-${NW:-0}} + 1 )) |
                     awk '{print $4}'; } |
                 awk '!seen[$0]++' | head -${GH_SKIPMAX:-12}); do
      w "hpskip $nd" 1.0; d "skip-$nd"; w "hpskip off" 0.6
    done ;;
  wordtest)  # GH_WORDS, each followed by a dump named after it
    IFS=';' read -r -a WS <<< "${GH_WORDS:-}"
    for x in "${WS[@]}"; do [ -n "$x" ] || continue; w "$x" 2.0; d "word-${x// /_}"; done ;;
  esac
done
echo "$(date +%T) holding ${GH_HOLD:-120} s (touch $R/ghost.done to end)"
for i in $(seq 1 $(( ${GH_HOLD:-120} / 3 ))); do
  userin && bail "the user is in VR"
  [ -f "$R/ghost.done" ] && break
  sleep 3
done
grep -E '^HEADPARTS (near|hpskip|t=.*near-log)' "$R/somvr.log" | tail -12 | cut -c1-300
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R, steps in $R/hd-steps.txt"
