#!/bin/bash
# ledge-drive.sh - the climb helper's words for build 9's wall-top bug (marks 170126_20 / 170147_21 of
# logs/boot-check-20261024-165018: the narrow ledge round a pillar at y ~2666, "B Drop To Hang", the game moved and turned
# Talion). One headless boot (SOMVR_CLIMB=b, SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt; climb-drive.sh's
# prologue): `climb watch` (a CLIMB state line on every change of the state tags and each second), then the practice
# trip to the ledge (practice back, practice go X Y Z), standing, the rig turned (LOCO's standing turn), the test stick
# along and off the ledge (SOMVR_CLIMB_LEDGES game, then off by `climb ledges off`); marks. LEDGE_AT="X Y Z" (default:
# a point of the rail the user walked, root 33656.5 2665.6 -28081.7, 10 cm over it; then the rail's corner). Stops at
# once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CLIMB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
AT=${LEDGE_AT:-33656 2675 -28082}
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
st() { grep -E '^CLIMB state' "$R/somvr.log" | tail -${1:-1} | cut -c1-520; }
lo() { grep -E '^LOCO t=.*last 1 s' "$R/somvr.log" | tail -${1:-2} | sed -E 's/follow in our.*yaw ours/... yaw ours/' | cut -c1-330; }
lastcl() { grep -E '^CLIMB t=' "$R/somvr.log" | tail -${1:-3} | cut -c1-330; }

# 0. the controls card away, both test squeezes open, the watch on
m k00-arrival 0.5
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
w "climb watch" 1.5; st 2
# 1. to the ledge: back, then go X Y Z (Teleport puts him there; he lands on the ledge under it, or falls past it)
onledge() { st 1 | grep -oE 'root [0-9.-]+,[0-9.-]+' | awk -F, '{exit !($2 > 2500)}'; }
for at in "$AT" "34275 2675 -28439"; do
  w "practice back" 1
  waitfor '^PRACTICE t=.* arrived at' 30 || echo "$(date +%T) no arrival back"
  sleep 2
  w "practice go $at" 1
  waitfor '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-220
  sleep 4; st 2; lo 2
  onledge && break
  echo "$(date +%T) not on the ledge at $at"
done
onledge || bail "never on the ledge"
m k01-ledge 0.3
# 2. the game's ledge (SOMVR_CLIMB_LEDGES=game): standing 6 s, the rig turned 120 deg (LOCO's standing turn: the yaw
# guard), the stick right and left (along the rail?), standing 4 s after each
sleep 6; st 1; lo 2
for k in 1 2 3 4; do w "turn+" 0.7; done
sleep 4; st 2; lo 3
grep -E '^LOCO t=.*yaw guard' "$R/somvr.log" | tail -3 | cut -c1-260
m k02-turned 0.3
for s in "1 0" "-1 0"; do
  w "stick $s 0 2" 3.2; st 1; lo 3
  sleep 4; st 1; lo 4
  m "k03-stick-${s// /_}" 0.2
done
# 3. no context action for Talion (SOMVR_CLIMB_LEDGES=off now): does the game let go of him? standing, the turn, the
# stick along, then ahead and back (off the ledge: the fall is the player's own walk)
w "climb ledges off" 1
sleep 4; st 3; lo 3
m k04-ledges-off 0.3
for k in 1 2 3 4; do w "turn-" 0.7; done
sleep 4; st 2; lo 3
for s in "1 0" "-1 0" "0 1" "0 -1"; do
  w "stick $s 0 2" 3.2; st 1; lo 3
  sleep 3; st 1; lo 3
  m "k05-off-stick-${s// /_}" 0.2
done
w "climb ledges game" 0.5
w "climb watch off" 1
grep -E '^CLIMB t=.*ledges|^LOCO t=.*yaw guard' "$R/somvr.log" | cut -c1-220 | tail -20
echo quit >> "$R/cmd"; echo "quit $(date +%T)"
