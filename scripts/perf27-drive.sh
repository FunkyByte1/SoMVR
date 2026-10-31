#!/bin/bash
# perf27-drive.sh - build 27's frame-cost round (docs/re-xr-pipeline.md 115, som-b27-perf): each place held still for a
# measured window named by the word `perf NAME` (the preload's PERF lines and the layer's `xr: perf NAME:` lines), the
# window logged when the next one starts. Boot with scripts/headset-build26.env + SOMVR_HANDS_TEST=1 + SOMVR_PERF=on
# SOMVR_XR_PERF=on SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600 (the windows only by the word) and, for the Sea Fort, the story's
# furthest save (HEADLESS_SAVE_FROM, re-xr-pipeline.md 110.9). No sound is played; no mark inside a window (a mark dumps a
# frame). PERF_STEPS (default "meadow crowd vision fort fire"), any of:
#   meadow   the practice meadow, the head still ahead, then turning (60 deg over 4 s, back)
#   crowd    `practice group 8`: eight orcs around Talion fighting him (SOMVR_PRACTICE_MINHEALTH keeps him up); they stay
#            until the step `remove`
#   skips    in the place the drive is in: eye 1 skips shadow passes (`perf skip M`, M in PERF_SKIPS, default
#            "1 2 4 8 7 15"), a window of PERF_SKIP_S (15) seconds each (a mark with pics), then 0 again
#   vision   the wraith vision (Y held) looking ahead
#   fort     the Sea Fort camp (24800 3600 3300): the world's orcs, alerted
#   fire     the campfire of the build 20c session's mark 16 (30873.5 3900 -32973.7, yaw -13.5, pitch -12)
#   cut      PERF_CUT_ON / PERF_CUT_OFF: words that turn a cut on / off: three windows (off, on, off) in the place
#            the drive is in, each PERF_CUT_S (20) seconds
#   share    SOMVR_SHADOW_SHARE by its word: a window with each mode (off, cascades, all)
#   shcheck  lossless dumps of 3 stereo frames with each mode (off, cascades, all, off): scripts/perf27-eyediff.py
#   shalt    the check in consecutive frames (`shadowshare alt|altall|altbad|off`: odd frames only): eyediff --alt
#   settings the game's render settings stepped down one at a time (PERF_SETTINGS)
#   look     the head pitched down PERF_PITCH (-20) degrees
#   pics     a mark in each place (after its window): the both-eye picture
# BG_RUN=RUN drives a running boot. PERF_S: each window's seconds (default 30).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PERF_PLACE=start
STEPS=${PERF_STEPS:-meadow crowd remove vision fort fire}; STEPS=${STEPS//_/ } # (underscores: one word through env)
WS=${PERF_S:-30}
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
m() { [[ " $STEPS " == *" pics "* ]] || return 0; sleep 0.6; w "mark $1" 3.5; echo "$(date +%T) mark $1 -> $(ls -t "$R/marks" 2>/dev/null | head -1)"; }
# a named window: both sides' lines of the window before it, then this one runs SEC seconds
win() { w "perf $1" 0.3; x "perf $1" 0.3; echo "$(date +%T) window $1 (${2:-$WS} s)" | tee -a "$R/perf-steps.txt"; sleep "${2:-$WS}"; }
# three consecutive stereo frames dumped losslessly (the layer's <log>.stereoN.ppm); NAME and the last frame noted
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\ndump 2\ndump 3\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 100); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 3)) ] && break; sleep 0.25; done
  echo "dump $1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -3 | grep -o '[0-9]*$' | tr '\n' ' ')" | tee -a "$R/perf-steps.txt"; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 -30 0 0 0"
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
goto() { local a0 f0; a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170; sleep 4; }
for s in $STEPS; do
  case $s in
  meadow)
    PERF_PLACE=meadow
    x "head 0 0 0 yaw 0 pitch 0" 2; win meadow
    x "head 0 0 0 yaw 60 pitch 0 s 4" 0.2; win meadow-turn 9; x "head 0 0 0 yaw 0 pitch 0 s 4" 5; m meadow ;;
  crowd)
    w "practice group 8" 6; grep 'group (' "$R/somvr.log" | tail -1 | cut -c1-160
    PERF_PLACE=crowd8; win crowd8; m crowd8 ;;
  remove) w "practice remove" 3 ;;
  vision)
    x "head 0 0 0 yaw 0 pitch 0" 1; x "btn L b 1" 3; win vision 25; m vision; x "btn L b 0" 2 ;;
  fort)
    goto 24800 3600 3300; PERF_PLACE=fort; win fort; m fort ;;
  fire)
    goto 30873.5 3900 -32973.7; x "head 0 0 0 yaw 0 pitch -12" 2; PERF_PLACE=fire; win fire; m fire ;;
  cut)
    CS=${PERF_CUT_S:-20}
    win cut-off-1 "$CS"; for c in ${PERF_CUT_ON:-}; do w "${c//_/ }" 0.5; done
    win cut-on "$CS"; for c in ${PERF_CUT_OFF:-}; do w "${c//_/ }" 0.5; done
    win cut-off-2 "$CS" ;;
  skips)   # what sharing eye 0's shadow maps with eye 1 would save: eye 1 skips the passes in M (`perf skip M`: 1 the
           # sky shadow, 2 the lights' shadows, 4 the sun cascades, 8 the deferred lights - a measurement, never for play)
    for M in ${PERF_SKIPS:-1 2 4 8 7 15}; do w "perf skip $M" 0.5; win "$PERF_PLACE-skip$M" "${PERF_SKIP_S:-15}"; m "$PERF_PLACE-skip$M"; done
    w "perf skip 0" 0.5; win "$PERF_PLACE-skip0" "${PERF_SKIP_S:-15}"; m "$PERF_PLACE-skip0" ;;
  share)   # SOMVR_SHADOW_SHARE live (`shadowshare MODE`): windows of PERF_SKIP_S (15) s with each mode, then PERF_SHARE (off)
    for M in off cascades all; do w "shadowshare $M" 0.5; win "$PERF_PLACE-share-$M" "${PERF_SKIP_S:-15}"; done
    w "shadowshare ${PERF_SHARE:-off}" 0.5 ;;
  shcheck) # eye 1 with eye 0's shadow maps, lossless: three consecutive stereo frames dumped in each mode (off, cascades,
           # all, off again); the frames in RUN/perf-steps.txt for scripts/perf27-eyediff.py
    for M in off cascades all off; do w "shadowshare $M" 2.5; dq "sh-$M"; done
    # the check's control: eye 1 without its deferred lights (`perf skip 8`: its lighting wrong) must stand out
    w "shadowshare off" 0.3; w "perf skip 8" 2.5; dq "sh-badcontrol"; w "perf skip 0" 0.5
    w "shadowshare ${PERF_SHARE:-off}" 0.5 ;;
  shalt)   # the same check in consecutive frames: `shadowshare alt` shares only in odd frames, so each dumped triple goes
           # off-on-off or on-off-on in eye 1 alone (altall: also the sky and lights; altbad: eye 1 without its deferred
           # lights, the control; off: the baseline); two triples each (scripts/perf27-eyediff.py RUN --alt)
    for M in off alt altall altbad off; do w "shadowshare $M" 2.5; dq "alt-$M"; dq "alt-$M"; done
    w "shadowshare ${PERF_SHARE:-off}" 0.5 ;;
  settings) # the game's render settings live (`perf console Render.Setting.NAME V`; SOMVR_FX_TEST's `fx console` costs
           # the render thread ~25 ms, 142404): a window of
           # PERF_SKIP_S (15) s per entry of PERF_SETTINGS ("NAME=V:BACK ..."; default the user's 10-28 settings stepped
           # down one at a time), each set back after; a window with nothing changed first
    win "$PERF_PLACE-set-base" "${PERF_SKIP_S:-15}"
    for kv in ${PERF_SETTINGS:-VegetationQuality=3:4 VegetationQuality=2:4 ShadowQuality=2:3 MeshQuality=2:3 LightingQuality=1:2 AmbientOcclusionQuality=2:3 TessellationToggle=0:1}; do
      n=${kv%%=*}; v=${kv#*=}; b=${v#*:}; v=${v%%:*}
      w "perf console Render.Setting.$n $v" 3; win "$PERF_PLACE-set-$n$v" "${PERF_SKIP_S:-15}"; m "$PERF_PLACE-set-$n$v"
      w "perf console Render.Setting.$n $b" 3
    done
    win "$PERF_PLACE-set-base2" "${PERF_SKIP_S:-15}" ;;
  look)    # the head pitched down PERF_PITCH (-20) degrees (the ground and its shadows)
    x "head 0 0 0 yaw 0 pitch ${PERF_PITCH:--20}" 2 ;;
  pics) ;;
  *) echo "unknown step $s" ;;
  esac
done
win end 1
echo "$(date +%T) done"
grep -E "^PERF " "$R/somvr.log" | cut -c1-400
grep -E "xr: perf" "$R/xr-layer.log" | cut -c1-500
echo quit >> "$R/cmd"
