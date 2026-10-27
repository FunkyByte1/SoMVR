#!/bin/bash
# collect16-drive.sh - the collect drill's round 2 (handoff/HANDOFF-collect.md "Round 2", docs/re-xr-pipeline.md 45.7-):
# an artifact's inspect view in VR (SOMVR_INSPECT), the glyph's trigger (SOMVR_GLYPH=log), the hints at collectibles.
# One headless boot (the save's own place), run as
#   HEADLESS_HOLD=1200 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build16.env | tr '\n' ' ')
#     SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_INSPECT=on SOMVR_GLYPH=log"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# with a copy of this script at the same time (never edit a running bash script; collect15_land.py and
# collect15_walk.py next to it). DC_PLAN (words in order, default "glyph art"): glyph = gly1 (the arch: walk to DG_STOP
# m, look at it, the wraith vision held DG_HOLD s, `practice status look`, then nearer, held again); art = art2 (under
# an arch: walk up, Y held until "(B) Pick Up", B: the artifact's menu, then the inspect view's steps: the eyes and the
# flat picture at each, A (the tutorial), the right stick round (the memory point), X (the memory: what it shows), B,
# B, the menu button, the world again); art3 = the crag-top artifact the same way. DR_NOQUIT=1 leaves the boot running.
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${DR_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
PLAN=${DC_PLAN:-glyph art}; PLAN=${PLAN//_/ }
T0=$(date +%s)
R=${DR_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
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
ARR='^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world'
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  [ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge 2 ] && break; sleep 3
done
[ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge 2 ] || bail "no arrival"
echo "arrived $(date +%T)"
sleep 6
N=0; S=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
d() { t "dump"; echo "shot dc-$1" >> "$R/cmd"; echo "$(date +%T) DUMP $1 ($(state))"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        echo "$(date +%T) tap $1 $2 ${3:-0.3}"; sleep 0.5; }
inworld() { state | grep -q 'ctx=world'; }
inmenu() { state | grep -q 'ctx=menu'; }
step() { S=$((S + 1)); echo "$(date +%T) STEP $S $*"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
since() { tail -n +"$1" "$R/somvr.log" | grep -E "$2" | cut -c1-${3:-230} | head -${4:-12}; }
IGN="'(UI_Wraith_|UI_Ranged_|Domination_|Combat_Grab|UI_Combat_|UI_Attract|Combat_Struggle|UI_Prompt_Interrogate)"
placep() { tail -n +"$1" "$R/somvr.log" | grep -E '^PROMPTS show|^USE t=.* pick prompt up' | grep -qvE "$IGN"; }
LOW="hand L head -25 -35 30 0 0 0"
# barx NAME: the right end of the bottom bar's text in the flat picture shots/dc-NAME.png (waits for it)
cat > "$R/barx.py" <<'PY'
import sys, time, os
from PIL import Image
p = sys.argv[1]
for i in range(40):
    if os.path.exists(p) and os.path.getsize(p) > 1000:
        break
    time.sleep(0.15)
time.sleep(0.2)
im = Image.open(p).convert('L'); w, h = im.size
y0, y1 = int(h * 0.955), int(h * 0.99)
px = im.load(); best = 0
for x in range(0, int(w * 0.6)):
    if any(px[x, y] > 170 for y in range(y0, y1, 2)):
        best = x
print(best)
PY
barx() { python3 "$R/barx.py" "$R/shots/dc-$1.png" 2>/dev/null || echo 0; }
REST="hands test R 28 -40 20 0 0 0"
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"; t "$LOW"; w "$REST" 2
w "collect status" 1
d start 1
L0=$(wc -l < "$R/somvr.log")
trip() { local a0=$(cnt 'arrived at') f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170; sleep 2.5; }
# land NAME X Y Z: collect15_land.py's open ground near the place, the trip there; 0 = arrived
land() { local nm=$1 x=$2 y=$3 z=$4 l
  l=$(python3 "$HERE/collect15_land.py" "$R" "$x" "$y" "$z" ${5:-} | tee "$R/land-$nm.txt" | tail -1); echo "$l"
  [ "${l%% *}" = LAND ] || { echo "$(date +%T) $nm: no landing"; return 1; }
  set -- $l; trip "$2" "$3" "$4"; return 0; }
# walk NAME X Y Z STOP: up to STOP m, the head pitched at the place's height at the end
walkto() { python3 "$HERE/collect15_walk.py" "$R" "$2" "$4" --stop "$5" --secs 35 ${6:-} --look "$3" | tee "$R/walk-$1.txt" | tail -4
  HY=$(grep -o 'yaw [-0-9.]* pitch [-0-9.]*' "$R/walk-$1.txt" | tail -1); echo "head: $HY"; }
# the glyph on the arch (round 1: 0/32 collected; the wraith vision held up to 10 s from 0.8-8 m, nothing)
glyph() { local x=34345 y=3900 z=-19589 l1
  step "glyph gly1 at $x $y $z"
  hintshot gly1                # (the game's 'Tut_Collectible_Glyph_Located' hint comes 3-10 s after the arrival)
  land gly1 $x $y $z "--near 4 --far 7" || return
  l1=$(wc -l < "$R/somvr.log")
  walkto gly1 $x $y $z "${DG_STOP:-4.5}"
  [ -n "$HY" ] && t "head 0 0 0 $HY s 0.4"
  d gly1-look 0.5
  t "btn L b 1"; sleep 2.5; d gly1-wraith 0.5; m gly1-wraith 2
  w "practice status look" 1.5
  sleep "${DG_HOLD:-10}"; d gly1-held 0.5
  w "collect status" 1
  echo "-- glyph (far): $(cnt '^GLYPH t=') GLYPH lines so far; the last ones:"; since "$l1" '^GLYPH|^PRACTICE look|HINTS' 260 16
  t "btn L b 0"; sleep 1.5
  # nearer: under the arch, looking up
  walkto gly1b $x $y $z "${DG_STOP2:-2.0}"
  [ -n "$HY" ] && t "head 0 0 0 $HY s 0.4"
  t "btn L b 1"; sleep 2.5; d gly1-near-wraith 0.5
  w "practice status look" 1.5
  sleep "${DG_HOLD:-10}"; d gly1-near-held 0.5
  echo "-- glyph (near): the last GLYPH lines:"; since "$l1" '^GLYPH t=.*(state [123]|see 1|vision look-at)' 260 10
  t "btn L b 0"; sleep 1
  w "practice status" 1; grep '^PRACTICE t=.* status' "$R/somvr.log" | tail -1 | grep -o 'Talion [^;]*' | head -1
  appendices gly1; }
# the pause menu's Appendices (three down, A): its first page = Ithildin N / 32 (the flat picture), then out
flick() { userin && bail "the user is in VR"; printf 'stick L %s %s\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep 0.4; printf 'stick L 0 0\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) flick $1 $2"; sleep 0.8; }
appendices() { step "the Appendices after $1"
  tap L menu; sleep 3; inworld && { echo "not paused"; return; }
  for i in 1 2 3; do flick 0 -1; done; d "$1-pause-sel" 0.5; tap R a; sleep 3; d "$1-appendices" 1; m "$1-appendices" 3
  tap R b; sleep 2; inworld || { tap L menu; sleep 2; }; inworld || { tap R b; sleep 2; }; inworld || { tap L menu; sleep 2; }
  echo "$(date +%T) out of the menu: $(state)"; }
# hintshot NAME: in the background, a flat picture 1.5 s after the next combat hint's show (SOMVR_HINTS_KEEP's hint)
hintshot() { local h0=$(cnt '^HINTS combat hint .* show'); ( for i in $(seq 1 80); do
    [ "$(cnt '^HINTS combat hint .* show')" -gt "$h0" ] && { sleep 1.5; echo "shot dc-$1-hint" >> "$R/cmd"; echo "$(date +%T) SHOT $1-hint"; break; }
    sleep 0.5; done ) & }
# glyphx: the glyph again with the test word `collect camhead N` (the camera's transform copy from our eye): the
# wraith vision held DG_HOLD s at each N (0, 1, 2), the flat picture (the vision's target panel), then the Appendices
glyphx() { local x=34345 y=3900 z=-19589 l1 n
  step "glyphx gly1 at $x $y $z: camhead 0/1/2"
  hintshot gly1
  land gly1 $x $y $z "--near 4 --far 7" || return
  walkto gly1 $x $y $z "${DG_STOP:-4.5}"
  [ -n "$HY" ] && t "head 0 0 0 $HY s 0.4"
  for n in 0 1 2; do l1=$(wc -l < "$R/somvr.log")
    w "collect camhead $n" 1
    t "btn L b 1"; sleep 3; d "gly1-cam$n-a" 0.5; sleep "${DG_HOLD:-8}"; d "gly1-cam$n-b" 0.5
    echo "-- camhead $n:"; since "$l1" '^GLYPH (camhead|t=.*state [0-9].*see 1)|^HINTS|^PROMPTS show|^USE t' 230 6
    t "btn L b 0"; sleep 2.5
  done
  w "collect camhead 0" 1; w "collect status" 1
  appendices gly1x; }
# glyphaim: the game's own camera (not our view) turned at the glyph's LookAtTrigger by YawAxis 13 / PitchAxis 12
# (glyph_aim.py, `crouch try`), then the wraith vision held: does a script-side check use the game's camera?
glyphaim() { local x=34345 y=3900 z=-19589
  step "glyphaim: the game's camera at the glyph's trigger (34415 3900 -19659)"
  hintshot gly1
  land gly1 $x $y $z "--near 4 --far 7" || return
  walkto gly1 $x $y $z "${DG_STOP:-4.5}"
  [ -n "$HY" ] && t "head 0 0 0 $HY s 0.4"
  python3 "$HERE/glyph_aim.py" "$R" 34415 3900 -19659 --tries 16 | tee "$R/aim-1.txt" | tail -12
  d gly1-aim0 0.5
  t "btn L b 1"; sleep 3; d gly1-aim-a 0.5
  python3 "$HERE/glyph_aim.py" "$R" 34415 3900 -19659 --tries 6 | tee "$R/aim-2.txt" | tail -4
  sleep "${DG_HOLD:-10}"; d gly1-aim-b 0.5; m gly1-aim 2
  echo "-- glyphaim:"; tail -n +"$L0" "$R/somvr.log" | grep -E '^GLYPH t=.*state [23]|^HINTS|^PROMPTS show|^USE t|^MINIMAP' | cut -c1-200 | tail -8
  t "btn L b 0"; sleep 2
  appendices gly1aim; }
# an artifact: the pick, then its inspect view step by step
art() { local nm=$1 x=$2 y=$3 z=$4 ml i
  step "artifact $nm at $x $y $z"
  hintshot "$nm"
  land "$nm" "$x" "$y" "$z" || return
  ml=$(wc -l < "$R/somvr.log")
  walkto "$nm" "$x" "$y" "$z" 1.0 --prompt
  [ -n "$HY" ] && t "head 0 0 0 $HY s 0.4"
  t "btn L b 0"; sleep 0.8; t "btn L b 1"
  for i in $(seq 1 12); do placep "$ml" && break; sleep 1; done
  echo "$(date +%T) $nm: the wraith vision held $i s: $(placep "$ml" && echo 'a prompt' || echo 'no prompt')"
  since "$ml" '^HINTS|^USE t' 230 8
  d "$nm-prompt" 0.5
  tap R b 0.3; sleep 2.5
  inworld && { t "btn R b 1"; sleep 2.5; t "btn R b 0"; sleep 1; }
  for i in 1 2 3 4 5; do sleep 1.5; inmenu && break; done
  t "btn L b 0"
  inmenu || { echo "$(date +%T) $nm: no menu ($(state))"; return; }
  echo "$(date +%T) $nm: the menu ($(state)); INSPECT:"; since "$ml" '^INSPECT' 230 4
  sleep 2; d "$nm-open" 1; m "$nm-open" 3
  tap R a 0.4; sleep 2.5; d "$nm-a" 1                       # the tutorial's (A) to Continue
  # the sticks round as round 1's run 140239 (which found the memory point, "Press (X) to play Memory"): the left
  # (pan / zoom) each way 1.2 s, then the right (rotate) each way 1.2 s
  for s in "1 0" "-1 0" "0 1" "0 -1"; do t "stick L $s"; sleep 1.2; t "stick L 0 0"; done; d "$nm-turnL" 1
  for s in "1 0" "-1 0" "0 1" "0 -1"; do t "stick R $s"; sleep 1.2; t "stick R 0 0"; done
  d "$nm-turned" 1; m "$nm-turned" 3
  # the memory point: the right stick in short pulses (yaw rows, then a pitch step), the flat picture after each:
  # "(X) Play Memory" joins the bottom bar (its text then reaches further right: barx)
  local b0=$(barx "$nm-turned") bx found=0 pr k
  echo "$(date +%T) $nm: the bar ends at x=$b0"
  for pr in 0 1 2 3 4 5; do
    for k in 1 2 3 4 5 6 7 8 9 10; do t "stick R 1 0"; sleep 0.3; t "stick R 0 0"; echo "shot dc-$nm-s$pr-$k" >> "$R/cmd"; sleep 0.4
      bx=$(barx "$nm-s$pr-$k"); [ "$bx" -gt $((b0 + 60)) ] && { found=1; break; }; done
    [ $found = 1 ] && break
    t "stick R 0 1"; sleep 0.35; t "stick R 0 0"
  done
  echo "$(date +%T) $nm: the memory point $([ $found = 1 ] && echo "found (row $pr, step $k: the bar to x=$bx)" || echo 'not found')"
  d "$nm-point" 1; m "$nm-point" 3
  local x0=$(wc -l < "$R/somvr.log") k0=$(wc -l < "$R/xr-layer.log")
  tap L a 0.4; sleep 2                                       # X: the memory
  for i in 1 2 3 4 5 6; do d "$nm-mem$i" 2.5; done
  echo "-- $nm: X (the memory):"; since "$x0" '^SESSION ui|^INSPECT|^BODYMOMENT .*window|^VIDEO|^HINTS' 200 10
  tail -n +"$k0" "$R/xr-layer.log" | grep -E 'theatre|menus: a game menu|loadspace' | cut -c1-200 | head -6
  for i in 1 2 3 4 5 6 7 8 9 10; do inmenu && state | grep -q 'video=0' && break; sleep 3; done
  d "$nm-after-mem" 1
  tap R b 0.4; sleep 3; d "$nm-b" 1
  inworld || { tap R b 0.4; sleep 3; d "$nm-b2" 1; }
  inworld || { tap L menu 0.3; sleep 3; d "$nm-b3" 1; }
  echo "-- $nm: the moments since the pick:"; since "$ml" '^(INSPECT|SESSION ui|ESIZE|WARN)' 200 24
  grep -E 'xr: (theatre|menus)' "$R/xr-layer.log" | tail -4 | cut -c1-200
  d "$nm-world" 1; }
for p in $PLAN; do
  case $p in
  glyph) glyph ;;
  glyphx) glyphx ;;
  glyphaim) glyphaim ;;
  art) art art2 34776 2448 -27463 ;;
  appx) appendices now ;;
  art3) art art3 28596 5553 -33884 ;;
  esac
done
echo "steps done $(date +%T)"
w "collect status" 1
echo "---- GLYPH lines $(tail -n +"$L0" "$R/somvr.log" | grep -c '^GLYPH'), INSPECT lines $(tail -n +"$L0" "$R/somvr.log" | grep -c '^INSPECT'), HINTS records:"
tail -n +"$L0" "$R/somvr.log" | grep -E '^HINTS record' | cut -c1-300 | head -8
[ "${DR_NOQUIT:-0}" = 1 ] && { echo "the boot left running (DR_NOQUIT=1)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
