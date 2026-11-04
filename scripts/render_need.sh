#!/bin/bash
# render_need.sh RUN [WHO] - som-b37-slotsvram (docs/re-xr-pipeline.md 158.5): the check a driver makes that judges
# pictures, grass, foliage, fog, weather, GPU or frame time: its Windows run must be at WIN_RENDER=vr (RUN/render.mode,
# headless_win.sh writes it before the game starts). Exit 0 = vr, or a Linux run (boot_check: its preset as it has it),
# or RENDER_ANY=1 (a deliberate look at light). Exit 1 = the run is light/asis: it says so and asks the run to quit
# (a `quit` line in RUN/cmd), so the slot frees at once instead of running a judgement on the wrong look.
R=${1:?usage: render_need.sh RUN [WHO]}; WHO=${2:-this driver}
[ "${RENDER_ANY:-0}" = 1 ] && exit 0
for i in $(seq 1 120); do [ -s "$R/launch-cmd.txt" ] && break; sleep 0.5; done
grep -q 'headless_win\.sh' "$R/launch-cmd.txt" 2>/dev/null || exit 0      # (not a Windows run)
for i in $(seq 1 120); do
  [ -s "$R/render.mode" ] && break
  grep -q -E 'render\.cfg: |STOP' "$R/summary.txt" 2>/dev/null && [ $i -gt 4 ] && break   # (an older harness: no file)
  sleep 0.5
done
m=$(cut -d' ' -f1 "$R/render.mode" 2>/dev/null)
[ "$m" = vr ] && exit 0
if [ -z "$m" ]; then   # an older headless_win.sh (before b37): it always seeded the VR preset unless asis
  grep -q "render.cfg: the PC's detection with the VR preset" "$R/summary.txt" 2>/dev/null && exit 0
  echo "note render: run ${R##*/} has no render.mode and no render.cfg line (yet): its look is unknown - going on"; exit 0
fi
echo "FAIL render: $WHO judges the look or the frame time and needs WIN_RENDER=vr; run ${R##*/} is $(cat "$R/render.mode") - boot it with WIN_RENDER=vr (docs/re-xr-pipeline.md 158.5); the run is asked to quit"
echo quit >> "$R/cmd"
exit 1
