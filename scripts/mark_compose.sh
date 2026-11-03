#!/bin/bash
# mark_compose.sh - build 31b (docs/re-xr-pipeline.md 139): each bug mark's picture as the headset showed it. A mark of
# a stereo frame (the layer's SOMVR_XR_MARK_LAYERS, default on) leaves RUN/marks/<id>/xr.log (both eyes and every quad
# of that frame) with xr.log.hudN.ppm, xr.log.handsN.eK.ppm, xr.log.vrmenuN.ppm, xr.log.objectiveN.ppm (+ .alpha.pgm)
# beside eyes.ppm (or the harness's eyes.jpg); this composes them with xr_compose.py --eyes (beside this script) into
#   marks/<id>/seen.jpg        both eyes, half size, the layer's quads (the HUD, the threat glows, low health's ring, the
#                              wrist/palm panel, the menus, the prompt tiles, the objective marker, our hands) over them
#   marks/<id>/seen/<quad>.png the left eye around each quad, full size
#   marks/<id>/seen.txt        xr_compose.py's report (each quad's distance, size, pixels per degree)
# and removes the raw pictures once seen.jpg exists (KEEP=1 keeps them). A mark already composed is skipped.
#   mark_compose.sh RUN_DIR [--budget SECONDS]   (stop starting a new mark after SECONDS; default: no limit)
# At the lowest priority (nice 19, SCHED_OTHER: a real-time parent's class is dropped first - a SCHED_FIFO compose
# pinned to one CPU stalled RCU and hung the machine on 11-02 19:37); about 2 s and 0.8 GB per mark. Run by
# boot_check.sh's cleanup after the game has stopped (never while it plays).
set -u
chrt -o -p 0 $$ > /dev/null 2>&1 || true   # (a real-time caller's class is not ours: back to the ordinary scheduler)
RUN=${1:?usage: mark_compose.sh RUN_DIR [--budget SECONDS]}
RUN=$(cd "$RUN" && pwd) || exit 2   # (absolute: the removals below)
BUDGET=0
[ "${2:-}" = --budget ] && BUDGET=${3:-0}
HERE=$(cd "$(dirname "$0")" && pwd)
XC=$HERE/xr_compose.py
[ -r "$XC" ] || { echo "mark_compose: no xr_compose.py beside $0"; exit 2; }
T0=$(date +%s) n=0 skipped=0 failed=0
for d in "$RUN"/marks/*/; do
  d=${d%/}
  [ -s "$d/xr.log" ] || continue
  [ -s "$d/seen.jpg" ] && { skipped=$((skipped + 1)); continue; }
  if [ "$BUDGET" -gt 0 ] && [ $(( $(date +%s) - T0 )) -ge "$BUDGET" ]; then
    echo "mark_compose: the budget ($BUDGET s) is used: the rest later (mark_compose.sh $RUN)"; break
  fi
  eyes=$d/eyes.ppm; [ -s "$eyes" ] || eyes=$d/eyes.jpg
  [ -s "$eyes" ] || { echo "mark_compose: ${d##*/}: no eyes picture yet"; failed=$((failed + 1)); continue; }
  rm -rf -- "$d/seen"
  if nice -n 19 python3 -I "$XC" "$d/xr.log" --eyes "$eyes" -o "$d/seen" > "$d/seen.txt" 2>&1; then
    f=
    for c in "$d/seen"/frame*.png; do case ${c##*/} in *-*) ;; frame*.png) [ -e "$c" ] && f=$c ;; esac; done
    if [ -n "$f" ] && nice -n 19 magick "$f" -quality 85 "$d/seen.jpg" 2>> "$d/seen.txt"; then
      rm -f -- "$f"
      for c in "$d/seen"/frame*-*.png; do [ -e "$c" ] && mv -f -- "$c" "$d/seen/$(basename "$c" | sed 's/^frame[0-9]*-//')"; done
      [ "${KEEP:-0}" = 1 ] || rm -f -- "$d"/xr.log.*.ppm "$d"/xr.log.*.pgm
      n=$((n + 1))
      continue
    fi
  fi
  echo "mark_compose: ${d##*/}: not composed (see $d/seen.txt)"; failed=$((failed + 1))
done
echo "mark_compose: $n composed, $skipped already, $failed not ($(( $(date +%s) - T0 )) s)"
