#!/bin/bash
# prologue-drive.sh - som-b37-ladder (docs/re-xr-pipeline.md 172): the prologue played forward by the game's own inputs
# (the test word `verbs press CMD MS`: the stock commands the prologue's lessons wait for) and the test aids, so a new
# game reaches Udûn's first mission. Run by scripts/ladder_boot.sh 00 prologue-drive.sh (LADDER_RUN = the run).
# The intro's script (intro.arch05's intro_blackgates.embb, its Havok Script chunk; read only) is I1_E1..E6: E1 two
# counters (Tutorial_Counter, a failsafe timer ends it), E2 three strikes (a 30 s failsafe), E3 the downed orc's ground
# kill (Tutorial_GroundTakedown: X with RT held; a timer), E4 the orcs outside and the fire orc, E5 to Ioreth (a trigger
# volume: SilentKissOnEnter), E6 the stealth kills (Tutorial_StealthKill), then the outro; the wraith's lessons follow in
# Udûn. Every PL_TICK s (0.8) the loop reads the session and the lesson up (the last HINTS show not hidden) and answers:
#   a video: A then B (skipped);  a paused page: A;  Counter: Y (119);  a strike / attack / no X: X (118);
#   GroundTakedown: X + RT (118+125) next to the nearest downed orc;  StealthKill: behind the nearest orc, X;
#   no lesson for PL_IDLE s (6): X and Y in turn (the fights), and when the game shows an objective marker farther than
#   PL_FAR m (25), Talion there (`practice go`).
# Each new prefix save (the game's checkpoints) is kept as RUN/saves/pro-<HHMMSS>.sav. PL_MIN (25) minutes at most;
# PL_STOP_RE: stop when a somvr.log line matches it (default: a golden marker shown = the prologue is over).
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
R=${LADDER_RUN:-${ST_RUN:-}}; [ -d "$R" ] || { echo "no run (LADDER_RUN)"; exit 1; }
TICK=${PL_TICK:-0.8}; IDLE=${PL_IDLE:-6}; FAR=${PL_FAR:-25}; MIN=${PL_MIN:-25}
STOP_RE=${PL_STOP_RE:-"OBJECTIVE t=.*'golden"}
PFXSAVE=${LADDER_PFXSAVE:-$(grep -m1 -oE 'save: a copy of .* as .*/GameData\.sav' "$R/summary.txt" 2>/dev/null | sed -E 's/.* as //')}
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
gone() { grep -qE '(program exit|the hold .* is over|the game was stopped)' "$R/summary.txt" 2>/dev/null; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.5}"; }
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.4; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
lines() { wc -l < "$R/somvr.log"; }
press() { w "verbs press $1 ${2:-150}" 0.5; NP=$((NP + 1)); }
shot() { echo "shot st-$1" >> "$R/cmd"; echo "$(date +%T) shot st-$1 ($(state))"; }
lesson() { # the last HINTS show whose id has no later hide
  grep -E "^HINTS tutorial #[0-9]+ t=[0-9.]+: (show|hide) '" "$R/somvr.log" | tail -1 |
    sed -nE "s/^HINTS tutorial #[0-9]+ t=[0-9.]+: show '([^']*)'.*/\1/p"; }
near_orc() { # "x y z" of the orc nearest Talion within R cm (`verbs orcs`; Talion's place from `practice status`)
  local l0 me rad=${1:-1500}; l0=$(lines); w "practice status" 1.0
  me=$(tail -n +"$l0" "$R/somvr.log" | grep -oE 'Talion known at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tail -1 | grep -oE '\-?[0-9.]+' | tr '\n' ' ')
  set -- $me; [ -n "$3" ] || return 1; MX=${1%.*}; MY=${2%.*}; MZ=${3%.*}
  l0=$(lines); w "verbs orcs $MX $MZ $rad" 1.2
  tail -n +"$l0" "$R/somvr.log" | grep -E '^VERBS orcs t=.* at -?[0-9]+,-?[0-9]+,-?[0-9]+' |
    awk -v X=$MX -v Z=$MZ '{match($0,/at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+)/,m); d=(m[1]-X)^2+(m[3]-Z)^2; if(best==""||d<bd){bd=d;best=m[1]" "m[2]" "m[3]}} END{print best}'; }
objective_far() { # the newest objective marker the game showed, if farther than FAR m from Talion: "x y z"
  local l o; l=$(grep -E "^OBJECTIVE t=.* shown by the game at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)" "$R/somvr.log" | tail -1)
  o=$(echo "$l" | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | grep -oE '\-?[0-9]+' | tr '\n' ' '); [ -n "$o" ] || return 1
  set -- $o; local d; d=$(python3 -c "import math;print(int(math.hypot($1-${MX:-0},$3-${MZ:-0})/100))")
  [ "$d" -gt "$FAR" ] && echo "$1 $2 $3"; }
echo "$(date +%T) run $R; tick $TICK s, idle $IDLE s, $MIN min; prefix save ${PFXSAVE:-none}"
for i in $(seq 1 200); do
  userin && bail "the user is in VR"; gone && exit 1
  grep -qE '^SESSION ui .*paused=0 video=0 ctx=world' "$R/somvr.log" 2>/dev/null && break; sleep 3
done
sleep 4; w "story invisible off" 0.6
T_END=$((SECONDS + MIN * 60)); LAST_L=""; IDLE_T=$SECONDS; NP=0; SV_T=""; ALT=0; LG0=$(lines)
while [ $SECONDS -lt $T_END ]; do
  userin && bail "the user is in VR"; gone && { echo "the game is gone"; exit 1; }
  grep -qE "$STOP_RE" <(tail -n +"$LG0" "$R/somvr.log") && { echo "$(date +%T) STOP_RE seen: $(grep -E "$STOP_RE" "$R/somvr.log" | tail -1 | cut -c1-160)"; break; }
  if [ -n "$PFXSAVE" ] && [ -f "$PFXSAVE" ]; then   # the game's checkpoints, each kept
    st=$(stat -c %Y "$PFXSAVE"); if [ "$st" != "$SV_T" ]; then [ -n "$SV_T" ] && { mkdir -p "$R/saves"
      cp --no-preserve=mode "$PFXSAVE" "$R/saves/pro-$(date +%H%M%S).sav"; echo "$(date +%T) SAVE the game saved: saves/pro-$(date +%H%M%S).sav ($(state))"; }; SV_T=$st; fi
  fi
  s=$(state); L=$(lesson)
  [ "$L" != "$LAST_L" ] && { echo "$(date +%T) LESSON '${L:-none}' ($s)"; LAST_L=$L; IDLE_T=$SECONDS; [ -n "$L" ] && shot "pl-$L"; }
  if echo "$s" | grep -q 'video=1'; then echo "$(date +%T) video: skip"; sleep 2; tap R a 0.3; sleep 0.7; tap R b 0.3; sleep 2; IDLE_T=$SECONDS; continue; fi
  if echo "$s" | grep -q 'paused=1'; then echo "$(date +%T) paused: A ($s)"; shot "pl-paused-$SECONDS"; sleep 1; tap R a 0.4; sleep 1.5; IDLE_T=$SECONDS; continue; fi
  case $L in
    *Counter*) press 119 150 ;;
    *GroundTakedown*) P=$(near_orc 600); [ -n "$P" ] && { set -- $P; w "practice go $(( $1 + 90 )) $(( $2 + 40 )) $3" 2.5; }; press 118+125 700; sleep 1 ;;
    *StealthKill*|*Stealth*) w "verbs behind 70" 2.0; press 118 200; sleep 1.5 ;;
    *Sprint*|*Run*) press 18 1500 ;;
    *Grab*|*Interrog*) press 26 300 ;;
    "") if [ $((SECONDS - IDLE_T)) -ge "$IDLE" ]; then
          ALT=$((ALT + 1)); [ $((ALT % 2)) = 1 ] && press 118 150 || press 119 150
          if [ $((ALT % 15)) = 0 ]; then near_orc 1500 >/dev/null; O=$(objective_far); [ -n "$O" ] && { set -- $O
            echo "$(date +%T) the objective is far: practice go $O"; w "practice go $1 $(( $2 + 60 )) $3" 4; }; fi
        fi ;;
    *) press 118 150 ;;
  esac
  sleep "$TICK"
done
echo "$(date +%T) end: $(state); presses $NP"
grep -E "^HINTS tutorial #[0-9]+ .*show" "$R/somvr.log" | sed -nE "s/.*show '([^']*)'.*/\1/p" | awk '!s[$0]++' | tr '\n' ' '; echo
[ "${LADDER_NOQUIT:-0}" = 1 ] || echo quit >> "$R/cmd"
