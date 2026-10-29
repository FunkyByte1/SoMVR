#!/bin/bash
# beasts22-drive.sh - build 22's beasts (handoff/HANDOFF-beasts.md, docs/re-xr-pipeline.md 92): drives one headless boot
# (scripts/headless_vr.sh with scripts/headset-build22.env, SOMVR_HANDS_TEST=1, SOMVR_BEASTS_TRACE, the layer's test
# file). B22_STEPS, in this order (default all):
#   skills  `beasts skills`: the save's Inventory/Skill/Skills with the owned bit (riding)
#   caragor a caragor ahead (`practice spawn caragor`): its nodes; its attacks at Talion with the blade held across its
#           path at its head's height (DEFENCE's verdicts; the trace: the view's per-frame path through its lunges and a
#           takedown), then the cuts at it until it is down
#   ghul    the same with a ghul (the cuts)
#   graug   the same with a graug (the guard at its fists' height, the cuts at its legs)
#   react   a caragor at 30 health: at each of its attacks the guard across its bearing (DEFENCE's verdict on its
#           jaws), then a low cut there, until it is down
#   guard   a caragor at full health: `fight guard path` (the test guard learns each attack's path on its jaws, then
#           puts the blade there), then `fight guard miss` (the control)
#   block   a caragor at full health: at each of its attacks the blade across its line (its bearing), then the
#           guard on the far side (the control)
#   smount  the Shadow Mount (PSS_Shadow_Mount): a far caragor and a far graug, the wraith vision, the gaze, A - does
#           the game mount it (Talion's P+0x1d8)
# B22_KINDS overrides the beasts (default caragor ghul graug). B22_RUN=RUN drives a running boot. Lines to read:
# BEASTS ..., COMBAT hit ..., THREAT ..., DEFENCE ..., BODYMOMENT ..., HOLD talion ...; b22-steps.txt (the dumps' frames
# for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B22_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B22_STEPS:-skills caragor ghul graug react smount}
KINDS=${B22_KINDS:-caragor ghul graug}
T0=$(date +%s)
R=${B22_RUN:-}
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
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
nsince() { since "$1" "$2" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/b22-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/b22-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/b22-steps.txt"; }
head_to() { t "head 0 0 0 yaw $1 pitch $2 s ${3:-0.3}"; sleep 0.3; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
RZ=90
# a low cut: from the right, level, at a caragor's back height (the head pitched down 20 deg)
LOW_A="hands test R 45 -55 45 0 -90 $RZ"; LOW="hands swing R -40 -60 60 0 90 $RZ 300"; LOW_B="hands swing R 45 -55 45 0 -90 $RZ 300"
# the orc's cut (combat21's ecut): chest height
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
lowcut() { w "$LOW_A" 0.3; w "$LOW" 0.6; w "$LOW_B" 0.6; }
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
bdown() { tail -400 "$L" | grep -qE "^PRACTICE t=.* (is down|the .* orc is down|down after)"; }

# 0. the controls card away, the hands at rest (combat21's start)
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG beasts|CONFIG hold)' "$L" | cut -c1-240
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p'); [ -n "$OB_Y" ]; }
HY=0; HP=0
look() { orc0 || return 1; HY=$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}'); HP=$(awk -v a="$HP" -v b="$OB_P" -v u="${1:-0}" 'BEGIN{v=a+b+u; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}'); head_to "$HY" "$HP" 0.25; }
# the guard: the blade level across the body at a caragor's head (90 cm under the eyes) / a graug's fists (chest)
GUARD_LOW="hands test R 40 -85 50 0 90 0"; GUARD="hands test R 42 -12 35 0 90 0"
LOW_A="hands test R 45 -70 45 0 -90 $RZ"; LOW="hands swing R -40 -75 60 0 90 $RZ 300"; LOW_B="hands swing R 45 -70 45 0 -90 $RZ 300"
lowcut() { w "$LOW_A" 0.3; w "$LOW" 0.6; w "$LOW_B" 0.6; }

if has skills; then
  echo "== skills"
  n0=$(lines); w "beasts skills" 1.5
  since "$n0" '^BEASTS t=.* skills:' | tail -1 | cut -c1-300
  since "$n0" '^BEASTS skills:' | grep -oiE ' [A-Za-z0-9_]*(warg|caragor|mount|ride|riding|troll|graug|brand|dominat|beast|goblin|ghul)[A-Za-z0-9_]*=[01]' | tr '\n' ' '; echo
fi

for kind in $KINDS; do
  has "$kind" || continue
  echo "== $kind"
  w "practice remove" 2; HY=0; HP=-15; head_to 0 -15
  drawn || draw_slot
  w "$REST" 0.4; w "$LREST" 0.3
  n0=$(lines); w "practice spawn $kind" 2.5
  since "$n0" "^PRACTICE t=.* spawn " | tail -1 | cut -c1-200
  since "$n0" '^BEASTS t=.* nodes of the' | tail -1 | cut -c1-600
  d "$kind-spawned"
  # its attacks with the guard held (the trace runs through them)
  n1=$(lines)
  case $kind in graug) w "$GUARD" 0.3 ;; *) w "$GUARD_LOW" 0.3 ;; esac
  for k in $(seq 1 7); do
    waitfor "^BEASTS t=.* attack '[^-][^']*' at Talion" 4 && { T_S=0.05 d "$kind-attack-$k"; sleep 0.4; T_S=0.05 d "$kind-attack-$k-b"; }
    sleep 1.0
  done
  echo "$kind attacks at Talion: $(nsince "$n1" "^BEASTS t=.* attack '[^-][^']*' at Talion") ($(since "$n1" "^BEASTS t=.* attack '[^']*' at Talion" | grep -oE "attack '[^']*'" | sort | uniq -c | sort -rn | head -8 | tr '\n' ' '))"
  echo "  Talion: $(since "$n1" '^BEASTS t=.* Talion: ' | grep -oE "(attack|target sync [^ ]+) '[^']+'" | sort | uniq -c | sort -rn | head -8 | tr '\n' ' ')"
  echo "  defence: contacts $(nsince "$n1" '^DEFENCE contact') hits $(nsince "$n1" '^DEFENCE hit') dodges $(nsince "$n1" '^DEFENCE dodge') parries $(nsince "$n1" '^DEFENCE (parry|counter)'); holds $(nsince "$n1" '^HOLD talion start') blinks $(nsince "$n1" '^BODYMOMENT .*released by a blink')"
  since "$n1" '^(DEFENCE (contact|hit)|HOLD talion (start|end))' | head -6 | cut -c1-260
  # the cuts until it is down (at most 24)
  w "$REST" 0.3; n2=$(lines)
  for k in $(seq 1 24); do
    bdown && break
    if [ "$kind" = graug ] || [ $((k % 2)) = 1 ]; then lowcut; else ecut; fi
    [ $((k % 6)) = 3 ] && T_S=0.05 d "$kind-cut-$k"
  done
  w "$REST" 0.4
  echo "$kind cuts: hits $(nsince "$n2" '^COMBAT hit tier='), kills $(since "$n2" '^COMBAT hit tier=' | grep -c 'dead=0->1'); down: $(bdown && echo yes || echo no)"
  since "$n2" '^COMBAT hit tier=' | head -3 | cut -c1-300
  bdown && pass "$kind down by the blade" || fail "$kind not down after the cuts"
  sleep 2; T_S=0.05 d "$kind-after"
  w "beasts status" 0.6
done

# a hand pose turned by the bearing B (deg, + right): pose X Y Z RX RY RZ -> the same about the head's vertical axis
rot() { awk -v b="$1" -v x="$2" -v y="$3" -v z="$4" -v rx="$5" -v ry="$6" -v rz="$7" 'BEGIN{r=b*3.14159265/180; c=cos(r); s=sin(r);
  printf "%.0f %.0f %.0f %s %.0f %s", x*c+z*s, y, -x*s+z*c, rx, ry+b, rz}'; }
bearing() { grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2 | tr -d '+'; }
if has react; then
  echo "== react: a caragor at 30 health; at each of its attacks the guard across its bearing, then a low cut there"
  w "practice remove" 2; HY=0; HP=-15; head_to 0 -15
  drawn || draw_slot
  w "$REST" 0.3; n0=$(lines); w "practice spawn caragor" 1.5; w "fight health 30" 0.5
  since "$n0" '^TRIGGER cmd=fight health' | tail -1 | cut -c1-120
  n1=$(lines); end=$(( $(date +%s) + 45 )); k=0
  while [ "$(date +%s)" -lt $end ]; do
    bdown && break
    na=$(cnt "^BEASTS t=.* attack '[^-][^']*' at Talion")
    for i in $(seq 1 16); do [ "$(cnt "^BEASTS t=.* attack '[^-][^']*' at Talion")" -gt "$na" ] && break; sleep 0.25; done
    B=$(bearing); B=${B:-0}; k=$((k + 1))
    w "hands test R $(rot "$B" 0 -60 55 0 90 0)" 0.7
    [ $k -le 2 ] && T_S=0.05 d "react-guard-$k"
    w "hands test R $(rot "$B" 50 -70 45 0 -90 90)" 0.25; w "hands swing R $(rot "$B" -50 -75 60 0 90 90) 300" 0.5
    w "hands swing R $(rot "$B" 50 -70 45 0 -90 90) 300" 0.4
  done
  w "$REST" 0.3
  echo "react: attacks $(nsince "$n1" "^BEASTS t=.* attack '[^-][^']*' at Talion"), defence contacts $(nsince "$n1" '^DEFENCE contact') hits $(nsince "$n1" '^DEFENCE hit') dodges $(nsince "$n1" '^DEFENCE dodge'); our hits $(nsince "$n1" '^COMBAT hit tier='), kills $(since "$n1" '^COMBAT hit tier=' | grep -c 'dead=0->1'); down $(bdown && echo yes || echo no)"
  since "$n1" '^(DEFENCE contact|COMBAT hit tier=)' | head -4 | cut -c1-300
  bdown && { pass "react: the caragor down by the blade"; sleep 1.5; T_S=0.05 d "react-down"; } || fail "react: the caragor not down"
fi

if has guard; then
  echo "== guard: a caragor; the test guard learns each attack's path on its jaws (DEFENCE's beast weapons), then guards it"
  w "practice remove" 2; HY=0; HP=-15; head_to 0 -15
  drawn || draw_slot
  w "$REST" 0.3; w "practice spawn caragor" 1.5; w "fight guard frame head" 0.4
  for mode in path miss; do
    n1=$(lines); w "fight guard $mode" 0.5
    sleep $([ $mode = path ] && echo 55 || echo 25)
    echo "guard $mode: attacks $(nsince "$n1" "^BEASTS t=.* attack '[^-][^']*' at Talion"); TGUARD learnt $(nsince "$n1" '^TGUARD t=.* learnt'), guards $(nsince "$n1" '^TGUARD t=.* guard for'); DEFENCE contacts $(nsince "$n1" '^DEFENCE contact') hits $(nsince "$n1" '^DEFENCE hit') dodges $(nsince "$n1" '^DEFENCE dodge'); holds $(nsince "$n1" '^HOLD talion start')"
    since "$n1" '^(DEFENCE contact|TGUARD t=.* (learnt|guard for))' | head -5 | cut -c1-260
    [ $mode = path ] && T_S=0.05 d "guard-path-end"
  done
  w "fight guard off" 0.5
fi

if has block; then
  echo "== block: a caragor; at each of its attacks the blade across its line (its bearing from the BEASTS line), then the control (the guard on the far side)"
  w "practice remove" 2; HY=0; HP=-15; head_to 0 -15
  drawn || draw_slot
  w "$REST" 0.3; w "practice spawn caragor" 1.5
  for mode in on off; do
    n1=$(lines); end=$(( $(date +%s) + $([ $mode = on ] && echo 50 || echo 25) ))
    while [ "$(date +%s)" -lt $end ]; do
      na=$(cnt "^BEASTS t=.* attack '[^-][^']*' at Talion")
      for i in $(seq 1 40); do [ "$(cnt "^BEASTS t=.* attack '[^-][^']*' at Talion")" -gt "$na" ] && break; sleep 0.1; done
      [ "$(cnt "^BEASTS t=.* attack '[^-][^']*' at Talion")" -gt "$na" ] || continue
      B=$(grep -E "^BEASTS t=.* attack '[^-][^']*' at Talion" "$L" | tail -1 | grep -oE 'bearing [-+][0-9]+' | grep -oE '[-+][0-9]+' | tr -d '+')
      B=${B:-0}; [ $mode = off ] && B=$((B + 180))
      w "hands test R $(rot "$B" 0 -55 60 0 90 0)" 1.0
      w "$REST" 0.2
    done
    echo "block $mode: attacks $(nsince "$n1" "^BEASTS t=.* attack '[^-][^']*' at Talion"); DEFENCE blocks $(nsince "$n1" '^DEFENCE block') parries $(nsince "$n1" '^DEFENCE parr') hits $(nsince "$n1" '^DEFENCE hit') dodges $(nsince "$n1" '^DEFENCE dodge'); holds $(nsince "$n1" '^HOLD talion start')"
    since "$n1" '^DEFENCE (block|parr|hit)' | head -4 | cut -c1-280
    [ $mode = on ] && T_S=0.05 d "block-on-end"
  done
fi

if has smount; then
  for kind in caragor graug; do
    echo "== smount $kind: the Shadow Mount (the vision first, the beast 10 m ahead with its back turned, A at once)"
    w "practice remove" 2; HY=0; HP=0; head_to 0 0
    t "btn L b 1"; waitfor '^VISION t=.* the flag 0 -> 1' 3; sleep 0.3
    n0=$(lines); w "practice far $kind" 1.0
    for i in 1 2 3; do look && break; sleep 0.3; done
    n1=$(lines); T_S=0.3 t "btn R a 1"; T_S=0.05 t "btn R a 0"
    waitfor "^BEASTS t=.* Talion's mount .*MOUNTED" 6 && { pass "smount $kind: MOUNTED"; T_S=0.05 d "smount-$kind-on"; } || fail "smount $kind: not mounted"
    t "btn L b 0"; sleep 1.5
    since "$n1" "^(VERBS t=.* Talion attack|BEASTS t=.* Talion)" | head -6 | cut -c1-220
    if since "$n1" "^BEASTS t=.* Talion's mount .*MOUNTED" >/dev/null; then
      d "smount-$kind-seat"; w "stick 0 1 0 2.0" 2.5; d "smount-$kind-ride"; w "stick 0.6 0.8 0 1.5" 2.0
      since "$n1" '^(BODYMOMENT|FALLBACK).*(mount|window)' | head -4 | cut -c1-200
      n2=$(lines); w "crouch try 182 600" 3; since "$n2" "^BEASTS t=.* Talion's mount" | head -2 | cut -c1-200
    fi
    HY=0; HP=0; head_to 0 0
  done
fi
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
