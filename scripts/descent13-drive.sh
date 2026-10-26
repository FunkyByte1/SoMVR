#!/bin/bash
# descent13-drive.sh - build 13's way down (SOMVR_CLIMB_DESCENT=on, re-xr-pipeline.md 40; handoff/HANDOFF-climb.md),
# headless, the drill's finding 1 (a forge tower top had no way down). One boot with the full scripts/headset-build13.env
# (its descent line) + the test hands, run as
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build13.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh
#     <this worktree>/ShadowOfMordor/mod/somvr
# with this script at the same time. DS_PLAN (words in order, joined by spaces or _; default "bank bankblink bankedge
# tower"):
#   bank      the meadow's stone bank (4.4 m here; `practice go` onto its top, DS_BANK; turned to its edge, walked to it,
#             `climb probe edge`): the lip from above (the right hand palm-down over the top at the edge, squeezed:
#             `grab R ... a lip ... DESCENT`), pushed up 84 cm in 4 cm steps (the lip step: out over the edge, then down
#             the face), two holds hand over hand down the face (behind the head: the wall ray), both
#             open: the hang; A: a short drop (the feet ~2.5 m up) = the real fall (`A: no blink: a short drop`)
#   bankblink the same with `climb blink 150`: A in the hang = the blink (the practice trip's fade) to the meadow
#   wall      the bank's face from the gravel at its foot (DS_FOOT): up three holds, down one (a hold pushed up lowers
#             him), the hang, A with `climb blink 100` = the blink to the ground; up one hold, the hang, A at the
#             default 400 = a short drop: the real fall
#   bankedge  on the bank's top at its edge, A on foot: the edge ahead, 4.4 m: the blink down
#   tower     `practice back` (the save's forge tower top), DS_WALK s ahead (the drill's H: the walk stops at an invisible
#             'Air' wall ~64 cm ahead of the root, run 010412), `climb probe edge`, the hand over it (the floor goes on:
#             no lip), A on foot: the blink to the tower's foot, then the stick walks on
#   probe     only the edge probe where Talion stands
# Eyes: the layer's dumps (`d NAME`: xr_compose.py RUN draws them) at the edge, in the hang, during the blink and on the
# ground; marks (`m NAME`: RUN/marks/*/eyes.jpg) at the main places. DS_RUN=RUN drives a running boot; DS_NOQUIT=1
# leaves it running. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${DS_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${DS_PLAN:-bank wall bankedge tower}; PLAN=${PLAN//_/ }
BANK=${DS_BANK:-31480 2260 -24797}
T0=$(date +%s)
R=${DS_RUN:-}
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
N=0; S=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
d() { t "dump"; echo "$(date +%T) DUMP $1"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
step() { S=$((S + 1)); echo "$(date +%T) STEP $S $*"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
swing() { local n0; n0=$(cnt "^BODY test swing $1: from"); w "hands swing $1 $2 $3 $4 0 0 0 $5" 0.01
  waitfor "^BODY test swing $1: from" "$n0" 4; sleep "$(awk "BEGIN{print $5/1000 + 0.4}")"; }
turnto() { local b=${1%.*} n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbs() { grep -E '^CLIMB t=' "$L" | tail -${1:-6} | cut -c1-230; }
# probe: the edge's profile along the heading -> E (cm ahead of the root where the landing drops > 150 cm under the
# root), EYE (the head over the floor), HEAD (the heading, deg)
probe() { local n0; n0=$(cnt '^CLIMB probe edge surfaces'); w "climb probe edge" 1; waitfor '^CLIMB probe edge surfaces' "$n0" 5
  w "climb probe" 1
  grep -E '^CLIMB probe edge' "$L" | tail -4 | cut -c1-400
  local pl; pl=$(grep -E '^CLIMB probe edge t=.*the profile' "$L" | tail -1)
  local pr; pr=$(echo "$pl" | sed 's/.*(60 m down)://' | tr ' ' '\n' | awk -F'[=a-zC#]' '/=/{k=$1+0; v=$2; if (n++ == 0) v0 = v + 0
    if (v == "-" || v + 0 > v0 + 30) {print k, last; exit} last = k}')
  E=${pr% *}; EF=${pr#* }
  HEAD=$(echo "$pl" | sed -E 's/.*heading (-?[0-9.]+) deg.*/\1/')
  local hl; hl=$(grep -E '^CLIMB probe t=' "$L" | tail -1)
  local hy ry; hy=$(echo "$hl" | sed -E 's/.* head [-0-9.]+,([-0-9.]+),.*/\1/'); ry=$(echo "$pl" | sed -E 's/.* root [-0-9.]+,([-0-9.]+),.*/\1/')
  EYE=$(awk "BEGIN{printf \"%d\", $hy - $ry + 102}")
  echo "PROBE: the floor to ${EF:-?} cm, the drop at ${E:-?} cm ahead of the root, heading $HEAD deg, the eyes $EYE cm over the floor"; }
# lip: the right hand palm-down 15 cm over the top, at the floor's last point before the edge, squeezed; 1 = a hold
lip() { local y=$(( -EYE + 15 )) z=$(( ${EF:-30} - 5 )) g0; g0=$(cnt '^CLIMB t=.* grab R:')
  [ "$z" -gt 60 ] && z=60   # an arm's reach
  w "hands test L -25 -35 20 0 0 0" 0.3; w "hands squeeze L 0" 0.2; w "hands squeeze R 0" 0.3
  w "hands test R 15 $y $z -90 0 0" 1.2; d "$1-reach" 0.3
  w "hands squeeze R 1" 0.3
  waitfor '^CLIMB t=.* grab R:' "$g0" 3 || { echo "NO LIP HOLD"; climbs 4; return 1; }
  climbs 3; LY=$y; LZ=$z; return 0; }
# hmv H X Y Z DY N: hand H from (X, Y, Z) by DY up a word N times, straight (the test swing is an arc about the
# shoulder: run 012245's pull went sideways and up); each word a 0.3 s step (the trigger word is polled every 250 ms)
hmv() { local k y=$3; for k in $(seq 1 $6); do y=$(( y + $5 )); w "hands test $1 $2 $y $4 0 0 0" 0.3; done; MY=$y; }
# over: the held hand pushed up 84 cm in 4 cm steps: the lip step takes the body out over the edge (and off the top
# if the game holds a standing Talion back), then down the face
over() { hmv R 15 $LY $LZ 4 11; climbs 2; d "$1-out" 0.3
  hmv R 15 $MY $LZ 4 10; climbs 3; d "$1-down" 0.3; }
# ontop: practice go onto the bank's top, turned to its edge (+x) and walked to it
ontop() { local A0; A0=$(cnt 'arrived at .* in the practice area')
  w "practice go $BANK" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
  sleep 3; probe
  turnto "$(awk "BEGIN{print 90 - $HEAD}")"; sleep 1; probe
  [ -n "$EF" ] && [ "$EF" -gt 60 ] && { w "stick 0 1 0 3" 3.5; probe; }
  d bank-edge 0.3; }
# face NAME N: hand over hand down the face N holds (the face behind the head of a player who went over facing out:
# the hand 25 cm behind the head, 110 under it; the wall ray finds it), each pushed up 70 cm
face() { local H=L O=R HX=-20 k g0 T
  for k in $(seq 1 $2); do
    g0=$(cnt "^CLIMB t=.* grab $H:")
    w "hands test $H $HX -170 -25 0 0 0" 0.8; w "hands squeeze $H 1" 0.3
    if ! waitfor "^CLIMB t=.* grab $H:" "$g0" 3; then echo "NO FACE HOLD $k ($H)"; climbs 3; return 1; fi
    w "hands squeeze $O 0" 0.6
    hmv $H $HX -170 -25 7 10; climbs 2
    [ "$k" = 1 ] && { d $1-face 0.3; m $1-face 2; }
    T=$H; H=$O; O=$T; HX=$(( -HX ))
  done; }
# press_a NAME: A pressed; let go with a dump (the eyes ~0.5 s after the press: a blink's black), two more after it
press_a() { t "btn R a 1"; sleep 0.25; t "btn R a 0" "dump"; echo "$(date +%T) DUMP $1-a1"; sleep 0.1; d "$1-a2" 0.2; d "$1-a3" 0.2; }
# footblink NAME: A on foot at the edge: the blink down (dumps in the fade), the arrival, the ground
footblink() { press_a "$1"
  for i in $(seq 1 20); do grep -q 'the blink down ended' "$L" && break; sleep 0.5; done
  grep -E '^CLIMB t=.*(A |A:)|^PRACTICE t=.*(down|arrived|blink)' "$L" | tail -5 | cut -c1-260
  sleep 1; d "$1-ground" 0.5; m "$1-ground" 2; }
# atfoot: practice go onto the gravel at the bank's foot (DS_FOOT), turned to the face (-x) and walked to it
atfoot() { local A0; A0=$(cnt 'arrived at .* in the practice area')
  w "practice go ${DS_FOOT:-31700 1830 -24860}" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area')" -gt "$A0" ] && break; sleep 1; done
  sleep 3; probe; turnto "$(awk "BEGIN{print -90 - $HEAD}")"; sleep 1; w "stick 0 1 0 3" 3.5; w "stick 0 1 0 2" 2.5
  probe; }
# climbs UPDOWN N: N holds on the face ahead hand over hand, up (each hold at the head + 20 pulled down 63 cm) or down
# (each at the head - 60 pushed up 63 cm: the body goes down as much)
climbs_() { local H=${NEXT:-R} O k g0 T Y DY HX
  [ $H = R ] && O=L HX=20 || O=R HX=-20
  [ $1 = up ] && Y=20 DY=-7 || Y=-60 DY=7
  for k in $(seq 1 $2); do
    g0=$(cnt "^CLIMB t=.* grab $H:")
    w "hands test $H $HX $Y 40 0 0 0" 0.6; w "hands squeeze $H 1" 0.3
    if ! waitfor "^CLIMB t=.* grab $H:" "$g0" 3; then echo "NO HOLD $1 $k ($H)"; climbs 3; return 1; fi
    w "hands squeeze $O 0" 0.4
    hmv $H $HX $Y 40 $DY 9; climbs 1
    T=$H; H=$O; O=$T; HX=$(( -HX ))
  done; NEXT=$H; }
L0=$(wc -l < "$L")
w "hands test L -25 -35 20 0 0 0"; w "hands test R 25 -35 20 0 0 0" 1
for p in $PLAN; do
  L1=$(wc -l < "$L")
  case $p in
  probe)
    step "probe: the edge here"; probe; d probe 0.5 ;;
  bank|bankblink)
    [ $p = bank ] && step "bank: practice go onto the meadow bank's top ($BANK), the edge, the lip, over, down the face, the hang, A (a short drop: the real fall)"
    [ $p = bankblink ] && step "bankblink: the same with \`climb blink 150\`: A in the hang = the blink"
    ontop
    if lip $p; then
      m $p-lip 2; over $p
      if grep -q '^CLIMB t=.* lip blocked' "$L"; then echo "LIP BLOCKED (the game's edge wall): A while holding the lip"
        d $p-blocked 0.3; m $p-blocked 2
      else face $p 2; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 2; climbs 3; d $p-hang 0.3; m $p-hang 2; fi
      [ $p = bankblink ] && w "climb blink 150" 0.5
      press_a $p
      for i in $(seq 1 12); do [ $p = bank ] && break; grep -q 'the blink down ended' "$L" && break; sleep 0.5; done
      grep -E '^CLIMB t=.*(A:|A |end:)|^PRACTICE t=.*(down|arrived|blink)|^COMFORT .*fall' "$L" | tail -5 | cut -c1-230
      sleep 2; d $p-down 0.3; m $p-down 2
      [ $p = bankblink ] && w "climb blink 400" 0.5
    fi ;;
  wall)
    step "wall: the bank's face from the gravel at its foot: up three holds, down one (a hold pushed up lowers him), the hang, A with \`climb blink 100\` = the blink; up one, the hang, A = a short drop (the real fall)"
    atfoot; NEXT=R
    climbs_ up 3; d wall-up 0.3; m wall-up 2
    climbs_ down 1; d wall-down 0.3; m wall-down 2
    w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 2; climbs 2; d wall-hang 0.3
    w "climb blink 100" 0.5; press_a wall
    for i in $(seq 1 12); do grep -q 'the blink down ended' "$L" && break; sleep 0.5; done
    grep -E '^CLIMB t=.*(A:|end:)|^PRACTICE t=.*(down|arrived|blink)' "$L" | tail -4 | cut -c1-230
    sleep 1; d wall-ground 0.3; m wall-ground 2; w "climb blink 400" 0.5
    w "stick 0 1 0 2" 2.5; NEXT=R
    climbs_ up 1; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 2; climbs 2; d wall-hang2 0.3
    press_a wall2; sleep 2
    grep -E '^CLIMB t=.*(A:|end:)|^COMFORT|^BODYMOMENT t' "$L" | tail -5 | cut -c1-230
    d wall-fallen 0.3; m wall-fallen 2 ;;
  bankedge)
    step "bankedge: on the bank's top at its edge (4.4 m over the meadow), A on foot: the blink down"
    ontop; footblink bankedge ;;
  tower)
    step "tower: practice back (the save's forge tower top), the walk to the edge, over it, down the face, A: the blink"
    B0=$(cnt 'arrived at .* back where Talion was')
    w "practice back" 1
    for i in $(seq 1 60); do [ "$(cnt 'arrived at .* back where Talion was')" -gt "$B0" ] && break; sleep 1; done
    grep 'arrived at .* back where Talion was' "$L" | tail -1 | cut -c1-160
    sleep 4; d tower 0.5
    w "stick 0 1 0 ${DS_WALK:-8}" "$(awk "BEGIN{print ${DS_WALK:-8} + 0.5}")"
    probe; d tower-edge 0.3; m tower-edge 2
    grep -E '^CLIMB t=.*edge cue' "$L" | tail -1 | cut -c1-200
    # the hands over the invisible wall: the floor goes on (no lip within reach): no climb
    lip tower || true; w "hands squeeze R 0" 0.5; w "hands test R 25 -35 20 0 0 0" 0.5
    footblink tower
    w "stick 0 1 0 3" 3.5; d tower-walk 0.5
    grep -E '^LOCO t=' "$L" | tail -2 | cut -c1-200 ;;
  esac
  echo "---- since the step:"
  tail -n +"$L1" "$L" | grep -E '^(CLIMB t=.*(grab|end|hang|A:|A at|start|descent|edge cue|lip blocked)|WARN|PRACTICE t=.*(down|arrived|refused))' | cut -c1-200 | tail -20
done
echo "steps done $(date +%T)"
echo "---- END lines:"; w "endlogs" 2; grep -E '^CLIMB END|^PRACTICE END' "$L" | tail -3 | cut -c1-400
grep -c '^WARN' "$L" | sed 's/^/WARN lines: /'
[ "${DS_NOQUIT:-0}" = 1 ] && { echo "the boot left running (DS_NOQUIT=1)"; exit 0; }
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
