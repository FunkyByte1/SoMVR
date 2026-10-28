#!/bin/bash
# bugs20-drive.sh - build 20's bugs round (handoff/HANDOFF-build20-bugs.md, docs/re-xr-pipeline.md 75): drives one
# headless boot (scripts/headless_vr.sh with scripts/headset-build20.env, SOMVR_HANDS_TEST=1, the layer's test file):
# the game's own sword sounds on Talion (SOMVR_SLOTS_GAMESOUND) and the collectible tutorial (SOMVR_HINTS_KEEP unset).
# BG_STEPS, in order (default "sheathe:log sheathe:drop draw:log draw:drop hint"):
#   sheathe:MODE  `slsnd MODE`; the sword drawn at the shoulder slot by the test hand (the slot's squeeze, the hand out
#                 in front, the grip let go: it stays in the hand), held out of a fight until the game sheathes it by
#                 itself ("the game put it away: kept in the hand", ~1.5 s), then put back at the slot by the hand
#   draw:MODE     `slsnd MODE`; the sword kept on the back, the far orc (18 m ahead, unaware), the test stick at it
#                 (`stealth chase 1.0 150`) until the game draws the sword by itself ("the game drew it: kept on the
#                 back") or 25 s, then the orc removed
#   hint          the trip to build 19b's session's artifact place (34449 3636 -27242, its hint at 940341.9) and 16 s
#                 there (the tutorial comes 3-8 s after arriving, for 12.5 s), a mark for the flat picture
# MODE log = build 19b's sounds (the game's posts logged, passed), drop = build 20's. BG_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${BG_STEPS:-sheathe:log sheathe:drop draw:log draw:drop hint}
T0=$(date +%s)
R=${BG_RUN:-}
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
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
# the lines since line L0 matching RE (cut)
since() { tail -n +"$1" "$R/somvr.log" | grep -E "$2" | cut -c1-"${3:-230}" | tail -n "${4:-12}"; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
put_back() { w "$SLOT" 0.8; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 0.6; }
held() { grep -E '^SLOTS t=.* sword: ' "$R/somvr.log" | tail -1 | grep -q 'now in the hand'; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
w "slsnd state" 0.5
for s in $STEPS; do
  case $s in
  sheathe:*)
    md=${s#sheathe:}; echo "== $s"; L0=$(($(wc -l < "$R/somvr.log") + 1)); w "slsnd $md" 0.5
    held && put_back
    draw_slot
    if waitfor '^SLOTS t=.* sword: the game put it away' 8; then echo "$(date +%T) the game sheathed it (kept in the hand)"
    else echo "$(date +%T) no game sheathe within 8 s"; fi
    sleep 1.5; m "sheathe-$md" 0.1
    put_back; sleep 1.5
    echo "-- $s:"; since "$L0" '^SLOTS t=.* sword: |^SLOTS t=.* SHEATH|^SLSND' 230 14 ;;
  draw:*)
    md=${s#draw:}; echo "== $s"; L0=$(($(wc -l < "$R/somvr.log") + 1)); w "slsnd $md" 0.5
    held && put_back
    w "practice far infantry" 3
    if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED\|refused'; then
      echo "$(date +%T) the far spawn failed: back to the practice area's start"; w "practice back" 6; w "practice go" 7
      w "practice far infantry" 3; fi
    w "stealth chase 1.0 150" 0.3
    if waitfor '^SLOTS t=.* sword: the game drew it' 25; then echo "$(date +%T) the game drew it (kept on the back)"
    else echo "$(date +%T) no game draw within 25 s"; fi
    sleep 1.5; m "draw-$md" 0.1
    w "stealth chase 0" 0.3; w "practice remove" 2; sleep 2
    echo "-- $s:"; since "$L0" '^SLOTS t=.* sword: |^SLOTS t=.* UNSHEATH|^SLSND|^PRACTICE t=.*(spawn|remove)' 230 14 ;;
  hint)
    echo "== hint"; L0=$(($(wc -l < "$R/somvr.log") + 1)); a0=$(cnt 'arrived at'); f0=$(cnt 'did not arrive')
    w "practice go 34449 3800 -27242" 1
    for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
    grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170
    waitfor "^HINTS combat hint .*show 'Tut_Collectible" 12 && echo "$(date +%T) the tutorial came"
    sleep 2; m hint-place 0.1; sleep 4
    echo "-- hint:"; since "$L0" '^HINTS combat hint|^TEXTBUTTONS' 200 8 ;;
  esac
done
w "slsnd state" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
