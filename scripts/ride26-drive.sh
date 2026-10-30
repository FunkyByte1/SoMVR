#!/bin/bash
# ride26-drive.sh - build 26's riding (som-b26-ride; docs/re-xr-pipeline.md 105, handoff/HANDOFF-beasts.md "build 26"):
# drives one headless boot (scripts/headless_vr.sh with scripts/headset-build26.env, SOMVR_HANDS_TEST=1,
# SOMVR_RIDE_TRACE, the layer's test file) in the practice meadow. R26_STEPS, in this order (default all):
#   grant   `beasts grant` PSS_Warg_Hunter (the leap's counter), PSS_Warg_Rider (riding), PSS_Mount_From_Above - a test
#           aid for the meadow (Big Game is what teaches them); `beasts skills warg` before and after
#   leap    a caragor 10 m off; `ride guard on` (the test blade across its line in each leap's parry window) - DEFENCE's
#           parry presses the game's counter; then the stunned caragor: the left hand on its head and squeezed (the
#           game's grab = its mount); if no mount: B, then the stock MNT_Mount pressed (probes, logged)
#   smount  (only if not mounted yet) the Shadow Mount: the vision, the caragor 10 m ahead marked by the gaze, A
#   ride    seated: the saddle view, the stick ahead (the run), ahead-left (a turn), A (the leap), the right trigger (the
#           bite) at a spawned orc, each mounted command probed; then B (off)
# R26_RUN=RUN drives a running boot. Lines: RIDE ..., DEFENCE ..., BEASTS ..., BODYMOMENT ...; r26-steps.txt (the
# dumps' frames for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${R26_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${R26_STEPS:-grant leap smount ride}
T0=$(date +%s)
R=${R26_RUN:-}
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
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/r26-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/r26-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/r26-steps.txt"; }
head_to() { t "head 0 0 0 yaw $1 pitch $2 s ${3:-0.3}"; sleep 0.3; }
REST="hands test R 22 -35 -30 0 180 0"
LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"; GUARDP="hands test R 30 -10 45 0 -35 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
mounted() { [ "$(cnt '^RIDE t=.* seated on the')" -gt "${1:-0}" ]; }
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p'); [ -n "$OB_Y" ]; }
HY=0; HP=0
look() { orc0 || return 1; HY=$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}'); HP=$(awk -v a="$HP" -v b="$OB_P" -v u="${1:-0}" 'BEGIN{v=a+b+u; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}'); head_to "$HY" "$HP" 0.25; }

# 0. the controls card away, the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG ride|CONFIG beasts)' "$L" | cut -c1-260
M0=$(cnt '^RIDE t=.* seated on the')

if has grant; then
  echo "== grant"
  n0=$(lines); w "beasts skills warg" 1.5
  since "$n0" '^BEASTS skills:' | head -2 | cut -c1-300
  for s in PSS_Warg_Hunter PSS_Warg_Rider PSS_Mount_From_Above; do w "beasts grant $s" 1.5; done
  n0=$(lines); w "beasts skills warg" 1.5
  since "$n0" '^BEASTS (skills:|t=.* grant)' | head -6 | cut -c1-300
fi

mount_try() { # the stunned caragor: the left hand on its head, squeezed; then B; then MNT_Mount
  local n1; n1=$(lines)
  w "verbs aim head" 0.5; w "hands squeeze L 1" 1.2
  waitfor '^RIDE t=.* mounted: a' 3 && { pass "the mount by the left hand's grab"; return 0; }
  w "hands squeeze L 0" 0.4; w "verbs aim off" 0.3
  since "$n1" "^(VERBS t=.*(grab|press|squeeze)|VIN press cmd=123)" | head -4 | cut -c1-200
  t "btn R b 1"; T_S=0.3 t "btn R b 0"
  waitfor '^RIDE t=.* mounted: a' 3 && { pass "the mount by B"; return 0; }
  w "ride press 181 400" 0.3
  waitfor '^RIDE t=.* mounted: a' 3 && { pass "the mount by MNT_Mount"; return 0; }
  return 1
}

if has leap; then
  echo "== leap: a caragor 10 m off; the test blade across its line in each leap's parry window"
  w "practice remove" 2; HY=0; HP=-10; head_to 0 -10
  draw_slot; w "$GUARDP" 0.6; w "fight guard learn" 0.5; sleep 1.5; w "ride guard on" 0.4
  n1=$(lines); w "practice far caragor" 2
  since "$n1" "^PRACTICE t=.* spawn " | tail -1 | cut -c1-200
  end=$(( $(date +%s) + 90 )); got=0
  while [ "$(date +%s)" -lt $end ]; do
    mounted "$M0" && break
    if waitfor "^BEASTS t=.* attack 'AI_Warg_JumpAttack[^']*' at Talion" 6; then
      since "$n1" "^BEASTS t=.* attack 'AI_Warg_JumpAttack" | tail -1 | cut -c1-200
      T_S=0.05 d "leap-$got"; sleep 0.5; T_S=0.05 d "leap-$got-b"; got=$((got + 1))
      sleep 1.5
      l=$(tail -300 "$L" | grep -E "^(DEFENCE (parry|counter|block|hit) .*JumpAttack|VERBS t=.* Talion attack 'PC_Counter_Warg|BEASTS t=.* Talion: attack 'PC_Counter)" | tail -3)
      echo "$l" | cut -c1-260
      if tail -300 "$L" | grep -qE "Talion attack 'PC_Counter_Warg|Talion: attack 'PC_Counter_Warg"; then
        pass "the leap countered by the blade across its line"; T_S=0.05 d "countered"; sleep 1.5
        w "ride guard off" 0.3
        mount_try && { sleep 3; break; }
        w "ride guard on" 0.3
      fi
    fi
    [ $got -ge 6 ] && break
  done
  w "ride guard off" 0.3
  echo "leap: leaps $(nsince "$n1" "^BEASTS t=.* attack 'AI_Warg_JumpAttack[^']*' at Talion") ($(since "$n1" "^BEASTS t=.* attack '[^']*' at Talion" | grep -oE "attack '[^']*'" | sort | uniq -c | sort -rn | head -6 | tr '\n' ' ')); guards $(nsince "$n1" '^RIDE guard t=.* across'); DEFENCE parries $(nsince "$n1" '^DEFENCE parr') presses $(nsince "$n1" '^DEFENCE .*press') hits $(nsince "$n1" '^DEFENCE hit'); counters $(nsince "$n1" "PC_Counter_Warg")"
  since "$n1" '^(RIDE guard|DEFENCE (parr|counter|press|hit|block))' | head -8 | cut -c1-260
  mounted "$M0" && pass "mounted after the leap" || fail "not mounted after the leap"
fi

if has smount && ! mounted "$M0"; then
  echo "== smount: the Shadow Mount (the vision, the caragor 10 m ahead, the gaze, A)"
  w "practice remove" 2; HY=0; HP=0; head_to 0 0
  t "btn L b 1"; waitfor '^VISION t=.* the flag 0 -> 1' 3; sleep 0.3
  w "practice far caragor" 1.0
  for i in 1 2 3; do look && break; sleep 0.3; done
  n1=$(lines); T_S=0.3 t "btn R a 1"; T_S=0.05 t "btn R a 0"
  waitfor '^RIDE t=.* mounted: a' 6 && pass "smount: mounted" || fail "smount: not mounted"
  t "btn L b 0"; sleep 2
  since "$n1" "^(VERBS t=.* Talion attack|BEASTS t=.* Talion|STRIKE)" | head -6 | cut -c1-220
fi

if has ride && mounted "$M0"; then
  echo "== ride"
  HY=0; HP=0; head_to 0 -5
  waitfor '^BODYMOMENT .*released by' 4; sleep 1
  grep -E '^RIDE (t=|bindings)' "$L" | tail -6 | cut -c1-400
  d "ride-seat"; head_to -50 -20; d "ride-seat-left"; head_to 0 -5
  n1=$(lines)
  w "stick 0 1 0 3.0" 1.5; d "ride-run"; sleep 1.6
  w "stick -0.7 0.7 0 2.5" 1.0; d "ride-turn"; sleep 0.6; d "ride-turn-b"; sleep 1.0
  w "stick 0 1 0 2.0" 0.3; t "btn R a 1"; T_S=0.05 d "ride-leap"; T_S=0.4 t "btn R a 0"; T_S=0.05 d "ride-leap-b"; sleep 1.5
  echo "run/turn/leap: $(since "$n1" '^RIDE (press|motion)' | cut -c1-200 | head -4)"
  # the bite at an orc spawned ahead
  w "practice spawn" 2; for i in 1 2 3; do look && break; sleep 0.3; done
  w "stick 0 0.6 0 1.0" 1.2
  n2=$(lines); t "btn R trigger 1"; T_S=0.05 d "ride-bite"; sleep 0.4; T_S=0.4 t "btn R trigger 0"; T_S=0.05 d "ride-bite-b"; sleep 1.5
  since "$n2" "^(RIDE press|VERBS t=.* Talion attack|BEASTS t=.* Talion|COMBAT hit)" | head -5 | cut -c1-220
  # each mounted command probed (what the game does with it)
  for c in 183 184 185 186 187 188; do
    n3=$(lines); w "ride press $c 300" 1.6
    echo "MNT $c: $(since "$n3" "^(VIN press cmd=$c|VERBS t=.* Talion attack|BEASTS t=.* Talion: attack)" | grep -oE "(VIN press cmd=[0-9]+ [A-Za-z_0-9]+|attack '[^']+')" | head -3 | tr '\n' ' ')" | tee -a "$R/r26-steps.txt"
  done
  w "ride status" 0.5
  # off by B
  HY=0; HP=0; head_to 0 -5
  n4=$(lines); t "btn R b 1"; T_S=0.05 d "ride-off"; T_S=0.4 t "btn R b 0"
  waitfor '^RIDE t=.* off: standing' 8 && pass "off by B" || fail "not off by B"
  sleep 1.0; d "ride-off-b"
  since "$n4" '^(RIDE|BODYMOMENT .*(released|blink))' | head -6 | cut -c1-220
fi
w "ride status" 0.5
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
