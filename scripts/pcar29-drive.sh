#!/bin/bash
# pcar29-drive.sh - build 29's Practice > Caragor (som-b29-pcaragor; docs/re-xr-pipeline.md 128, handoff/HANDOFF-beasts.md
# "build 29"): drives one headless boot (scripts/headless_vr.sh with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the
# layer's test file) in the practice meadow. PC_STEPS, in this order (default refs tmpl):
#   refs   Practice > Caragor (`practice far caragor`); `abil refs JumpAttack` (every record reachable from its character
#          records that names a leap, with its path and holders) and `abil rec` of both leaps; its leaps for PC_WATCH s
#   tmpl   each of PC_TMPLS (AI/Templates names, `verbs template`) spawned 10 m off in turn, its leaps for PC_WATCH s
#   chain  Practice > Caragor 10 m off (PC_SPAWNS spawns, 25 s each): the test blade across each leap (`ride guard on`),
#          the counter, the stun; then the mount by the left hand reached out and closed at the game's offer (PC_PRESS=1: 123
#          pressed at the stunned caragor instead, a probe), the domination held, Seat mode, the run, off by B.
#          PC_RECS: records dumped first (`abil rec`/`abil skill`); PC_PRE: grants / `unask:NAME` before; PC_GRANT: the
#          same after a first try without a mount (probes)
# PC_RUN=RUN drives a running boot. Lines: ABIL refs/skill, BEASTS, DEFENCE, RIDE; pc-steps.txt (PASS/FAIL, dump frames).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${PC_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${PC_STEPS:-refs tmpl}
WATCH=${PC_WATCH:-40}
TMPLS=${PC_TMPLS:-Warg_Tame Warg_Rare Warg_Unique_LowXP DLC1_Warg_Rare_TorvinMount}
T0=$(date +%s)
R=${PC_RUN:-}
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
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/pc-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/pc-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/pc-steps.txt"; }
head_to() { t "head 0 0 0 yaw $1 pitch $2 s ${3:-0.3}"; sleep 0.3; }
REST="hands test R 22 -35 -30 0 180 0"
LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"; GUARDP="hands test R 30 -10 45 0 -35 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
mounted() { [ "$(cnt '^RIDE t=.* seated on the')" -gt "${1:-0}" ]; }
leaps() { # $1 = from line, $2 = label: the leap records met since
  echo "$2: $(since "$1" "^BEASTS t=.* attack '[^']*' at Talion" | grep -oE "attack '[^']*'" | sort | uniq -c | sort -rn | head -8 | tr '\n' ' ')" | tee -a "$R/pc-steps.txt"; }

# 0. the controls card away, the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG ride|CONFIG beasts|CONFIG practice caragor)' "$L" | cut -c1-260
M0=$(cnt '^RIDE t=.* seated on the')

if has refs; then
  echo "== refs: Practice > Caragor and its leaps' records"
  w "practice remove" 2
  n1=$(lines); w "practice far caragor" 3
  since "$n1" "^PRACTICE t=.* spawn " | tail -1 | cut -c1-240
  w "abil refs JumpAttack" 3
  w "abil rec AI_Warg_JumpAttack" 1.5
  w "abil rec AI_Warg_JumpAttack_NoCounter" 1.5
  since "$n1" "^ABIL refs t=" | cut -c1-300
  sleep "$WATCH"; leaps "$n1" "refs Practice > Caragor"
fi

if has tmpl; then
  for tp in $TMPLS; do
    echo "== tmpl $tp"
    w "practice remove" 2
    w "verbs template $tp" 0.5
    n1=$(lines); w "practice far caragor" 3
    since "$n1" "^PRACTICE t=.* spawn " | tail -1 | cut -c1-240
    sleep "$WATCH"; leaps "$n1" "tmpl $tp"
  done
  w "verbs template off" 0.5
fi

press_try() { # PC_PRESS=1 (a probe): the game's mount pressed (123, RB) by the test at the stunned caragor as Talion walks
  # at it, whatever the offer - Talion's attack after each press says what the game's selection took
  local n1 i; n1=$(lines)
  w "ride face on" 0.3
  for i in $(seq 1 8); do
    mounted "$M0" && break
    w "ride press 123 400" 0.9
    echo "press $i: $(tail -200 "$L" | grep -E "^(VERBS t=.* Talion attack|BEASTS t=.* Talion: attack)" | tail -1 | grep -oE "attack '[^']*'" | head -1); caragor $(tail -200 "$L" | grep -E "^VERBS t=.* orc .* (now|near) at" | tail -1 | grep -oE "at [0-9]+ cm|state '[^']*'" | tr '\n' ' ')" | tee -a "$R/pc-steps.txt"
    since "$n1" "^RIDE t=.* (mounted|seated|the domination)" | grep -q . && break
    w "stick 0 1 0 0.3" 0.4
  done
  w "ride face off" 0.3
  since "$n1" "^RIDE t=.* (mounted|seated)" | grep -q . && { pass "the mount by 123 pressed at the stunned caragor"; return 0; }
  fail "123 at the stunned caragor: no mount"; return 1
}
mount_try() { # the stunned caragor: the left hand reached out and closed at the game's offer, held through the domination
  [ -n "${PC_PRESS:-}" ] && { press_try; local r=$?; [ $r = 0 ] && { w "hands squeeze L 1" 0.3; waitfor '^RIDE t=.* seated on the' 14 && pass "seated"; w "hands squeeze L 0" 0.3; }; mounted "$M0"; return; }
  local n1; n1=$(lines)
  w "ride face on" 0.3
  waitfor "^RIDE t=.* the game offers the mount" 3 # standing by it first (the kneeling mounts ask 150 units)
  for i in $(seq 1 12); do
    since "$n1" "^RIDE t=.* the game offers the mount" | grep -q . && break
    w "stick 0 1 0 0.4" 0.6
  done
  since "$n1" "^RIDE t=.* the game offers the mount" | grep -q . || { fail "no mount offer"; w "ride face off" 0.3; return 1; }
  pass "the game offers the mount"
  w "hands test L -25 -35 30 0 0 0" 0.2; w "hands squeeze L 1" 0.3
  waitfor '^RIDE t=.* the mount: the left hand' 3 && pass "the mount pressed by the reached-out hand"
  waitfor '^RIDE t=.* seated on the' 14 && pass "seated" || fail "not seated"
  w "hands squeeze L 0" 0.3; w "ride face off" 0.3
  since "$n1" "^(RIDE t=.*(domination|mounted|seated|struggle)|BEASTS t=.* Talion's mount)" | head -8 | cut -c1-240
  mounted "$M0"
}

chain_try() { # $1 = label: the leap across the blade, the counter, the stun, the mount; 0 = mounted. Each spawn 10 m off
  # (its first leap comes straight at Talion), PC_SPAWNS spawns at most, each watched for 25 s
  local sp n1 n0 got=0
  HY=0; HP=-10; head_to 0 -10
  [ -n "$DRAWN" ] || { draw_slot; DRAWN=1; } # (once: a second draw puts the sword back - 127.2)
  w "$GUARDP" 0.6; w "fight guard learn" 0.5; sleep 1.5
  n0=$(lines)
  for sp in $(seq 1 ${PC_SPAWNS:-4}); do
    mounted "$M0" && break
    w "practice remove" 1.5; head_to 0 -10; w "ride guard on" 0.4
    n1=$(lines); w "practice far caragor" 1.5
    if waitfor "^BEASTS t=.* attack 'AI_Warg_JumpAttack[^']*' at Talion" 25; then
      since "$n1" "^BEASTS t=.* attack 'AI_Warg_JumpAttack" | tail -1 | cut -c1-200
      got=$((got + 1)); sleep 2.0
      if since "$n1" "now 'AI_Warg_PC_Sync_Cntr" | grep -q .; then
        pass "$1: the leap countered by the blade across its line"; T_S=0.05 d "countered"
        w "ride guard off" 0.3
        if [ -n "${PC_PICK:-}" ]; then
          for i in 1 2; do w "beasts pick" 0.7; done
          for u in $PC_PICK; do w "beasts unask $u" 0.6; w "beasts pick" 0.7; done
          since "$n1" "^BEASTS t=.* (pick|practice caragor: unask)" | cut -c1-240
        fi
        mount_try && break
      fi
    fi
  done
  w "ride guard off" 0.3
  leaps "$n0" "$1"
  echo "$1: DEFENCE parries $(nsince "$n0" '^DEFENCE parr') counters $(nsince "$n0" "now 'AI_Warg_PC_Sync_Cntr")" | tee -a "$R/pc-steps.txt"
  mounted "$M0"
}

if has chain; then
  echo "== chain: Practice > Caragor - the leap across the blade, the counter, the stun, the mount, the ride, off"
  grep -E "^BEASTS practice caragor:   " "$L" | cut -c1-300
  for r in ${PC_RECS:-PC_Warg_Domination_Mount_Grnd_KD_R}; do case $r in PSS_*) w "abil skill $r" 1.5 ;; *) w "abil rec $r" 1.5 ;; esac; done
  for s in ${PC_PRE:-}; do case $s in unask:*) w "beasts unask ${s#unask:}" 1.2 ;; *) w "beasts grant $s" 1.2 ;; esac; done
  ok=1; chain_try "chain" || ok=0
  if [ $ok = 0 ] && [ -n "${PC_GRANT:-}" ]; then
    echo "== chain again with the grants $PC_GRANT (a probe: the mount's next requirement)"
    for s in $PC_GRANT; do case $s in unask:*) w "beasts unask ${s#unask:}" 1.2 ;; *) w "beasts grant $s" 1.2 ;; esac; done
    chain_try "chain+grant" && ok=1
  fi
  if mounted "$M0"; then
    pass "mounted on Practice > Caragor"
    sleep 1.5; head_to 0 -5; d "ride-seat"
    n2=$(lines); w "stick 0 1 0 3.0" 1.5; d "ride-run"; sleep 1.8
    since "$n2" '^RIDE (press|motion)' | head -3 | cut -c1-220
    n4=$(lines); t "btn R b 1"; T_S=0.05 d "ride-off"; T_S=0.4 t "btn R b 0"
    waitfor '^RIDE t=.* off: standing' 8 && pass "off by B" || fail "not off by B"
    sleep 1.0; d "ride-off-b"
    since "$n4" '^(RIDE|BODYMOMENT .*(released|blink))' | head -6 | cut -c1-220
  else
    fail "not mounted on Practice > Caragor"
  fi
fi
w "ride status" 0.5
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
