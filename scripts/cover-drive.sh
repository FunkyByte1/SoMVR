#!/bin/bash
# cover-drive.sh - round 2 of the void (handoff/HANDOFF-void.md): the game's in-world loading cover and the in-world
# menus' backdrop, headless. One boot with the full scripts/headset-build13.env (SOMVR_XR_COVER=on, SOMVR_XR_MENU_BACK=on
# among them), the test hands, the layer's test file and SOMVR_PRACTICE=1 (the save's own place, the forge tower top
# at 35400 5402 -22400; with SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_MINHEALTH=0 SOMVR_PRACTICE_DAY=0
# for a death), run as scripts/drill13-drive.sh says, with this script at the same time (CV_WT: the worktree's name in
# launch-cmd.txt). CV_PLAN (words, default "menu travel travel"): pause = the left menu button at the tower top (the
# white marble behind the board), Options, Game Settings, out (four flicks + A can reach QUIT: run 005633 quit to the
# main menu); menu = the pause menu alone, the fingertip and the laser on it, out; travel = the map (the right stick's click: the cross on
# the tower), X (the left controller's A: Fast Travel), the eyes every second for 16 s through the game's cover; death =
# the P8 camp, held still until Talion dies, the screens, at the Nemesis board the word `practice back` (with
# SOMVR_PRACTICE_WAIT=0 its teleport lands in the respawn; CV_BOARD_WORD="practice go 32500 2100 -24800" = the
# drill's far one: the game's 40-60 s cover), B, the eyes every second for 35 s.
# Every step: STEP lines, the eyes (`dump`: scripts/xr_compose.py RUN), the flat picture (shots/dr-NAME.png).
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CV_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${CV_PLAN:-menu travel travel}; PLAN=${PLAN//_/ }
T0=$(date +%s)
R=${CV_RUN:-}
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
ARR='^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world'; AN=2   # the second ctx=world: after the load's pause
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
LOW="hand L head -25 -35 30 0 0 0"
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
drawn() { tail -800 "$R/somvr.log" | grep -qE '^SLOTS .*sword: .*(now in the hand|kept in the hand)'; }
logs() { # the interesting lines since line $1 of somvr.log (counts by kind, the last of each)
  tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT|FBSITE|FALLBACK|PRACTICE|SESSION ui|WARN|VRMENU)' |
    cut -c1-170 | awk '{k=$1" "$2; c[k]++; l[k]=$0} END {for (k in c) print c[k]"x " l[k]}' | sort -k2 | head -30; }
xlogs() { # the layer's cover / backdrop / env lines since line $1 of xr-layer.log
  tail -n +"$1" "$R/xr-layer.log" | grep -E 'xr: (cover|menu backdrop|env: (world|environment)|loadspace: placed)' | cut -c1-200 | head -40; }
L0=$(wc -l < "$R/somvr.log"); X0=$(wc -l < "$R/xr-layer.log")
# 0. the controls card away, the left hand low, the right hand at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
t "$LOW"
w "$REST" 2
d tower 1
for p in $PLAN; do
  L1=$(wc -l < "$R/somvr.log"); X1=$(wc -l < "$R/xr-layer.log")
  case $p in
  pause)
    step "pause: the left menu button at the tower top (the white marble behind the board)"
    tap L menu; sleep 3; d pause 0.5; m pause
    inworld && { echo "NOT PAUSED by the tap: Escape"; k Escape; sleep 3; d pause-esc 0.5; }
    step "pause: Options (four down, A), Game Settings (three down, A)"
    for i in 1 2 3 4; do flick 0 -1; done; tap R a; sleep 3; d options 0.5
    for i in 1 2 3; do flick 0 -1; done; tap R a; sleep 3; d gamesettings 0.5
    step "pause: the right fingertip on the board, then the laser (touch and the pointer over the backdrop)"
    t "hand R touch board 0.5 0.5 5"; sleep 1; d pause-touch 0.5
    t "hand R head 12 -18 20 11 17 0 s 0.3" "btn R trigger 0.3"; sleep 1; d pause-laser 0.5
    t "btn R trigger 0" "hand R off"; sleep 0.5; w "$REST" 1
    step "pause: out (B, B, the menu button)"
    tap R b; sleep 2; tap R b; sleep 2; inworld || { tap L menu; sleep 2; }; inworld || { tap R b; sleep 2; }
    d pause-out 0.5; sleep 0.3; d pause-out2 0.5 ;;
  menu)
    step "menu: the pause menu at the tower top (no page: four flicks + A reached QUIT in run 005633), touch, laser, out"
    tap L menu; sleep 3; d menu 0.5; m menu
    inworld && { echo "NOT PAUSED by the tap: Escape"; k Escape; sleep 3; d menu-esc 0.5; }
    sleep 2; d menu2 0.5
    t "hand R touch board 0.5 0.5 5"; sleep 1; d menu-touch 0.5
    t "hand R head 12 -18 20 11 17 0 s 0.3" "btn R trigger 0.3"; sleep 1; d menu-laser 0.5
    t "btn R trigger 0" "hand R off"; sleep 0.5; w "$REST" 1
    tap L menu; sleep 2; inworld || { tap R b; sleep 2; }; d menu-out 0.3; sleep 0.2; d menu-out2 0.5 ;;
  travel)
    step "travel: the map (the cross starts on this tower), X = Fast Travel; the eyes every second through the cover"
    tap R stick; sleep 3; d map 0.5; m map 2
    inworld && { echo "NO MAP"; continue; }
    tap L a; TT=$(date +%s.%N)
    for i in $(seq 1 16); do d "travel-$i" 0.4; done
    sleep 2; d travel-after 0.5; m travel 2 ;;
  death)
    step "death: the P8 camp, the sword drawn, held still until Talion dies"
    A0=$(cnt 'arrived at .* in the practice area')
    w "practice go 33774.72 3733.07 -28189.33" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    sleep 2; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 1
    DT=$(date +%s); dead=0
    while [ $(( $(date +%s) - DT )) -lt "${CV_FIGHT_S:-180}" ]; do
      sleep 2; tail -400 "$R/somvr.log" | grep -qE '^BODYMOMENT .*dead=1' && { dead=1; break; }
    done
    echo "dead=$dead after $(( $(date +%s) - DT )) s"
    for i in $(seq 1 40); do grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -q 'paused=1' && break; sleep 2; done
    sleep 3; w "${CV_BOARD_WORD:-practice back}" 1; echo "shot cv-board" >> "$R/cmd"; m board 2
    for i in $(seq 1 12); do tap R b; sleep 4; inworld && break; done
    RT0=$(date +%s)
    for i in $(seq 1 35); do d "respawn-$(( $(date +%s) - RT0 ))" 1.0; done
    m respawned 1 ;;
  esac
  echo "---- log since the step:"; logs "$L1"; echo "---- the layer:"; xlogs "$X1"
done
echo "steps done $(date +%T)"
echo "---- the whole run:"; logs "$L0"
[ "${CV_NOQUIT:-0}" = 1 ] && { echo "the boot left running (CV_NOQUIT=1)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
