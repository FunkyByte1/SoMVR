#!/bin/bash
# cues37-drive.sh - build 37 (som-b37-cues, docs/re-xr-pipeline.md 169): the cues for what the hidden prompts said, in
# a headless Windows boot (the env block + SOMVR_HANDS_TEST=1). Run beside
#   HEADLESS_HOLD=1500 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ') SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" scripts/headless_vr.sh --win <wt>/ShadowOfMordor/mod/somvr
# CU_STEPS (default "aware use timer"):
#   aware  the practice meadow: an infantry orc put ahead facing Talion (SOMVR_PRACTICE_FAR_CM sets how far) (`practice far` finds the spot, `practice put`
#          turns it to him), stood still, then walked at; the game's own triangles on (`gprompts on`) the first time, off
#          the second; `cues probe` lines (its alert state, 4 a second); dumps every 2.5 s (white, amber, red marks)
#   use    the save's artifact (Collectible_Artifact 34776 2448 -27463) and an unsynced forge tower's top (its stone, a
#          RessurrectionStone marker, 15862 6232 -3175): the glow
#          (the `CUES use ... cue` line's place), the left hand put there open, dumps, the grip -> the game's pick
#   timer  the golden M (Queen of the Shore): B, the card's A, the video skipped (A, then B), the timed first objective
#          (1:30): dumps at its start (the popup), the watch at arm's length (45 cm), 30 s and 10 s left, the end
# CU_RUN=RUN drives a given run. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${CU_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${CU_STEPS:-aware use timer}
T0=$(date +%Y%m%d-%H%M%S) # (the run's folder name, not its mtime: boot 211723's driver took an older run touched later)
R=${CU_RUN:-}
for i in $(seq 1 ${CU_WAIT_N:-900}); do   # (5 s each: the slot may take a while)
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    n=${d##*boot-check-}
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [[ ! "$n" < "$T0" ]] && R=$d && break
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
for i in $(seq 1 200); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$R/somvr.log}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/cu-steps.txt"; }
mk() { w "mark $1" 2; }
dq() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.6
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f $(grep -E '^CUES (probe|aware) ' "$R/somvr.log" | tail -1 | grep -oE '(-> [a-z]+ \(shown [a-z]+ [0-9.]+\)|: [a-z]+ -> [a-z]+)')" | tee -a "$R/cu-steps.txt"; }
tap() { userin && bail "the user is in VR"; x "btn $1 $2 1" "${3:-0.3}"; x "btn $1 $2 0" 0.5; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
trip() { local a0=$(cnt 'arrived at') f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170; sleep 3; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6
x "head 0 0 0 yaw 0 pitch -3" 0.6
if has aware; then
  echo "== aware: an orc 18 m ahead, facing Talion"
  for pass in ${CU_AWARE_PASSES:-off}; do
    w "practice far infantry" 3
    sp=$(grep -E '^PRACTICE t=.* spawn.* at \(' "$R/somvr.log" | tail -1 | grep -o 'at ([-0-9. ]*)' | tr -d 'at()')
    echo "the spot: $sp"
    [ -n "$sp" ] || { echo "no spot"; break; }
    w "gprompts $pass" 0.5
    w "practice put $sp infantry" 2.5
    grep -E '^PRACTICE t=.* spawn' "$R/somvr.log" | tail -1 | cut -c1-200
    w "cues probe 60" 0.3
    dp "aware-$pass-0"
    for k in $(seq 1 10); do dq "aware-$pass-still$k"; done
    for k in $(seq 1 6); do sleep 1.3; dp "aware-$pass-still-b$k"; done
    echo "-- the walk at it"
    for k in $(seq 1 6); do w "stick 0 0.5 0 1.5" 0.2; sleep 1.0; dp "aware-$pass-walk$k"; done
    for k in $(seq 1 6); do sleep 1.3; dp "aware-$pass-near$k"; done
    mk "aware-$pass"
    grep -E '^CUES aware' "$R/somvr.log" | tail -8 | cut -c1-260
    w "practice remove" 2
  done
  w "gprompts off" 0.5
  echo "-- the probe's states seen:"; grep -E '^CUES probe' "$R/somvr.log" | grep -o 'state -\?[0-9]* .* -> [a-z]*' | sed -E 's/\+0xeb8 [-0-9.]+ //; s/action .([^ ]*). /\1 /' | sort | uniq -c | sort -rn | head -20
fi
if has rim; then
  echo "== rim: high alert forced (test word), the watch raised"
  WATCHR="hand L head 0 -12 42 0 -90 -130"
  w "cues rim 10" 0.5; x "$WATCHR" 2; dp rim-on; mk rim; sleep 9; dp rim-off; x "hand L off" 1
  grep -E 'xr: watch rim' "$R/xr-layer.log" | tail -3 | cut -c1-200
fi
usetry() { # $1 name: the cue's place -> the left hand there, open; then the grip
  local L0 cue p
  L0=$(wc -l < "$R/somvr.log")
  for i in $(seq 1 12); do cue=$(tail -n +"$L0" "$R/somvr.log" | grep -E '^CUES use t=.* cue:' | tail -1); [ -n "$cue" ] && break; sleep 1; done
  if [ -z "$cue" ]; then echo "$1: no use cue (prompts: $(tail -n +"$L0" "$R/somvr.log" | grep -cE '^USE t=.* pick prompt up'))"
    w "stick 0 0.4 0 0.6" 1.5; for i in $(seq 1 8); do cue=$(grep -E '^CUES use t=.* cue:' "$R/somvr.log" | tail -1); [ -n "$cue" ] && break; sleep 1; done; fi
  echo "$cue" | cut -c1-260
  p=$(echo "$cue" | grep -oE '[-+][0-9.]+ right [-+][0-9.]+ up [-+][0-9.]+ ahead' | sed -E 's/ (right|up|ahead)//g')
  [ -n "$p" ] || { echo "$1: no place"; return 1; }
  if [ "${CU_LOOK:-0}" = 1 ]; then # turn the head to it (a player looks at what he reaches for): the yaw's sign checked
    local nm="$1" yw pt c2 r2 a2 sg
    set -- $p; yw=$(awk -v r="$1" -v a="$3" 'BEGIN{printf "%.0f", atan2(r, a) * 57.2958}')
    for sg in 1 -1; do
      x "head 0 0 0 yaw $((sg * yw)) pitch 0" 1.5; L0=$(wc -l < "$R/somvr.log"); sleep 1.5
      c2=$(tail -n +"$L0" "$R/somvr.log" | grep -E '^CUES use t=.* cue:' | tail -1)
      p=$(echo "$c2" | grep -oE '[-+][0-9.]+ right [-+][0-9.]+ up [-+][0-9.]+ ahead' | sed -E 's/ (right|up|ahead)//g')
      [ -n "$p" ] || continue
      set -- $p; r2=${1#+}; a2=${3#+}
      awk -v r="$r2" -v a="$a2" 'BEGIN{exit !(a > 0 && (r < 0 ? -r : r) < 40)}' && break
    done
    set -- $p; pt=$(awk -v u="$2" -v a="$3" 'BEGIN{v=atan2(u, a) * 57.2958 + 15; if (v < -55) v = -55; printf "%.0f", v}')
    x "head 0 0 0 yaw $((sg * yw)) pitch $pt" 1.5; L0=$(wc -l < "$R/somvr.log"); sleep 1.5
    c2=$(tail -n +"$L0" "$R/somvr.log" | grep -E '^CUES use t=.* cue:' | tail -1); echo "looked: yaw $((sg * yw)) pitch $pt: $c2" | cut -c1-240
    p=$(echo "$c2" | grep -oE '[-+][0-9.]+ right [-+][0-9.]+ up [-+][0-9.]+ ahead' | sed -E 's/ (right|up|ahead)//g')
    set -- "$nm" $p
  else
    set -- "$1" $p
  fi
  w "hands test L $2 $3 $4 0 0 0" 1.2; dp "$1-reach"
  grep -E '^CUES use t=.* cue:' "$R/somvr.log" | tail -1 | cut -c1-200
  w "hands squeeze L 1" 1.0; dp "$1-grip"
  grep -E '^CUES use t=.* taken|^USE t=' "$R/somvr.log" | tail -3 | cut -c1-200
  sleep 2.5; dp "$1-after"; w "hands squeeze L 0" 0.6; w "$LREST" 0.6
}
inworld() { state | grep -q 'ctx=world'; }
if has use; then
  echo "== use: the artifact"
  trip 34776 2498 -27463; x "btn L b 1" 3.5; dp use-artifact-at   # the wraith vision held: the artifact's prompt (HANDOFF-collect)
  usetry use-artifact; x "btn L b 0" 1
  for k in 1 2 3; do sleep 2; dp "use-artifact-$k"; done
  echo "-- out of the examine screen (boot 210104: A, then B until the world)"; tap R a 0.4; sleep 3
  for k in 1 2 3 4; do inworld && break; tap R b 0.5; sleep 2.5; done; state
fi
if has use || has tower; then
  echo "== use: the forge tower"
  trip 15790 6240 -3110; dp use-tower-at; usetry use-tower   # an unsynced tower's top (boot 20261030-110011: its stone 15862 6232 -3175)
  for k in 1 2 3; do sleep 2; dp "use-tower-$k"; done
  grep -E "^CUES use|Activate_(SpiritStone|ArtifactCollectable)" "$R/somvr.log" | tail -8 | cut -c1-220
  echo "-- the map after the sync: B until the world"; for k in 1 2 3 4 5; do inworld && break; tap R b 0.5; sleep 3; done; state
fi
if has herb; then
  echo "== herb: Practice > New herb, the hand coming in (the glow brightens, the ring tightens), the grip, the mouth, the pick"
  w "practice herb" 1
  for i in $(seq 1 30); do grep -qE '^PRACTICE t=.* (new herb .*refused|arrived at)' <(tail -n 40 "$R/somvr.log") && break; sleep 1; done
  grep -E '^PRACTICE t=.* new herb' "$R/somvr.log" | tail -1 | cut -c1-220; sleep 3
  hcue() { grep -E '^HERB t=.* cue:' "$R/somvr.log" | tail -1; }
  hpos() { hcue | grep -oE '[-+][0-9.]+ right [-+][0-9.]+ up [-+][0-9.]+ ahead' | sed -E 's/ (right|up|ahead)//g'; }
  for i in $(seq 1 10); do [ -n "$(hpos)" ] && break; w "stick 0 0.3 0 0.4" 1.2; done
  hcue | cut -c1-240
  p=$(hpos)
  if [ -n "$p" ]; then
    set -- $p; yw=$(awk -v r="$1" -v a="$3" 'BEGIN{printf "%.0f", atan2(r, a) * 57.2958}')
    for sg in 1 -1; do x "head 0 0 0 yaw $((sg * yw)) pitch 0" 1.5; L0=$(wc -l < "$R/somvr.log"); sleep 1.6
      set -- $(hpos); awk -v r="${1#+}" -v a="${3#+}" 'BEGIN{exit !(a > 0 && (r < 0 ? -r : r) < 40)}' && break; done
    set -- $(hpos); pt=$(awk -v u="$2" -v a="$3" 'BEGIN{v=atan2(u, a) * 57.2958 + 10; if (v < -60) v = -60; printf "%.0f", v}')
    x "head 0 0 0 yaw $((sg * yw)) pitch $pt" 1.5; sleep 1.6; hcue | cut -c1-240
    set -- $(hpos); HR=$1; HU=$2; HA=$3
    for k in far mid near; do
      dy=$([ $k = far ] && echo 70 || ([ $k = mid ] && echo 35 || echo 6))
      w "hands test L $HR $(awk -v u="$HU" -v d="$dy" 'BEGIN{printf "%.1f", u + d}') $HA 0 0 0" 1.4; dp "herb-$k"
      hcue | grep -oE "the (left|right) hand [0-9]+ cm from it.*" | cut -c1-60
    done
    w "hands squeeze L 1" 0.8; dp herb-grip
    grep -E '^HERB t=.* taken' "$R/somvr.log" | tail -1 | cut -c1-200
    w "hands test L 0 -9 6 0 0 0" 0.4; sleep 0.6; dp herb-mouth
    for i in $(seq 1 12); do grep -qE '^CUES pick t=' "$R/somvr.log" && break; sleep 0.25; done
    sleep 0.15; dq herb-heal1; dq herb-heal2; sleep 1; dp herb-after
    w "hands squeeze L 0" 0.5; w "$LREST" 0.5
    grep -E '^HERB t=.* (eaten|after)|^CUES pick' "$R/somvr.log" | tail -4 | cut -c1-220
    x "head 0 0 0 yaw 0 pitch -3" 0.6
  else
    echo "herb: no cue"
  fi
  echo "-- the herb icons the game placed (SOMVR_GAME_PROMPTS_ALLOW herbs):"; grep -E "^GPROMPTS t=.* places the world icon 'Collectible_Flower" "$R/somvr.log" | head -3 | cut -c1-160
fi
if has timer; then
  echo "== timer: Queen of the Shore's 1:30"
  WATCH="hand L head 0 -12 42 0 -90 -130"   # the watch at arm's length (42 cm, 12 below the eyes), the back of the wrist to the eyes
  trip 42632 3839 -22529; sleep 2
  w "$LREST" 0.3; tap R b 0.4; sleep 3; dp timer-card; tap R a 0.4
  for i in $(seq 1 40); do sleep 3
    [ $i = 4 ] && state | grep -q 'video=1' && { echo "$(date +%T) the video: A, B 1 s later (the skip)"; tap R a 0.3; sleep 0.5; tap R b 0.3; }
    [ $i = 7 ] && state | grep -q 'video=1' && { echo "$(date +%T) the video: A, B again"; tap R a 0.3; sleep 0.5; tap R b 0.3; }
    [ $i -gt 3 ] && state | grep -q 'video=0 ctx=world' && break; done
  echo "$(date +%T) world: $(state)"
  if waitfor '^CUES timer t=.* a mission timer starts' 30; then
    grep -E '^CUES timer' "$R/somvr.log" | tail -2 | cut -c1-200
    sleep 0.5; dp timer-start; sleep 1.5; dp timer-start2
    sleep 4; dp timer-back
    x "$WATCH" 2; dp timer-watch; mk timer-watch; x "hand L off" 1
    dq timer-hud
    left() { grep -E '^CUES timer t=.* s left' "$R/somvr.log" | tail -1 | grep -oE '[0-9.]+ s left' | grep -oE '^[0-9]+'; }
    for i in $(seq 1 120); do l=$(left); [ -n "$l" ] && [ "$l" -le 29 ] && break; sleep 0.5; done
    echo "$(date +%T) $(left) s left"; dp timer-30; sleep 2; dp timer-30b; sleep 4; dp timer-30after
    x "$WATCH" 2; dp timer-watch2; x "hand L off" 1
    for i in $(seq 1 60); do l=$(left); [ -n "$l" ] && [ "$l" -le 9 ] && break; sleep 0.5; done
    echo "$(date +%T) $(left) s left"; dp timer-10; sleep 4; dp timer-5; mk timer-10
    for i in $(seq 1 20); do grep -qE '^CUES timer t=.* the timer is over' "$R/somvr.log" && break; sleep 1; done
    sleep 2; dp timer-over
    grep -E '^CUES timer' "$R/somvr.log" | tail -12 | cut -c1-200
  else
    echo "no timer: $(grep -E '^CUES' "$R/somvr.log" | tail -3 | cut -c1-200)"
  fi
  sleep 3; state; tap R b 0.4; sleep 4; state | grep -q 'ctx=menu' && { tap R a 0.4; sleep 4; }
fi
if has arc; then
  echo "== arc: the practice's captain, his health arc while he fights Talion"
  trip 32500 2150 -24800; w "captain stakes off" 0.5   # the meadow; (boot 211723: the save's captain killed the idle Talion)
  P0=$(cnt '^CAPTAIN t=.* a presentation starts'); w "practice captain" 2.5
  grep -E '^CAPTAIN t=.* spawn ' "$R/somvr.log" | tail -1 | cut -c1-220
  waitfor '^CAPTAIN t=.* a presentation starts' 40 && waitfor '^CAPTAIN t=.* the presentation is over' 40
  for k in $(seq 1 8); do dp "arc-$k"; done
  grep -E '^CUES arc' "$R/somvr.log" | tail -3 | cut -c1-200; mk arc
  w "practice remove" 2
fi
w "cues status" 1; grep -E '^CUES status' "$R/somvr.log" | tail -1 | cut -c1-300
w "endlogs" 2; sleep 2
grep -E '^CUES END' "$R/somvr.log" | tail -1 | cut -c1-400
echo "done $(date +%T)"
echo quit >> "$R/cmd"
