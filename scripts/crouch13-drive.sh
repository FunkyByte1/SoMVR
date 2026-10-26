#!/bin/bash
# crouch13-drive.sh - build 13's crouch by ducking (SOMVR_CROUCH=duck, docs/re-xr-pipeline.md 41), headless, at the
# practice arrival. The duck: the layer's test head (`head 0 -35 0 s 0.4` in RUN/xrtest.txt: SOMVR_XR_TEST); the
# button: `stick R 0 -1`; the walks: the word `stick 0 1 0 S`. Boot (sandbox off):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build13.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
#   (+ SOMVR_SEATED=1 for the seated plan), this script at the same time.
# Plans (CROUCH_PLAN):
#   duck   bobs (no duck), a duck (down, a picture), the band, a crouched walk (the cap), up, the button down/up, a
#          button crouch stood up by a duck's end, a recentre while ducked, a duck near the practice orc
#   hands  the test hands at a player's places, a duck; the sword drawn, a duck with it, up; the orc's recoils (cues)
#   seated with SOMVR_SEATED=1: a duck crouches nothing, the button does
#   stealth (SOMVR_VERBS=on) verbs13-drive.sh's e2 stealth-kill staging, ducked for real
#   try    which command crouches: CROUCH_TRY (default 15 123 125 34) each tapped twice and held 2.5 s (watch lines)
#   probe  the stock Crouch (run it with SOMVR_CROUCH off): the stick's pulse, taps, a hold (run 025439 also took a
#          byte diff of the player's objects: 0 flips; those words are gone)
# Env: CROUCH_PLAN (default duck), CROUCH_TRY, CROUCH_WT. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CROUCH_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${CROUCH_PLAN:-duck}
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
# the head node's height above the anchor's eyes (cm), the last ANCHOR line
hn() { grep '^ANCHOR frame=' "$L" | tail -1 | grep -oE 'head node [-+0-9.]+,[-+0-9.]+' | sed 's/.*,//'; }
watchhn() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; echo "  $(date +%T) head node y $(hn)"; sleep 1; done; }
if [ "$PLAN" = probe ]; then
  # 1. the stick's pulse (one tick of Crouch, with SOMVR_CROUCH off): the head node before and after
  echo "standing:"; watchhn 3
  x "stick R 0 -1" 0.6; x "stick R 0 0" 0.2; echo "after pulse 1:"; watchhn 5
  m crouch-pulse1
  x "stick R 0 -1" 0.6; x "stick R 0 0" 0.2; echo "after pulse 2:"; watchhn 5
  m crouch-pulse2
  # 2. a tap of 3 ticks (ours), then a 3 s hold: does a hold crouch him only while held?
  w "crouch press 3" 0.2; echo "after a 3-tick tap:"; watchhn 5
  w "crouch press 3" 0.2; echo "after a second tap:"; watchhn 5
  w "crouch hold 3000" 0.2; echo "during a 3 s hold, then after:"; watchhn 7
  # 3. the stick walk crouched (after a tap) and standing: the root's speed in the LOCO lines
  w "crouch press 3" 0.2; watchhn 2; w "stick 0 1 0 3" 4; watchhn 2
  w "crouch press 3" 0.2; watchhn 2; w "stick 0 1 0 3" 4; watchhn 2
  w "crouch status" 1
  grep -c 'VIN press cmd=15' "$L"
fi
if [ "$PLAN" = try ]; then
  # which command crouches Talion: each held 2.5 s, and tapped 150 ms twice (a toggle stays after the first tap); the
  # CROUCH watch lines (5 a second: the head node over the root, the FSM, the eyes' push) during and 3 s after
  for c in ${CROUCH_TRY:-15 123 125 34}; do
    w "crouch try $c 150" 4; w "crouch try $c 150" 4; w "crouch try $c 2500" 6
  done
fi
st() { w "crouch status" 1; grep '^CROUCH status' "$L" | tail -1 | cut -c1-260; }
ons() { grep -c '^CROUCH duck on' "$L"; }
offs() { grep -c '^CROUCH duck off' "$L"; }
speed() { grep '^LOCO t=.* last 1 s' "$L" | tail -1 | grep -oE 'asked [0-9.]+ cm, the body [0-9.]+ cm'; }
if [ "$PLAN" = duck ]; then
  # 1. standing: the standing height, a picture
  st; watchhn 2; m duck-stand
  # 2. bobs and a look at the floor (10, 18 cm down): no crouch
  n0=$(ons); x "head 0 -10 0 s 0.3" 1.5; x "head 0 -18 0 s 0.3" 2.5; x "head 0 0 0 s 0.3" 1.5
  echo "bobs: ducks on $(( $(ons) - n0 )) (want 0)"
  # 3. the duck (35 cm down in 0.4 s): the crouch, the head node, a picture
  n0=$(ons); x "head 0 -35 0 s 0.4" 2.5; echo "duck: ducks on $(( $(ons) - n0 )) (want 1)"; watchhn 3; st; m duck-down
  # 4. up to 24 cm down (inside the band): still crouched
  n0=$(offs); x "head 0 -24 0 s 0.3" 2; echo "band: ducks off $(( $(offs) - n0 )) (want 0)"; x "head 0 -35 0 s 0.3" 1
  # 5. a walk crouched: the speed, a picture mid-walk
  w "stick 0 1 0 4" 2; echo "crouched walk: $(speed)"; m duck-walk 0.1; sleep 1; echo "crouched walk: $(speed)"
  # 6. stand up: the stand, a picture; a walk standing
  n0=$(offs); x "head 0 0 0 s 0.4" 2.5; echo "stand: ducks off $(( $(offs) - n0 )) (want 1)"; watchhn 3; st; m duck-up
  w "stick 0 1 0 4" 2; echo "standing walk: $(speed)"; sleep 1.5; echo "standing walk: $(speed)"
  # 6b. the button (the right stick down) standing: down; again: up
  x "stick R 0 -1" 0.6; x "stick R 0 0" 2; st; x "stick R 0 -1" 0.6; x "stick R 0 0" 2; st
  # 7. the stick's crouch (seated players' button) standing, then a duck and a stand: the duck's stand stands him up
  x "stick R 0 -1" 0.6; x "stick R 0 0" 2; watchhn 2; st
  x "head 0 -35 0 s 0.4" 2.5; x "head 0 0 0 s 0.4" 2.5; watchhn 2; st
  # 8. a recentre while ducked: that is the standing height now (he stands); standing up raises it again
  x "head 0 -35 0 s 0.4" 2.5; w recenter 3; st; x "head 0 0 0 s 0.4" 3; st
  # 9. near the practice orc: a duck, a picture (the band's prompts), up
  w "practice spawn infantry" 0.3; sleep 3; x "head 0 -35 0 s 0.4" 2.5; st; m duck-orc; x "head 0 0 0 s 0.4" 2.5; st
  grep -E '^CROUCH (duck|game|stand|button)' "$L" | tail -40 | cut -c1-220
fi
if [ "$PLAN" = hands ]; then
  # the test hands where a player's are (the IK'd arms on them), a duck with them; the sword drawn, a duck with it in
  # the hand (the game's stealth sheathes its melee: does our sword stay in the hand?), up, the sword still there
  REST="hands test R 28 -40 20 0 0 0"; SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
  slots() { grep -E '^SLOTS .*sword: ' "$L" | tail -${1:-2} | cut -c1-200; }
  x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
  grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
  w "hands test L -25 -35 30 0 0 0"; w "$REST" 2; st; m hands-stand
  x "head 0 -35 0 s 0.4" 3; st; m hands-down; x "head 0 0 0 s 0.4" 2.5; st
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 1.5; slots; m sword-stand
  x "head 0 -35 0 s 0.4" 3; st; slots 3; m sword-down
  x "head 0 0 0 s 0.4" 3; st; slots 3; m sword-up
  # a stick walk with the sword, then a duck walking (the cap) and up
  w "stick 0 1 0 3" 1.5; x "head 0 -35 0 s 0.4" 2; echo "walk, ducked: $(speed)"; x "head 0 0 0 s 0.4" 2; slots 3
  # the practice orc: a duck while it fights (the recoils stand him up for a moment): the cues once per hold
  c0=$(grep -c '^CROUCH game' "$L"); w "practice spawn infantry" 0.3; sleep 3; x "head 0 -35 0 s 0.4" 5; st; m hands-orc
  x "head 0 0 0 s 0.4" 2.5; st; echo "game lines with the orc: $(( $(grep -c '^CROUCH game' "$L") - c0 ))"
  grep -E '^CROUCH (duck|game|button)' "$L" | tail -14 | cut -c1-200
fi
if [ "$PLAN" = stealth ]; then
  # with SOMVR_VERBS=on (build 13's env): verbs13-drive.sh's e2 staging (a patrol orc 12 m off, Talion put 3 m behind
  # it, the sneak in, the thrust at its back) but crouched for real (ducked: 125 held) - e2's crouch was the stock 15
  REST="hands test R 22 -35 -30 0 180 0"; SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
  x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
  grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
  w "hands test L -25 -35 30 0 0 0"; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5
  w "$REST" 0.6
  for r in 1 2 3; do
    w "verbs ahead 1200" 0.3; w "verbs template Orc_Infantry_Patrol" 0.3; w "practice spawn infantry" 3; w "verbs status" 3
    x "head 0 -35 0 s 0.4" 1.5; st
    w "verbs behind 300" 0.8; w "execute face" 0.6; w "stick 0 0.35 0 2.5" 2.8; w "execute face" 0.4
    w "verbs tree" 1.2; st; m "st-$r-behind" 0.1
    w "hands swing R 5 -12 75 0 0 0 250" 0.6; m "st-$r-stab" 0.3; w "$REST" 0.5; sleep 1.5; m "st-$r-after" 0.1
    x "head 0 0 0 s 0.4" 1.5; w "practice remove" 2
  done
  w "verbs template off" 0.3; w "verbs ahead 350" 0.3; w "verbs status" 1
  grep -E '^VERBS .*(stealth|tree|offer|kill)' "$L" | tail -20 | cut -c1-220
  grep -E '^CROUCH (duck|game)' "$L" | tail -12 | cut -c1-160
fi
if [ "$PLAN" = seated ]; then
  # SOMVR_SEATED=1: a duck crouches nothing; the right stick down toggles
  st; n0=$(ons); x "head 0 -35 0 s 0.4" 2.5; watchhn 2; echo "seated duck: ducks on $(( $(ons) - n0 )) (want 0)"; st
  x "head 0 0 0 s 0.4" 1.5
  x "stick R 0 -1" 0.6; x "stick R 0 0" 2; watchhn 2; st; m seated-button
  x "stick R 0 -1" 0.6; x "stick R 0 0" 2; watchhn 2; st
  grep -E '^CROUCH (duck|game|stand|button)' "$L" | tail -20 | cut -c1-220
fi
echo "$(date +%T) done"; echo quit >> "$R/cmd"
