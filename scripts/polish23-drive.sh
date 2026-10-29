#!/bin/bash
# polish23-drive.sh - build 23's polish round (som-b23-polish; docs/re-xr-pipeline.md 95, handoff/HANDOFF-captain.md and
# HANDOFF-hud.md "build 23"): the captain's introduction card at the gaze, the grab cue on a captain. Drives one headless
# boot (scripts/headless_vr.sh with scripts/headset-build23.env, SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt);
# captain22-drive.sh's helpers. PL_STEPS, in this order (default "intro rules grabfull low grablow kill"):
#   intro    `practice captain` (the save's next captain, stakes off), the head on him, his presentation awaited: dumps
#            at +1, +2, +3 and +4.5 s (the card: in the subtitles' popup at the gaze, one piece), the STORY subtitle lines
#            and the layer's popup lines while it runs
#   rules    `cue rules` near him at full health: the data behind the grab's end (RE: the attacks' predicates, Talion's
#            TraitReactionNodes, the attempt's links, his traits; the game's pick from each node)
#   grabfull the left palm at his collar (the cue's answer for him logged), a squeeze: the game's grab - resisted at full
#            health (`PC_Grab_Into_F_Resistant`) - so no cue must have shown
#   low      `captain hp` 20 % of his max, `cue rules` again
#   grablow  the palm at his collar again: the cue's answer (it holds: the cue), a squeeze: `PC_Grab_Into_F`, the prompts
#   kill     `captain hp 45`, the sword drawn, edge cuts until he dies; dumps for the kill's XP popup (+0.5..+4 s)
# PL_RUN=RUN drives a running boot. pl-steps.txt: PASS/FAIL/NOTE and the dumps' frames (xr_compose.py -f FRAME).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${PL_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${PL_STEPS:-intro rules grabfull low grablow kill}
T0=$(date +%s)
R=${PL_RUN:-}
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
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
nsince() { since "$1" "$2" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/pl-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/pl-steps.txt"; }
note() { echo "$(date +%T) NOTE $*" | tee -a "$R/pl-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/pl-steps.txt"; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest, low (out of every orc's reach)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
LA="hands test R 30 -10 45 0 -35 0"; LMS=${KP_LMS:-1000}
LUNGE_A="hands test R 40 -8 35 0 -90 0"
slash() { if [ $(( $1 % 2 )) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 $LMS" 0.6; else w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.6; fi; }
heavy() { w "$LUNGE_A" 0.5; w "hands swing R -20 -18 62 0 90 0 300" 1.0; w "hands swing R 40 -8 35 0 -90 0 900" 1.0; }
RZ=90   # the edge cut (combat21's): the blade's edge leads (a flat blade is a light hit since build 20's stroke)
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
head_to() { t "head 0 0 0 yaw $1 pitch $2 s ${3:-0.3}"; sleep 0.3; }
HY=0; HP=0
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p'); [ -n "$OB_Y" ]; }
look() { orc0 || return 1; HY=$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}'); HP=$(awk -v a="$HP" -v b="$OB_P" -v u="${1:-0}" 'BEGIN{v=a+b+u; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}'); head_to "$HY" "$HP" 0.25; }
hp() { grep -E '^CAPTAIN t=.* his health ' "$L" | tail -1 | sed -nE 's/.*his health [0-9.-]+ -> ([0-9.]+).*/\1/p'; }
hp0() { grep -E '^CAPTAIN t=.* spawn .* ok:' "$L" | tail -1 | sed -nE 's/.* health ([0-9.]+);.*/\1/p'; }
dead() { [ "$(cnt '^CAPTAIN t=.* he died')" -gt "${DEAD0:-0}" ]; }
ready() { grep -E '^EXECUTE ready (off )?t=' "$L" | tail -1 | grep -q '^EXECUTE ready t='; }
pres_wait() { # $1 = seconds: a presentation's start (CAPTAIN line); marks and a dump inside it
  local n0 k; n0=$(cnt '^CAPTAIN t=.* a presentation starts'); for k in $(seq 1 $(( $1 * 2 ))); do
    userin && bail "the user is in VR"; [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$n0" ] && break; sleep 0.5; done
  [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$n0" ] || return 1
  grep -E '^CAPTAIN t=.* a presentation starts' "$L" | tail -1 | cut -c1-400; return 0; }

# 0. the controls card away, the hands at rest, the sword on the back
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG captain|CONFIG verbs_cue|CAPTAIN site .* MISMATCH)' "$L" | cut -c1-300
grep -E 'xr: (prompts|subtitles|popups):' "$R/xr-layer.log" | head -4 | cut -c1-300
m pl-start 0.2

if has intro; then
  echo "== intro"
  w "captain status" 1.5; grep -cE '^CAPTAIN member' "$L"
  w "captain stakes off" 0.3; w "practice remove" 1.5
  P0=$(cnt '^CAPTAIN t=.* a presentation starts'); n0=$(lines); x0=$(wc -l < "$R/xr-layer.log"); w "practice captain" 0.3
  for k in $(seq 1 80); do [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$P0" ] && break; sleep 0.5; done
  if [ "$(cnt '^CAPTAIN t=.* a presentation starts')" -gt "$P0" ]; then
    sleep 0.6; d "intro-1"; sleep 0.6; d "intro-2"; m pl-intro 0; d "intro-3"; sleep 0.8; d "intro-4"
  fi
  l=$(since "$n0" '^CAPTAIN t=.* spawn ' | head -1); echo "$l" | cut -c1-400
  case $l in *" ok:"*) pass "intro: the practice's Captain came ($(echo "$l" | grep -oE 'member 0x[0-9a-f]+ [^;]*' | cut -c1-120))" ;;
    *) fail "intro: no captain: $(echo "$l" | cut -c1-200)"; bail "no captain" ;; esac
  waitfor '^CAPTAIN t=.* the presentation is over' 20; grep -E '^CAPTAIN t=.* (a presentation starts|the presentation is over)' "$L" | tail -2 | cut -c1-260
  echo "STORY lines in the intro:"; since "$n0" '^STORY t=.* subtitle' | head -8 | cut -c1-260
  echo "the layer's popups in the intro:"; tail -n +"$((x0 + 1))" "$R/xr-layer.log" | grep -E 'popups: tile [0-9] placed' | head -10 | cut -c40-300
  HY=0; HP=0; head_to 0 0; look 0; m pl-spawned 0.3
fi

chase_near() { w "execute chase on" 0.3; sleep "${1:-3}"; w "execute chase off" 0.3; }
rules() { local n0; n0=$(lines); w "cue rules" 0.5; waitfor '^VERBS rules END' 10 || note "rules: no END"
  echo "rules ($1): $(nsince "$n0" '^VERBS rules') lines"; since "$n0" "^VERBS rules .*(the game's pick|TraitReactionNodes|orc 0x)" | cut -c1-330; }
grab_try() { # $1 = the label: the palm at his collar (the cue's answer for him), the grip closed 3 s, Talion's attacks
  local n0; w "execute face" 0.4; HY=0; HP=0; head_to 0 0; look -10; n0=$(lines); w "verbs aim head" 0.2; sleep 1.8
  echo "$1: the cue: $(since "$n0" '^VERBS cue t=' | grep -E 'in reach|captain|on:|off:' | head -4 | cut -c1-330)"
  CUE_ON=$(nsince "$n0" '^VERBS cue t=.* on:'); CUE_YES=$(nsince "$n0" '^VERBS cue t=.*would grab it'); CUE_NO=$(nsince "$n0" '^VERBS cue t=.*would not grab it')
  n0=$(lines); w "hands squeeze L 1" 3.0
  GRAB=$(since "$n0" "^VERBS t=.* Talion attack 'PC_Grab_Into[^']*'" | grep -oE "attack 'PC_Grab_Into[^']*'" | head -1)
  echo "$(date +%T) $1: cue on $CUE_ON yes $CUE_YES no $CUE_NO; Talion's attacks $(since "$n0" "^VERBS t=.* Talion attack 'PC_" | grep -oE "attack 'PC_[^']*'" | uniq | tr '\n' ' '); prompts $(since "$n0" '^VERBS t=.* prompt up' | grep -oE "'(UI|Combat|Domination)_[^']*'" | sort -u | tr '\n' ' ')"
  m "pl-$1" 0
  w "hands squeeze L 0" 2.0; w "verbs aim off" 0.2; w "$LREST" 1.5; }
if has rules; then echo "== rules"; chase_near 3; rules "full"; fi
if has grabfull; then
  echo "== grabfull"; chase_near 2; grab_try "grab-full"
  case "$GRAB" in *Resistant*|*Immune*)
      [ "$CUE_ON" = 0 ] && pass "grab-full: thrown off ($GRAB) and no cue (VERBS cue END: the ends asked)" || fail "grab-full: thrown off ($GRAB) but the cue on $CUE_ON / yes $CUE_YES" ;;
    *"PC_Grab_Into"*) [ "$CUE_ON" -ge 1 ] && pass "grab-full: held ($GRAB) and the cue" || fail "grab-full: held ($GRAB) without the cue" ;;
    *) note "grab-full: no grab's end seen (cue on $CUE_ON yes $CUE_YES no $CUE_NO)" ;; esac
fi
if has low; then
  echo "== low"; H=$(hp0); [ -n "$H" ] || H=976; v=$(awk -v h="$H" 'BEGIN{printf "%d", h*0.2}'); w "captain hp $v" 0.8
  chase_near 2; rules "20%"
fi
if has grablow; then
  echo "== grablow"; chase_near 1; grab_try "grab-low"
  case "$GRAB" in *Resistant*|*Immune*)
      [ "$CUE_ON" = 0 ] && pass "grab-low: thrown off ($GRAB) and no cue" || fail "grab-low: thrown off ($GRAB) but the cue on $CUE_ON" ;;
    *"PC_Grab_Into"*) [ "$CUE_ON" -ge 1 ] && pass "grab-low: held ($GRAB) and the cue" || fail "grab-low: held ($GRAB) without the cue (yes $CUE_YES no $CUE_NO)" ;;
    *) note "grab-low: no grab's end seen (cue on $CUE_ON yes $CUE_YES no $CUE_NO)" ;; esac
fi
if has kill; then
  echo "== kill"
  DEAD0=$(cnt '^CAPTAIN t=.* he died'); drawn || draw_slot; w "$LA" 0.4
  w "captain hp 45" 0.4; w "execute chase on" 0.4; x0=$(wc -l < "$R/xr-layer.log")
  for k in $(seq 1 30); do dead && break; ecut; done
  w "execute chase off" 0.3
  if dead; then pass "kill: he died ($(grep -E '^CAPTAIN t=.* he died' "$L" | tail -1 | grep -oE 'IsDead [0-9], KilledByPlayer [0-9]'))"
    sleep 0.3; d "kill-1"; sleep 0.8; d "kill-2"; sleep 0.8; d "kill-3"; m pl-dead 0; d "kill-4"
    echo "the layer's popups after the kill:"; tail -n +"$((x0 + 1))" "$R/xr-layer.log" | grep -E 'popups: tile [0-9] placed' | head -8 | cut -c40-300
  else fail "kill: alive after 30 cuts (health $(hp))"; fi
fi
echo "$(date +%T) done"; grep -E '^VERBS cue END|^CAPTAIN END' "$L" | cut -c1-400
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
