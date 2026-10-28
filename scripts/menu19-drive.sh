#!/bin/bash
# menu19-drive.sh - build 19's VR menu in one headless boot (docs/re-xr-pipeline.md 73, handoff/HANDOFF-menus.md
# "build 19"): the 64-bit availability word, the Fight page, every row of both pages, a v6 settings file. Boot (sandbox
# off) with scripts/headset-build19.env's switches and a COPY of a settings file (never the user's own):
#   cp ~/.local/share/somvr/settings.conf /tmp/somvr-menu19/settings.conf   (or any v6 file)
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build19.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_SETTINGS=/tmp/somvr-menu19/settings.conf"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (M19_WT = a word of the launch command, default this worktree's name; M19_FILE = the
# settings copy, default /tmp/somvr-menu19/settings.conf). The panel is pressed by the layer's right test controller's
# fingertip (menu13-drive.sh's press) at the uv tools/vm_uv.c gives for the avail the layer logs.
#   0. the start: VRMENU config (the file's keys taken, no bad line), the layer's avail (bits 32..37)
#   1. the Settings page (dump): every row's > then < (a dimmed row: "nothing"); Main hand left out (it moves the panel
#      to the other wrist: test_layer's)
#   2. Fight settings > (dump): every row the same, the game side's VRMENU apply line for each new row; the Bow string's
#      BOW line (the bow away); the Crowd pace row hovered (dump: its two lines of help); < Settings back
#   3. the Controls page (dump), the Practice page (dump)
#   4. the Wraith step row live: Off -> the right stick forward aims nothing; On -> "aiming", cancelled by the move stick
#   5. the settings copy: its old lines kept
# Dumps: RUN/m19-steps.txt (scripts/xr_compose.py RUN -f ...). Prints PASS/FAIL per step, ends with a quit; quits at
# once when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WTD=$(cd "$HERE/.." && pwd -P)
WT=${M19_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
FILE=${M19_FILE:-/tmp/somvr-menu19/settings.conf}
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
cp "$FILE" "$R/m19-settings-before.conf" 2>/dev/null
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
t() { local x; userin && bail "the user is in VR"; { for x in "$@"; do echo "$x"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$2"; }
NP=0; NF=0
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2" "$5")" -gt "$4" ] && { pass "$1: $(grep -E "$2" "$5" | tail -1 | cut -c1-200)"; return 0; }
  sleep 0.25; done; fail "$1 (no '$2')"; return 1; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/m19-steps.txt"; }
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
echo "== 0. the start"
grep -E '^VRMENU config on|^WARN VRMENU' "$L" | cut -c1-400
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
[ -n "$AV" ] || bail "no avail in the layer's log"
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
  "$WTD/mod/somvr/somvr_font.c" -lm || bail "vm_uv did not build"
"$R/vm_uv" "$AV" > "$R/m19-uv.tsv"
echo "avail $AV (bits 32..37: $(( (AV >> 32) & 63 )))"
[ $(( (AV >> 32) & 63 )) -eq 63 ] && pass "the six Fight bits in the 64-bit avail" || fail "the Fight bits: $(( (AV >> 32) & 63 ))"
grep -q 'keys taken TURN=smooth TURN_DPS=90 VIGNETTE=0.5 SNAP_BLINK_MS=140 HAPTICS=100 PHASE=on COMBAT_BITE=on, bad lines 0' "$L" &&
  pass "the v6 settings file: its 7 keys taken, no bad line" || fail "the v6 file's keys: $(grep -o 'keys taken [^;]*bad lines [0-9]*' "$L" | head -1)"
# uv LABEL PART PAGE -> "U V"
uv() { awk -F'\t' -v l="$1" -v p="$2" -v g="$3" '$1 == l && $2 == p && $3 == g && !e { e = $4 " " $5 }
  $1 == l && $2 == p && $3 == "any" && !a { a = $4 " " $5 } END { print e ? e : a }' "$R/m19-uv.tsv"; }
# row LABEL PAGE: > then < (or < then > when > is held at the end): the layer's click lines
row() { local lab=$1 pg=$2 um ul n0 n1 last
  um=$(uv "$lab" more "$pg"); ul=$(uv "$lab" less "$pg")
  [ -n "$um" ] || { fail "$lab: no rect on the $pg page"; return; }
  n0=$(grep -c "xr: vrmenu: click $lab [<>]: " "$X")
  press $um; sleep 0.3
  last=$(grep "xr: vrmenu: click $lab >: " "$X" | tail -1)
  if echo "$last" | grep -q ': changed '; then
    press $ul; sleep 0.3
    n1=$(grep -c "xr: vrmenu: click $lab [<>]: changed" "$X")
    pass "$lab: $(echo "$last" | grep -o 'changed.*-> [^(]*') and back ($(grep "xr: vrmenu: click $lab <: " "$X" | tail -1 | grep -o 'changed.*-> [^(]*\|nothing'))"
  elif echo "$last" | grep -q ': nothing'; then
    press $ul; sleep 0.3
    if grep "xr: vrmenu: click $lab <: " "$X" | tail -1 | grep -q ': changed '; then
      press $um; sleep 0.3
      pass "$lab: > held at the end, < $(grep "xr: vrmenu: click $lab <: " "$X" | tail -1 | grep -o 'changed.*-> [^(]*') and back"
    else
      pass "$lab: shown, dimmed here (> and <: nothing)"
    fi
  else
    fail "$lab: no click line (last: ${last:0:160})"
  fi
}
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$X" || { t "btn R a 1"; t "btn R a 0"; }
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 1
echo "== 1. the Settings page"
menu_open || bail "the menu did not open"
d settings-before
A0=$(grep -c '^VRMENU apply ' "$L")
for lab in "Turn" "Snap angle" "Turn speed" "Vignette" "Snap blink" "Seated" "Seat height" "HUD" "HUD distance" "View" \
           "Haptics" "Music" "Player size" "Crouch" "Climb fling" "Hands at walls"; do
  row "$lab" settings
done
echo "VRMENU apply lines on the Settings page: $(( $(grep -c '^VRMENU apply ' "$L") - A0 ))"
d settings-after
echo "== 2. the Fight page"
n0=$(grep -c 'click Fight settings button: the fight page' "$X")
press $(uv "Fight settings" button settings); sleep 0.8
seen "Fight settings > opens the Fight page" 'click Fight settings button: the fight page' 3 "$n0" "$X"
d fight
A0=$(grep -c '^VRMENU apply ' "$L")
for lab in "Sword angle" "Sword weight" "Sword hand" "Blade bite" "Arrows" "Bow string" "Dodge (A)" "Wraith step" \
           "Stealth kill" "Crowd pace" "Counter icon"; do
  if [ -z "$(uv "$lab" more fight)" ]; then echo "$(date +%T) (no $lab row in this build: its bit is not in the avail)"; continue; fi
  row "$lab" fight
done
echo "VRMENU apply lines on the Fight page: $(( $(grep -c '^VRMENU apply ' "$L") - A0 ))"
for k in BOW_STRING DODGE WRAITH_STEP STEALTH_KILL CROWD_PACE_MS COUNTER_ICON; do
  [ "$(grep -c "^VRMENU apply $k=.* from the menu" "$L")" -ge 2 ] &&
    pass "the game applied $k live: $(grep "^VRMENU apply $k=" "$L" | head -2 | sed 's/^VRMENU apply //' | cut -c1-90 | tr '\n' '|')" ||
    fail "$k: $(grep -c "^VRMENU apply $k=" "$L") apply lines"
done
seen "the bow took the Bow string (away)" '^BOW t=.* string (grip|trigger) -> (grip|trigger) \(the VR menu' 3 0 "$L"
seen "the wraith step took the row" '^WSTEP t=.* now (on|off) \(the word or the VR menu' 3 0 "$L"
# the Crowd pace row hovered (the fingertip 3 cm in front of its label): its two lines of help
t "hand R touch panel $(uv "Crowd pace" row fight) 3"; sleep 1
d fight-hover-crowd
n0=$(grep -c 'click Settings button: the settings page' "$X")
press $(uv "Settings" button fight); sleep 0.8
seen "< Settings back" 'click Settings button: the settings page' 3 "$n0" "$X"
echo "== 3. Controls, Practice"
press $(uv "Controls" button any); sleep 1; d controls
press $(uv "Practice" button any); sleep 1; d practice
press $(uv "Settings" button any); sleep 0.8
menu_shut
echo "== 4. the Wraith step row live"
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.6
menu_open || bail "the menu did not open (2)"
press $(uv "Fight settings" button settings); sleep 0.8
press $(uv "Wraith step" more fight); sleep 0.5
grep "xr: vrmenu: click Wraith step >: " "$X" | tail -1 | grep -q 'changed On -> Off' && pass "Wraith step Off" || fail "Wraith step not Off"
press $(uv "Settings" button fight); sleep 0.5
menu_shut
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.6
n0=$(grep -c 'the right stick forward' "$L")
t "stick R 0 1"; sleep 1.5; t "stick R 0 0"; sleep 0.8
[ "$(grep -c 'the right stick forward' "$L")" -eq "$n0" ] && pass "Off: the right stick forward aims nothing" ||
  fail "Off: $(grep 'the right stick forward' "$L" | tail -1 | cut -c1-160)"
menu_open || bail "the menu did not open (3)"
press $(uv "Fight settings" button settings); sleep 0.8
press $(uv "Wraith step" more fight); sleep 0.5
grep "xr: vrmenu: click Wraith step >: " "$X" | tail -1 | grep -q 'changed Off -> On' && pass "Wraith step On" || fail "Wraith step not On"
press $(uv "Settings" button fight); sleep 0.5
menu_shut
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.6
n0=$(grep -c 'the right stick forward' "$L")
t "stick R 0 1"; sleep 1.2
seen "On: the right stick forward aims (or says why not)" 'the right stick forward' 3 "$n0" "$L"
t "stick L 0 0.8"; sleep 0.5; t "stick L 0 0" "stick R 0 0"; sleep 1
grep -E '^WSTEP t=.* (cancelled|arrived|refused)' "$L" | tail -2 | cut -c1-200
echo "== 5. the settings copy"
sleep 2
cp "$FILE" "$R/m19-settings-after.conf" 2>/dev/null
ok=1
while IFS= read -r line; do
  case "$line" in \#*|"") continue ;; esac
  grep -qxF "$line" "$FILE" || { ok=0; echo "  lost: $line"; }
done < "$R/m19-settings-before.conf"
[ $ok = 1 ] && pass "the settings copy keeps every old line ($(grep -c '=' "$FILE") keys now)" || fail "the settings copy lost lines"
diff "$R/m19-settings-before.conf" "$FILE" | sed 's/^/  /' | head -30
echo "steps done $(date +%T): PASS $NP, FAIL $NF"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (m19-steps.txt), compose: python3 scripts/xr_compose.py $R -f FRAMES"
