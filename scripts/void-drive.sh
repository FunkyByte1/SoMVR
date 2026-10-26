#!/bin/bash
# void-drive.sh - never a void (HG-9, handoff/HANDOFF-void.md): the loads of one headless boot for the layer's gap lines
# (`xr: gap N: .. ms without a game present`, `xr: gaps (..)`) and, with SOMVR_XR_KEEPALIVE=on, its keep-alive frames.
# Boot (outside the sandbox) with the layer's test file and the build's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build13.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <build>/mod/somvr
# and this script at the same time (VOID_WT=<a word of the build's path>: the newest run of it). The loads: the first
# (the launcher's Continue, somvr-vr6.cmd), the practice arrival, the pause menu (Escape: in the world, no load), the
# pause menu's QUIT to the main menu (5 Downs, Return, Return for the dialog's Yes), the main menu's Continue
# again (a second story load); marks along the way. VOID_STALL=1: also the layer's test stall (`stall MS` in the test
# file: the present thread sleeps, as a load that stops presenting) in the world and on the main menu. Prints the gap
# lines and ends with a quit. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${VOID_WT:-t3-633ce2fc}
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
c() { userin && bail "the user is in VR"; echo "$*" >> "$R/cmd"; echo "$(date +%T) cmd: $*"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { w "mark $1" 4; }
gaps() { grep -c 'xr: gap [0-9]*:' "$R/xr-layer.log" 2>/dev/null; }
n() { grep -c "$1" "$R/somvr.log" 2>/dev/null; }
waitn() { local k; for k in $(seq 1 "$1"); do userin && bail "the user is in VR"; [ "$(n "$2")" -gt "$3" ] && return 0; sleep 1; done; return 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T); gap lines so far $(gaps)"
sleep 10
m v01-world
if [ "${VOID_STALL:-0}" = 1 ]; then # the layer's test stall in the world: 3 s without a present
  t "stall 3000"; sleep 6; m v02-after-stall
fi
# the pause menu (in the world: no load expected)
c "key Escape"; sleep 5; c "shot v03-pause"; m v03-pause
c "key Escape"; sleep 5
echo "pause done $(date +%T); gap lines $(gaps)"
# the pause menu's QUIT: 5 Downs, Return, the dialog's Yes by Return (its prompt: A Yes; boot 221150: the click at
# 930,641 missed it)
M0=$(n 'SESSION ui ifs=1 menus=15')
c "key Escape"; sleep 3
for k in 1 2 3 4 5; do c "key Down"; sleep 1; done
c "shot v04-on-quit"; c "key Return"; sleep 3; c "shot v05-quit-dialog"
c "key Return"
for k in 1 2 3 4 5 6; do sleep 4; c "shot v06-to-menu-$k"; done
waitn 120 'SESSION ui ifs=1 menus=15' "$M0" && echo "PASS the main menu again $(date +%T)" || echo "FAIL no main menu after QUIT"
sleep 6
t "hand L head -14 -22 38 0 0 0" "hand R head 14 -22 38 0 0 0" # the test hands in view (the loading space draws ours)
m v07-main-menu
if [ "${VOID_STALL:-0}" = 1 ]; then # the test stall on a mono screen (the main menu): 3 s
  t "stall 3000"; sleep 6; m v08-after-menu-stall
fi
echo "menu done $(date +%T); gap lines $(gaps)"
# Continue again: the game mode's first item, the story load
W0=$(n 'SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world')
c "key Return"; sleep 5; c "shot v09-game-mode"; c "key Return"
for k in 1 2 3 4 5 6; do sleep 5; c "shot v10-load-$k"; done
waitn 150 'SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' "$W0" && echo "PASS the world again $(date +%T)" || echo "FAIL no world after Continue"
sleep 15
m v11-world-again
echo "loads done $(date +%T); gap lines $(gaps)"
grep -E 'xr: gap [0-9]+:|xr: gaps \(' "$R/xr-layer.log" | cut -c1-330 | tail -40
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
c quit
echo "$(date +%T) quit; marks in $R/marks"
