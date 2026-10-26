#!/bin/bash
# menu13-drive.sh - build 13's VR menu by fingertip in one headless boot (handoff/HANDOFF-build13.md): the Settings
# page's Hands at walls and Climb fling rows (their < boxes: off, then on again: the VRMENU apply lines, the surfaces'
# exception), then the Controls tab (a dump: the page in the world). Boot as practice-menu-drive.sh says, with
# scripts/headset-build13.env (the rows show only with SOMVR_HANDS_SURFACE=on and SOMVR_CLIMB_FLING=on), and this script
# at the same time (MENU13_WT = a word of the build's path in launch-cmd.txt). The uv: somvr_vm_rect_av with build 13's
# avail (18 rows of 33 px): Climb fling's < 0.547 0.792, Hands at walls' < 0.547 0.835, the Controls tab 0.723 0.042, the
# Settings tab 0.160 0.042. Prints PASS/FAIL per step, ends with a quit. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${MENU13_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { w "mark $1" 4; }
# press U V: the right fingertip from 5 mm in front, through the panel, back out (menus-drive.sh's front approach)
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
# the wrist menu: the palm gesture (held off while an attack is incoming: the palm away and back, up to 4 tries), then
# the left menu button held 1 s (a long press: the panel toggles past the gate); prints how it opened
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; grep 'held off\|vrmenu: opened\|vrmenu: closed' "$R/xr-layer.log" | tail -3 | cut -c1-200; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
n() { grep -c "$1" "$R/somvr.log" 2>/dev/null; }
check() { if [ "$(n "$2")" -gt "$3" ]; then echo "PASS $1"; else echo "FAIL $1 (no '$2')"; fi; }

A0=$(n 'VRMENU apply HANDS_SURFACE=off from the menu'); A1=$(n 'VRMENU apply HANDS_SURFACE=on from the menu')
F0=$(n 'VRMENU apply CLIMB_FLING=off from the menu'); F1=$(n 'VRMENU apply CLIMB_FLING=on from the menu')
E0=$(n "except hand=.: the VR menu's Hands at walls: Off")
# a hand at the stone first? (the meadow arrival has none near): the exception line shows once a hand is evaluated
menu_open; t "dump"; sleep 1.5
press 0.547 0.835; sleep 1
check "Hands at walls: Off by its <" 'VRMENU apply HANDS_SURFACE=off from the menu' "$A0"
t "dump"; sleep 1.5
check "the surfaces' exception for the menu's Off" "except hand=.: the VR menu's Hands at walls: Off" "$E0"
press 0.547 0.835; sleep 1
check "Hands at walls: On again" 'VRMENU apply HANDS_SURFACE=on from the menu' "$A1"
press 0.547 0.792; sleep 1
check "Climb fling: Off by its <" 'VRMENU apply CLIMB_FLING=off from the menu' "$F0"
press 0.547 0.792; sleep 1
check "Climb fling: On again" 'VRMENU apply CLIMB_FLING=on from the menu' "$F1"
press 0.723 0.042; sleep 1.5; t "dump"; sleep 1.5
grep -q 'the controls page' "$R/xr-layer.log" && echo "PASS the Controls tab" || echo "FAIL the Controls tab (no 'the controls page')"
press 0.160 0.042; sleep 1
menu_shut; m m13-after
grep -E '^VRMENU apply (HANDS_SURFACE|CLIMB_FLING)' "$R/somvr.log" | cut -c1-200
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (xr-layer.log.stereoN / vrmenuN), marks in $R/marks"
