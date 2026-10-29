#!/bin/bash
# finish22-drive.sh - finish20-drive.sh (build 20's end of an interrogation) with build 22's rounds (docs/re-xr-pipeline.md
# 93.3: after the finisher his body is put under the held view - no re-seat blink): blink / tp below; NOQUIT=1 leaves the
# game running at the end, NODRAW=1 skips the sword's draw (already in the hand: scripts/fixes22-drive.sh). Drives one
# headless boot (scripts/headless_vr.sh with scripts/headset-build22.env, SOMVR_HANDS_TEST=1, the layer's test file). Each round: the far unaware orc (practice far), the crouched sneak to the stand-off, the left hand's stealth grab
# by its back (stealth19b's pose G1), the Touch's A = the interrogation, then FD_STEPS, in order:
#   old     build 19b's code (`verbs old`): the trigger = Interrogate, the intel menu left by B (its read of Talion's
#           attack in the pause is the interrogation's: the user's case), the Touch's B = Eliminate (the game's
#           finisher), the stick's walk - the view stays on the held point (the user's stuck movement); then `verbs
#           new`: it lets go
#   new     build 20's code: the same, the finisher by the intel menu's X (the pad's continue; run 113444) when it
#           takes, else by B: the hold ends with the finisher, the stick's walk takes the view along
#   blade   the sneak from the spawn (10 m: SOMVR_PRACTICE_FAR_CM) unaware all the way, the grab, A, the sword's stab
#           into the held orc = its Eliminate (the game's finisher under the held view; the body walks back), the walk
#   blink   (build 22's env, `bmreturn off` = build 21) the blade round: the stab = its Eliminate, the game's finisher
#           carries Talion off the held view; the hold's end = the re-seat's blink (`released by a blink`)
#   tp      the same with `bmreturn tp` (SOMVR_BM_RETURN_TP): his body put under the held view, no blink
#   back    the grab, A, the trigger = Interrogate, the intel menu left by our Back (SOMVR_VERBS_INTEL_BACK_S; else the
#           Touch's B): the game goes back to the held orc and its choices; the sword's stab = its Eliminate
# FD_PLACE (cm, default 220; 0 = the sneak from the spawn) puts Talion behind the far orc first (`verbs behind`) in the
# old and new rounds. FD_RUN=RUN drives a running boot. Lines to read: VERBS / BODYMOMENT / STEALTH / LOCO, the marks'
# state.txt (Talion's root against the view's head), fd-steps.txt (the dumps' frames for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${FD_STEPS:-old blade back}
T0=$(date +%s)
R=${FD_RUN:-}
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
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest
READY2="hands test R 8 -18 -40 0 0 0"    # the sword hand drawn back by the chest, the blade forward
THRUST="hands swing R 8 -14 60 0 0 0 250"
THRUST_LOW="hands swing R 4 -40 50 -30 0 0 250"   # ... down at the held orc (the blade pitched 30 deg down)
THRUST_FAR="hands swing R 6 -24 75 -15 0 0 250"   # ... farther out (the view eased 35 cm back from its head)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/fd-steps.txt"; }
spawn_far() { w "practice far infantry" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED\|refused'; then
    echo "$(date +%T) the far spawn failed: back to the practice area's start"; w "practice back" 6; w "practice go" 7; w "practice far infantry" 3
  fi; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6
[ -n "$NODRAW" ] || draw_slot; w "$REST" 0.6; w "front keep 0" 0.3
noticed() { [ "$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log")" -gt "$N0" ]; }
hold() { w "crouch hold 9000" 0.3; }
NR=0
reset() { NR=$((NR + 1)); [ "$NR" -gt 1 ] && { w "practice back" 6; w "practice go" 7; }; }
round() { echo "== $1"; reset; N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); spawn_far; m "$1-start" 0.1; }
place_behind() { [ "${FD_PLACE:-220}" -gt 0 ] && { w "verbs behind ${FD_PLACE:-220}" 0.8; w "execute face" 1.0; }; }
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
  echo "$(date +%T) $(grep -E '^STEALTH t=.* stand-off: ' "$R/somvr.log" | tail -2 | cut -c1-200 | tr '\n' '|')"
  sleep 1.5; w "stealth chase 0" 0.3; sleep 1.0; }
# the grab: pose G1 (the hand on its lower back, leaned in 30 cm, ducked 50), squeezed and held
grab() { local b g0; b=$(bearing); echo "bearing $b"; head_at 50 30 "$b"; w "hands test L -10 -6 62 180 -90 0" 0.8
  g0=$(cnt '^VERBS t=.* grab: the left hand'); w "hands squeeze L 1" 0.8
  if [ "$(cnt '^VERBS t=.* grab: the left hand')" -le "$g0" ]; then   # the pose missed: the palm put at its head
    w "hands squeeze L 0" 0.8; w "verbs aim head" 0.8; w "hands squeeze L 1" 0.3; waitfor '^VERBS t=.* grab: the left hand' 3; fi
  [ "$(cnt '^VERBS t=.* grab: the left hand')" -gt "$g0" ] || { echo "$(date +%T) no grab"; w "verbs aim off" 0.3; return 1; }
  grep -E '^VERBS t=.* grab: the left hand' "$R/somvr.log" | tail -1 | cut -c1-200; }
# the interrogation: the Touch's A (the prompt's own letter), its two choices up
ask() { local i; for i in 1 2 3; do
    waitfor "^VERBS t=.* prompt up: action 121 .*Domination_Subdue" 3; x "btn R a 1" 0.8; x "btn R a 0" 0.5
    waitfor "^VERBS t=.* prompt up: action 16[34] .*Interrogate" 6 && { echo "$(date +%T) the choices up"; return 0; }; done
  echo "$(date +%T) no choices"; return 1; }
# the gap: Talion's root against the view's head in the last mark (cm, horizontal)
gap() { local f; f=$(ls -dt "$R"/marks/*/ 2>/dev/null | head -1)/state.txt
  awk '/^talion root/{rx=$3; rz=$5} /^head /{hx=$2; hz=$4} END{printf "gap %.0f cm (root %s,%s head %s,%s)\n", sqrt((rx-hx)^2+(rz-hz)^2), rx, rz, hx, hz}' "$f" | tee -a "$R/fd-steps.txt"; }
ctx() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | grep -oE 'ctx=[a-z]+'; }
# the trigger = Interrogate; the intel menu (the game paused) left by B (the pad's Back), else its Exit
interrogate() { local t0=$(date +%s); w "$LREST" 0.3; w "hands trigger L 1" 0.4; w "hands trigger L -1" 0.5
  waitfor '^SESSION ui .*ctx=menu' 6 && echo "$(date +%T) the intel menu" || echo "$(date +%T) no intel menu"
  sleep 2; dp "$1-intel"
  if [ "${2:-wait}" = back ]; then
    for k in 1 2 3; do [ "$(ctx)" = ctx=world ] && break; x "btn R b 1" 0.4; x "btn R b 0" 1.5; done
  elif [ "$2" = auto ]; then waitfor '^SESSION ui .*ctx=world' 14 || interrogate_b
  elif [ "$2" = x ]; then x "btn L a 1" 0.8; x "btn L a 0" 2; [ "$(ctx)" = ctx=world ] || interrogate_b
  else waitfor '^SESSION ui .*ctx=world' 30; fi
  echo "$(date +%T) $(ctx) ($(( $(date +%s) - t0 )) s from the trigger)"; }
# the stab into the held orc: three thrusts at most, until the blade's kill
# (slow thrusts: the headless 16-20 Hz frames sweep a 250 ms thrust in 4 jumps - no contact; run 120916 passed 49 cm
# from the held orc's axis, its radius 35) - a fan of 800 ms thrusts from the chest, ahead and down at the held orc
stab_ask() { local k0 xy x y; k0=$(cnt 'kill: the blade in the interrogated orc')
  for xy in 25,-25 25,-45 10,-30 40,-30 10,-50 40,-50; do x=${xy%,*}; y=${xy#*,}   # (run 120916: the held orc stands right of the view's centre)
    w "hands test R $((x - 10)) $y -30 -15 0 0" 0.6; w "hands swing R $x $((y - 10)) 75 -15 0 0 800" 1.2; dp "$1-stab"
    [ "$(cnt 'kill: the blade in the interrogated orc')" -gt "$k0" ] && break; done
  grep -E '^VERBS t=.*(kill: the blade in the interrogated|the blade on the interrogated)' "$R/somvr.log" | tail -1 | cut -c1-220
  m "$1-stab" 0.01; w "$REST" 0.3; dp "$1-finisher1"; sleep 1; dp "$1-finisher2"; sleep 1; dp "$1-finisher3"
  w "hands squeeze L 0" 0.5; w "$LREST" 0.4; sleep 6; dp "$1-killed"; }
walk() { w "stick 0 1 0 ${1:-2}" "${1:-2}"; sleep 0.8; }
interrogate_b() { local k; for k in 1 2 3; do [ "$(ctx)" = ctx=world ] && break; x "btn R b 1" 0.4; x "btn R b 0" 1.5; done; }
# after the finisher: the hold's end, the walk; the gap before and after
after_finisher() { waitfor "Talion attack .*Intrgtn_Kill|BODYMOMENT t=.* attack 'PC_Bip_Sync_Intrgtn_Kill" 4 && echo "$(date +%T) the game's finisher"
  dp "$1-finisher"; sleep 8; dp "$1-after"; m "$1-after" 0.1; gap; walk 2; m "$1-walked" 0.1; gap; }
for s in $STEPS; do
  case $s in
  old)
    round old; w "verbs old" 0.3; place_behind; sneak_to 0.6; grab || { endr; continue; }; ask || { endr; continue; }
    dp old-choices; interrogate old back; sleep 1.5; m old-held 0.1
    x "btn R b 1" 0.4; x "btn R b 0" 0.5; w "hands squeeze L 0" 0.4; w "$LREST" 0.3   # the Touch's B = Eliminate (19b)
    after_finisher old
    w "verbs new" 0.3; sleep 2; m old-then-new 0.1; gap; walk 1.5; m old-then-new-walked 0.1; gap; endr ;;
  new)
    round new; w "verbs new" 0.3; place_behind; sneak_to 0.6; grab || { endr; continue; }; ask || { endr; continue; }
    dp new-choices; w "verbs old" 0.3; interrogate new x; w "verbs new" 0.3; w "hands squeeze L 0" 0.4; w "$LREST" 0.3
    after_finisher new; endr ;;
  blade)
    round blade; w "verbs new" 0.3; sneak_to 0.6; noticed && echo "$(date +%T) blade: noticed on the way"
    grab || { endr; continue; }; ask; sleep 1.5; dp blade-choices
    stab_ask blade; m blade-after 0.1; gap; walk 2; m blade-walked 0.1; gap; endr ;;
  blink|tp)
    round "$s"; w "verbs new" 0.3; [ "$s" = blink ] && w "bmreturn off" 0.3 || w "bmreturn tp" 0.3
    place_behind; sneak_to 0.6; noticed && echo "$(date +%T) $s: noticed on the way"
    grab || { endr; continue; }; ask; sleep 1.5; dp "$s-choices"
    stab_ask "$s"; waitfor '^BODYMOMENT t=.* released by' 12; m "$s-after" 0.1; gap
    grep -E '^BODYMOMENT t=.* (the hold ends|the return|released by|no Teleport)' "$R/somvr.log" | tail -4 | cut -c1-300
    walk 2; m "$s-walked" 0.1; gap; w "bmreturn tp" 0.3; endr ;;
  back)
    round back; w "verbs new" 0.3; place_behind; sneak_to 0.6; grab || { endr; continue; }; ask || { endr; continue; }
    interrogate back auto; sleep 1.5; dp back-held; m back-held 0.1
    stab_ask back; m back-after 0.1; gap; walk 2; m back-walked 0.1; gap; endr ;;
  esac
done
w "verbs status" 1
[ -n "$NOQUIT" ] && { echo "finish22 done $(date +%T) (NOQUIT)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
