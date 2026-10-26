#!/bin/bash
# size-drive.sh - the player size (SOMVR_PLAYER_SIZE, docs/re-xr-pipeline.md 35) in one headless boot: the size probe
# (Talion's and the orcs' skeleton heights, the gaze, a column of level rays and the ground's steps along it) at 12
# headings in the practice meadow, then both-eye marks and stereo dumps at 100, 115 and 130 %: standing (facing the
# arches, heading SIZE_HEADING, default 175 deg), the three practice orcs at arm's length (each probed), the hands
# holding the sword, the wrist menu open (its Player size row's > pressed by the fingertip: the menu's own path), the
# experimental orc scale (`size orc 1.3`, SIZE_ORC=0 skips it) and a test of Talion's own object scale in third person
# (`size talion 1.3`, SIZE_TALION=0 skips it). Boot (outside the sandbox, from ShadowOfMordor/):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' logs/build-vr9/headset.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (SIZE_WT: the worktree's name in RUN/launch-cmd.txt, default this script's). Ends
# with a quit; stops at once (quit) when the user goes into VR. Marks: RUN/marks/<time>_<n>/eyes.jpg. SIZE_PLAN=world:
# the measures instead (see the plan's comment below; run 181...).
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${SIZE_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
HEAD=${SIZE_HEADING:-175}
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
  grep -q '"user": true' /run/user/1000/vr-session/status.json 2>/dev/null && return 0
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
dump() { t "dump"; sleep 1.2; }
size() { w "size $1" 1.5; grep "^SIZE land" "$R/somvr.log" | tail -1 | cut -c1-200; }
n() { grep -c "$1" "$R/somvr.log" 2>/dev/null; }
# the newest gaze heading (deg) of the size probe
heading() { grep '^SIZEPROBE gaze' "$R/somvr.log" | tail -1 | grep -oE 'heading -?[0-9]+' | grep -oE -- '-?[0-9]+$'; }
face() { local h k; w "size probe" 2; h=$(heading); [ -n "$h" ] || return 0
  k=$(( ( ($1 - h + 540) % 360 - 180 ) )); k=$(( (k + (k < 0 ? -15 : 15)) / 30 ))
  while [ "$k" -gt 0 ]; do w "turn+" 0.7; k=$((k - 1)); done; while [ "$k" -lt 0 ]; do w "turn-" 0.7; k=$((k + 1)); done
  w "size probe" 2; echo "facing $(heading) (asked $1)"; }
# the controls card away (the right grip squeezed in the layer's test file, else A)
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "size status" 1
# SIZE_PLAN=world: the measures (the probe all round, the scale tests in first and third person, a walk to the ruins'
# arches with the probe on the way and pictures there at 100 / 115 / 130); default: the pictures of the plan above
if [ "${SIZE_PLAN:-pics}" = world ]; then
  w "size probe" 2; H0=$(heading); echo "arrival heading $H0"
  for k in $(seq 1 12); do w "turn+" 0.8; w "size probe" 1.6; done
  # Talion's own object scale in first person: the hands ahead (do they grow, do they move?)
  w "hands test R 20 -25 40 0 -90 0" 0.6; w "hands test L -20 -25 40 0 90 0" 1
  m t1-hands-1.0 0.5; w "size talion 1.3" 1.5; m t1-hands-1.3 0.5; w "size probe" 1.5
  w "size talion 1" 1.5; m t1-hands-back 0.5; w "hands test off" 0.5
  # third person (the fallback's flat window): Talion and an infantry orc; the orc at 1.3, then Talion at 1.3
  w "practice spawn infantry" 3
  w "fallback third" 3; m t3-orc-1.0 0.3
  w "size orc 1.3" 5; m t3-orc-1.3 0.3; w "size talion 1.3" 2; m t3-talion-1.3 0.3
  w "size talion 1" 1; w "size orc 1" 4; w "fallback auto" 3; w "size probe" 1.5
  w "practice remove" 2
  # the arches: the arrival heading + 180 (run 180704), walk there probing, then the pictures
  for k in 1 2 3 4 5 6; do w "turn-" 0.7; done
  w "size probe" 2; echo "facing $(heading) for the arches"
  for k in $(seq 1 10); do
    d=$(grep '^SIZEPROBE gaze' "$R/somvr.log" | tail -1 | grep -oE 'a hit at [0-9.]+' | grep -oE '[0-9]+' | head -1)
    echo "the gaze: ${d:-none} cm"; [ -n "$d" ] && [ "$d" -lt 900 ] && break
    if [ -n "$d" ] && [ "$d" -lt 2000 ]; then w "stick 0 0.6 0 1.5" 2; else w "stick 0 1 0 2" 2.5; fi
    w "size probe" 2
  done
  for s in 100 115 130; do size $s; m "w$s-ruins" 0.5; dump; done
  size 100; w "turn+" 0.8; w "size probe" 2; w "turn-" 0.8; w "turn-" 0.8; w "size probe" 2; w "turn+" 0.8
  w "climb probe" 2; w "climb probe up" 2
  w "size status" 1
  grep -c '^SIZEPROBE' "$R/somvr.log" | sed 's/^/SIZEPROBE lines: /'
  grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
  sleep 2; echo quit >> "$R/cmd"; echo "$(date +%T) quit (plan world)"; exit 0
fi
# SIZE_PLAN=body (the boot with SOMVR_SIZE_BODY=1): the hands open ahead, the sword in both hands, a fight with swings at
# the infantry and the wrist menu at 100 / 115 / 130, third person (his own scale again), the sword's tip measure
if [ "${SIZE_PLAN:-pics}" = body ]; then
  w "hands test R 20 -25 40 0 -90 0" 0.6; w "hands test L -20 -25 40 0 90 0" 1
  for s in 100 115 130; do size $s; m "b$s-hands" 0.5; dump; done
  size 100
  w "hands draw" 2
  w "hands test R 8 -30 35 60 0 0" 0.6; w "hands test L 8 -38 30.5 60 0 0" 0.6; w "hands squeeze L 1" 1
  for s in 100 115 130; do size $s; m "b$s-sword" 0.5; dump; done
  w "hands squeeze L -1" 0.5
  FORE_A="hands test R 38 -15 22 0 -90 0"; FORE="hands swing R -22 -25 45 0 90 0 300"; BACK="hands swing R 38 -15 22 0 -90 0 300"
  H0=$(n 'COMBAT hit'); w "practice spawn infantry" 4
  for k in 1 2 3 4 5 6; do w "$FORE_A" 0.5; w "$FORE" 1.0; w "$BACK" 1.0; done
  m b130-fight 0.3; dump
  echo "hits at 130 %: $(( $(n 'COMBAT hit') - H0 ))"
  w "practice remove" 2; w "hands test off" 0.5
  opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
  o=$(opens); t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 2; [ "$(opens)" -gt "$o" ] || { t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 1; }
  dump; m b130-menu 0.3
  t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1
  w "fallback third" 3; m b130-third 0.3; w "fallback auto" 3
  size 100
  grep -E '^SIZE body|^COMBAT sword tip' "$R/somvr.log" | tail -8 | cut -c1-220
  grep "xr: the floor" "$R/xr-layer.log" | cut -c1-200
  grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
  sleep 2; echo quit >> "$R/cmd"; echo "$(date +%T) quit (plan body)"; exit 0
fi
# SIZE_PLAN=sword (with SOMVR_SIZE_BODY=1): the sword drawn by the test hand and held in both hands at 100 / 115 /
# 130, then the wrist menu at 130 and 100 (the hand under the panel)
if [ "${SIZE_PLAN:-pics}" = sword ]; then
  w "hands test R 20 -25 40 0 -90 0" 0.6; w "hands draw" 2.5
  w "hands test R 8 -30 35 60 0 0" 0.6; w "hands test L 8 -38 30.5 60 0 0" 0.6; w "hands squeeze L 1" 1
  for s in 100 115 130; do size $s; m "c$s-sword" 0.5; dump; done
  w "hands squeeze L -1" 0.5; w "hands test off" 0.5
  opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
  for s in 130 100; do
    size $s; o=$(opens); t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 2
    [ "$(opens)" -gt "$o" ] || { t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 1; }
    dump; m "c$s-menu" 0.3
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1
  done
  grep -E '^SIZE body|^COMBAT sword tip' "$R/somvr.log" | tail -4 | cut -c1-200
  grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
  sleep 2; echo quit >> "$R/cmd"; echo "$(date +%T) quit (plan sword)"; exit 0
fi
# 1. the probe at 12 headings (30 deg snaps all round)
w "size probe" 2
for k in $(seq 1 12); do w "turn+" 0.8; w "size probe" 1.6; done
# 2. standing in the meadow, facing the arches: 100, 115, 130
face "$HEAD"
for s in 100 115 130; do size $s; m "s$s-meadow" 0.5; dump; done
# 3. an orc at arm's length (each type probed at 130, then 115 and 100), the sword drawn
w "hands draw" 2
w "hands test R 30 -25 40 0 -90 0" 1
for o in infantry shield berserker; do
  w "practice spawn $o" 4
  for s in 130 115 100; do size $s; w "size probe" 1.5; m "s$s-$o" 0.3; done
  dump; size 130
done
w "practice remove" 2
# 4. the hands holding the sword (both hands on the hilt, the blade up ahead): 130, 115, 100
w "hands test R 8 -30 35 60 0 0" 0.6; w "hands test L 8 -38 30.5 60 0 0" 0.6; w "hands squeeze L 1" 1
for s in 130 115 100; do size $s; m "s$s-sword" 0.5; dump; done
w "hands squeeze L -1" 0.5; w "hands test off" 0.5
# 5. the wrist menu open (the left palm toward the eyes), the Player size row's > pressed by the right fingertip
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN"; }
size 100
menu_open; dump; m s100-menu 0.3
A0=$(n 'VRMENU apply PLAYER_SIZE=')
press 0.922 0.857; sleep 1.5; press 0.922 0.857; sleep 1.5; press 0.922 0.857; sleep 2
echo "menu applies: $(( $(n 'VRMENU apply PLAYER_SIZE=') - A0 )); $(grep 'VRMENU apply PLAYER_SIZE=' "$R/somvr.log" | tail -1 | cut -c1-160)"
t "hand R off"; dump; m s115-menu 0.3
size 130; dump; m s130-menu 0.3
t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1
# 6. the experimental orc scale: an infantry orc at 1.3, probed before and 5 s after
if [ "${SIZE_ORC:-1}" = 1 ]; then
  size 100
  w "practice spawn infantry" 4; w "size probe" 1.5; m orc-scale-1.0 0.3
  w "size orc 1.3" 6; w "size probe" 1.5; m orc-scale-1.3 0.3; dump
  w "practice remove" 2
fi
# 7. a test: Talion's own object scale (does his model grow, where are his feet?), seen in third person by an orc
if [ "${SIZE_TALION:-1}" = 1 ]; then
  size 100
  w "practice spawn infantry" 4
  w "fallback third" 3; m third-talion-1.0 0.5
  w "size talion 1.3" 2; w "size probe" 1.5; m third-talion-1.3 0.5
  w "size talion 1" 1; w "fallback auto" 3
  w "practice remove" 2
fi
w "size status" 1
grep -E '^SIZE (land|ask|start|hold|status|talion|orc)' "$R/somvr.log" | tail -8 | cut -c1-220
grep -c '^SIZEPROBE' "$R/somvr.log" | sed 's/^/SIZEPROBE lines: /'
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; marks in $R/marks"
