#!/bin/bash
# climb-drive.sh - the climb helper's live words for one headless boot (SOMVR_CLIMB=b with SOMVR_HANDS_TEST=1,
# SOMVR_MARK=1, SOMVR_XR_TEST=@RUN@/xrtest.txt; from build7-drive.sh): in the practice meadow, find a climbable wall
# with `climb probe` (12 bearings x 4 heights from the view's head, mod somvr.c cl_probe), face it with snap turns, walk
# to it with the test stick, reach both test hands to it, squeeze and pull down hand over hand (`hands swing`: the grip
# moves in MS ms of the views' time; the body moves by minus the holding hand's motion), let go of both (the fall), walk
# again. Waits for the newest boot-check run of this worktree (CLIMB_WT, default: the worktree this script is in), then
# for the practice arrival; ends with a quit. Grips: `hands test L|R x y z rx ry rz` = cm right/up/ahead of the head's
# yaw frame, deg about the OpenXR grip's X Y Z (0 0 0 = the fingers ahead); `hands squeeze L|R V` (-1 = the
# controller's again). Marks: `m NAME` = RUN/marks/<time>_<n>/eyes.jpg (both eyes), listed in RUN/marks.log.
# CLIMB_PLAN=f (at the wall: back off, the free-run latch, run at it: the stock climb, with SOMVR_GAMEMOVES=on) | o (p,
# then the practice infantry while hanging: the stand-down) | p (find, climb, fall, walk) | t (p, and where a hand finds no wall above: the palm down over the top,
# pushed down then toward the body; SOMVR_CLIMB_TOP decides) | s (p with slow pulls: 1.6 s each). CLIMB_PULLS = the most pull pairs (default 4). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CLIMB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${CLIMB_PLAN:-p}
PULLS=${CLIMB_PULLS:-4}
PULL_MS=${CLIMB_PULL_MS:-1000}
[ "$PLAN" = s ] && PULL_MS=1600
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
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
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
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
# waitfor PATTERN SECS: until one more somvr.log line matches (ERE) than when called; 1 = it came
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
# probe [up]: the word, then its lines (the newest of each height)
probe() { local n0 i; n0=$(grep -c '^CLIMB probe surfaces' "$R/somvr.log"); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(grep -c '^CLIMB probe surfaces' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.25; done
  grep '^CLIMB probe' "$R/somvr.log" | tail -$([ -n "$1" ] && echo 2 || echo 5) | cut -c1-420; }
# near H: the nearest held hit (C = CanWallClimb, h = held by SOMVR_CLIMB_SURF) at head H (-50 / +0) of the newest
# probe: "BEARING DIST" (empty: none)
near() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
# ahead H: the bearing-0 distance at head H of the newest probe (empty: nothing or not climbable)
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbing() { grep -E '^CLIMB t=.* (start|end):' "$R/somvr.log" | tail -1 | grep -q ' start:'; }
headnow() { grep -E '^CLIMB t=.*(last 1 s|top:)' "$R/somvr.log" | tail -1 | grep -oE 'head [0-9.]+' | tail -1 | cut -d' ' -f2; }
topnow() { grep -E '^CLIMB t=.* grab [LR]:.*edge 1, top ' "$R/somvr.log" | tail -1 | grep -oE 'top [0-9.]+' | cut -d' ' -f2; }
lastcl() { grep -E '^CLIMB t=' "$R/somvr.log" | tail -${1:-3} | cut -c1-330; }

# 0. the controls card away (the right grip, the layer's test button), both hands' test squeezes open
m k00-arrival 0.5
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" && echo "$(date +%T) CARD put away by the grip" || { echo "$(date +%T) CARD not put away by the grip: A"; x "btn R a 1"; x "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
w "climb probe table" 1; grep '^CLIMB table' "$R/somvr.log" | cut -c1-600
# 1. find the nearest climbable wall, face it
probe
NB=$(near -50); [ -n "$NB" ] || NB=$(near +0)
echo "$(date +%T) nearest climbable: ${NB:-none}"
[ -n "$NB" ] || { m k01-no-wall 0; w "climb probe up"; echo quit >> "$R/cmd"; exit 1; }
turnto "${NB% *}"
probe
# 2. walk to it: the test stick ahead until the view's head is within ~60 cm of it (bearing 0 at head -50 or +0)
for k in $(seq 1 25); do
  d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
  [ -n "$d" ] || { NB=$(near -50); echo "$(date +%T) nothing ahead; nearest ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"; probe >/dev/null; continue; }
  echo "$(date +%T) ahead $d cm"
  [ "$d" -le 62 ] && break
  if [ "$d" -gt 500 ]; then w "stick 0 1 0 2" 2.4; elif [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
  probe >/dev/null
  [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && { echo "$(date +%T) stopped at $d cm (the wall)"; break; }
  PREV=$d
done
probe up
m k01-at-wall 0.3
if [ "$PLAN" = f ]; then
  # design-vr.md 4 B, report only: the stock climb under the free-run latch (needs SOMVR_GAMEMOVES=on): back off 3 m,
  # click the left stick (the latch: FreeRun held until the stick rests 0.3 s), run at the wall, watch who moves Talion
  w "stick 0 -1 0 1.5" 2.2
  probe >/dev/null
  # the click while the stick moves (the latch lets go 0.3 s after the stick rests: run 064713's click at rest did)
  w "stick 0 1 0 6" 0.4
  x "btn L stick 1"; x "btn L stick 0"
  m kf1-run 0.3; m kf2-wall 0; m kf3-wall 0; m kf4-after 0
  sleep 1
  grep -E '^LOCO t=.*owner' "$R/somvr.log" | tail -8 | cut -c1-220
  grep -E '^(COMFORT|ANCHOR)' "$R/somvr.log" | tail -3 | cut -c1-260
  probe >/dev/null; grep '^CLIMB probe t=.*at head +0' "$R/somvr.log" | tail -1 | cut -c1-200
  echo quit >> "$R/cmd"; echo "quit $(date +%T)"; exit 0
fi
# 3. reach: both hands ahead at the wall (the fingers' ray ahead), the left at the head's height, the right 35 cm up
REACH=${CLIMB_REACH_Z:-50}
w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
m k02-reach 0.2
# grab HAND Y [TRIES]: squeeze until a grab (z further ahead on a miss); 0 = held
grab() { local h=$1 y=$2 z=$REACH xs n0 i; [ "$h" = L ] && xs=-20 || xs=20
  for k in $(seq 1 ${3:-4}); do
    n0=$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log")
    w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done
    lastcl 1
    w "hands squeeze $h 0" 0.5; z=$((z + 12)); w "hands test $h $xs $y $z 0 0 0" 0.5
  done; return 1; }
# grabtop HAND: the hand over the wall's top, the palm down (OpenXR grip +X out of the left palm, into the right one:
# rz -90 / +90), 55 cm ahead, from 30 cm over the eyes lower each try; 0 = held
grabtop() { local h=$1 xs rz n0 i z=55 y; [ "$h" = L ] && { xs=-20; rz=-90; } || { xs=20; rz=90; }
  for y in 30 0 -30 -60; do
    w "hands squeeze $h 0" 0.4; w "hands test $h $xs $y $z 0 0 $rz" 0.5
    n0=$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(grep -cE "^CLIMB t=.* grab $h:" "$R/somvr.log")" -gt "$n0" ] && { TOPZ=$z; return 0; }; sleep 0.25; done
    lastcl 1
  done; return 1; }
grab L 0 || { echo "$(date +%T) no grab L"; lastcl 4; }
lastcl 3
# 4. hand over hand: the holding hand pulls down 45 cm, the other reaches up and grabs, the first lets go and reaches up;
# plan t stops at the top: a `top:` line (push: the climb goes on) or a hold with edge 1 (the wall's top within reach)
NTOP0=$(grep -cE '^CLIMB t=.* top: ' "$R/somvr.log")
attop() { [ "$(grep -cE '^CLIMB t=.* top: ' "$R/somvr.log")" -gt "$NTOP0" ] ||
  grep -E "^CLIMB t=.* grab $1:" "$R/somvr.log" | tail -1 | grep -q 'edge 1'; }
HOLD=L
for k in $(seq 1 $((PULLS * 2))); do
  climbing || { echo "$(date +%T) not climbing: $(lastcl 1)"; break; }
  [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
  w "hands swing $HOLD $HX -45 $REACH 0 0 0 $PULL_MS" "$(awk "BEGIN{print $PULL_MS/1000 + 0.4}")"
  [ $k = 1 ] && m k03-after-pull-1 0
  [ $k = 3 ] && m k04-after-pull-3 0
  climbing || { echo "$(date +%T) ended in the pull: $(lastcl 2)"; break; }
  [ "$PLAN" = t ] && attop "$HOLD" && { TOP=$HOLD; break; }
  w "hands test $O $OX 20 $REACH 0 0 0" 0.5
  if ! grab $O 20 $([ "$PLAN" = t ] && echo 2 || echo 4); then
    if [ "$PLAN" = t ] && grabtop $O; then TOP=$O; echo "$(date +%T) the top held by $O (z $TOPZ)"; else echo "$(date +%T) no grab $O"; break; fi
  fi
  w "hands squeeze $HOLD 0" 0.6
  w "hands test $HOLD $HX $([ -n "$TOP" ] && echo -40 || echo 20) $REACH 0 0 0" 0.5
  HOLD=$O
  lastcl 2
  [ "$PLAN" = t ] && attop $O && TOP=$O
  [ -n "$TOP" ] && break
done
m k05-high 0
probe up
if [ "$PLAN" = o ]; then
  # an orc while hanging: the practice infantry spawns below; its attack (FSM 7 on a hit) should end the climb (stand
  # down: "end: game"), gravity back, the fall
  w "practice spawn infantry" 0.5
  waitfor '^CLIMB t=.* end: ' 45 && echo "$(date +%T) the climb ended: $(grep -E '^CLIMB t=.* end: ' "$R/somvr.log" | tail -1 | cut -c1-200)"
  grep -E '^(THREAT start|COMBAT|DEFENCE|REACT)' "$R/somvr.log" | tail -4 | cut -c1-200
  m k05o-orc 0.5
  w "practice remove" 1
fi
if [ -n "$TOP" ]; then
  # the mantle (SOMVR_CLIMB_TOP=push: the climb goes on 1:1): the free hand down by the side; the top hand pushes down
  # to 90 cm under the eyes (the arm straight: the body up), then 90 cm toward the body (the body forward over the
  # edge): "end: over" once the head is 30 cm past the hold, gravity back, Talion on the top
  [ "$TOP" = L ] && { TX=-20; OT=R; OX=20; } || { TX=20; OT=L; OX=-20; }
  echo "$(date +%T) the top held by $TOP: $(grep -E "^CLIMB t=.* grab $TOP:" "$R/somvr.log" | tail -1 | grep -oE 'edge [01][^;]*')"
  w "hands squeeze $OT 0" 0.3; w "hands test $OT $OX -40 10 0 0 0" 0.5
  w "hands swing $TOP $TX -90 40 0 0 0 1800" 2.4
  lastcl 3; m k05b-pushed 0
  # forward: the top hand 80 cm toward the body (push's edge step lifts the part the edge blocks); not over yet: the
  # other hand onto the top again (the palm down 10 cm over the top's height: the down assist), the first lets go,
  # that one forward again
  TYH=-90
  for st in 1 2 3; do
    climbing || break
    w "hands swing $TOP $TX $TYH -40 0 0 0 2000" 2.6; lastcl 2
    climbing || break
    [ $st = 3 ] && break
    TY=$(topnow); HY=$(headnow); GOT=""
    [ "$OT" = L ] && OR=-90 || OR=90
    for dy in 10 -5 -20; do
      REL=$(awk "BEGIN{printf \"%d\", $TY - $HY + $dy}")
      w "hands squeeze $OT 0" 0.3; w "hands test $OT $OX $REL 40 0 0 $OR" 0.4
      n0=$(grep -cE "^CLIMB t=.* grab $OT:" "$R/somvr.log"); w "hands squeeze $OT 1" 1.0
      [ "$(grep -cE "^CLIMB t=.* grab $OT:" "$R/somvr.log")" -gt "$n0" ] && { GOT=$REL; break; }
    done
    echo "$(date +%T) stage $st: top $TY head $HY: the $OT hand ${GOT:+held at $GOT}${GOT:-found nothing}"
    [ -n "$GOT" ] || { lastcl 1; break; }
    w "hands squeeze $TOP 0" 0.5; w "hands test $TOP $TX -40 10 0 0 0" 0.4
    X=$TOP; TOP=$OT; OT=$X; X=$TX; TX=$OX; OX=$X; TYH=$((GOT - 10))
    m k05c-stage-$st 0
  done
  m k05d-over 0.8
  grep -E '^CLIMB t=.* end: ' "$R/somvr.log" | tail -1 | cut -c1-220
  probe up
fi
# 5. the fall: both hands open
w "hands squeeze L 0"; w "hands squeeze R 0" 1.5
lastcl 3
m k06-after-fall 1.5
# 6. walk again: back from the wall, then turn and walk
w "hands test off"
if grep -E '^CLIMB t=.* end: ' "$R/somvr.log" | tail -1 | grep -q 'end: over'; then w "stick 0 0.7 0 1.2" 1.8; else w "stick 0 -0.7 0 1.2" 1.8; fi
probe >/dev/null
w "stick 0.7 0 0 1.0" 1.5
probe >/dev/null
m k07-walked 0
grep -E '^CLIMB t=.*(start|end):' "$R/somvr.log" | cut -c1-300
echo "steps done $(date +%T)"
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
