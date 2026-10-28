#!/bin/bash
# menu20-drive.sh - build 20's steady menus and the pointer's lock in one headless boot (docs/re-xr-pipeline.md 80,
# handoff/HANDOFF-menus.md "build 20"): the wrist panel over a shaking hand, the ray locked on its parts through a shaking
# aim, presses under the shake, a fingertip's press (touch unchanged), the lock between two buttons, the game's pause
# menu's held point. Boot (sandbox off) with scripts/headset-build20.env's switches and a COPY of a settings file:
#   cp ~/.local/share/somvr/settings.conf /tmp/somvr-menu20/settings.conf
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build20.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_SETTINGS=/tmp/somvr-menu20/settings.conf"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (M20_WT = a word of the launch command, default this worktree's name). The layer's
# test hands: `hand L|R shake CM DEG` (a tremor over the scripted pose), `hand R aim panel|board U V D` (the ray, not the
# fingertip). The numbers come from the layer's lines: `xr: vrmenu steady:` every 2 s while the panel is open (the
# corners' speed of the pose shown and of the hand's pose = build 19's panel; the lock's changes, the bare hit's and
# build 19's hover's with the same hands), `xr: vrmenu: the press on the locked ...` (and what build 19 would have
# pressed), `xr: pointer held:` (the game's menu: the point's spread and the bare ray's).
#   0. the start: the layer's build 20 lines (steady, the lock, the held point)
#   1. the panel over a shaking hand (0.5 cm, 1.5 deg): its corners at most a tenth of the hand's pose's speed
#   2. the ray on the Turn row with both hands shaking (the aim 1.5 deg): the lock holds, build 19's hover flips
#   3. four presses on Vignette's < and > under the shake: each on the aimed part
#   4. a fingertip's press (no shake): the click as before, no lock line
#   5. the ray between Recentre and Report (on no part): the nearest locks, the press goes there; the ray aimed away
#      from the open panel (step 1's end) not drawn (SOMVR_XR_VM_RAY=near)
#   6. the pause menu's board, the aim shaking (1.0 deg): the point's spread at most a third of the bare ray's
# Dumps: RUN/m20-steps.txt (scripts/xr_compose.py RUN -f ...). PASS/FAIL per check; quits at once when the user goes
# into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WTD=$(cd "$HERE/.." && pwd -P)
WT=${M20_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
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
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
t() { local x; userin && bail "the user is in VR"; { for x in "$@"; do echo "$x"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
k() { echo "key $1" >> "$R/cmd"; echo "$(date +%T) key $1"; }
cnt() { grep -cE "$1" "$2"; }
NP=0; NF=0
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/m20-steps.txt"; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
trig() { t "btn R trigger 1"; sleep 0.35; t "btn R trigger 0"; sleep 0.5; }
opens() { grep -c 'vrmenu: opened by' "$X" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN (gesture x4, long press)"; return 1; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
# the numbers of the last `xr: vrmenu steady:` line: shown hand most most_hand lock bare b19
steady() { grep 'xr: vrmenu steady: ' "$X" | tail -1 | sed -E 's/.*speed ([0-9.]+) mm\/s rms \(the hand.s pose: ([0-9.]+)\), most ([0-9.]+) mm in a frame \(([0-9.]+)\); the lock changed ([0-9]+) times \(the bare hit: ([0-9]+), build 19.s hover: ([0-9]+)\).*/\1 \2 \3 \4 \5 \6 \7/'; }
le() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a <= b) }'; }
echo "== 0. the start"
grep -E 'xr: vrmenu: build 20|xr: vrmenu: (steady|lock|held point):|xr: pointer: held point' "$X" | cut -c1-260
grep -q 'xr: vrmenu: build 20: the panel held steady; the pointer locks on its parts (SOMVR_XR_PTR_SNAP); its point held' "$X" &&
  pass "the layer: the panel held steady, the lock, the held point" || fail "the layer's build 20 line"
grep -q 'xr: vrmenu: SOMVR_XR_\|xr: pointer: SOMVR_XR_PTR_HOLD=.* is not' "$X" && fail "a switch not understood" || pass "every switch understood"
AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
[ -n "$AV" ] || bail "no avail in the layer's log"
gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
  "$WTD/mod/somvr/somvr_font.c" -lm || bail "vm_uv did not build"
"$R/vm_uv" "$AV" > "$R/m20-uv.tsv"
uv() { awk -F'\t' -v l="$1" -v p="$2" -v g="$3" '$1 == l && $2 == p && $3 == g && !e { e = $4 " " $5 }
  $1 == l && $2 == p && $3 == "any" && !a { a = $4 " " $5 } END { print e ? e : a }' "$R/m20-uv.tsv"; }
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$X" || { t "btn R a 1"; t "btn R a 0"; }
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 1
echo "== 1. the panel over a shaking hand"
t "hand R off"
menu_open || bail "the menu did not open"
sleep 2.5
d still
t "hand L shake 0.5 1.5"; sleep 6.5
S=$(steady); echo "steady: $S"
set -- $S
[ $# -eq 7 ] && le 20 "$2" && le "$1" "$(awk -v h="$2" 'BEGIN { print 0.1 * h }')" &&
  pass "the panel held steady: its corners $1 mm/s rms, the hand's pose (build 19's panel) $2 mm/s; most $3 mm in a frame ($4)" ||
  fail "the steady panel: $S"
d steady-shake
sleep 0.3
d steady-shake-b
# the ray off the panel (SOMVR_XR_VM_RAY=near): the right hand aimed ahead, away from the panel: no line out of it
t "hand R head 22 -40 15 0 -60 0"; sleep 1.5
d ray-away
echo "== 2. the ray near the Turn row's bottom edge, both hands shaking"
UT=$(uv "Turn" row settings); [ -n "$UT" ] || bail "no Turn row"
# 4 texels above the row's bottom edge (the Settings page's rows: 33 px): the aim on Turn, the shake across the edge
set -- $UT; UT="$1 $(awk -v v="$2" 'BEGIN { printf "%.4f", v + 12.5 / 768 }')"
t "hand R aim panel $UT 30"; sleep 1.5
t "hand R shake 0.3 1.5"; sleep 5.5
S=$(steady); echo "steady: $S"
set -- $S
[ $# -eq 7 ] && [ "$5" -eq 0 ] && [ "$7" -ge 3 ] &&
  pass "the lock held through the shake: 0 changes (the bare hit $6, build 19's hover $7)" || fail "the lock: $S"
d lock-shake
echo "== 3. four presses under the shake, 5 texels inside the boxes' outer edges"
# the < box's left edge and the > box's right edge (48 px boxes): 5 texels in = 19 texels from their middles
set -- $(uv "Vignette" less settings); UL="$(awk -v u="$1" 'BEGIN { printf "%.4f", u - 19 / 512 }') $2"
set -- $(uv "Vignette" more settings); UM="$(awk -v u="$1" 'BEGIN { printf "%.4f", u + 19 / 512 }') $2"
n0=$(grep -c 'xr: vrmenu: the press on the locked' "$X"); ok=0
for p in "$UL <" "$UM >" "$UL <" "$UM >"; do
  set -- $p
  t "hand R shake 0 0"; t "hand R aim panel $1 $2 30"; sleep 1.0
  t "hand R shake 0.3 1.5"; sleep 1.5
  trig
  last=$(grep 'xr: vrmenu: the press on the locked' "$X" | tail -1)
  echo "  $(echo "$last" | grep -o 'the press.*')"
  echo "$last" | grep -q "the press on the locked Vignette $3 " && ok=$((ok + 1))
done
[ $ok -eq 4 ] && [ "$(grep -c 'xr: vrmenu: the press on the locked' "$X")" -eq $((n0 + 4)) ] &&
  pass "four presses under the shake, each on the aimed part ($(grep 'xr: vrmenu: click Vignette' "$X" | tail -4 | grep -o 'changed [^(]*' | tr '\n' '|'))" ||
  fail "presses under the shake: $ok of 4 on the aimed part"
echo "  build 19 would have pressed: $(grep 'xr: vrmenu: the press on the locked' "$X" | tail -4 | grep -o "build 19's: [^;]*" | tr '\n' '|')"
echo "== 4. a fingertip's press: as before"
t "hand R shake 0 0" "hand L shake 0 0"; sleep 1
echo "  build 19's hover under the presses' shakes: $(grep 'xr: vrmenu steady: ' "$X" | tail -4 | grep -o "build 19.s hover: [0-9]*" | tr '\n' ' ')"
n0=$(grep -c 'xr: vrmenu: the press on the locked' "$X"); c0=$(grep -c 'xr: vrmenu: click Haptics <: ' "$X")
press $(uv "Haptics" less settings); sleep 0.4
c1=$(grep -c 'xr: vrmenu: click Haptics <: ' "$X")
[ "$c1" -gt "$c0" ] && [ "$(grep -c 'xr: vrmenu: the press on the locked' "$X")" -eq "$n0" ] &&
  pass "the fingertip's press: $(grep 'xr: vrmenu: click Haptics <: ' "$X" | tail -1 | grep -o 'Haptics <: [^(]*'), no lock line" ||
  fail "the fingertip's press: clicks $c0 -> $c1"
grep 'xr: vrmenu: click Haptics <: ' "$X" | tail -1 | grep -q changed && { press $(uv "Haptics" more settings); sleep 0.4; }
echo "== 5. between Recentre and Report"
t "hand R aim panel 0.3398 0.9271 30"; sleep 1.5
d gap
trig
last=$(grep 'xr: vrmenu: the press on the locked' "$X" | tail -1); echo "  $(echo "$last" | grep -o 'the press.*')"
echo "$last" | grep -qE 'the press on the locked (Recentre view|Report) button' &&
  pass "between two buttons: the nearest locked, the press went there ($(echo "$last" | grep -o 'the bare hit: [^;]*'))" ||
  fail "between two buttons: $last"
H=$(grep 'xr: vrmenu steady: ' "$X" | tail -1 | grep -o 'the ray hidden off it [0-9]*' | grep -o '[0-9]*$')
[ -n "$H" ] && [ "$H" -gt 0 ] && pass "the ray aimed away from the open panel: not drawn ($H frames)" ||
  fail "the ray away: $(grep 'xr: vrmenu steady: ' "$X" | tail -1 | cut -c1-200)"
menu_shut
echo "== 6. the pause menu's board"
k Escape; sleep 3
t "hand R aim board 0.5 0.55 100"; sleep 1.5
t "hand R shake 0.3 1.0"; sleep 4.5
d board-shake
P=$(grep 'xr: pointer held: ' "$X" | tail -1); echo "  $(echo "$P" | grep -o 'the point moved.*')"
set -- $(echo "$P" | sed -E 's/.*moved over ([0-9.]+) x ([0-9.]+) uv .*the bare ray: ([0-9.]+) x ([0-9.]+);.*/\1 \2 \3 \4/')
if [ $# -eq 4 ]; then
  a=$(awk -v a="$1" -v b="$2" 'BEGIN { print (a + 0 > b + 0 ? a : b) }'); r=$(awk -v a="$3" -v b="$4" 'BEGIN { print (a + 0 > b + 0 ? a : b) }')
  le 0.003 "$r" && le "$a" "$(awk -v r="$r" 'BEGIN { print r / 3 }')" &&
    pass "the board's point held: over $a uv (the bare ray $r)" || fail "the board's point: $a uv (the bare ray $r)"
else
  fail "no 'pointer held' line on the board"
fi
t "hand R shake 0 0" "hand R off"; sleep 0.5
k Escape; sleep 3
echo "steps done $(date +%T): PASS $NP, FAIL $NF"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
grep -c 'xr: vrmenu steady: ' "$X" | sed 's/^/steady lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (m20-steps.txt), compose: python3 scripts/xr_compose.py $R -f FRAMES"
