#!/bin/bash
# settings34-drive.sh - build 34's VR menu groups (docs/re-xr-pipeline.md 150, som-b34-settings). Boot with
# scripts/headset-build26.env, SOMVR_HANDS_TEST=1 and SOMVR_XR_TEST=@RUN@/xrtest.txt (no SOMVR_SETTINGS: nothing is read
# or saved - the user's settings file is never touched). In the practice meadow after the arrival: the panel opened by
# the left palm, then by the right fingertip (the layer's `hand R touch panel U V`, the uv from tools/vm_uv.c for the
# session's avail): each group's button (a dump of its page) and a click on each v8 row, its `VRMENU apply KEY=VALUE
# from the menu` line checked; the panel closed. S34_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WTD=$(cd "$(dirname "$0")/.." && pwd -P)
WT=${S34_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
T0=$(date +%s)
R=${S34_RUN:-}
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
L=$R/somvr.log; X=$R/xr-layer.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$L}" 2>/dev/null; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -E "$2"; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/s34-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/s34-steps.txt"; }
d() { x "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/s34-steps.txt"; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
grep -E '^(WARN|VRMENU config on)' "$L" | cut -c1-200
grep -o 'av8 0x[0-9a-f]*' "$L" | head -1
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*' "$X" | head -1
[ -n "$AV" ] || bail "no somvr_vrmenu in the layer's log"
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
    "$WTD/mod/somvr/somvr_font.c" -lm || bail "vm_uv"
"$R/vm_uv" "$AV" > "$R/vm_uv.txt"
uv() { awk -F'\t' -v l="$1" -v p="$2" '$1 == l && $2 == p { print $4, $5; exit }' "$R/vm_uv.txt"; }
press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
          x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"
x "$PALM s 0.3"; sleep 1.5
grep -E 'vrmenu: (opened|closed)' "$X" | tail -1 | grep -q opened && pass "the panel open" || fail "the panel: $(grep -E 'vrmenu: (opened|closed)' "$X" | tail -1 | cut -c1-120)"
# group LABEL: its button; then click LABEL PART KEY=VALUE: the row's part, the game's apply line
group() { local u; u=$(uv "$1" button); [ -n "$u" ] || { fail "no button $1"; return; }
          local nx; nx=$(lines "$X"); press $u
          since "$nx" "click $1 button: " "$X" | grep -q . && pass "group $1: $(since "$nx" "click $1 button" "$X" | tail -1 | cut -c1-100)" \
            || fail "group $1"; }
click() { local u; u=$(uv "$1" "$2"); [ -n "$u" ] || { fail "no $2 of $1"; return; }
          local n0; n0=$(lines); press $u; sleep 0.5
          local l; l=$(since "$n0" "^VRMENU apply $3 from the menu" | tail -1)
          [ -n "$l" ] && pass "$1 $2: $(echo "$l" | cut -c1-200)" || fail "$1 $2: no '$3' ($(since "$n0" '^VRMENU apply' | tail -1 | cut -c1-120))"; }
group Display; d display
click "Watch" row "XR_WATCH_LOOK=any"
click "Watch size" row "WATCH_SIZE=large"
click "Low health" less "LOW_HEALTH=edge"
d display-after
group Sword; d sword
click "Force needed" less "FORCE_NEEDED=low"
click "Kill moves" row "KILLS=off"
group Fight; d fight
click "Shove push" more "SHOVE_PUSH=hard"
click "Grab time" more "VERBS_GRAB_MAX_S=20"
click "Knife grip" row "SLOTS_DAGGER_HOLD=off"
click "Herbs" row "HERBS=off"
group Climbing; d climbing
click "Fling down" row "CLIMB_FLING_DOWN=off"
click "Climb sword" row "CLIMB_SWORD=off"
group Bow; d bow
group Comfort; d comfort
x "hand R off" "$PALM_AWAY"; sleep 1; x "hand L off"; sleep 1
echo "== done $(date +%T)"; cat "$R/s34-steps.txt"
echo "warns: $(grep -c '^WARN' "$L") somvr, $(grep -c 'WARN' "$X") layer"
echo quit >> "$R/cmd"
