#!/bin/bash
# cost13-drive.sh - the frame cost of an env file, headless (build 13's integration, handoff/HANDOFF-build13.md): the
# null compositor paces at 20 Hz, so the frame rate says nothing; this measures the work per frame instead, in the same
# place (the practice arrival) with the same eye size (SOMVR_EYE_SIZE=2015x2217), in phases of COST_S seconds (30):
#   still   the hands at rest, the view ahead (the world, the HUD, the hands, the cover's grid read each stereo frame)
#   swing   the sword drawn and swung across the front (the surfaces' rays, the verbs' reach tests, the arms)
#   pause   the game's pause menu (a short left menu button: the board, the backdrop and its dim with SOMVR_XR_MENU_BACK)
# Per phase: the stereo frames (somvr.log's RATE lines), the game's CPU per frame (all its threads, and the three
# busiest by tid: /proc/PID/task/*/stat utime+stime), the GPU's busy share (amdgpu gpu_busy_percent sampled every 50 ms)
# times the phase's time per frame = the GPU's ms per frame at the clock it ran (pp_dpm_sclk's mean: at 20 Hz the GPU
# clocks down, so busy is high and the ms an upper bound), the layer's
# and the preload's own lines (the surfaces' "us a frame"). Run with scripts/headless_vr.sh (any env file; the test
# hands and the layer's test file) at the same time; COST_WT = a word of the build's path in launch-cmd.txt. The budget
# at 90 Hz: 11.1 ms. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${COST_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PH=${COST_PHASES:-still swing pause}
S=${COST_S:-30}
GPU=${COST_GPU:-$(ls /sys/class/drm/card*/device/gpu_busy_percent 2>/dev/null | head -1)}
SCLK=$(dirname "${GPU:-/nonexistent/x}")/pp_dpm_sclk   # the GPU's shader clock now (the line with '*'), MHz
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
sleep 10
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
tap() { printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${3:-0.3}"
        printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) tap $1 $2"; sleep 0.5; }
GP=$(pgrep -x ShadowOfMordor | head -1)
[ -n "$GP" ] || bail "no game process"
# the threads' ticks (utime + stime, 1/100 s) as "tid ticks" lines
ticks() { for t in /proc/$GP/task/*; do awk -v t="${t##*/}" '{print t, $14 + $15}' "$t/stat" 2>/dev/null; done | sort; }
stereo() { grep -E '^RATE t=' "$L" | tail -1 | grep -oE 'stereo=[0-9]+' | cut -d= -f2; }
# one phase: the samples over S seconds while BODY (a command run in the background) acts
measure() { local name=$1 a b s0 s1 g0 n gsum t0 t1 bp
  ticks > "$R/cost-$name.t0"; s0=$(stereo); t0=$(date +%s.%N)
  ( eval "$2" ) & bp=$!
  gsum=0; n=0; csum=0
  local end=$(( $(date +%s) + S ))
  while [ "$(date +%s)" -lt "$end" ]; do
    userin && { kill $bp 2>/dev/null; bail "the user is in VR"; }
    [ -n "$GPU" ] && gsum=$((gsum + $(cat "$GPU"))) && n=$((n + 1)) &&
      csum=$((csum + $(grep -m1 '\*' "$SCLK" 2>/dev/null | grep -oE '[0-9]+Mhz' | grep -oE '[0-9]+' || echo 0))); sleep 0.05
  done
  ticks > "$R/cost-$name.t1"; s1=$(stereo); t1=$(date +%s.%N)
  wait $bp 2>/dev/null
  local f=$((s1 - s0)) el d tot top
  el=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.3f", b - a }')
  [ "$f" -gt 0 ] || { echo "COST $name: no stereo frames"; return; }
  d=$(join "$R/cost-$name.t0" "$R/cost-$name.t1" | awk '$3 > $2 { print $3 - $2, $1 }' | sort -rn)
  tot=$(echo "$d" | awk '{ s += $1 } END { print s + 0 }')
  top=$(echo "$d" | head -3 | awk -v f="$f" '{ printf " tid %s %.2f", $2, $1 * 10.0 / f }')
  awk -v nm="$name" -v f="$f" -v el="$el" -v tot="$tot" -v top="$top" -v gb="$gsum" -v gn="$n" -v cs="$csum" 'BEGIN {
    printf "COST %s: %d stereo frames in %.1f s (%.1f fps); the game CPU %.2f ms a frame (all threads;%s ms)", nm, f, el,
      f / el, tot * 10.0 / f, top
    if (gn > 0) { g = gb / gn; printf "; GPU busy %.1f %% = %.2f ms a frame (of %.1f ms a frame) at %.0f MHz on average", g,
      g / 100 * el * 1000 / f, el * 1000 / f, cs / gn }
    printf "\n" }'
}
# the controls card away (as climb13-drive.sh)
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
w "hands test R 28 -40 20 0 0 0"; w "hands test L -28 -40 20 0 0 0"
sleep 3
for p in $PH; do
  case $p in
    still) measure still "sleep $S" ;;
    swing)
      w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6; w "hands squeeze R 0" 0.5
      measure swing 'for i in $(seq 1 '"$((S / 2))"'); do printf "%s" "hands swing R -10 -10 50 0 35 0 600" > "'"$R"'/somvr.trigger"; sleep 1;
        printf "%s" "hands swing R 30 -10 45 0 -35 0 600" > "'"$R"'/somvr.trigger"; sleep 1; done' ;;
    pause)
      tap L menu; sleep 3
      grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-160
      measure pause "sleep $S"
      tap L menu; sleep 3 ;;
  esac
done
grep -E '^SURF t=.* last 10 s' "$L" | tail -2 | grep -oE 'the casts [0-9]+ us a frame \(max [0-9]+\)'
echo "$(date +%T) done"; echo quit >> "$R/cmd"
