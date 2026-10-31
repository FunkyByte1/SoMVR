#!/bin/bash
# abil27-drive.sh - build 27's abilities round (som-b27-abilities; docs/re-xr-pipeline.md 114): the skills' records,
# the game's offers of its streak specials against a practice orc (no skills, the skills granted on the run's save copy
# with `beasts grant`, the streak raised, stunned, knocked down), then the VR gestures: the wraith flash (the right
# trigger held + the open left hand thrust out at the orc; first without the trigger: nothing may start) and the ground
# finisher (the sword's stab down into a downed orc; first a slow lowering: nothing may die). One headless boot at the
# practice arrival (crowd27-drive.sh's setup, scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the layer's test file);
# marks and dumps for both-eye pictures (scripts/xr_compose.py).
# ABIL27_STEPS = any of (in this order; default: flash ground): info recs flash ground gaoe
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${ABIL27_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${ABIL27_STEPS:-flash ground}
T0=$(date +%s)
R=${ABIL27_RUN:-}
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
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
d() { x "${2:-dump}" ${2:+dump}; sleep 0.6; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/abil-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
lines() { wc -l < "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
offered() { grep -E "^ABIL t=.*: the game offers $1" "$L" | tail -1 | grep -q .; }
offer_now() { local n0; n0=$(lines); w "abil probe" 1.2; since "$n0" '^ABIL probe t=.* orc ' | head -1 | grep -q "$1 -> '"; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
LREST="hands test L -22 -35 25 0 0 0"
RZ=90
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# the push: the open left hand from the chest 45 cm out and a little up toward the orc ahead in 130 ms (~3.5 m/s); the
# grip's -Z (pinky -> thumb) level ahead, the palm facing out as a real hand pushing holds it (rz 90: the palm to the right
# turned forward by the wrist is not modelled - the judge reads the hand's way, not its palm)
PUSH_A="hands test L -18 -30 22 0 0 0"; PUSH="hands swing L -10 -24 67 0 0 0 130"
push() { w "$PUSH_A" 0.5; w "$PUSH" 0.5; w "$LREST" 0.5; }
# the stab: the blade straight down (the grip's -Z down, rx -90) held out at the chest 55 cm ahead, driven 50 cm down in
# 150 ms (3.3 m/s, point first): the tip from about the knees to the ground where the downed orc lies
STAB_A="hands test R 12 -30 55 -90 0 0"; STAB="hands swing R 12 -80 60 -90 0 0 150"
LOWER="hands swing R 12 -80 60 -90 0 0 1400"   # the same way over 1.4 s: a sword lowered, not a stab
stab() { w "$STAB_A" 0.6; w "$STAB" 0.6; w "$REST" 0.6; }
# 0. the controls card away, the left hand forward, the sword drawn, the blade behind the hip
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST"
w "$REST" 2
drawn || draw_slot
w "$REST" 1
grep -E '^(WARN|CONFIG abil)' "$L" | head -8 | cut -c1-400
GRANTS="PSS_Wraith_Stun_1 PSS_Wraith_Flash_1 PSS_Wraith_AOE_1 PSS_Wraith_Stun_2 PSS_Wraith_Flash_2 PSS_Wraith_AOE_2 PSS_GroundKillAoE PSS_Ground_Slam PSS_Wraith_Ground_Slam"
spawn1() { w "practice remove" 2; w "practice spawn ${1:-infantry}" 4; w "execute face" 1; }
YAW=0
look() { YAW=$1; x "head 0 0 0 yaw $YAW pitch ${2:-0} s 0.3"; }
pick() { local n0; n0=$(lines); w "abil probe${2:-}" 1.0; since "$n0" '^ABIL probe t=.* orc ' | head -1 | grep -oE "$1 -> [^;]*" | cut -c1-160; }
if has info; then
  echo "== which skill opens which node (a full charge each time)"
  spawn1 infantry; w "fight guard path" 0.5
  w "verbs brand charge" 0.5; echo "none: $(pick PC_WraithAoE)"
  for s in $GRANTS; do w "beasts grant $s" 0.6; w "verbs brand charge" 0.5
    echo "+$s: $(pick PC_WraithAoE) | $(pick PC_Stun_Root_Lowest) | $(pick PC_DashOver)"; done
  for r in PC_GroundTakedown PC_Bip_GroundTakedown_F_1 PC_Bip_KD_InstaKill_L; do w "abil rec $r" 1; done
fi
if has recs; then
  for r in PC_Stun_Root PC_Stun_Root_Lowest PC_WraithAoE PC_GroundKill_AoE; do w "abil rec $r" 1; done
fi
if has flash; then
  echo "== flash"
  has info || { spawn1 infantry; w "fight guard path" 0.5; for s in $GRANTS; do w "beasts grant $s" 0.6; done; }
  w "verbs brand charge" 0.6; offer_now PC_WraithAoE
  F0=$(cnt '^ABIL t=.* start: the wraith flash')
  w "execute face" 0.6; push; sleep 1        # first without the trigger: the same push - nothing may start
  [ "$(cnt '^ABIL t=.* start: the wraith flash')" -eq "$F0" ] && echo "no trigger: nothing started (right)" || echo "NO TRIGGER STARTED IT (wrong)"
  for i in 1 2 3; do
    [ "$i" -gt 1 ] && { spawn1 infantry; w "fight guard path" 0.4; z 2; }   # a fresh orc in reach (the stunned one backs off)
    w "execute face" 0.5; w "verbs brand charge" 0.3
    TS=0.3 x "btn R trigger 1"; d "flash$i-cue" "btn R trigger 1"   # (the dump's file keeps the trigger held)
    n0=$(lines); w "$PUSH_A" 0.3; w "$PUSH" 0.3; TS=0.2 x "btn R trigger 0"; w "$LREST" 0.2
    waitfor '^ABIL t=.* start: the wraith flash' "$F0" 3 && { sleep 0.4; d "flash$i-after"; }
    sleep 3.2
    since "$n0" '^ABIL (t=.* (push|press|start|a fast left)|watch)' | head -14 | cut -c1-260
    F0=$(cnt '^ABIL t=.* start: the wraith flash')
  done
fi
kd_orc() { local n0; n0=$(lines); w "verbs brand charge" 0.3; w "abil kd" 0.4; sleep 1.8; w "abil probe" 0.7
  since "$n0" '^ABIL (kd|probe t=.* orc)' | grep -oE "^ABIL kd.*|state '[^']*'|PC_GroundTakedown -> [^;]*|PC_PoolAttacks_KD -> [^;]*|PC_InstaKill -> [^;]*|PC_GroundKill_AoE -> [^;]*" | cut -c1-200; }
goto_orc() { local n0 by; n0=$(lines); w "abil goto ${1:-70}" 0.6
  by=$(since "$n0" '^ABIL t=.* goto:' | tail -1 | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p')
  since "$n0" '^ABIL t=.* goto:' | tail -1 | cut -c1-300
  [ -n "$by" ] && look $(( YAW + ${by#+} )) 0; }
if has ground; then
  echo "== ground"
  spawn1 infantry; w "fight guard path" 0.5; look 0 0
  G0=$(cnt '^ABIL t=.* the ground finisher: Combat_DealHit')
  kd_orc; goto_orc 70; look $YAW -35; d "ground-cue"; look $YAW 0
  w "$STAB_A" 0.5; w "$LOWER" 1.7; w "$REST" 0.4      # a lowered sword: nothing may die
  [ "$(cnt '^ABIL t=.* the ground finisher: Combat_DealHit')" -eq "$G0" ] && echo "lowered: nothing (right)" || echo "LOWERED KILLED (wrong)"
  for k in 1 2; do
    z 3; spawn1 infantry; w "fight guard path" 0.5; look 0 0; kd_orc; goto_orc 70
    n0=$(lines); stab; look $YAW -35; d "ground$k-after"; sleep 0.8; d "ground$k-after2"; sleep 1.5
    since "$n0" '^ABIL (t=.* (stab|the ground|a downward)|watch)' | head -12 | cut -c1-300
  done
  look 0 0
fi
if has gaoe; then
  echo "== ground AoE"
  spawn1 infantry; w "fight guard path" 0.5
  kd_orc; goto_orc 70; TS=0.3 x "btn R trigger 1"; n0=$(lines); stab; TS=0.3 x "btn R trigger 0"; sleep 3
  since "$n0" '^ABIL (t=.* (stab|start|press|the ground|a downward)|watch)' | head -16 | cut -c1-300
fi
echo "== the run's summary"
w "abil status" 1
grep -E '^ABIL (status|END)' "$L" | tail -1 | cut -c1-900
echo "$(date +%T) done"; [ -n "${ABIL27_NOQUIT:-}" ] || echo quit >> "$R/cmd"
