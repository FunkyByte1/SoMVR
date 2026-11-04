#!/bin/bash
# looks37-boots.sh - som-b37-looks (docs/re-winport.md 7.15): one Windows headless boot of a VR graphics preset, driven
# the same way each time; prints the run. Sandbox off; from any directory (the worktree is this script's).
#   looks37-boots.sh pics LABEL PRESET   the overcast's pictures: a free normal slot (HEADLESS_SLOT=auto, waits up to an
#       hour; LOOKS_SOLO=1: slot 1 of a solo the caller holds), SOMVR_PRACTICE_SKY=dry, scripts/looks36-drive.sh with LK_SKY=DayNoRain (SON_Day03 forced at the arrival
#       and checked at each place), its five places, no PERF window, each view aimed within LOOKS_AIM_TOL (3) degrees
#   looks37-boots.sh perf LABEL PRESET HZ   the frame cost: slot 1 inside a solo the caller holds
#       (scripts/game_slot.sh take NAME solo), WIN_MONADO_RATE=HZ (Monado's real compositor paces the game), the
#       env file's weather (the practice's clear day), scripts/perf27-drive.sh "meadow crowd remove fire sky fire pics"
#       (LOOKS_STEPS), the windows named by the word only (SOMVR_PERF_S / SOMVR_XR_PERF_S 600); LOOKS_CUT_ON/OFF -> the
#       driver's PERF_CUT_ON/OFF (the step `cut`)
# PRESET = NAME:V,... for the slot's render.cfg (HEADLESS_RENDER, over the PC's detection). The run's raw .ppm/.pgm
# dumps are deleted at the end (the marks' eyes.jpg kept). The log: RUN/looks37-boot.txt.
set -u
MODE=${1:?pics|perf}; LABEL=${2:?label}; PRESET=${3:?NAME:V,...}; HZ=${4:-}
SCR=$(cd "$(dirname "$0")" && pwd)
SOMT=$(cd "$SCR/.." && pwd)
OWN=som-b37-looks
OUT=${LOOKS_OUT:-/home/bara/Projects/flat2vr/ShadowOfMordor/logs/looks37}
mkdir -p "$OUT"
LOG=$OUT/boot-$MODE-$LABEL-$(date +%H%M%S).txt
EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' "$SCR/headset-build26.env" | grep -v -E '^SOMVR_PRACTICE_SKY=' | tr '\n' ' ')"
EXTRA+=" SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
gate1() {   # slot 1 inside the caller's solo: the owner and the VR gate
  grep -q "$OWN" /tmp/somvr-boot.lock/owner 2>/dev/null || { echo "STOP: slot 1 is not $OWN's (take the solo first)"; exit 1; }
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && { echo "STOP: the user's session runs"; exit 1; }
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && { echo "STOP: the user is in VR"; exit 1; }
  pgrep -f '[H]and of Fate 2\.' > /dev/null && { echo "STOP: Hand of Fate 2 runs"; exit 1; }
  journalctl -k -b --no-pager 2>/dev/null | grep -qiE 'amdgpu.*(GPU reset|VRAM is lost)' && { echo "STOP: an amdgpu reset this boot"; exit 1; }
  return 0
}
case $MODE in
  pics)   # LOOKS_SOLO=1: slot 1 of the caller's solo (no take of its own)
    EXTRA+=" SOMVR_PRACTICE_SKY=dry"
    if [ "${LOOKS_SOLO:-0}" = 1 ]; then
      gate1
      ( HEADLESS_HOLD=900 HEADLESS_RENDER="$PRESET" SOMVR_EXTRA="$EXTRA" setsid -w "$SCR/headless_vr.sh" --win "$SOMT/mod/somvr" ) > "$LOG" 2>&1 &
    else
      ( HEADLESS_SLOT=auto HEADLESS_OWNER=$OWN HEADLESS_SLOT_WAIT=3600 HEADLESS_HOLD=900 HEADLESS_RENDER="$PRESET" \
          SOMVR_EXTRA="$EXTRA" setsid -w "$SCR/headless_vr.sh" --win "$SOMT/mod/somvr" ) > "$LOG" 2>&1 &
    fi
    ;;
  perf)
    [[ $HZ =~ ^[0-9]{2,3}$ ]] || { echo "perf needs HZ"; exit 1; }
    gate1
    EXTRA+=" SOMVR_PRACTICE_SKY=$(sed -n 's/^SOMVR_PRACTICE_SKY=//p' "$SCR/headset-build26.env" | tail -1)"
    EXTRA+=" SOMVR_XR_PERF=on SOMVR_PERF=on SOMVR_PERF_S=600 SOMVR_XR_PERF_S=600"
    ( WIN_MONADO_RATE=$HZ HEADLESS_HOLD=900 HEADLESS_RENDER="$PRESET" SOMVR_EXTRA="$EXTRA" \
        setsid -w "$SCR/headless_vr.sh" --win "$SOMT/mod/somvr" ) > "$LOG" 2>&1 &
    ;;
  *) echo "pics or perf"; exit 1 ;;
esac
BP=$!
RUN=""
for i in $(seq 1 4000); do
  RUN=$(sed -n 's/^windows: \([^ ]*\) .*/\1/p' "$LOG" | head -1)
  [ -n "$RUN" ] && break
  kill -0 $BP 2>/dev/null || break
  sleep 1
done
[ -n "$RUN" ] || { echo "no run (see $LOG)"; wait $BP; tail -5 "$LOG"; exit 1; }
echo "$LABEL ($MODE${HZ:+ $HZ Hz}, $PRESET): $RUN" | tee -a "$OUT/runs.txt"
for i in $(seq 1 300); do [ -f "$RUN/somvr.log" ] && break; kill -0 $BP 2>/dev/null || break; sleep 1; done
if [ "$MODE" = pics ]; then
  BG_RUN=$RUN LK_SKY=DayNoRain LK_PERF_S=0 LK_AIM_TOL=${LOOKS_AIM_TOL:-3} bash "$SCR/looks36-drive.sh" > "$RUN/looks37-drive.txt" 2>&1
else
  BG_RUN=$RUN PERF_STEPS="${LOOKS_STEPS:-meadow crowd remove fire sky fire pics}" PERF_CUT_ON="${LOOKS_CUT_ON:-}" \
    PERF_CUT_OFF="${LOOKS_CUT_OFF:-}" bash "$SCR/perf27-drive.sh" > "$RUN/looks37-drive.txt" 2>&1
fi
echo "driver exit $? ($(date +%T))" >> "$RUN/looks37-drive.txt"
wait $BP; RC=$?
cp -f "$LOG" "$RUN/looks37-boot.txt" 2>/dev/null
# the marks' both-eye pictures (the layer's eyes.ppm, its quads in) kept as eyes.jpg before the raw dumps go
python3 -I - "$RUN" <<'EOF'
import glob, os, sys
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], 'marks', '*', 'eyes.ppm'))):
    j = p[:-4] + '.jpg'
    if not os.path.exists(j):
        Image.open(p).convert('RGB').save(j, quality=90)
EOF
find "$RUN" -maxdepth 3 -type f \( -name '*.ppm' -o -name '*.pgm' \) -delete
echo "$LABEL: harness exit $RC; $(grep -c '^WARN' "$RUN/somvr.log" 2>/dev/null) WARN in somvr.log; $(grep -E 'save .*(UNCHANGED|CHANGED)|install' "$LOG" | tail -2 | tr '\n' ' ' | cut -c1-200)" | tee -a "$OUT/runs.txt"
