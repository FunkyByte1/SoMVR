#!/bin/bash
# sword16-drive.sh - build 16's sword (docs/re-xr-pipeline.md 52, handoff/HANDOFF-feel.md "build 16"), headless at the
# practice arrival: the VR menu's Sword weight row, the fist on the handle (SOMVR_SWORD_GRIP_CM as booted), the weight's
# wiggles and cuts, the practice orc's attacks on the guard (the clank) and lunges (the hits). Boot (sandbox off) with
# scripts/headset-build16.env's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build16.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (S16_WT = a word of the launch command, default this worktree's name). The menu and
# the grip use the layer's test controllers (RUN/xrtest.txt: `hand L|R head X Y Z PITCH YAW ROLL [s S]`, `hand R touch
# panel U V D`, `btn ...`); the weight uses the preload's test hands (`hands test`, `hands swing`, `hands wiggle`) with
# the layer's buttons. Dumps: RUN/s16-steps.txt names each (compose with scripts/xr_compose.py RUN). S16_STEPS = any of
# menu grip wiggle cuts orc (default all, in that order). PASS/FAIL lines; quits at the end, at once if the user goes
# into VR. The Sword weight row with build 16's avail (19 rows of 32 px): < 0.547 0.6458, > 0.922 0.6458.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${S16_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${S16_STEPS:-menu grip wiggle cuts orc}
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
fastwait() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 20))); do
  [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.05; done; userin && bail "the user is in VR"; return 1; }
check() { if [ "$(cnt "$2")" -gt "$3" ]; then echo "$(date +%T) PASS $1"; else echo "$(date +%T) FAIL $1 (no '$2')"; fi; }
# d NAME: a stereo dump now (both eyes + the layer's quads), its frame noted
d() { T_S=0.1 t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/s16-steps.txt"; }
# dmid NAME PATTERN: in the background, a dump as soon as PATTERN's next line shows (a swing's frame)
dmid() { ( fastwait "$2" 12 && { printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/s16-steps.txt"; } ) & }
wig_end() { grep -E '^BODY test wiggle R end: ' "$L" | tail -1 | cut -c1-260; }
REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
SLOT_R="hand R head 10 -10 -15 0 0 0"
OUT_R="hand R head 30 -25 40 0 0 0"
held() { grep -E '^SLOTS t=.* sword: (done|DENIED|the game put it away: kept|our keep undone)' "$L" | tail -1 |
         grep -qE 'now in the hand|kept in the hand'; }
draw_sword() { t "$SLOT_R"; sleep 0.4; t "btn R squeeze 1"; sleep 0.3; t "$OUT_R"; t "btn R squeeze 0"; sleep 0.5; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
down() { grep -q "^PRACTICE t=.* orc is down" "$L"; }
grep -E '^CONFIG smooth' "$L" | cut -c1-400
# 0. the controls card away (the right grip, else A), the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
t "$REST_L" "$REST_R"

if has menu; then
  echo "== the VR menu's Sword weight row (heavy as booted): < = light, > = heavy again"
  press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
            t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
  opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
  menu_open() { local o=$(opens) k
    for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
      [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
      t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
    t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
    echo "NOT OPEN"; }
  A0=$(cnt 'VRMENU apply SWORD_WEIGHT=light from the menu'); A1=$(cnt 'VRMENU apply SWORD_WEIGHT=heavy from the menu')
  menu_open; d menu-heavy
  press 0.547 0.6458; sleep 1
  check "Sword weight: Light by its <" 'VRMENU apply SWORD_WEIGHT=light from the menu' "$A0"
  check "the level live" 'SWORDWT t=.* weight heavy -> light \(the VR menu\)' 0
  d menu-light
  press 0.922 0.6458; sleep 1
  check "Sword weight: Heavy again by its >" 'VRMENU apply SWORD_WEIGHT=heavy from the menu' "$A1"
  t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "$REST_L" "$REST_R"; sleep 1.5
fi

if has grip; then
  echo "== the fist on the handle (SOMVR_SWORD_GRIP_CM as booted): one hand, the two hands' grab"
  held || draw_sword
  held && echo "$(date +%T) the sword is in the hand" || echo "$(date +%T) FAIL no sword in the hand"
  t "btn L trigger 0" "$OUT_R" "$REST_L"; sleep 1; d grip-one; m s16-grip-one 0.2
  n=$(cnt '^TWOHAND t=.* grab'); t "$OUT_R" "hand L head 30 -27 30 0 0 0"; sleep 0.5; t "btn L trigger 1"; sleep 1
  [ "$(cnt '^TWOHAND t=.* grab')" -gt "$n" ] && echo "$(date +%T) PASS two hands: $(grep -E '^TWOHAND t=.* grab' "$L" | tail -1 | grep -oE 'the left on the hilt [0-9.]+ cm behind')" ||
    echo "$(date +%T) FAIL no TWOHAND grab"
  d grip-two; m s16-grip-two 0.2; t "btn L trigger 0" "$REST_L"; sleep 0.8
  grep -E '^CONFIG body' "$L" | head -1 | grep -oE 'grip [0-9.]+ cm[^;]*' | head -1
fi

HOLD="hands test R 30 -25 40 0 0 0"        # the sword held out ahead (the blade straight ahead), still
if has wiggle; then
  echo "== the weight: wiggles of the wrist (heavy as booted)"
  held || draw_sword
  w "hands test L -25 -35 30 0 0 0"; w "$HOLD" 1.5
  d still; m s16-still 0.2
  WIGS=("10 5" "15 4" "20 3" "25 2"); [ -n "$S16_WIGGLES" ] && IFS=, read -ra WIGS <<< "$S16_WIGGLES"  # "DEG HZ,DEG HZ"
  for wg in "${WIGS[@]}"; do
    set -- $wg
    n=$(cnt '^BODY test wiggle R end: ')
    w "hands wiggle R $1 $2 3" 0.2; waitfor '^BODY test wiggle R: ' 4; sleep 1.3; d "wiggle-$1-$2"
    waitfor '^BODY test wiggle R end: ' 6; echo "$(date +%T) WIGGLE +-$1 deg $2 Hz: $(wig_end)"
  done
  echo "== two hands on the hilt: a 3 Hz wiggle near 1:1"
  n=$(cnt '^TWOHAND t=.* grab'); w "hands test L 30 -27 30 0 0 0" 0.5; t "btn L trigger 1"; sleep 1
  [ "$(cnt '^TWOHAND t=.* grab')" -gt "$n" ] && echo "$(date +%T) PASS two hands (test hands)" || echo "$(date +%T) FAIL no TWOHAND grab (test hands)"
  w "hands wiggle R 20 3 3" 0.2; waitfor '^BODY test wiggle R: ' 4; sleep 1.3; d wiggle-two
  waitfor '^BODY test wiggle R end: ' 6; echo "$(date +%T) WIGGLE two hands +-20 deg 3 Hz: $(wig_end)"
  t "btn L trigger 0"; w "hands test L -25 -35 30 0 0 0" 1
fi

if has cuts; then
  echo "== cuts: slow (1.2 s) and fast (0.25 s), right to left; pictures mid-cut"
  held || draw_sword
  CUT_A="hands test R 45 -15 35 0 -70 0"
  for ms in 1200 250; do
    w "$CUT_A" 1.5
    dmid "cut-$ms" "^BODY test swing R frame $([ $ms -gt 600 ] && echo 8 || echo 3) "
    w "hands swing R -30 -25 45 0 70 0 $ms" $(( ms / 1000 + 2 ))
    wait
    grep -E '^BODY test swing R end: ' "$L" | tail -1 | cut -c1-120
  done
  waitfor '^SWORDWT t=' 12; grep -E '^SWORDWT t=.* last 10 s' "$L" | tail -1 | cut -c1-330
fi

if has orc; then
  echo "== the practice infantry: the guard (the clank), then lunges (the hits)"
  held || draw_sword
  GUARD="hands test R 42 -12 35 0 90 0"
  LUNGE_A="hands test R 40 -8 35 0 -90 0"; LUNGE="hands swing R -20 -18 62 0 90 0 300"; LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
  C0=$(cnt '^DEFENCE clank '); H0=$(cnt '^COMBAT hit '); P0=$(cnt '^HAP R .*src=combat')
  w "practice spawn infantry" 0.5
  waitfor '^THREAT start .* impact=0\.[0-9]' 20
  # the guard where the blows come (build 16: the 90 cm space keeps the infantry's blows lower and further out than
  # build 15b's face guards; run 060138: 64-77 cm off them): the test guard learns each strike's path from the head
  # toward the orc (`fight guard frame head`, the blade behind the hip meanwhile), then puts the blade on it at the
  # reaction time (`fight guard path`), as a player who watches the blow - until two clanks
  w "fight guard frame head" 0.3; w "hands test R 22 -35 -30 0 180 0" 0.5; w "fight guard learn" 0.3
  nl=0; for k in $(seq 1 6); do face; fastwait '^TGUARD .*learnt' 12 && nl=$((nl + 1)); [ $nl -ge 4 ] && break; done
  w "fight guard path" 0.3
  ng=0
  for k in $(seq 1 8); do
    face
    if fastwait '^DEFENCE (clank|parry|block|parry-block|hit|dodge) ' 12 && grep -E '^DEFENCE (clank|parry|block|parry-block|hit|dodge) ' "$L" | tail -1 | grep -q '^DEFENCE clank '; then
      printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2; ng=$((ng + 1))
      echo "clank-$ng $(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$') (the path guard)" | tee -a "$R/s16-steps.txt"
      [ $ng -ge 2 ] && break
    else echo "$(date +%T) path guard: $(grep -E '^DEFENCE (clank|parry|block|parry-block|hit|dodge) ' "$L" | tail -1 | cut -c1-150)"; fi
  done
  w "fight guard off" 0.3
  [ "$(cnt '^DEFENCE clank ')" -gt "$C0" ] && echo "$(date +%T) PASS the clank ($(( $(cnt '^DEFENCE clank ') - C0 )))" || echo "$(date +%T) FAIL no clank"
  grep -E '^DEFENCE clank ' "$L" | tail -1 | cut -c1-200
  for k in $(seq 1 10); do
    down && break; face
    [ $k -le 2 ] && dmid "lunge-$k" '^BODY test swing R frame 3 '
    w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; wait
    [ "$(cnt '^COMBAT hit ')" -ge $((H0 + 4)) ] && break
  done
  HN=$(( $(cnt '^COMBAT hit ') - H0 ))
  [ "$HN" -ge 1 ] && echo "$(date +%T) PASS hits $HN, combat pulses $(( $(cnt '^HAP R .*src=combat') - P0 ))" || echo "$(date +%T) FAIL no hits"
  grep -E '^COMBAT hit ' "$L" | tail -2 | cut -c1-200
  waitfor '^SWORDWT t=' 12; grep -E '^SWORDWT t=.* last 10 s' "$L" | tail -2 | cut -c1-330
fi
echo "steps done $(date +%T)"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
