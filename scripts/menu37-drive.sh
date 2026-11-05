#!/bin/bash
# menu37-drive.sh - som-b37-live3 part B in a headless Windows game (docs/re-xr-pipeline.md 162.10): the palm menu that
# stays open while the other hand uses it, and the live card that goes out by itself beside the hand. Boot as
# live37-drive.sh says (WIN_MARK_VOICE optional). LV_WT = the worktree's name in launch-cmd.txt; LV_RUN=RUN drives a
# running boot. Steps (M37_STEPS, default "menu dot card tap"):
#   menu  the palm up (the panel opens), the right fingertip to Settings, Display, the Refresh rate row's > until 90 Hz
#         is asked - the left wrist rolled 15-25 deg toward the watch and back between presses; then the wrist swept over toward
#         the watch with the fingertip still at the panel (build 37: "closed: the wrist turned over"), and last the palm
#         turned down: "closed: the palm dropped". PASS = no close from the open to the palm's drop
#   dot   the TTS worker paused (SIGSTOP: the boot's own process), a message: the card waits for its line (8 s): the dot
#         on the back of the left hand (the back toward the eyes; a picture), then the palm toward the eyes: no dot
#   card  the worker resumed: the card goes out by itself beside the left hand (no palm gesture; a picture), its line
#         said, gone by itself after its 8 s
#   tap   a second message: out by itself, tapped away by the right fingertip
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${LV_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${M37_STEPS:-menu dot card tap}
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
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/menu37-steps.txt
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$S"; [ -n "${WPID:-}" ] && kill -CONT "$WPID" 2>/dev/null; echo quit >> "$R/cmd"; exit 1; }
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
  echo "dump $1 $(grep -o 'dump stereo frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')" | tee -a "$S"; }
for i in $(seq 1 120); do grep -q '^SOMVR_LIVE_DIR=' "$R/env.txt" 2>/dev/null && break; sleep 1; done
LD=$(sed -n 's/^SOMVR_LIVE_DIR=//p' "$R/env.txt" | tail -1)
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
# the left hand: the palm toward the eyes (the menu's gesture), rolled toward the watch, the back toward the eyes
palm() { echo "hand L head 5 -16 32 0 -90 ${1:-50}"; }
LOW="hand L head -25 -50 30 0 0 0"
press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
          x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.3; }
tap() { x "hand R touch live $1 $2 5"; x "hand R touch live $1 $2 1.0 s 0.3"; x "hand R touch live $1 $2 -1.0 s 0.15"
        x "hand R touch live $1 $2 4 s 0.2"; x "hand R off"; }
for s in $STEPS; do
  case $s in
    menu)
      n0=$(lines "$X"); nl=$(lines)
      x "$(palm -130)"; sleep 0.6; x "$(palm 50) s 0.3"; sleep 1.2
      waitfor "$n0" 'vrmenu: opened' "$X" 4 && pass "$(since "$n0" 'vrmenu: opened' "$X" | tail -1 | cut -c1-120)" || fail "the menu did not open"
      no=$(lines "$X")
      AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
      WTD=$(cd "$HERE/.." && pwd -P)
      gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
          "$WTD/mod/somvr/somvr_font.c" -lm 2>> "$S" && "$R/vm_uv" "$(python3 -c "print(hex(int('${AV:-0}', 16) | 1 << 43 | 1 << 63))")" > "$R/vm_uv.txt"
      uv() { awk -F'\t' -v l="$1" -v p="$2" '$1 == l && $2 == p { print $4, $5; exit }' "$R/vm_uv.txt"; }
      x "hand R touch panel 0.5 0.5 8"; sleep 0.5
      x "$(palm 25)"; sleep 0.6; x "$(palm 50)"   # (the wrist rolled toward the watch and back, the hand at the panel)
      press $(uv Settings button)
      x "hand R touch panel 0.5 0.5 8"; x "$(palm 30)"; sleep 0.4; x "$(palm 50)"
      press $(uv Display button)
      dq "menu-display"
      for k in 1 2 3 4; do
        since "$no" 'refresh: 90 Hz asked' "$X" | grep -q . && break
        x "$(palm 35)"; sleep 0.3; x "$(palm 50)"
        press $(uv "Refresh rate" more)
        waitfor "$no" 'refresh: (90|120|72|80) Hz asked' "$X" 2
      done
      since "$no" 'refresh: 90 Hz asked' "$X" | grep -q . && pass "Refresh rate 90: $(since "$no" 'refresh: 90 Hz asked' "$X" | tail -1 | cut -c1-160)" \
        || fail "no 90 Hz asked ($(since "$no" 'click|refresh' "$X" | tail -3 | tr '\n' ' ' | cut -c1-300))"
      sleep 0.5; dq "menu-refresh-90"
      since "$no" 'vrmenu: click' "$X" | sed 's/^.*vrmenu: //' | cut -c1-90 | tee -a "$S"
      # the wrist swept over toward the watch, the fingertip still at the panel (build 37 closed it here)
      # (the fingertip 12 cm out from the panel's middle, following it - near, never on it: nothing pressed)
      TS=0.45
      for r in 30 10 -10 -30 -50 -70 -90; do x "$(palm "$r")" "hand R touch panel 0.5 0.5 12"
        since "$no" 'watch turn held off' "$X" | grep -q . && { echo "the watch faced at a roll of $r deg" | tee -a "$S"; break; }; done
      TS=0.8
      since "$no" 'vrmenu: closed' "$X" | grep -q . && fail "the panel closed on the way: $(since "$no" 'vrmenu: closed' "$X" | head -1 | cut -c1-160)" \
        || pass "the panel stayed open from the open to here; $(since "$no" 'watch turn held off' "$X" | head -1 | sed 's/^.*vrmenu: //' | cut -c1-110) - build 37 closed it there"
      nc=$(lines "$X"); x "$(palm -130)" "hand R touch panel 0.5 0.5 12"; sleep 1.2; x "hand R off"
      waitfor "$nc" 'vrmenu: closed' "$X" 3 && pass "the close: $(since "$nc" 'vrmenu: closed' "$X" | head -1 | sed 's/^.*vrmenu: //' | cut -c1-140)" \
        || fail "the palm down did not close it"
      since "$no" 'vrmenu: closed' "$X" | head -1 | grep -q 'palm dropped' && pass "the first close: the palm dropped (the deliberate close)" \
        || fail "the first close was not the palm's drop"
      since "$no" 'click .*changed' "$X" | grep -v 'Refresh rate' | grep -q . && fail "a row changed by accident: $(since "$no" 'click .*changed' "$X" | grep -v Refresh | head -1 | cut -c1-120)" ;;
    dot)
      x "$LOW"; sleep 4
      WPID=$(grep -a 'tts: worker started in the lane (pid' "$LD/live.log" | tail -1 | sed 's/.*(pid \([0-9]*\).*/\1/')
      [ -n "$WPID" ] && kill -STOP "$WPID" && echo "the TTS worker $WPID paused" | tee -a "$S"
      n0=$(lines "$X"); "$HERE/live_say.sh" --dir "$LD" "The dot test: this card waits for its voice." | tee -a "$S"
      waitfor "$n0" 'message [0-9]+ arrived' "$X" 4
      x "$(palm -130)"; sleep 0.5; dq "dot-back-of-hand"
      waitfor "$n0" 'live dot' "$X" 3 && pass "the dot in the dumped frame: $(since "$n0" 'live dot' "$X" | tail -1 | cut -c1-150)" ||
        fail "no dot in the dumped frame ($(since "$n0" 'xr: live' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"
      [ -n "$WPID" ] && kill -CONT "$WPID" && echo "the TTS worker resumed" | tee -a "$S" ;;
    card)
      n0=$(lines "$X"); x "$LOW"
      waitfor "$n0" 'xr: live: card say:[0-9]+ out by itself' "$X" 15 && pass "$(since "$n0" 'out by itself' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-260)" \
        || fail "no card out by itself ($(since "$n0" 'xr: live' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"
      since "$n0" 'vrmenu: opened' "$X" | grep -q . && fail "the menu opened" || pass "no palm gesture, no menu"
      sleep 1; dq "card-out-beside-hand"
      x "hand L head -10 -45 35 0 -30 0"; sleep 1.0; dq "card-still-hand-moved"
      waitfor "$n0" 'gone by itself' "$X" 30 && pass "$(since "$n0" 'gone by itself' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-140)" \
        || fail "the card did not go by itself" ;;
    tap)
      x "$LOW"; sleep 2; n0=$(lines "$X")
      "$HERE/live_say.sh" --dir "$LD" "Tap test: tap this card away." | tee -a "$S"
      waitfor "$n0" 'out by itself' "$X" 15 && pass "out: $(since "$n0" 'out by itself' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-120)" || fail "not out"
      sleep 1; tap 0.5 0.5
      waitfor "$n0" 'xr: live: card say:[0-9]+ tapped' "$X" 5 && pass "$(since "$n0" ' tapped ' "$X" | tail -1 | sed 's/^.*xr: live: //' | cut -c1-120)" \
        || fail "not tapped ($(since "$n0" 'xr: live' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))" ;;
  esac
done
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; grep -E 'PASS|FAIL|dump' "$S" | cut -c1-220
