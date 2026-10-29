#!/bin/bash
# view21-drive.sh - build 21's view round (docs/re-xr-pipeline.md 87, handoff/HANDOFF-hud.md "build 21"): the popups
# where you look, nothing smoky in the face. Drives one headless boot (scripts/headless_vr.sh with
# scripts/headset-build21.env, SOMVR_HANDS_TEST=1, the layer's test file SOMVR_XR_TEST=@RUN@/xrtest.txt). The helpers are
# finish20-drive.sh's (the far orc, the stand-off, the grab G1, the Touch's A, the sword's stab = Eliminate). VW_STEPS
# (default "wraith wraithf ask askf"; the second boot "wv xpband xpgaze"), in order:
#   wraith   Y held (the wraith vision): `vbglow all` logs every FX made; dumps level, 45 and 75 deg up, 60 deg right
#            and up; Y let go
#   wraithf  the same with `vbglow face hide` (SOMVR_FX_FACE's words: VW_FACE, default the code's)
#   ask      the interrogation (grab, A, the choices; dumps level and 40 deg up during the hold), the sword's stab =
#            Eliminate, the kill's XP: dumps ahead, then the head turned 70 deg right and 50 left (where is the popup?)
#   askf     the same with `vbglow face hide`
#   wv       the wraith vision's sky 0/45/80 deg up: the game's, then without vm_interactiveobjects, vm_ping_wraith, both
#   xpband   the layer's `popups band` (build 20c), the face FX the game's: an interrogation (looks up 40, 80 deg), the
#            Touch's B = Eliminate, the kill's XP popup: ahead, the head 30 deg left, 70 right, back (dumps)
#   xpgaze   the same with `popups gaze` and `vbglow face hide` (VW_FACE's words)
#   wt       the sky in three consecutive frames (the layer's `dump K`) 60 deg up, without and with the wraith vision
#   xp2band, xp2gaze  an interrogation, the head turned 60 deg right, the Touch's B: the XP popup with `popups band` /
#            `gaze` - dumps at once, the head at 25 deg, back ahead (the ease)
#   xp3      gaze only, the pictures timed to the XP: as it comes up 60 deg from the band's place, the head turned back
#            (the ease), after
# Dumps: RUN/vw-steps.txt (NAME FRAME; compose: python3 scripts/xr_compose.py RUN -f FRAME -o DIR). VW_RUN=RUN drives a
# running boot. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${VW_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${VW_STEPS:-wraith wraithf ask askf}
T0=$(date +%s)
R=${VW_RUN:-}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/vw-steps.txt"; }
look() { x "head 0 0 0 yaw ${1:-0} pitch ${2:-0} s ${3:-0.6}" "${4:-1.2}"; }
spawn_far() { w "practice far infantry" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED\|refused'; then
    echo "$(date +%T) the far spawn failed: back to the practice area's start"; w "practice back" 6; w "practice go" 7; w "practice far infantry" 3
  fi; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6
draw_slot; w "$REST" 0.6; w "front keep 0" 0.3
noticed() { [ "$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log")" -gt "$N0" ]; }
NR=0
reset() { NR=$((NR + 1)); [ "$NR" -gt 1 ] && { w "practice back" 6; w "practice go" 7; }; }
round() { echo "== $1"; reset; N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); spawn_far; m "$1-start" 0.1; }
place_behind() { [ "${VW_PLACE:-220}" -gt 0 ] && { w "verbs behind ${VW_PLACE:-220}" 0.8; w "execute face" 1.0; }; }
endr() { w "verbs aim off" 0.3; x "head 0 0 0 s 0.4" 0.6; w "crouch stand" 0.4; w "practice remove" 2; }
bearing() { w "execute face" 1.2 >&2; w "execute where" 0.6 >&2; grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1 | grep -oE 'bearing [-+][0-9]+' | grep -oE '[-+][0-9]+'; }
head_at() { local dn=$1 ln=$2 b=${3:-0}
  local hx hz; hx=$(awk -v l="$ln" -v b="$b" 'BEGIN{printf "%.1f", l*sin(b*3.14159265/180)}')
  hz=$(awk -v l="$ln" -v b="$b" 'BEGIN{printf "%.1f", -l*cos(b*3.14159265/180)}')
  x "head $hx -$dn $hz yaw $b s ${4:-0.6}" 1.0; }
sneak_to() { local n0 r0 i; n0=$(cnt '^STEALTH t=.* stand-off: orc'); r0=$(cnt '^STEALTH t=.* stand-off: at rest')
  x "head 0 -50 0 s 0.5" 1.2; waitfor '^CROUCH (game|duck) on' 3; w "stealth chase ${1:-0.6} 60" 0.3
  for i in $(seq 1 240); do userin && bail "the user is in VR"; noticed && { echo "$(date +%T) noticed"; break; }
    [ "$(cnt '^STEALTH t=.* stand-off: at rest')" -gt "$r0" ] && break
    [ "$(cnt '^STEALTH t=.* stand-off: orc')" -gt "$n0" ] && [ -z "$S0" ] && S0=$i
    [ -n "$S0" ] && [ $((i - S0)) -gt 12 ] && break; sleep 0.25; done; S0=
  sleep 1.5; w "stealth chase 0" 0.3; sleep 1.0; }
grab() { local b g0; b=$(bearing); echo "bearing $b"; head_at 50 30 "$b"; w "hands test L -10 -6 62 180 -90 0" 0.8
  g0=$(cnt '^VERBS t=.* grab: the left hand'); w "hands squeeze L 1" 0.8
  if [ "$(cnt '^VERBS t=.* grab: the left hand')" -le "$g0" ]; then
    w "hands squeeze L 0" 0.8; w "verbs aim head" 0.8; w "hands squeeze L 1" 0.3; waitfor '^VERBS t=.* grab: the left hand' 3; fi
  [ "$(cnt '^VERBS t=.* grab: the left hand')" -gt "$g0" ] || { echo "$(date +%T) no grab"; w "verbs aim off" 0.3; return 1; }
  grep -E '^VERBS t=.* grab: the left hand' "$R/somvr.log" | tail -1 | cut -c1-200; }
ask() { local i; for i in 1 2 3; do
    waitfor "^VERBS t=.* prompt up: action 121 .*Domination_Subdue" 3; x "btn R a 1" 0.8; x "btn R a 0" 0.5
    waitfor "^VERBS t=.* prompt up: action 16[34] .*Interrogate" 6 && { echo "$(date +%T) the choices up"; return 0; }; done
  echo "$(date +%T) no choices"; return 1; }
# the stab into the held orc (finish20's fan of 800 ms thrusts), then the kill's XP: where its popup is
stab_ask() { local k0 xy x y; k0=$(cnt 'kill: the blade in the interrogated orc')
  for xy in 25,-25 25,-45 10,-30 40,-30 10,-50 40,-50; do x=${xy%,*}; y=${xy#*,}
    w "hands test R $((x - 10)) $y -30 -15 0 0" 0.6; w "hands swing R $x $((y - 10)) 75 -15 0 0 800" 1.2
    [ "$(cnt 'kill: the blade in the interrogated orc')" -gt "$k0" ] && break; done
  grep -E '^VERBS t=.*(kill: the blade in the interrogated|the blade on the interrogated)' "$R/somvr.log" | tail -1 | cut -c1-220
  w "$REST" 0.3; sleep 1; dp "$1-finisher"; w "hands squeeze L 0" 0.5; w "$LREST" 0.4; }
# the XP popup after a kill: ahead, the head turned right, left (each a dump), back
xp_look() { local i; for i in 1 2 3 4 5 6 7 8; do dp "$1-xp$i"; sleep 0.6; done
  look 70 0; dp "$1-xp-right70"; look -50 0; dp "$1-xp-left50"; look 0 -30; dp "$1-xp-down30"; look 0 0; }
wraith() { echo "== $1: the wraith vision"; look 0 0; w "vbglow all 150" 0.3; x "btn L b 1" 0.4
  waitfor '^VISION t=.* the flag 0 -> 1' 3 || echo "$(date +%T) no vision flag line"; sleep 1.2
  dp "$1-level"; look 0 45; dp "$1-up45"; m "$1-up45" 0.1; look 0 75; dp "$1-up75"; look 60 35; dp "$1-right60up35"
  look 0 0; dp "$1-level2"; x "btn L b 0" 0.4; sleep 1.5; dp "$1-off"; w "vbglow all 0" 0.3; }
askr() { round "$1"; w "verbs new" 0.3; place_behind; sneak_to 0.6; grab || { endr; return; }
  w "vbglow all 200" 0.3; ask; sleep 1.0; dp "$1-choices"; look 0 40 0.6 1.0; dp "$1-choices-up40"; m "$1-up40" 0.1
  look 0 0 0.6 1.0; stab_ask "$1"; xp_look "$1"; w "vbglow all 0" 0.3; endr; }
# build 21's second boot: the interrogation's kill by the Touch's B (the prompt's own), the XP popup it gives
killb() { local k; for k in 1 2 3; do x "btn R b 1" 0.4; x "btn R b 0" 1.2
    waitfor "Talion attack .*Intrgtn_Kill|BODYMOMENT t=.* attack 'PC_Bip_Sync_Intrgtn_Kill" 3 && { echo "$(date +%T) the kill"; return 0; }; done
  echo "$(date +%T) no kill"; return 1; }
grab2() { grab && return 0; echo "$(date +%T) a second try"; w "practice remove" 2; w "practice back" 6; w "practice go" 7
  spawn_far; place_behind; sneak_to 0.6; grab; }
# an interrogation round: the look up 40 and 80 deg in the wraith world, the kill by B, the XP popup: ahead (8 dumps),
# the head 30 deg left (a popup holds), 70 right (it eases), back
xpr() { round "$1"; w "verbs new" 0.3; place_behind; sneak_to 0.6; grab2 || { endr; return; }
  w "vbglow all 200" 0.3; ask; sleep 1.0; dp "$1-choices"; look 0 40 0.6 1.0; dp "$1-up40"; look 0 80 0.6 1.0; dp "$1-up80"
  m "$1-up80" 0.1; look 0 0 0.6 1.0; w "hands squeeze L 0" 0.3; w "$LREST" 0.3; killb; sleep 2.5
  local i; for i in 1 2 3 4 5 6; do dp "$1-xp$i"; sleep 0.5; done
  look 30 0 0.8 1.0; dp "$1-xp-left30"; look -70 0 0.8 1.6; dp "$1-xp-right70"; sleep 0.5; dp "$1-xp-right70b"
  look 0 0 0.8 1.2; dp "$1-xp-back"; w "vbglow all 0" 0.3; endr; }
# the wraith vision's sky: Y held, dumps 45 and 80 deg up (NAME), Y let go
wv() { look 0 0; x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3; sleep 1.5
  dp "$1-up0"; look 0 45; dp "$1-up45"; look 0 80; dp "$1-up80"; look 0 0; x "btn L b 0" 0.4; sleep 2; }
# build 21's third boot: quick dumps (the XP shows ~4 s), consecutive frames (dump K)
dq() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.7
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/vw-steps.txt"; }
d3() { userin && bail "the user is in VR"; printf 'dump\ndump 2\ndump 3\n' > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 3
  grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -3 | grep -o '[0-9]*$' | while read f; do echo "$1 $f" | tee -a "$R/vw-steps.txt"; done; }
# the sky in three consecutive frames: the wraith vision 60 deg up, then the vision off
wt() { look 0 60; d3 wt-off; x "btn L b 1" 0.4; waitfor '^VISION t=.* the flag 0 -> 1' 3; sleep 2; d3 wt-vision; look 0 0
  x "btn L b 0" 0.4; sleep 2; }
# the XP with the head turned 60 deg right of the band's place before the kill (MODE band|gaze): where does it come up?
xp2() { local m=$1 n0; x "popups $m"; round "xp2$m"; w "verbs new" 0.3; place_behind; sneak_to 0.6; grab2 || { endr; return; }
  ask; sleep 1.0; look -60 0 0.6 1.0; w "hands squeeze L 0" 0.3; w "$LREST" 0.3
  n0=$(grep -c 'xr: popups: tile 2 placed' "$R/xr-layer.log"); killb
  if [ "$m" = gaze ]; then local i; for i in $(seq 1 40); do [ "$(grep -c 'xr: popups: tile 2 placed' "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.2; done
    echo "$(date +%T) $(grep 'xr: popups: tile 2 placed' "$R/xr-layer.log" | tail -1 | cut -c1-200)"; sleep 0.3
  else sleep 3.2; fi
  dq "xp2$m-right60"; look -25 0 0.3 0.4; dq "xp2$m-right25"; look 0 0 0.3 0.6; dq "xp2$m-back0"; sleep 0.3; dq "xp2$m-back0b"
  w "vbglow all 0" 0.3; endr; }
# the XP with gaze, pictures timed to it: the head 60 deg away from the band's place, the kill, a dump as it comes up,
# the head back (60 deg from the popup: it eases to the new front), dumps during and after the ease (the release blink
# comes ~3.3 s after the kill)
xp3() { local n0 i; x "popups gaze"; round xp3; w "verbs new" 0.3; place_behind; sneak_to 0.6; grab2 || { endr; return; }
  ask; sleep 1.0; look -60 0 0.6 1.0; w "hands squeeze L 0" 0.3; w "$LREST" 0.3
  n0=$(grep -c 'xr: popups: tile 2 placed' "$R/xr-layer.log"); x "btn R b 1" 0.4; x "btn R b 0" 0.1
  for i in $(seq 1 60); do [ "$(grep -c 'xr: popups: tile 2 placed' "$R/xr-layer.log")" -gt "$n0" ] && break
    [ $((i % 15)) = 0 ] && { x "btn R b 1" 0.4; x "btn R b 0" 0.1; }; sleep 0.2; done
  grep 'xr: popups: tile 2 placed' "$R/xr-layer.log" | tail -1 | cut -c1-200
  dq xp3-up; look 0 0 0.3 0.05; sleep 0.15; dq xp3-turning; dq xp3-turned; dq xp3-turned2
  w "vbglow all 0" 0.3; endr; }
for s in $STEPS; do
  case $s in
  wraith) w "vbglow face game" 0.3; wraith wraith ;;
  wraithf) w "vbglow face fx ${VW_FACE:-pres_wraith_camera,pres_wraith_player}" 0.3; w "vbglow face hide" 0.3; wraith wraithf
    w "vbglow face game" 0.3 ;;
  ask) w "vbglow face game" 0.3; askr ask ;;
  askf) w "vbglow face fx ${VW_FACE:-pres_wraith_camera,pres_wraith_player}" 0.3; w "vbglow face hide" 0.3; askr askf
    w "vbglow face game" 0.3 ;;
  xpband) w "vbglow face game" 0.3; x "popups band"; xpr xpband ;;
  xpgaze) w "vbglow face fx ${VW_FACE:-pres_wraith_camera,pres_wraith_player}" 0.3; w "vbglow face hide" 0.3
    x "popups gaze"; xpr xpgaze; w "vbglow face game" 0.3 ;;
  wt) wt ;;
  xp2band) xp2 band ;;
  xp2gaze) xp2 gaze ;;
  xp3) xp3 ;;
  wv) w "vbglow face game" 0.3; wv wv-game
    w "vbglow face fx vm_interactiveobjects" 0.3; w "vbglow face hide" 0.3; wv wv-noio
    w "vbglow face fx vm_ping_wraith" 0.3; wv wv-noping
    w "vbglow face fx vm_ping_wraith,vm_interactiveobjects" 0.3; wv wv-noboth; w "vbglow face game" 0.3 ;;
  esac
done
w "vbglow all 0" 0.3
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
