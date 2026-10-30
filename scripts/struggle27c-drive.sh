#!/bin/bash
# struggle27c-drive.sh - build 27 (som-b27-combat, docs/re-xr-pipeline.md 108.6): the death struggle answered by the hands
# (SOMVR_STRUGGLE=thrust). From the soak's struggle27-drive.sh (below its first lines): each try a captain kills Talion
# (no guard); as soon as the game's struggle starts (`STRUGGLE t=.* start`) the test hands thrust (alternating, fast,
# forward and up: SC_THRUSTS of them, SC_GAP_S apart, from SC_AT_S after the start) until it is over; the result:
# Talion up ('PC_PreDeathRecover') or down. SC_TRIES (4). Original header follows.
# struggle27-drive.sh - build 27 (som-b27-soak, docs/re-xr-pipeline.md 107.6): the death struggle soak 2 found stuck
# (boot 20261030-022913: the practice's Captain Role_Drowned, AI_Berserker, Talion's 'PC_OrcBzrk_PreDeath' struggle,
# the game's first-time 'PC_StruggleTutorial' paused the game with its text dropped by SOMVR_HINTS=off; A closed it after
# 154 s; then 167 s more in the struggle until the orc was removed). One headless boot (the soak's recipe; for the
# tutorial's text add SOMVR_HINTS_KEEP_TUT=GP_,PC_Struggle). Up to SK_TRIES (6) tries, Role_Drowned and
# Role_Executioner (both AI_Berserker) in turn: picked, the stakes on, the practice's Captain, no guard, his blows until
# Talion kneels; the quick struggle ('_B': dead in ~5 s, no chance) = the screens answered (B) and the practice area
# again, the next try; the long one (the tutorial, or no end in 8 s) = the experiment: at the tutorial's pause: a
# both-eye dump, the HINTS lines, A; in the struggle: a dump, the PROMPTS lines, then each of SK_BUTTONS ("R x;R y;R b;
# R a;R trigger;L trigger;L x;L y") mashed 10 times in 3 s until the struggle ends (Talion up or down) - which one ends
# it, and what the game showed. Then the orc removed, the stakes off, quit. SK_RUN=RUNDIR attaches to a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${SK_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
BUTTONS=${SK_BUTTONS:-"R x;R y;R b;R a;R trigger;L trigger;L x;L y"}
T0=$(date +%s)
R=${SK_RUN:-}
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
L=$R/somvr.log; X=$R/xr-layer.log; ST=$R/struggle-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1" | tee -a "$ST"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "${2:-$L}"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); note "dump $1 $f"; }
ui() { grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-90; }
struggling() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -q 'ctx=struggle'; }
over() { [ "$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down")" -gt "$1" ]; }
# a press the layer saw (each line written alone: the layer reads the file's last state)
press() { local h=${1% *} b=${1#* } k; for k in $(seq 1 "${2:-10}"); do T_S=0.12 t "btn $h $b 1"; T_S=0.15 t "btn $h $b 0"; done; }

t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$X" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0" 0.4; w "hands test R 22 -35 -30 0 180 0" 0.4; w "front keep 0" 0.3
w "captain status" 2
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
member() { grep -E "^CAPTAIN member [0-9]+: .*$1" "$L" | tail -1 | sed -nE 's/^CAPTAIN member ([0-9]+):.*/\1/p'; }
# (the death's screens come a few seconds after "Talion is down": wait for them first - boot 20261030-035843's tries 2,
# 4 and 6 started behind unanswered Nemesis screens)
back() { local k ok=0; for k in $(seq 1 20); do inworld || break; sleep 1; done
  for k in $(seq 1 30); do userin && bail "the user is in VR"; sleep 3
    if inworld; then ok=$((ok + 1)); [ $ok -ge 2 ] && break; else ok=0; T_S=0.3 t "btn R b 1"; t "btn R b 0"; fi; done
  w "practice remove" 2; w "practice go 32500 2100 -24800" 8; note "back: $(ui)"; }
# one try: the member's Captain, no guard until Talion kneels; the long struggle (the tutorial, or no end in 8 s) = the
# experiment (returns 0); the quick one (_B: dead in ~5 s) or none = back to the practice area (returns 1)
try() { local n0 i tut=0 st=0 R0 v
  [ -n "$1" ] && w "captain pick $1" 0.4
  w "captain stakes on" 0.3; n0=$(lines); w "practice captain" 3
  since "$n0" '^CAPTAIN t=.* spawn ' | head -1 | cut -c1-200 | tee -a "$ST"
  for i in $(seq 1 60); do [ "$(since "$n0" '^CAPTAIN t=.* the presentation is over' | wc -l)" -gt 0 ] && break; sleep 1; done
  w "execute chase on" 0.4
  R0=$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down")
  for i in $(seq 1 200); do userin && bail "the user is in VR"
    [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && { tut=1; break; }
    struggling && { st=1; break; }
    over "$R0" && break; sleep 1; done
  w "execute chase off" 0.3
  [ $st = 1 ] && for i in $(seq 1 8); do sleep 1; [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && { tut=1; break; }
    over "$R0" && break; struggling || break; done
  v=$(since "$n0" "^BODYMOMENT t=.*attack 'PC_OrcBzrk_PreDeath[A-Za-z_]*'" | head -1 | grep -oE "PC_OrcBzrk_PreDeath[A-Za-z_]*")
  note "try: tutorial $tut, struggle $st ($v), over $(over "$R0" && echo yes || echo no); $(ui)"
  since "$n0" '^(HINTS tutorial|SCREENS t=.* the death|BODYMOMENT t=.*cond [a-z,-]+ -> [a-z,]*(death|struggle))' | cut -c1-200 | tee -a "$ST"
  if [ $tut = 1 ] || { struggling && ! over "$R0"; }; then RR=$R0; N0=$n0; TUT=$tut; return 0; fi
  back; return 1; }

LREST="hands test L -25 -35 25 0 0 0"; RREST="hands test R 25 -35 25 0 0 0"
thrust() { if [ "$1" = L ]; then w "hands swing L -10 -12 70 0 0 0 110" 0.15; w "$LREST" 0.2
           else w "hands swing R 10 -12 70 0 0 0 110" 0.15; w "$RREST" 0.2; fi; }
stry() { local n0 i k got=0 h=R
  [ -n "$1" ] && w "captain pick $1" 0.4
  w "captain stakes on" 0.3; n0=$(lines); w "practice captain" 3
  since "$n0" '^CAPTAIN t=.* spawn ' | head -1 | cut -c1-200 | tee -a "$ST"
  for i in $(seq 1 60); do [ "$(since "$n0" '^CAPTAIN t=.* the presentation is over' | wc -l)" -gt 0 ] && break; sleep 1; done
  w "execute chase on" 0.4; w "$LREST" 0.2; w "$RREST" 0.2
  local R0; R0=$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down")
  for i in $(seq 1 400); do userin && bail "the user is in VR"
    [ "$(since "$n0" '^STRUGGLE t=.* start' | wc -l)" -gt 0 ] && { got=1; break; }
    [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && { T_S=0.3 t "btn R a 1"; t "btn R a 0"; }
    over "$R0" && break; sleep 0.5; done
  w "execute chase off" 0.3
  since "$n0" '^STRUGGLE t=.* start' | head -1 | cut -c1-600 | tee -a "$ST"
  if [ $got = 1 ]; then
    local mode=${2:-thrust} a x y
    note "the answer: $mode"
    sleep "${SC_AT_S:-0.3}"
    for k in $(seq 1 120); do
      [ "$(since "$n0" '^STRUGGLE t=.* over after' | wc -l)" -gt 0 ] && break
      [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && [ "$(since "$n0" '^SESSION ui .*paused=1' | wc -l)" -gt 0 ] && { T_S=0.3 t "btn R a 1"; t "btn R a 0"; }
      a=$(( (k * 360 / ${SC_STEPS:-16}) % 360 ))
      case $mode in
        thrust) thrust $h; [ $h = R ] && h=L || h=R; sleep "${SC_GAP_S:-0.05}" ;;
        hand) x=$(awk "BEGIN{printf \"%.0f\", 25 + 24 * cos($a * 3.14159 / 180)}"); y=$(awk "BEGIN{printf \"%.0f\", -35 + 24 * sin($a * 3.14159 / 180)}")
              printf '%s' "hands test R $x $y 30 0 0 0" > "$R/somvr.trigger"; sleep 0.1 ;;
        aimhand) [ $k = 1 ] && printf '%s' "hands test R 25 -5 30 0 0 0" > "$R/somvr.trigger"; sleep 0.25
                 w "hands swing L -25 -33 72 0 0 0 110" 0.15; w "hands test L -25 -35 25 0 0 0" 0.0 ;;
        aimstick) [ $k = 1 ] && T_S=0.1 t "stick L 0 0.95"; sleep 0.25
                  w "hands swing L -25 -33 72 0 0 0 110" 0.15; w "hands test L -25 -35 25 0 0 0" 0.0 ;;
        handup) printf '%s' "hands test R 25 -5 30 0 0 0" > "$R/somvr.trigger"; sleep 0.2 ;;
        handdown) printf '%s' "hands test R 25 -65 30 0 0 0" > "$R/somvr.trigger"; sleep 0.2 ;;
        handright) printf '%s' "hands test R 55 -35 30 0 0 0" > "$R/somvr.trigger"; sleep 0.2 ;;
        stickup) T_S=0.2 t "stick L 0 0.95" ;;
        stickdown) T_S=0.2 t "stick L 0 -0.95" ;;
        stick) x=$(awk "BEGIN{printf \"%.2f\", 0.95 * cos($a * 3.14159 / 180)}"); y=$(awk "BEGIN{printf \"%.2f\", 0.95 * sin($a * 3.14159 / 180)}")
               T_S=0.1 t "stick L $x $y" ;;
      esac
    done
    case $mode in stick*|aimstick) t "stick L 0 0" ;; esac
    w "$RREST" 0.2
    for i in $(seq 1 20); do [ "$(since "$n0" '^STRUGGLE t=.* over after' | wc -l)" -gt 0 ] && break; sleep 0.5; done
    sleep 3
  fi
  note "stry: struggle $got; recover $(since "$n0" 'PC_PreDeathRecover' | wc -l), down $(since "$n0" '^CAPTAIN t=.* Talion is down' | wc -l); $(ui)"
  since "$n0" '^STRUGGLE t=' | grep -vE 'a thrust of|the aim [-+]' | head -14 | cut -c1-300 | tee -a "$ST"
  echo "aim lines: $(since "$n0" '^STRUGGLE t=.* the aim [-+]' | wc -l)" | tee -a "$ST"
  echo "thrusts: $(since "$n0" '^STRUGGLE t=.* a thrust of' | wc -l)" | tee -a "$ST"
  back; }
D=$(member Role_Drowned); E=$(member Role_Executioner); note "members: Drowned ${D:-none}, Executioner ${E:-none}"
MODES=(${SC_MODES:-hand stick hand thrust})
for k in $(seq 1 "${SC_TRIES:-4}"); do
  [ $((k % 2)) = 1 ] && m=$D || m=$E
  md=${MODES[$(( (k - 1) % ${#MODES[@]} ))]}
  note "== try $k (member ${m:-next}, the answer $md)"
  stry "$m" "$md"
done
w "practice remove" 2; w "captain stakes off" 0.3; sleep 3
grep -E '^STRUGGLE END|^CONFIG struggle' "$L" | cut -c1-500 | tee -a "$ST"
echo "$(date +%T) done"; [ -n "${SC_NOQUIT:-}" ] || echo quit >> "$R/cmd" # SC_NOQUIT=1: leave it running (a next driver)
