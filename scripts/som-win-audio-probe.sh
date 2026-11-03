#!/bin/bash
# som-win-audio-probe.sh - where the Windows build's sound goes under Proton's wine, proven with SILENCE only
# (docs/re-winport.md 7.10): mod/somvr/win/tests/silence.c (built here into build/win/silence.exe) opens the default
# render endpoint as Wwise does (WASAPI eRender/eConsole, shared mode) and writes zero-filled buffers flagged silent;
# Wine's winepulse makes a PulseAudio stream for it. This script makes a null sink of its own (module-null-sink, unloaded
# at the end), runs the program with PULSE_SINK=<that sink> in a copy of our prefix, and reads the stream while it runs:
# its sink, its properties (how the launcher finds the game's streams). Nothing audible can play (the samples are zeros)
# and the null sink stays loaded for the whole run. Run it OUTSIDE the Bash sandbox; it needs no GPU and no game slot.
#   som-win-audio-probe.sh [SECONDS]      (default 5) -> logs/win-launch/audio-probe-<ts>/probe.txt
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")/.." && pwd)            # the checkout this script is in (its silence.c)
SECS=${1:-5}; [[ $SECS =~ ^[0-9]+$ ]] || { echo "STOP: seconds"; exit 1; }
PROTON_FILES=${WIN_PROTON_FILES:-"/home/bara/.local/share/Steam/steamapps/common/Proton - Experimental/files"}
TEMPLATE=$SOM/logs/winport/prefix
OUT=$SOM/logs/win-launch/audio-probe-$(date +%Y%m%d-%H%M%S); mkdir -p "$OUT"
say() { echo "$*" | tee -a "$OUT/probe.txt"; }
X=/home/bara/Projects/flat2vr/shared/toolchain/xwin-sdk64
EXE=$HERE/mod/somvr/build/win/silence.exe
if [ ! -f "$EXE" ] || [ "$EXE" -ot "$HERE/mod/somvr/win/tests/silence.c" ]; then
  mkdir -p "$(dirname "$EXE")/obj"
  clang --target=x86_64-pc-windows-msvc -std=gnu11 -O2 -Wall -Wno-unused-parameter -isystem "$X/crt/include" \
    -isystem "$X/sdk/include/ucrt" -isystem "$X/sdk/include/um" -isystem "$X/sdk/include/shared" -D_CRT_SECURE_NO_WARNINGS \
    -c -o "$(dirname "$EXE")/obj/silence.obj" "$HERE/mod/somvr/win/tests/silence.c" &&
  lld-link /nologo /out:"$EXE" /subsystem:console /machine:x64 /ignore:4099 /libpath:"$X/crt/lib/x86_64" \
    /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" "$(dirname "$EXE")/obj/silence.obj" \
    libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib ole32.lib || { say "STOP: silence.exe did not build"; exit 1; }
fi
[ -d "$TEMPLATE/pfx/drive_c/windows/system32" ] || { say "STOP: no prefix $TEMPLATE"; exit 1; }
PFX=$OUT/prefix
cp -a --reflink=auto "$TEMPLATE" "$PFX" || { say "STOP: the prefix copy failed"; exit 1; }
rm -rf "$PFX/pfx/drive_c/users/steamuser/Documents/WB Games"   # (a save copy: not needed here)

NAME=somvr_probe_null_$$
DEF0=$(pactl get-default-sink 2>/dev/null)
MODID=$(pactl load-module module-null-sink "sink_name=$NAME" "sink_properties=device.description=$NAME") ||
  { say "STOP: no null sink"; exit 1; }
unload() { pactl unload-module "$MODID" 2>/dev/null; }
trap unload EXIT
pactl list short sinks | grep -q -F "$NAME" || { say "STOP: the null sink $NAME is not listed"; exit 1; }
say "null sink $NAME (module $MODID); the default sink: $DEF0"

WS=$PROTON_FILES/bin/wineserver
( cd / && env -u DISPLAY -u WAYLAND_DISPLAY -u LD_PRELOAD WINEPREFIX="$PFX/pfx" WINEDEBUG=-all \
    WINEDLLOVERRIDES="winealsa.drv=d;wineoss.drv=d" PULSE_SINK="$NAME" \
    timeout -k 5 $((SECS + 60)) "$PROTON_FILES/bin/wine" "$EXE" "$SECS" > "$OUT/wine.out" 2>&1 < /dev/null ) &
PP=$!
seen=""
for i in $(seq 1 $(( (SECS + 50) * 4 ))); do
  kill -0 $PP 2>/dev/null || break
  pactl list sink-inputs > "$OUT/sink-inputs.now" 2>/dev/null
  if grep -q -i -E 'silence\.exe|wine' "$OUT/sink-inputs.now"; then seen=1; cp "$OUT/sink-inputs.now" "$OUT/sink-inputs.txt"; fi
  sleep 0.25
done
wait $PP; RC=$?
env WINEPREFIX="$PFX/pfx" "$WS" -k 2>/dev/null
say "silence.exe exit $RC:"; sed 's/^/  /' "$OUT/wine.out" | grep 'silence:' | tee -a "$OUT/probe.txt"
if [ -n "$seen" ]; then
  # the stream's sink by index -> name
  sidx=$(awk '/^Sink Input #/{s=""} /^\s*Sink: /{s=$2} /silence\.exe|wine/{ if (s != "") { print s; exit } }' "$OUT/sink-inputs.txt")
  sname=$(pactl list short sinks | awk -v i="$sidx" '$1 == i { print $2 }')
  say "the stream's sink: #$sidx ${sname:-?} ($([ "$sname" = "$NAME" ] && echo 'the null sink: PULSE_SINK is honoured' || echo 'NOT the null sink'))"
  grep -E 'application\.(name|process\.binary|process\.id)|media\.name|node\.name|target\.object' "$OUT/sink-inputs.txt" | sed 's/^\s*/  /' | sort -u | tee -a "$OUT/probe.txt"
else
  say "no stream from wine seen"
fi
say "the default sink after: $(pactl get-default-sink 2>/dev/null) (before: $DEF0)"
rm -rf "$PFX"
