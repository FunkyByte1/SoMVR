#!/bin/bash
# menu17-drive.sh - build 17's VR menu rows and pages in one headless boot (docs/re-xr-pipeline.md 56, handoff/
# HANDOFF-menus.md "build 17"), at the practice arrival. Boot (sandbox off) with scripts/headset-build17.env's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build17.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (M17_WT = a word of the launch command, default this worktree's name). The panel is
# pressed by the layer's right test controller's fingertip (menu13-drive.sh's press); the uv are somvr_vm_rect_av's with
# build 17's avail (21 rows of 28 px): Sword hand's > 0.922 0.612, Arrows' < 0.547 0.648, the tabs Settings 0.160,
# Practice 0.441, Controls 0.723 (v 0.042), Refill arrows + focus 0.500 0.587, Refill health 0.500 0.663.
#   1. the Settings page (dump), Sword hand -> Follow (the VR menu's line, SWORDWT's), Arrows -> Auto (BOW quiver on ->
#      off), the page again, the Controls page with Auto (dump), the Practice page in the area (dump)
#   2. the refills: arrows spent (`bow arrows -5`), health and Focus lowered (`practice hurt 60`, `practice focus 0`), the
#      two buttons pressed: the PRACTICE refill lines (the page dumped after)
#   3. the bow with Arrows: Auto: out from the left shoulder, RT at the string = nocked with no arrow taken, drawn, let go =
#      the shot; put back
#   4. the menu again: Arrows -> Quiver (the bow away: at once), the Controls page with the quiver (dump); the bow: RT at
#      the string with no arrow = "no arrow to nock" (the quiver's rule back)
#   5. the sword drawn, a fast cut and a wiggle with Sword hand: Follow: SWORDWT's "the drawn hands followed in N frames"
# Dumps: RUN/m17-steps.txt (scripts/xr_compose.py RUN). Prints PASS/FAIL per step, ends with a quit; quits at once when
# the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${M17_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$L"; }
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2")" -gt "$4" ] && { echo "$(date +%T) PASS $1: $(grep -E "$2" "$L" | tail -1 | cut -c1-230)"; return 0; }
  sleep 0.25; done; echo "$(date +%T) FAIL $1 (no '$2')"; return 1; }
ex() { local nm=$1 re=$2 sec=$3 n0; shift 3; n0=$(cnt "$re"); T_S=0.05 t "$@"; seen "$nm" "$re" "$sec" "$n0"; }
exw() { local n0; n0=$(cnt "$2"); w "$4" 0.05; seen "$1" "$2" "$3" "$n0"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/m17-steps.txt"; }
# press U V: the right fingertip from 5 mm in front, through the panel, back out (menu13-drive.sh's)
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
# pr NAME RE U V: a press that must bring a somvr.log line
pr() { local n0; n0=$(cnt "$2"); press "$3" "$4"; seen "$1" "$2" 4 "$n0"; }
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; grep 'held off\|vrmenu: opened\|vrmenu: closed' "$R/xr-layer.log" | tail -3 | cut -c1-200; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
# the rows: 22 of 27 px since build 17's Blade bite row (after Sword hand): the row's middle (64 + slot x 27 + 13.5) / 768
SWHAND_MORE="0.922 0.593"; ARROWS_LESS="0.547 0.663"
TAB_SET="0.160 0.042"; TAB_PR="0.441 0.042"; TAB_CTL="0.723 0.042"
FILL_ARROWS="0.500 0.587"; FILL_HEALTH="0.500 0.663"
# the bow (bow16-drive.sh's poses: the fist upright)
REST_L="hand L head -22 -40 15 0 0 0"; REST_R="hand R head 22 -40 15 0 0 0"
SLOT_L="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_NOCK="hand R head -4 -11 47 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
bow_out() { t "$SLOT_L"; sleep 0.4; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1; }
bow_back() { t "$REST_R"; t "$SLOT_L"; sleep 0.5; ex "the bow put back" '^BOW .* exit: ' 3 "btn L squeeze 0"; sleep 0.8; t "$REST_L"; }
grep -E '^CONFIG (bow quiver|smooth)|^VRMENU config' "$L" | cut -c1-260
# 0. the controls card away (the right grip, else A)
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
t "$REST_L" "$REST_R"; sleep 1
echo "== 1. the rows"
menu_open; d settings-before
pr "Sword hand: Follow (the row's >)" 'VRMENU apply SWORD_WEIGHT_HAND=follow from the menu' $SWHAND_MORE
seen "the drawn hands follow (the arms' next frame)" "^SWORDWT t=.* the drawn hands follow the blade \(the VR menu's Sword hand\)" 3 0
pr "Arrows: Auto (the row's <)" 'VRMENU apply BOW_QUIVER=off from the menu' $ARROWS_LESS
seen "the bow takes it (away)" "^BOW t=.* quiver on -> off \(the VR menu's Arrows" 3 0
d settings-after
press $TAB_CTL; sleep 1; d controls-auto
press $TAB_PR; sleep 1; d practice-page
echo "== 2. the refills"
exw "arrows spent (the bow's test word)" '^BOW t=.* arrows: -5 added for the test' 3 "bow arrows -5"
exw "health lowered (the test aid)" "^PRACTICE t=.* Talion's health .* -> 60.0 of " 3 "practice hurt 60"
exw "Focus emptied (the test aid)" '^PRACTICE t=.* the Focus .* -> 0.0 of ' 3 "practice focus 0"
sleep 0.5; d practice-low
pr "Refill arrows + focus" '^PRACTICE t=.* refill arrows \(the menu\): ' $FILL_ARROWS
pr "Refill health" '^PRACTICE t=.* refill health \(the menu\): ' $FILL_HEALTH
sleep 0.5; d practice-refilled
grep -E '^PRACTICE t=.* (refill|a test aid)|^BOW t=.* arrows: ' "$L" | tail -6 | cut -c1-260
press $TAB_SET; sleep 0.8
menu_shut
echo "== 3. the bow with Arrows: Auto"
t "$REST_L" "$REST_R"; sleep 0.6
q0=$(cnt 'quiver: an arrow taken')
bow_out
ex "nocked at the string (RT), no arrow taken" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK" "btn R trigger 1"
ex "full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.4
r0=$(cnt '^BOW .* release: ')
T_S=0.4 t "btn R trigger 0"; seen "the shot (let go of RT)" '^BOW .* release: ' 2 "$r0"
[ "$(cnt 'quiver: an arrow taken')" = "$q0" ] && echo "PASS no arrow from the quiver (Auto)" || echo "FAIL an arrow was taken from the quiver"
sleep 1; bow_back
echo "== 4. Arrows: Quiver again"
menu_open
pr "Arrows: Quiver (the row's <)" 'VRMENU apply BOW_QUIVER=on from the menu' $ARROWS_LESS
seen "the bow takes it (away)" "^BOW t=.* quiver off -> on \(the VR menu's Arrows" 3 0
press $TAB_CTL; sleep 1; d controls-quiver
press $TAB_SET; sleep 0.8
menu_shut
t "$REST_L" "$REST_R"; sleep 0.6
bow_out
ex "the quiver's rule: no arrow to nock" 'no arrow to nock' 3 "$BOW_STRAIGHT" "$AT_NOCK" "btn R trigger 1"
t "btn R trigger 0"; sleep 0.5; bow_back
echo "== 5. the sword with Sword hand: Follow"
t "hand R head 10 -10 -15 0 0 0"; sleep 0.4; t "btn R squeeze 1"; sleep 0.3; t "hand R head 30 -25 40 0 0 0"; t "btn R squeeze 0"; sleep 0.5
grep -E '^SLOTS t=.* sword: (done|DENIED)' "$L" | tail -1 | cut -c1-160
w "hands test L -25 -35 30 0 0 0"
w "hands test R 45 -15 35 0 -70 0" 1.5
w "hands swing R -30 -25 45 0 70 0 500" 2
w "hands test R 30 -25 40 0 0 0" 1
w "hands wiggle R 15 4 3" 4
n0=$(cnt '^SWORDWT t=.* last 10 s')
seen "SWORDWT's window" '^SWORDWT t=.* last 10 s' 14 "$n0"
grep -E '^SWORDWT t=.* last 10 s' "$L" | tail -1 | grep -oE 'the drawn hands followed in [0-9]+ frames \([a-z]+\)' |
  sed 's/^/the window: /'
grep -E '^SWORDWT t=.* last 10 s' "$L" | tail -1 | grep -qE 'followed in [1-9][0-9]* frames \(follow\)' &&
  echo "PASS the drawn hands followed the blade" || echo "FAIL no frames followed"
echo "steps done $(date +%T)"
grep -E '^VRMENU apply (SWORD_WEIGHT_HAND|BOW_QUIVER)' "$L" | cut -c1-200
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (m17-steps.txt), compose: python3 scripts/xr_compose.py $R"
