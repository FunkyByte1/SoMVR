#!/bin/bash
# objective15-drive.sh - the story's way-finding (handoff/HANDOFF-objective.md, docs/re-xr-pipeline.md 44): the HUD's
# objective marker in VR (SOMVR_OBJECTIVE) and the first timed objective ("Make haste to the Sea Fort", 1:30). One
# headless boot as story14-drive.sh's (the save's own place: SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0
# SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0), run as
#   HEADLESS_HOLD=1500 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build15.env | tr '\n' ' ')
#     SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# with this script at the same time. OB_PLAN (words in order; default "start ctl"):
#   start  = the golden M (42632 3889 -22529: "Queen of the Shore"), B (the card), A (Accept), the video skipped (A, B
#            1 s later), back in the world: the timed objective; `objective status` and `practice status nav` at once
#            and 3 s later (the markers the game shows, their places).
#   ctl    = then commands, one per file OB_CTL_DIR/cmdN.txt (N = 1, 2, ...; default dir RUN/ctl), read in order (the
#            driver waits up to 15 min for each; the game paused meanwhile only if a command says so):
#              walk X,Z;X,Z;... [story2_route.py options]   the route walked as a player (the sprint latch, A at the
#                                                           edge cue), timed; then the moments and a picture
#              tp X Y Z     `practice go` there (a teleport: for finding the trigger, not a player's way)
#              look YAW [S] the test head turned YAW deg (+ = left) for S s, the eyes and the flat picture (pics), back
#              pics NAME    the eyes (a dump: the next stereo frame) and the flat picture (shots/ob-NAME.png)
#              status       `objective status`, `practice status nav`, `practice status`
#              word WORDS   a trigger word (e.g. `objective game`)
#              pause|unpause the menu button tapped (a real pause: the timer stops)
#              retry        A on the failed screen (a fresh 1:30 at the objective's start)
#              abandon      B on the failed screen, A on the Leave Mission dialog
#              wait S       sleep S seconds
#              end          leave the loop (then the boot quits unless OB_NOQUIT=1)
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${OB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${OB_PLAN:-start ctl}; PLAN=${PLAN//_/ }
T0=$(date +%s)
R=${OB_RUN:-}
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
CTL=${OB_CTL_DIR:-$R/ctl}; mkdir -p "$CTL"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
ARR='^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world'
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  [ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge 2 ] && break; sleep 3
done
[ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge 2 ] || bail "no arrival"
echo "arrived $(date +%T)"
sleep 6
N=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
d() { t "dump"; echo "shot ob-$1" >> "$R/cmd"; echo "$(date +%T) PICS $1 ($(state))"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        echo "$(date +%T) tap $1 $2 ${3:-0.3}"; sleep 0.5; }
inworld() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -q 'ctx=world'; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
moments() { tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT t=.*window|SESSION ui|WARN|OBJECTIVE t=.*(eyes|marker [0-9]+ m=)|PRACTICE t=.*arrived|HINTS)' |
  cut -c1-200 | head -${2:-40}; }
status() { local n0=$(wc -l < "$R/somvr.log"); w "objective status" 1.5; w "practice status nav" 2; w "practice status" 1
  tail -n +"$n0" "$R/somvr.log" | grep -E "^OBJECTIVE status|^PRACTICE nav [0-9]+ '(Objective|golden|quest|Quest|mission)|^PRACTICE t=.*status" | cut -c1-230; }
L0=$(wc -l < "$R/somvr.log")
# the controls card away, the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
t "hand L head -25 -35 30 0 0 0"
w "hands test R 28 -40 20 0 0 0" 2
d arrival 1
for p in $PLAN; do
  echo "$(date +%T) ==== $p"
  case $p in
  start)
    status
    a0=$(cnt 'arrived at'); w "practice go 42632 3839 -22529" 1
    for i in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done
    grep 'arrived at' "$R/somvr.log" | tail -1 | cut -c1-150; sleep 3; d goldenM 0.5
    ml0=$(wc -l < "$R/somvr.log")
    tap R b 0.4; sleep 3; d card 0.5; tap R a 0.4
    for i in $(seq 1 20); do state | grep -q 'video=1' && break; sleep 1; done
    echo "$(date +%T) the video: $(state)"; sleep 8; tap R a 0.3; sleep 0.7; tap R b 0.3
    for i in $(seq 1 60); do sleep 2; state | grep -q 'video=0 ctx=world' && break; done
    echo "$(date +%T) back in the world: $(state)"; OBJ_T0=$(date +%s.%N)
    status; d obj0 0.3; sleep 2; status; d obj3 0.3
    echo "-- since the start:"; moments "$ml0" 40 ;;
  ctl)
    k=0
    while :; do
      k=$((k + 1)); f=$CTL/cmd$k.txt; echo "$(date +%T) waiting for $f"
      for i in $(seq 1 300); do [ -s "$f" ] && break; userin && bail "the user is in VR"; sleep 3; done
      [ -s "$f" ] || { echo "no $f"; break; }
      set -- $(head -1 "$f"); c=$1; shift; echo "$(date +%T) CMD $k: $c $*"; n0=$(wc -l < "$R/somvr.log")
      case $c in
      walk) wp=$1; shift; f0=$(cnt 'window 0 -> 1'); s0=$(date +%s.%N)
            python3 "$(dirname "$0")/story2_route.py" "$R" --secs "${OB_ROUTE_S:-100}" --tag "ob$k" --waypoints "$wp" "$@" || bail "route"
            echo "$(date +%T) walked $(echo "$(date +%s.%N) - $s0" | bc | cut -c1-6) s"; sleep 1; d "walk$k-end" 0.5
            [ "$(cnt 'window 0 -> 1')" -gt "$f0" ] && { sleep 2; d "walk$k-window" 0.5; m "walk$k-window" 2; } ;;
      tp) a0=$(cnt 'arrived at'); w "practice go $1 $2 $3" 1
          for i in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done
          grep 'arrived at' "$R/somvr.log" | tail -1 | cut -c1-150; sleep 2; d "tp$k" 0.5 ;;
      look) t "head 0 0 0 yaw $1 s 0.4"; sleep "${2:-2}"; d "look$k-$1" 1.5; m "look$k-$1" 2; t "head 0 0 0 yaw 0 s 0.4"; sleep 1 ;;
      pics) d "$1" 1 ;;
      status) status ;;
      word) w "$*" 1.5 ;;
      pause|unpause) tap L menu 0.2; sleep 2; echo "$(date +%T) $(state)" ;;
      retry) c0=$(cnt 'window 1 -> 0'); tap R a 0.4
             for i in $(seq 1 30); do sleep 1; [ "$(cnt 'window 1 -> 0')" -gt "$c0" ] && inworld && break; done
             sleep 0.5; echo "$(date +%T) retried: $(state)" ;;
      abandon) tap R b 0.4; sleep 3; state | grep -q 'ctx=menu' && { tap R a 0.4; sleep 4; }; echo "$(date +%T) $(state)" ;;
      wait) sleep "$1" ;;
      end) break ;;
      *) echo "unknown command $c" ;;
      esac
      echo "-- after $c:"; moments "$n0" 30
      tail -n +"$n0" "$R/somvr.log" | grep -cE '^WARN' | sed 's/^/WARN lines: /'
    done ;;
  esac
done
echo "steps done $(date +%T)"
tail -n +"$L0" "$R/somvr.log" | grep -E '^WARN' | head -5
[ "${OB_NOQUIT:-0}" = 1 ] && { echo "the boot left running (OB_NOQUIT=1)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
