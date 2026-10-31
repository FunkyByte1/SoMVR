#!/bin/bash
# nemesis27-drive.sh - build 27's Nemesis round beyond one captain (som-b27-nemesis2; docs/re-xr-pipeline.md 116,
# handoff/HANDOFF-captain.md "build 27"): drives one headless boot (scripts/headless_vr.sh with scripts/headset-build26.env,
# SOMVR_HANDS_TEST=1, the layer's test file, SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0; best from the furthest save,
# HEADLESS_SAVE_FROM). nemesis25-drive.sh's helpers. NM_STEPS, in this order:
#   survey   `captain status`, `practice status nav` (the save's Nemesis markers), the warchief board (the right stick's
#            click held, SOMVR_ARMY): a picture; the menu button out
#   struggle the Beast Hunt (nemesis25's): B, A, his introduction
#   fight    (after struggle) `execute chase captain` (the test chase walks to the world's captain, not his crowd),
#            `captain hp` to NM_KILL_HP when near, edge cuts until he dies: the credit, the rune (B), the struggle's end (B)
#   army     Sauron's Army from the pause menu by the laser: a picture (his slot after the kill)
#   runes    Weapons & Runes by the laser: the sword's rune row, the grid, a rune picked and equipped: pictures
#   death    `practice mortal on`, Talion hurt, orcs at him: the death, the Nemesis screens (B), the respawn; then
#            `practice status nav` and `captain status` again (what time's passing brought: new struggles, promotions)
# NM_RUN=RUN drives a running boot. nm-steps.txt: PASS/FAIL/NOTE lines.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${NM_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${NM_STEPS:-survey struggle fight army runes death}
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
export SCR_DIR=${SCR_DIR:-/tmp/som-b27-nemesis2/scr}; mkdir -p "$SCR_DIR"; echo "$R" > "$SCR_DIR/run"
S=$HERE/scr22.sh
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
arrived() { grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null || [ "$(grep -cE '^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' "$L" 2>/dev/null)" -ge 2 ]; }
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  arrived && break; sleep 3
done
arrived || bail "no arrival"
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
near() { local p a0 i; w "captain where" 0.8; p=$(wat); [ -n "$p" ] || return 1; a0=$(cnt 'arrived at')
  w "practice go $(echo "$p" | awk -v k="${1:-400}" -v MX="${MX:--30877}" -v MZ="${MZ:-11629}" '{dx=MX-$1; dz=MZ-$3; l=sqrt(dx*dx+dz*dz); if (l<1) l=1; printf "%d %d %d", $1+dx/l*k, $2+80, $3+dz/l*k}')" 3
  for i in $(seq 1 20); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 0.5; done; sleep 1; }

# 0. the controls card away, the hands at rest, the sword on the back
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG captain|CAPTAIN site .* MISMATCH)' "$L" | cut -c1-300
m nm27-start 0.2
X=$R/xr-layer.log
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'menus=0 paused=0'; }
sshot() { "$S" shot "$1" | tail -1; echo "shot $1" >> "$R/nm-steps.txt"; }
laser() { "$S" aimto "$1" "$2" | tail -1; sleep 0.5; "$S" trig; sleep "${3:-2.5}"; }
pause_open() { inworld || return 0; tap L menu 0.3; sleep 3; }
pause_close() { for i in 1 2 3 4; do inworld && return 0; "$S" off > /dev/null; tap R b 0.3; sleep 1.5; done; inworld || { tap L menu 0.3; sleep 2; }; }
board() { # the warchief board: the right stick's click held 1.0 s (SOMVR_ARMY)
  local n0; n0=$(lines); x "btn R stick 1" 1.0; x "btn R stick 0" 3.0
  since "$n0" '^INPUT t=.* army ' | head -1 | cut -c1-200; sshot "$1"; eyes "$1"; }
nav() { local n0; n0=$(lines); w "practice status nav" 2
  since "$n0" "^PRACTICE nav [0-9]+ '" | grep -vE "'(Collectible|FT_|RessurrectionStone|ObjectiveGoldNoClutter)" | cut -c1-200 | head -30; }

if has survey; then
  echo "== survey"
  w "captain status" 2; nav
  grep -E '^CAPTAIN member [0-9]+: ' "$L" | tail -27 | grep -E 'warchief|in the world' | cut -c1-260
  board survey-board
  for k in 1 2 3 4 5; do x "stick L 1 0" 0.4; x "stick L 0 0" 1.2; done; sshot survey-board-r5
  tap L menu 0.3; sleep 2; inworld || pause_close
  inworld && pass "survey: the board opened and left" || fail "survey: not back in the world"
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
    [ -z "${NM_FAST:-}" ] && { sleep 1.5; eyes struggle-intro-1; sleep 1.5; eyes struggle-intro-2; }
    since "$nA" "$PAT" | head -4 | cut -c1-600
    waitfor '^CAPTAIN t=.* the presentation is over' $( [ -n "${NM_FAST:-}" ] && echo 8 || echo 30 )
    l=$(since "$nA" '^CAPTAIN t=.* a presentation starts' | head -1)
    case $l in *"in the world"*) pass "struggle: the introduction in the world: $(echo "$l" | grep -oE 'the captain .* off the view' | cut -c1-200)" ;;
      "") note "struggle: no introduction (a presentation) since the start" ;;
      *) note "struggle: the introduction in the window: $(echo "$l" | grep -oE 'the window: .*' | cut -c1-250)" ;; esac
  else fail "struggle: no captain met, no presentation"; fi
  since "$nA" '^(CAPTAIN t=.* a captain of the world met|STORY|BODYMOMENT t=.* cond)' | head -10 | cut -c1-400
  [ -z "${NM_FAST:-}" ] && { sleep 2; eyes struggle-after; }
  since "$n0" "^CAPTAIN t=.* a captain of the world met" >/dev/null && pass "struggle: $(since "$n0" "^CAPTAIN t=.* a captain of the world met" | head -1 | grep -oE 'member [0-9]+, [^;]*; [0-9.]+ m from Talion' | cut -c1-200)" || fail "struggle: no world captain met"
fi

lw_since() { since "$1" '^VERBS t=.* last words: ' | head -1; }
paused() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'paused=1'; }
ups() { since "$1" '^VERBS t=.* prompt up: action 1(6[1-9]|70) ' | cut -c1-160 | awk '!s[$0]++' | tail -4; }
# the captain's finisher struggle after an Eliminate (SOMVR_STRUGGLE_FINISHER): the right hand steers ($2: up right down
# left, 30 cm from where it was), the left hand thrusts until it is over
fin_answer() { local n0=$1 dir=$2 TG=$3 k hx hy l
  waitsince "$n0" '^STRUGGLE t=.* start' 8 || { note "$TG: no struggle after the Eliminate"; return 1; }
  since "$n0" '^STRUGGLE t=.* start' | head -1 | cut -c1-260
  if [ "$dir" = stab ]; then # the sword hand's thrust at him (SOMVR_STRUGGLE_FINISHER's stab), twice at most
    sleep 0.3   # (no picture first: a layer dump takes seconds, the struggle 4 s - boot 172517's stab came after it)
    for k in 1 2; do since "$n0" '^STRUGGLE t=.* over after' > /dev/null && break
      w "hands test R 20 -30 10 0 0 0" 0.35; w "hands swing R 8 -45 75 0 0 0 110" 0.5; done
    eyes "$TG-stabbed"
    since "$n0" '^STRUGGLE t=.* (the finisher|\+[0-9.]+ s: the finisher)' | head -3 | cut -c1-260
  else
  case $dir in up) hx=25; hy=-5 ;; right) hx=55; hy=-35 ;; down) hx=25; hy=-65 ;; *) hx=-5; hy=-35 ;; esac
  sleep 0.3; w "hands test R $hx $hy 30 0 0 0" 0.25; eyes "$TG-ring"
  for k in $(seq 1 40); do since "$n0" '^STRUGGLE t=.* over after' > /dev/null && break; userin && bail "the user is in VR"
    w "hands swing L -25 -33 72 0 0 0 110" 0.15; w "hands test L -25 -35 25 0 0 0" 0.05; done
  fi
  for k in $(seq 1 20); do since "$n0" '^STRUGGLE t=.* over after' > /dev/null && break; sleep 0.25; done
  w "$LREST" 0.2; w "$LA" 0.2
  l=$(since "$n0" '^STRUGGLE t=.* over after' | head -1); echo "$l" | cut -c1-300
  echo "  presses: $(nsince "$n0" '^STRUGGLE t=.* (press|pressed)'), the aim lines: $(nsince "$n0" '^STRUGGLE t=.* the aim [-+]')"
  case $l in *PCWin*) pass "$TG: the finisher's struggle won ($dir): $(echo "$l" | grep -oE "Talion's attack now '[^']*'")" ;;
    *) note "$TG: the finisher's struggle not won ($dir): $(echo "$l" | grep -oE "result\(\+8c\) [0-9]+|Talion's attack now '[^']*'" | tr '\n' ' ')" ;; esac; }

# one captain of the world fought to his Last Words, then the choice $1 (blade|b|interrogate); $2 the pictures' tag
fight_one() {
  local CH=$1 TG=$2 n0 F0 F1 k d l
  head_to 0 0; near 250; WD0=$(cnt "^CAPTAIN t=.* the world's captain died"); n0=$(lines); F0=$(date +%s)
  drawn || draw_slot; w "$LA" 0.3; w "fight guard learn" 0.3; w "execute chase captain" 0.3; w "captain hp ${NM_KILL_HP:-60}" 0.3
  for k in $(seq 1 ${NM_FIGHT_N:-90}); do
    userin && bail "the user is in VR"; wdead && break; [ -n "$(lw_since "$n0")" ] && break
    [ "$(nsince "$n0" "^CAPTAIN t=.* the world's captain left the world")" -gt 0 ] && { note "$TG: he left the world"; return 1; }
    # beaten: his last line comes - no more cuts (a player reads the choice; boot 174916's next cut chose Kill)
    [ "$(nsince "$n0" "attack 'LastWord")" -gt 0 ] && { sleep 0.5; continue; }
    d=$(grep -E "^CAPTAIN t=.* the world's captain('s (health|place)|.* met: )" "$L" | tail -1 | grep -oE '[0-9.]+ m from Talion' | grep -oE '^[0-9.]+')
    [ $((k % 12)) = 0 ] && [ "$(nsince "$n0" "^CAPTAIN t=.* our .* hit on the captain")" -eq 0 ] && d=99
    [ $((k % 6)) = 0 ] && ! drawn && { draw_slot; w "$LA" 0.3; }   # (boot 170418: the draw missed once, no blade 4 min)
    [ -n "$d" ] && awk -v d="$d" 'BEGIN{exit !(d > 10)}' && { near 250; drawn || draw_slot; w "$LA" 0.3; w "execute chase captain" 0.3; }
    if [ -n "$d" ] && awk -v d="$d" 'BEGIN{exit !(d < 4)}'; then
      set -- $(whp); [ "${1%.*}" -gt "${NM_KILL_HP:-60}" ] 2>/dev/null && w "captain hp ${NM_KILL_HP:-60}" 0.4
    fi
    ecut
  done
  w "execute chase off" 0.3; w "fight guard off" 0.3
  echo "$(date +%T) our hits on him: $(nsince "$n0" "^CAPTAIN t=.* our .* hit on the captain"), on others: $(nsince "$n0" '^COMBAT hit ')"
  l=$(lw_since "$n0")
  if [ -n "$l" ]; then
    echo "$l" | cut -c1-400; pass "$TG: his Last Words answered as an interrogation (the choices kept: $(nsince "$n0" '^PROMPTS show .* action 16[14] .*: kept'))"
    since "$n0" "^(STORY t=.* subtitle ch=1 '|PROMPTS show .* action 16[1-9])" | cut -c1-200 | awk '!s[$0]++' | head -5
    sleep 1; eyes "$TG-lw"; sshot "$TG-lw"
    case $CH in
    interrogate)
      local ni; ni=$(lines); x "btn L trigger 1" 0.3; x "btn L trigger 0" 3
      since "$ni" '^VERBS t=.* choice' | head -2 | cut -c1-200
      for k in 1 2 3 4 5 6 7 8; do paused && break; sleep 0.5; done
      # "Dominate" (the Last Words' LT at this point of the story): the brand first, then the Domination's Gain Intel
      for k in 1 2 3; do paused && break; sleep 2; x "btn L trigger 1" 0.3; x "btn L trigger 0" 1.5
        for j in 1 2 3 4 5 6 7 8; do paused && break; sleep 0.5; done; done
      since "$ni" '^(VERBS t=.* (choice|prompt up)|PROMPTS show)' | cut -c1-180 | awk '!s[$0]++' | head -8
      if paused; then sshot "$TG-intel"; eyes "$TG-intel"
        for u in 1 2 3; do x "stick L 0 1" 0.3; x "stick L 0 0" 0.8; done; sshot "$TG-intel-up"
        tap R a 0.3; sleep 4; sshot "$TG-intel-a"; paused && { tap R a 0.3; sleep 4; sshot "$TG-intel-a2"; }
        pass "$TG: the Interrogate gave the GAIN INTEL screen"
      else note "$TG: no paused screen after the Interrogate"; fi
      sleep 2; eyes "$TG-after-intel"; ups "$ni"
      since "$ni" '^(STORY t=.* subtitle ch=1 |VERBS t=.* (choice|button|Talion attack))' | cut -c1-200 | awk '!s[$0]++' | head -8
      # what is left: the Eliminate by the blade, a Domination's Dismiss by B
      for k in 1 2 3; do wdead && break
        if ups "$ni" | grep -q "action 161 .*Eliminate"; then ecut; ecut; sleep 1.5
        elif ups "$ni" | grep -qE 'Cancel|Dismiss'; then tap R b 0.4; sleep 3; else sleep 2; fi; done ;;
    b) local tr; for tr in 1 2 3 4; do
         local nb; nb=$(lines); w "hands test R 25 -35 30 0 0 0" 0.4
         if [ $((tr % 2)) = 1 ]; then tap L a 0.4; else tap R b 0.4; fi   # the Touch's X ("Kill (X)"), then B
         since "$nb" '^VERBS t=.* button' | head -1 | cut -c1-160
         fin_answer "$nb" "$(echo ${NM_FIN:-stab stab up right} | cut -d' ' -f$tr)" "$TG-try$tr"; sleep 3
         wdead && break
         set -- $(whp); echo "$(date +%T) after try $tr: his health $1 of $2"
         [ "${1%.*}" -gt 100 ] 2>/dev/null || break
         # he got up whole: beaten again to his Last Words
         local nr; nr=$(lines); w "$LA" 0.3; w "fight guard learn" 0.3; w "execute chase captain" 0.3
         for k in $(seq 1 60); do wdead && break; [ "$(nsince "$nr" '^VERBS t=.* last words: ')" -gt 0 ] && break
           set -- $(whp); [ "${1%.*}" -gt "${NM_KILL_HP:-60}" ] 2>/dev/null && w "captain hp ${NM_KILL_HP:-60}" 0.3; ecut; done
         w "execute chase off" 0.3; w "fight guard off" 0.3; sleep 1
         [ "$(nsince "$nr" '^VERBS t=.* last words: ')" -gt 0 ] || break
       done ;;
    *) for k in 1 2 3 4; do wdead && break; ecut; sleep 0.8; done ;;
    esac
  else note "$TG: no Last Words line"; fi
  for k in 1 2 3 4 5 6 7 8 9 10; do wdead && break; sleep 1; done
  F1=$(date +%s); wdead && sleep 1.5
  if wdead; then
    l=$(grep -E "^CAPTAIN t=.* the world's captain died" "$L" | tail -1); echo "$l" | cut -c1-600
    case $l in *"by the player 1"*) pass "$TG: he died, the player's kill, after $((F1 - F0)) s; $(echo "$l" | grep -oE 'the last death: [^;]*')" ;;
      *) fail "$TG: he died, not the player's: $(echo "$l" | grep -oE 'the last death: [^;]*')" ;; esac
    eyes "$TG-dead"; n1=$(lines); w "$REST" 0.4
    for k in 1 2 3 4 5 6; do since "$n1" '^PROMPTS show' | grep -qE 'Collectable|[Rr]une|Pick' && break; sleep 1; done
    since "$n1" '^PROMPTS show' | head -3 | cut -c1-200; eyes "$TG-rune"; sshot "$TG-rune"
    for k in 1 2 3 4; do tap R b 0.4; sleep 2; since "$n1" '^USE t=.* pick prompt down' > /dev/null && break; done
    since "$n1" '^(USE |COLLECT).*' | head -6 | cut -c1-220
    since "$n1" '^USE .*(Activate|Collectable)' > /dev/null && pass "$TG: B picked his rune up" || note "$TG: no pick line (see the pictures)"
    waitfor "^CAPTAIN t=.* the world's captain 2 s on" 6; grep -E "^CAPTAIN t=.* the world's captain 2 s on" "$L" | tail -1 | cut -c1-500
    return 0
  fi
  note "$TG: he is alive after the choice ($(whp)); $(grep -E "^CAPTAIN t=.* the world's captain" "$L" | tail -1 | cut -c1-200)"; return 1
}

# a power struggle from its marker (X Y Z): B (Start Mission) until its card, a picture of the card, A; the captains met
struggle_at() { local X=$1 Y=$2 Z=$3 TG=$4 a0 wn0 nA k
  MX=$X; MZ=$Z; head_to 0 0
  a0=$(cnt 'arrived at'); w "practice go $X $((Y + 50)) $Z" 1
  for k in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done
  grep 'arrived at' "$L" | tail -1 | cut -c1-160; sleep 4; eyes "$TG-at"
  wn0=$(cnt 'window 0 -> 1 by pres')
  for k in 1 2 3 4 5; do tap R b 0.4; sleep 3; [ "$(cnt 'window 0 -> 1 by pres')" -gt "$wn0" ] && break; done
  if [ "$(cnt 'window 0 -> 1 by pres')" -gt "$wn0" ]; then sshot "$TG-card"; tap R a 0.4; else fail "$TG: no card after 5 B"; return 1; fi
  nA=$(lines)
  # a struggle plays out in 45-60 s after its captain comes (boot 20261030-154016: each left the world or won by then):
  # the player runs to him at once - Talion brought 3 m from him as soon as he is met, the introduction where he stands
  waitsince "$nA" '^CAPTAIN t=.* a captain of the world met' 45 && near 300
  sleep 2; eyes "$TG-intro"; waitfor '^CAPTAIN t=.* the presentation is over' 15
  since "$nA" "^(CAPTAIN t=.* (a presentation starts|the presentation|the world's captains|a captain of the world met)|STORY t=.* subtitle ch=1 ')" | cut -c1-330 | head -14
  since "$nA" "^CAPTAIN t=.* a captain of the world met" > /dev/null && pass "$TG: $(since "$nA" "^CAPTAIN t=.* a captain of the world met" | head -1 | grep -oE 'member [0-9]+, [^;]*; [0-9.]+ m from Talion' | cut -c1-160)" || { fail "$TG: no world captain met"; return 1; }
  eyes "$TG-after"; return 0; }

if has struggles; then
  echo "== the save's power struggles, each from its marker"
  n0=$(lines); w "practice status nav" 2
  since "$n0" "^PRACTICE nav [0-9]+ 'powerStruggle'" | sed -E 's/.* at \((-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\), ([0-9.]+) m.*/\4 \1 \2 \3/' | sort -n > "$R/nm-struggles.txt"
  cat "$R/nm-struggles.txt"; si=0
  CHS=(${NM_CHOICES:-b interrogate b})
  mapfile -t SL < "$R/nm-struggles.txt"
  for sl in "${SL[@]}"; do read -r dd X Y Z <<< "$sl"; si=$((si + 1)); [ $si -gt ${NM_NSTRUG:-3} ] && break
    echo "-- struggle $si at $X $Y $Z ($dd m): the choice ${CHS[$((si - 1))]:-b}"
    w "captain follow near" 0.3
    struggle_at "$X" "$Y" "$Z" "s$si" && fight_one "${CHS[$((si - 1))]:-b}" "s$si-k"
    for k in 1 2 3 4; do sleep 3
      if grep -E '^BODYMOMENT t=.* window' "$L" | tail -1 | grep -q 'window 0 -> 1'; then sshot "s$si-end-$k"; tap R b 0.4; fi; done
    w "execute chase off" 0.3; w "fight guard off" 0.3; w "$REST" 0.4
  done
fi

if has warchief; then   # the identified warchief's next step (boot 224857's save): his mission start point, as found
  echo "== warchief: the save's markers, a new one visited"
  n0=$(lines); w "practice status nav" 2
  since "$n0" "^PRACTICE nav [0-9]+ '" | grep -vE "'(Collectible|FT_|RessurrectionStone|ObjectiveGoldNoClutter|Goblins|Troll|OrcHealthBar)" | cut -c1-160 | tee "$R/nm-nav.txt"
  l=$(grep -vE "'(powerStruggle|goldenL|goldenT|confrontationBranded|resistance_QuestGiver|Objective[A-Za-z_]*)'" "$R/nm-nav.txt" | head -1)
  [ -z "$l" ] && l=$(grep -E "'confrontationBranded'" "$R/nm-nav.txt" | head -1)
  echo "the marker: $l"
  read -r X Y Z <<< "$(echo "$l" | sed -nE 's/.* at \((-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\).*/\1 \2 \3/p')"
  if [ -n "$X" ]; then
    struggle_at "$X" "$Y" "$Z" wc; nw=$(lines); sleep 20; eyes wc-20; sshot wc-20; sleep 10
    since "$nw" "^(CAPTAIN t=.* (a presentation starts|the world's captains|a captain of the world met)|STORY t=.* subtitle ch=1 ')" | cut -c1-260 | head -20
    eyes wc-30; sshot wc-30
  else note "warchief: no marker"; fi
fi

if has fight; then
  echo "== fight: the world's captain by the player's own blade (his Last Words: the blade = Eliminate)"
  fight_one "${NM_CHOICE1:-blade}" k1
  for k in 1 2 3 4 5 6; do sleep 3; eyes "k1-end-$k"
    if grep -E '^BODYMOMENT t=.* window' "$L" | tail -1 | grep -q 'window 0 -> 1'; then sshot "k1-end-$k"; tap R b 0.4; fi; done
  since "$n0" '^(CAPTAIN t=.* (a presentation|the presentation|the world.s captains)|USE t)' | tail -12 | cut -c1-240
  if [ -n "${NM_CHOICE2:-interrogate}" ] && [ "${NM_CHOICE2:-interrogate}" != none ]; then
    echo "== a second captain of the world (one who joins), his Last Words: ${NM_CHOICE2:-interrogate}"
    # one met since the fight began who is not the first (boot 172517: the Runt was met while the first one died)
    K1=$(since "$n0" "^CAPTAIN t=.* a captain of the world met" | head -1 | sed -nE 's/.* met: member ([0-9]+),.*/\1/p')
    n2=$n0; other() { since "$n2" "^CAPTAIN t=.* a captain of the world met" | grep -v "met: member ${K1:-x}," | tail -1; }
    for k in $(seq 1 40); do [ -n "$(other)" ] && break; sleep 3; done
    if [ -n "$(other)" ]; then
      other | cut -c1-300; w "captain follow $(other | sed -nE 's/.* met: member ([0-9]+),.*/\1/p')" 0.5
      fight_one "${NM_CHOICE2:-interrogate}" k2
    else note "no second captain of the world within 2 min"; fi
  fi
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "$REST" 0.4
fi

if has army; then
  echo "== army: Sauron's Army from the pause menu by the laser"
  inworld || pause_close; pause_open; sshot army-pause
  laser 0.12 0.231 3; sshot army-board; eyes army-board
  "$S" off > /dev/null; pause_close
fi

if has runes; then
  echo "== runes: Weapons & Runes by the laser"
  inworld || pause_close; pause_open
  m0=$(cnt '^POINTER (down|up)')
  laser 0.12 0.287 3; sshot wr-page
  laser 0.20 0.375 2.5; sshot wr-grid
  laser 0.172 0.213 2; sshot wr-pick
  laser 0.172 0.213 2.5; sshot wr-equip; eyes wr-equip
  echo "  the laser's clicks: $(( $(cnt '^POINTER (down|up)') - m0 )) POINTER lines; $(grep -c 'xr: pointer frame .* edges 0x1 ' "$X") on-edges"
  "$S" off > /dev/null; pause_close
fi

if has death; then
  echo "== death: Talion killed, the Nemesis screens, time passes"
  inworld || pause_close
  w "practice mortal on" 0.3; w "fight guard off" 0.3; w "$REST" 0.4
  n0=$(lines); D0=$(cnt '^SCREENS t=.* the death: dying')
  w "practice hurt 20" 0.5
  [ "$(cnt '^COMBAT|^THREAT')" -gt 0 ] && true
  w "practice spawn berserker" 2; w "practice spawn berserker" 2; w "execute chase on" 0.3
  for k in $(seq 1 120); do userin && bail "the user is in VR"
    [ "$(cnt '^SCREENS t=.* the death: dying')" -gt "$D0" ] && break; [ $((k % 30)) -eq 0 ] && w "practice hurt 5" 0.2; sleep 1; done
  w "execute chase off" 0.3
  [ "$(cnt '^SCREENS t=.* the death: dying')" -gt "$D0" ] && pass "death: Talion is dying" || fail "death: Talion alive after 120 s"
  for s in 1 2 3; do sleep 2; eyes "death-$s"; done
  for k in $(seq 1 16); do
    st=$(grep -E '^SESSION ui ' "$L" | tail -1); echo "$(date +%T) $(echo "$st" | cut -c1-120)"
    echo "$st" | grep -q 'menus=0 paused=0' && [ "$k" -gt 4 ] && break
    sshot "death-screen-$k"; tap R b 0.4; sleep 4
  done
  w "practice mortal off" 0.3
  inworld && pass "death: back in the world" || fail "death: not back in the world"
  sleep 3; eyes death-back
  echo "-- after the death: the markers and the members"
  nav; w "captain status" 2
  grep -E '^CAPTAIN member [0-9]+: ' "$L" | tail -27 | cut -c1-200 | grep -E 'level|in the world' | head -27
  board death-board
  tap L menu 0.3; sleep 2; inworld || pause_close
fi
echo "$(date +%T) done"; grep -E '^CAPTAIN END' "$L" | cut -c1-400; grep -cE '^WARN' "$L"
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
