#!/bin/bash
# climb13-drive.sh - build 13's fling (SOMVR_CLIMB_FLING=on, re-xr-pipeline.md 32.14), headless, with climb10-drive.sh's
# way to a wall (CLIMB_AT X Y Z first: practice back, practice go; the probe finds the nearest wall, snap turns face it,
# the stick walks to it). The release mid-pull: `hands swing H x y z 0 0 0 MS OPEN` (the test squeeze opens in the frame
# the swing passes u = OPEN; the 250 ms trigger words cannot place it; a 10th number CATCH: the other hand's squeeze
# closes at u = CATCH, a grip during the flight). At 20 Hz a frame's hand step must stay under the
# climb's jump_cm (25): the swings are 60-75 cm in 300 ms (at most ~23 cm a frame, ~330-410 cm/s at the release).
#   PLAN wall (CLIMB_AT the user's pillar 33350 2000 -27900: a tall face):
#     1. two plain pulls up the face; a slow let-go (the open word, the hand still): the hang as build 10, the root still
#     2. a fast pull + release (OPEN), the other hand squeezing at the swing's end (CATCH): the flight and a catch
#     3. a fast pull + release, nothing squeezed: the apex hang, 3 s still (no fall)
#     4. the hand pushed up and into the wall fast + release: no fling (fling none), the root still
#     5. A: the drop (the fall)
#   PLAN top (at the practice arrival: the meadow's stone banks): up the face hand over hand to an edge hold, then a fast
#     pull down and toward the body + release: the flight over the top (`end: over`), standing, the stick walks on it.
# Dumps (the layer's `dump`) mid-flight and marks (`m NAME`: RUN/marks/*/eyes.jpg) after each ending.
# Env: CLIMB_PLAN (wall|top|both, default both: top at the arrival first, then wall at CLIMB_AT), CLIMB_AT,
# CLIMB_WT, CLIMB_CATCH ("X Z", default "20 50": the catching hand; "0 65" reaches a narrow pillar's face from any
# approach). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CLIMB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${CLIMB_PLAN:-both}
AT=${CLIMB_AT-33350 2000 -27900}
CATCH=${CLIMB_CATCH:-20 50}   # the catching hand's x and z (cm right, ahead of the head) in step 2
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
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
probe() { local n0 i; n0=$(cnt '^CLIMB probe surfaces'); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(cnt '^CLIMB probe surfaces')" -gt "$n0" ] && break; sleep 0.25; done
  grep '^CLIMB probe' "$L" | tail -$([ -n "$1" ] && echo 2 || echo 5) | cut -c1-300; }
near() { grep "^CLIMB probe t=.*at head $1 cm" "$L" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$L" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbing() { grep -E '^CLIMB t=.* (start|end):' "$L" | tail -1 | grep -q ' start:'; }
phase() { grep -E '^CLIMB t=.*last 1 s: ' "$L" | tail -1 | sed -E 's/.*last 1 s: ([a-z]+).*/\1/'; }
lastcl() { grep -E '^CLIMB t=' "$L" | tail -${1:-3} | cut -c1-330; }
fl() { grep -E '^CLIMB t=.* (fling|end:|hang:|resume:|grab [LR])' "$L" | tail -${1:-4} | cut -c1-360; }
secs() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -${1:-3} | cut -c1-200; }
lo() { grep -E '^LOCO t=.*last 1 s' "$L" | tail -${1:-2} | sed -E 's/follow in our.*yaw ours/... yaw ours/; s/dt timer.*between/... between/' | cut -c1-260; }
dumpin() { sleep "$1"; printf '# %s\ndump\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: dump"; }
REACH=50
# the swing of hand $1 (x $2) from where it is to y $3 z $4 in $5 ms, the squeeze opening at u $6 (the other's closing
# at u $7); with DUMPS two dumps 0.3 and 0.5 s after the word (the flight: the take within 250 ms, the release 165 ms
# later, ~0.35 s up); waits for the take
swing() { local n0 i; n0=$(cnt "^BODY test swing $1: from"); w "hands swing $1 $2 $3 $4 0 0 0 $5 $6 $7" 0.01
  [ -n "$DUMPS" ] && { (dumpin "${D1:-0.3}"; dumpin "${D2:-0.2}") & }
  for i in $(seq 1 40); do [ "$(cnt "^BODY test swing $1: from")" -gt "$n0" ] && return 0; sleep 0.05; done; return 1; }
grab() { local h=$1 y=$2 z=$REACH xs n0 i; [ "$h" = L ] && xs=-20 || xs=20
  for k in $(seq 1 ${3:-4}); do
    n0=$(cnt "^CLIMB t=.* grab $h:")
    w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && return 0; sleep 0.25; done
    lastcl 1
    w "hands squeeze $h 0" 0.5; z=$((z + 12)); w "hands test $h $xs $y $z 0 0 0" 0.5
  done; return 1; }
# one hand-over-hand step: hold H pulls 45 cm in 1 s; the other hand onto the face 20 cm up and grabs; H opens
pull() { local H=$1 O HX OX; [ "$H" = L ] && { O=R; HX=-20; OX=20; } || { O=L; HX=20; OX=-20; }
  w "hands swing $H $HX -45 $REACH 0 0 0 1000" 1.4
  w "hands test $O $OX 20 $REACH 0 0 0" 0.5
  grab $O 20 2 || return 1
  w "hands squeeze $H 0" 0.6
  w "hands test $H $HX 20 $REACH 0 0 0" 0.5; }
toface() {
  w "climb watch" 1
  probe
  NB=$(near -50); [ -n "$NB" ] || NB=$(near +0)
  echo "$(date +%T) nearest climbable: ${NB:-none}"
  [ -n "$NB" ] || { m k01-no-wall 0; return 1; }
  turnto "${NB% *}"
  probe >/dev/null
  PREV=""
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
  w "climb watch off" 0.5
  probe up
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
}
# a fling of hand H: from its hold down to y $3, z $4 (x $8, default its side's), in $5 ms, open at $6 (the other hand
# closing at $7), dumps
fling() { local H=$1 HX; [ "$H" = L ] && HX=-20 || HX=20; [ -n "$8" ] && HX=$8
  DUMPS=1 swing $H $HX "$3" "$4" "$5" "$6" "$7" || echo "$(date +%T) the swing word not taken"; }

# 0. the controls card away, both test squeezes open
m k00-arrival 0.5
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"

if [ "$PLAN" = top ] || [ "$PLAN" = both ]; then
  echo "== 6. the top"
  toface || bail "no wall"
  m k60-at-bank 0.3
  grab L 0 || echo "$(date +%T) no grab L"
  H=L; TOP=""
  for k in 1 2 3 4 5 6; do
    climbing || break
    grep -E "^CLIMB t=.* grab $H:" "$L" | tail -1 | grep -q 'edge 1' && { TOP=$H; break; }
    pull $H || break
    [ "$H" = L ] && H=R || H=L
  done
  echo "$(date +%T) the top hand: ${TOP:-none} ($(grep -E "^CLIMB t=.* grab $H:" "$L" | tail -1 | grep -oE 'edge [01][^;]*'))"
  # the fling at the top: the edge hand (at y 20) pushed down to the chest slowly (the push, 1 s), then down and
  # toward the body fast from under the shoulder (the test swing's arc about the shoulder goes outward above it), 400
  # ms (under the 25 cm a frame at 20 Hz), released at u 0.55; the dumps after the release (a dump stalls a frame)
  [ "$H" = L ] && O=R || O=L
  w "hands squeeze $O 0" 0.3; w "hands test $O $([ $O = L ] && echo -20 || echo 20) -30 20 0 0 0" 0.5
  w "hands swing $H $([ $H = L ] && echo -20 || echo 20) -30 50 0 0 0 1000" 1.6
  fl 2
  # (back along the line from the hand to the head: a hold on the top has its `toward` from the head to the hold)
  D1=0.45 fling $H x -90 5 400 0.55 "" "$([ $H = L ] && echo -4 || echo 4)"
  sleep 2.5; fl 6
  climbing && { echo "$(date +%T) still on: $(fl 1)"; m k61-top-hang 0.3; }
  m k62-after-top-fling 0.3
  grep -E '^CLIMB t=.* end: over' "$L" | tail -1 | cut -c1-260
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 2; }
  w "hands test off"
  w "stick 0 1 0 2" 3; lo 3
  m k63-walked 0.3
fi
if [ "$PLAN" = wall ] || [ "$PLAN" = both ]; then
  if [ -n "$AT" ]; then
    w "practice back" 1
    waitfor '^PRACTICE t=.* arrived at' 30 || echo "$(date +%T) no arrival back"
    sleep 2
    w "practice go $AT" 1
    waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
    grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-200
    sleep 3
  fi
  toface || bail "no wall"
  m k01-at-wall 0.3
  grab L 0 || echo "$(date +%T) no grab L"
  pull L && pull R || echo "$(date +%T) the pulls: $(lastcl 2)"
  HOLD=L
  secs 2
  # 1. the slow let-go: the open word, the hand still: the hang (no fling line)
  echo "== 1. slow let-go"
  w "hands squeeze $HOLD 0" 0.3
  waitfor '^CLIMB t=.* hang: ' 3 && fl 2
  sleep 3; secs 2
  m k10-slow-hang 0.3
  # 2. a fast pull + release, the free hand squeezing right after the release: a catch on the face
  echo "== 2. fling + catch"
  w "hands test L -20 0 $REACH 0 0 0" 0.5
  grab L 0 || echo "$(date +%T) no grab L"
  w "hands squeeze R 0" 0.3; w "hands test R ${CATCH% *} 20 ${CATCH#* } 0 0 0" 0.5
  NF=$(cnt '^CLIMB t=.* fling launch')
  fling L 0 -60 "$REACH" 300 0.55 1
  sleep 2.5
  fl 6
  [ "$(cnt '^CLIMB t=.* fling launch')" -gt "$NF" ] || echo "$(date +%T) NO LAUNCH"
  m k20-after-catch 0.3
  # 3. a fast pull + release, nothing squeezed: the apex hang, 3 s still
  echo "== 3. fling, no catch: the apex"
  # a fresh hold with L (both open first: a slow let-go, the hang), R lowered out of the way
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.5; w "hands test R 20 -30 20 0 0 0" 0.3
  w "hands test L -20 0 $REACH 0 0 0" 0.5
  grab L 0 || echo "$(date +%T) no grab L"
  H=L
  fling L x -70 "$REACH" 300 0.55 # 70 cm down: ~21 cm a frame
  waitfor '^CLIMB t=.* fling apex hang' 3; fl 3
  sleep 3; secs 3
  m k30-apex-hang 0.3
  # 4. the hand pushed up and into the wall fast + release: no fling, still
  echo "== 4. up and into the wall: no fling"
  [ "$H" = L ] && HX=-20 || HX=20
  w "hands test $H $HX 0 $REACH 0 0 0" 0.5
  grab $H 0 || echo "$(date +%T) no grab $H"
  swing $H $HX 40 "$((REACH + 15))" 300 0.55
  sleep 2; fl 3; secs 2
  m k40-no-fling 0.3
  # 5. A: the drop
  echo "== 5. A"
  climbing && { x "btn R a 1"; x "btn R a 0"; }
  waitfor '^CLIMB t=.* end: drop' 3; fl 2
  sleep 2
  m k50-dropped 0.5
  w "hands squeeze L 0"; w "hands squeeze R 0"
fi

grep -E '^CLIMB t=.*(start|end|hang|resume|fling [a-z]+):' "$L" | cut -c1-200 | tail -40
echo "steps done $(date +%T)"
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
