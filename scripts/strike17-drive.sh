#!/bin/bash
# strike17-drive.sh - build 17: the Shadow Strike by gaze (SOMVR_STRIKE_GAZE=on; docs/re-xr-pipeline.md 63,
# handoff/HANDOFF-wraith.md "build 17"). Run next to a headless boot of this worktree with build 17's env +
# SOMVR_STRIKE_GAZE=on SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt and SOMVR_PRACTICE_AHEAD_CM=2000 (required:
# at the practice's default 350 cm the orc runs in and hits Talion within ~2 s, the game ends the wraith vision and the
# mark with it - run 20261027-162355) + SOMVR_XR_DUMP_MARKED=4 (the strike's pictures).
# Steps (SG_STEPS; run in this order): probe (the aim held with its ray on the orc: the game's prompt against the
# distance), away (marked, look away: unmarked, A = nothing), yoff (marked, let go of Y: unmarked, A = nothing),
# noarrows (0 arrows: the grey mark, A refused), strike:move|sync (Y, look, the mark, A: the strike lands; the aim held
# until the move | the sync's end; then the game free again: practice back), bowx (the bow's own X; not a default:
# the test bow is held ahead, not aimed at the orc, so the game shows no prompt - runs 111356, 162355).
# Pictures: `d NAME` dumps both eyes (compose: python3 scripts/xr_compose.py RUN -f FRAME); names in RUN/sg-steps.txt.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SG_STEPS:-away yoff noarrows strike:move}
STRIKE_M=${SG_STRIKE_M:-99}
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
if [ "${AHEAD:-350}" -lt 1500 ]; then # the orcs must spawn out of reach (see the header)
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

if has probe; then # the game's prompt with the aim held and its ray on the orc, the orc walking in
  spawn
  w "bow strike gaze probe 14" 15
  grep -E '^STRIKE t=.* gaze probe' "$L" | tail -12 | cut -c1-200 | tee -a "$R/sg-steps.txt"
  again
fi

strike() { # $1 = move|sync (the aim held until the strike's move begins | its sync is over)
  local nm="strike-$1"
  w "bow strike gaze hold $1" 0.5; w "bow strike marks ${SG_MARKS:-25}" 0.5
  w "practice remove" 2; head_to 0 0; spawn; nm0=$(lines); yon
  # the headless head drifts ~3 deg/s in yaw (re-bow.md 14.4): A right after the mark, the picture is the press's dump
  lookm "$nm0"
  if marked "$nm0" 1; then pass "$nm: marked ($(grep -E 'gaze: (marked|the mark moved)' "$L" | tail -1 | cut -c1-160))"
  else fail "$nm: no mark"; look 0.2; fi
  n0=$(lines); press_a
  waitfor '^STRIKE t=.* (start|no move)|gaze: no Shadow Strike prompt' 4
  sleep 7
  if since "$n0" 'gaze: A at orc .* committed' >/dev/null && since "$n0" 'press [0-9]+: A by gaze' >/dev/null &&
     since "$n0" '^STRIKE t=.* start' >/dev/null; then pass "$nm: A committed, 121 after the prompt, the dash started"
  else fail "$nm: $(since "$n0" '^STRIKE t=.* (gaze|press|start|no move)' | tail -3 | cut -c1-200 | tr '\n' '|')"; fi
  since "$n0" '^STRIKE t=.* (gaze|press|hold|start|end|no move|the orcs)' | cut -c1-260 | tee -a "$R/sg-steps.txt"
  grep -E 'xr: dump mark' "$R/xr-layer.log" | tail -4 | sed -E 's/^[0-9.]+ \[[0-9/]+\] //' | cut -c1-160 | tee -a "$R/sg-steps.txt"
  t "btn L b 0"; sleep 1; w "bow strike gaze status" 0.8
  grep -E "^STRIKE gaze status: Talion's sync" "$L" | tail -1 | tee -a "$R/sg-steps.txt"
  n1=$(lines); w "practice remove" 2; w "practice back" 0.5
  if waitfor '^PRACTICE t=.* the blink back ended' 12; then pass "$nm: the game free after the strike (practice back ran)"
  else fail "$nm: practice back did not run: $(since "$n1" '^PRACTICE' | tail -2 | cut -c1-160 | tr '\n' '|')"; fi
  w "practice go" 0.5; waitfor '^PRACTICE t=.* the blink there ended' 15; sleep 1.5; head_to 0 0
}
for st in $STEPS; do case $st in strike:*) strike "${st#strike:}" ;; esac; done

fresh() { w "practice remove" 1.5; head_to 0 0; spawn; }
if has away; then # marked, then the head turns away before A: unmarked, A does nothing
  fresh; nm0=$(lines); yon; lookm "$nm0"
  marked "$nm0" 1 && pass "away: marked" || fail "away: no mark"
  n0=$(lines); head_to "$(awk -v a="$HY" 'BEGIN{printf "%.1f", a+45}')" "$HP" 0.3; sleep 0.5
  since "$n0" '^STRIKE t=.* gaze: unmarked .*looked away' >/dev/null && pass "away: unmarked (looked away): $(since "$n0" 'gaze: unmarked' | head -1 | cut -c1-150)" || fail "away: still marked"
  d "away-unmarked"
  n0=$(lines); press_a; sleep 2
  if since "$n0" 'gaze: A without a mark: nothing' >/dev/null && ! since "$n0" 'press [0-9]+: A by gaze|^STRIKE t=.* start' >/dev/null
  then pass "away: A after looking away = nothing"; else fail "away: $(since "$n0" '^STRIKE' | head -3 | cut -c1-160 | tr '\n' '|')"; fi
  t "btn L b 0"; sleep 1.2
fi

if has yoff; then # marked, then Y let go before A: unmarked at once, A does nothing
  fresh; nm0=$(lines); yon; lookm "$nm0"
  marked "$nm0" 1 && pass "yoff: marked" || fail "yoff: no mark"
  d "yoff-mark"
  n0=$(lines); t "btn L b 0"; sleep 0.3
  since "$n0" '^STRIKE t=.* gaze: unmarked .*off \(Y' >/dev/null && pass "yoff: unmarked (Y let go): $(since "$n0" 'gaze: unmarked' | head -1 | cut -c1-150)" || fail "yoff: still marked"
  n0=$(lines); press_a; sleep 2
  if since "$n0" 'gaze: A without a mark: nothing' >/dev/null && ! since "$n0" 'press [0-9]+: A by gaze|^STRIKE t=.* start' >/dev/null
  then pass "yoff: A after letting go of Y = nothing"; else fail "yoff: $(since "$n0" '^STRIKE' | head -3 | cut -c1-160 | tr '\n' '|')"; fi
fi

if has noarrows; then # 0 arrows: the mark grey ("no arrows"), A refused with the buzz, nothing moves
  w "bow arrows -99" 1
  grep -E '^BOW t=.* arrows: ' "$L" | tail -1 | cut -c1-160
  fresh; nm0=$(lines); yon; lookm "$nm0"
  marked "$nm0" 1 && pass "noarrows: marked" || fail "noarrows: no mark"
  sleep 0.4; d "noarrows-mark"
  n0=$(lines); press_a; sleep 2.5
  if since "$n0" 'gaze: A at orc .*refused: under 2 arrows' >/dev/null && ! since "$n0" 'press [0-9]+: A by gaze|^STRIKE t=.* start' >/dev/null
  then pass "noarrows: A refused, nothing moved"; else fail "noarrows: $(since "$n0" '^STRIKE' | head -3 | cut -c1-160 | tr '\n' '|')"; fi
  since "$n0" '^HAP R .*src=combat' | head -2 | cut -c1-160
  t "btn L b 0"; w "bow arrows 40" 1
fi


if has bowx; then # the bow's own path (X with the bow aimed at the orc), then the sync record and the trip back
  fresh; t "btn L b 0"
  t "hand L head -12 -10 -15 90 0 0"; sleep 0.4; T_S=0.4 t "btn L squeeze 1"; t "hand L head -8 -15 55 90 0 0"; sleep 1.2
  look 0.3; sleep 0.6
  for k in $(seq 1 15); do w "bow where" 0.35; grep -E '^BOW where' "$L" | tail -1 | grep -q 'prompt up$' && break; done
  grep -E '^BOW where' "$L" | tail -1 | cut -c1-160
  n0=$(lines); T_S=0.3 t "btn L a 1"; T_S=0.3 t "btn L a 0"; sleep 7
  if since "$n0" 'X with the bow out .*prompt up' >/dev/null && since "$n0" '^STRIKE t=.* start' >/dev/null; then pass "bowx: X with the bow out struck"
  else fail "bowx: $(since "$n0" '^STRIKE t=.* (press|start|no move)' | head -2 | cut -c1-160 | tr '\n' '|')"; fi
  t "hand L head -12 -10 -15 90 0 0"; sleep 0.5; t "btn L squeeze 0"; sleep 1; rest
  w "bow strike gaze status" 0.8; grep -E "^STRIKE gaze status: Talion's sync" "$L" | tail -1 | tee -a "$R/sg-steps.txt"
  n1=$(lines); w "practice remove" 2; w "practice back" 0.5
  if waitfor '^PRACTICE t=.* the blink back ended' 12; then pass "bowx: the game free after the strike (practice back ran)"
  else fail "bowx: practice back did not run: $(since "$n1" '^PRACTICE' | tail -1 | cut -c1-160)"; fi
  w "practice go" 0.5; waitfor '^PRACTICE t=.* the blink there ended' 15; sleep 1.5; head_to 0 0
fi


w "bow strike gaze status" 0.5
grep -E '^STRIKE gaze status' "$L" | tail -1 | cut -c1-300
grep -cE '^WARN' "$L" | sed 's/^/WARN lines: /'
echo "$(date +%T) done"
echo quit >> "$R/cmd"
