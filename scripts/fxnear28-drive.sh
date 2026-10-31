#!/bin/bash
# fxnear28-drive.sh - build 28's SOMVR_FX_NEAR at no cost to the render thread (som-b28-fxnear2; docs/re-xr-pipeline.md
# 126). One headless boot at the practice arrival with scripts/headset-build26.env (SOMVR_FX_NEAR=on: the hook in),
# SOMVR_HANDS_TEST=1 and the layer's test file. No sound is played. FXN_STEPS = any of (in this order; default "perf ab"):
#   perf   the render thread's cost by the fold's mode, live: in the meadow (the head level), with `practice group 8`
#          (eight orcs fighting Talion) and at perf27's campfire (30873.5 3900 -32973.7), a window of FXN_S (20) s per
#          mode in FXN_MODES (default "shadow off direct shadow off"; shadow = `fxnear on` + `fxnear mem shadow`, off =
#          `fxnear off`, direct = `fxnear on` + `fxnear mem direct`: build 27's read-back) - the PERF lines named
#          PLACE-MODE-N (boot with SOMVR_PERF=on SOMVR_PERF_S=600: the windows only by the word)
#   ab     at the campfire, the player's path: the fold in eye 0 only at a test radius of 1500,2000 cm, the left hand ahead (its
#          wraith wisps gone in eye 0, the game's in eye 1), then both eyes at 15,30 (nothing folded: both eyes the game's
#          picture through our copy) - a dump each (scripts/xr_compose.py); the FXNEAR lines
#   same   (SOMVR_FX_TEST=1: `fx eyesame 1`, both eyes one pose) at the campfire: eye 0 through our copy, eye 1 the
#          game's own path (`fxnear eye 0`) at 15,30 - the two halves must be the same picture (the floor first: both
#          eyes the game's, `fxnear off`); the control: 1500,2000 in eye 0 (the wisps gone: they must differ); then direct at 15,30 (the same as shadow). Dumps named *-same in
#          RUN/fx27-steps.txt for scripts/fx27-eyediff.py
# FXN_RUN=RUN drives a running boot; FXN_NOQUIT=1 leaves the game running. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FXN_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${FXN_STEPS:-perf ab}; STEPS=${STEPS//_/ }
WS=${FXN_S:-20}
MODES=${FXN_MODES:-shadow off direct shadow off}; MODES=${MODES//_/ }
T0=$(date +%s)
R=${FXN_RUN:-}
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
# d NAME: one stereo frame dumped; NAME FRAME TIME into RUN/fx27-steps.txt (fx27-eyediff.py reads the *-same ones)
d() { local n0 i f; userin && bail "the user is in VR"; n0=$(grep -c 'dump layers frame' "$R/xr-layer.log")
  echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 40); do [ "$(grep -c 'dump layers frame' "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.1; done
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f $(date +%T.%N | cut -c1-12)" | tee -a "$R/fx27-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
# a named window (the preload's PERF line of the window before it is logged when this one starts)
win() { w "perf $1" 0.3; echo "$(date +%T) window $1 (${2:-$WS} s)" | tee -a "$R/perf-steps.txt"; z "${2:-$WS}"; }
mode() { case $1 in
  shadow) w "fxnear mem shadow" 0.3; w "fxnear on" 0.5 ;;
  direct) w "fxnear mem direct" 0.3; w "fxnear on" 0.5 ;;
  off)    w "fxnear off" 0.5 ;;
  esac; }
modes() { local k=0 m; for m in $MODES; do k=$((k + 1)); mode "$m"; win "$1-$m-$k"; done; mode shadow; }
goto() { local a0 f0; a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$L" | tail -1 | cut -c1-170; sleep 4; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 -30 0 0 0"
LUP="hands test L -22 -35 25 0 0 0" # (the left hand ahead: its wraith wisps in view)
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
w "fxnear state" 0.5; grep -E '^(WARN|CONFIG fx_near)' "$L" | head -4 | cut -c1-300
FIRE=no
fire() { [ $FIRE = yes ] && return 0; w "practice remove" 1; goto 30873.5 3900 -32973.7; x "head 0 0 0 yaw 0 pitch -12"; z 2; FIRE=yes; }
if has perf; then
  echo "== perf: the render thread by the fold's mode ($MODES; $WS s each)"
  x "head 0 0 0 yaw 0 pitch 0"; z 2; modes meadow
  w "practice group 8" 6; grep 'group (' "$L" | tail -1 | cut -c1-160; modes crowd8; w "practice remove" 3
  fire; modes fire
  win end 1
  grep -E '^PERF [a-z0-9-]+ t=' "$L" | sed -E 's/^PERF ([a-z0-9-]+) .*render thread \(([0-9]+) presents\): cpu ([0-9.\/]+).*/\1 \2 \3/' |
    grep -E -- '-(shadow|off|direct)-' | tee -a "$R/perf-steps.txt"
  w "fxnear state" 0.5
fi
if has ab; then
  echo "== ab: the fold in eye 0 at 1500,2000 on the player's path (our copy), then both at 15,30"
  fire; mode shadow; w "$LUP" 1
  w "fxnear eye -1" 0.3; w "fxnear 15,30" 0.6; d ab-game
  n0=$(lines); w "fxnear 1500,2000" 0.3; w "fxnear eye 0" 1.0; d ab-eye0-folded; sleep 0.5; d ab-eye0-folded-b
  w "fxnear state" 0.5; since "$n0" '^FXNEAR t=' | head -4 | cut -c1-240
  w "fxnear eye -1" 0.3; w "fxnear 15,30" 1.0; d ab-both-15-30; sleep 0.5; d ab-both-15-30-b; w "fxnear state" 0.5
fi
if has same; then
  echo "== same: both eyes one pose; eye 0 through our copy, eye 1 the game's"
  fire; mode shadow; w "$LUP" 1
  w "fx eyesame 1" 1.2
  w "fxnear off" 1.0; d game-floor-same
  mode shadow; w "fxnear eye 0" 0.3; w "fxnear 15,30" 1.0; d shadow-15-30-same; sleep 0.4; d shadow-15-30-b-same
  w "fxnear 1500,2000" 1.0; d shadow-1500-control-same
  w "fxnear 15,30" 0.3; mode direct; z 1; d direct-15-30-same
  mode shadow; z 1; d shadow-15-30-c-same
  w "fx eyesame 0" 0.6; w "fxnear eye -1" 0.3; w "fxnear state" 0.5
fi
grep -E '^FXNEAR state' "$L" | tail -1 | cut -c1-400
[ -n "${FXN_NOQUIT:-}" ] || { echo quit >> "$R/cmd"; echo "$(date +%T) quit"; }
