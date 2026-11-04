#!/bin/bash
# deaths36-drive.sh - build 36's deaths (docs/re-xr-pipeline.md 164, som-b36-deaths): every orc death the blow's, at once.
# Drives one headless boot (scripts/headless_vr.sh [--win] with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the
# layer's test file). DS_STEPS = any of (in this order; default "log quick recoil cuts neck ground dying stealth charge spare"):
#   log     `kills quick log` + `kills trace on`: `kills kill ROW` for DS_ROWS on a fresh fighting orc each - the old
#           rules kill, the quick rule logs what it would do (the data: each half's pose at +0.05..1.2 s)
#   quick   `kills quick on`: the same rows - the deaths by the quick rule
#   recoil  a fighting orc at 35 health: a cut (its hit recoil), then the killing cut - the half on an orc in a recoil
#   cuts    the killing blow by the hand (a level cut and a stab into a fighting orc at 12): the table's row, pictures
#   neck    the same at the neck: the head off at the blow
#   ground  a downed orc (`abil kd`, read unaware when it is): stabs that land as hits, then the killing stab (at once)
#   dying   `kills quick log`, a test kill, two cuts through the dying orc: the hit shown (blood, sound, a pulse)
#   stealth an unaware orc from behind, standing still: a full cut into its back - the ordinary hit, the fight starts
#   charge  `practice charge` (the page's Charge execution): the charge ready, the finisher's ready on the orc ahead
#   spare   the hit counter one short (`kills charge 1`), an orc at 12 facing Talion: the killing cut completes the
#           charge - the orc kept at the floor for the finisher (SOMVR_FINISHER owns the charged kill)
# Words wait for their TRIGGER line (the Windows build reads the trigger file every ~1-2 s). Pictures:
# scripts/xr_compose.py RUN -f FRAME (frames in RUN/ds-steps.txt). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${DS_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${DS_STEPS:-log quick recoil cuts neck ground dying stealth charge spare}  # (boot 3: quick recoil cuts neck ground stealth spare)
ROWS=${DS_ROWS:-Standing_InstaKill_F Standing_InstaKill_F3 Standing_InstaKill_F4 Standing_InstaKill_F5 Standing_InstaKill_F6
  Standing_InstaKill_F8 Standing_InstaKill_F9 Standing_InstaKill_B Standing_InstaKill_B2 Standing_InstaKill_B3_Sever
  Stunned_InstaKill_F} # (AI_Bip_PC_ + each)
T0=$(date +%s)
R=${DS_RUN:-}
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
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
# a word taken for sure: written, then its TRIGGER line waited for (4 s at most), then the pause
w() { userin && bail "the user is in VR"; local a0 i; a0=$(cnt '^TRIGGER cmd='); printf '%s' "$1" > "$R/somvr.trigger"
  for i in $(seq 1 40); do [ "$(cnt '^TRIGGER cmd=')" -gt "$a0" ] && break; sleep 0.1; done
  echo "$(date +%T) word: $1"; sleep "${2:-0.3}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
d() { TS=0.1 x dump; sleep 0.5; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/ds-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
waitn() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"; LREST="hands test L -22 -35 25 0 0 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
RZ=90  # kills31's execution cut: wide and fast, right to left across the chest in 110 ms (the arm in it: build 32's force)
EDGE_A="hands test R 55 -8 30 0 -90 $RZ"; EDGE="hands swing R -35 -18 62 0 90 $RZ 110"
NECK_A="hands test R 55 4 30 0 -90 $RZ"; NECKC="hands swing R -35 2 62 0 90 $RZ 110"
PRE="hands test R 20 -27 -18 12 0 0"; THRUST="hands swing R 5 -11 54 12 0 0 180"
STAB_A="hands test R 12 -30 55 -90 0 0"; STABD="hands swing R 12 -85 58 -90 0 0 220"
# the unaware orc's cut (mark 7): the blade raised over the right shoulder (clear of its back at 112 cm), then down and
# across into its back in 160 ms
UP="hands test R 30 25 0 80 -40 0"; UPCUT="hands swing R -15 -20 55 -20 30 0 160"
spawn1() { w "practice remove" 2; w "practice spawn ${1:-infantry}" 4; w "execute face" 0.6; }
look() { x "head 0 0 0 yaw ${1:-0} pitch ${2:-0} s 0.3"; }
say() { since "$1" "${2:-^(KILLS (start|kill|t=|quick)|STEALTH (kill|t=.* (contact|touch|air))|ABIL t=.* the ground|FINISHER t=|PRACTICE t=.* charge|EXECUTE ready|WARN)}" |
  head -${3:-10} | cut -c1-360 | sed 's/^/    /'; }
fight1() { spawn1 infantry; w "practice health" 0.2; w "fight guard path" 0.3; z 2; w "execute face" 0.4; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.4; draw_slot; w "$REST" 0.4
grep -E '^(WARN|CONFIG kills)' "$L" | head -6 | cut -c1-400
w "kills trace on" 0.2; w "finisher off" 0.2 # (the charge fills over the kills: the finisher on only for charge/spare)
rowkill() { local n0 k0; fight1; n0=$(lines); k0=$(cnt '^KILLS kill t='); w "kills kill AI_Bip_PC_${1}" 0.2
  waitn '^KILLS kill t=' "$k0" 8 || echo "  $1: no death in 8 s"; z 1; say "$n0" '^KILLS (start|kill|quick|t=.*(kill|not))' 4; }
if has log; then echo "== log (the old rules kill, the quick rule logged)"; w "kills quick log" 0.2
  for r in $ROWS; do echo "  $r"; rowkill "$r"; done; fi
w "kills quick on" 0.2
if has quick; then echo "== quick"; for r in $ROWS; do echo "  $r"; rowkill "$r"; done; fi
cut_kill() { local tag=$1 a=$2 s=$3 hp=${4:-12}; fight1; w "bow orchp $hp" 0.3; w "abil goto 110" 0.4; w "execute face" 0.3
  local n0 k0; n0=$(lines); k0=$(cnt '^KILLS (start|kill) t='); w "$a" 0.3; w "$s" 0.05
  local i; for i in $(seq 1 80); do [ "$(cnt '^KILLS (start|kill) t=')" -gt "$k0" ] && break; sleep 0.05; done
  if [ "$(cnt '^KILLS (start|kill) t=')" -gt "$k0" ]; then # the dumps at once (+~0.05, 0.25, 0.55, 1.05 s): frames by time later
    TS=0.15 x dump; TS=0.25 x dump; TS=0.45 x dump; TS=0.1 x dump; echo "$tag dumps" >> "$R/ds-steps.txt"; fi
  z 2; w "$REST" 0.3; say "$n0" '' 8; }
if has recoil; then echo "== recoil"; for i in 1 2; do fight1; w "bow orchp 35" 0.3; w "abil goto 110" 0.4; w "execute face" 0.3; n0=$(lines)
  w "$PRE" 0.2; w "$THRUST" 0.1; w "$PRE" 0.1; w "$THRUST" 0.1; z 3; w "$REST" 0.3
  say "$n0" '^(COMBAT hit|KILLS (start|kill|t=))' 8; done; fi
has cuts && { echo "== cuts"; cut_kill stab "$PRE" "$THRUST"; cut_kill front "$EDGE_A" "$EDGE"; cut_kill stab2 "$PRE" "$THRUST"; }
has neck && { echo "== neck"; cut_kill neck "$NECK_A" "$NECKC"; }
if has ground; then echo "== ground (a downed orc: hits, then the kill)"
  w "practice remove" 2; z 3; w "practice remove" 1; w "practice far infantry" 3; w "verbs behind 200" 0.6; w "execute face" 0.4
  w "abil kd" 0.3; sleep 2.4
  n1=$(lines); w "abil goto 70" 0.6; by=$(since "$n1" '^ABIL t=.* goto:' | tail -1 | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p')
  look "${by:-0}" -35; n0=$(lines); w "stealth status" 0.3; w "bow orchp 22" 0.3
  w "$STAB_A" 0.3; w "$STABD" 0.3; w "$STAB_A" 0.3; w "$STABD" 0.1; sleep 0.1; d ground-a; sleep 0.3; d ground-b; z 2; look 0 0
  say "$n0" '^(STEALTH t=.* (downed|touch|air|aware)|COMBAT (hit|brush|glance)|ABIL t=|KILLS (start|kill|t=))' 14
  w "$REST" 0.3; say "$n0" '' 8; fi
if has dying; then echo "== dying (a blade through an orc whose kill plays)"; w "kills quick log" 0.2
  fight1; n0=$(lines); w "kills kill AI_Bip_PC_Standing_InstaKill_F3" 0.1
  for i in 1 2; do w "$EDGE_A" 0.1; w "$EDGE" 0.2; done; z 2; w "$REST" 0.3; w "kills quick on" 0.2
  say "$n0" '^(KILLS (start|kill|t=.*while its kill plays)|HITFEEL)' 10; fi
if has stealth; then echo "== stealth (mark 7: a full cut into an unaware orc's back, standing)"
  w "practice remove" 2; z 3; w "practice remove" 1; w "practice far infantry" 3; w "$REST" 0.4; w "verbs behind 110" 1.0
  w "execute face" 0.5; w "stealth status" 0.3
  n0=$(lines); w "$EDGE_A" 0.2; w "$EDGE" 0.2; z 3; w "$REST" 0.3
  say "$n0" '^(STEALTH (kill|t=.* (contact|touch|air|kill|alert|aware))|COMBAT hit|KILLS)' 10
  echo "  the orc after: $(since "$n0" '^STEALTH t=.* aware orc' | tail -1 | cut -c1-160)"; fi
if has charge; then echo "== charge (Practice > Charge execution)"; w "finisher on" 0.2; fight1; n0=$(lines); w "practice charge" 1.5; z 2
  say "$n0" '^(PRACTICE t=.* charge|EXECUTE ready|FINISHER t=.* (ready|cancel))' 6; w "finisher off" 0.3; z 1; w "finisher on" 0.3; fi
if has spare; then echo "== spare (the killing cut completes the charge)"; w "finisher on" 0.2; fight1; w "bow orchp 12" 0.3; w "abil goto 110" 0.4
  w "$PRE" 0.3; n0=$(lines); w "kills charge 1" 0.05; w "$THRUST" 0.2; z 3; w "$REST" 0.3
  say "$n0" '^(KILLS (start|kill|t=.*charge)|FINISHER t=|EXECUTE ready|COMBAT hit)' 10; fi
w "kills status" 1
grep -E '^KILLS status' "$L" | tail -2 | cut -c1-500
echo "$(date +%T) done"
[ -n "${DS_NOQUIT:-}" ] || echo quit >> "$R/cmd"
