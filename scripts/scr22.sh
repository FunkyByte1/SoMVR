#!/bin/bash
# scr22.sh - som-b22-screens' step driver for a running headless boot (handoff/HANDOFF-drill.md "build 22"): one step per
# call, so a helper can look at each picture before the next (the Nemesis screens, Upgrades, Weapons & Runes, the map,
# a death). The boot: scripts/headless_vr.sh with scripts/headset-build22.env + the test hands; this script finds it.
#   scr22.sh find                      the newest run of this worktree (kept in $SCR_DIR/run)
#   scr22.sh tap HAND BTN [S]          a button pressed and let go (HAND L|R; a, b, x, y, menu, trigger, squeeze, stick)
#   scr22.sh flick X Y                 the left stick flicked (menus: one step)
#   scr22.sh rflick X Y                the right stick flicked
#   scr22.sh aim U V [CM]              the right ray at the board (or the theatre's screen) uv U,V from CM cm (100)
#   scr22.sh trig [S]                  the right trigger pulled S s (0.35)
#   scr22.sh touch U V                 the right fingertip pressed into the board at uv U,V
#   scr22.sh off                       the right hand back to rest
#   scr22.sh word WORDS...             a trigger word (somvr.trigger)
#   scr22.sh t LINE...                 raw test lines (xrtest.txt)
#   scr22.sh shot NAME                 the flat picture (shots/scr-NAME.png) -> $SCR_DIR/NAME.jpg (960 x 540)
#   scr22.sh eyes NAME                 a layer dump -> both eyes composed -> $SCR_DIR/NAME-eyes.jpg (1280 wide)
#   scr22.sh dump NAME                 a layer dump only (NAME FRAME appended to $SCR_DIR/dumps.txt)
#   scr22.sh state                     the session's last line, the last SCREENS / BODYMOMENT / POINTER lines
#   scr22.sh quit                      endlogs, quit
# Stops (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=$(basename "$(cd "$HERE/../.." && pwd -P)")
SCR_DIR=${SCR_DIR:-/tmp/b22scr}; mkdir -p "$SCR_DIR"
R=$(cat "$SCR_DIR/run" 2>/dev/null)
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
[ "$1" = find ] || [ -n "$R" ] || { echo "no run (scr22.sh find)"; exit 1; }
if [ "$1" != find ] && userin; then echo "STOP: the user is in VR"; echo quit >> "$R/cmd"; exit 3; fi
xt() { { echo "# $(date +%s.%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; }
case $1 in
find)
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && { echo "$d" > "$SCR_DIR/run"; echo "run $d"; exit 0; }
  done; echo "no run yet"; exit 1 ;;
tap) xt "btn $2 $3 1"; sleep "${4:-0.3}"; xt "btn $2 $3 0"; sleep 0.5 ;;
flick) xt "stick L $2 $3"; sleep 0.4; xt "stick L 0 0"; sleep 0.6 ;;
rflick) xt "stick R $2 $3"; sleep 0.4; xt "stick R 0 0"; sleep 0.6 ;;
aim) xt "hand R aim board $2 $3 ${4:-100}"; sleep 1.2 ;;
aimto) TU=$2; TV=$3 # the ray onto uv U,V of the mono quad as the pointer sees it: aimed, the raw hit read back (the pointer's on edge),
       # corrected (the test hands' target pose can differ from the shown quad's), at most 4 tries, 0.004 uv
  X=$R/xr-layer.log; u=$2; v=$3
  for k in 1 2 3 4; do
    xt "hand R off"; sleep 0.8; n0=$(grep -c 'xr: pointer frame .* edges 0x1 ' "$X")
    xt "hand R aim board $u $v ${4:-100}"; sleep 1.2
    l=$(grep 'xr: pointer frame .* edges 0x1 ' "$X" | tail -1); [ "$(grep -c 'xr: pointer frame .* edges 0x1 ' "$X")" -gt "$n0" ] || { echo "no on-edge line"; break; }
    set -- $(echo "$l" | sed -E 's/.* R 1 ([0-9.-]+),([0-9.-]+) .*/\1 \2/')
    echo "try $k: aimed $u $v -> hit $1 $2"
    r=$(awk -v tu="$TU" -v tv="$TV" -v hu="$1" -v hv="$2" -v u="$u" -v v="$v" 'BEGIN {
      du = tu - hu; dv = tv - hv; ok = (du < 0.004 && du > -0.004 && dv < 0.004 && dv > -0.004);
      printf "%d %.4f %.4f", ok, u + du, v + dv }')
    set -- $r; [ "$1" = 1 ] && { echo "on target"; break; }; u=$2; v=$3
  done ;;
trig) xt "btn R trigger 1"; sleep "${2:-0.35}"; xt "btn R trigger 0"; sleep 0.6 ;;
touch) xt "hand R touch board $2 $3 5"; sleep 0.7; xt "hand R touch board $2 $3 1.0 s 0.3"; sleep 0.7
       xt "hand R touch board $2 $3 -1.0 s 0.15"; sleep 0.6; xt "hand R touch board $2 $3 4 s 0.2"; sleep 0.6 ;;
off) xt "hand R off"; sleep 0.5 ;;
word) shift; printf '%s' "$*" > "$R/somvr.trigger"; sleep 0.8 ;;
t) shift; xt "$@"; sleep 0.6 ;;
shot)
  echo "shot scr-$2" >> "$R/cmd"
  for i in $(seq 1 30); do [ -s "$R/shots/scr-$2.png" ] && break; sleep 0.3; done; sleep 0.3
  python3 -c "
from PIL import Image; import sys
Image.open(sys.argv[1]).convert('RGB').resize((960,540)).save(sys.argv[2], quality=85)" "$R/shots/scr-$2.png" "$SCR_DIR/$2.jpg" &&
  echo "$SCR_DIR/$2.jpg" ;;
dump) # a layer dump only (compose later: xr_compose.py RUN -f FRAME); "NAME FRAME" -> $SCR_DIR/dumps.txt
  X=$R/xr-layer.log; n0=$(grep -c 'dump layers frame [0-9]*: eye 1' "$X"); xt "dump"
  for i in $(seq 1 20); do [ "$(grep -c 'dump layers frame [0-9]*: eye 1' "$X")" -gt "$n0" ] && break; sleep 0.25; done
  f=$(grep -o 'dump layers frame [0-9]*: eye 1' "$X" | tail -1 | grep -o '[0-9][0-9]*' | head -1)
  echo "$2 $f" | tee -a "$SCR_DIR/dumps.txt" ;;
eyes)
  X=$R/xr-layer.log; n0=$(grep -c 'dump layers frame [0-9]*: eye 1' "$X")
  xt "dump"
  for i in $(seq 1 30); do [ "$(grep -c 'dump layers frame [0-9]*: eye 1' "$X")" -gt "$n0" ] && break; sleep 0.3; done
  f=$(grep -o 'dump layers frame [0-9]*: eye 1' "$X" | tail -1 | grep -o '[0-9][0-9]*' | head -1)
  sleep 1
  python3 "$HERE/xr_compose.py" "$R" -f "$f" -o "$SCR_DIR/eyes-$2" ${SCR_PPD:+--ppd $SCR_PPD} > "$SCR_DIR/eyes-$2.log" 2>&1
  grep -iE 'deg|px' "$SCR_DIR/eyes-$2.log" | head -8 | cut -c1-220
  p=$SCR_DIR/eyes-$2/frame$f.png; [ -f "$p" ] || p=
  [ -n "$p" ] || { echo "no composed picture (frame $f)"; tail -3 "$SCR_DIR/eyes-$2.log"; exit 1; }
  python3 -c "
from PIL import Image; import sys
im = Image.open(sys.argv[1]).convert('RGB'); w, h = im.size
im.resize((1280, int(h * 1280 / w))).save(sys.argv[2], quality=85)" "$p" "$SCR_DIR/$2-eyes.jpg" && echo "$SCR_DIR/$2-eyes.jpg (frame $f; $p)" ;;
state)
  grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c1-90
  grep -E '^(SCREENS|INSPECT t=[0-9.]* (flat|eyes)|POINTER (down|up|on|park|LP_)|VIN press cmd=100)' "$R/somvr.log" | tail -${2:-6} | cut -c1-260
  grep -E '^BODYMOMENT t=' "$R/somvr.log" | tail -2 | cut -c1-220 ;;
quit) printf '%s' "endlogs" > "$R/somvr.trigger"; sleep 3; echo quit >> "$R/cmd"; echo "quit $(date +%T)" ;;
*) echo "unknown step $1"; exit 2 ;;
esac
