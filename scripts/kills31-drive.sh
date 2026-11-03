#!/bin/bash
# kills31-drive.sh - build 31's SOMVR_KILLS (docs/re-xr-pipeline.md 133): the catalogue of the victims' halves and the
# kills by the hand. Drives one headless boot (scripts/headless_vr.sh with scripts/headset-build26.env, SOMVR_HANDS_TEST=1,
# the layer's test file). KL_STEPS = any of (in this order; default "diag cat test front neck stab stealth exec ground sever"):
#   hold    the AI hold (SOMVR_KILLS_AI_HOLD): F3, F4, Sneak_IK_B_3 played on a fighting orc, Sneak_IK_B and F4 on an
#           unaware one - do the halves run to their end now, does an unaware orc play its half?
#   rows    `kills kill ROW` on a fresh fighting orc for KL_ROWS (default the counter kill, the stunned kill, a sneak kill)
#   diag2   an unaware orc: Sneak_IK_B with `kills prep off`, then Sneak_IK_B, _B_3 and F3 with `kills prep on` (the
#           orc readied as the sync start readies its target: does an unaware orc play its half?)
#   diag    a few rows played (`kills play`) on a fighting orc and on an unaware one, `kills free off` then `on`: which
#           rows play alone, and whether the victim's SyncToAnim off lets them (boot 20261102-113313: most stood idle)
#   cat     every table row played on an unaware practice orc 3 m ahead (`kills cat`): its trace (KILLS play lines),
#           a dump at +0.5 s and +1.4 s of each (KL_CAT_DUMPS=0: none)
#   test    `kills kill ROW` for KL_TEST_ROWS on a fresh orc: the full path without a blade (the death's moment, the
#           credit); dumps through it
#   front   a fighting infantry near death (`bow orchp 12`), a full level cut into its chest: the killing blow
#   neck    the same, the cut at its neck's height: a _Sever row, the head off
#   stab    the same, a thrust into its chest: a stab row
#   stealth an unaware orc from behind, crouched at the stand-off: the thrust (kill22's) - a Sneak_IK_B row
#   exec    the charge full (`verbs brand charge`), a full cut (no trigger): the execution
#   ground  a downed orc (`abil kd`), the stab down (abil27's): its KD row
#   sever   `kills sever` on a live orc (the game's sever on a living one: does it hold?)
# Pictures: scripts/xr_compose.py RUN -f FRAME (frames in RUN/kl-steps.txt). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${KL_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${KL_STEPS:-diag2 cat test front neck stab stealth exec ground}
T0=$(date +%s)
R=${KL_RUN:-}
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
d() { TS=0.1 x dump; sleep 0.5; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/kl-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
waitn() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"; LREST="hands test L -22 -35 25 0 0 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
RZ=90  # abil27's level edge cut, right to left across the chest (300 ms)
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"
NECK_A="hands test R 40 4 35 0 -90 $RZ"; NECKC="hands swing R -20 2 62 0 90 $RZ 300"
# kill22's thrust (the blade along the shoulder ray, the point leading)
PRE="hands test R 20 -27 -18 12 0 0"; READY2="hands swing R 16 -23 0 12 0 0 500"; THRUST="hands swing R 5 -11 54 12 0 0 180"
STAB_A="hands test R 12 -30 55 -90 0 0"; STABD="hands swing R 12 -85 58 -90 0 0 220" # (220 ms: 134's weighted tip lags a 150 ms stab)
spawn1() { w "practice remove" 2; w "practice spawn ${1:-infantry}" 4; w "execute face" 1; }
look() { x "head 0 0 0 yaw ${1:-0} pitch ${2:-0} s 0.3"; }
klsay() { since "$1" '^(KILLS|STEALTH t=.* kill|EXECUTE kill|ABIL t=.* the ground|COMBAT death|WARN)' | head -${2:-12} | cut -c1-420 | sed 's/^/    /'; }
# a kill's frames: right after the start, then through it
seq_dumps() { local tag=$1 n0=$2; waitn '^KILLS start t=' "$n0" 6 || { echo "  $tag: no KILLS start"; return 1; }
  [ -n "${KL_LOOK:-}" ] && TS=0.05 x "head 0 0 0 yaw 0 pitch ${KL_LOOK} s 0.15"
  grep -E '^KILLS start t=' "$L" | tail -1 | cut -c1-400 | sed 's/^/    /'
  sleep 0.1; d "$tag-a"; sleep 0.2; d "$tag-b"; sleep 0.3; d "$tag-c"; sleep 0.5; d "$tag-d"; sleep 1.0; d "$tag-e"
  [ -n "${KL_LOOK:-}" ] && TS=0.3 x "head 0 0 0 yaw 0 pitch 0 s 0.3"; return 0; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; draw_slot; w "$REST" 0.6
grep -E '^(WARN|CONFIG kills)' "$L" | head -6 | cut -c1-500
playone() { local n0; n0=$(lines); w "kills play $1 5" 0.3; z 5; since "$n0" "^KILLS play (t=|END)" | cut -c1-600 | sed 's/^/    /'; z 1; }
if has diag; then
  echo "== diag"
  spawn1 infantry; w "fight guard path" 0.4; z 3; w "execute face" 0.5
  for fr in off on; do w "kills free $fr" 0.3; for row in AI_Bip_PC_Standing_InstaKill_F3 AI_Bip_PC_Standing_InstaKill_F4 AI_Bip_PC_Sneak_IK_B_2 AI_Bip_PC_Sync_Intrgtn_Stnd_F_Kill_Silent; do
    echo "  fighting orc, free $fr: $row"; playone $row; done; done
  w "practice remove" 2; w "practice far infantry" 3; w "verbs behind 300" 1; w "execute face" 1
  for row in AI_Bip_PC_Standing_InstaKill_F3 AI_Bip_PC_Standing_InstaKill_B2; do echo "  unaware orc, free on: $row"; playone $row; done
fi
if has brain; then # the unaware orc: its brain let go first (SOMVR_KILLS_BRAIN) - off, then on
  echo "== brain"
  w "practice remove" 2; w "practice far infantry" 3; w "verbs behind 250" 1; w "execute face" 1
  w "kills brain off" 0.3; echo "  unaware, brain off: AI_Bip_PC_Sneak_IK_B"; playone AI_Bip_PC_Sneak_IK_B
  w "kills brain on" 0.3
  for row in AI_Bip_PC_Sneak_IK_B AI_Bip_PC_Sneak_IK_B_2; do echo "  unaware, brain on: $row"; n0=$(lines)
    w "kills play $row 5" 0.3; sleep 0.4; d "brain-$row-a"; sleep 0.6; d "brain-$row-b"; sleep 1.0; d "brain-$row-c"; z 4
    since "$n0" "^KILLS play (t=|END)" | sed -E 's/its AnimationData.*//' | cut -c1-300 | sed 's/^/    /'; z 1; done
fi
if has hold; then
  echo "== hold"
  w "kills aihold on" 0.3; spawn1 infantry; w "fight guard path" 0.4; z 3; w "execute face" 0.5
  for row in AI_Bip_PC_Standing_InstaKill_F3 AI_Bip_PC_Standing_InstaKill_F4 AI_Bip_PC_Sneak_IK_B_3; do
    echo "  fighting, held: $row"; n0=$(lines); w "kills play $row 6" 0.3; sleep 0.5; d "hold-$row-a"; sleep 1.0; d "hold-$row-b"
    z 5; since "$n0" "^KILLS play END" | cut -c1-300 | sed 's/^/    /'; done
  w "practice remove" 2; w "practice far infantry" 3; w "verbs behind 250" 1; w "execute face" 1
  for row in AI_Bip_PC_Sneak_IK_B AI_Bip_PC_Standing_InstaKill_F4; do echo "  unaware, held: $row"; playone $row; done
fi
if has rows; then
  echo "== rows"
  for row in ${KL_ROWS:-AI_Bip_PC_Sync_CntrKill_F AI_Bip_PC_Stunned_InstaKill_F AI_Bip_PC_Sneak_IK_B_3}; do
    spawn1 infantry; w "fight guard path" 0.4; z 3; w "execute face" 0.6
    n0=$(lines); k0=$(cnt '^KILLS start t='); w "kills kill $row" 0.1; seq_dumps "row-$row" "$k0"; z 3; klsay "$n0" 6; done
fi
if has diag2; then
  echo "== diag2"
  w "practice remove" 2; w "practice far infantry" 3; w "verbs behind 250" 1; w "execute face" 1
  w "kills prep off" 0.3; echo "  unaware, prep off: AI_Bip_PC_Sneak_IK_B"; playone AI_Bip_PC_Sneak_IK_B
  w "kills prep on" 0.3
  for row in AI_Bip_PC_Sneak_IK_B AI_Bip_PC_Sneak_IK_B_3 AI_Bip_PC_Standing_InstaKill_F3; do
    echo "  unaware, prep on: $row"; playone $row; done
fi
if has cat; then
  echo "== cat (a fighting orc, the test guard up)"
  w "front keep 0" 0.3; spawn1 infantry; w "fight guard path" 0.4; z 3; w "execute face" 1
  n0=$(lines); P0=$(cnt '^KILLS play t='); G0=$(cnt '^KILLS play t=.* no orc'); w "kills cat" 0.5
  for i in $(seq 1 600); do userin && bail "the user is in VR"
    grep -q '^KILLS cat t=.* done' "$L" && break
    p=$(cnt '^KILLS play t=')
    if [ "$p" -gt "$P0" ] && [ "${KL_CAT_DUMPS:-1}" = 1 ]; then P0=$p; nm=$(grep -E '^KILLS play t=' "$L" | tail -1 | grep -oE "'AI_[A-Za-z0-9_]+'" | head -1 | tr -d "'")
      sleep 0.4; d "cat-$nm-a"; sleep 1.0; d "cat-$nm-b"; fi
    if [ "$(cnt '^KILLS play t=.* no orc')" -gt "$G0" ]; then G0=$(cnt '^KILLS play t=.* no orc')
      echo "the orc is gone: a fresh one"; spawn1 infantry; z 3; w "execute face" 1
      k=$(grep -cE "^KILLS play END" "$L"); w "kills cat $k" 0.5; fi
    sleep 0.25; done
  grep -E '^KILLS play END' "$L" | cut -c1-260 | sed 's/^/    /'
fi
if has test; then
  echo "== test (unaware ${KL_UNAWARE:-off})"; w "kills unaware ${KL_UNAWARE:-off}" 0.3
  for row in ${KL_TEST_ROWS:-AI_Bip_PC_Standing_InstaKill_F3 AI_Bip_PC_Standing_InstaKill_F2_Sever AI_Bip_PC_Sneak_IK_B}; do
    w "practice remove" 2; w "practice far infantry" 3; w "verbs behind 160" 1; w "execute face" 1
    n0=$(lines); k0=$(cnt '^KILLS start t='); w "kills kill $row" 0.1; seq_dumps "test-$row" "$k0"; z 3; klsay "$n0"; done
fi
cut_kill() { local tag=$1 a=$2 s=$3; spawn1 infantry; w "fight guard path" 0.4; z 2; w "execute face" 0.6; w "bow orchp 12" 0.6
  n0=$(lines); k0=$(cnt '^KILLS start t='); w "$a" 0.4; w "$s" 0.1; seq_dumps "$tag" "$k0"; z 2; w "$REST" 0.4; klsay "$n0" 14; }
has front && { echo "== front"; cut_kill front "$EDGE_A" "$EDGE"; }
has neck && { echo "== neck"; cut_kill neck "$NECK_A" "$NECKC"; }
has stab && { echo "== stab"; cut_kill stab "$PRE" "$THRUST"; }
# the stealth thrust (build 31, the weighted sword): from the right hip forward and a little left onto its spine, the
# blade pitched up 10 deg, the grip 4 cm right of the eyes and 62 cm out, the point ~140 cm ahead (the stand-off 118 cm:
# its back ~98 cm off), 250 ms (a real stab: ~2 m/s at the hand; kill22's 180 ms thrust passed the body 13-32 cm off)
SREADY="hands test R 12 -24 15 10 -6 0"; STHRUST="hands swing R 4 -20 62 10 -6 0 250"
if has stealth; then
  echo "== stealth (unaware ${KL_UNAWARE:-off})"
  w "kills unaware ${KL_UNAWARE:-off}" 0.3
  w "practice remove" 2; w "practice far infantry" 3; w "$PRE" 0.4; w "verbs behind 220" 0.8; w "execute face" 1
  # kill22's sneak: ducked (the real crouch), the test stick at 0.6 toward it: the stand-off holds Talion at 118 cm
  r0=$(cnt '^STEALTH t=.* stand-off: at rest'); x "head 0 -50 0 s 0.5"; sleep 1.2; waitn '^CROUCH (game|duck) on' 0 3
  w "stealth chase 0.6 60" 0.3; waitn '^STEALTH t=.* stand-off: at rest' "$r0" 12; sleep 1.5; w "stealth chase 0" 0.3; sleep 1.0
  w "execute face" 0.4; w "$SREADY" 0.8; n0=$(lines); k0=$(cnt '^KILLS start t='); w "$STHRUST" 0.1; seq_dumps stealth "$k0"
  z 2; klsay "$n0" 14
  since "$n0" '^STEALTH t=.* (air|touch|kill|carried)' | head -4 | cut -c1-260 | sed 's/^/    /'
  x "head 0 0 0 s 0.4"; w "crouch stand" 0.4; w "$REST" 0.4
fi
# stealth2 / held: Talion put 112 cm behind an unaware orc (outside its ~108 cm notice; no walk, no brake overshoot),
# ducked, faced to it; then the thrust (stealth2) or the left hand's grab at its back and the thrust (held)
# (the blade at rest behind the hip through the teleport and the duck - boot 142539: the ready blade touched its back as
# the head went down and woke it - then drawn up short of its back: the point ~77 cm ahead, its back ~92 cm off)
SREADY2="hands test R 12 -24 -5 10 -6 0"
behind_ducked() { w "practice remove" 2; w "practice far infantry" 3; w "$REST" 0.4; w "verbs behind ${KL_BEHIND:-112}" 1.0
  x "head 0 -45 0 s 0.4"; sleep 1.0; w "execute face" 0.6; w "stealth status" 0.6; w "$SREADY2" 0.8; }
for sn in $(seq 1 ${KL_STEALTH_N:-1}); do has stealth2 || break
  echo "== stealth2 #$sn (unaware ${KL_UNAWARE:-off})"; w "kills unaware ${KL_UNAWARE:-off}" 0.3
  behind_ducked; n0=$(lines); s0=$(cnt '^STEALTH kill'); k0=$(cnt '^KILLS start t='); w "$STHRUST" 0.1; sleep 0.3; d stealth2-a
  sleep 0.5; d stealth2-b; sleep 0.7; d stealth2-c; sleep 0.8; d stealth2-d; sleep 1.2; d stealth2-e; z 4
  since "$n0" '^(KILLS sync|HOLD talion|CAPTAIN t=.*presentation)' | head -8 | cut -c1-400 | sed 's/^/    /' 
  echo "  stealth kills $(( $(cnt '^STEALTH kill') - s0 )), KILLS starts $(( $(cnt '^KILLS start t=') - k0 ))"
  since "$n0" '^(STEALTH (kill|t=.* (air|touch|kill by|carried|contact))|KILLS (start|kill|t=))' | head -6 | cut -c1-300 | sed 's/^/    /'
  w "$REST" 0.4; x "head 0 0 0 s 0.4"; w "crouch stand" 0.4
done
if has held; then
  echo "== held"
  behind_ducked; w "hands test L -10 -8 80 180 -90 0" 0.8; w "hands squeeze L 1" 1.0
  since "$(( $(lines) - 60 ))" '^VERBS t=.* (grab:|squeeze)' | tail -2 | cut -c1-220 | sed 's/^/    /'
  n0=$(lines); w "$SREADY2" 0.6; w "$STHRUST" 0.1; sleep 0.4; d held-a; sleep 0.8; d held-b; z 2; w "hands squeeze L 0" 0.5
  since "$n0" '^(HOLD31|KILLS (start|kill|t=)|STEALTH t=.* (air|touch))' | head -6 | cut -c1-300 | sed 's/^/    /'
  w "$REST" 0.4; w "hands test L -22 -35 25 0 0 0" 0.4; x "head 0 0 0 s 0.4"; w "crouch stand" 0.4
fi
if has exec; then
  echo "== exec"
  # (the fighting orc circles: Talion put 110 cm short of it right before the cut - the suite's 20627 cut met nothing at
  # 143 cm after the orc had backed off to 3.8 m)
  spawn1 infantry; w "fight guard path" 0.4; z 2; w "verbs brand charge" 0.6; w "abil goto 110" 0.5; w "execute face" 0.4
  # (134's weighted sword: the 300 ms edge cut read light in 173839, a 200 ms one at 11.2 m/s in 181649 - the heavy
  # tier wants the contact point at 12 m/s: 140 ms)
  n0=$(lines); k0=$(cnt '^KILLS start t='); w "hands test R 55 -8 30 0 -90 $RZ" 0.4; w "hands swing R -35 -18 62 0 90 $RZ 110" 0.1
  seq_dumps exec "$k0"; z 2; w "$REST" 0.4; klsay "$n0" 14
fi
if has ground; then
  echo "== ground"
  # (the knockdown slides it - the suite's orc went 'IntoWall_KD' to 4 m: Talion placed once it lies in its loop, the head
  # turned to its bearing, as abil27's goto_orc)
  spawn1 infantry; w "fight guard path" 0.4; w "verbs brand charge" 0.3; w "abil kd" 0.4
  sleep 2.4 # (the knockdown's slide is over by then; 181649 waited for a KDLoop line 8 s and the orc had got up)
  n1=$(lines); w "abil goto 70" 0.8; by=$(since "$n1" '^ABIL t=.* goto:' | tail -1 | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p')
  look "${by:-0}" -35; n0=$(lines); k0=$(cnt '^KILLS start t='); w "$STAB_A" 0.5; w "$STABD" 0.1; seq_dumps ground "$k0"; z 2; look 0 0
  w "$REST" 0.4; klsay "$n0" 14
fi
if has sever; then
  echo "== sever (alive)"
  spawn1 infantry; z 2; n0=$(lines); w "kills sever" 0.6; d sever-a; z 2; d sever-b; z 3; d sever-c; klsay "$n0" 6
  since "$n0" '^(VERBS t=.* orc .* now|COMBAT death)' | head -6 | cut -c1-200
fi
w "kills status" 1
grep -E '^KILLS status' "$L" | tail -1 | cut -c1-700
echo "$(date +%T) done"
if [ -n "${KL_KILL22:-}" ]; then # then kill22's own stealth rounds in the same boot (its proven approach and thrust)
  w "practice remove" 2; draw_slot; w "$REST" 0.4 # (the sword back on its slot: kill22 draws it with the same squeeze)
  SD_RUN=$R SD_STEPS="$KL_KILL22" bash "$(dirname "$0")/kill22-drive.sh"; exit 0; fi
[ -n "${KL_NOQUIT:-}" ] || echo quit >> "$R/cmd"
