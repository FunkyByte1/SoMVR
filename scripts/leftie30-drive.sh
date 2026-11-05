#!/bin/bash
# leftie30-drive.sh - build 30's left-handed mirror (som-b30-leftie, docs/re-xr-pipeline.md 132), headless. One boot with
# scripts/headset-build26.env's switches and a settings file of its own that says HANDED=left (never the user's file):
#   mkdir -p /tmp/somvr-leftie30 && printf 'HANDED=left\n' > /tmp/somvr-leftie30/settings.conf
#   HEADLESS_HOLD=1200 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_SETTINGS=/tmp/somvr-leftie30/settings.conf"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time, from this worktree's scripts/ (PIN_WT = a word of the launch command, default this
# worktree's name). The steps are the right-handed drivers' own choreography (hla27's card, menu and first fight,
# bow20's bow, verbs13's grab, pin16's bank climb) played MIRRORED - every test hand and test button through mirw /
# mirx: L <-> R, the hand's x and its turns about Y and Z negated (a body mirrored about its middle); the menu button
# stays the left controller's (it has no twin), a touch's point on the panel and the sticks' directions stay. So the
# game must answer as it answered the right-handed run (the logs name the hands' ROLES: R = the sword hand), while
# the controllers that do it are the other ones - plus the physical checks: the card's A is the left X, the physical
# A closes nothing, the sword hand's grip at the left of the head, the layer's left fist round the sword and right
# fist round the bow, the grab refused by the left grip and taken by the right one, the stun by the right trigger.
# Each step a stereo dump (RUN/lh-steps.txt; `python3 scripts/xr_compose.py RUN -f FRAMES`). PASS/FAIL per check (run
# it with 2>&1). Quits at once when the user goes into VR. On build 29 (no mirror) the card, the draw, the grab and the
# climb's grabs FAIL (the mirrored controllers are the wrong roles there).
PIN_LIB=1 . "$(dirname "$0")/pin16-drive.sh" # R, L, userin, bail, go, probe, near, turnto, grabh, bankplan ...
HERE=$(cd "$(dirname "$0")" && pwd)
WTD=$(cd "$HERE/.." && pwd -P)
X=$R/xr-layer.log
FILE=${LH_FILE:-/tmp/somvr-leftie30/settings.conf}
. "$HERE/lh-mirror.sh" # mirw, mirx and the mirrored w, t, x
raw() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest (as is): $1" >&2; sleep 0.8; }
cnt() { grep -cE "$1" "${2:-$L}" 2>/dev/null; }
NP=0; NF=0
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2" "$5")" -gt "$4" ] && { pass "$1: $(grep -E "$2" "$5" | tail -1 | cut -c1-200)"; return 0; }
  sleep 0.25; done; fail "$1 (no '$2')"; return 1; }
waitl() { local i; for i in $(seq 1 $(($2 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$1")" -gt "${3:-0}" ] && return 0; sleep 0.25; done; return 1; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/lh-steps.txt"; }
# the newest HAND line's sword-hand grip from the head: "R-head=+30.0 right": its x (cm, + = right)
rhead() { grep -E '^HAND frame=' "$L" | tail -1 | grep -oE 'R-head=[+-][0-9.]+' | cut -d= -f2; }
# the layer's hands entry flags the preload logs every 10 s (after the mirror back: physical) - a fresh one
vhflags() { local n0; n0=$(cnt '^VRHANDS t=.* flags 0x'); waitl '^VRHANDS t=.* flags 0x' 12 "$n0"
  grep -E '^VRHANDS t=.* flags 0x' "$L" | tail -1 | grep -oE 'flags 0x[0-9a-f]+' | cut -d' ' -f2; }
md5() { md5sum "$1" 2>/dev/null | cut -c1-32; }
REAL=$HOME/.local/share/somvr/settings.conf
REAL0=$(md5 "$REAL")

echo "== 0. the session's mirror from the settings file"
grep -m1 '^CONFIG handed=' "$L" | cut -c1-200
grep -q '^CONFIG handed=left (the settings file' "$L" && pass "HANDED=left read from the settings file at the start" || fail "no CONFIG handed=left from the file"
sleep 3
echo "== 1. the arrival card: its words mirrored; the physical A closes nothing, the left X (the game's A) does"
d 01-card
grep -q 'xr: card: shown' "$X" && pass "the controls card at the arrival" || fail "no card shown"
J0=$(cnt '^JUMP t=.* go')
raw "btn R a 1"; sleep 0.3; raw "btn R a 0"; sleep 1.5
[ "$(cnt 'card: put away' "$X")" = 0 ] && pass "the physical A (now the X role) leaves the card up" || fail "the physical A closed the card"
t "btn R a 1"; sleep 0.3; t "btn R a 0"; sleep 2
seen "the card put away by the left X" 'card: put away \(X: the left-handed mirror' 3 0 "$X"
[ "$(cnt '^JUMP t=.* go')" -eq "$J0" ] && pass "no jump from the card's X" || fail "the card's X jumped"
grep -q 'card: put away' "$X" || { t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"; }
echo "== 2. the wrist menu on the right wrist, pointed at by the left hand: the Controls pages mirrored, Handedness"
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
[ -n "$AV" ] || bail "no avail in the layer's log"
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
  "$WTD/mod/somvr/somvr_font.c" -lm || bail "vm_uv did not build"
"$R/vm_uv" "$AV" > "$R/lh-uv.tsv"
uv() { awk -F'\t' -v l="$1" -v p="$2" -v g="$3" '$1 == l && $2 == p && $3 == g && !e { e = $4 " " $5 }
  $1 == l && $2 == p && $3 == "any" && !a { a = $4 " " $5 } END { print e ? e : a }' "$R/lh-uv.tsv"; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
opens() { grep -c 'vrmenu: opened by' "$X" 2>/dev/null; }
# (b37 live3: the menu button's long press pins the panel in front now - the long-press fallbacks below press its "Back to the wrist" (0.3359 0.9706) so the wrist panel goes on as before)
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8; press 0.3359 0.9706
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; return 1; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
grep -E 'xr: vrmenu: (found|the (left|right) hand points)' "$X" | head -2 | cut -c1-200
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 1
menu_open || bail "the menu did not open"
grep -E 'vrmenu: opened by' "$X" | tail -1 | grep -q 'right' && pass "opened at the right wrist" || echo "(the open line: $(grep -E 'vrmenu: opened by' "$X" | tail -1 | cut -c1-160))"
n0=$(cnt 'click Controls button: the controls page' "$X")
press $(uv "Controls" button any); sleep 1
seen "the Controls tab by the left hand" 'click Controls button: the controls page' 3 "$n0" "$X" && d 02-controls-1
for p in 2 3 4; do
  n0=$(cnt "click Next button: the controls page $p of" "$X")
  press $(uv "Next" button any); sleep 1
  [ "$(cnt "click Next button: the controls page $p of" "$X")" -gt "$n0" ] && d 0$((p + 1))-controls-$p
done
n0=$(cnt 'click Settings button: the settings page' "$X")
press $(uv "Settings" button any); sleep 1
seen "the Settings tab" 'click Settings button: the settings page' 3 "$n0" "$X" && d 06-settings
[ -n "$(uv "Handedness" more settings)" ] && [ -z "$(uv "Pointer hand" more settings)" ] &&
  pass "Handedness on the Settings page in Pointer hand's place" || fail "no Handedness row (or Pointer hand still there)"
menu_shut
echo "== 3. the sword in the left hand: from the left shoulder, cuts at an orc"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"; REST="hands test R 28 -40 20 0 0 0"
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.5
w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test L -25 -45 15 0 0 0"; w "$REST" 1
w "practice spawn infantry" 3
grep -E '^PRACTICE t=.* (orc|spawn)' "$L" | tail -1 | cut -c1-200
S0=$(cnt '^SLOTS .*sword: .*(now in the hand|kept in the hand)')
w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.8
[ "$(cnt '^SLOTS .*sword: .*(now in the hand|kept in the hand)')" -gt "$S0" ] && pass "the sword drawn: $(grep -E '^SLOTS .*sword: ' "$L" | tail -1 | cut -c1-160)" || fail "the sword not drawn"
sleep 1.3; RH=$(rhead)
[ -n "$RH" ] && awk -v x="$RH" 'BEGIN { exit !(x < -15) }' && pass "the sword hand's grip $RH cm right of the head (the left controller)" || fail "the sword hand at $RH cm (want < -15)"
d 07-sword-drawn
FL=$(vhflags); echo "VRHANDS flags $FL"
[ -n "$FL" ] && [ $(( FL & 8 )) = 8 ] && [ $(( FL & 16 )) = 0 ] && pass "the layer's LEFT fist round the sword (flags $FL)" || fail "the fists: flags $FL (want HOLD_L 8, not HOLD_R 16)"
C0=$(cnt '^COMBAT (hit|glance) ')
for k in 1 2 3 4; do
  w "stick 0 1 0 0.4" 0.6
  w "hands test R 40 -8 35 0 -90 90" 0.5; w "hands swing R -20 -18 62 0 90 90 300" 0.6; w "hands swing R 40 -8 35 0 -90 90 300" 0.6
  [ $k = 2 ] && d 08-fight
done
w "$REST" 1
NC=$(( $(cnt '^COMBAT (hit|glance) ') - C0 ))
[ "$NC" -gt 0 ] && pass "the left hand's cuts landed: $NC contacts ($(grep -E '^COMBAT (hit|glance) ' "$L" | tail -1 | cut -c1-120))" || fail "no contacts from the left hand's cuts"
w "hands test L off"; w "hands test R off"; w "hands squeeze L -1"; w "hands squeeze R -1"
echo "== 4. the bow in the right hand, drawn by the left (bow20's 'auto' mirrored)"
REST_L="hand L head -22 -40 15 0 0 0"; REST_R="hand R head 22 -40 15 0 0 0"
SLOT_L="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0 s 0.5"; DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
NOCK='^BOW t=[0-9.]+ frame=[0-9]+ nock: '
ex() { local nm=$1 re=$2 sec=$3 n0; shift 3; n0=$(cnt "$re"); T_S=0.05 t "$@"; seen "$nm" "$re" "$sec" "$n0" "$L"; }
t "$REST_L" "$REST_R"; sleep 1
t "$SLOT_L"; sleep 0.3
ex "the bow out of the right shoulder by the right grip" '^BOW .* enter ' 3 "$SLOT_L" "btn L squeeze 1"
t "$BOW_STRAIGHT" "$REST_R"; sleep 1
w "bow arrows 20" 0.6
FL=$(vhflags); echo "VRHANDS flags $FL"
[ -n "$FL" ] && [ $(( FL & 16 )) = 16 ] && pass "the layer's RIGHT fist round the bow (flags $FL)" || fail "the bow's fist: flags $FL (want HOLD_R 16)"
t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.8
ex "the left trigger at the string nocks" "$NOCK" 3 "$BOW_STRAIGHT" "btn R trigger 1"
ex "the full draw by the left hand" '^BOW .* full draw' 4 "$BOW_STRAIGHT" "$DRAWN"; sleep 0.6; d 09-bow-drawn
ex "the shot at the left trigger's release" '^BOW .* release: ' 3 "$BOW_STRAIGHT" "btn R trigger 0"
sleep 1.5; d 10-bow-after
t "$SLOT_L"; sleep 0.4; ex "the bow put back" '^BOW .* exit: ' 3 "$SLOT_L" "btn L squeeze 0"; sleep 0.6
t "btn L squeeze 0" "$REST_L" "$REST_R"; sleep 1
echo "== 5. the grab by the right hand (the left grip refused), the stun by the right trigger"
spawn() { w "practice spawn ${1:-infantry}" 3
  if grep -E '^PRACTICE t=.*spawn ' "$L" | tail -1 | grep -q 'FAILED'; then w "practice go" 6; w "practice spawn ${1:-infantry}" 3; fi; }
nearo() { local i dd; for i in $(seq 1 ${1:-20}); do w "execute where" 0.4
    dd=$(grep -E '^EXECUTE where ' "$L" | tail -1 | grep -oE 'dist [0-9]+' | cut -d' ' -f2)
    [ -n "$dd" ] && [ "$dd" -lt "${2:-200}" ] && return 0; w "execute face" 0.3; done; return 1; }
grep -qE '^PRACTICE t=.* (orc .*(dead|down|removed))' "$L" && spawn infantry
G0=$(cnt '^VERBS t=[0-9.]* grab: ')
GOTL=0; GOTR=0
for k in 1 2 3 4 5 6; do
  nearo 25 130; w "execute face" 0.3; w "verbs aim head" 0.5
  n0=$(cnt '^VERBS t=[0-9.]* grab: '); raw "btn L squeeze 1"; sleep 0.6
  [ "$(cnt '^VERBS t=[0-9.]* grab: ')" -gt "$n0" ] && GOTL=1
  raw "btn L squeeze 0"; sleep 0.4
  n0=$(cnt '^VERBS t=[0-9.]* grab: '); t "btn L squeeze 1"; sleep 0.5
  [ "$(cnt '^VERBS t=[0-9.]* grab: ')" -gt "$n0" ] && { GOTR=1; break; }
  t "btn L squeeze 0"; w "execute face" 0.3
done
[ $GOTL = 0 ] && pass "the left grip (the sword hand) grabs no orc" || fail "the left grip grabbed"
[ $GOTR = 1 ] && pass "the right grip grabs: $(grep -E '^VERBS t=[0-9.]* grab: ' "$L" | tail -1 | cut -c1-160)" || fail "the right grip grabbed no orc"
if [ $GOTR = 1 ]; then
  d 11-grab
  n0=$(cnt '^VERBS t=.* wraith: the left hand holding orc')
  raw "btn R trigger 1"; sleep 0.25; raw "btn R trigger 0"; sleep 1.5
  [ "$(cnt '^VERBS t=.* wraith: the left hand holding orc')" -gt "$n0" ] &&
    pass "the right trigger on the held orc = the wraith hand's press: $(grep -E '^VERBS t=.* wraith: ' "$L" | tail -1 | cut -c1-160)" ||
    fail "the right trigger did nothing on the held orc"
  t "btn L squeeze 0"; sleep 1
fi
w "verbs aim off" 0.3
# build 30: the meadow bank is not a climbing wall since SOMVR_CLIMB_SURF=walls (stone, not CanWallClimb): the climb's
# proof is climb27-drive.sh with MIRROR_LH=1 at a real wall; LH_BANK=1 tries the bank anyway
if [ "${LH_BANK:-0}" = 1 ]; then
echo "== 6. climbing the meadow bank (pin16's bank climb, mirrored)"
T_S=0.6
n0=$(cnt '^CLIMB t=.* grab [LR]:')
bankplan
NG=$(( $(cnt '^CLIMB t=.* grab [LR]:') - n0 ))
grep -E '^CLIMB t=.* grab [LR]:' "$L" | tail -2 | cut -c1-200
[ "$NG" -ge 3 ] && pass "climbing: $NG holds taken hand over hand" || fail "climbing: $NG holds"
grep -qE 'pullover: the pull done' "$L" && pass "climbing: the pullover onto the bank's top" || echo "(no pullover done)"
fi
echo "== 7. the files"
[ "$(md5 "$REAL")" = "$REAL0" ] && pass "the user's settings file unchanged" || fail "the user's settings file CHANGED"
grep -q '^HANDED=left$' "$FILE" && pass "the run's settings file still says HANDED=left" || fail "the run's file: $(cat "$FILE" | tr '\n' ' ')"
echo "steps done $(date +%T): PASS $NP, FAIL $NF"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (lh-steps.txt), compose: python3 scripts/xr_compose.py $R -f FRAMES"
