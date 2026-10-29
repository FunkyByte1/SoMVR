#!/bin/bash
# captain22-drive.sh - build 22's captain round (handoff/HANDOFF-captain.md, docs/re-xr-pipeline.md 88): drives one
# headless boot (scripts/headless_vr.sh with scripts/headset-build22.env, SOMVR_HANDS_TEST=1, the layer's test file).
# KP_STEPS, in this order (default all):
#   intro   `captain status` (the save's hierarchy), `captain stakes off`, the practice's Captain (`practice captain`):
#           the head turned to him, his presentation awaited (CAPTAIN ... a presentation starts: world or window), marks
#           and dumps in it (the eyes: in the world = the meadow and him, not the window)
#   grabfull the left palm at his collar and a squeeze at full health, then at half: the game's grab or its fail
#   fight   the game's grab answers at full health (`cue probe`), the sword drawn, the test chase to him, light slashes
#           and heavy cuts: his health per hit (CAPTAIN his health), his attacks (THREAT / DEFENCE), the rules' lines
#   grab    his health set low (`captain hp`), the game's answers again; the left palm at his collar and a squeeze: the
#           game's grab (Talion's attack PC_Grab_*), what the grab offers (the interrogation)
#   exec    the streak to ready, the right trigger and a heavy cut: EXECUTE kill (forceKill by his traits)
#   kill    heavy cuts to the end: CAPTAIN he died, the member's flags, marks of what follows (the game's screens)
#   death   the next captain with the stakes on (`captain stakes on`): the hands down, no guard - his blows until Talion
#           dies (CAPTAIN Talion is down), marks of the Nemesis screens (the window), `captain status`
# KP_RUN=RUN drives a running boot. kp-steps.txt: PASS/FAIL and the dumps' frames for xr_compose.py.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${KP_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${KP_STEPS:-intro grabfull fight grab exec kill death}
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
LA="hands test R 30 -10 45 0 -35 0"; LMS=${KP_LMS:-1000}
LUNGE_A="hands test R 40 -8 35 0 -90 0"
slash() { if [ $(( $1 % 2 )) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 $LMS" 0.6; else w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.6; fi; }
heavy() { w "$LUNGE_A" 0.5; w "hands swing R -20 -18 62 0 90 0 300" 1.0; w "hands swing R 40 -8 35 0 -90 0 900" 1.0; }
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
hp() { grep -E '^CAPTAIN t=.* his health ' "$L" | tail -1 | sed -nE 's/.*his health [0-9.-]+ -> ([0-9.]+).*/\1/p'; }
hp0() { grep -E '^CAPTAIN t=.* spawn .* ok:' "$L" | tail -1 | sed -nE 's/.* health ([0-9.]+);.*/\1/p'; }
dead() { [ "$(cnt '^CAPTAIN t=.* he died')" -gt "${DEAD0:-0}" ]; }
ready() { grep -E '^EXECUTE ready (off )?t=' "$L" | tail -1 | grep -q '^EXECUTE ready t='; }
pres_wait() { # $1 = seconds: a presentation's start (CAPTAIN line); marks and a dump inside it
  local n0 k; n0=$(cnt '^CAPTAIN t=.* a presentation starts'); for k in $(seq 1 $(( $1 * 2 ))); do
    userin && bail "the user is in VR"; [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$n0" ] && break; sleep 0.5; done
  [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$n0" ] || return 1
  grep -E '^CAPTAIN t=.* a presentation starts' "$L" | tail -1 | cut -c1-400; return 0; }

# 0. the controls card away, the hands at rest, the sword on the back
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG captain|CAPTAIN site .* MISMATCH)' "$L" | cut -c1-300
m kp-start 0.2

if has intro; then
  echo "== intro"
  w "captain status" 1.5; grep -E '^CAPTAIN (member|status)' "$L" | cut -c1-330
  w "captain stakes off" 0.3; w "practice remove" 1.5
  P0=$(cnt '^CAPTAIN t=.* a presentation starts'); n0=$(lines); w "practice captain" 0.3
  for k in $(seq 1 80); do [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$P0" ] && break; sleep 0.5; done
  if [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$P0" ]; then sleep "${KP_INTRO_S:-2.0}"; d "intro-1"; m kp-intro-1 0; d "intro-2"; fi
  l=$(since "$n0" '^CAPTAIN t=.* spawn ' | head -1); echo "$l" | cut -c1-600
  case $l in *" ok:"*) pass "intro: the practice's Captain came ($(echo "$l" | grep -oE 'member 0x[0-9a-f]+ [^;]*' | cut -c1-120))" ;;
    *) fail "intro: no captain: $(echo "$l" | cut -c1-200)"; bail "no captain" ;; esac
  HY=0; HP=0; head_to 0 0; look 0; m kp-spawned 0.3
  if [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$P0" ] || pres_wait 40; then
    l=$(grep -E '^CAPTAIN t=.* a presentation starts' "$L" | tail -1)
    case $l in *"in the world"*) pass "intro: in the world ($(echo "$l" | grep -oE '[0-9.]+ m from the head, [0-9]+ deg off'))" ;;
      *) note "intro: the window ($(echo "$l" | grep -oE 'the window: [^;]*'))" ;; esac
    waitfor '^CAPTAIN t=.* the presentation is over' 30 && grep -E '^CAPTAIN t=.* the presentation is over' "$L" | tail -1 | cut -c1-200
    grep -E '^BODYMOMENT t=.* (cond|act) ' "$L" | tail -6 | cut -c1-260
  else
    w "execute chase off" 0.2
    fail "intro: no presentation in 80 s"
  fi
fi

grab_try() { # $1 = the label: the palm at his collar, the grip closed 3 s, what Talion's attack and the prompts were
  local n0; w "execute face" 0.4; HY=0; HP=0; head_to 0 0; look -10; w "verbs aim head" 0.2; sleep 1.5
  w "cue probe" 1.0; since 0 '^VERBS cue probe' | tail -1 | cut -c1-500
  n0=$(lines); w "hands squeeze L 1" 3.0
  echo "$(date +%T) $1: Talion's attacks $(since "$n0" "^VERBS t=.* Talion attack 'PC_" | grep -oE "attack 'PC_[^']*'" | uniq | tr '\n' ' '); prompts $(since "$n0" '^VERBS t=.* prompt up' | grep -oE "'(UI|Combat|Domination)_[^']*'" | sort -u | tr '\n' ' ')"
  if since "$n0" "Talion attack 'PC_Grab_Into" >/dev/null; then pass "$1: the game's grab took the captain"; m "kp-$1" 0
  elif since "$n0" "Talion attack 'PC_Grab_Attempt[^']*Fail" >/dev/null; then pass "$1: the game's grab failed on the captain (its _Fail)"; m "kp-$1" 0
  else note "$1: neither a grab nor a fail"; fi
  w "hands squeeze L 0" 2.0; w "verbs aim off" 0.2; w "$LREST" 1.5; }
if has grabfull; then
  echo "== grabfull"
  w "execute chase on" 0.3; sleep 3; w "execute chase off" 0.3
  grab_try "grab-full"
  H=$(hp0); [ -n "$H" ] || H=500; v=$(awk -v h="$H" 'BEGIN{printf "%d", h*0.9}'); w "captain hp $v" 0.5
  w "execute chase on" 0.3; sleep 2; w "execute chase off" 0.3
  grab_try "grab-half"
fi

if has grab30; then   # the grab's threshold: 30 % of his health (a level-9 captain: 976)
  echo "== grab30"
  w "captain hp ${KP_HP30:-290}" 0.5; w "execute chase on" 0.3; sleep 2; w "execute chase off" 0.3
  grab_try "grab-30"
fi

if has fight; then
  echo "== fight"
  w "cue probe" 1.2; since 0 '^VERBS cue probe' | tail -2 | cut -c1-900
  draw_slot; w "$LA" 0.5
  w "fight guard learn" 0.5; w "execute chase on" 0.5
  H0=$(hp0); n0=$(lines)
  for k in $(seq 1 24); do
    userin && bail "the user is in VR"
    [ $((k % 3)) = 0 ] && ecut || slash "$k"
    dead && break
  done
  echo "$(date +%T) fight: heavy $(since "$n0" '^COMBAT hit tier=heavy' | grep -oE 'health [0-9.]+->[0-9.]+' | head -6 | tr '\n' ' ')"
  echo "$(date +%T) fight: hits $(nsince "$n0" '^COMBAT hit ') (no damage $(nsince "$n0" '^COMBAT hit .*NO DAMAGE')), his health $H0 -> $(hp); his attacks $(nsince "$n0" '^THREAT (cue|warn) ') THREAT lines, defence verdicts $(since "$n0" '^DEFENCE .*verdict' | grep -oE 'verdict [a-z]+' | sort | uniq -c | tr '\n' ' ')"
  since "$n0" '^CAPTAIN ' | head -8 | cut -c1-300
  since "$n0" '^COMBAT hit ' | head -3 | cut -c1-400
  [ "$(nsince "$n0" '^CAPTAIN .*skipped - his hits are the game')" -ge 1 ] && pass "fight: no stagger health write on the captain" \
    || note "fight: no stagger line (no heavy hit landed?)"
  w "cue probe" 1.2; since 0 '^VERBS cue probe' | tail -1 | cut -c1-900
  m kp-fight 0.2
fi

if has grab; then
  echo "== grab"
  w "execute chase off" 0.3; w "fight guard off" 0.3
  H=$(hp0); [ -n "$H" ] || H=300
  for frac in 50 20; do
    v=$(awk -v h="$H" -v f="$frac" 'BEGIN{printf "%d", h*f/100}'); w "captain hp $v" 0.8
    w "cue probe" 1.2; echo "at $frac% ($v):"; since 0 '^VERBS cue probe' | tail -1 | cut -c1-900
  done
  w "execute face" 0.4; HY=0; HP=0; head_to 0 0; look -10; w "verbs aim head" 0.2; sleep 2
  n0=$(lines); w "hands squeeze L 1" 2.5
  since "$n0" "^VERBS t=.* Talion attack 'PC_(Grab|Interrog|Recoil)|^VERBS .*grab|attack '[^']*GrabAttempt" | head -6 | cut -c1-260
  if since "$n0" "Talion attack 'PC_Grab_Into" > /dev/null; then pass "grab: the game grabbed the captain at 20%"; m kp-grab 0
    sleep 2; since "$n0" "PC_Interrogation|Interrogat|PC_Drain|PC_Brand" | head -4 | cut -c1-260
  else note "grab: no grab at 20% ($(since "$n0" '^VERBS cue t=.*would not grab' | head -1 | grep -oE 'would not grab it: [^(]*' ))"; fi
  w "hands squeeze L 0" 1.5; w "verbs aim off" 0.2; w "$LREST" 1.5
fi

if has exec; then
  echo "== exec"
  RD0=$(cnt '^EXECUTE ready t='); K0=$(cnt '^EXECUTE kill t=')
  drawn || draw_slot   # (the sword in the hand: the fight step may not have run)
  w "captain hp ${KP_EXEC_HP:-600}" 0.5; w "$LA" 0.4; w "execute chase on" 0.4
  w "captain charge" 1.0   # (the game's own charge setter: the execution at once - the streak is the game's, tested before)
  for k in $(seq 1 10); do ready && break; slash "$k"; dead && break; done
  if ready && ! dead; then
    printf '%s\n' "btn R trigger 1" > "$R/xrtest.txt"; sleep 0.25
    for k in $(seq 1 10); do ecut; [ "$(cnt '^EXECUTE kill t=')" -gt "$K0" ] && break
      ready || w "captain charge" 0.8; done   # (an edge cut can come out light: again, the charge kept up)
    printf '%s\n' "btn R trigger 0" > "$R/xrtest.txt"
    grep -E '^CAPTAIN t=.* the execution' "$L" | tail -1 | cut -c1-400
    grep -E '^EXECUTE kill t=' "$L" | tail -1 | cut -c1-500
    [ "$(cnt '^EXECUTE kill t=')" -gt "$K0" ] && pass "exec: the execution on the captain ($(grep -E '^EXECUTE kill t=' "$L" | tail -1 | grep -oE 'forceKill=[0-9]+[^;]*dead=[0-9]->[0-9] health [0-9.]+->[0-9.]+'))" \
      || note "exec: no execution dealt ($(grep -E '^CAPTAIN t=.* the execution' "$L" | tail -1 | grep -oE -- '-> [^(]*'))"
  else note "exec: never ready (or he died first)"; fi
fi

if has kill; then
  echo "== kill"
  DEAD0=$(cnt '^CAPTAIN t=.* he died'); w "captain hp 45" 0.4; w "execute chase on" 0.4
  for k in $(seq 1 30); do dead && break; ecut; done
  w "execute chase off" 0.3
  if dead; then grep -E '^CAPTAIN t=.* he died' "$L" | tail -1 | cut -c1-400
    pass "kill: he died by our blows ($(grep -E '^CAPTAIN t=.* he died' "$L" | tail -1 | grep -oE 'IsDead [0-9], KilledByPlayer [0-9]'))"
    sleep 0.5; m kp-dead-1 0; sleep 2; m kp-dead-2 0; d "dead"
    # his rune at the body: "(B) Pick Up" (SOMVR_USE: B picks); the press and what the game did
    n0=$(lines); t "btn R b 1"; sleep 0.4; t "btn R b 0"; sleep 2
    since "$n0" '^USE |Activate|[Rr]une' | head -4 | cut -c1-240; m kp-rune 0
    sleep 12; w "captain status" 1.5; grep -E '^CAPTAIN member 1:' "$L" | tail -1 | cut -c1-160   # (the member later)
    waitfor '^CAPTAIN t=.* the member 2 s on' 6; grep -E '^CAPTAIN t=.* the member 2 s on' "$L" | tail -1 | cut -c1-300
    pres_wait 8 && { m kp-after-1 0; sleep 2; m kp-after-2 0; }
  else fail "kill: alive after 30 heavy cuts (health $(hp))"; fi
  w "captain status" 1.5
fi

if has vuln; then   # a captain vulnerable to combat finishers (KP_VULN, the member index): the execution kills
  echo "== vuln"
  w "practice remove" 1.5; w "captain pick ${KP_VULN:-5}" 0.3; n0=$(lines); w "practice captain" 2.5
  since "$n0" '^CAPTAIN t=.* spawn ' | head -1 | cut -c1-300
  sleep 12; K0=$(cnt '^EXECUTE kill t='); drawn || draw_slot; w "$LA" 0.4; w "execute chase on" 0.4; w "captain charge" 1.0
  printf '%s\n' "btn R trigger 1" > "$R/xrtest.txt"; sleep 0.25
  for k in 1 2 3 4 5 6; do ecut; [ "$(cnt '^EXECUTE kill t=')" -gt "$K0" ] && break; done
  printf '%s\n' "btn R trigger 0" > "$R/xrtest.txt"; w "execute chase off" 0.3
  grep -E '^CAPTAIN t=.* the execution' "$L" | tail -1 | cut -c1-300
  [ "$(cnt '^EXECUTE kill t=')" -gt "$K0" ] && pass "vuln: $(grep -E '^EXECUTE kill t=' "$L" | tail -1 | grep -oE 'forceKill=[0-9]+[^;]*dead=[0-9]->[0-9] health [0-9.]+->[0-9.]+')" || note "vuln: no execution"
  sleep 3; grep -E '^CAPTAIN t=.* (he died|the member 2 s)' "$L" | tail -2 | cut -c1-300; m kp-vuln-dead 0
fi

if has death; then
  echo "== death"
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "practice remove" 1.5
  w "captain stakes on" 0.3; n0=$(lines); w "practice captain" 2.5
  since "$n0" '^CAPTAIN t=.* spawn ' | head -1 | cut -c1-400
  since "$n0" '^PRACTICE t=.* min health back' | head -1 | cut -c1-200
  w "$LREST" 0.3; w "$REST" 0.3; HY=0; HP=0; head_to 0 0; look 0
  pres_wait 40 && { sleep 0.5; m kp-intro2 0; }
  w "execute chase on" 0.4   # (to him: his blows; nothing defends)
  for k in $(seq 1 120); do userin && bail "the user is in VR"
    [ "$(cnt '^CAPTAIN t=.* Talion is down')" -gt 0 ] && break; sleep 1; done
  w "execute chase off" 0.3
  if [ "$(cnt '^CAPTAIN t=.* Talion is down')" -gt 0 ]; then
    grep -E '^CAPTAIN t=.* Talion is down' "$L" | tail -1 | cut -c1-300; pass "death: Talion died to the captain"
    for k in 1 2 3 4 5 6; do sleep 4; m "kp-death-$k" 0; done
    grep -E '^BODYMOMENT t=.* act ' "$L" | tail -6 | cut -c1-240
  else fail "death: Talion alive after 120 s ($(grep -E '^PRACTICE t=.* min health' "$L" | tail -1 | cut -c1-160))"; fi
  w "captain status" 1.5
fi
echo "$(date +%T) done"; grep -E '^CAPTAIN t=.* (spawn|he died|Talion is down)' "$L" | cut -c1-300
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
