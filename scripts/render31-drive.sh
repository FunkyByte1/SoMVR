#!/bin/bash
# render31-drive.sh - build 31's render round (docs/re-xr-pipeline.md 137): the sun's rays and the fog wall that moved
# with the head (the user's 11-02 marks 5 and 6, the practice cliff 31832 4820 -30530, weather SON_Day01). Boot
# scripts/headless_vr.sh with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, SOMVR_FX_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt.
# Every picture is one lossless stereo dump (the layer's <log>.stereoN.ppm with its `xr: dump layers` poses and fovs; the
# name and the frame in RUN/fx-steps.txt; scripts/render31-yaw.py compares them). RD_STEPS (default "sun fog"):
#   sun   the sun found by `fx fsprobe` (the farthest flare sprite), faced; the head turned -15/0/+15 deg and rolled
#         -25/+25 deg with build 30's SOMVR_FLATFX (the five old parts), with `stars` too (flatfx all), and with the stars
#         drawn and `fxbill eye`
#   fog   the session's mark-6 view (world yaw -8, pitch -10): the head at -30/0/+30 deg with the game's look, each
#         ClientFX renderer hidden (`fx hide 1|2|4`), the fog overlay skipped (`fx nofog 1`), and `fxbill eye` (also
#         rolled -25/+25)
# RD_SUNV / RD_FOGV: the variants (default "before stars bill" / "before hide1 hide2 hide4 nofog bill").
#   perf  the frame's cost with SOMVR_FX_BILLBOARD off and on in three places (see the step)
# BG_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${RD_STEPS:-sun fog}
OLDALL="flares,shafts,vignette,rain,overlay"   # build 30's SOMVR_FLATFX=all
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
m() { sleep "${2:-0.6}"; w "mark $1" 3.5; echo "$(date +%T) mark $1 -> $(ls -t "$R/marks" 2>/dev/null | head -1)"; }
lastyaw() { sed -n 's/^head .* yaw \(-\?[0-9.]*\) deg.*/\1/p' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt" | head -1; }
# one lossless stereo dump; NAME and its frame noted
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  echo "$1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')" | tee -a "$R/fx-steps.txt"; }
# the head at test yaw Y, pitch P, roll O; settle (the exposure re-adapts), dump as NAME
pose() { x "head 0 0 0 yaw $1 pitch $2 roll $3" "${PSET:-3}"; dq "$4"; }
add() { python3 -c "print(round(($1)+($2),1))"; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 -30 0 0 0"
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
w "flatfx state" 0.5; w "fxbill state" 0.5
w "${RD_CONSOLE:-fx console} GameServerShell.ChangeWeather ${RD_WEATHER:-SON_Day01}" 10   # (no SOMVR_FX_TEST: RD_CONSOLE="perf console")
# go to X Y Z and face the head's world yaw YAW (deg) pitched P: TURN = the test yaw for that world yaw
TURN=0
goface() {
  local a0 f0 y d; a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170
  sleep "${7:-3}"; x "head 0 0 0 yaw 0 pitch 0" 1; m "$5-yaw0" 0.2; y=$(lastyaw); echo "yaw now $y (want $4)"
  d=$(python3 -c "d=($4)-($y); d=(d+180)%360-180; print(round(d,1))")
  x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned" 0.2; y2=$(lastyaw); echo "yaw now $y2 after +$d"
  if python3 -c "import sys; e=lambda a: abs((($4)-(a)+180)%360-180); sys.exit(0 if e($y2) > e($y) else 1)"; then
    d=$(python3 -c "print(-($d))"); x "head 0 0 0 yaw $d pitch $6" 1.2; m "$5-turned2" 0.2; y2=$(lastyaw); echo "yaw now $y2 after $d"
  fi
  TURN=$d; Y0=${y2:-$4}   # the world yaw measured at test yaw TURN
}
for s in $STEPS; do
  case $s in
  sun)
    echo "== sun"; w "flatfx $OLDALL" 0.5; w "fxbill game" 0.3
    goface 31832 4880 -30530 ${RD_SUN_YAW:-10} sun ${RD_SUN_PITCH:-8} 4
    # the sun: the farthest flare sprite the probe sees (world yaw atan2(dx, dz), elevation; SOMVR_FX_TEST only - else
    # RD_SUN_YAW / RD_SUN_PITCH: boot 112029 found it at -32.3, 17.4)
    n0=$(cnt '^FXFSPROBE'); [ -n "${RD_SUN_KNOWN:-}" ] || w "fx fsprobe 24" 6
    SUN=$(grep '^FXFSPROBE' "$R/somvr.log" | tail -n +$((n0 + 1)) | python3 -c "
import sys, re, math
best = None
for l in sys.stdin:
    m = re.search(r'the sprite at ([-\d.]+),([-\d.]+),([-\d.]+) q .*?, ([\d.]+) m from the camera \(([-\d.]+),([-\d.]+),([-\d.]+)\)', l)
    if not m: continue
    s = [float(m.group(i)) for i in (1, 2, 3)]; c = [float(m.group(i)) for i in (5, 6, 7)]; dist = float(m.group(4))
    if best is None or dist > best[0]: best = (dist, s, c)
if best:
    d = [best[1][i] - best[2][i] for i in range(3)]
    print('%.1f %.1f %.0f' % (math.degrees(math.atan2(d[0], d[2])), math.degrees(math.atan2(d[1], math.hypot(d[0], d[2]))), best[0]))
")
    echo "the sun (world yaw, elevation, m): ${SUN:-not found}"
    if [ -n "$SUN" ]; then SY=${SUN%% *}; SP=$(echo "$SUN" | cut -d' ' -f2); else SY=${RD_SUN_YAW:-10}; SP=${RD_SUN_PITCH:-8}; fi
    [ -n "${RD_SUN_KNOWN:-}" ] && { SY=${RD_SUN_KNOWN% *}; SP=${RD_SUN_KNOWN#* }; }
    ST=$(add "$TURN" "$SY - $Y0"); SPI=$(python3 -c "print(round(max(-60, min(60, $SP - 4)), 1))")
    echo "sun: test yaw $ST pitch $SPI" | tee -a "$R/fx-steps.txt"
    x "head 0 0 0 yaw $ST pitch $SPI" 3; m sun-faced 0.2
    for v in ${RD_SUNV:-before stars bill}; do
      case $v in
        before) w "flatfx $OLDALL" 0.5; w "fxbill game" 0.5 ;;
        stars) w "flatfx all" 0.5; w "fxbill game" 0.5 ;;
        bill) w "flatfx $OLDALL" 0.5; w "fxbill eye" 0.5 ;;
      esac
      pose "$ST" "$SPI" 0 "sun-$v-y0"
      pose "$ST" "$SPI" -25 "sun-$v-rm25"
      pose "$ST" "$SPI" 0 "sun-$v-y0b"
      pose "$ST" "$SPI" 25 "sun-$v-rp25"
      pose "$(add "$ST" -15)" "$SPI" 0 "sun-$v-ym15"
      pose "$(add "$ST" 15)" "$SPI" 0 "sun-$v-yp15"
    done
    w "fxbill game" 0.3; w "flatfx all" 0.3; w "flatfx state" 0.3 ;;
  fog)
    echo "== fog"; w "flatfx all" 0.5; w "fxbill game" 0.3; w "fx hide 0" 0.3; w "fx nofog 0" 0.3
    [ "$Y0" ] || goface 31832 4880 -30530 -8 fog -10 4
    FT=$(add "$TURN" "${RD_FOG_YAW:--8} - $Y0"); FP=${RD_FOG_PITCH:--10}
    echo "fog: test yaw $FT pitch $FP" | tee -a "$R/fx-steps.txt"
    x "head 0 0 0 yaw $FT pitch $FP" 3; m fog-faced 0.2
    for v in ${RD_FOGV:-before hide1 hide2 hide4 nofog bill}; do
      case $v in
        before) ;;
        hide1) w "fx hide 1" 0.5 ;;
        hide2) w "fx hide 2" 0.5 ;;
        hide4) w "fx hide 4" 0.5 ;;
        nofog) w "fx hide 0" 0.3; w "fx nofog 1" 0.5 ;;
        bill) w "fx nofog 0" 0.3; w "fxbill eye" 0.5 ;;
      esac
      # the same pose again between the turns: what the mist's own drift changes in that time (the baseline)
      pose "$FT" "$FP" 0 "fog-$v-y0"
      pose "$(add "$FT" 30)" "$FP" 0 "fog-$v-yp30"
      pose "$FT" "$FP" 0 "fog-$v-y0b"
      pose "$(add "$FT" -30)" "$FP" 0 "fog-$v-ym30"
      pose "$FT" "$FP" 0 "fog-$v-y0c"
      if [ $v = bill ] || [ $v = before ]; then pose "$FT" "$FP" 25 "fog-$v-rp25"; pose "$FT" "$FP" 0 "fog-$v-y0d"; fi
    done
    w "fxbill game" 0.3; w "fx hide 0" 0.3; w "fx nofog 0" 0.3
    x "head 0 0 0 yaw $FT pitch $FP" 2; m fog-end 0.2 ;;
  perf)  # the frame's cost with SOMVR_FX_BILLBOARD off and on (boot with SOMVR_FX_BILLBOARD=eye: the hooks in; no
         # SOMVR_FX_TEST): in the misty valley (the fog view), the practice meadow and the build 20c campfire, windows of
         # RD_PERF_S (20) s named by `perf NAME` (the preload's PERF lines, the layer's `xr: perf NAME:` lines), off/on/off/on
    PS=${RD_PERF_S:-20}
    pwin() { w "perf $1" 0.3; x "perf $1" 0.3; echo "$(date +%T) window $1 ($PS s)" | tee -a "$R/perf-steps.txt"; sleep "$PS"; }
    for place in valley meadow fire; do
      case $place in
        valley) x "head 0 0 0 yaw ${FT:-0} pitch ${FP:--10}" 3 ;;
        meadow) w "practice go 32500 2100 -24800" 12; x "head 0 0 0 yaw 0 pitch 0" 3 ;;
        fire) w "practice go 30873.5 3900 -32973.7" 12; x "head 0 0 0 yaw 0 pitch -12" 3 ;;
      esac
      for k in 1 2; do
        w "fxbill game" 0.5; pwin "$place-game-$k"
        w "fxbill eye" 0.5; pwin "$place-bill-$k"
      done
      w "fxbill state" 0.3
    done
    pwin end 1 ;;
  esac
done
w "flatfx state" 1
w "fxbill state" 1
w "fxnear state" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
