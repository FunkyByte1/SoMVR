#!/bin/bash
# reload37-drive.sh - som-b37-live3's hot-reload proof (docs/re-xr-pipeline.md 163.11) in a headless Windows game: a
# patch applied by the VR menu's Apply patch (the player's swap, the host's split), then: the session's frame file
# (SOMVR_PERF_RAW) keeps growing - one header, the reload's "appends" marker, frames after the swap; the input recording
# of the reload's module in <log>.g1.inrec beside the boot's (kept); the lane's watcher, made to follow this run's frames
# (a protect file of our own, loop on, removed at the end), logs no FRAMES-stopped line and holds no "probe" for long;
# a voice note after the swap gets its words within seconds and an answer is said (the speech never frozen). Boot as
# live37-drive.sh says (WIN_MARK_VOICE=NOTE.wav). LV_WT = the worktree's name in launch-cmd.txt; LV_RUN=RUN.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${LV_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
T0=$(date +%s); R=${LV_RUN:-}
PROT=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/somvr-lane/protect.d/live3-reload.conf
trap 'rm -f -- "$PROT"' EXIT
for i in $(seq 1 "${LV_FIND:-720}"); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -L -c %Y "$d")" -ge "$T0" ] && R=$(readlink -f "$d") && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/reload37-steps.txt; P=$R/perf-raw.txt
LANELOG=$SOM/logs/lane/lane-$(date +%Y%m%d).log
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$S"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -aE "$2"; }
waitfor() { local n0=$1 re=$2 f=$3 s=${4:-20} i; for i in $(seq 1 $((s * 4))); do since "$n0" "$re" "$f" | grep -q . && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$S"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$S"; }
for i in $(seq 1 120); do grep -q '^SOMVR_LIVE_DIR=' "$R/env.txt" 2>/dev/null && break; sleep 1; done
LD=$(sed -n 's/^SOMVR_LIVE_DIR=//p' "$R/env.txt" | tail -1)
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 6
[ -s "$P" ] || bail "no $P (SOMVR_PERF=on and SOMVR_PERF_RAW in the boot?)"
mkdir -p "$(dirname "$PROT")"; printf 'raw=%s\nloop=on\n' "$P" > "$PROT"   # the watcher follows this run's frames
nlane=$(lines "$LANELOG" 2>/dev/null || echo 0)
grep -aE '^CONFIG perf raw|^CONFIG input rec' "$L" | cut -c1-200 | tee -a "$S"
x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"; LOW="hand L head -25 -50 30 0 0 0"
press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
          x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
# 1. the patch: the build's own logic DLL (the same tree: kind live), staged unchecked (the checks ran at the commit)
MODD=$(sed -n -e 's/^# mod (windows, headless_win.sh): //p' -e 's/^# headless_win.sh [^ ]* (the Windows build; MOD \(.*\))$/\1/p' "$R/launch-cmd.txt" | head -1)
n0=$(lines "$X")
"$HERE/live_patch.sh" stage "$MODD/build/win/somvr_logic.dll" "The reload proof: the same code again" "Nothing to try" --dir "$LD" --force-unchecked | tee -a "$S"
pid=$(cat "$LD/patches/ready" 2>/dev/null)
waitfor "$n0" "xr: live: patch $pid staged" "$X" 6 && pass "the layer: $(since "$n0" 'patch .* staged' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-120)" \
  || fail "no 'patch $pid staged'"
x "$LOW"; sleep 3
# 2. Apply patch: the palm, the Test tab, Apply
x "$PALM_AWAY"; sleep 0.6; x "$PALM s 0.3"; sleep 1.5
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
WTD=$(cd "$HERE/.." && pwd -P)
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
    "$WTD/mod/somvr/somvr_font.c" -lm 2>> "$S" && "$R/vm_uv" "$(python3 -c "print(hex(int('${AV:-0}', 16) | 1 << 43 | 1 << 63))")" > "$R/vm_uv.txt"
tu=$(awk -F'\t' '$1 == "Test" && $2 == "button" { print $4, $5; exit }' "$R/vm_uv.txt"); [ -n "$tu" ] || tu="0.900 0.0395"
press $tu; sleep 1
sz0=$(stat -c %s "$P"); nl=$(lines); nx=$(lines "$X")
press 0.2578 0.7892
waitfor "$nl" 'RELOAD gen 1 ok' "$L" 40 && pass "the swap: $(since "$nl" 'RELOAD gen 1 ok' | tail -1 | cut -c1-200)" || fail "no 'RELOAD gen 1 ok' ($(since "$nl" 'RELOAD|LIVE request' | tail -2 | tr '\n' ' ' | cut -c1-240))"
since "$nl" 'CONFIG perf raw' | tail -1 | cut -c1-200 | tee -a "$S"
x "$PALM_AWAY"; sleep 0.6; x "$LOW"; x "hand R off"
# 3. the frame file: growing after the swap, one header, the reload's marker
sleep 10; sz1=$(stat -c %s "$P"); sleep 5; sz2=$(stat -c %s "$P")
tsw=$(since "$nl" 'RELOAD gen 1 ok' | head -1 | sed -n 's/^WIN t=\([0-9.]*\).*/\1/p')
nh=$(grep -c '^# somvr perf raw 1:' "$P"); nm=$(grep -c "^# somvr perf raw: a hot reload's module appends" "$P")
after=$(awk -v t="${tsw:-0}" '$1 !~ /^#/ && $1 + 0 > t + 0 { n++ } END { print n + 0 }' "$P")
mono=$(awk '$1 !~ /^#/ { if (p != "" && $1 + 0 < p + 0) b++; p = $1 } END { print b + 0 }' "$P")
[ "$sz1" -gt "$sz0" ] && [ "$sz2" -gt "$sz1" ] && [ "$nh" = 1 ] && [ "$nm" = 1 ] && [ "$after" -gt 100 ] && [ "$mono" = 0 ] \
  && pass "perf-raw.txt grows across the swap: $sz0 -> $sz1 -> $sz2 bytes, 1 header, the reload's marker, $after frames after t=$tsw, times never back" \
  || fail "perf-raw.txt: $sz0 -> $sz1 -> $sz2 bytes, headers $nh, markers $nm, $after frames after t=$tsw, $mono times back"
# 4. the input recording: the reload's own file, the boot's kept
g1=$(ls "$R"/somvr.log.g1.inrec 2>/dev/null); b0=$(stat -c %s "$R/somvr.log.inrec" 2>/dev/null || echo 0)
sleep 2; g1s=$(stat -c %s "$g1" 2>/dev/null || echo 0)
[ -n "$g1" ] && [ "$g1s" -gt 64 ] && [ "$b0" -gt 100000 ] && pass "INREC: somvr.log.g1.inrec $g1s bytes (the reload's module), somvr.log.inrec $b0 bytes kept" \
  || fail "INREC: g1 '$g1' $g1s bytes, the boot's $b0 bytes ($(grep -a 'INREC\|input rec' "$L" | tail -2 | tr '\n' ' ' | cut -c1-200))"
# 5. the speech after the swap: a note's words, an answer said
n_in=$(ls "$LD/inbox" 2>/dev/null | grep -c '\.json$'); w "mark reload37 voice" 1
f=$LD/inbox/$((n_in + 1)).json
for i in $(seq 1 160); do [ -s "$f" ] && break; sleep 0.25; done
if [ -s "$f" ]; then
  md=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["dir"])' "$f")
  te=$(stat -c %.6Y "$md/voice.txt"); ti=$(stat -c %.6Y "$f")
  python3 -I -c 'import json,sys; j=json.load(open(sys.argv[1])); sys.exit(0 if j.get("transcript") else 1)' "$f" \
    && pass "a note after the swap: in the inbox $(python3 -c "print(round($ti - $te, 2))") s after it ended, with its words: $(python3 -I -c 'import json,sys; print(repr(json.load(open(sys.argv[1]))["transcript"][:80]))' "$f")" \
    || fail "a note after the swap without its words ($(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("why"))' "$f"))"
else fail "no note after the swap"; fi
k=$(( $(ls "$LD/say" | grep -cE '^[0-9]+\.txt$') + 1 ))
"$HERE/live_say.sh" --dir "$LD" "After the swap: this line is said." | tee -a "$S"
for i in $(seq 1 120); do [ -s "$LD/say/say-$k.spoken" ] && break; sleep 0.25; done
grep -q ' spoken: on somvr_live_tts_' "$LD/say/say-$k.spoken" 2>/dev/null && pass "an answer after the swap said: $(cut -c21-140 "$LD/say/say-$k.spoken")" \
  || fail "the answer after the swap not said ($(cat "$LD/say/say-$k.spoken" 2>/dev/null | cut -c1-120))"
# 6. the watcher on this run's frames
rm -f -- "$PROT"; sleep 2
since "$nlane" 'live3-reload' "$LANELOG" | cut -c1-200 | tee -a "$S"
since "$nlane" 'FRAMES live3-reload: none|FRAMES live3-reload: .*began again' "$LANELOG" | grep -q . \
  && fail "the watcher: $(since "$nlane" 'FRAMES live3-reload' "$LANELOG" | head -1 | cut -c1-160)" || pass "the watcher followed the frames across the swap: no FRAMES-stopped line"
since "$nlane" 'FREEZE the lane .*probe live3-reload: no baseline' "$LANELOG" | awk 'END { print NR }' | {
  read np; [ "${np:-0}" -le 1 ] && pass "no probe-forever: $np 'no baseline' probe (the start's)" || fail "$np 'no baseline' probes"; }
grep -aE 'tts: .*(dropped|not synthesised)|transcription waits' "$LD/live.log" | grep -q . && fail "the speech waited: $(grep -aE 'dropped|transcription waits' "$LD/live.log" | tail -1 | cut -c1-140)" \
  || pass "the speech never waited (no dropped line, no late words in live.log)"
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; grep -E 'PASS|FAIL' "$S" | cut -c1-240
