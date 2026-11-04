#!/bin/bash
# watch33-drive.sh - build 33 (docs/re-xr-pipeline.md 146): the wrist watch's raise-to-read face and the fog at the
# 11-03 session's mark-21 spot. Boot scripts/headless_vr.sh with headset-build26.env + SOMVR_HANDS_TEST=1
# SOMVR_FX_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt (W33_WT = the worktree or build name in launch-cmd.txt). Every picture
# is one lossless stereo dump; RUN/w33-steps.txt names them. W33_STEPS (default "watch fog"):
#   watch  the left wrist read (the forearm across the chest, the back of the wrist tilted to the eyes), turned away
#   fog    mark 21's spot (30689 3726 -29639, world yaw 4.8, pitch -10): the head at -30/0/+30 deg, down 25 deg into the
#          valley, then `fx nofog 1`; W33_WEATHER = a weather for `fx console ChangeWeather` first; W33_PERF=1 = a 25 s
#          `perf fog21` window at the view (and `perf nofog21` with `fx nofog 1`); W33_NEED_WEATHER = quit at once unless the world's weather is that one (boot with SOMVR_PERF=on SOMVR_XR_PERF=on SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor; WT=${W33_WT:?worktree or build name}; STEPS=${W33_STEPS:-watch fog}
T0=$(date +%s); R=""
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -8); do   # (b35: up to 3 slots start runs)
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
bash "$(dirname "$0")/render_need.sh" "$R" watch33-drive.sh || exit 1   # b37: a Windows run at WIN_RENDER=vr (re-xr-pipeline.md 158.5)
echo "run $R"
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
if [ -n "${W33_NEED_WEATHER:-}" ] && ! grep "light: weather" "$R/somvr.log" | tail -1 | grep -q "'$W33_NEED_WEATHER'"; then
  echo "WRONG WEATHER: $(grep -o "light: weather '[A-Za-z0-9_]*'" "$R/somvr.log" | tail -1) (want $W33_NEED_WEATHER)"; echo quit >> "$R/cmd"; exit 3
fi
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
m() { sleep "${2:-0.6}"; w "mark $1" 3.5; }
lastyaw() { sed -n 's/^head .* yaw \(-\?[0-9.]*\) deg.*/\1/p' "$R/marks/$(ls -t "$R/marks" | head -1)/state.txt" | head -1; }
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$R/xr-layer.log")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$R/xr-layer.log")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  echo "$1 $(grep -o 'dump stereo frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')" | tee -a "$R/w33-steps.txt"; }
add() { python3 -c "print(round(($1)+($2),1))"; }
x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
REST="hands test R 22 -35 -30 0 180 0"; w "$REST" 0.6; w "front keep 0" 0.3
for s in $STEPS; do
  case $s in
    watch)
      # the layer's left controller (SOMVR_XR_TEST `hand L head`): the forearm across the chest, the back of the wrist up
      # and tilted 40 deg toward the eyes (test_vrhands.c's read pose), then turned about the forearm 60 / 90 deg away
      x "head 0 0 0 yaw 0 pitch -30" 2
      for v in "read -140" "away60 -200" "away90 -230" "read2 -140"; do
        set -- $v; x "hand L head -5 -22 30 $2 0 90" 1.5; dq "watch-$1"
      done
      grep -E "watch face (seen|away)" "$R/xr-layer.log" | tail -6 | cut -c1-150
      w "hands test L -25 -35 -30 0 0 0" 0.6 ;;
    fog)
      [ -n "${W33_WEATHER:-}" ] && w "fx console GameServerShell.ChangeWeather $W33_WEATHER" "${W33_WEATHER_S:-12}"   # (SOMVR_FX_TEST; the change takes a while)
      a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
      w "practice go 30689.5 3790 -29639.3" 1
      for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
      grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170
      sleep 4; x "head 0 0 0 yaw 0 pitch 0" 1; m fog21-yaw0 0.2; y=$(lastyaw)
      d=$(python3 -c "d=(4.8)-($y); d=(d+180)%360-180; print(round(d,1))")
      echo "fog21: world yaw $y at test yaw 0 -> test yaw $d" | tee -a "$R/w33-steps.txt"
      x "head 0 0 0 yaw $d pitch -10" 4
      for v in "y0 0" "yp30 30" "y0b 0" "ym30 -30" "y0c 0"; do set -- $v; x "head 0 0 0 yaw $(add "$d" "$2") pitch -10" 3; dq "fog21-$1"; done
      x "head 0 0 0 yaw $d pitch -25" 3; dq "fog21-down25"   # into the valley over the rim (som-b33-fogray)
      x "head 0 0 0 yaw $(add "$d" 30) pitch -25" 3; dq "fog21-down25-yp30"
      if [ "${W33_PERF:-0}" = 1 ]; then   # the frame's cost held still at the view (SOMVR_PERF/XR_PERF windows by word)
        x "head 0 0 0 yaw $d pitch -10" 3; x "perf fog21" 0.3; w "perf fog21" 25; x "perf after" 0.3; w "perf after" 3
      fi
      w "fx nofog 1" 0.5; x "head 0 0 0 yaw $d pitch -10" 3; dq "fog21-nofog-y0"
      if [ "${W33_PERF:-0}" = 1 ]; then   # the same view without the fog (this boot's own baseline, the same weather)
        x "perf nofog21" 0.3; w "perf nofog21" 25; x "perf after" 0.3; w "perf after" 3
      fi
      w "fx nofog 0" 0.5 ;;
  esac
done
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"
