#!/bin/bash
# hla27-drive.sh - build 27's first ten minutes as a first-time player, held to Half-Life: Alyx (docs/re-xr-pipeline.md
# 112, handoff/HANDOFF-hla.md; som-b27-hla). One headless boot with scripts/headset-build26.env's switches and a COPY of
# the user's settings file (never the file itself):
#   mkdir -p /tmp/somvr-hla27 && cp ~/.local/share/somvr/settings.conf /tmp/somvr-hla27/settings.conf
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_SETTINGS=/tmp/somvr-hla27/settings.conf"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time, from this worktree's scripts/ (H27_WT = a word of the launch command, default this
# worktree's name; H27_FILE = the settings copy). Steps (each a stereo dump, listed in RUN/hla-steps.txt; compose with
# `python3 scripts/xr_compose.py RUN -f FRAMES`):
#   0. the launch's way in: the main menu, the load, the world, the practice area's arrival with the controls card
#   1. the card away by A (its own "or A": no jump - build 26 jumped); the wrist menu; the Controls tab's pages 1..N (Next, Next at the end = nothing, Prev);
#      the Settings page (Walk toward under Snap blink); the layer's avail has build 27's bits
#   2. Walk toward live: the left hand turned 90 deg from the head; the stick forward walks with the head (Head), then
#      with the hand (the row set to Hand: the game's VRMENU apply MOVE=hand), then Head again; Talion's way from
#      `bow where` - with Hand the walk turns by the hand's angle, with Head it does not (fails on build 26: no row)
#   3. the first fight: an orc spawned, the sword drawn from the shoulder, cuts at it; then Back to where I was
#   4. the settings copy keeps its old lines (MOVE=head added: the menu pins a touched key)
# Prints PASS/FAIL per step (run it with 2>&1: the words and xrtest lines go to stderr); quits at once when the user
# goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WTD=$(cd "$HERE/.." && pwd -P)
WT=${H27_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
FILE=${H27_FILE:-/tmp/somvr-hla27/settings.conf}
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
X=$R/xr-layer.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
t() { local x; userin && bail "the user is in VR"; { for x in "$@"; do echo "$x"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*" >&2; sleep "${T_S:-0.6}"; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1" >&2; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$2" 2>/dev/null; }
NP=0; NF=0
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
# seen NAME REGEX SECONDS N0 FILE: a line past the first N0 matches within SECONDS
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2" "$5")" -gt "$4" ] && { pass "$1: $(grep -E "$2" "$5" | tail -1 | cut -c1-200)"; return 0; }
  sleep 0.25; done; fail "$1 (no '$2')"; return 1; }
# waitl REGEX SECONDS N0: a somvr.log line past the first N0 (no PASS / FAIL)
waitl() { local i; for i in $(seq 1 $(($2 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$1" "$L")" -gt "${3:-0}" ] && return 0; sleep 0.25; done; return 1; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/hla-steps.txt"; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
opens() { grep -c 'vrmenu: opened by' "$X" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; return 1; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
md5() { md5sum "$1" 2>/dev/null | cut -c1-32; }
REAL=$HOME/.local/share/somvr/settings.conf
REAL0=$(md5 "$REAL")
cp "$FILE" "$R/h27-settings-before.conf" 2>/dev/null

echo "== 0. the way in: the main menu, the load, the world, the arrival"
waitl 'SESSION ui ifs=1 menus=15' 200 && d 01-main-menu || fail "no main menu"
waitl 'SESSION ui ifs=3 menus=0 paused=1' 60 && { sleep 4; d 02-loading; } || echo "(no paused load line)"
N0=$(cnt 'SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' "$L")
waitl 'SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' 200 "$N0" && { sleep 1.5; d 03-world; } || fail "no world"
waitl '^PRACTICE t=.* arrived' 120 || bail "no arrival"
sleep 3
d 04-arrival-card
grep -q 'xr: card: shown' "$X" && pass "the controls card at the arrival" || fail "no card shown"
echo "== 1. the card away, the menu, the Controls pages, the Settings page"
# the card says "Close: X box or A": A, as a first-time player reads it - the card's, never a jump (112.3; build 26
# jumped: the press reached the jump while the layer shut the card)
J0=$(cnt '^JUMP t=.* go' "$L"); P0=$(cnt '^JUMP t=.* press' "$L")
t "btn R a 1"; sleep 0.3; t "btn R a 0"; sleep 2
seen "the card put away by A" 'card: put away \(A\)' 2 0 "$X"
grep -E '^JUMP t=.* press' "$L" | tail -1 | cut -c1-220
[ "$(cnt '^JUMP t=.* go' "$L")" -eq "$J0" ] && pass "no jump from the card's A ($(( $(cnt '^JUMP t=.* press' "$L") - P0 )) press line(s))" ||
  fail "the card's A jumped: $(grep -E '^JUMP t=.* go' "$L" | tail -1 | cut -c1-160)"
grep -q 'card: put away' "$X" || { t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"; }
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
[ -n "$AV" ] || bail "no avail in the layer's log"
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
  "$WTD/mod/somvr/somvr_font.c" -lm || bail "vm_uv did not build"
"$R/vm_uv" "$AV" > "$R/h27-uv.tsv"
uv() { awk -F'\t' -v l="$1" -v p="$2" -v g="$3" '$1 == l && $2 == p && $3 == g && !e { e = $4 " " $5 }
  $1 == l && $2 == p && $3 == "any" && !a { a = $4 " " $5 } END { print e ? e : a }' "$R/h27-uv.tsv"; }
B27=$(( (AV >> 47) & 0xffff )); B39=$(( (AV >> 39) & 3 ))
echo "avail $AV: bits 47..62 = $(printf '%#x' $B27) (WALKDIR VAULT ABOVE BRAND RALLY DISMISS COMMAND FAILED NEMESIS DOWN OVERHANG COUNTER DOMHOLD OVERORC STRUGGLE MANTLE), 39..40 = $B39 (GLYPH SHOVE)"
[ $(( B27 & 1 )) = 1 ] && pass "the Walk toward bit (47)" || fail "no Walk toward bit"
[ $B27 = $(( 0xffff )) ] && [ $B39 = 3 ] && pass "every build 27 gesture bit with the build 26 env" ||
  fail "build 27's bits: $(printf '%#x' $B27) / $B39 (want 0xffff / 3)"
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 1
menu_open || bail "the menu did not open"
n0=$(cnt 'click Controls button: the controls page' "$X")
press $(uv "Controls" button any); sleep 1
seen "the Controls tab" 'click Controls button: the controls page' 3 "$n0" "$X"
d 05-controls-1
NPG=3 # the page count: the layer's "page N of M" (build 27: 4 pages once the abilities' lines came)
for p in 2 3 4 5 6; do
  [ "$p" -gt "$NPG" ] && break
  n0=$(cnt "click Next button: the controls page $p of" "$X")
  press $(uv "Next" button any); sleep 1
  seen "Next: page $p" "click Next button: the controls page $p of" 3 "$n0" "$X" && d 0$((p + 4))-controls-$p
  m=$(grep -oE "click Next button: the controls page $p of [0-9]+" "$X" | tail -1 | grep -oE '[0-9]+$'); [ -n "$m" ] && NPG=$m
done
n0=$(cnt "click Next button: nothing \(the controls page's end\)" "$X")
press $(uv "Next" button any); sleep 0.8
seen "Next at the last page: nothing" "click Next button: nothing \(the controls page's end\)" 3 "$n0" "$X"
n0=$(cnt "click Prev button: the controls page $((NPG - 1)) of" "$X")
press $(uv "Prev" button any); sleep 0.8
seen "Prev: page $((NPG - 1))" "click Prev button: the controls page $((NPG - 1)) of" 3 "$n0" "$X"
n0=$(cnt 'click Settings button: the settings page' "$X")
press $(uv "Settings" button any); sleep 1
seen "the Settings tab" 'click Settings button: the settings page' 3 "$n0" "$X"
d 08-settings
[ -n "$(uv "Walk toward" more settings)" ] && pass "the Walk toward row on the Settings page" || fail "no Walk toward row"
menu_shut
echo "== 2. Walk toward live"
where() { local n0; n0=$(cnt '^BOW where t=' "$L"); w "bow where" 0.3; waitl '^BOW where t=' 4 "$n0"
  grep '^BOW where t=' "$L" | tail -1 | grep -oE 'Talion at [-0-9.]+,[-0-9.]+,[-0-9.]+' | sed 's/Talion at //'; }
# walk NAME: the left hand at the hip turned 90 deg to the right (its fist pointing right), the head ahead; 1.5 s of the
# stick forward; prints "dx dz" of Talion's way
HL="hand L head -25 -45 20 0 -90 0"; HLW="hands test L -25 -45 20 0 -90 0"
walk() { local a b; t "$HL" "hand R head 22 -40 15 0 0 0"; w "$HLW" 0.6; sleep 0.8
  a=$(where); w "stick 0 1 0 1.5" 0.2; sleep 2.6; b=$(where)
  echo "$(date +%T) WALK $1: from $a to $b" | tee -a "$R/hla-steps.txt" >&2
  python3 -c "import sys; a=[float(x) for x in '$a'.split(',')]; b=[float(x) for x in '$b'.split(',')]; print(b[0]-a[0], b[2]-a[2])"; }
ang() { python3 -c "import math; a=[float(x) for x in '$1'.split()]; b=[float(x) for x in '$2'.split()]
d=math.degrees(math.atan2(a[0]*b[1]-a[1]*b[0], a[0]*b[0]+a[1]*b[1])); print('%.0f %.0f %.0f' % (d, math.hypot(*a), math.hypot(*b)))"; }
row_click() { local lab=$1 part=$2 n0; menu_open || bail "the menu did not open ($lab)"
  n0=$(cnt "click $lab [<>]: " "$X"); press $(uv "$lab" "$part" settings); sleep 0.6
  grep "xr: vrmenu: click $lab " "$X" | tail -1 | cut -c1-200; menu_shut; }
V1=$(walk head-1)
A0=$(cnt '^VRMENU apply MOVE=hand' "$L")
row_click "Walk toward" more
seen "the game applied Walk toward = Hand" '^VRMENU apply MOVE=hand from the menu' 4 "$A0" "$L"
V2=$(walk hand)
A0=$(cnt '^VRMENU apply MOVE=head' "$L")
row_click "Walk toward" less
seen "the game applied Walk toward = Head" '^VRMENU apply MOVE=head from the menu' 4 "$A0" "$L"
V3=$(walk head-2)
w "hands test L off" 0.3
read -r a12 m1 m2 <<< "$(ang "$V1" "$V2")"
read -r a13 m1b m3 <<< "$(ang "$V1" "$V3")"
echo "$(date +%T) the way: head-1 ($V1) vs hand ($V2): $a12 deg; head-1 vs head-2 ($V3): $a13 deg; lengths $m1 $m2 $m3 cm"
[ "${m1%.*}" -gt 100 ] && [ "${m2%.*}" -gt 100 ] && [ "${m3%.*}" -gt 100 ] && pass "each walk went somewhere (> 1 m)" || fail "a walk too short"
a12a=${a12#-}; a13a=${a13#-}
[ "$a12a" -ge 60 ] && [ "$a12a" -le 120 ] && pass "Hand: the walk turned by the hand's 90 deg ($a12)" || fail "Hand: $a12 deg (want 60..120)"
[ "$a13a" -le 20 ] && pass "Head again: the walk follows the head ($a13)" || fail "Head again: $a13 deg (want <= 20)"
echo "== 3. the first fight"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"; REST="hands test R 28 -40 20 0 0 0"
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.5
w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test L -25 -45 15 0 0 0"; w "$REST" 1
n0=$(cnt '^PRACTICE t=.* (orc|spawn)' "$L")
w "practice spawn infantry" 3
grep -E '^PRACTICE t=.* (orc|spawn)' "$L" | tail -2 | cut -c1-200
d 09-orc
w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.8
grep -E '^SLOTS .*sword: .*(now in the hand|kept in the hand)' "$L" | tail -1 | grep -q . && pass "the sword drawn from the shoulder" || fail "the sword not drawn"
d 10-sword-drawn
C0=$(cnt '^COMBAT (hit|glance) ' "$L")
for k in 1 2 3 4; do
  w "stick 0 1 0 0.4" 0.6
  w "hands test R 40 -8 35 0 -90 90" 0.5; w "hands swing R -20 -18 62 0 90 90 300" 0.6; w "hands swing R 40 -8 35 0 -90 90 300" 0.6
  [ $k = 2 ] && d 11-fight
done
w "$REST" 1
echo "$(date +%T) contacts $(( $(cnt '^COMBAT (hit|glance) ' "$L") - C0 ))"
d 12-after
w "practice remove" 1
# back to the game the card's way: Menu > Practice > Back to where I was
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.5
if menu_open; then
  press $(uv "Practice" button any); sleep 0.8
  n0=$(cnt '^PRACTICE t=.* arrived at' "$L")
  press $(uv "Back to where I was" button practice); sleep 0.5
  menu_shut
  seen "Back to where I was (the card's way to the game)" '^PRACTICE t=.* arrived at' 20 "$n0" "$L" ||
    grep -E '^PRACTICE t=.* back .*refused' "$L" | tail -1 | cut -c1-200
  sleep 4; d 13-back-in-the-world
else fail "the menu did not open for Back"; fi
echo "== 4. the settings copy, the user's file"
sleep 4
cp "$FILE" "$R/h27-settings-after.conf" 2>/dev/null
ok=1
while IFS= read -r line; do
  case "$line" in \#*|"") continue ;; esac
  grep -qxF "$line" "$FILE" || { ok=0; echo "  lost: $line"; }
done < "$R/h27-settings-before.conf"
[ $ok = 1 ] && pass "the settings copy keeps every old line ($(grep -c '=' "$FILE") keys now)" || fail "the settings copy lost lines"
grep -q '^MOVE=head$' "$FILE" && pass "the copy saved MOVE=head (the key touched)" || echo "(MOVE not in the copy: $(grep MOVE "$FILE"))"
[ "$(md5 "$REAL")" = "$REAL0" ] && pass "the user's settings file unchanged" || fail "the user's settings file CHANGED"
echo "steps done $(date +%T): PASS $NP, FAIL $NF"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (hla-steps.txt), compose: python3 scripts/xr_compose.py $R -f FRAMES"
