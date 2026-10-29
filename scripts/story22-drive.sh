#!/bin/bash
# story22-drive.sh - a story mission played straight through in VR (build 22, som-b22-story; docs/re-xr-pipeline.md 89,
# handoff/HANDOFF-story.md "build 22"). objective15-drive.sh's driver (the same boot: the save's own place,
# SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0, the test hands, the layer's test
# file) with more commands, run as
#   HEADLESS_HOLD=2400 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build22.env | tr '\n' ' ')
#     SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# ST_PLAN (words in order; default "ctl"): `ctl` = commands one per file ST_CTL_DIR/cmdN.txt (default RUN/ctl), read in
# order (up to 15 min each); with ST_SCRIPT=FILE its lines first (the same commands, run without waiting), then the files:
#   go X Y Z        `practice go` there (a teleport), wait for the arrival, the pictures
#   walk X,Z;...    story2_route.py (the sprint latch, A at the edge cue), timed; then the pictures
#   tap HAND BTN [S] a button pressed S s (0.3) (HAND L|R, BTN a b x y menu stick trigger squeeze)
#   xr LINE;LINE    raw lines to the layer's test file (e.g. `head 0 0 0 yaw 30 s 0.4`, `stick L 0 1`)
#   waitfor RE [S]  until a new somvr.log line matches RE (up to S s, 60)
#   waitlayer RE [S] the same in xr-layer.log
#   world [S]       until the session is the world again (ctx=world, not paused, no video; up to S s, 120)
#   video S N       the eyes and the flat picture every S s, N times (a video, a cinematic)
#   pics NAME       the eyes (a dump: the next stereo frame) and the flat picture (shots/st-NAME.png)
#   look YAW [S]    the test head turned YAW deg for S s, the pictures, back
#   status          `objective status`, `practice status nav`, `practice status`
#   word WORDS      a trigger word;  mark NAME: a mark (the eyes, the flat picture, the tails)
#   brand [CM]      crouched (`crouch hold 9000`), the left palm at the nearest orc's collar (`verbs aim head`), CM > 0:
#                   `verbs behind CM` first; the left trigger tapped (0.2 s): the stealth brand where the game offers it
#   gobrand X Y Z   the same set up first, then `practice go` there, the trigger tapped as soon as the game shows the
#                   stealth brand's prompt (polled every 50 ms, up to 12 s)
#   retry | abandon A on the failed screen | B there, A on Leave Mission
#   wait S          sleep;  end: leave the loop (then the boot quits unless ST_NOQUIT=1)
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${ST_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${ST_PLAN:-ctl}; PLAN=${PLAN//_/ }
T0=$(date +%s)
R=${ST_RUN:-}
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
CTL=${ST_CTL_DIR:-$R/ctl}; mkdir -p "$CTL"
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
d() { t "dump"; echo "shot st-$1" >> "$R/cmd"; echo "$(date +%T) PICS $1 ($(state))"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        echo "$(date +%T) tap $1 $2 ${3:-0.3}"; sleep 0.5; }
inworld() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -q 'ctx=world'; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
moments() { tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT t=.*window|SESSION ui|WARN|OBJECTIVE t=.*(eyes|marker [0-9]+ m=)|PRACTICE t=.*arrived|HINTS|USE t=|PROMPTS show)' |
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
      k=$((k + 1))
      ns=0; [ -n "${ST_SCRIPT:-}" ] && ns=$(grep -cvE '^\s*(#|$)' "$ST_SCRIPT")
      if [ "$k" -le "$ns" ]; then
        line=$(grep -vE '^\s*(#|$)' "$ST_SCRIPT" | sed -n "${k}p")
      else
        f=$CTL/cmd$((k - ns)).txt; echo "$(date +%T) waiting for $f"
        for i in $(seq 1 300); do [ -s "$f" ] && break; userin && bail "the user is in VR"; sleep 3; done
        [ -s "$f" ] || { echo "no $f"; break; }
        line=$(head -1 "$f")
      fi
      set -f; set -- $line; set +f; c=$1; shift; echo "$(date +%T) CMD $k: $c $*"; n0=$(wc -l < "$R/somvr.log")
      case $c in
      walk) wp=$1; shift; f0=$(cnt 'window 0 -> 1'); s0=$(date +%s.%N)
            python3 "$(dirname "$0")/story2_route.py" "$R" --secs "${ST_ROUTE_S:-100}" --tag "st$k" --waypoints "$wp" "$@" || bail "route"
            echo "$(date +%T) walked $(echo "$(date +%s.%N) - $s0" | bc | cut -c1-6) s"; sleep 1; d "walk$k-end" 0.5
            [ "$(cnt 'window 0 -> 1')" -gt "$f0" ] && { sleep 2; d "walk$k-window" 0.5; m "walk$k-window" 2; } ;;
      go|tp) a0=$(cnt 'arrived at'); w "practice go $1 $2 $3" 1
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
      mark) m "$1" 3 ;;
      gobrand) w "crouch hold 9000" 0.2; w "verbs aim head" 0.2; b0=$(stat -c %s "$R/somvr.log"); w "practice go $1 $2 $3" 0.1
             for i in $(seq 1 240); do tail -c +"$b0" "$R/somvr.log" | grep -qE "prompt up: action 120 .*Stealth_(Brand|Torment)" && break; sleep 0.05; done
             echo "$(date +%T.%N | cut -c1-12) the stealth brand's prompt: $(tail -c +"$b0" "$R/somvr.log" | grep -cE 'prompt up: action 120 .*Stealth_(Brand|Torment)')"
             tap L trigger 0.2; sleep 3; d "gobrand$k" 0.5 ;;
      brand) w "crouch hold 9000" 0.2; w "verbs aim head" 0.2; [ "${1:-0}" -gt 0 ] && w "verbs behind $1" 0.35
             tap L trigger 0.2; sleep 3; d "brand$k" 0.5 ;;
      tap) tap "$1" "$2" "${3:-0.3}" ;;
      xr) IFS=';' read -ra XL <<< "$*"; t "${XL[@]}" ;;
      waitfor|waitlayer) lf=$R/somvr.log; [ "$c" = waitlayer ] && lf=$R/xr-layer.log; l0=$(wc -l < "$lf"); re=$1; s=${2:-60}
              for i in $(seq 1 $((s * 2))); do tail -n +"$((l0 + 1))" "$lf" | grep -qE -- "$re" && break; userin && bail "the user is in VR"; sleep 0.5; done
              tail -n +"$((l0 + 1))" "$lf" | grep -E -- "$re" | head -2 | cut -c1-220 ;;
      world) s=${1:-120}; for i in $(seq 1 "$s"); do state | grep -q 'paused=0 video=0 ctx=world' && break; userin && bail "the user is in VR"; sleep 1; done
             echo "$(date +%T) $(state)" ;;
      video) for i in $(seq 1 "${2:-3}"); do d "v$k-$i" 0.3; sleep "${1:-5}"; done ;;
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
[ "${ST_NOQUIT:-0}" = 1 ] && { echo "the boot left running (ST_NOQUIT=1)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
