#!/bin/bash
# far18-drive.sh - build 18's practice page button "Spawn far orc (unaware)" (docs/re-xr-pipeline.md 56.9, handoff/
# HANDOFF-menus.md "build 18"), headless at the practice arrival. Boot (sandbox off) with scripts/headset-build18.env:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build18.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (F18_WT = a word of the launch command, default this worktree's name).
#   1. the wrist menu, the Practice tab (dump), "Spawn far orc (unaware)" (uv 0.500 0.482): PRACTICE spawn far ... ok
#   2. 32 s with the hands at rest: the watch lines every 2 s (its target none, unaware; dumps at +4 s and +30 s)
#   3. the bow from the left shoulder, an arrow from the quiver, `bow aimhead` + `bow orchp 400` (the far orc is the
#      nearest: the arrow on its head, its health high enough for a reaction), the shot: "far orc noticed Talion",
#      it comes (dumps); then the orc removed
# Dumps: RUN/f18-steps.txt (scripts/xr_compose.py RUN). Quits at the end, at once if the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${F18_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
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
  [ "$(cnt "$2")" -gt "$4" ] && { echo "$(date +%T) PASS $1: $(grep -E "$2" "$L" | tail -1 | cut -c1-240)"; return 0; }
  sleep 0.25; done; echo "$(date +%T) FAIL $1 (no '$2')"; return 1; }
ex() { local nm=$1 re=$2 sec=$3 n0; shift 3; n0=$(cnt "$re"); T_S=0.05 t "$@"; seen "$nm" "$re" "$sec" "$n0"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/f18-steps.txt"; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
REST_L="hand L head -22 -40 15 0 0 0"; REST_R="hand R head 22 -40 15 0 0 0"
SLOT_L="hand L head -12 -10 -15 90 0 0"; QUIVER="hand R head 12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_NOCK="hand R head -4 -11 47 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
grep -E '^CONFIG practice' "$L" | grep -oE 'orcs [0-9]+ cm ahead \(the far orc [0-9]+ cm[^)]*\)'
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
t "$REST_L" "$REST_R"; sleep 1
echo "== 1. the button"
menu_open
press 0.441 0.042; sleep 1; d practice-page
n0=$(cnt '^PRACTICE t=.* spawn far ')
press 0.500 0.482
seen "Spawn far orc (unaware)" '^PRACTICE t=.* spawn far .* ok: one orc out' 4 "$n0"
menu_shut
echo "== 2. 32 s unaware"
t "$REST_L" "$REST_R"
sleep 3; d far-4s
sleep 22; d far-30s
sleep 4
grep -E '^PRACTICE t=.* far orc ' "$L" | tail -16 | cut -c1-200
grep -E '^PRACTICE t=.* far orc \+3[0-9] s: .*its target none; unaware' "$L" | tail -1 | grep -q . &&
  echo "PASS unaware for 30 s (no target, its line at +30..39 s)" || echo "FAIL not unaware at +30 s"
grep -qE '^PRACTICE t=.* far orc noticed' "$L" && echo "FAIL noticed before the attack" || echo "PASS not noticed before the attack"
echo "== 3. attacked: the bow"
t "$REST_L" "$REST_R"; sleep 0.5
t "$SLOT_L"; sleep 0.4; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1
t "$QUIVER"; sleep 0.5; ex "an arrow taken" 'quiver: an arrow taken' 3 "btn R trigger 1"
ex "nocked" '^BOW .* nock: ' 3 "$BOW_STRAIGHT" "$AT_NOCK"
w "bow orchp 400" 0.6
w "bow aimhead" 0.6
ex "full draw" '^BOW .* full draw' 4 "$DRAWN"; sleep 0.4
n1=$(cnt '^PRACTICE t=.* far orc noticed')
T_S=0.4 t "btn R trigger 0"
seen "the shot" '^BOW .* release: ' 2 0
seen "the far orc noticed Talion" '^PRACTICE t=.* far orc noticed Talion' 8 "$n1"
sleep 1.5; d attacked-2s
sleep 3; d attacked-5s
grep -E '^PRACTICE t=.* far orc |^BOW t=.* (hit|head)' "$L" | tail -6 | cut -c1-220
t "$REST_R"; t "$SLOT_L"; sleep 0.5; ex "the bow put back" '^BOW .* exit: ' 3 "btn L squeeze 0"
w "practice remove" 1
echo "steps done $(date +%T)"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (f18-steps.txt)"
