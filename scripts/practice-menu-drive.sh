#!/bin/bash
# practice-menu-drive.sh - the practice page's buttons pressed by a fingertip in one headless boot (the user's way to
# get an orc: the wrist menu > Practice > Spawn an orc), never seen in a real run before 10-24 (only the trigger word).
# Boot (outside the sandbox) with the layer's test file and the build's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' logs/build-vrN/headset.env | tr '\n' ' ')
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid scripts/headless_vr.sh <build>/ShadowOfMordor/mod/somvr
# and this script at the same time (PMENU_WT=build-vrN: the newest run of it). The uv of each button is the panel's own
# layout (somvr_vm_rect, practice page): the tab 0.441 0.042, Spawn 0.5 0.474, the orc type's > 0.922 0.389, Remove
# 0.5 0.560. Steps: the wrist menu (the left palm toward the eyes; retried, then the menu button's long press), the Practice tab, Spawn (infantry), the menu shut,
# a mark; again: the type's > (shield), Spawn (it replaces the first), a mark; Remove. Prints PASS/FAIL per step from
# somvr.log and ends with a quit. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${PMENU_WT:-build-vr9}
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
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { w "mark $1" 4; }
# press U V: the right fingertip from 5 mm in front, through the panel, back out (menus-drive.sh's front approach)
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
# the wrist menu: the palm gesture (held off while an attack is incoming: the palm away and back, up to 4 tries), then
# the left menu button held 1 s (a long press: the panel toggles past the gate); prints how it opened
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; grep 'held off\|vrmenu: opened\|vrmenu: closed' "$R/xr-layer.log" | tail -3 | cut -c1-200; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
n() { grep -c "$1" "$R/somvr.log" 2>/dev/null; }
check() { if [ "$(n "$2")" -gt "$3" ]; then echo "PASS $1"; else echo "FAIL $1 (no '$2')"; fi; }
S0=$(n 'spawn [A-Za-z]* (the menu)'); I0=$(n 'spawn Infantry (the menu)'); H0=$(n 'spawn Shield (the menu)')
# 1. the wrist menu, the Practice tab, Spawn
menu_open; t "dump"; sleep 1.5
press 0.441 0.042; t "dump"; sleep 1.5
press 0.5 0.474; sleep 4
check "spawn from the menu (infantry)" 'spawn Infantry (the menu)' "$I0"
t "dump"; sleep 1.5
menu_shut; m p01-infantry
grep -q 'spawn check 1: the Infantry orc' "$R/somvr.log" && echo "PASS the infantry orc alive" || echo "FAIL no infantry check"
# 2. the type's > (shield), Spawn: it replaces the infantry
menu_open; press 0.441 0.042; t "dump"; sleep 1.5
press 0.922 0.389; t "dump"; sleep 1.5
press 0.5 0.474; sleep 4
check "spawn from the menu (shield)" 'spawn Shield (the menu)' "$H0"
menu_shut; m p02-shield
# 3. Remove (the shield's spawn removed the infantry: count from here)
RM0=$(n 'PRACTICE t=.* remove the Shield orc')
menu_open; press 0.441 0.042
press 0.5 0.560; sleep 3
check "remove from the menu" 'PRACTICE t=.* remove the Shield orc' "$RM0"
t "dump"; sleep 1.5
menu_shut; m p03-removed
echo "menu spawns: $(( $(n 'spawn [A-Za-z]* (the menu)') - S0 ))"
grep -E '^PRACTICE t=.* (spawn|remove|refused|the .* orc)' "$R/somvr.log" | tail -12 | cut -c1-220
grep -c '^WARN' "$R/somvr.log" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (xr-layer.log.stereoN / vrmenuN), marks in $R/marks"
