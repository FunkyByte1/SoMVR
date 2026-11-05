#!/bin/bash
# next40-drive.sh - som-b40-live4 (docs/re-xr-pipeline.md 162.13): a patch staged for the next launch before the boot
# (LIVE_ROOT's next/, live_patch.sh stage --next) is the session's logic module from its start - no Apply; the Test page's
# box "Patch N (applied at start)"; Undo by the VR menu returns to the build's own module (the host's swap: its view
# check, no "view moved"). The patch: the build's own logic DLL with one string changed ("LIVE config ON:" for "on:"),
# so the module's own line tells which one runs. Boot with LIVE_ROOT set as for the stage. LV_WT / LV_RUN as usual.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${LV_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
T0=$(date +%s); R=${LV_RUN:-}
for i in $(seq 1 "${LV_FIND:-720}"); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -L -c %Y "$d")" -ge "$T0" ] && R=$(readlink -f "$d") && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/next40-steps.txt
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
dq() { local n0 i; userin && bail "the user is in VR"; n0=$(grep -c 'dump stereo frame' "$X")
  printf 'dump\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  for i in $(seq 1 160); do [ "$(grep -c 'dump stereo frame' "$X")" -ge $((n0 + 1)) ] && break; sleep 0.25; done
  sleep 0.5; echo "dump $1 $(grep -o 'dump stereo frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')" | tee -a "$S"; }
for i in $(seq 1 120); do grep -q '^SOMVR_LIVE_DIR=' "$R/env.txt" 2>/dev/null && break; sleep 1; done
LD=$(sed -n 's/^SOMVR_LIVE_DIR=//p' "$R/env.txt" | tail -1)
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 6
# 1. the launch: the patch in at the start
grep -a 'live: patch' "$R/run.log" | tail -1 | cut -c1-200 | tee -a "$S"
grep -aq 'applied at the start' "$R/run.log" && pass "the launcher: $(grep -a 'applied at the start' "$R/run.log" | tail -1 | cut -c12-170)" || fail "the launcher did not pre-apply"
grep -aq '^LIVE config ON:' "$L" && ! grep -aq '^LIVE config on:' "$L" && pass "the session's first module is the patch (its own line 'LIVE config ON:'; no reload before)" \
  || fail "the first module is not the patch ($(grep -a '^LIVE config' "$L" | head -1 | cut -c1-60))"
grep -aq '^RELOAD gen [1-9]' "$L" && fail "a swap before Undo" || pass "no swap at the start (the host's first load)"
grep -a 'LIVE patch [0-9]* applied at the session' "$L" | head -1 | cut -c1-150 | tee -a "$S"
waitfor 0 "xr: live: the game's answer 1: start patch" "$X" 10 && pass "the layer: $(since 0 "the game's answer 1" "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-120)" || fail "the layer saw no start state"
grep -aq 'xr: live: patch 1 staged before the launch and applied at its start' "$X" && ! grep -aq 'xr: live: card patch:1' "$X" \
  && pass "no 'Patch 1 ready' card (it is in already): the layer's own line" || fail "a 'Patch 1 ready' card for the patch applied at start"
# 2. its card goes out by itself (calm), then the Test page's box
x "hand L head -25 -50 30 0 0 0"; x "btn R squeeze 0"
waitfor 0 'xr: live: card state:1 out by itself' "$X" 20 && pass "$(since 0 'card state:1 out by itself' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-110)" || echo "(no start card seen)" | tee -a "$S"
sleep 9
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"
press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
          x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
x "$PALM_AWAY"; sleep 0.6; x "$PALM s 0.3"; sleep 1.5
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
WTD=$(cd "$HERE/.." && pwd -P)
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
    "$WTD/mod/somvr/somvr_font.c" -lm 2>> "$S" && "$R/vm_uv" "$(python3 -c "print(hex(int('${AV:-0}', 16) | 1 << 43 | 1 << 63))")" > "$R/vm_uv.txt"
tu=$(awk -F'\t' '$1 == "Test" && $2 == "button" { print $4, $5; exit }' "$R/vm_uv.txt"); [ -n "$tu" ] || tu="0.900 0.0395"
press $tu; sleep 1; dq "box-applied-at-start"
# 3. Undo: the build's own module
nl=$(lines); nx=$(lines "$X"); press 0.7422 0.7892
waitfor "$nx" 'xr: live: Undo patch pressed' "$X" 3 && pass "$(since "$nx" 'Undo patch pressed' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-100)" || fail "Undo not pressed"
waitfor "$nl" '^LIVE request [0-9]+: (undone|refused|bad)' "$L" 40 && pass "the game side: $(since "$nl" '^LIVE request [0-9]+: (undone|refused|bad)' | tail -1 | cut -c1-200)" || fail "no answer to Undo"
since "$nl" '^LIVE config o[nN]:' | tail -1 | grep -q '^LIVE config on:' && pass "after Undo the module is the build's own ('LIVE config on:')" || fail "after Undo: $(since "$nl" '^LIVE config' | tail -1 | cut -c1-40)"
since "$nl" 'RELOAD gen 1 ok' | head -1 | cut -c1-200 | tee -a "$S"
grep -aq 'RELOAD view moved' "$L" && fail "the view moved at the swap: $(grep -a 'RELOAD view moved' "$L" | head -1 | cut -c1-150)" || pass "the host's view check: no 'view moved'"
sleep 1; dq "box-after-undo"
x "$PALM_AWAY"; sleep 1
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; grep -E 'PASS|FAIL|dump' "$S" | cut -c1-220
