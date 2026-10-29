#!/bin/bash
# screens22-drive.sh - som-b22-screens' proof (handoff/HANDOFF-drill.md "build 22", docs/re-xr-pipeline.md 90): the
# game's screens with the laser and the fingertip, and a death in first person, in one headless boot with
# scripts/headset-build22.env + the test hands + the drill's real-world overrides (SOMVR_PRACTICE=1 _QUIET=0 _CLEAR=0
# _MINHEALTH=0 _DAY=0: the save's tower top, Talion can die). Steps (SC_PLAN, default "army runes skills map death"):
#   army   the pause menu (the left menu button) > Sauron's Army by the laser; the laser held over a captain with
#          `screens mouse1 off` (build 21: nothing) and `nemesis` (the selection follows), the trigger (his card), B; the
#          fingertip on another captain and pressed (selected, his card), B, B
#   runes  Weapons & Runes by the fingertip: the sword's second rune row, a rune pressed twice (picked, equipped)
#   skills Upgrades by the fingertip: an unbought ability pressed twice (picked, bought)
#   map    the map (the right stick's click): its picture, B
#   death  `practice go` to the P8 fight camp, the sword drawn, held still until Talion dies: the eyes every 2 s while
#          dying (first person, greyed, no window), the Nemesis screens (the window, the theatre) - the laser on a
#          captain there and its trigger -, B until the world, the respawn watched
# Every step prints PASS/FAIL lines; pictures via scripts/scr22.sh (flat shots shots/scr-NAME.png, eyes dumps).
# Stops at once (quit) when the user goes into VR.
HERE=$(cd "$(dirname "$0")" && pwd); S=$HERE/scr22.sh
PLAN=${SC_PLAN:-army runes skills map death}; PLAN=${PLAN//_/ }
NP=0; NF=0
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
chk() { "$S" "$@" || { [ $? = 3 ] && exit 3; }; }
T0=$(date +%s)
for i in $(seq 1 120); do "$S" find > /dev/null 2>&1 && R=$(cat /tmp/b22scr/run) && [ "$(stat -c %Y "$R")" -ge $((T0 - 60)) ] && break; R=; sleep 5; done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log
for i in $(seq 1 150); do [ "$(grep -cE '^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' "$L" 2>/dev/null)" -ge 2 ] && break; sleep 3; done
[ "$(grep -cE '^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world' "$L")" -ge 2 ] || { echo "no arrival"; "$S" quit; exit 1; }
echo "arrived $(date +%T)"; sleep 6
cnt() { grep -cE "$1" "$L"; }
last() { grep -E "$1" "$L" | tail -1 | cut -c1-${2:-230}; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'ctx=world'; }
flat_wait() { for i in $(seq 1 20); do grep -E '^INSPECT t=[0-9.]* (flat|eyes)' "$L" | tail -1 | grep -q ' flat:' && return 0; sleep 0.5; done; return 1; }
echo "== 0. the start"
grep -E '^(CONFIG screens|HOOK SCR_NEM_PAD)' "$L" | cut -c1-220
grep -q '^HOOK SCR_NEM_PAD ok' "$L" && pass "SCR_NEM_PAD patched" || fail "SCR_NEM_PAD"
grep -q 'menus: .*0\.860 x' "$X" && pass "the board 0.86 m wide: $(grep -o 'placed at [^;]*0\.860 x [0-9.]* m' "$X" | head -1)" ||
  echo "(the board's placement comes with the first menu)"
"$S" t "btn R squeeze 0"; "$S" t "btn R squeeze 1"; sleep 1; "$S" t "btn R squeeze 0"
grep -q 'card: put away' "$X" || { "$S" t "btn R a 1"; "$S" t "btn R a 0"; }
"$S" word "hands test L -25 -35 30 0 0 0"; "$S" t "hand L head -25 -35 30 0 0 0"; "$S" word "hands test R 28 -40 20 0 0 0"; sleep 1
pause_open() { inworld || return 0; "$S" tap L menu 0.3; sleep 2.5; flat_wait; sleep 0.5; }
pause_close() { for i in 1 2 3 4; do inworld && return 0; "$S" off; "$S" tap R b; sleep 1.5; done; inworld || { "$S" tap L menu 0.3; sleep 2; }; }
for p in $PLAN; do
  case $p in
  army)
    echo "== army: Sauron's Army by the fingertip; the hover and the press by the fingertip, then by the laser"
    pause_open; "$S" shot pause > /dev/null
    grep -q 'menus: .*0\.860 x' "$X" && pass "the board 0.86 m wide: $(grep -o 'board placed at .*0\.860 x [0-9.]* m' "$X" | tail -1)" ||
      fail "the board's width: $(grep -o 'board placed at .*m (tilt' "$X" | tail -1)"
    "$S" touch 0.12 0.231; sleep 3; "$S" shot army > /dev/null
    "$S" word screens status; sleep 1
    last '^SCREENS t=.* status:' | grep -q 'top layer is the Nemesis' && pass "army by the fingertip: the hierarchy on top" ||
      fail "army by the fingertip: $(last '^SCREENS t=.* status:')"
    h0=$(cnt 'Nemesis hover on'); m0=$(cnt 'Mouse1 \(command 100\) held')
    "$S" t "hand R touch board 0.70 0.62 4"; sleep 2; "$S" t "hand R touch board 0.70 0.62 1.0 s 0.3"; sleep 0.7
    "$S" shot army-fhover > /dev/null   # (the touch is on within 3 cm: the hover from here, run 20261029-022848)
    [ "$(cnt 'Nemesis hover on')" -gt "$h0" ] && pass "the fingertip's hover: $(last 'Nemesis hover on' 200 | cut -c20-)" || fail "no hover by the fingertip"
    "$S" t "hand R touch board 0.70 0.62 -1.0 s 0.15"; sleep 0.6
    "$S" t "hand R touch board 0.70 0.62 4 s 0.2"; sleep 2; "$S" shot army-fpress > /dev/null
    [ "$(cnt 'Mouse1 \(command 100\) held')" -gt "$m0" ] && pass "the fingertip's press: $(last 'Mouse1 \(command 100\) held' 200 | cut -c20-)" ||
      fail "no Mouse1 by the fingertip"
    "$S" off; sleep 1
    h0=$(cnt 'Nemesis hover on')
    "$S" aimto 0.30 0.58 | tail -1; sleep 1.5; "$S" shot army-lhover > /dev/null
    [ "$(cnt 'Nemesis hover on')" -gt "$h0" ] && pass "the laser's hover: $(last 'Nemesis hover on' 200 | cut -c20-)" || fail "no hover by the laser"
    "$S" trig; sleep 2; "$S" shot army-lpress > /dev/null
    "$S" off; "$S" tap R b; sleep 2; "$S" shot army-out > /dev/null ;;
  runes)
    echo "== runes: Weapons & Runes by the fingertip"
    pause_open
    "$S" touch 0.12 0.287; sleep 3; "$S" shot wr-touch > /dev/null
    "$S" touch 0.20 0.375; sleep 2.5; "$S" shot wr-grid > /dev/null
    "$S" touch 0.172 0.213; sleep 2; "$S" shot wr-pick > /dev/null
    "$S" touch 0.172 0.213; sleep 2.5; "$S" shot wr-equip > /dev/null
    echo "  fingertip presses: $(grep -c 'xr: touch: .* edges 0x[0-9a-f]*[13579bdf] ' "$X") (the layer), clicks sent $(last '^POINTER (down|up)' 120)"
    "$S" off; pause_close ;;
  skills)
    echo "== skills: Upgrades by the fingertip"
    pause_open
    "$S" touch 0.10 0.344; sleep 3; "$S" shot up-touch > /dev/null
    "$S" touch 0.073 0.344; sleep 2; "$S" shot up-pick > /dev/null
    "$S" touch 0.073 0.344; sleep 2.5; "$S" shot up-buy > /dev/null
    "$S" off; pause_close ;;
  map)
    echo "== map"
    inworld || pause_close
    "$S" tap R stick; sleep 3; "$S" shot map > /dev/null; sleep 0.5
    "$S" tap R b; sleep 2; inworld || { "$S" tap R stick; sleep 2; } ;;
  death)
    echo "== death: the fight camp, held still until Talion dies"
    inworld || pause_close
    A0=$(cnt 'arrived at .* in the practice area')
    "$S" word practice go 33774.72 3733.07 -28189.33
    for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
    sleep 2
    "$S" word "hands test R 10 -10 -15 0 0 0"; "$S" word "hands squeeze R 1"; "$S" word "hands test R 30 -25 40 0 -90 0"
    "$S" word "hands squeeze R 0"; "$S" word "hands test R 28 -40 20 0 0 0"
    L0=$(wc -l < "$L"); DT=$(date +%s); dead=0
    while [ $(( $(date +%s) - DT )) -lt "${SC_FIGHT_S:-240}" ]; do
      tail -n +"$L0" "$L" | grep -qE '^SCREENS t=.* the death: dying' && { dead=1; break; }; sleep 2
    done
    echo "dying=$dead after $(( $(date +%s) - DT )) s"
    [ $dead = 1 ] || { fail "no death in ${SC_FIGHT_S:-240} s"; continue; }
    k=0; P0=$(date +%s)
    while [ $(( $(date +%s) - P0 )) -lt 40 ]; do
      k=$((k + 1)); "$S" dump "dying$k"; "$S" shot "dying$k" > /dev/null
      grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'paused=1' && break; sleep 1
    done
    tail -n +"$L0" "$L" | grep -E '^(SCREENS t|BODYMOMENT t=.* (cond|act))' | head -14 | cut -c1-230
    tail -n +"$L0" "$L" | grep -qE '^BODYMOMENT t=.*window 0 -> 1' && w1=$(tail -n +"$L0" "$L" | grep -E '^BODYMOMENT t=.*window 0 -> 1' | head -1) || w1=""
    pz=$(tail -n +"$L0" "$L" | grep -E '^SESSION ui .*paused=1' | head -1 | grep -o 't=[0-9.]*' | cut -c3-)
    wt=$(echo "$w1" | grep -o 't=[0-9.]*' | cut -c3-)
    [ -n "$pz" ] && [ -n "$wt" ] && awk -v w="$wt" -v p="$pz" 'BEGIN { exit !(w + 0.05 >= p) }' &&
      pass "no window while dying and the game ran; the window at the pause (+$(awk -v w="$wt" -v p="$pz" 'BEGIN { printf "%.2f", w - p }') s)" ||
      fail "the window: '$w1' (the pause at ${pz:-none})"
    sleep 4; "$S" shot nemesis1 > /dev/null; sleep 4; "$S" shot nemesis2 > /dev/null
    grep 'xr: theatre [0-9]*: ' "$X" | tail -1 | cut -c1-200
    echo "-- the laser on a captain at the death's board (when it is up), its trigger"
    for i in $(seq 1 8); do "$S" shot "nemesis-b$i" > /dev/null; "$S" word screens status; sleep 1
      last '^SCREENS t=.* status:' | grep -q 'top layer is the Nemesis' && break; sleep 3; done
    echo "  $(last '^SCREENS t=.* status:' 200)"
    "$S" aimto 0.49 0.60 | tail -1; sleep 1.5; "$S" shot nemesis-hover > /dev/null
    "$S" trig; sleep 2.5; "$S" shot nemesis-card > /dev/null; "$S" off; "$S" tap R b; sleep 2
    echo "-- B until the world, the respawn"
    for i in $(seq 1 12); do inworld && break; "$S" tap R b; sleep 3; done
    RT0=$(date +%s)
    for i in 1 2 3 4 5 6 7 8; do "$S" dump "respawn$i"; "$S" shot "respawn$i" > /dev/null; sleep 0.5; done
    tail -n +"$L0" "$L" | grep -E '^(SCREENS t|BODYMOMENT t=.* act)' | tail -6 | cut -c1-230
    grep -E '^SCREENS t=.* the death: idle' "$L" | tail -1 | grep -q 'respawns 1' && pass "the respawn: $(last 'the death: idle' 200 | cut -c20-)" || fail "no respawn line" ;;
  esac
done
echo "steps done $(date +%T): PASS $NP, FAIL $NF"
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
[ "${SC_NOQUIT:-0}" = 1 ] || "$S" quit
