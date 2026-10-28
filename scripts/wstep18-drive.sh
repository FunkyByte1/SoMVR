#!/bin/bash
# wstep18-drive.sh - build 18's wraith step (SOMVR_WRAITH_STEP=on, re-xr-pipeline.md 68; handoff/HANDOFF-wraith.md
# "build 18: the wraith step"): the right stick pushed forward and held = the ring where the head's ray meets the
# ground (gold = there, red and crossed = refused), let go = the quick blink onto it, the move stick while aiming =
# cancel. Headless, one boot with scripts/headset-build18.env + the test hands, run as
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build18.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh
#     <this worktree>/ShadowOfMordor/mod/somvr
# with this script at the same time. WS_PLAN (words in order, joined by spaces or _; default "flat cancel wall ledge orc"):
#   flat    in the practice meadow: the ring at the head's -20 deg (dump), -30 deg (nearer), level (the farthest, 7 m),
#           back to -20, let go: the blink (dumps in the fade and after), the distance and the ring's error logged;
#           pushed again at once: refused (the cooldown)
#   cancel  aiming, the move stick pulled back: no ring, nothing moves when the right stick is let go; the walk waits
#   wall    the meadow bank's face from the gravel at its foot (WS_FOOT), looking level at it: refused (the red ring)
#   ledge   the practice bank's top plateau (WS_TOP), at its edge looking over it: refused (a ledge)
#   orc     the practice infantry in front: the ring on the ground behind it (the head's ray through it), let go: the
#           blink past it (where Talion is, or WS_ORC_AT="X Y Z")
# Eyes: the layer's dumps (`d NAME`: python3 scripts/xr_compose.py RUN -f FRAME); the steps and their frames in
# RUN/ws18-steps.txt. WS_RUN=RUN drives a running boot; WS_NOQUIT=1 leaves it running. Stops at once (quit) when the
# user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${WS_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${WS_PLAN:-flat cancel wall ledge orc}; PLAN=${PLAN//_/ }
TOP=${WS_TOP:-33760 3650 -27320}
FOOT=${WS_FOOT:-31700 1830 -24860}
T0=$(date +%s)
R=${WS_RUN:-}
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
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -qE '^PRACTICE t=.* arrived' "$L" 2>/dev/null && break; sleep 3
done
grep -qE '^PRACTICE t=.* arrived' "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 6
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
N=0
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep "${TS:-0.6}"; }
d() { t "dump"; sleep 0.6; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/ws18-steps.txt"; sleep "${2:-0.2}"; }
S=0
step() { S=$((S + 1)); echo "$(date +%T) STEP $S $*"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
wsl() { grep -E '^WSTEP t=' "$L" | tail -${1:-4} | cut -c1-330; }
turnto() { local b=${1%.*} n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
root() { w "climb state" 0.6; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+ yaw [0-9.-]+'; }
probe() { local n0; n0=$(cnt '^CLIMB probe edge surfaces'); w "climb probe edge" 1; waitfor '^CLIMB probe edge surfaces' "$n0" 5
  local pl; pl=$(grep -E '^CLIMB probe edge t=.*the profile' "$L" | tail -1)
  local pr; pr=$(echo "$pl" | sed 's/.*(60 m down)://' | tr ' ' '\n' | awk -F'[=a-zC#]' '/=/{k=$1+0; v=$2; if (n++ == 0) v0 = v + 0
    if (v == "-" || v + 0 > v0 + 30) {print k, last, (v == "-" ? 9999 : v - v0); exit} last = k}')
  E=$(echo "$pr" | cut -d' ' -f1); EF=$(echo "$pr" | cut -d' ' -f2); DROP=$(echo "$pr" | cut -d' ' -f3)
  HEAD=$(echo "$pl" | sed -E 's/.*heading (-?[0-9.]+) deg.*/\1/')
  echo "PROBE: heading $HEAD: the floor to ${EF:-?} cm, the drop at ${E:-?} cm (${DROP:-?} cm down)"; }
go() { local A0; A0=$(cnt 'arrived at .* in the practice area|did not arrive')
  w "practice go${1:+ $1}" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area|did not arrive')" -gt "$A0" ] && break; sleep 1; done
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-200; sleep 3; }
YAW=0
hd() { t "head 0 0 0 yaw $YAW $*"; }
# aim P: the right stick forward with the head at pitch P (a dump NAME after S s)
aim() { hd "pitch $1 s 0.3"; t "stick R 0 0.9"; sleep "${3:-0.8}"; [ -n "$2" ] && d "$2" 0.2; }
letgo() { t "stick R 0 0" "dump"; sleep 0.3; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1-letgo $f" | tee -a "$R/ws18-steps.txt"; }
blinks() { cnt '^WSTEP t=.* the blink onto'; }
arrivals() { cnt '^WSTEP t=.* arrived'; }
# the orc's bearing from the head's ray (the gaze's test word) -> OB_Y OB_P OB_D
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_D=$(echo "$l" | sed -nE 's/.* at ([0-9.]+) m: .*/\1/p'); [ -n "$OB_Y" ]; }
# the controls card away, the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"
grep -E '^(WARN|CONFIG wraith)' "$L" | head -6 | cut -c1-300
L0=$(wc -l < "$L")
w "wstep state" 0.5; grep -E '^WSTEP state' "$L" | tail -1 | cut -c1-300
for p in $PLAN; do
  case $p in
  flat)
    step "flat: the ring at -20 deg, -30, level, -20; let go: the blink"
    hd "pitch -20 s 0.3"; sleep 0.5; d flat-before 0.2
    aim -20 flat-ring20 1.0; wsl 3
    hd "pitch -30 s 0.3"; sleep 0.6; d flat-ring30 0.2
    hd "pitch 0 s 0.3"; sleep 0.6; d flat-ring-level 0.2
    hd "pitch -20 s 0.3"; sleep 0.8; d flat-ring20b 0.2; wsl 4
    b0=$(blinks); a0=$(arrivals); echo "$(date +%T) before: $(root)"
    TS=0.05 letgo flat; d flat-fade1 0.02; d flat-fade2 0.02
    waitfor '^WSTEP t=.* arrived' "$a0" 6; sleep 0.8; d flat-landed 0.2; wsl 3; echo "$(date +%T) after: $(root)"
    [ "$(blinks)" -gt "$b0" ] && echo "FLAT: THE BLINK" || echo "FLAT: NO BLINK"
    step "flat: pushed again at once (a boot with SOMVR_WRAITH_STEP_COOL_MS=3000 for the headless pace): the cooldown"
    TS=0.3 t "stick R 0 0.9"; TS=0.5 t "stick R 0 0"; wsl 2; sleep 2 ;;
  cancel)
    step "cancel: aiming, the move stick pulled back: nothing; the walk waits for its rest"
    sleep 1.5; b0=$(blinks); echo "$(date +%T) before: $(root)"
    aim -20 cancel-ring 1.0
    t "stick L 0 -0.8"; sleep 0.5; d cancel-after 0.2
    t "stick L 0 0"; sleep 0.3; t "stick R 0 0"; sleep 1.5; wsl 3; echo "$(date +%T) after: $(root)"
    [ "$(blinks)" -eq "$b0" ] && echo "CANCEL: no blink" || echo "CANCEL: A BLINK" ;;
  wall)
    step "wall: the bank's face from its foot ($FOOT), looking level at it: refused"
    YAW=0; hd "s 0.2"; go "$FOOT"; probe; turnto "$(awk "BEGIN{print -90 - $HEAD}")"; sleep 1
    w "stick 0 1 0 ${WS_WALK_S:-0.8}" 1.5; probe
    b0=$(blinks)
    aim 0 wall-level 1.0; wsl 2
    aim 10 wall-up 0.8; wsl 2
    letgo wall; sleep 1.5; wsl 2
    [ "$(blinks)" -eq "$b0" ] && echo "WALL: no blink" || echo "WALL: A BLINK"
    hd "pitch 0 s 0.3" ;;
  ledge)
    step "ledge: onto the plateau ($TOP), to its edge, looking over it: refused"
    YAW=0; hd "s 0.2"; go "$TOP"; probe; turnto "$(awk "BEGIN{print 15 - $HEAD}")"; sleep 0.5; probe
    for k in 1 2 3 4; do
      [ -n "$EF" ] && [ "$EF" -gt 40 ] || break
      w "stick 0 1 0 $(awk "BEGIN{print ($EF - 30) / 400}")" 1.2; probe
    done
    echo "$(date +%T) at the edge: $(root)"
    b0=$(blinks)
    aim -40 ledge-short 1.0; wsl 2
    aim -15 ledge-over 0.8; wsl 2
    letgo ledge; sleep 1.5; wsl 2
    [ "$(blinks)" -eq "$b0" ] && echo "LEDGE: no blink" || echo "LEDGE: A BLINK"
    hd "pitch 0 s 0.3" ;;
  orc)
    step "orc: back to the meadow, the infantry in front, the ring behind it, let go: past it"
    YAW=0; hd "s 0.2"; [ -n "$WS_ORC_AT" ] && go "$WS_ORC_AT"; sleep 1
    w "practice spawn infantry" 0.3; waitfor '^PRACTICE t=.* spawn' "$(cnt '^PRACTICE t=.* spawn')" 10; sleep 1.5
    for k in 1 2 3; do orc0 && break; sleep 0.5; done
    if [ -z "$OB_Y" ]; then echo "ORC: none found"; else
      YAW=$(awk -v a="$YAW" -v b="$OB_Y" 'BEGIN{printf "%.0f", a+b}'); hd "pitch 0 s 0.3"; sleep 0.4; orc0
      # the ground 2 m behind it: the eyes ~165 cm over it
      P=$(awk -v d="$OB_D" 'BEGIN{printf "%.1f", -atan2(165, d * 100 + 200) * 57.29578}')
      echo "$(date +%T) the orc ${OB_D} m ahead (${OB_Y} deg right): the head at pitch $P; before: $(root)"
      b0=$(blinks); a0=$(arrivals)
      aim "$P" orc-ring 1.0; wsl 3
      TS=0.05 letgo orc; d orc-fade 0.02
      waitfor '^WSTEP t=.* arrived' "$a0" 6; sleep 0.8; d orc-landed 0.2; wsl 3; echo "$(date +%T) after: $(root)"
      [ "$(blinks)" -gt "$b0" ] && echo "ORC: THE BLINK" || echo "ORC: NO BLINK"
      orc0 && echo "ORC: now ${OB_D} m away, ${OB_Y} deg right of the head's ray"
      YAW=$(awk -v a="$YAW" 'BEGIN{printf "%.0f", a+180}'); hd "pitch 0 s 0.5"; sleep 0.8; d orc-behind 0.2
    fi
    w "practice remove" 1 ;;
  esac
done
echo "== the wraith step's lines"
sed -n "$((L0 + 1)),\$p" "$L" | grep -E '^WSTEP|^OBJECTIVE t=.*wraith|^PRACTICE t=.*(down|arrived)' | cut -c1-300 | tail -50
grep -E 'objective frame .*landing' "$R/xr-layer.log" | tail -8 | cut -c1-250
cat "$R/ws18-steps.txt" 2>/dev/null
[ -n "$WS_NOQUIT" ] || { echo quit >> "$R/cmd"; echo "$(date +%T) quit"; }
