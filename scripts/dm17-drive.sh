#!/bin/bash
# dm17-drive.sh - build 17's drop mark (SOMVR_CLIMB_DESCENT_MARK=on, re-xr-pipeline.md 55; handoff/HANDOFF-climb.md
# "build 17: the drop's marker"): A held = the landing shown (a ring where Talion will stand, a line from the edge
# down to it), A let go = the quick blink there, the move stick while A is held = cancel, a tap = nothing. Headless,
# one boot with scripts/headset-build17.env + the test hands, run as
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build17.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh
#     <this worktree>/ShadowOfMordor/mod/somvr
# with this script at the same time. DM_PLAN (words in order, joined by spaces or _; default "edge hang"):
#   edge  the practice bank's top plateau (DM_TOP, the user's 16 m cliff over the meadow, run 025601's wall run's top):
#         `climb probe edge` along four headings, turned to the deepest drop, walked to its edge; then a tap (A for 0.1 s:
#         the ring a moment, nothing moves), A held looking down (the ring and the line from the top: dumps), the stick
#         pulled back while A is held (cancelled: nothing moves when A is let go), walked to the edge again, A held 0.8 s
#         and let go: the quick blink down onto the ring (dumps in the fade and on the ground)
#   hang  the meadow bank's face from the gravel at its foot (DM_FOOT): up three holds, both open (the hang), A held:
#         the ring under him (a short drop: the fall), let go: the fall; up again, `climb blink 100`, the hang, A held:
#         the ring 40 cm out from the wall, let go: the blink
#   probe only the edge probes where Talion stands
# Eyes: the layer's dumps (`d NAME`: python3 scripts/xr_compose.py RUN -f FRAME); the steps and their frames in
# RUN/dm17-steps.txt. DM_RUN=RUN drives a running boot; DM_NOQUIT=1 leaves it running. Stops at once (quit) when the
# user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${DM_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
PLAN=${DM_PLAN:-edge hang}; PLAN=${PLAN//_/ }
TOP=${DM_TOP:-33760 3650 -27320}
FOOT=${DM_FOOT:-31700 1830 -24860}
T0=$(date +%s)
R=${DM_RUN:-}
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
      echo "$(date +%T) xrtest $N: $*"; sleep "${TS:-0.6}"; }
d() { t "dump"; sleep 0.6; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/dm17-steps.txt"; sleep "${2:-0.2}"; }
m() { w "mark $1" "${2:-4}"; }
step() { S=$((S + 1)); echo "$(date +%T) STEP $S $*"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
turnto() { local b=${1%.*} n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
climbs() { grep -E '^CLIMB t=' "$L" | tail -${1:-6} | cut -c1-260; }
marks() { grep -E '^CLIMB t=.*drop mark' "$L" | tail -${1:-3} | cut -c1-330; }
root() { w "climb state" 0.6; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+ yaw [0-9.-]+'; }
# probe: the edge's profile along the heading -> E (cm ahead of the root where the floor drops > 30 cm), EF (the last
# floor before it), DROP (how far down past it), HEAD (the heading, deg)
probe() { local n0; n0=$(cnt '^CLIMB probe edge surfaces'); w "climb probe edge" 1; waitfor '^CLIMB probe edge surfaces' "$n0" 5
  local pl; pl=$(grep -E '^CLIMB probe edge t=.*the profile' "$L" | tail -1)
  local pr; pr=$(echo "$pl" | sed 's/.*(60 m down)://' | tr ' ' '\n' | awk -F'[=a-zC#]' '/=/{k=$1+0; v=$2; if (n++ == 0) v0 = v + 0
    if (v == "-" || v + 0 > v0 + 30) {print k, last, (v == "-" ? 9999 : v - v0); exit} last = k}')
  E=$(echo "$pr" | cut -d' ' -f1); EF=$(echo "$pr" | cut -d' ' -f2); DROP=$(echo "$pr" | cut -d' ' -f3)
  HEAD=$(echo "$pl" | sed -E 's/.*heading (-?[0-9.]+) deg.*/\1/')
  echo "PROBE: heading $HEAD: the floor to ${EF:-?} cm, the drop at ${E:-?} cm (${DROP:-?} cm down)"; }
go() { local A0; A0=$(cnt 'arrived at .* in the practice area|did not arrive')
  w "practice go $1" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at .* in the practice area|did not arrive')" -gt "$A0" ] && break; sleep 1; done
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-200; sleep 3; }
# ontop: onto the plateau, turned to DM_HEAD (default 15: run 092148's 16 m drop to the meadow, the edge ~1.2 m
# ahead): snap turns, then the test head's yaw for the rest (YAW, kept in every later head line), walked to the edge
# in short steps (the probe's last floor under 40 cm ahead)
YAW=0
hd() { t "head 0 0 0 yaw $YAW $*"; }
ontop() { local k h0
  YAW=0; hd "s 0.2"; go "$TOP"; probe; turnto "$(awk "BEGIN{print ${DM_HEAD:-15} - $HEAD}")"; sleep 0.5; probe
  h0=$HEAD; YAW=$(awk "BEGIN{printf \"%d\", ${DM_HEAD:-15} - $h0}"); hd "s 0.2"; sleep 0.4; probe
  if awk "BEGIN{exit !((${DM_HEAD:-15} - $HEAD) ^ 2 > (${DM_HEAD:-15} - $h0) ^ 2)}"; then YAW=$(( -YAW )); hd "s 0.2"; sleep 0.4; probe; fi
  for k in 1 2 3 4; do
    [ -n "$EF" ] && [ "$EF" -gt 40 ] || break
    w "stick 0 1 0 $(awk "BEGIN{print ($EF - 30) / 400}")" 1.2; probe
  done
  echo "$(date +%T) at the edge (yaw $YAW): $(root)"; }
# A held for S seconds (dumps NAME-held while held, at most two), let go with a dump
hold_a() { t "btn R a 1"; sleep "${2:-0.5}"; d "$1-held"; [ -n "$3" ] && { eval "$3"; }; t "btn R a 0" "dump"
  sleep 0.4; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1-letgo $f" | tee -a "$R/dm17-steps.txt"; }
landed() { waitfor '^PRACTICE t=.*(arrived|did not arrive)' "$1" 10
  grep -E '^PRACTICE t=.*(down|arrived|did not)' "$L" | tail -3 | cut -c1-220; sleep 1.5
  echo "$(date +%T) after: $(root)"; }
# atfoot: the gravel at the bank's foot, turned to the face, walked to it
atfoot() { go "$FOOT"; probe; turnto "$(awk "BEGIN{print -90 - $HEAD}")"; sleep 1
  w "stick 0 1 0 3" 3.5; w "stick 0 1 0 2" 2.5; }
hmv() { local k y=$3; for k in $(seq 1 $6); do y=$(( y + $5 )); w "hands test $1 $2 $y $4 0 0 0" 0.3; done; MY=$y; }
# up N: N holds on the face ahead hand over hand, each at the head + 20 pulled down 63 cm
up() { local H=R O=L k g0 T HX=20
  for k in $(seq 1 $1); do
    g0=$(cnt "^CLIMB t=.* grab $H:")
    w "hands test $H $HX 20 40 0 0 0" 0.6; w "hands squeeze $H 1" 0.3
    if ! waitfor "^CLIMB t=.* grab $H:" "$g0" 3; then echo "NO HOLD $k ($H)"; climbs 3; return 1; fi
    w "hands squeeze $O 0" 0.4
    hmv $H $HX 20 40 -7 9; climbs 1
    T=$H; H=$O; O=$T; HX=$(( -HX ))
  done; }
openhang() { w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 1.2; climbs 2
  grep -E '^CLIMB t=' "$L" | tail -3 | grep -q 'hang:'; }
L0=$(wc -l < "$L")
w "hands test L -25 -35 20 0 0 0"; w "hands test R 25 -35 20 0 0 0" 1
for p in $PLAN; do
  case $p in
  probe)
    step "probe"; for k in 1 2 3 4; do probe; w "turn+" 0.5; w "turn+" 0.5; w "turn+" 0.5; done ;;
  edge)
    step "edge: onto the plateau ($TOP), to its edge"
    ontop; hd "pitch -15 s 0.3"; sleep 0.5; d edge-at 0.3; m edge-at 2
    r0=$(cnt '^PRACTICE t=.*down \(')
    step "edge: A held looking down: the ring and the line; the stick pulled back: cancelled; let go: nothing"
    hd "pitch -50 s 0.4"; sleep 0.5
    t "btn R a 1"; sleep 0.8; d edge-ring-down 0.2; m edge-ring-down 2
    hd "pitch -25 s 0.3"; sleep 0.5; d edge-ring-ahead 0.2
    w "stick 0 -1 0 0.5" 0.3; d edge-cancel 0.2; sleep 0.8
    t "btn R a 0"; sleep 2; marks 3
    [ "$(cnt '^PRACTICE t=.*down \(')" -eq "$r0" ] && echo "CANCEL: no blink" || echo "CANCEL: A BLINK"
    echo "$(date +%T) after the cancel: $(root)"
    step "edge: a tap (A let go at the layer's next look at the file: 0.25-0.45 s headless, so the hold a go needs is 1 s"
    step "      for it: \`climb dropmark 1000\`; back to 300 after): the ring a moment, nothing moves"
    w "climb dropmark ${DM_TAP_MS:-1000}" 0.5
    n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")
    TS=0.02 t "btn R a 1"
    for i in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.02; done
    TS=0.02 t "btn R a 0" "dump"; sleep 0.6
    f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "edge-tap $f" | tee -a "$R/dm17-steps.txt"
    sleep 2; marks 3; [ "$(cnt '^PRACTICE t=.*down \(')" -eq "$r0" ] && echo "TAP: no blink" || echo "TAP: A BLINK"
    w "climb dropmark 300" 0.5
    step "edge: back to the edge, A held 0.8 s and let go: the quick blink onto the ring"
    if [ "$(cnt '^PRACTICE t=.*down \(')" -gt "$r0" ]; then ontop; else
      probe; for k in 1 2 3; do [ -n "$EF" ] && [ "$EF" -gt 40 ] || break; w "stick 0 1 0 $(awk "BEGIN{print ($EF - 30) / 400}")" 1.2; probe; done; fi
    hd "pitch -45 s 0.3"; sleep 0.4
    r0=$(cnt '^PRACTICE t=.*arrived|did not arrive')
    TS=0.05 hold_a edge-go ${DM_GO_S:-0.8}
    d edge-fade1 0.05; d edge-fade2 0.05
    landed "$r0"; hd "pitch -10 s 0.3"; sleep 0.5; d edge-landed 0.3; m edge-landed 2
    t "head 0 0 0 s 0.3"; marks 4 ;;
  hang)
    step "hang: the bank's face from its foot ($FOOT), three holds up, the hang"
    t "head 0 0 0 s 0.3"; atfoot
    if up 3 && openhang; then
      step "hang: A held: the ring under him (a short drop: the fall); let go: the fall"
      t "head 0 0 0 pitch -55 s 0.4"; sleep 0.5
      hold_a hang-fall 0.8; sleep 2; marks 3; climbs 2; echo "$(date +%T) after the fall: $(root)"
    else echo "NO HANG (fall)"; fi
    t "head 0 0 0 s 0.3"
    step "hang: up again, \`climb blink 100\`, the hang, A held: the ring 40 cm out; let go: the blink"
    atfoot; w "climb blink 100"
    if up 3 && openhang; then
      t "head 0 0 0 pitch -55 s 0.4"; sleep 0.5
      r0=$(cnt '^PRACTICE t=.*arrived|did not arrive')
      hold_a hang-blink 0.8
      landed "$r0"; t "head 0 0 0 s 0.3"; sleep 0.5; d hang-landed 0.3; marks 3; climbs 2
    else echo "NO HANG (blink)"; fi
    w "climb blink 400" ;;
  esac
done
echo "== the drop mark's lines"
sed -n "$((L0 + 1)),\$p" "$L" | grep -E '^CLIMB t=.*(drop mark|end:)|^OBJECTIVE t=.*landing|^PRACTICE t=.*(down|arrived)' | cut -c1-300 | tail -40
grep -E 'objective frame .*(landing|line)' "$R/xr-layer.log" | tail -6 | cut -c1-250
cat "$R/dm17-steps.txt" 2>/dev/null
[ -n "$DM_NOQUIT" ] || { w "climb state" 0.5; echo quit >> "$R/cmd"; echo "$(date +%T) quit"; }
