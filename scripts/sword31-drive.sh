#!/bin/bash
# sword31-drive.sh - build 31's sword (docs/re-xr-pipeline.md 134, handoff/HANDOFF-combat.md "build 31 sword"): one
# headless boot with scripts/headset-build26.env (SOMVR_SWORD_PHYS=on, SOMVR_SWORD_FLESH=on, SOMVR_SWORD_WEIGHT=heavy,
# SOMVR_COMBAT_BITE=on), the test hands (SOMVR_HANDS_TEST=1) and the layer's test file (SOMVR_XR_TEST=@RUN@/xrtest.txt).
# S31_STEPS = any of (in this order; default all):
#   air    no orc: a flick of the wrist (100 deg in 150 ms, the hand still), a committed cut of the arm (140 deg, 450 ms),
#          a slow one (1.5 s): the `SWORDPHYS swing` lines (the hand's and the sword's turn rates, the lag, the tip)
#   two    both hands on the hilt (TWOHAND, sword18's grab): the same flick
#   cut    an infantry: cuts across its chest (S31_CUT_MS, 500)
#          until a wound opens; `fight bite dumps 1` marks the landed one for the dumps 0.12 s and 0.5 s after
#   weak   the same orc: a slow cut (1500 ms) that stops in its body, held, then drawn back out
#   stab   the same orc: a thrust along the blade into its chest (250 ms) that stays in; held 1.5 s (dumps); drawn back
#          slowly along the blade (900 ms): out
#   tier   (build 31b) 4 committed 300 ms cuts, then 4 wrist flicks (150 ms, the fist still) into a fighting orc: the tiers
#   exec   (build 31b) the charge full (`verbs brand charge`), one committed 300 ms cut: the execution (SOMVR_KILLS_EXEC)
#   force32 (build 32) a held blade the orc is pressed into, wrist flicks, committed cuts, cuts at 165 cm (a brush)
#   down32 (build 32) `abil kd`, the head 45 cm down (crouched), cuts down at the downed orc: hits, not stealth touches
#   parry  parry18-drive.sh's `inf` step on this boot (P18_RUN): the test guard's parries with the weighted sword
# Dumps: RUN/s31-steps.txt. Quits at the end, at once if the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${S31_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${S31_STEPS:-air two cut weak stab tier exec force32 down32 parry}
CUT_MS=${S31_CUT_MS:-500}
HERE=$(cd "$(dirname "$0")" && pwd)
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
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
d() { T_S=0.1 t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/s31-steps.txt"; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(awk -v y="$y" 'BEGIN { y += 0; printf "%d", (y + (y < 0 ? -15 : 15)) / 30 }')
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
BEHIND="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
spawn() { w "practice spawn $1" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5; }
lines() { awk -v a="$1" 'NR > a' "$L" | grep -E "$2" | cut -c1-"${3:-300}"; }
# a swing in the air: its SWORDPHYS swing lines (the peak lag, the hand's and the sword's peak rates, the tip)
air() { local nm=$1 a=$2 b=$3 ms=$4 n0; w "$a" 1.0; n0=$(wc -l < "$L"); w "$b" "$(awk -v m="$ms" 'BEGIN { print m / 1000 + 1.0 }')"
  lines "$n0" '^SWORDPHYS t=.* swing:' | awk -v nm="$nm" '{ match($0, /hand turns [0-9.]+/); h = substr($0, RSTART + 11, RLENGTH - 11)
      match($0, /the sword [0-9.]+;/); s = substr($0, RSTART + 10, RLENGTH - 11); match($0, /the sword [0-9.]+ deg/); d = substr($0, RSTART + 10, RLENGTH - 14)
      match($0, /its tip [0-9.]+/); tp = substr($0, RSTART + 8, RLENGTH - 8)
    if (h + 0 > hm) hm = h + 0; if (s + 0 > sm) sm = s + 0; if (d + 0 > dm) dm = d + 0; if (tp + 0 > tm) tm = tp + 0; n++ }
    END { printf "%s AIR %s: %d frames, the hand up to %.1f rad/s, the sword %.1f; the sword up to %.1f deg off the hand, its tip up to %.1f m/s\n", strftime("%T"), nm, n, hm, sm, dm, tm }'; }
hit_it() { local k h0; h0=$(cnt '^SWORDPHYS t=.* wound open'); for k in $(seq 1 "${2:-8}"); do
    [ "$(cnt '^SWORDPHYS t=.* wound open')" -gt "$h0" ] && return 0
    face; w "fight health 125" 0.3; w "stick 0 1 0 0.40" 0.5; $1
  done; [ "$(cnt '^SWORDPHYS t=.* wound open')" -gt "$h0" ]; }
# (at the orc's chest: its neck is ~58 cm over its hip, about Talion's eyes - boot 20261102-121749's stab at -18 cm passed
# its neck by 1.2 cm; the edge leading: the roll 90, parry18's EDGE)
cut_swing() { w "hands test R 40 -35 35 0 -90 90" 0.6; w "hands swing R -20 -42 62 0 90 90 $CUT_MS" "$(awk -v m="$CUT_MS" 'BEGIN { print m / 1000 + 0.4 }')"; }
# the weak cut ends with the blade still across the orc's middle (it can stop in it); the stab starts with the blade out of
# combat's 35 cm capsule (boot 20261102-122858: started inside it, the thrust was no new stroke - glances only): a step
# back, the hand drawn in, then 55 cm along the blade in 300 ms
weak_swing() { w "hands test R 40 -35 35 0 -90 90" 0.6; w "hands swing R 10 -40 45 0 -10 90 1500" 1.8; }
stab_in() { w "stick 0 -1 0 0.60" 0.9; w "hands test R 12 -40 -28 0 0 90" 0.6; w "hands swing R 12 -40 25 0 0 90 300" 0.8; }

# 0. the controls card away, the left hand low, the sword drawn
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
w "hands test L -25 -45 10 0 0 0"; w "$REST" 1.5
drawn || draw_slot
sleep 1
drawn && echo "$(date +%T) the sword is in the hand" || echo "$(date +%T) FAIL no sword in the hand"
grep -E '^CONFIG swordphys' "$L" | cut -c1-600
grep -E '^CONFIG hitfeel' "$L" | cut -c1-200
if has air; then
  echo "== the weight in the air"
  air "flick 100 deg / 150 ms (the wrist)" "hands test R 25 -15 40 0 -50 0" "hands swing R 25 -15 40 0 50 0 150" 150
  air "committed arm cut 140 deg / 450 ms" "hands test R 45 -10 25 0 -70 0" "hands swing R -25 -15 55 0 70 0 450" 450
  air "slow cut 140 deg / 1500 ms" "hands test R 45 -10 25 0 -70 0" "hands swing R -25 -15 55 0 70 0 1500" 1500
  w "$REST" 1
fi
if has two; then
  echo "== both hands on the hilt"
  w "hands test R 0 -25 35 0 -90 0" 0.8
  n=$(cnt '^TWOHAND t=.* grab'); w "hands test L -23 -25 35 0 -90 0" 0.6; w "hands squeeze L 1" 1.2
  SX=-1
  if [ "$(cnt '^TWOHAND t=.* grab')" -le "$n" ]; then
    w "hands squeeze L 0" 0.6; SX=1; w "hands test L 23 -25 35 0 -90 0" 0.6; w "hands squeeze L 1" 1.2; fi
  [ "$(cnt '^TWOHAND t=.* grab')" -gt "$n" ] && echo "$(date +%T) PASS two hands" || echo "$(date +%T) FAIL no TWOHAND grab"
  # the same flick, both hands (the left goes with it)
  n0=$(wc -l < "$L"); w "hands swing R 0 -25 35 0 10 0 150" 1.2
  lines "$n0" '^SWORDPHYS t=.* swing:' | tail -4
  w "hands squeeze L 0" 0.8
  w "hands test L -25 -45 10 0 0 0"; w "$REST" 1
fi
if has cut || has weak || has stab; then spawn infantry; w "$BEHIND" 0.5; fi
if has cut; then
  echo "== cuts across its chest (${CUT_MS} ms) until a wound opens"
  n0=$(wc -l < "$L"); w "fight flesh dumps 2" 0.3
  hit_it cut_swing 8 && echo "$(date +%T) PASS a wound opened" || echo "$(date +%T) FAIL no wound"
  sleep 2.5
  lines "$n0" '^(COMBAT hit tier|SWORDPHYS t=.* (hit|wound|blood))' 330 | tail -10
  echo "cut $(grep -E '^BITE t=.* dump mark SWORDPHYS' "$L" | grep -oE 'frame [0-9]+' | cut -d' ' -f2 | tr '\n' ' ')" | tee -a "$R/s31-steps.txt"
  w "$BEHIND" 0.8
fi
if has weak; then
  echo "== a slow cut that stops in the body, then drawn back"
  n0=$(wc -l < "$L")
  if hit_it weak_swing 6; then
    sleep 0.3; d weak-held
    w "hands swing R 40 -35 15 0 -90 90 900" 1.5
  else echo "$(date +%T) no wound from the weak cut"; fi
  lines "$n0" '^(COMBAT hit tier|SWORDPHYS t=.* (hit|wound|blood))' 330 | tail -10
  w "$BEHIND" 0.8
fi
if has stab; then
  echo "== a thrust into its chest that stays in; drawn back out along the blade"
  n0=$(wc -l < "$L"); w "fight flesh dumps 1" 0.3
  if hit_it stab_in 6; then
    sleep 0.8; d stab-held-1; sleep 0.6; d stab-held-2
    w "hands swing R 12 -40 10 0 0 90 900" 1.8
  else echo "$(date +%T) no wound from the stab"; fi
  lines "$n0" '^(COMBAT hit tier|SWORDPHYS t=.* (hit|wound|blood))' 330 | tail -12
  echo "stab $(grep -o 'dump layers frame [0-9]*: eye 0 pose' "$R/xr-layer.log" | tail -4 | grep -o 'frame [0-9]*' | cut -d' ' -f2 | tr '\n' ' ')" | tee -a "$R/s31-steps.txt"
  w "$BEHIND" 0.8
fi
if has tier; then
  echo "== build 31b's tier: committed 300 ms cuts and 150 ms wrist flicks into a fighting orc (the tier by the hand's swing)"
  w "practice remove" 2; w "practice spawn infantry" 4; w "execute face" 1; w "fight guard path" 0.4
  n0=$(wc -l < "$L")
  for k in 1 2 3 4; do face; w "fight health 125" 0.3; w "abil goto 110" 0.5; w "execute face" 0.4
    w "hands test R 55 -8 30 0 -90 90" 0.4; w "hands swing R -35 -18 62 0 90 90 300" 1.0; w "$BEHIND" 0.6; done
  echo "committed: $(lines "$n0" '^COMBAT hit tier=' 400 | grep -oE 'tier=[a-z]+' | sort | uniq -c | tr '\n' ' ')"
  lines "$n0" '^COMBAT hit tier=' 400 | grep -oE 'tier=[a-z]+.{0,12}|speed=[0-9.]+|stroke=[0-9]+ contact=[0-9.]+ hand=[0-9.]+ k=[0-9.]+( \(by the hand.s swing\))? arm=[0-9]+' | paste - - - | head -6
  n0=$(wc -l < "$L")
  for k in 1 2 3 4; do face; w "fight health 125" 0.3; w "abil goto 110" 0.5; w "execute face" 0.4
    w "hands test R 20 -15 25 0 -55 90" 0.6; w "hands swing R 20 -15 25 0 55 90 150" 1.0; w "$BEHIND" 0.6; done
  echo "flicks: $(lines "$n0" '^COMBAT hit tier=' 400 | grep -oE 'tier=[a-z]+' | sort | uniq -c | tr '\n' ' ')"
  lines "$n0" '^COMBAT hit tier=' 400 | grep -oE 'tier=[a-z]+.{0,12}|speed=[0-9.]+|stroke=[0-9]+ contact=[0-9.]+ hand=[0-9.]+ k=[0-9.]+( \(by the hand.s swing\))? arm=[0-9]+' | paste - - - | head -6
fi
if has exec; then
  echo "== the execution: the charge full (verbs brand charge), one committed 300 ms cut"
  w "practice remove" 2; w "practice spawn infantry" 4; w "execute face" 1; w "fight guard path" 0.4; sleep 2
  w "verbs brand charge" 0.6; w "abil goto 110" 0.5; w "execute face" 0.4
  n0=$(wc -l < "$L"); w "hands test R 55 -8 30 0 -90 90" 0.4; w "hands swing R -35 -18 62 0 90 90 300" 0.1; sleep 4; w "$REST" 0.4
  lines "$n0" '^(COMBAT hit tier|EXECUTE (kill|t=)|KILLS (start|play|end|exec)|SWORDPHYS t=.* wound (open|end))' 300 | head -14
fi
hits() { lines "$1" '^COMBAT (hit tier|brush)' 400 | grep -oE '^COMBAT (hit tier=[a-z]+|brush)|blow=[0-9.na-]+J|fist=[0-9.]+|health [0-9.]+->[0-9.]+' | paste -sd' ' | sed 's/COMBAT /\n  /g'; }
if has force32; then
  echo "== build 32: hits by force - a held blade the orc walks into, wrist flicks, committed cuts, a brush at the capsule's edge"
  w "practice remove" 2; w "practice spawn infantry" 4; w "execute face" 1
  n0=$(wc -l < "$L"); w "hands test R 20 -35 30 0 0 90" 0.6
  for k in 1 2 3; do face; w "abil goto 120" 0.4; w "stick 0 1 0 0.6" 1.4; done
  echo "held blade (the orc pressed into it): hits $(lines "$n0" '^COMBAT hit tier=' 400 | wc -l), brushes $(lines "$n0" '^COMBAT brush' 400 | wc -l)"; hits "$n0"
  n0=$(wc -l < "$L")
  for k in 1 2 3 4; do face; w "fight health 125" 0.3; w "abil goto 110" 0.5; w "execute face" 0.4
    w "hands test R 20 -35 25 0 -55 90" 0.6; w "hands swing R 20 -35 25 0 55 90 150" 1.0; w "$BEHIND" 0.6; done
  echo "wrist flicks: hits $(lines "$n0" '^COMBAT hit tier=' 400 | wc -l)"; hits "$n0"
  n0=$(wc -l < "$L")
  for k in 1 2 3 4; do face; w "fight health 125" 0.3; w "abil goto 110" 0.5; w "execute face" 0.4
    w "hands test R 55 -35 30 0 -90 90" 0.4; w "hands swing R -35 -42 62 0 90 90 300" 1.0; w "$BEHIND" 0.6; done
  echo "committed cuts: hits $(lines "$n0" '^COMBAT hit tier=' 400 | wc -l)"; hits "$n0"
  n0=$(wc -l < "$L")
  for k in 1 2 3; do face; w "fight health 125" 0.3; w "abil goto 165" 0.5; w "execute face" 0.4
    w "hands test R 55 -35 30 0 -90 90" 0.4; w "hands swing R -35 -42 62 0 90 90 300" 1.0; w "$BEHIND" 0.6; done
  echo "cuts at 165 cm (the tip at combat's capsule, short of the body): hits $(lines "$n0" '^COMBAT hit tier=' 400 | wc -l), brushes $(lines "$n0" '^COMBAT brush' 400 | wc -l)"; hits "$n0"
fi
if has down32; then
  echo "== build 32: a knocked-down orc, the player crouched over it (the head 45 cm down), cuts down at it"
  # (kills31's ground staging: the knockdown's slide over after 2.4 s, Talion 70 cm from it, the head down; crouched)
  w "practice remove" 2; w "practice spawn infantry" 4; w "fight guard path" 0.4; w "execute face" 1; w "abil kd" 0.4; sleep 2.4
  n1=$(wc -l < "$L"); w "abil goto 70" 0.8
  by=$(lines "$n1" '^ABIL t=.* goto:' 400 | tail -1 | sed -nE 's/.* ([-+]?[0-9]+) deg right of the head.*/\1/p')
  t "head 0 0 0 yaw ${by:-0} pitch -35 s 0.3"; w "crouch hold 9000" 1.0; echo "the orc ${by:-?} deg right of the head"
  n0=$(wc -l < "$L")
  for k in 1 2 3; do w "hands test R 12 -30 55 -90 0 0" 0.5; w "hands swing R 12 -85 58 -90 0 0 220" 1.0; done
  for k in 1 2; do w "hands test R 40 -35 40 -40 -60 90" 0.5; w "hands swing R -20 -95 50 40 60 90 300" 1.0; done
  w "crouch stand" 0.5
  echo "on the downed orc: hits $(lines "$n0" '^COMBAT hit tier=' 400 | wc -l), stealth touches $(lines "$n0" '^STEALTH t=.* touch' 400 | wc -l)"; hits "$n0"
  lines "$n0" "^(ABIL t=.*KD|STEALTH t=.* (touch|air|downed))|^CROUCH" 300 | head -5
  t "head 0 0 0 s 0.4"; sleep 1
fi
if has parry; then
  echo "== parry18's inf step with the weighted sword"
  P18_RUN=$R P18_STEPS=inf P18_LEARN_N=${S31_LEARN_N:-3} P18_PATH_N=${S31_PATH_N:-4} P18_INF_S=${S31_INF_S:-45} bash "$HERE/parry18-drive.sh" 2>&1 | grep -vE 'word: |xrtest: ' | tail -25 | sed 's/^/  p18: /'
fi
grep -E '^SWORDPHYS t=.* last 10 s' "$L" | tail -3 | cut -c1-500
echo "WARN lines: $(grep -c 'WARN' "$L")"
echo "$(date +%T) done"; echo quit >> "$R/cmd"
