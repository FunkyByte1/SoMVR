#!/bin/bash
# abil27d-drive.sh - build 27d's abilities round 3 (som-b27-abilities3; docs/re-xr-pipeline.md 121): the skills the save
# lacks granted on the run's save copy (`beasts grant`: Taunt, Shadow Chaining, Wraith Distract - the user will buy them);
# the records of the distract and the taunt; the dagger's quick throw by the game's own input (124 tapped) at a fighting
# orc at range; the taunt by the beckon (the open left hand out, palm up, pulled in twice) at a fighting orc; brutalize
# (crouched behind an unaware orc, a witness 10 m beyond him facing Talion, the left trigger held, the stealth thrust);
# Wraith Distract (the bow out, an arrow drawn, X, the release). One headless boot at the practice arrival with
# scripts/headset-build26.env (+ the switches), SOMVR_HANDS_TEST=1 and the layer's test file; dumps for both-eye pictures
# (scripts/xr_compose.py). Scripted hands held as real hands (OpenXR grip: -Z pinky -> thumb, +X out of the left palm).
# ABIL27D_STEPS = any of (in this order; default: info taunt brutal distract chain)
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${ABIL27D_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${ABIL27D_STEPS:-info brutal distract}
T0=$(date +%s)
R=${ABIL27D_RUN:-}
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
d() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.0
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/abil3-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
cnt() { grep -cE "$1" "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
lines() { wc -l < "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
look() { x "head 0 0 0 yaw $1 pitch ${2:-0} s 0.3"; }
LREST="hands test L -22 -35 25 0 0 0"
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back (kill22's)
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST"; w "$REST" 1
grep -E '^(WARN|CONFIG abil3)' "$L" | head -8 | cut -c1-300
DRAWN=0
draw_sword() { [ "$DRAWN" = 1 ] && return 0; DRAWN=1; w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6
  w "hands test R 30 -25 40 0 -90 0" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 0.6; }
probe() { local n0; n0=$(lines); w "abil2 probe" 1.5; since "$n0" '^ABIL2 probe' | cut -c1-1200; }
spawn_far() { w "practice far infantry" 3
  if grep -E '^PRACTICE t=.*spawn ' "$L" | tail -1 | grep -q 'FAILED\|refused'; then
    w "practice back" 6; w "practice go" 7; w "practice far infantry" 3; fi; }
fighter() { w "practice remove" 1.2; w "practice spawn infantry" 3; w "fight guard path" 0.3; w "execute face" 1.5; }
if has info; then
  echo "== info: the skills granted on the run's save copy, the records"
  for s in PSS_Taunt PSS_Shadow_Chaining PSS_Wraith_Distract; do w "beasts grant $s" 1.2; done
  grep -E '^BEASTS t=.* grant' "$L" | tail -3 | cut -c1-200
  n0=$(lines)
  [ -n "${ABIL27D_RECS:-}" ] && for r in PC_Wraith_Ranged_Normal_Distract PC_Taunt PC_StealthTorment; do w "abil rec $r" 1.5; done
  w "abil links PC_Wraith_Dash_Kill_GetThere" 1.5
  since "$n0" "^ABIL (skill t=.* (node|linked) '[^']+'|links)" | cut -c1-200 | head -40
  # the distract's records: its pool (the node's 4th attribute) lists them
  DN=$(since "$n0" "^ABIL skill   linked 1 hash e9b632db" | head -1 | grep -oE "'[A-Za-z0-9_]+'" | head -1 | tr -d "'")
  echo "the distract's first record: ${DN:-none found}"
  [ -n "$DN" ] && w "abil3 distract $DN" 0.8
fi
# the bow (the layer's test hands: the mod's test hands and squeezes off) - abil27c's
BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
bow_out() { w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
  x "$BOW_SLOT" "$R_REST"; sleep 0.3; x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1
  grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | cut -c1-160; }
draw() { x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.4
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  x "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.3; }
xpress() { x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "btn L a 1"; sleep 0.3
  x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "btn L a 0"; sleep 0.5; }
release() { TS=0.1 x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 0"; }
if has distract; then
  echo "== distract: an unaware orc 10 m ahead (his back); the bow, an arrow drawn at him, X at the game's 'UI_Wraith_Distraction' prompt; where he goes"
  w "practice remove" 1.5; w "$REST" 0.3
  bow_out; w "bow arrows 30" 0.5
  for k in ${ABIL27D_DISTRACT:-1 2}; do
    w "practice remove" 1.5; spawn_far
    echo "-- round $k: drawn at him 8 s without X (the control), then X"
    draw; d "distract$k-drawn"; n0=$(lines); w "abil watch 8" 0.2; z 8
    echo "control: $(since "$n0" '^ABIL watch t=.* orc|^PRACTICE t=.* far orc' | tail -3 | cut -c1-160 | tr '\n' '|')"
    n0=$(lines); xpress; d "distract$k-x"; sleep 1.0; d "distract$k-after1"; sleep 3; d "distract$k-after4"; sleep 4; d "distract$k-after8"; sleep 3
    since "$n0" '^ABIL3 t=|^ABIL watch|^PRACTICE t=.* far orc|^STEALTH t=.* aware orc' | head -16 | cut -c1-260
    x "$BOW_STRAIGHT" "$AT_STRING s 0.5" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8
    x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 0"; sleep 0.8
  done
  echo "-- the taunt with the bow out (the game's aim held): its offer at a fighting orc 4 m off; the right (string) hand beckons"
  w "abil2 aim off" 0.3; fighter; w "abil goto 400" 0.5; probe | grep -oE "at [0-9]+ cm \(state '[^']*'\):|PC_Taunt -> [^;]*" | tr '\n' ' '; echo
  PR=
  for r in "0 0 -90" "0 0 90"; do x "$BOW_STRAIGHT" "hand R head 5 -18 45 $r" "btn L squeeze 1"; sleep 0.6; n0=$(lines); w "abil3 palm" 1.0
    l=$(since "$n0" '^ABIL3 t=.* palm \(a test\): the right' | tail -1); echo "R rot $r: ${l#*palm (a test): }" | cut -c1-220
    echo "$l" | grep -q 'open, palm up and free' && { PR="$r"; break; }; done
  if [ -n "$PR" ]; then
    S0=$(cnt '^ABIL2 t=.* (start|NOT started): the taunt|^ABIL3 t=.* the taunt'); n0=$(lines); w "abil watch 4" 0.1
    x "$BOW_STRAIGHT" "hand R head 5 -18 45 $PR" "btn L squeeze 1"; sleep 0.6; d "taunt-bow-before"
    TS=0.25 x "$BOW_STRAIGHT" "hand R head 5 -18 33 $PR s 0.15" "btn L squeeze 1"
    TS=0.3 x "$BOW_STRAIGHT" "hand R head 5 -18 44 $PR s 0.2" "btn L squeeze 1"
    TS=0.25 x "$BOW_STRAIGHT" "hand R head 5 -18 33 $PR s 0.15" "btn L squeeze 1"; d "taunt-bow-beckoned"; sleep 1.5; d "taunt-bow-after"; sleep 2.5
    since "$n0" '^ABIL3 t=|^ABIL2 t=.* (start|NOT)|^ABIL watch' | grep -v 'palm (a test)' | head -14 | cut -c1-260
  fi
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 0.5
  x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; x "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1
  w "practice remove" 1
fi
if has throw; then
  echo "== throw: the game's own quick throw input (124 tapped 100 ms) at a fighting orc 8 m and 4 m off; the game's selection asked first"
  for cm in 800 400; do
    fighter; n0=$(lines); w "abil goto $cm" 0.5
    by=$(since "$n0" '^ABIL t=.* goto:' | tail -1 | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p')
    [ -n "$by" ] && look "${by#+}" 0
    probe | grep -oE "at [0-9]+ cm \(state '[^']*'\):|PC_Ranged_QuickThrow -> [^;]*" | tr '\n' ' '; echo
    n0=$(lines); w "abil watch 3" 0.2; w "verbs press 124 100" 0.2; d "throw$cm-press"; sleep 0.6; d "throw$cm-after"; sleep 2.5
    since "$n0" "^ABIL watch|^VERBS t=.* test press" | head -12 | cut -c1-240
    look 0 0
  done
  if grep -q 'CONFIG abil2 build 27d: the quick throw.s start press' "$L"; then
    echo "-- the throw gesture (SOMVR_ABIL_THROW=on, _START=press): the dagger drawn at the left hip, thrown overhand at the orc 8 m off"
    SLOT="hands test R 10 -10 -15 0 0 0"; OUT_R="hands test R 30 -25 40 0 -90 0"
    draw_sword
    w "hands test L -18 -62 -5 0 0 0" 0.6; w "hands squeeze L 1" 1.0; grep -E '^SLOTS .*dagger' "$L" | tail -1 | cut -c1-160
    for cm in 800 500; do
      fighter; n0=$(lines); w "abil goto $cm" 0.5
      by=$(since "$n0" '^ABIL t=.* goto:' | tail -1 | sed -nE 's/.* ([-+][0-9]+) deg right of the head.*/\1/p')
      [ -n "$by" ] && look "${by#+}" 0
      w "hands test L -16 10 -20 0 0 0" 0.3; d "gesture$cm-wind"
      n0=$(lines); w "hands swing L -6 -8 50 0 0 0 200 0.6" 0.15; d "gesture$cm-release"; sleep 0.5; d "gesture$cm-after"; sleep 2
      since "$n0" '^ABIL2 t=|^ABIL watch|^SLOTS .*dagger' | grep -vE 'the quick throw: (not offered|the game offers)' | head -14 | cut -c1-260
      w "hands squeeze L 1" 0.8; look 0 0
    done
    w "hands squeeze L 0" 0.5; w "$LREST" 0.5
  fi
  w "practice remove" 1
fi
if has taunt; then
  echo "== taunt: a fighting orc 3 m ahead; the left palm calibrated; the open hand out palm up, beckoned twice"
  fighter; w "abil goto 300" 0.5; look 0 0
  for r in "0 0 90" "0 0 -90"; do w "hands test L -8 -18 45 $r" 0.6; n0=$(lines); w "abil3 palm" 1.0
    l=$(since "$n0" '^ABIL3 t=.* palm \(a test\): the left' | tail -1); echo "rot $r: ${l#*palm (a test): }" | cut -c1-200
    echo "$l" | grep -q 'open, palm up and free' && { PR="$r"; break; }; done
  [ -n "$PR" ] || { echo "no palm-up pose found"; PR="0 0 90"; }
  probe | grep -oE "at [0-9]+ cm \(state '[^']*'\):|PC_Taunt -> [^;]*" | tr '\n' ' '; echo
  OUT="hands test L -8 -18 45 $PR"
  P1="hands swing L -8 -18 33 $PR 150"; P2="hands swing L -8 -18 44 $PR 200"
  echo "-- one pull (nothing)"
  n0=$(lines); w "$OUT" 0.8; w "$P1" 0.5; w "$OUT" 2.5
  since "$n0" '^ABIL3 t=.* taunt' | head -4 | cut -c1-220
  echo "-- the beckon"
  S0=$(cnt '^ABIL2 t=.* start: the taunt'); n0=$(lines); w "abil watch 4" 0.1
  w "$OUT" 0.6; d "taunt-before"; w "$P1" 0.25; w "$P2" 0.3; w "$P1" 0.25; d "taunt-beckoned"
  waitfor '^ABIL2 t=.* start: the taunt' "$S0" 2 && echo "taunt started" || echo "no taunt start"
  sleep 1; d "taunt-after"; sleep 3
  since "$n0" '^ABIL3 t=|^ABIL2 t=.* (start|NOT)|^ABIL watch' | head -16 | cut -c1-260
  w "$LREST" 0.5; w "practice remove" 1
fi
# brutalize (kill22's approach: the sword drawn, the far orc's back, the witness, the sneak to the stand-off, the thrust)
PRE="hands test R 20 -27 -18 12 0 0"
READY2="hands swing R 16 -23 0 12 0 0 500"
THRUST="hands swing R 5 -11 54 12 0 0 180"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT_R="hands test R 30 -25 40 0 -90 0"
witness() { w "front keep 1" 0.3; w "verbs ahead ${WIT_CM:-1100}" 0.3; w "practice spawn infantry" 3; w "verbs ahead 350" 0.3; w "front keep 0" 0.3; }
sneak_to() { local r0 n0 i S0=; n0=$(cnt '^STEALTH t=.* stand-off: orc'); r0=$(cnt '^STEALTH t=.* stand-off: at rest')
  x "head 0 -50 0 s 0.5"; sleep 0.5; waitfor '^CROUCH (game|duck) on' "$(cnt '^CROUCH (game|duck) on')" 3; w "stealth chase 0.6 60" 0.3
  for i in $(seq 1 160); do userin && bail "the user is in VR"
    [ "$(cnt '^STEALTH t=.* stand-off: at rest')" -gt "$r0" ] && break
    [ "$(cnt '^STEALTH t=.* stand-off: orc')" -gt "$n0" ] && [ -z "$S0" ] && S0=$i
    [ -n "$S0" ] && [ $((i - S0)) -gt 12 ] && break; sleep 0.25; done
  echo "$(date +%T) $(grep -E '^STEALTH t=.* stand-off: ' "$L" | tail -1 | cut -c1-200)"
  sleep 1.5; w "stealth chase 0" 0.3; sleep 1.0; }
if has brutal; then
  echo "== brutalize: crouched behind an unaware orc, a witness 10 m beyond him facing Talion; the left trigger held, the thrust"
  draw_sword; w "front keep 0" 0.3
  for k in ${ABIL27D_BRUTAL:-brutal1 brutal2}; do
    case $k in *:*) WIT_CM=${k#*:}; k=${k%%:*} ;; esac
    w "practice remove" 1.5; spawn_far
    w "$PRE" 0.4; w "verbs behind 220" 0.8; w "execute face" 1.0; sneak_to
    w "$PRE" 0.4; witness; w "execute face" 0.8; w "$READY2" 0.8; probe | grep -oE "at [0-9]+ cm \(state '[^']*'\):|PC_StealthTorment -> [^;]*|PC_InstaKill_Sneak_Loud -> [^;]*" | tr '\n' ' '; echo
    d "$k-ready"
    B0=$(cnt '^ABIL3 t=.* brutalize|^STEALTH kill'); n0=$(lines)
    w "hands trigger L 1" 0.4
    # the thrust (180 ms; then 260 and 340 ms if the frames missed the hand's own push: run 20261031-004828's "carried")
    for ms in ${ABIL27D_THRUSTS:-180 260 340}; do
      w "hands swing R 5 -11 54 12 0 0 $ms" 0.25; d "$k-thrust$ms"
      waitfor '^ABIL3 t=.* brutalize|^STEALTH kill' "$B0" 2 && break
      echo "$(date +%T) the $ms ms thrust: no verdict - drawn back slowly"; w "$READY2" 1.2; done
    sleep 0.6; d "$k-sync"; TS=0.6 x "head 0 -50 0 pitch -35 s 0.3"; d "$k-sync-down"; sleep 1.0; d "$k-after"
    TS=0.4 x "head 0 -50 0 s 0.3"; sleep 1.6; d "$k-after2"
    sleep 4; w "hands trigger L -1" 0.3
    since "$n0" '^ABIL3 t=|^ABIL2 t=.* (start|NOT)|^ABIL watch|^STEALTH (kill|t=.* (contact|touch|kill by))|^BODYMOMENT t=.* act' | head -40 | cut -c1-260
  done
  x "head 0 0 0 s 0.4"; w "crouch stand" 0.4; w "practice remove" 2; w "$REST" 0.4
fi
if has chain; then
  echo "== chain: two orcs 9 m and 6 m ahead; at once Y (the vision), A at the nearer (the strike), then during the strike's kill the gaze on the other and A again"
  x "btn L b 0"; w "hands test off" 0.3; w "bow arrows 40" 0.5; look 0 0
  w "practice remove" 1.5; w "front keep 1" 0.3; w "verbs ahead 900" 0.3; w "practice spawn infantry" 2.5
  w "verbs ahead 600" 0.3; w "practice spawn infantry" 2.5; w "verbs ahead 350" 0.3; w "front keep 0" 0.3
  # orc N's bearing from the head (the gaze's test word): "yaw pitch"
  ob() { local n0 i l; n0=$(cnt "^STRIKE gaze orc $1:"); w "bow strike gaze orcs" 0.1
    for i in $(seq 1 12); do [ "$(cnt "^STRIKE gaze orc $1:")" -gt "$n0" ] && break; sleep 0.25; done
    l=$(grep -E "^STRIKE gaze orc $1:" "$L" | tail -1)
    echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1 \2/p'; }
  HY=0; HP=0
  lookat() { local yp; yp=$(ob "$1"); [ -n "$yp" ] || return 1; set -- $yp
    HY=$(awk -v a="$HY" -v b="$1" 'BEGIN{printf "%.1f", a+b}'); HP=$(awk -v a="$HP" -v b="$2" 'BEGIN{v=a+b; if (v>30) v=30; if (v<-30) v=-30; printf "%.1f", v}')
    TS=0.3 x "btn L b 1" "head 0 0 0 yaw $HY pitch $HP s 0.2"; }
  pressa() { TS=0.25 x "btn L b 1" "head 0 0 0 yaw $HY pitch $HP" "btn R a 1"; TS=0.25 x "btn L b 1" "head 0 0 0 yaw $HY pitch $HP" "btn R a 0"; }
  n0=$(lines); TS=0.6 x "btn L b 1"
  lookat 0; sleep 0.5; d "chain-mark1"; pressa
  waitfor '^STRIKE t=.* (start|no move)|gaze: no Shadow Strike prompt' "$(since "$n0" '^STRIKE t=.* (start|no move)|gaze: no Shadow Strike prompt' | wc -l)" 3
  sleep 0.4; d "chain-struck1"
  lookat 1; sleep 0.4; d "chain-mark2"; pressa
  sleep 1.0; d "chain-struck2"; sleep 3; x "btn L b 0"; sleep 1
  since "$n0" '^STRIKE t=.* (gaze: (A at|marked|the mark moved|no Shadow|unmarked|A without)|press|start|end)|^STRIKE gaze orc|^VISION t=.* flag|^ABIL watch' | head -30 | cut -c1-240
  w "practice remove" 1; look 0 0
fi
echo "== the run's summary"
w "abil3 status" 1
grep -E '^ABIL3 (status|END)' "$L" | tail -1 | cut -c1-900
echo "$(date +%T) done"; [ -n "${ABIL27D_NOQUIT:-}" ] || echo quit >> "$R/cmd"
