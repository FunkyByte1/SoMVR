#!/bin/bash
# pin37-drive.sh - the VR menu pinned in front (docs/re-xr-pipeline.md 162.12) in a headless Windows game. Boot as
# live37-drive.sh says. LV_WT = the worktree's name in launch-cmd.txt; LV_RUN=RUN. The hands: the left one resting at
# the side the whole time (only its palm raised where a step says so), the right fingertip pressing (as a real index
# finger does: the test hands' `touch panel`, on whatever panel is shown). Steps:
#   1. the left menu button held 1 s: "pinned in front (the menu button's long press)", a picture
#   2. pages with the right hand: Controls, Practice, Settings, the Display group - each a click, the panel never closes
#   3. the head turned 50 deg to the left and back: the panel's place in the dumped frames unchanged, a picture
#   4. the head walked 1.6 m: it glides to the new place ahead ("gliding there"), never a jump; walked back
#   5. the palm up while pinned: no second panel, nothing opens
#   6. Close: closed; the palm up: it opens pinned again (the saved mode)
#   7. Back to the wrist: on the palm again (a picture), the palm down: "closed: the palm dropped"
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
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/pin37-steps.txt
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
panelpose() { grep -a "dump layers frame $1: vrmenu panel pose" "$X" | tail -1 | sed -n 's/.*pose \([-+0-9.]*,[-+0-9.]*,[-+0-9.]*\).*/\1/p'; }
for i in $(seq 1 200); do userin && bail "the user is in VR"; grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 6
x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
SIDE="hand L head -22 -58 2 0 0 0"   # the left hand resting at the side (the grip's back out, the palm in)
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"
x "$SIDE"; x "hand R head 22 -58 2 0 0 0"; sleep 1
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
WTD=$(cd "$HERE/.." && pwd -P)
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
    "$WTD/mod/somvr/somvr_font.c" -lm 2>> "$S" && "$R/vm_uv" "$(python3 -c "print(hex(int('${AV:-0}', 16) | 1 << 43 | 1 << 63))")" > "$R/vm_uv.txt"
uv() { awk -F'\t' -v l="$1" -v p="${2:-button}" '$1 == l && $2 == p { print $4, $5; exit }' "$R/vm_uv.txt"; }
press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
          x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.3; }
# 1. the long press
n0=$(lines "$X"); nl=$(lines)
x "btn L menu 1"; sleep 1.0; x "btn L menu 0"; sleep 0.6
waitfor "$n0" 'pinned in front \(the menu button' "$X" 4 && pass "$(since "$n0" 'pinned in front' "$X" | tail -1 | sed 's/^.*vrmenu: //' | cut -c1-170)" \
  || fail "not pinned by the long press ($(since "$n0" 'vrmenu' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200); $(since "$nl" 'VRMENU menu button' | tail -1 | cut -c1-100))"
since "$n0" 'vrmenu: opened' "$X" | tail -1 | sed 's/^.*vrmenu: //' | cut -c1-100 | tee -a "$S"
dq "pinned"; f1=$(tail -1 "$S" | awk '{print $3}'); p1=$(panelpose "$f1")
# 2. pages with the right hand (the left at the side)
nc=$(lines "$X")
for t in Controls Practice Settings; do press $(uv "$t"); done
press $(uv Display)
nclk=$(since "$nc" 'vrmenu: click' "$X" | wc -l)
since "$nc" 'vrmenu: click' "$X" | sed 's/^.*vrmenu: //' | cut -c1-80 | tee -a "$S"
[ "$nclk" -ge 4 ] && pass "pages with the right hand: $nclk clicks, the left hand at the side" || fail "only $nclk clicks"
x "hand R head 22 -58 2 0 0 0"; dq "pinned-display"; f2=$(tail -1 "$S" | awk '{print $3}'); p2=$(panelpose "$f2")
# 3. the head turned
x "head 0 0 0 yaw -50 s 0.6"; sleep 1.0; dq "head-turned-left"; f3=$(tail -1 "$S" | awk '{print $3}'); p3=$(panelpose "$f3")
x "head 0 0 0 yaw 0 s 0.6"; sleep 1.0
[ -n "$p1" ] && [ "$p1" = "$p2" ] && [ "$p2" = "$p3" ] && pass "the panel's place held: $p1 (pinned), $p2 (the pages), $p3 (the head turned 50 deg)" \
  || fail "the panel moved: '$p1' / '$p2' / '$p3'"
# 4. the follow: the head 1.6 m to the right, then back
nf=$(lines "$X"); x "head 160 0 0 s 1.0"; sleep 2.0
waitfor "$nf" 'pinned in front \(the head 1.5 m' "$X" 3 && pass "followed: $(since "$nf" 'pinned in front' "$X" | tail -1 | sed 's/^.*vrmenu: //' | cut -c1-160)" \
  || fail "no follow after 1.6 m"
dq "followed"; f4=$(tail -1 "$S" | awk '{print $3}'); p4=$(panelpose "$f4")
echo "the panel after the follow: $p4" | tee -a "$S"
x "head 0 0 0 s 1.0"; sleep 2.0
# 5. the palm up while pinned
no=$(lines "$X"); x "$PALM_AWAY"; sleep 0.5; x "$PALM s 0.3"; sleep 1.5
since "$no" 'vrmenu: opened|vrmenu: closed' "$X" | grep -q . && fail "the palm while pinned: $(since "$no" 'vrmenu: (opened|closed)' "$X" | head -1 | cut -c1-120)" \
  || pass "the palm up while pinned: no second panel, nothing opened or closed"
x "$SIDE"; sleep 0.5
# 6. Close; the palm opens it pinned again (the saved mode)
nx=$(lines "$X"); press $(uv Close)
waitfor "$nx" 'vrmenu: closed' "$X" 3 && pass "Close: $(since "$nx" 'vrmenu: closed' "$X" | tail -1 | sed 's/^.*vrmenu: //' | cut -c1-80)" || fail "Close did not close it"
x "hand R head 22 -58 2 0 0 0"; nx=$(lines "$X"); x "$PALM_AWAY"; sleep 0.5; x "$PALM s 0.3"; sleep 1.5
waitfor "$nx" 'pinned in front \(opened, pinned is the saved mode' "$X" 3 && pass "the palm opened it pinned again: the saved mode (MENU_PIN on)" \
  || fail "not reopened pinned ($(since "$nx" 'vrmenu' "$X" | tail -2 | tr '\n' ' ' | cut -c1-200))"
# 7. Back to the wrist (the palm still up): on the palm; then the palm down closes it
nb=$(lines "$X"); press $(uv "Pin in front")
waitfor "$nb" 'back to the wrist' "$X" 3 && pass "$(since "$nb" 'back to the wrist' "$X" | tail -1 | sed 's/^.*vrmenu: //' | cut -c1-120)" || fail "no back to the wrist"
x "hand R head 22 -58 2 0 0 0"; sleep 1.0; dq "back-on-the-wrist"; f5=$(tail -1 "$S" | awk '{print $3}'); p5=$(panelpose "$f5")
echo "the panel on the wrist: $p5" | tee -a "$S"
nd=$(lines "$X"); x "$PALM_AWAY"; sleep 1.5
waitfor "$nd" 'vrmenu: closed: (the palm dropped|the wrist turned over)' "$X" 3 && pass "the wrist panel closed: $(since "$nd" 'vrmenu: closed' "$X" | tail -1 | sed 's/^.*vrmenu: //' | cut -c1-80)" \
  || fail "the wrist panel did not close by the palm"
grep -aE 'MENU_PIN' "$L" | tail -2 | cut -c1-160 | tee -a "$S"
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer" | tee -a "$S"
w "endlogs" 2; echo quit >> "$R/cmd"; echo "end: $(date +%T)"; grep -E 'PASS|FAIL|dump' "$S" | cut -c1-240
