#!/bin/bash
# comfort10-drive.sh - build 10's comfort / sound / menu / prompts checks in one headless boot (handoff/HANDOFF-comfort.md
# "Build 10"): the vignette's lens fit at 20 / 40 / 70 % and 0 % (walking with the stick, smooth turning; both-eye dumps),
# the VR menu's rows (no step / dash / phase with SOMVR_GAMEMOVES=off, Music) and its Controls page, the Music row's
# share sent to the game (MUSIC lines), the wraith vision (Y held), the flat game's hints in a fight (SOMVR_HINTS=log,
# then the word "hints off": dropped), A near an orc with SOMVR_PHASE=on (does the phase start with GAMEMOVES=off?).
# Boot (outside the sandbox, the slot checked first; WT = this worktree):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' logs/build-vr9/headset.env | tr '\n' ' ')
#     SOMVR_VIGNETTE_FIT=lens SOMVR_VIGNETTE=0.2 SOMVR_TURN=smooth SOMVR_MUSIC=0.3 SOMVR_HINTS=log SOMVR_PHASE=on
#     SOMVR_STEP=1 SOMVR_XR_DUMP=999999 SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
#     setsid scripts/headless_vr.sh $WT/ShadowOfMordor/mod/somvr
# and this script at the same time (C10_WT: the worktree's name in RUN/launch-cmd.txt). The panel's uvs are build 10's
# layout with the headless avail (14 rows: no MOVES, with MUSIC; somvr_vm_rect_av): Vignette < 0.547 0.2565, > 0.922
# 0.2565; Music < 0.547 0.7513, > 0.922 0.7513; the Controls tab 0.723 0.042, the Settings tab 0.160 0.042. Prints
# PASS/FAIL lines from the logs; ends with a quit. Stops at once (quit) when the user goes into VR.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${C10_WT:-t3-7a4617f8}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
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
  grep -q '"user": true' /run/user/1000/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep 0.6; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { w "mark $1" 4; }
dump() { t "dump"; sleep 1.2; echo "  dump: $1 (frame $(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'))"; }
press() { t "hand R touch panel $1 $2 5"; t "hand R touch panel $1 $2 1.0 s 0.3"; t "hand R touch panel $1 $2 -1.0 s 0.15"
          t "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
opens() { grep -c 'vrmenu: opened by' "$R/xr-layer.log" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 0.8; t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
  [ "$(opens)" -gt "$o" ] && { echo "OPEN by the menu button's long press"; return 0; }
  echo "NOT OPEN"; }
menu_shut() { t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5; }
n() { grep -c "$1" "$R/somvr.log" 2>/dev/null; }
check() { if [ "$(n "$2")" -gt "$3" ]; then echo "PASS $1"; else echo "FAIL $1 (no '$2')"; fi; }
ring() { grep 'xr: comfort frame' "$R/xr-layer.log" | tail -1 | sed 's/.*-> target/target/' | cut -c1-200; }
# walk 3 s, a dump in it; a smooth turn 3 s, a dump in it
walkturn() {
  w "stick 0 1 0 3" 1.6; dump "$1 walking"; ring; sleep 1.5
  w "stick 0 0 1 3" 1.6; dump "$1 turning"; ring; sleep 2.5; }

if [ "${C10_PLAN:-a}" = b ]; then
  # plan b (the second boot): SOMVR_HINTS=off from the start, SOMVR_VISION=hold, the user's 40 % vignette. Boot with
  #   SOMVR_VIGNETTE_FIT=lens SOMVR_VIGNETTE=0.4 SOMVR_TURN=smooth SOMVR_MUSIC=0.3 SOMVR_HINTS=off SOMVR_VISION=hold
  #   SOMVR_XR_DUMP=999999 SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217
  w "gameopts" 1
  check "hints off at start" 'CONFIG hints=off' 0
  check "vision hold" 'CONFIG vision=hold' 0
  # the wraith vision: Y held 3 s (a dump in it), let go (a dump after)
  dump "before Y"
  t "btn L b 1"; sleep 2.5; dump "Y held"; w "gameopts" 1; t "btn L b 0"; sleep 2; dump "Y let go"; w "gameopts" 1
  grep '^VISION\|GAMEOPTS t=' "$R/somvr.log" | tail -6 | grep -o 'VISION.*\|the wraith vision [a-z]*' | cut -c1-160
  # a second hold (does it come back?)
  t "btn L b 1"; sleep 2; w "gameopts" 1; t "btn L b 0"; sleep 2; w "gameopts" 1
  grep 'GAMEOPTS t=' "$R/somvr.log" | tail -2 | grep -o 'the wraith vision [a-z]*'
  # the user's 40 %: a walk
  w "stick 0 1 0 3" 1.6; dump "40% walking (the user's)"; sleep 2
  # an orc: the Shadow Strike hint (run 181931: ~5 s after the spawn, shown 10.8 s) must be dropped; dumps at +4 and +8 s
  D0=$(n ': dropped (SOMVR_HINTS=off')
  w "hands test R 28 -40 20 0 0 0"; w "hands draw" 2
  w "practice spawn infantry" 3.5; dump "orc +4 s"; sleep 2.5; dump "orc +8 s"; sleep 3; w "gameopts" 1
  echo "hints dropped since the spawn: $(( $(n ': dropped (SOMVR_HINTS=off') - D0 ))"
  grep '^HINTS ' "$R/somvr.log" | tail -6 | cut -c1-200
  check "a hint dropped" ': dropped (SOMVR_HINTS=off' 0
  w "practice remove" 3
  m c10b-end
  grep 'GAMEOPTS t=' "$R/somvr.log" | tail -1 | cut -c1-400
  echo "$(date +%T) done"
  echo quit >> "$R/cmd"
  exit 0
fi
w "gameopts" 1
check "music hook" 'CONFIG music=0.30' 0
check "hints hooks" 'CONFIG hints=log' 0
dump "standing (no ring)"
# 1. the vignette at 20 % (SOMVR_VIGNETTE=0.2)
walkturn "20%"
# 2. the menu: its rows (a dump), Vignette > x2 = 40 %
menu_open; t "dump"; sleep 1.5
press 0.922 0.2565; press 0.922 0.2565
check "vignette 40% from the menu" 'VRMENU apply VIGNETTE=0.4 from the menu' 0
menu_shut
walkturn "40%"
# 3. Vignette > x3 = 70 %
menu_open; press 0.922 0.2565; press 0.922 0.2565; press 0.922 0.2565
check "vignette 70% from the menu" 'VRMENU apply VIGNETTE=0.7 from the menu' 0
menu_shut
walkturn "70%"
# 4. Music < (30 -> 20 %), the Controls page (a dump), back; Vignette < x7 = Off
M0=$(n 'MUSIC the VR menu')
menu_open; press 0.547 0.7513; sleep 1
check "music 20% from the menu" 'VRMENU apply MUSIC=0.2 from the menu' 0
check "music sent to the game" 'MUSIC the VR menu' "$M0"
press 0.723 0.042; t "dump"; sleep 1.5
press 0.160 0.042
for k in 1 2 3 4 5 6 7; do press 0.547 0.2565; done
check "vignette off from the menu" 'VRMENU apply VIGNETTE=0 from the menu' 0
menu_shut
RB=$(grep -c ': ring (clear' "$R/xr-layer.log")
w "stick 0 1 0 3" 1.6; dump "0% walking"; sleep 1.5
w "stick 0 0 1 3" 1.6; sleep 2.5
RA=$(grep -c ': ring (clear' "$R/xr-layer.log")
[ "$RA" = "$RB" ] && echo "PASS 0%: no ring line while walking and turning" || echo "FAIL 0%: ring lines $RB -> $RA"
# 5. the wraith vision: Y held 2.5 s
t "btn L b 1"; sleep 2.5; dump "Y held (wraith vision)"; w "gameopts" 1; t "btn L b 0"; sleep 2; w "gameopts" 1
grep 'GAMEOPTS t=' "$R/somvr.log" | tail -2 | grep -o 'the wraith vision [a-z]*'
# 6. a fight with the hints logged: the sword drawn, an orc, idle (it attacks), swings
FORE_A="hands test R 38 -15 22 0 -90 0";  FORE="hands swing R -22 -25 45 0 90 0 300"
BACK="hands swing R 38 -15 22 0 -90 0 300"
pair() { w "$FORE_A" 0.5; w "$FORE" 1.0; w "$BACK" 1.0; }
w "hands test R 28 -40 20 0 0 0"; w "hands draw" 2
H0=$(n '^HINTS ')
w "practice spawn infantry" 5
dump "fight 1 (hints logged)"; sleep 6; dump "fight 1, the orc attacking"
for k in 1 2 3; do pair; done
dump "fight 1 after swings"; w "gameopts" 1
echo "hint events in fight 1: $(( $(n '^HINTS ') - H0 ))"
grep '^HINTS ' "$R/somvr.log" | tail -6 | cut -c1-200
# 7. A near the orc (SOMVR_PHASE=on, SOMVR_GAMEMOVES=off): does the phase start?
S0=$(n '^STEP ')
t "btn R a 1"; sleep 0.3; t "btn R a 0"; sleep 2
echo "STEP lines after A: $(( $(n '^STEP ') - S0 ))"; grep '^STEP ' "$R/somvr.log" | tail -3 | cut -c1-220
# 8. the hints off: a new orc, idle, swings
w "hints off" 1
H1=$(n ': dropped (SOMVR_HINTS=off')
w "practice spawn infantry" 5
sleep 6; dump "fight 2 (hints off)"
for k in 1 2 3; do pair; done
dump "fight 2 after swings"; w "gameopts" 1
echo "hints dropped in fight 2: $(( $(n ': dropped (SOMVR_HINTS=off') - H1 ))"
grep '^HINTS ' "$R/somvr.log" | tail -6 | cut -c1-200
w "practice remove" 3
m c10-end
grep 'GAMEOPTS t=' "$R/somvr.log" | tail -1 | cut -c1-400
echo "$(date +%T) done"
echo quit >> "$R/cmd"
