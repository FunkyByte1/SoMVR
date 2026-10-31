#!/bin/bash
# abil27c-drive.sh - build 27c's abilities round 2 (som-b27-abilities2; docs/re-xr-pipeline.md 118): the records of the
# ignite shot, the quick throw and the whistle; the game's offers against an unaware practice orc 10 m ahead; then the VR
# gestures with scripted hands held as real hands: the whistle (a hand held at the mouth; first at the chest: nothing),
# the quick throw (the dagger drawn at the left hip, thrown overhand and let go mid-swing; first an underhand toss and a
# throw away from the orc: no start), the ignite (the bow out, an arrow drawn, B, the release; the shot's ray turned onto
# the nearest fire pit or explosive by `abil2 aim N`; then a plain arrow at the next one). One headless boot at the
# practice arrival with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, SOMVR_PRACTICE_DAY=0 and SOMVR_FX_TEST=1 (the
# fires light: `fx console GameServerShell.ChangeWeather SON_Day03`, the practice orc out), the layer's test file; marks
# and dumps for both-eye pictures (scripts/xr_compose.py).
# ABIL27C_STEPS = any of (in this order; default: info whistle ignite throw): info whistle ignite throw
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${ABIL27C_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${ABIL27C_STEPS:-info whistle ignite throw}
T0=$(date +%s)
R=${ABIL27C_RUN:-}
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
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
d() { x "${2:-dump}" ${2:+dump}; sleep 0.6; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/abil2-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
lines() { wc -l < "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
YAW=0
look() { YAW=$1; x "head 0 0 0 yaw $YAW pitch ${2:-0} s 0.3"; }
LREST="hands test L -22 -35 25 0 0 0"
REST="hands test R 28 -40 20 0 0 0"
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST"; w "$REST" 1
grep -E '^(WARN|CONFIG abil2)' "$L" | head -8 | cut -c1-300
spawnfar() { w "practice remove" 1.5; w "practice far infantry" 5; }
probe() { local n0; n0=$(lines); w "abil2 probe" 1.5; since "$n0" '^ABIL2 probe' | cut -c1-900; }
if has info; then
  echo "== info: the records, the offers with no orc"
  probe
  for r in PC_Wraith_Ranged_Immolate_Shoot PC_Ranged_QuickThrow PC_Attract; do w "abil rec $r" 1.5; done
fi
crouch() { x "head 0 -45 0 yaw 0 pitch 0 s 0.3"; sleep 1.5; }
stand() { x "head 0 0 0 yaw 0 pitch 0 s 0.3"; sleep 1; }
if has whistle; then
  echo "== whistle: an unaware orc 10 m ahead, crouched (the duck: the stealth the game's whistle wants); the hand at the chest (nothing), then at the mouth"
  spawnfar; crouch; probe
  W0=$(cnt '^ABIL2 t=.* whistle: '); S0=$(cnt '^ABIL2 t=.* start: the whistle')
  w "hands test L -10 -32 25 0 0 0" 1.5
  [ "$(cnt '^ABIL2 t=.* whistle: ')" -eq "$W0" ] && echo "the hand at the chest: nothing (right)" || echo "THE CHEST WHISTLED (wrong)"
  d "whistle-before"
  n0=$(lines); w "hands test L 0 -9 9 0 0 0" 0.2
  waitfor '^ABIL2 t=.* start: the whistle' "$S0" 3 && echo "whistle started" || echo "no whistle start"
  d "whistle-at-mouth"; sleep 1.5; w "$LREST" 0.3; d "whistle-after"; sleep 3; d "whistle-after2"
  since "$n0" '^ABIL2 t=|^ABIL watch' | head -20 | cut -c1-300
  stand
fi
# the bow (the layer's test hands: the mod's test hands and squeezes off)
BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
bow_out() { w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
  x "$BOW_SLOT" "$R_REST"; sleep 0.3; x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1
  grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | cut -c1-160; }
draw() { x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.4
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  x "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.3; }
bpress() { x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "btn R b 1"; sleep 0.3
  x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "btn R b 0"; sleep 0.5; }
release() { TS=0.1 x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 0"; }
if has ignite; then
  echo "== ignite: an unaware orc 10 m ahead; the bow, an arrow drawn, B (kindled), the release at his head; then a plain arrow at a fresh one; then B and a let-down"
  n0=$(lines); w "abil2 props" 1.5; since "$n0" '^ABIL2 props' | head -6 | cut -c1-200
  bow_out; w "bow arrows 30" 0.5
  for k in ignite plain; do
    spawnfar
    draw
    [ "$k" = ignite ] && bpress
    d "$k-drawn"
    n0=$(lines); w "bow aimhead" 0.3; w "abil watch 8" 0.2
    release
    sleep 0.3; d "$k-hit"; sleep 1.0; d "$k-after"; sleep 2.0; d "$k-after2"; sleep 4
    since "$n0" '^ABIL2 t=|^ABIL watch|^BOW (t=.* (assist|dropped|test)|hit)' | head -24 | cut -c1-300
  done
  echo "-- B, then a let-down: put out"
  draw; bpress
  x "$BOW_STRAIGHT" "$AT_STRING s 0.5" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 0"; sleep 0.8
  grep -E '^ABIL2 t=.* (kindled|put out)' "$L" | tail -2 | cut -c1-200
  x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; x "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1
  w "practice remove" 1
fi
# the sword drawn (abil27's way), the dagger: the left hand at the hip, the grip closed (SOMVR_SLOTS' Acharn)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
dagger() { w "hands test L -18 -62 -5 0 0 0" 0.6; w "hands squeeze L 1" 1.0
  grep -E '^SLOTS .*dagger' "$L" | tail -1 | cut -c1-200; }
WIND="hands test L -16 10 -20 0 0 0"           # the hand up behind the left shoulder, the dagger held
THROW="hands swing L -6 -8 50 0 0 0 200 0.6"   # forward over the shoulder to ahead at eye level in 200 ms, let go at u 0.6
TOSS_A="hands test L -20 -62 -12 0 0 0"; TOSS="hands swing L -14 -30 45 0 0 0 200 0.6"  # underhand from the hip
if has throw; then
  echo "== throw: an orc in the fight at range - Talion teleported 12 / 8 m short of him (abil goto), the dagger drawn, the game's offer watched as he charges; the throw at him when offered"
  w "hands test off" 0.3; w "$LREST" 0.3; w "$REST" 0.5
  drawn || { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
  T0s=$(cnt '^ABIL2 t=.* start: the quick throw')
  dagger
  O='^ABIL2 t=.* the quick throw: the game offers it'
  for cm in 1200 800 1200 800; do
    w "practice remove" 1.2; w "practice spawn infantry" 3; w "fight guard path" 0.3; w "execute face" 2
    o0=$(cnt "$O"); n0=$(lines); w "abil goto $cm" 0.3
    by=$(since "$n0" '^ABIL t=.* goto:' | tail -1 | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p')
    YAW=0; [ -n "$by" ] && look "${by#+}" 0
    if waitfor "$O" "$o0" 4; then
      l=$(grep -E "$O" "$L" | tail -1); echo "$l" | cut -c1-260
      by=$(echo "$l" | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p'); [ -n "$by" ] && look $(( YAW + ${by#+} )) 0
      w "$WIND" 0.25; d "throw$cm-wind"
      n0=$(lines); w "$THROW" 0.15; d "throw$cm-release"; sleep 0.25; d "throw$cm-flight"; sleep 1.0; d "throw$cm-after"; sleep 1.5
      since "$n0" '^ABIL2 t=|^ABIL watch|^SLOTS .*dagger' | grep -vE 'the quick throw: (not offered|the game offers)' | head -16 | cut -c1-300
    else
      echo "not offered in 4 s ($cm cm):"; since "$n0" '^ABIL2 t=.* the quick throw: not offered' | head -4 | cut -c1-230
    fi
    w "hands squeeze L 1" 0.8
  done
  probe
  echo "quick throws started: $(( $(cnt '^ABIL2 t=.* start: the quick throw') - T0s ))"
  look 0 0; w "hands squeeze L 0" 0.5; w "$LREST" 0.5; w "practice remove" 1
fi
echo "== the run's summary"
w "abil2 status" 1
grep -E '^ABIL2 (status|END)' "$L" | tail -1 | cut -c1-900
echo "$(date +%T) done"; [ -n "${ABIL27C_NOQUIT:-}" ] || echo quit >> "$R/cmd"
