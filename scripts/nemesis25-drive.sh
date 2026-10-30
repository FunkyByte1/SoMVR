#!/bin/bash
# nemesis25-drive.sh - build 25's Nemesis round (som-b25-nemesis; docs/re-xr-pipeline.md 104, handoff/HANDOFF-captain.md
# "build 25"): drives one headless boot (scripts/headless_vr.sh with scripts/headset-build25.env, SOMVR_HANDS_TEST=1, the
# layer's test file). captain24-drive.sh's helpers. NM_STEPS, in this order:
#   thrust   per kind (NM_THRUST_KINDS: immune vulnerable): the first eligible member with that stealth rule, his back to
#            Talion (`captain away`), Talion NM_BEHIND (170) cm behind him, standing still, the head at his chest; then
#            `captain thrust` (the right hand on the eyes' line to his spine, the point 20 cm short, the thrust along it)
#   struggle the save's power struggle (the Beast Hunt, 750 m west: `practice go`), B (Start Mission), A on its card; the
#            captain's introduction (where, which path), pictures; the world's captain met (CAPTAIN ... of the world)
#   fight    (after struggle) the sword, the test guard and chase, edge cuts; his health to NM_FLEE_PCT (25) % of his max
#            (`captain hp`) and a tap: his fear and flight watched 20 s; then the cuts until he dies (Talion teleported
#            near him when he ran); his death and its credit, his rune (B), the mission's end
#   death    (after struggle) Talion's health low (`practice hurt`), no guard, the chase onto him: Talion killed by the
#            world's captain - the death in first person (SCREENS), the Nemesis screens (pictures), B through them, the
#            world again; his promotion (his level)
# NM_RUN=RUN drives a running boot. nm-steps.txt: PASS/FAIL/NOTE lines.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${NM_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${NM_STEPS:-thrust struggle fight}
T0=$(date +%s)
R=${NM_RUN:-}
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
export SCR_DIR=${SCR_DIR:-/tmp/som-b25-nemesis/scr}; mkdir -p "$SCR_DIR"; echo "$R" > "$SCR_DIR/run"
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
tap() { x "btn $1 $2 1" "${3:-0.3}"; x "btn $1 $2 0" 0.3; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
nsince() { since "$1" "$2" | wc -l; }
waitsince() { local i; for i in $(seq 1 $((${3:-10} * 4))); do userin && bail "the user is in VR"
  [ "$(nsince "$1" "$2")" -gt 0 ] && return 0; sleep 0.25; done; return 1; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/nm-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/nm-steps.txt"; }
note() { echo "$(date +%T) NOTE $*" | tee -a "$R/nm-steps.txt"; }
eyes() { "$S" eyes "$1" 2>&1 | tail -1; echo "eyes $1" >> "$R/nm-steps.txt"; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
LA="hands test R 30 -10 45 0 -35 0"; RZ=90
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
spawn_cap() { local n0; n0=$(lines); w "practice captain" 2.5
  local l; l=$(since "$n0" '^CAPTAIN t=.* spawn ' | head -1); echo "$l" | cut -c1-300
  case $l in *" ok:"*) pass "$1: the Captain came ($(echo "$l" | grep -oE 'Role_[A-Za-z]+ level [0-9]+'))"; return 0 ;;
    *) fail "$1: no captain: $(echo "$l" | cut -c1-200)"; return 1 ;; esac; }
member_of() { local k=$1; [ "$k" = none ] && k=-
  grep -E '^CAPTAIN member [0-9]+: .*: eligible' "$L" | tail -40 | grep -F "stealth $k," | head -1 | sed -nE 's/^CAPTAIN member ([0-9]+):.*/\1/p'; }
whp() { grep -E "^CAPTAIN t=.* the world's captain's health " "$L" | tail -1 | sed -nE 's/.*health [0-9.na-]+ -> ([0-9.]+) \(of ([0-9.]+)\).*/\1 \2/p'; }
wat() { grep -E "^CAPTAIN t=.* the world's captain('s (health|place)|.* met: )" "$L" | tail -1 | grep -oE 'from Talion at [-0-9]+ [-0-9]+ [-0-9]+' | grep -oE '[-0-9]+ [-0-9]+ [-0-9]+$'; }
wdead() { [ "$(cnt "^CAPTAIN t=.* the world's captain died")" -gt "${WD0:-0}" ]; }
# near: Talion teleported 2.5 m short of the world's captain on the line from Talion (whole cm, the ground's snap)
near() { local p a0; p=$(wat); [ -n "$p" ] || return 1; a0=$(cnt 'arrived at')
  w "practice go $(echo "$p" | awk -v k="${1:-400}" '{dx=-30877-$1; dz=11629-$3; l=sqrt(dx*dx+dz*dz); if (l<1) l=1; printf "%d %d %d", $1+dx/l*k, $2+80, $3+dz/l*k}')" 3
  for i in $(seq 1 20); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 0.5; done; sleep 1; }

# 0. the controls card away, the hands at rest, the sword on the back
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG captain|CAPTAIN site .* MISMATCH)' "$L" | cut -c1-300
m nm-start 0.2

if has thrust; then
  echo "== thrust"
  w "captain stakes off" 0.3; w "captain status" 1.5
  for kind in ${NM_THRUST_KINDS:-immune vulnerable}; do
    idx=$(member_of "$kind"); [ -n "$idx" ] || { note "thrust $kind: no eligible member"; continue; }
    echo "-- thrust into a captain $kind to stealth kills (member $idx)"
    w "practice remove" 1.5; w "captain pick $idx" 0.3; w "captain away" 0.3
    spawn_cap "thrust-$kind" || continue
    drawn || draw_slot; w "$REST" 0.4; sleep 2
    N0=$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1'); n0=$(lines)
    w "verbs behind ${NM_BEHIND:-170}" 0.8; w "execute face" 0.6
    HY=0; HP=0; head_to 0 0; look 0; echo "$(date +%T) his chest: yaw $HY pitch $HP"
    [ "$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1')" -gt "$N0" ] && note "thrust $kind: he noticed Talion before the thrust"
    w "captain thrust ${NM_GAP:-20} ${NM_MPS:-2.5}" 0.3
    waitfor "^CAPTAIN t=.* thrust: (aimed|stopped)" 8; eyes "thrust-$kind-aimed"
    waitfor "^CAPTAIN t=.* thrust: (done|stopped)" 6; sleep 0.3; eyes "thrust-$kind-after"
    since "$n0" "^(STEALTH (kill|t=.* (contact|kill by|stick|alert|touch|air|carried))|CAPTAIN t=.* (thrust|the stealth|stealth thrust|he survives|he died|our ))" | head -12 | cut -c1-420
    if since "$n0" '^CAPTAIN t=.* the stealth thrust turned' >/dev/null; then pass "thrust $kind: turned (no wound, the clank, his alert): $(since "$n0" '^CAPTAIN t=.* thrust: done' | grep -oE 'his health [0-9.]+')"
    elif since "$n0" '^STEALTH kill ' >/dev/null; then pass "thrust $kind: a stealth kill: $(since "$n0" '^STEALTH kill ' | head -1 | grep -oE 'dead=[0-9]->[0-9] health [0-9.]+->[0-9.]+')"
    else fail "thrust $kind: neither a kill nor a turn"; fi
    sleep 3; since "$n0" '^STEALTH t=.* aware orc .*-> 1|^CAPTAIN t=.* his health ' | head -3 | cut -c1-200
    m "nm-thrust-$kind" 0; x "head 0 0 0 s 0.4" 0.6; w "$REST" 0.4
  done
  w "practice remove" 1.5
fi

if has struggle; then
  echo "== struggle"
  w "practice remove" 1.0
  # the sword in the hand before the trip, as a player would carry it into a fight (run 170714: the game's own draw at the
  # fight's start is kept on the back - the sword is the player's to draw - and a draw during a teleport did not take)
  has fight && { for k in 1 2; do drawn && break; draw_slot; sleep 1; done; drawn && echo "the sword in the hand" || note "struggle: the sword not drawn"; w "$LA" 0.4; }
  a0=$(cnt 'arrived at'); w "practice go -30877 2924 11629" 1
  for i in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done
  grep 'arrived at' "$L" | tail -1 | cut -c1-200; sleep 4
  n0=$(lines); P0=$(cnt '^CAPTAIN t=.* a presentation starts'); eyes struggle-at
  grep -E '^PROMPTS show' "$L" | tail -2 | cut -c1-200
  wn0=$(cnt 'window 0 -> 1 by pres')   # B (QuestConfirm, once the game has it active: run 192456: the first B came too early)
  for k in 1 2 3 4 5; do tap R b 0.4; sleep 3; [ "$(cnt 'window 0 -> 1 by pres')" -gt "$wn0" ] && break; done
  [ "$(cnt 'window 0 -> 1 by pres')" -gt "$wn0" ] && { echo "a card: A"; eyes struggle-card; tap R a 0.4; } || fail "struggle: no card after 5 B" 
  nA=$(lines)
  PAT='^CAPTAIN t=.* (a presentation starts|a captain of the world met)'
  if ! waitsince "$nA" "$PAT" ${NM_INTRO_S:-45}; then   # not in 45 s: where the game put him (`captain status`), 15 m from him
    w "captain status" 1.5
    p=$(grep -E '^CAPTAIN member [0-9]+: .*in the world at' "$L" | tail -1 | grep -oE 'in the world at [-0-9]+ [-0-9]+ [-0-9]+' | grep -oE '[-0-9]+ [-0-9]+ [-0-9]+$')
    if [ -n "$p" ]; then note "struggle: a captain in the world at $p, not met: Talion brought 15 m from him"
      w "practice go $(echo "$p" | awk '{printf "%d %d %d", $1, $2 + 60, $3 + 1500}')" 3
    else note "struggle: no captain in the world 45 s after the start"; fi
    waitsince "$nA" "$PAT" 40
  fi
  if [ "$(nsince "$nA" "$PAT")" -gt 0 ]; then
    ml=$(since "$nA" '^CAPTAIN t=.* a captain of the world met' | head -1)
    by=$(echo "$ml" | sed -nE 's/.* ([-+][0-9]+) deg right ([-+][0-9]+) deg up.*/\1/p'); bp=$(echo "$ml" | sed -nE 's/.* ([-+][0-9]+) deg right ([-+][0-9]+) deg up.*/\2/p')
    if [ -n "$by" ] && [ "$(nsince "$nA" '^CAPTAIN t=.* a presentation starts')" -eq 0 ]; then
      if [ "${NM_INTRO_FACE:-1}" = 1 ]; then head_to "$by" "$(awk -v p="$bp" 'BEGIN{if (p<-40) p=-40; if (p>40) p=40; print p}')"; echo "$(date +%T) the head to him: $by $bp"
      else head_to "$(awk -v y="$by" 'BEGIN{y=y+180; if (y>180) y-=360; print y}')" 0; echo "$(date +%T) the head away from him"; fi
    fi
    waitsince "$nA" '^CAPTAIN t=.* a presentation starts' 20
    sleep 1.5; eyes struggle-intro-1; sleep 1.5; eyes struggle-intro-2
    since "$nA" "$PAT" | head -4 | cut -c1-600
    waitfor '^CAPTAIN t=.* the presentation is over' 30
    l=$(since "$nA" '^CAPTAIN t=.* a presentation starts' | head -1)
    case $l in *"in the world"*) pass "struggle: the introduction in the world: $(echo "$l" | grep -oE 'the captain .* off the view' | cut -c1-200)" ;;
      "") note "struggle: no introduction (a presentation) since the start" ;;
      *) note "struggle: the introduction in the window: $(echo "$l" | grep -oE 'the window: .*' | cut -c1-250)" ;; esac
  else fail "struggle: no captain met, no presentation"; fi
  since "$nA" '^(CAPTAIN t=.* a captain of the world met|STORY|BODYMOMENT t=.* cond)' | head -10 | cut -c1-400
  sleep 2; eyes struggle-after
  since "$n0" "^CAPTAIN t=.* a captain of the world met" >/dev/null && pass "struggle: $(since "$n0" "^CAPTAIN t=.* a captain of the world met" | head -1 | grep -oE 'member [0-9]+, [^;]*; [0-9.]+ m from Talion' | cut -c1-200)" || fail "struggle: no world captain met"
fi

if has fight; then
  echo "== fight"
  if [ "${NM_HUNT_END:-0}" = 1 ]; then   # his hunt's end first (Talion away): its outcome screen - in the window? B to close
    nh=$(lines); waitsince "$nh" '^CAPTAIN t=.* a presentation starts' 90 && { sleep 1; eyes hunt-end-1; sleep 2; eyes hunt-end-2; "$S" shot hunt-end | tail -1; }
    since "$nh" '^CAPTAIN t=.* (a presentation starts|the presentation is a screen|the presentation is over)|^BODYMOMENT t=.* window|^FLATFX' | head -6 | cut -c1-300
    since "$nh" '^CAPTAIN t=.* the presentation is a screen' >/dev/null && pass "hunt-end: the outcome screen to the window" || fail "hunt-end: no screen to the window"
    for k in 1 2 3; do since "$nh" '^CAPTAIN t=.* the presentation is over' >/dev/null && break; tap R b 0.4; sleep 3; done
    since "$nh" '^CAPTAIN t=.* the presentation is over' >/dev/null && pass "hunt-end: B closed it" || fail "hunt-end: still up"
    eyes hunt-end-after
  fi
  head_to 0 0; near 400; WD0=$(cnt "^CAPTAIN t=.* the world's captain died"); n0=$(lines); F0=$(date +%s)
  if [ "${NM_KILL:-}" = thrust ]; then   # his kill by the thrust aid at his own spine (the test chase walks to his crowd)
    w "captain hp ${NM_KILL_HP:-8}" 0.5
    for k in $(seq 1 14); do
      userin && bail "the user is in VR"; wdead && break
      d=$(grep -E "^CAPTAIN t=.* the world's captain('s (health|place)|.* met: )" "$L" | tail -1 | grep -oE '[0-9.]+ m from Talion' | grep -oE '^[0-9.]+')
      [ -n "$d" ] && awk -v d="$d" 'BEGIN{exit !(d > 3 || d < 1.4)}' && near 200
      drawn || draw_slot; nt=$(lines); w "captain thrust 10 3" 0.3; waitsince "$nt" '^CAPTAIN t=.* thrust: (done|stopped)' 6
      since "$nt" "^CAPTAIN t=.* (thrust: (aimed|done|stopped)|our .* hit on the captain)" | head -3 | cut -c1-220
    done
    eyes fight-kill
  fi
  drawn || draw_slot; w "$LA" 0.5; w "fight guard learn" 0.5; w "execute chase on" 0.5
  wdead || for k in $(seq 1 10); do ecut; done   # him and his crowd: a first exchange
  eyes fight-1
  since "$n0" "^(CAPTAIN t=.* (the world's captain's health|our )|THREAT t=.*(attack|warn)|FIGHT hurt)" | awk '!s[substr($0,1,40)]++' | head -10 | cut -c1-240
  set -- $(whp); MX=${2:-0}; echo "$(date +%T) his health $1 of $MX; the fight's orcs: $(since "$n0" '^COMBAT hit ' | grep -oE 'orc 0x[0-9a-f]+' | sort -u | wc -l) hit"
  if ! wdead && [ "${MX%.*}" -gt 0 ] 2>/dev/null; then
    v=$(awk -v h="$MX" -v p="${NM_FLEE_PCT:-25}" 'BEGIN{printf "%d", h*p/100}'); w "captain hp $v" 0.5
    w "hands swing R -10 -10 50 0 35 0 1000" 1.2; w "execute chase off" 0.3; w "$REST" 0.4
    np=$(lines); echo "$(date +%T) 12 s: his fear and flight"; sleep 12
    far=$(since "$np" "^CAPTAIN t=.* the world's captain's place" | tail -1 | grep -oE '[0-9.]+ m from Talion' | grep -oE '^[0-9.]+')
    note "fight: 12 s after the 25 % tap: $( [ -n "$far" ] && echo "his place $far m" || echo "no place lines (he stood or fought)"); a presentation: $(nsince "$np" '^CAPTAIN t=.* a presentation starts')"
    drawn || draw_slot; w "$LA" 0.3; w "execute chase on" 0.3
    for k in $(seq 1 ${NM_FIGHT_N:-50}); do
      userin && bail "the user is in VR"; wdead && break
      [ "$(nsince "$n0" '^CAPTAIN t=.* Talion is down')" -gt 0 ] && { note "fight: Talion is down"; break; }
      d=$(grep -E "^CAPTAIN t=.* the world's captain('s (health|place)|.* met: )" "$L" | tail -1 | grep -oE '[0-9.]+ m from Talion' | grep -oE '^[0-9.]+')
      [ -n "$d" ] && awk -v d="$d" 'BEGIN{exit !(d > 8)}' && { near 300; drawn || draw_slot; w "$LA" 0.3; w "execute chase on" 0.3; }
      [ $((k % 8)) -eq 4 ] && { set -- $(whp); [ "${1%.*}" -gt 60 ] 2>/dev/null && w "captain hp 60" 0.4; }
      ecut
    done
  fi
  F1=$(date +%s); wdead && sleep 0.3
  since "$n0" "^CAPTAIN t=.* our (heavy|stab|light) hit" | head -3 | cut -c1-260
  if wdead; then
    l=$(grep -E "^CAPTAIN t=.* the world's captain died" "$L" | tail -1); echo "$l" | cut -c1-600
    pass "fight: he died after $((F1 - F0)) s; $(echo "$l" | grep -oE 'the last death: [^;]*')"
    eyes fight-dead; n1=$(lines); w "execute chase off" 0.3; w "$REST" 0.4; w "fight guard off" 0.3
    for k in 1 2 3; do grep -qE '^PROMPTS show.*(Collectable|[Rr]une)' <(since "$n1" .) && break; sleep 1; done
    tap R b 0.4; sleep 2; since "$n1" '^(USE |PROMPTS show).*(Activate|Collectable|[Rr]une)' | head -3 | cut -c1-200
    waitfor "^CAPTAIN t=.* the world's captain 2 s on" 6; grep -E "^CAPTAIN t=.* the world's captain 2 s on" "$L" | tail -1 | cut -c1-500
    # the struggle's end: its presentations (the outcome screen: B Back), pictures
    for k in 1 2 3 4 5 6; do sleep 3; eyes "fight-end-$k"
      if grep -E '^BODYMOMENT t=.* window' "$L" | tail -1 | grep -q 'window 0 -> 1'; then "$S" shot "fight-end-$k" | tail -1; tap R b 0.4; fi; done
    since "$n1" '^(STORY t|CAPTAIN t=.* (a presentation|the presentation)|BODYMOMENT t=.* window|USE t)' | head -14 | cut -c1-240
  else fail "fight: he is alive ($(whp))"; fi
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "$REST" 0.4
fi

if has death; then
  echo "== death"
  w "fight guard off" 0.3; w "$REST" 0.4
  set -- $(whp); echo "$(date +%T) his health $*"
  n0=$(lines); w "practice hurt 25" 0.5; w "execute chase on" 0.3
  for k in $(seq 1 90); do
    userin && bail "the user is in VR"
    grep -qE "^CAPTAIN t=.* Talion is down" <(since "$n0" .) && break
    d=$(grep -E "^CAPTAIN t=.* the world's captain('s (health|place)|.* met: )" "$L" | tail -1 | grep -oE '[0-9.]+ m from Talion' | grep -oE '^[0-9.]+')
    [ -n "$d" ] && awk -v d="$d" 'BEGIN{exit !(d > 10)}' && near
    sleep 1
  done
  w "execute chase off" 0.3
  since "$n0" "^(CAPTAIN t=.* Talion is down|SCREENS t=.* the death|FIGHT hurt)" | head -8 | cut -c1-300
  since "$n0" "^CAPTAIN t=.* Talion is down" >/dev/null && pass "death: $(since "$n0" "^CAPTAIN t=.* Talion is down" | head -1 | grep -oE 'the world.s captain [0-9.]+ m away \([^)]*\)' | cut -c1-200)" || fail "death: Talion alive"
  for s in 1 2 3 4; do sleep 2; eyes "death-$s"; done
  # the Nemesis screens: pictures, then B (PC_DeathRecover) until the world again
  for k in $(seq 1 14); do
    st=$(grep -E '^SESSION ui ' "$L" | tail -1); echo "$(date +%T) $(echo "$st" | cut -c1-160)"
    echo "$st" | grep -q 'menus=0 paused=0' && [ "$k" -gt 4 ] && break
    "$S" shot "death-screen-$k" | tail -1; eyes "death-screen-$k"; tap R b 0.4; sleep 4
  done
  since "$n0" "^SCREENS t=.* the death" | head -10 | cut -c1-260
  waitfor "^CAPTAIN t=.* the world's captain who was near Talion's death" 40
  since "$n0" "^CAPTAIN t=.* the world's captain (2 s on|who was near)" | head -3 | cut -c1-400
  grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'menus=0 paused=0' && pass "death: back in the world" || fail "death: not back in the world"
  eyes death-back
fi
echo "$(date +%T) done"; grep -E '^CAPTAIN END' "$L" | cut -c1-400; grep -cE '^WARN' "$L"
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
