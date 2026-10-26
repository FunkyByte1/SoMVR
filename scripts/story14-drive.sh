#!/bin/bash
# story14-drive.sh - the story and cinematics drill (handoff/HANDOFF-story.md): what a VR player meets when they play
# the actual story with build 13's switches - a story mission's start and its cutscene, an Activate prompt and a pick
# (an herb, an artifact, an Ithildin glyph), a mission giver, a Nemesis mission (a captain's presentation). One headless
# boot as scripts/drill13-drive.sh's (the same overrides, DR_ARRIVE=world: the save's own place), run as
#   HEADLESS_HOLD=1500 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build13.env | tr '\n' ' ')
#     SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# with this script at the same time. The places are the game's own nav markers on the save (the probe's `navdump`,
# logs/boot-check-20261020-001117/probe-out/nav-4.tsv: record, x y z): `practice go` there (0.5 m above the marker),
# then short steps until the game's prompt shows (PROMPTS show), B (our Activate) tapped, then held, and the result
# watched: the eyes (`dump`: the next stereo frame) and the flat picture (`shot`) every few seconds, the log's
# BODYMOMENT / SESSION ui / PROMPTS / xr: theatre lines after each. Marks at the main places.
# DR_PLAN (words in order, joined by spaces or `_`; default "herb artifact glyph giver mission"):
#   herb = Collectible_Flower_I 38599 3566 -25606 (49 m south-east of the save's forge tower); artifact =
#   Collectible_Artifact 34776 2448 -27463 (the examine screen: the sticks turned, A, B out); glyph = Collectible_Glyph
#   34345 3900 -19589; giver = resistance_QuestGiver 27107 3363 -19520 (a side mission's giver); mission = goldenM
#   42632 3889 -22529 (a story mission's start: the golden M), its start watched DR_WATCH_S (default 150) seconds, A and
#   B tried to skip only after 60 s; struggle = powerStruggle -30877 2874 11629 (a Nemesis mission, 750 m west);
#   objective = squiller Objective_Squiller 36316 3694 -26541. With SOMVR_USE=1 (our buttons only): herbb, missionb,
#   missionc (the video to its end - DR_SKIP=1: A, then B in it -, the timed first objective, the failed screen's B,
#   the Leave Mission dialog's A), struggleb, strugglec (B, A on a card, B in the captain's introduction). DR_NOQUIT=1 leaves the boot running; DR_RUN=RUN drives
#   a running boot. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${DR_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${DR_PLAN:-herb artifact glyph giver mission}; PLAN=${PLAN//_/ }   # (words may be joined by _)
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
[ "${DR_ARRIVE:-world}" = world ] && ARR='^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' && AN=2
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
# the moments' lines since line $1 (the order kept, repeated LOCO/hands lines left out)
moments() { tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT t|SESSION ui|PROMPTS (show|hold)|WARN|FBSITE|FALLBACK t|PRACTICE t=.*(arrived|waits|game is back)|HINTS|VRMENU .*card)' |
  sed -E 's/ key 0x[0-9a-f]+//; s/h4=[0-9]+ //' | cut -c1-200 | head -${2:-40}
  grep -E 'xr: (theatre|loadspace|cover|space)' "$R/xr-layer.log" | tail -n +"${TH0:-1}" | cut -c1-200 | head -12; }
TH0=1
# go NAME X Y Z: `practice go` there (Y = the marker's height + 50 cm: the teleport snaps down), the arrival, a look
go_to() { local a0=$(cnt 'arrived at'); TH0=$(($(wc -l < "$R/xr-layer.log") + 1)); AL=$(wc -l < "$R/somvr.log")
  w "practice go $2 $(( $3 + ${5:-50} )) $4" 1
  for i in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done
  grep 'arrived at' "$R/somvr.log" | tail -1 | cut -c1-150
  sleep 3; d "$1-at" 0.5; m "$1" 2; }
# use NAME [HOLD_S]: B (our Activate) tapped, then held, the result watched (dumps every 2 s for $3 s, default 12)
use() { local ml=$(wc -l < "$R/somvr.log")   # (not `l`: t's loop variable)
  d "$1-before" 0.3; tap R b 0.3; sleep 1.5; d "$1-tap" 0.3
  t "btn R b 1"; sleep "${2:-2.5}"; t "btn R b 0"; sleep 0.5
  for i in $(seq 1 $(( ${3:-12} / 2 ))); do d "$1-$i" 1.0; done
  echo "-- $1: the moments since the press:"; moments "$ml" 30; }
# try NAME CMD [MS] [WATCH_S]: the game's command CMD held MS (the crouch probe's word `crouch try`: the deny list
# passed), then watched (dumps every 2 s for WATCH_S, default 10) - which command a prompt really wants
try() { local ml=$(wc -l < "$R/somvr.log"); d "$1-before" 0.3; w "crouch try $2 ${3:-400}" 1.5; d "$1-try" 0.3
  for i in $(seq 1 $(( ${4:-10} / 2 ))); do d "$1-$i" 1.0; done
  echo "-- $1 (command $2): the moments since:"; moments "$ml" 30; }
# prompted PATTERN [S]: wait up to S s (default 8) for a PROMPTS show line matching PATTERN since the arrival
prompted() { for i in $(seq 1 ${2:-8}); do tail -n +"$AL" "$R/somvr.log" | grep -qE "^PROMPTS show.*$1" && return 0; sleep 1; done; return 1; }
# steps NAME: four short steps around the spot (ahead, right, back, left 0.7 s each), a picture after each
steps() { for s in "0 1" "1 0" "0 -1" "-1 0"; do w "stick $s 0 0.7" 1.2; done; d "$1-steps" 0.3; }
for p in $PLAN; do
  L1=$(wc -l < "$R/somvr.log")
  case $p in
  herb)
    step "herb: Collectible_Flower_I (a root) 49 m south-east of the tower: the prompt, B, the pick (a forced crouch?)"
    go_to herb 38599 3566 -25606; use herb 1.5 10; steps herb; use herb2 1.5 6 ;;
  artifact)
    step "artifact: Collectible_Artifact 59 m south: B, the examine screen (the sticks, A), B out"
    go_to artifact 34776 2448 -27463; use artifact 2.5 8
    for s in "1 0" "0 1" "-1 0" "0 -1"; do t "stick L $s"; sleep 1.5; t "stick L 0 0"; t "stick R $s"; sleep 1.5; t "stick R 0 0"; done
    d artifact-turned 0.5; tap R a 0.4; sleep 3; d artifact-a 0.5
    for i in 1 2 3 4 5 6 7 8; do d "artifact-mem$i" 2; done
    tap R b 0.4; sleep 3; d artifact-out 0.5; inworld || { tap R b 0.4; sleep 3; }
    echo "-- artifact all:"; moments "$L1" 40 ;;
  glyph)
    step "glyph: Collectible_Glyph (an Ithildin) 34 m north: B"
    go_to glyph 34345 3900 -19589 -350; use glyph 2.5 8;   # (the glyph is 3.8 m up a wall: the ground 3523)
    steps glyph; use glyph2 2.5 6 ;;
  giver)
    step "giver: resistance_QuestGiver 90 m north-west: the giver's prompt, B, his scene"
    go_to giver 27107 3363 -19520; use giver 2.5 20; steps giver; use giver2 2.5 20 ;;
  mission)
    step "mission: goldenM (a story mission's start) 74 m east: the prompt, B, the start watched ${DR_WATCH_S:-150} s"
    go_to mission 42632 3889 -22529; use mission 2.5 10
    for i in $(seq 1 $(( ${DR_WATCH_S:-150} / 3 ))); do d "mission-w$i" 2.0
      [ $i = 20 ] && { echo "60 s: A then B tapped (a skip?)"; tap R a 0.3; sleep 1; tap R b 0.3; }; done
    echo "-- mission all:"; moments "$L1" 60 ;;
  struggle)
    step "struggle: powerStruggle (a Nemesis mission) 750 m west: arrive, the prompt, B, the presentation"
    go_to struggle -30877 2874 11629; use struggle 2.5 30; steps struggle; use struggle2 2.5 20 ;;
  herbtry)
    step "herbtry: the herb again, its prompt (123 'Combat_Activate_Flower'), then command 123 sent (what B should send)"
    go_to herbtry 38599 3566 -25606 110; prompted Flower 10 && echo "the flower's prompt is up" || steps herbtry
    try herbtry 123 300 8 ;;
  missiontry)
    step "missiontry: the golden M, its '(RB) Start Mission': commands 283 QuestConfirmStart, 282 QuestConfirm, 123 in turn"
    go_to missiontry 42632 3889 -22529 -50
    ml0=$(wc -l < "$R/somvr.log")
    for c in ${DR_MISSION_CMDS:-283 282 123}; do
      s0=$(cnt '^SESSION ui'); b0=$(cnt '^BODYMOMENT t=.* act ')
      try "mission$c" "$c" 400 8
      if [ "$(cnt '^SESSION ui')" -gt "$s0" ] || [ "$(cnt '^BODYMOMENT t=.* act ')" -gt "$b0" ]; then
        echo "command $c changed the session: the start watched ${DR_WATCH_S:-150} s"
        for i in $(seq 1 $(( ${DR_WATCH_S:-150} / 3 ))); do d "mission$c-w$i" 2.0; done
        echo "-- the mission's start:"; moments "$ml0" 80; break; fi
    done ;;
  struggletry)
    step "struggletry: the power struggle (750 m west): arrive, look, commands 283 then 123"
    go_to struggletry -30877 2874 11629 0; sleep 4; d struggle-look 0.5; steps struggletry
    for c in 283 123; do try "struggle$c" "$c" 400 16; done ;;
  herbb)
    step "herbb: the herb with our B only (SOMVR_USE=1: the prompt shown as B, B = 123 too)"
    go_to herbb 38599 3566 -25606 110; prompted Flower 10 && echo "the flower's prompt is up" || steps herbb
    use herbb 1.5 8 ;;
  missionb)
    step "missionb: the golden M with our buttons only: B (= the pad's RB: Start Mission), the card, A (Accept), the start watched ${DR_WATCH_S:-150} s (A then B at 60 s)"
    go_to missionb 42632 3889 -22529 -50; ml0=$(wc -l < "$R/somvr.log"); sleep 2; d missionb-prompt 0.5
    tap R b 0.4; sleep 3; d missionb-card 0.5; m missionb-card 2
    tap R a 0.4; sleep 2; d missionb-accept 0.5
    for i in $(seq 1 $(( ${DR_WATCH_S:-150} / 3 ))); do d "missionb-w$i" 2.0
      [ $i = 20 ] && { echo "60 s: A then B tapped (a skip?)"; tap R a 0.3; sleep 1; tap R b 0.3; }; done
    echo "-- the mission's start:"; moments "$ml0" 80 ;;
  struggleb)
    step "struggleb: the power struggle with our buttons: a failed mission's screen answered by B (Abandon), then B (= RB: Start Mission), the captain"
    go_to struggleb -30877 2874 11629 0; ml0=$(wc -l < "$R/somvr.log"); sleep 4; d struggleb-at 0.5
    tap R b 0.4; sleep 3; d struggleb-b1 0.5; tap R b 0.4; sleep 3; d struggleb-b2 0.5
    for i in $(seq 1 12); do d "struggleb-w$i" 2.0; done
    echo "-- the struggle:"; moments "$ml0" 60 ;;
  missionc)
    step "missionc: the golden M: B, the card, A; the video watched to its end (the theatre); the first objective; waited in place until the failed screen (the timer), B (Abandon)"
    go_to missionc 42632 3889 -22529 -50; ml0=$(wc -l < "$R/somvr.log")
    tap R b 0.4; sleep 3; d missionc-card 0.5; tap R a 0.4
    for i in $(seq 1 30); do sleep 3; echo "$(date +%T) SHOT missionc-v$i ($(state))"; echo "shot dr-missionc-v$i" >> "$R/cmd"
      [ "${DR_SKIP:-0}" = 1 ] && [ $i = 5 ] && { echo "$(date +%T) the video: A (a skip?)"; tap R a 0.3; }
      [ "${DR_SKIP:-0}" = 1 ] && [ $i = 8 ] && state | grep -q 'video=1' && { echo "$(date +%T) the video: B (a skip?)"; tap R b 0.3; }
      [ $i -gt 3 ] && state | grep -q 'video=0 ctx=world' && break; done
    for i in $(seq 1 6); do d "missionc-obj$i" 3.0; done
    f0=$(cnt 'window 0 -> 1 by pres')
    for i in $(seq 1 60); do [ "$(cnt 'window 0 -> 1 by pres')" -gt "$f0" ] && break; sleep 3; done
    echo "$(date +%T) the failed screen: $(grep 'window 0 -> 1 by pres' "$R/somvr.log" | tail -1 | cut -c1-60)"
    sleep 3; d missionc-failed 0.5; m missionc-failed 2; tap R b 0.4; sleep 4; d missionc-abandon 0.5
    state | grep -q 'ctx=menu' && { echo "$(date +%T) the Leave Mission dialog: A (Yes)"; tap R a 0.4; sleep 4; }
    d missionc-after 2
    echo "-- the mission:"; moments "$ml0" 80 ;;
  strugglec)
    step "strugglec: the power struggle: B (Start Mission), A if a card, the captain's introduction, B tapped in it (a skip?)"
    go_to strugglec -30877 2874 11629 0; ml0=$(wc -l < "$R/somvr.log"); sleep 3
    w0=$(cnt 'window 0 -> 1 by pres'); tap R b 0.4; sleep 3; d strugglec-b 0.5
    [ "$(cnt 'window 0 -> 1 by pres')" -gt "$w0" ] && { echo "a card: A"; tap R a 0.4; sleep 2; }
    w1=$(cnt 'window 0 -> 1 by pres')
    for i in $(seq 1 40); do [ "$(cnt 'window 0 -> 1 by pres')" -gt "$w1" ] && break; sleep 1; done
    sleep 1.5; echo "shot dr-strugglec-intro" >> "$R/cmd"; sleep 1; tap R b 0.3; sleep 2; echo "shot dr-strugglec-skip" >> "$R/cmd"
    for i in $(seq 1 8); do d "strugglec-w$i" 2.0; done
    echo "-- the struggle:"; moments "$ml0" 60 ;;
  route)
    step "route: the golden M, B, A, the video skipped (A, B 1 s later); the timed objective walked toward its marker (story2_route.py): first ${DR_ROUTE1:-run} (the stick only), then after the failed screen's A (Retry) ${DR_ROUTE2:-sprint} (the left stick's click latched)"
    go_to route 42632 3889 -22529 -50; ml0=$(wc -l < "$R/somvr.log")
    tap R b 0.4; sleep 3; d route-card 0.5; tap R a 0.4
    for i in $(seq 1 20); do state | grep -q 'video=1' && break; sleep 1; done
    echo "$(date +%T) the video: $(state)"; sleep 8; tap R a 0.3; sleep 0.7; tap R b 0.3
    for i in $(seq 1 60); do sleep 2; state | grep -q 'video=0 ctx=world' && break; done
    echo "$(date +%T) back in the world: $(state)"; sleep 1; d route-obj0 0.3
    for a in 1 2 3; do
      mode=$( [ $a = 1 ] && echo "${DR_ROUTE1:-run}" || echo "${DR_ROUTE2:-sprint}" )
      [ "$mode" = none ] && break
      f0=$(cnt 'window 0 -> 1'); echo "$(date +%T) ATTEMPT $a ($mode)"
      python3 "$(dirname "$0")/story2_route.py" "$R" --secs "${DR_ROUTE_S:-130}" --tag "rt$a" $( [ "$mode" = run ] && echo --nosprint ) || bail "route"
      sleep 2; d "route$a-end" 0.5
      if [ "$(cnt 'window 0 -> 1')" -gt "$f0" ]; then
        echo "$(date +%T) a window after attempt $a: $(grep 'window 0 -> 1' "$R/somvr.log" | tail -1 | cut -c1-80)"
        sleep 2; d "route$a-window" 0.5; m "route$a-window" 2
        if [ "$a" -lt 3 ] && [ "${DR_ROUTE2:-sprint}" != none ]; then
          c0=$(cnt 'window 1 -> 0'); echo "$(date +%T) A (Retry?)"; tap R a 0.4; sleep 3; d "route$a-retry" 0.5
          for i in $(seq 1 30); do sleep 1; [ "$(cnt 'window 1 -> 0')" -gt "$c0" ] && inworld && break; done
          sleep 2; d "route$a-retried" 0.5; echo "-- after the retry:"; moments "$ml0" 60 | tail -15; continue
        fi
      else
        for i in $(seq 1 10); do d "route$a-after$i" 2.0; done
      fi
      break
    done
    echo "-- the route:"; moments "$ml0" 90 ;;
  routemap)
    step "routemap: the golden M, B, A, the video skipped; the world map (the right stick's click) shot; the game paused (the menu button) until /tmp/story2-wpN.txt (N = 1, 2, ...: 'X,Z;X,Z;... [route options]') gives a route: unpaused, walked (story2_route.py), on a failed screen A (Retry) and paused again"
    go_to routemap 42632 3889 -22529 -50; ml0=$(wc -l < "$R/somvr.log")
    tap R b 0.4; sleep 3; tap R a 0.4
    for i in $(seq 1 20); do state | grep -q 'video=1' && break; sleep 1; done
    sleep 8; tap R a 0.3; sleep 0.7; tap R b 0.3
    for i in $(seq 1 60); do sleep 2; state | grep -q 'video=0 ctx=world' && break; done
    echo "$(date +%T) back in the world: $(state)"; sleep 1; d routemap-obj0 0.3
    tap R stick 0.3; sleep 3; echo "$(date +%T) the map? $(state)"; echo "shot dr-map1" >> "$R/cmd"; sleep 2
    tap R stick 0.3; sleep 2; state | grep -q 'ctx=world' || { tap R b 0.3; sleep 2; }
    tap L menu 0.2; sleep 2; echo "$(date +%T) paused? $(state)"; echo "shot dr-paused" >> "$R/cmd"
    for k in 1 2 3 4 5 6; do
      f=/tmp/story2-wp$k.txt; echo "$(date +%T) waiting for $f"
      for i in $(seq 1 300); do [ -s "$f" ] && break; userin && bail "the user is in VR"; sleep 3; done
      [ -s "$f" ] || { echo "no $f"; break; }
      wp=$(head -1 "$f" | cut -d' ' -f1); ro=$(head -1 "$f" | cut -s -d' ' -f2-)
      [ "$wp" = stop ] && break
      tap L menu 0.2; sleep 1.5; state | grep -q 'ctx=world' || { tap R b 0.3; sleep 1.5; }
      echo "$(date +%T) unpaused? $(state)"; f0=$(cnt 'window 0 -> 1'); echo "$(date +%T) ATTEMPT $k: $wp $ro"
      python3 "$(dirname "$0")/story2_route.py" "$R" --secs "${DR_ROUTE_S:-130}" --tag "rm$k" --waypoints "$wp" $ro || bail "route"
      sleep 2; d "routemap$k-end" 0.5
      if [ "$(cnt 'window 0 -> 1')" -gt "$f0" ]; then
        sleep 2; d "routemap$k-window" 0.5
        c0=$(cnt 'window 1 -> 0'); tap R a 0.4
        for i in $(seq 1 30); do sleep 1; [ "$(cnt 'window 1 -> 0')" -gt "$c0" ] && inworld && break; done
        sleep 1; tap L menu 0.2; sleep 2; echo "$(date +%T) retried and paused? $(state)"; d "routemap$k-retried" 0.3
      else
        echo "$(date +%T) no window after attempt $k"; for i in $(seq 1 10); do d "routemap$k-after$i" 2.0; done
        tap L menu 0.2; sleep 2; echo "$(date +%T) paused? $(state)"
      fi
    done
    echo "-- the route:"; moments "$ml0" 90 ;;
  collect)
    step "collect: the artifacts, the Ithildin glyphs, the resistance giver (the probe's nav markers): at each, rings of short steps until a prompt shows (PROMPTS show), then B (tapped, held) and the result; an artifact's examine view: the sticks, A, B"
    for spot in ${DR_COLLECT:-artifact1:61239:980:-15547 glyph2:34269:3233:-28618 artifact3:28596:5553:-33884 glyph3:57272:1919:-10603 artifact2:34776:2448:-27463 giver:27107:3363:-19520}; do
      IFS=: read -r nm cx cy cz <<< "$spot"; L2=$(wc -l < "$R/somvr.log")
      step "collect $nm at $cx $cy $cz"
      go_to "$nm" "$cx" "$cy" "$cz" 60; w "practice status" 1; grep '^PRACTICE t=.* status' "$R/somvr.log" | tail -1 | grep -o 'Talion [^;]*' | head -1
      found=0
      for ring in 0.3 0.6 1.0 1.5; do
        for s in "0 1" "1 1" "1 0" "1 -1" "0 -1" "-1 -1" "-1 0" "-1 1"; do
          prompted '' 1 && { found=1; break 2; }
          w "stick $s 0 $ring" "$(echo "$ring + 0.4" | bc)"
          prompted '' 1 && { found=1; break 2; }
          set -- $s; w "stick $((0 - $1)) $((0 - $2)) 0 $ring" "$(echo "$ring + 0.4" | bc)"
        done
      done
      echo "$(date +%T) $nm: prompt found=$found"; tail -n +"$L2" "$R/somvr.log" | grep -E '^PROMPTS show|^USE t' | cut -c1-220 | head -6
      d "$nm-prompt" 0.5; m "$nm" 2
      [ $found = 1 ] || continue
      use "$nm" 2.5 10
      if [ "${nm#artifact}" != "$nm" ]; then
        echo "$(date +%T) the examine view? $(state)"
        for s in "1 0" "0 1" "-1 0" "0 -1"; do t "stick L $s"; sleep 1.2; t "stick L 0 0"; t "stick R $s"; sleep 1.2; t "stick R 0 0"; done
        d "$nm-turned" 0.5; tap R a 0.4; sleep 3; d "$nm-a" 0.5; tap R b 0.4; sleep 3; d "$nm-b" 0.5
        inworld || { echo "still not the world: B again"; tap R b 0.4; sleep 3; d "$nm-b2" 0.5; }
      fi
      echo "-- $nm:"; moments "$L2" 40
    done ;;
  route2)
    step "route2: the golden M, B, A, the video skipped; the mission's own markers (practice status nav, new since the start); the objective walked with the sprint latch along the route learnt in run 20261026-100942 (two cliff edges: their cue, A = the blink down) to the new marker; on a failed screen A (Retry) and again; at the end B (Abandon), A (Yes)"
    w "practice status nav" 2; nav0=$(grep -E "^PRACTICE nav [0-9]+ '" "$R/somvr.log" | sed -E "s/^PRACTICE nav [0-9]+ '([^']*)'.*/\1/" | sort -u)
    go_to route2 42632 3889 -22529 -50; ml0=$(wc -l < "$R/somvr.log")
    tap R b 0.4; sleep 3; d route2-card 0.5; tap R a 0.4
    for i in $(seq 1 20); do state | grep -q 'video=1' && break; sleep 1; done
    sleep 8; tap R a 0.3; sleep 0.7; tap R b 0.3
    for i in $(seq 1 60); do sleep 2; state | grep -q 'video=0 ctx=world' && break; done
    echo "$(date +%T) back in the world: $(state)"; n1=$(wc -l < "$R/somvr.log"); w "practice status nav" 2
    tail -n +"$n1" "$R/somvr.log" | grep -E "^PRACTICE nav [0-9]+ '" | cut -c1-120 > "$R/route2-nav.txt"
    goal=""; while IFS= read -r l; do nm=$(echo "$l" | sed -E "s/^PRACTICE nav [0-9]+ '([^']*)'.*/\1/")
      echo "$nav0" | grep -qxF "$nm" && continue
      xyz=$(echo "$l" | sed -E 's/.* at \((-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\).*/\1 \2 \3/'); echo "NEW marker: $l"
      [ -z "$goal" ] && set -- $xyz && goal="$1,$3"; done < "$R/route2-nav.txt"
    [ -n "$goal" ] || { goal=${DR_GOAL:-24000,-8000}; echo "no new marker: the goal $goal"; }
    WPS="38500,-21300;35100,-21450;35300,-19100;33960,-17938;33812,-17563;33217,-16953;33350,-16400;32646,-16108;31354,-15976;30693,-15937;28444,-14841"
    echo "$(date +%T) the goal $goal"
    for a in 1 2 3; do
      f0=$(cnt 'window 0 -> 1'); echo "$(date +%T) ATTEMPT $a"
      python3 "$(dirname "$0")/story2_route.py" "$R" --secs 100 --tag "r2a$a" --waypoints "$WPS;$goal" || bail "route"
      sleep 2; d "route2-$a-end" 0.5; w "practice status" 1
      if [ "$(cnt 'window 0 -> 1')" -gt "$f0" ]; then
        sleep 2; d "route2-$a-window" 0.5; m "route2-$a-window" 2
        echo "-- the window:"; tail -n +"$ml0" "$R/somvr.log" | grep -E '^BODYMOMENT t=.*window|^SESSION ui' | tail -3 | cut -c1-120
        [ $a -lt 3 ] || break
        c0=$(cnt 'window 1 -> 0'); tap R a 0.4
        for i in $(seq 1 30); do sleep 1; [ "$(cnt 'window 1 -> 0')" -gt "$c0" ] && inworld && break; done
        sleep 0.5; echo "$(date +%T) retried: $(state)"
      else
        echo "$(date +%T) no window after attempt $a: the objective?"; for i in $(seq 1 8); do d "route2-$a-after$i" 2.0; done
        w "practice status nav" 2; break
      fi
    done
    echo "-- the route2 moments:"; moments "$ml0" 60 | grep -v '^PROMPTS' | tail -30
    # leave the mission (so the next steps' teleports do not fail it): the failed screen's B or the pause menu's
    if [ "$(cnt 'window 0 -> 1')" -gt "$(cnt 'window 1 -> 0')" ]; then tap R b 0.4; sleep 3; state | grep -q 'ctx=menu' && { tap R a 0.4; sleep 4; }; fi
    d route2-left 1 ;;
  squiller)
    step "squiller: Objective_Squiller 26 m from the camp: what is there"
    go_to squiller 36316 3694 -26541; use squiller 2.5 10 ;;
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
