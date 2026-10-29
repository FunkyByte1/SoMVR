#!/bin/bash
# captain24-drive.sh - build 24's captain round (som-b24-captain; docs/re-xr-pipeline.md 99, handoff/HANDOFF-captain.md
# "build 24"): drives one headless boot (scripts/headless_vr.sh with scripts/headset-build24.env, SOMVR_HANDS_TEST=1,
# the layer's test file). captain22-drive.sh's helpers; kill22-drive.sh's stealth approach. KP_STEPS, in this order:
#   measure  `captain gamelog` (the game's own damage lines into RUN/combat-debug.log), `captain status`, the practice's
#            Captain (stakes off), his introduction; `captain tiers` (the four strike records' own damage on him), then
#            `captain charge` and the tiers again (the hit streak's effect); 8 s without a hit (his regeneration)
#   fight    his health full again (`captain hp MAX`), the sword drawn, the test guard, the test chase: edge cuts (and the
#            execution with the right trigger whenever the game's charge is ready) until he dies - the cuts, executions
#            and seconds it took (SOMVR_CAPTAIN_DAMAGE's lines); his death: the member's record of it, his rune (B)
#   army     the pause menu > Sauron's Army (scripts/scr22.sh: the fingertip), a flat shot, back to the world
#   reload   (a boot from an earlier run's save copy, HEADLESS_SAVE_FROM) `captain status`: the dead member's record
#   stealth  per kind (KP_STEALTH_KINDS: immune vulnerable resistant): the first eligible member of that stealth rule, his back
#            to Talion (`captain away`), Talion KP_BEHIND (150) cm behind him, standing
#            still, the head at his chest, the thrust (kill22's poses)
#   flee     the Captain KP_FLEE_PICK (3: the save's Fear_LowHealth + FastRunner), his health to KP_FLEE_PCT (22) % of his max, a light tap: his place for 30 s (CAPTAIN his place), then
#            the test chase after him 20 s
# KP_RUN=RUN drives a running boot. kp-steps.txt: PASS/FAIL/NOTE lines.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${KP_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${KP_STEPS:-measure fight army}
T0=$(date +%s)
R=${KP_RUN:-}
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
export SCR_DIR=${SCR_DIR:-/tmp/som-b24-captain/scr}; mkdir -p "$SCR_DIR"; echo "$R" > "$SCR_DIR/run"
S=$HERE/scr22.sh
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
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
nsince() { since "$1" "$2" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/kp-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/kp-steps.txt"; }
note() { echo "$(date +%T) NOTE $*" | tee -a "$R/kp-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/kp-steps.txt"; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest, low (out of every orc's reach)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
LA="hands test R 30 -10 45 0 -35 0"
RZ=90   # the edge cut (combat21's): the blade's edge leads (a flat blade is a light hit since build 20's stroke)
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
head_to() { t "head 0 0 0 yaw $1 pitch $2 s ${3:-0.3}"; sleep 0.3; }
HY=0; HP=0
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p'); [ -n "$OB_Y" ]; }
look() { orc0 || return 1; HY=$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}'); HP=$(awk -v a="$HP" -v b="$OB_P" -v u="${1:-0}" 'BEGIN{v=a+b+u; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}'); head_to "$HY" "$HP" 0.25; }
hpmax() { grep -E '^CAPTAIN t=.* his health ' "$L" | tail -1 | sed -nE 's/.*\(of ([0-9.]+);.*/\1/p'; }
hp() { grep -E '^CAPTAIN t=.* his health ' "$L" | tail -1 | sed -nE 's/.*his health [0-9.-]+ -> ([0-9.]+).*/\1/p'; }
dead() { [ "$(cnt '^CAPTAIN t=.* he died')" -gt "${DEAD0:-0}" ]; }
ready() { grep -E '^EXECUTE ready (off )?t=' "$L" | tail -1 | grep -q '^EXECUTE ready t='; }
pres_over() { waitfor '^CAPTAIN t=.* the presentation is over' "${1:-30}"; }
spawn_cap() { # $1 = a label; the practice's Captain (KP_PICK: `captain pick` first); the spawn line
  local n0; n0=$(lines); P0=$(cnt '^CAPTAIN t=.* a presentation starts'); w "practice captain" 2.5
  local l; l=$(since "$n0" '^CAPTAIN t=.* spawn ' | head -1); echo "$l" | cut -c1-500
  case $l in *" ok:"*) pass "$1: the Captain came ($(echo "$l" | grep -oE 'Role_[A-Za-z]+ level [0-9]+'))"; return 0 ;;
    *) fail "$1: no captain: $(echo "$l" | cut -c1-200)"; return 1 ;; esac; }

# 0. the controls card away, the hands at rest, the sword on the back
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG captain|CAPTAIN site .* MISMATCH|CONFIG front)' "$L" | cut -c1-400
m kp-start 0.2

if has reload; then
  echo "== reload"
  w "captain status" 1.5; grep -E '^CAPTAIN (member|status)' "$L" | cut -c1-600
  l=$(grep -E '^CAPTAIN member [0-9]+: .*the last death: by the player 1' "$L" | head -3)
  [ -n "$l" ] && pass "reload: a member dead by the player in the save copy: $(echo "$l" | head -1 | grep -oE 'Role_[A-Za-z]+|flags 0x[0-9a-f]+|the last death: [^;]*' | tr '\n' ' ')" \
    || note "reload: no member with the player's kill in its record"
fi

if has measure; then
  echo "== measure"
  w "captain gamelog" 1.0; grep -E '^CAPTAIN t=.* gamelog' "$L" | tail -1 | cut -c1-400
  w "captain status" 1.5; grep -E '^CAPTAIN (member|status)' "$L" | cut -c1-330
  w "captain stakes off" 0.3; w "practice remove" 1.5
  spawn_cap measure || bail "no captain"
  HY=0; HP=0; head_to 0 0
  waitfor '^CAPTAIN t=.* a presentation starts' 40 && { grep -E '^CAPTAIN t=.* a presentation starts' "$L" | tail -1 | cut -c1-300; pres_over 30; }
  sleep 2; w "execute chase on" 0.3; sleep 3; w "execute chase off" 0.3
  n0=$(lines); w "captain tiers" 7.5
  since "$n0" '^CAPTAIN t=.* tiers' | cut -c1-300
  [ "$(nsince "$n0" '^CAPTAIN t=.* tiers: ')" -ge 4 ] && pass "measure: the four strikes' own damage on him" || fail "measure: tiers"
  w "captain charge" 1.0; n0=$(lines); w "captain tiers" 7.5
  since "$n0" '^CAPTAIN t=.* tiers' | cut -c1-300
  n0=$(lines); echo "$(date +%T) 8 s without a hit"; sleep 8
  since "$n0" '^CAPTAIN t=.* his health ' | cut -c1-240
  m kp-measure 0
fi

if has fight; then
  echo "== fight"
  MX=$(hpmax); [ -n "$MX" ] || MX=976
  w "captain hp ${MX%.*}" 0.8
  drawn || draw_slot; w "$LA" 0.5
  w "fight guard learn" 0.5; w "execute chase on" 0.5
  DEAD0=$(cnt '^CAPTAIN t=.* he died'); K0=$(cnt '^EXECUTE kill t='); n0=$(lines); F0=$(date +%s)
  for k in $(seq 1 ${KP_FIGHT_N:-80}); do
    userin && bail "the user is in VR"
    if ready; then x "btn R trigger 1" 0.25; ecut; x "btn R trigger 0" 0.3; else ecut; fi
    dead && break
  done
  F1=$(date +%s)
  echo "$(date +%T) fight: $(nsince "$n0" '^COMBAT hit ') hits ($(nsince "$n0" '^COMBAT hit tier=heavy') heavy, $(nsince "$n0" '^COMBAT hit tier=light') light), our scaled hits $(nsince "$n0" '^CAPTAIN t=.* our heavy hit on the captain'), executions $(( $(cnt '^EXECUTE kill t=') - K0 )), $((F1 - F0)) s; his health $MX -> $(hp)"
  since "$n0" '^CAPTAIN t=.* our (heavy|stab|light) hit' | head -6 | cut -c1-300
  since "$n0" '^CAPTAIN t=.* the execution' | head -2 | cut -c1-300
  if dead; then
    l=$(grep -E '^CAPTAIN t=.* he died' "$L" | tail -1); echo "$l" | cut -c1-700
    pass "fight: he died after $((F1 - F0)) s ($(nsince "$n0" '^CAPTAIN t=.* our heavy hit on the captain') cuts, $(( $(cnt '^EXECUTE kill t=') - K0 )) executions); $(echo "$l" | grep -oE 'the last death: [^;]*')"
    sleep 0.5; m kp-dead-1 0; d "dead"
    n1=$(lines); t "btn R b 1"; sleep 0.4; t "btn R b 0"; sleep 2
    since "$n1" '^USE |Activate|[Rr]une' | head -4 | cut -c1-240; m kp-rune 0
    waitfor '^CAPTAIN t=.* the member 2 s on' 6; grep -E '^CAPTAIN t=.* the member 2 s on' "$L" | tail -1 | cut -c1-500
    sleep 10; w "captain status" 1.5; grep -E '^CAPTAIN member [0-9]+: .*the last death' "$L" | tail -2 | cut -c1-500
  else fail "fight: alive after ${KP_FIGHT_N:-80} actions (health $(hp))"; fi
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "$REST" 0.4
fi

if has army; then
  echo "== army"
  inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'menus=0 paused=0'; }
  "$S" tap L menu 0.3; sleep 3; "$S" shot pause | tail -1
  "$S" touch 0.12 0.231; sleep 3; "$S" shot army | tail -1
  w "screens status" 1.0; grep -E '^SCREENS t=.* status' "$L" | tail -1 | cut -c1-300
  for i in 1 2 3 4; do inworld && break; "$S" off; "$S" tap R b; sleep 1.5; done
  inworld && pass "army: back in the world" || { "$S" tap L menu 0.3; sleep 2; note "army: the menu button to close"; }
fi

STL_PRE="hands test R 20 -27 -18 12 0 0"; STL_READY="hands swing R 16 -23 0 12 0 0 500"; STL_THRUST="hands swing R 5 -11 54 12 0 0 180"
member_of() { # $1 = a stealth rule word (immune vulnerable resistant none): the first eligible member's index with it
  local k=$1; [ "$k" = none ] && k=-
  grep -E '^CAPTAIN member [0-9]+: .*: eligible' "$L" | tail -40 | grep -F "stealth $k," | head -1 | sed -nE 's/^CAPTAIN member ([0-9]+):.*/\1/p'; }
if has stealth; then
  echo "== stealth"
  w "captain stakes off" 0.3; w "captain status" 1.5
  grep -E '^CAPTAIN member [0-9]+: ' "$L" | tail -30 | grep -oE '^CAPTAIN member [0-9]+: [^;]*eligible[^;]*; combat finishers [a-z]+, stealth [a-z]+' | cut -c1-160 | head -30
  for kind in ${KP_STEALTH_KINDS:-immune vulnerable resistant}; do
    idx=$(member_of "$kind"); [ -n "$idx" ] || { note "stealth $kind: no eligible member"; continue; }
    echo "-- stealth on a captain $kind to stealth kills (member $idx)"
    w "practice remove" 1.5; w "captain pick $idx" 0.3; w "captain away" 0.3
    spawn_cap "stealth-$kind" || continue
    drawn || draw_slot; w "$REST" 0.4; sleep 2
    N0=$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1'); n0=$(lines)
    # (a captain's body is larger than an infantry's: at the infantry's stand-off (118 cm) the ready blade was already
    # in his back - run 120340's "carried ... 43-58 cm in". So Talion is put KP_BEHIND (175) cm behind him, no sneak)
    # (build 24's last try, run 123314's lesson: crouched, the thrust's ray passed by his hip - he is a head taller than an
    # infantry. Standing still (the stealth rule: crouched or slow), the head turned to his chest, the thrust along it)
    w "$STL_PRE" 0.4; w "verbs behind ${KP_BEHIND:-150}" 0.6; w "execute face" 0.6
    HY=0; HP=0; head_to 0 0; look 0; echo "$(date +%T) his chest: yaw $HY pitch $HP"; w "$STL_READY" 0.8
    w "cue probe" 1.0; since 0 '^VERBS cue probe' | tail -1 | cut -c1-600
    [ "$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1')" -gt "$N0" ] && note "stealth $kind: he noticed Talion before the thrust"
    d "stealth-$kind-ready"; w "$STL_THRUST" 0.25; d "stealth-$kind-in"; sleep 1.5; d "stealth-$kind-after"
    since "$n0" '^(STEALTH (kill|t=.* (contact|kill by|stick|alert))|CAPTAIN t=.* (the stealth|stealth thrust|he survives|he died|our ))' | head -8 | cut -c1-400
    if since "$n0" '^CAPTAIN t=.* the stealth thrust turned' >/dev/null; then pass "stealth $kind: the thrust turned (no wound, the clank, his alert)"
    elif since "$n0" '^STEALTH kill ' >/dev/null; then pass "stealth $kind: $(since "$n0" '^STEALTH kill ' | head -1 | grep -oE 'dead=[0-9]->[0-9] health [0-9.]+->[0-9.]+'); stick: $(since "$n0" '^STEALTH t=.* kill by' | head -1 | grep -oE 'the blade stays[^;]*|no stick')"
    else note "stealth $kind: neither a kill nor a turn"; fi
    m "kp-stealth-$kind" 0; sleep 2
    x "head 0 0 0 s 0.4" 0.6; w "$REST" 0.4
  done
  w "practice remove" 1.5
fi

if has flee; then
  echo "== flee"
  w "captain stakes off" 0.3; w "practice remove" 1.5
  [ -n "${KP_FLEE_PICK:-3}" ] && w "captain pick ${KP_FLEE_PICK:-3}" 0.3   # (member 3 of the save: Fear_LowHealth, Quirk_FastRunner)
  spawn_cap flee || bail "no captain"
  waitfor '^CAPTAIN t=.* a presentation starts' 30 && pres_over 30
  sleep 3; MX=$(hpmax); [ -n "$MX" ] || MX=976
  v=$(awk -v h="$MX" -v p="${KP_FLEE_PCT:-22}" 'BEGIN{printf "%d", h*p/100}'); n0=$(lines); w "captain hp $v" 0.5
  # (a light tap now - the flat of the blade: his fear may be judged when he takes damage; run 120340's edge cut killed him)
  drawn || draw_slot; w "$LA" 0.4; w "execute chase on" 0.4; w "hands swing R -10 -10 50 0 35 0 1000" 1.2; w "execute chase off" 0.3; w "$REST" 0.4
  echo "$(date +%T) 30 s: his place"; for k in 1 2 3 4 5 6; do sleep 5; m "kp-flee-$k" 0; done
  since "$n0" '^CAPTAIN t=.* (his place|his health|he left the world|he died)' | head -20 | cut -c1-260
  far=$(since "$n0" '^CAPTAIN t=.* his place' | tail -1 | grep -oE '[0-9.]+ m from Talion' | grep -oE '^[0-9.]+')
  [ -n "$far" ] && note "flee: his place after 30 s $far m from Talion" || note "flee: no place lines (he did not move 1 m, or not low)"
  w "execute chase on" 0.3; for k in 1 2 3 4; do sleep 5; m "kp-chase-$k" 0; done; w "execute chase off" 0.3
  since "$n0" '^CAPTAIN t=.* (his place|he left the world|he died)' | tail -6 | cut -c1-260
fi
echo "$(date +%T) done"; grep -E '^CAPTAIN t=.* (spawn|he died|Talion is down)' "$L" | cut -c1-300
grep -E '^CAPTAIN END' "$L" | cut -c1-400
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
