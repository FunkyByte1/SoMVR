#!/bin/bash
# drill13-drive.sh - the real-game survey drill (handoff/HANDOFF-drill.md): what a VR player meets outside the practice
# meadow with build 12's switches. One headless boot with the full scripts/headset-build13.env, the test hands
# (SOMVR_HANDS_TEST=1) and the layer's test file, the practice's real-world overrides
#   SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_MINHEALTH=0 SOMVR_PRACTICE_DAY=0
# (the save's own place, the ambient AI stays, no clear pass - `_CLEAR_M=5` still removed every orc who came close -,
# Talion can die, the real light: `practice go X Y Z` is then a plain teleport under the trip's blink), run as
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build13.env | tr '\n' ' ') <the
#     overrides> SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w
#     scripts/headless_vr.sh <this worktree>/ShadowOfMordor/mod/somvr
# with this script at the same time (DR_WT: the worktree's name in launch-cmd.txt). Every step prints a STEP line and
# takes the eyes (the layer's `dump`: <log>.stereoN/hudN/vrmenuN; scripts/xr_compose.py RUN draws them, DUMP lines name
# them in order) and the flat picture (boot_check's `shot`: shots/NAME.png); marks at the main places.
# DR_PLAN (words in order, joined by spaces or `_`; default "look pause map camp go back"): back = the user's way out
#   of the meadow (the wrist menu > Practice > Back to where I was: the save's forge tower top); go = `practice go` to
#   build 12's meadow spot; cycles = Back and Go by the wrist menu DR_CYCLES times, each press counted; look = four snap
#   turns; pause = the left menu button (the game's Menu), the pause menu's pages, B out; options / sound = the pause
#   menu > Options > Game Settings / Sound; map = the right stick's click (the game's PDA), its pages, B out; camp =
#   `practice go` to the P8 fight camp (3 infantry, 2 archers; re-combat-spawn.md 3), the sword drawn, held still until
#   Talion dies (or DR_FIGHT_S), the death screens, B (Return to Game) on the Nemesis board, the respawn watched;
#   camp2 = the camp again after it; walk = four 8 s walks with snap turns; pausetrip = Go by the wrist menu in the pause
#   menu, then unpaused; boardtrip = a death at the camp, the word `practice back` at the Nemesis board, B, the respawn
#   watched (pausetrip/boardtrip: SOMVR_PRACTICE_WAIT's proof); mission = `practice go` to the map's mission icon and
#   short walks until a prompt shows. DR_ARRIVE=world: the arrival is the world itself (SOMVR_PRACTICE=1); DR_NOQUIT=1
#   leaves the boot running at the end; DR_RUN=RUN drives a running boot.
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${DR_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${DR_PLAN:-look pause map camp go back}; PLAN=${PLAN//_/ }   # (words may be joined by _)
T0=$(date +%s)
R=${DR_RUN:-}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
# the arrival: SOMVR_PRACTICE=auto's in the meadow; DR_ARRIVE=world (SOMVR_PRACTICE=1: the save's own place) = the
# second `ctx=world` (the first comes for a moment before the load's pause)
ARR='^PRACTICE t=.* arrived'; AN=1
[ "${DR_ARRIVE:-}" = world ] && ARR='^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' && AN=2
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  [ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge $AN ] && break; sleep 3
done
[ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge $AN ] || bail "no arrival"
echo "arrived $(date +%T)"
sleep 6
N=0; S=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
# d NAME: the eyes (a dump) and the flat picture (shots/NAME.png)
d() { t "dump"; echo "shot dr-$1" >> "$R/cmd"; echo "$(date +%T) DUMP $1 ($(state))"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
k() { echo "key $1" >> "$R/cmd"; echo "$(date +%T) key $1"; sleep 0.6; }
# tap HAND BUTTON [SECONDS]: pressed and let go (the file written directly: t's 0.6 s wait would make every tap a long
# press, and the left menu button held 0.6 s toggles the wrist menu instead of the game's Menu; the layer reads the file
# every 5th sync, about 0.25 s at the null compositor's 20 Hz)
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        echo "$(date +%T) tap $1 $2 ${3:-0.3}"; sleep 0.5; }
flick() { userin && bail "the user is in VR"; printf 'stick L %s %s\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep 0.4; printf 'stick L 0 0\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) flick $1 $2"; sleep 0.8; }
inworld() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -q 'ctx=world'; }
step() { S=$((S + 1)); echo "$(date +%T) STEP $S $*"; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN"; return 1; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
LOW="hand L head -25 -35 30 0 0 0"
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
drawn() { tail -800 "$R/somvr.log" | grep -qE '^SLOTS .*sword: .*(now in the hand|kept in the hand)'; }
logs() { # the interesting lines since line $1 of somvr.log (counts by kind, the last of each)
  tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT|FBSITE|FALLBACK|PRACTICE|SESSION ui|WARN|HUD group|PROMPT|COMFORT|VRMENU)' |
    cut -c1-170 | awk '{k=$1" "$2; c[k]++; l[k]=$0} END {for (k in c) print c[k]"x " l[k]}' | sort -k2 | head -30; }
L0=$(wc -l < "$R/somvr.log")
# 0. the controls card away, the left hand low, the right hand at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
t "$LOW"
w "$REST" 2
d meadow 1
for p in $PLAN; do
  L1=$(wc -l < "$R/somvr.log")
  case $p in
  back)
    step "back: the wrist menu > Practice > Back to where I was"
    menu_open; sleep 0.5; press 0.441 0.042; sleep 1; d back-menu 0.5
    press 0.5 0.305; sleep 1.5
    if ! tail -50 "$R/somvr.log" | grep -q 'PRACTICE .*back'; then   # once more, slower (the drill's first boot: no click)
      echo "no trip back after the press: again, slower"; d back-menu2 0.3
      t "hand R touch panel 0.5 0.305 5"; t "hand R touch panel 0.5 0.305 1.0 s 0.5"; sleep 0.3
      t "hand R touch panel 0.5 0.305 -1.5 s 0.4"; sleep 0.3; t "hand R touch panel 0.5 0.305 4 s 0.3"; sleep 1
    fi
    menu_shut
    for i in $(seq 1 40); do grep -q 'arrived at .* back where Talion was' "$R/somvr.log" && break; sleep 1; done
    grep 'arrived at .* back where Talion was' "$R/somvr.log" | tail -1 | cut -c1-160
    sleep 3; d back-arrived 1 ;;
  go)
    step "go: the practice meadow (the word with build 12's spot: the camp's trip moved it)"
    A0=$(cnt 'arrived at .* in the practice area')
    w "practice go 32500 2100 -24800" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    sleep 3; d go-arrived 1 ;;
  look)
    step "look: four snap turns where Talion is"
    d look0 0.5; m look
    for i in 1 2 3 4; do t "stick R 1 0"; sleep 0.5; t "stick R 0 0"; sleep 1.5; d "look$i" 0.5; done ;;
  pause)
    step "pause: the left menu button (a short press: the game's Menu)"
    tap L menu; sleep 3; d pause 0.5; m pause
    inworld && { echo "NOT PAUSED by the tap: Escape"; k Escape; sleep 3; d pause-esc 0.5; }
    step "pause: Options (four down, A), its pages by the triggers"
    for i in 1 2 3 4; do flick 0 -1; done; d pause-opt-sel 0.3; tap R a; sleep 3; d options 0.5
    for i in 1 2 3 4 5; do tap R trigger; sleep 2; d "options-rt$i" 0.3; done
    tap R b; sleep 2; d options-b 0.3
    step "pause: Appendices (one up, A)"
    flick 0 1; tap R a; sleep 3; d appendices 0.5; tap R trigger; sleep 2; d appendices-rt 0.3; tap R b; sleep 2
    step "pause: the right fingertip at the board's middle, then the laser"
    t "hand R touch board 0.5 0.5 5"; sleep 1; d pause-touch 0.5
    t "hand R head 12 -18 20 11 17 0 s 0.3" "btn R trigger 0.3"; sleep 1; d pause-laser 0.5
    t "btn R trigger 0" "hand R off"; sleep 0.5
    step "pause: out by the menu button"
    tap L menu; sleep 2; inworld || { tap R b; sleep 2; }; inworld || { tap L menu; sleep 2; }; d pause-out 0.5 ;;
  map)
    step "map: the right stick's click (the game's PDA); the triggers' pages; out"
    tap R stick; sleep 3; d map 0.5; m map
    for i in 1 2; do tap R trigger; sleep 2.5; d "map-rt$i" 0.5; done
    for i in 1 2 3; do inworld && break; tap R b; sleep 2; done
    inworld || { tap R stick; sleep 2; }; d map-out 0.5 ;;
  camp)
    step "camp: practice go to the P8 fight camp; the sword drawn; held still"
    A0=$(cnt 'arrived at .* in the practice area')
    w "practice go 33774.72 3733.07 -28189.33" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    sleep 2; d camp 0.5
    w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 1
    drawn && echo "sword drawn" || echo "sword NOT drawn"
    m camp
    DT=$(date +%s); dead=0
    while [ $(( $(date +%s) - DT )) -lt "${DR_FIGHT_S:-180}" ]; do
      d "camp-$(( $(date +%s) - DT ))" 2
      tail -400 "$R/somvr.log" | grep -qE '^BODYMOMENT .*dead=1' && { dead=1; break; }
    done
    echo "dead=$dead after $(( $(date +%s) - DT )) s"
    step "death: the death screens watched (flat pictures every 2 s: the window), until the game pauses (the Nemesis board)"
    [ $dead = 1 ] && m death 1
    for i in $(seq 1 40); do
      grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -q 'paused=1' && break
      echo "shot dr-death-$i" >> "$R/cmd"; sleep 2
    done
    sleep 5; echo "shot dr-board" >> "$R/cmd"; m board 2
    step "respawn: B (Return to Game) every 4 s until the world (the board takes it; the screens before it do not), then
    the respawn watched: the eyes every 2.5 s for 60 s (no practice trip pending)"
    for i in $(seq 1 12); do tap R b; sleep 4; echo "shot dr-b$i" >> "$R/cmd"; inworld && break; done
    RT0=$(date +%s)
    for i in $(seq 1 24); do d "respawn-$(( $(date +%s) - RT0 ))" 1.5; [ $i = 3 ] && m respawn-3 1; done
    sleep 3; d respawned 1; m respawned ;;
  camp2)
    step "camp2: back to the camp after the respawn (the orc who killed Talion is a captain now: his intro?)"
    A0=$(cnt 'arrived at .* in the practice area')
    w "practice go 33774.72 3733.07 -28189.33" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    sleep 2; d camp2 0.5
    drawn || { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 1; }
    W0=$(cnt '^BODYMOMENT .*window'); DT=$(date +%s)
    while [ $(( $(date +%s) - DT )) -lt "${DR_CAMP2_S:-90}" ]; do
      d "camp2-$(( $(date +%s) - DT ))" 2
      tail -400 "$R/somvr.log" | grep -qE '^BODYMOMENT .*dead=1' && { echo "dead again"; break; }
    done
    m camp2 1 ;;
  cycles)
    step "cycles: the user's way between the meadow and the real world, by the wrist menu only: Back, Go, ${DR_CYCLES:-3} times
    (each press counted: a click = a PRACTICE line from the menu within 3 s; a miss is pressed again)"
    for k in $(seq 1 "${DR_CYCLES:-3}"); do
      for b in back go; do
        [ $b = back ] && V=0.305 || V=0.219
        M0=$(cnt "^PRACTICE t=.* \\(the menu\\)"); A0=$(cnt 'arrived at'); tries=0
        menu_open; sleep 0.5; press 0.441 0.042; sleep 1
        while [ $tries -lt 3 ]; do
          tries=$((tries + 1)); press 0.5 $V; sleep 3
          [ "$(cnt "^PRACTICE t=.* \\(the menu\\)")" -gt "$M0" ] && break
          echo "MISS: $b press $tries (cycle $k)"; d "miss-$b-$k-$tries" 0.3
          grep -E 'vrmenu frame .*uv 0.500' "$R/xr-layer.log" | tail -2 | cut -c60-200
        done
        echo "PRESS $b cycle $k: $tries tries"; menu_shut
        for i in $(seq 1 30); do [ "$(cnt 'arrived at')" -gt "$A0" ] && break; sleep 1; done
        sleep 3; d "cyc-$b-$k" 0.5
      done
    done ;;
  mission)
    step "mission: practice go to the map's yellow mission icon (DR_MISSION_AT, read off the map: ~33 m east of the save's
    tower), short walks until a prompt shows, then B and the start watched (dumps every 2 s)"
    A0=$(cnt 'arrived at .* in the practice area'); P0=$(cnt '^PROMPTS show')
    w "practice go ${DR_MISSION_AT:-38700 4300 -22950}" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    grep 'arrived at' "$R/somvr.log" | tail -1 | cut -c1-120
    sleep 2; d mission-at 0.5; m mission-at 2
    for k in $(seq 1 10); do
      [ "$(cnt '^PROMPTS show')" -gt "$P0" ] && break
      w "stick 0 1 0 2.5" 3; d "mission-walk$k" 0.3
      [ $((k % 2)) = 0 ] && { t "stick R 1 0"; sleep 0.5; t "stick R 0 0"; sleep 1; }
    done
    grep -E '^PROMPTS show' "$R/somvr.log" | tail -3 | cut -c1-170
    if [ "$(cnt '^PROMPTS show')" -gt "$P0" ]; then
      d mission-prompt 0.3; m mission-prompt 2; tap R b 0.4; sleep 0.5; t "btn R b 1"; sleep 2.5; t "btn R b 0"
      for i in $(seq 1 30); do d "mission-$i" 1.5; done
      grep -E '^SESSION ui|^BODYMOMENT t' "$R/somvr.log" | tail -5 | cut -c1-170
    fi ;;
  edge)
    step "edge: walk to the tower top's edge, look down, then the hands reach over it and squeeze (a climb hold on the
    outer wall? SOMVR_CLIMB=b), then both open (the hang) and A (the drop)"
    w "stick 0 1 0 8" 8.5; d edge-at 0.3
    t "hand L off"; w "hands squeeze L 0"; w "hands squeeze R 0"
    G0=$(cnt '^CLIMB t=.* grab [LR]:'); got=""
    for q in "-10 50" "-30 55" "-50 60" "-70 60" "-90 55" "-110 50"; do
      set -- $q
      w "hands test L -20 $1 $2 0 0 0" 0.4; w "hands test R 20 $1 $2 0 0 0" 0.6; d "edge-reach$1" 0.2
      w "hands squeeze L 1" 0.3; w "hands squeeze R 1" 1.2
      [ "$(cnt '^CLIMB t=.* grab [LR]:')" -gt "$G0" ] && { got="$q"; break; }
      w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.5
    done
    grep -E '^CLIMB t=' "$R/somvr.log" | tail -4 | cut -c1-170
    if [ -n "$got" ]; then
      echo "GRAB at $got"; d edge-grab 0.3; m edge-grab 2
      w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 2; d edge-hang 0.3
      t "btn R a 1"; sleep 0.4; t "btn R a 0"; for i in 1 2 3 4 5 6; do d "edge-drop$i" 1; done
      grep -E '^CLIMB t=|^BODYMOMENT t|^COMFORT .*fall' "$R/somvr.log" | tail -6 | cut -c1-170
    else
      echo "NO GRAB over the edge"; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.5
    fi ;;
  pausetrip)
    step "pausetrip: the pause menu, then the wrist menu > Practice > Go (SOMVR_PRACTICE_WAIT: no black over the pause menu)"
    tap L menu; sleep 3; d pt-paused 0.5
    menu_open; sleep 0.5; press 0.441 0.042; sleep 1; press 0.5 0.219; sleep 1; d pt-pressed 0.3; menu_shut; sleep 2
    d pt-paused2 0.5; m pausetrip 2
    tail -30 "$R/somvr.log" | grep -E '^PRACTICE' | tail -2 | cut -c1-170
    step "pausetrip: unpaused (the menu button): the trip 1 s later"
    tap L menu; sleep 0.4; d pt-out 0.3; sleep 1.2; d pt-1s 0.3
    for i in $(seq 1 20); do grep -q 'arrived at .* in the practice area' "$R/somvr.log" && break; sleep 1; done
    sleep 2; d pt-arrived 0.5 ;;
  boardtrip)
    step "boardtrip: the camp, a death; at the Nemesis board the word practice back; B; the respawn watched"
    A0=$(cnt 'arrived at .* in the practice area')
    w "practice go 33774.72 3733.07 -28189.33" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    sleep 2; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 1
    DT=$(date +%s); dead=0
    while [ $(( $(date +%s) - DT )) -lt "${DR_FIGHT_S:-180}" ]; do
      sleep 2; tail -400 "$R/somvr.log" | grep -qE '^BODYMOMENT .*dead=1' && { dead=1; break; }
    done
    echo "dead=$dead after $(( $(date +%s) - DT )) s"
    for i in $(seq 1 40); do grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -q 'paused=1' && break; sleep 2; done
    sleep 3; w "practice back" 1; echo "shot bt-board" >> "$R/cmd"; m boardtrip 2
    tail -30 "$R/somvr.log" | grep -E '^PRACTICE' | tail -2 | cut -c1-170
    for i in $(seq 1 12); do tap R b; sleep 4; inworld && break; done
    RT0=$(date +%s)
    for i in $(seq 1 24); do d "bt-$(( $(date +%s) - RT0 ))" 1.5; done
    grep -E '^PRACTICE' "$R/somvr.log" | tail -4 | cut -c1-170 ;;
  sound)
    step "sound: the pause menu > Options > Sound (the subtitles?)"
    tap L menu; sleep 3
    for i in 1 2 3 4; do flick 0 -1; done; tap R a; sleep 3; d snd-opt 0.3; tap R a; sleep 3; d sound 0.5
    for i in 1 2 3 4 5 6 7 8; do flick 0 -1; done; d sound-down 0.5
    tap R b; sleep 2; tap R b; sleep 2; inworld || { tap L menu; sleep 2; }; d sound-out 0.3 ;;
  options)
    step "options: the pause menu > Options > Game Settings (the subtitles?)"
    tap L menu; sleep 3
    for i in 1 2 3 4; do flick 0 -1; done; tap R a; sleep 3; d opt 0.3
    for i in 1 2 3; do flick 0 -1; done; d opt-sel 0.3; tap R a; sleep 3; d gamesettings 0.5
    for i in 1 2 3 4 5 6 7 8; do flick 0 -1; done; d gamesettings-down 0.5
    tap R b; sleep 2; tap R b; sleep 2; inworld || { tap L menu; sleep 2; }; d options-out 0.3 ;;
  walk)
    step "walk: the left stick ahead 8 s, four times with a snap turn between (prompts on the way?)"
    for i in 1 2 3 4; do w "stick 0 1 0 8" 8.5; d "walk$i" 0.5; t "stick R 1 0"; sleep 0.5; t "stick R 0 0"; sleep 1; done ;;
  esac
  echo "---- log since the step:"; logs "$L1"
done
echo "steps done $(date +%T)"
echo "---- the whole run:"; logs "$L0"
[ "${DR_NOQUIT:-0}" = 1 ] && { echo "the boot left running (DR_NOQUIT=1)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
