#!/bin/bash
# strike18-drive.sh - build 18 (docs/re-xr-pipeline.md 63.8): after a gaze Shadow Strike LOCO owns Talion again
# (SOMVR_LOCO_SYNC_STALE=on) and the stick walks him 1:1, twice in a row; the wraith vision lit again when the game
# ends it while Y is held (SOMVR_VISION_RELIGHT=on). Run next to a headless boot of this worktree with build 18's env +
# SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_PRACTICE_AHEAD_CM=2000 (required: see strike17-drive.sh).
# Steps (SG_STEPS): pics (the landing mark from 20, 10, 5 m), walk (strike, the sync, then walk; SG_STRIKES times),
# relight (Y held, a raw tap = the game's off).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SG_STEPS:-pics walk relight}
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
AHEAD=$(grep -oE 'SOMVR_PRACTICE_AHEAD_CM=[0-9]+' "$R/launch-cmd.txt" | tail -1 | cut -d= -f2)
if [ "${AHEAD:-350}" -lt 1500 ] && [ -z "${SG_AHEAD_OK:-}" ]; then # SG_AHEAD_OK=1: a near spawn on purpose (pics5) # the orcs must spawn out of reach (see the header)
  echo "STOP: this boot spawns orcs ${AHEAD:-350} cm ahead: add SOMVR_PRACTICE_AHEAD_CM=2000 to its SOMVR_EXTRA"
  echo quit >> "$R/cmd"; exit 1
fi
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
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
# since N REGEX: lines matching after line N of the log
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
lines() { wc -l < "$L"; }
pass() { echo "$(date +%T) PASS $1" | tee -a "$R/sg-steps.txt"; }
fail() { echo "$(date +%T) FAIL $1" | tee -a "$R/sg-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/sg-steps.txt"; }
REST="hand L head -22 -40 15 0 0 0
hand R head 22 -40 15 0 0 0"
rest() { local IFS=$'\n'; t $REST; }
HY=0; HP=0
head_to() { HY=$1; HP=$2; t "head 0 0 0 yaw $HY pitch $HP s ${3:-0.3}"; sleep 0.3; }
# the nearest orc's bearing from the head's ray (the gaze's test word) -> OB_Y OB_P OB_D
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p')
  OB_D=$(echo "$l" | sed -nE 's/.* at ([0-9.]+) m: .*/\1/p'); [ -n "$OB_Y" ]; }
look() { orc0 || return 1
  head_to "$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}')" "$(awk -v a="$HP" -v b="$OB_P" 'BEGIN{v=a+b; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}')" "${1:-0.3}"
  echo "$(date +%T) look: the orc ${OB_D} m away was ${OB_Y} right ${OB_P} up -> head yaw $HY pitch $HP"; }
# marked N SECS: a mark line after log line N within SECS
marked() { local i; for i in $(seq 1 $((${2:-3} * 4))); do since "$1" '^STRIKE t=.* gaze: (marked|the mark moved)' >/dev/null && return 0
  userin && bail "the user is in VR"; sleep 0.25; done; return 1; }
# lookm: look at the orc until it is marked (the headless head's turns and the rig's can drift: measure again), 4 tries
# (from line $1 if given: an orc spawned inside the cone is marked as Y comes on - that mark counts, no look needed)
markednow() { since "$1" '^STRIKE t=.* gaze: (marked|the mark moved|unmarked)' | tail -1 | grep -qv 'unmarked'; }
lookm() { local i n0; n0=${1:-$(lines)}; markednow "$n0" && return 0
  for i in 1 2 3 4; do look 0.25 || sleep 0.5; marked "$n0" 1 && return 0; done; return 1; }
spawn() { w "practice spawn infantry" 0.3; waitfor '^PRACTICE t=.* spawn' 10 || echo "$(date +%T) no spawn line"; sleep 1.5; }
again() { t "btn L b 0"; w "practice remove" 2; head_to 0 0; sleep 0.5
  w "practice back" 0.5; waitfor '^PRACTICE t=.* the blink back ended' 15; sleep 1
  w "practice go" 0.5; waitfor '^PRACTICE t=.* the blink there ended' 15; sleep 1.5; }
yon() { t "btn L b 1"; waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 0.3; }
press_a() { T_S=0.3 t "btn R a 1"; T_S=0.3 t "btn R a 0"; }
# the controls card away, the hands at rest, arrows
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
grep -E '^(WARN|CONFIG strike)' "$L" | head -6 | cut -c1-260
w "bow arrows 40" 1
w "bow strike gaze status" 0.5

# walk_check NAME: the stick back 2 s (the orc lies ahead), then still; LOCO "ours" with the body's move ~ the asked (1:1)
walk_check() { local n0 l; n0=$(lines); t "stick L 0 -1"; sleep 2.2; t "stick L 0 0"; sleep 1.2
  l=$(since "$n0" '^LOCO t=.* last 1 s: ticks=[0-9]+ ours=[1-9]' | awk '{for(i=1;i<=NF;i++) if($i=="asked"){a=$(i+1)}; if (a+0 > 50) print}' | tail -1)
  if [ -n "$l" ] && echo "$l" | grep -qE 'game=0 '; then pass "$1: the stick walked him, LOCO ours: $(echo "$l" | grep -oE 'ours=[0-9]+ game=[0-9]+|asked [0-9.]+ cm, the body [0-9.]+ cm \([0-9]+ %\)' | tr '\n' ' ')"
  else fail "$1: no walk of ours: $(since "$n0" '^LOCO t=.* (last 1 s|frame)' | tail -2 | cut -c1-170 | tr '\n' '|')"; fi; }
k=0
if has walk; then
  for k in $(seq 1 "${SG_STRIKES:-2}"); do
    nm="walk$k"; w "practice remove" 2; head_to 0 0; spawn; nm0=$(lines); yon
    lookm "$nm0" || fail "$nm: no mark"
    n0=$(lines); press_a
    waitfor '^STRIKE t=.* (start|no move)|gaze: no Shadow Strike prompt' 4
    since "$n0" '^STRIKE t=.* start' >/dev/null && pass "$nm: struck ($(since "$n0" 'gaze: A at' | head -1 | grep -oE 'at orc [0-9]+ \([0-9.]+ m'))" || fail "$nm: no strike"
    t "btn L b 0"
    # the knockdown's sync: Talion's part ~3.7 s, the stale rule 1 s after it; LOCO ours again
    for i in $(seq 1 40); do since "$n0" '^LOCO t=.* frame=.*-> ours' >/dev/null && break; sleep 0.25; done
    if since "$n0" '^LOCO t=.* frame=.*-> ours' >/dev/null; then pass "$nm: LOCO ours again: $(since "$n0" 'sync record .* is stale|^LOCO t=.* frame=.*-> ours' | head -2 | cut -c1-150 | tr '\n' '|')"
    else fail "$nm: LOCO not ours: $(since "$n0" '^LOCO t=.* frame' | tail -1 | cut -c1-150)"; fi
    sleep 1; walk_check "$nm"
    [ "$k" -eq 1 ] && { w "practice back" 0.5; waitfor '^PRACTICE t=.* the blink back ended' 12 && pass "$nm: practice back ran" || fail "$nm: practice back"
      w "practice go" 0.5; waitfor '^PRACTICE t=.* the blink there ended' 15; sleep 1.5
      w "turn+" 0.6; w "turn+" 0.6; } # the next orc spawns 60 deg further round: the spot's first bearing, out of the hollow
  done
fi
# pics: the mark from 20, 10 and 5 m (Y held, the head on the orc, A never pressed): the orc runs in by itself after a
# while; a dump as it passes 10 and 5 m (if it stays out, Talion walks at it)
if has pics; then
  w "practice remove" 2; head_to 0 0; spawn; nm0=$(lines); yon; lookm "$nm0" || fail "pics: no mark"
  sleep 0.3; orc0; d "mark-${OB_D}m"
  for want in 10.5 5.5; do
    for i in $(seq 1 60); do orc0; awk -v d="$OB_D" -v m="$want" 'BEGIN{exit !(d <= m)}' && break
      [ "$i" -gt 40 ] && { t "stick L 0 1"; sleep 0.4; t "stick L 0 0"; }; done
    d "mark-${OB_D}m"
  done
  grep -E "the Shadow Strike's mark" "$R/xr-layer.log" | tail -3 | sed -E 's/^[0-9.]+ \[[0-9/]+\] //' | cut -c1-230 | tee -a "$R/sg-steps.txt"
  t "btn L b 0"; sleep 1
fi
# picswalk: the mark at 20 m, then Talion walks at the orc (the stick held) with a dump as it passes 10 and 5 m
if has picswalk; then
  w "practice remove" 2; head_to 0 0; spawn; nm0=$(lines); yon; lookm "$nm0" || fail "picswalk: no mark"
  d "mark-20m"
  t "stick L 0 1"
  for want in 10.5 5.5; do
    for i in $(seq 1 40); do T_S=0.05 look 0.1 >/dev/null; awk -v d="$OB_D" -v m="$want" 'BEGIN{exit !(d <= m)}' && break; done
    [ "$want" = 5.5 ] && t "stick L 0 0"
    T_S=0.1 head_to "$HY" "$(awk -v p="$HP" 'BEGIN{printf "%.1f", p-4}')" 0.1; d "mark-${OB_D}m"
  done
  t "stick L 0 0"; t "btn L b 0"; sleep 1
fi
# pics5 (with SOMVR_PRACTICE_AHEAD_CM=500 and SG_AHEAD_OK=1): the mark 5 m away, at once (the orc runs in within ~2 s)
if has pics5; then
  w "practice remove" 2; head_to 0 0; spawn; nm0=$(lines); yon; lookm "$nm0" || fail "pics5: no mark"
  T_S=0.1 head_to "$HY" "$(awk -v p="$HP" 'BEGIN{printf "%.1f", p-8}')" 0.1; d "mark-5m"; orc0; echo "pics5: the orc ${OB_D} m away after the dump"
  t "btn L b 0"; sleep 1
fi
if has relight; then # Y held; a raw tap of 122 (no wish of ours: the game's own off, as an orc's hit); on again ~1.05 s
  w "practice remove" 2; head_to 0 0; yon; sleep 1.5
  n0=$(lines); w "vision taps 1 100" 0.3
  waitfor '^VISION t=.* the flag 1 -> 0' 3
  for i in $(seq 1 16); do since "$n0" '^VISION t=.* the flag 0 -> 1: our tap' >/dev/null && break; sleep 0.25; done
  if since "$n0" 'on wished again' >/dev/null && since "$n0" '^VISION t=.* the flag 0 -> 1: our tap' >/dev/null; then
    pass "relight: $(since "$n0" '^VISION t=.* the flag' | head -2 | cut -c1-110 | tr '\n' '|')"
  else fail "relight: $(since "$n0" '^VISION' | head -4 | cut -c1-140 | tr '\n' '|')"; fi
  # the same with Y let go right after the game's off: stays off
  sleep 1.5; n1=$(lines); w "vision taps 1 100" 0.3; waitfor '^VISION t=.* the flag 1 -> 0' 3; t "btn L b 0"; sleep 2.5
  if ! since "$n1" '^VISION t=.* the flag 0 -> 1' >/dev/null; then pass "relight: Y let go after the off: stays off"
  else fail "relight: lit again after Y let go"; fi
  # a real hit: an orc 3.5 m away is not possible with this boot's spawn; the strike's own aim release is the other case
fi
w "bow strike gaze status" 0.5
grep -cE '^WARN' "$L" | sed 's/^/WARN lines: /'
echo "$(date +%T) done"
echo quit >> "$R/cmd"
