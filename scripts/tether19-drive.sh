#!/bin/bash
# tether19-drive.sh - build 19 (the tether19 helper, re-xr-pipeline.md 69.5-69.7): SOMVR_HEAD_TETHER=on's proof in two
# headless boots, chaining existing drivers on one running boot (each attached with its *_RUN, left running with its
# *_NOQUIT; this script quits at the end). TT19 (default edge):
#   edge  the true positives: tether18-drive.sh PLAN="edge6 flat wall" (mark 6's plateau edge: the stick into it, the
#         test head 100 cm out over it; a step on flat ground; mark 4's face), then `practice back` and
#         climb13-drive.sh CLIMB_PLAN=top (a climb up the meadow bank, the fling over its top, standing, walking on)
#   play  tether18-drive.sh PLAN="step6 flat wall" (the true positives with the final code: the head out over mark 6's
#         edge, mark 4's face; a step on flat ground), then `practice go` to the meadow's arrival and ordinary play,
#         no fade wanted: wstep18-drive.sh WS_PLAN="flat orc" (the wraith step), parry18-drive.sh
#         P18_STEPS="base dodge" (cuts, its blocks, the walks to it, the dodge's leans 45 cm aside at its red blows),
#         strike18-drive.sh SG_STEPS=walk SG_AHEAD_OK=1 (the strike, the stick walk after it)
#   fight strike18-drive.sh SG_STEPS=walk (the strike; the boot needs SOMVR_PRACTICE_AHEAD_CM=2000 for it), then
#         crowd19-drive.sh CROWD19_STEPS="guard dodge" (the group of 4: the guard's blocks and parries, the A dodge)
# Boot (sandbox off), SOMVR_HEAD_TETHER=on in place of the env file's off:
#   HEADLESS_HOLD=1200 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build19.env | grep -v '^SOMVR_HEAD_TETHER=' |
#     tr '\n' ' ') SOMVR_HEAD_TETHER=on SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# with this script at the same time. Each driver's output: RUN/tt19-<driver>.txt; the counts at the end (fade-ups by
# `TETHER t=.* the view fades`, TETHER END). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
S=$(cd "$(dirname "$0")" && pwd -P)
WT=${TT19_WT:-$(basename "$(cd "$S/../.." && pwd -P)")}
PLAN=${TT19:-edge}
MEADOW=${TT19_MEADOW:-32500 2100 -24800} # the practice meadow's arrival (play: the drivers' orcs spawn only in its area)
T0=$(date +%s)
R=${TT19_RUN:-}
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
fades() { grep -cE '^TETHER t=.* the view fades' "$L"; }
step() { local name=$1 f0; shift; userin && bail "the user is in VR"; [ -f "$R/game.exit" ] && bail "the game is gone"
  f0=$(fades); echo "$(date +%T) == $name ($*)"
  env "$@" bash "$S/$name-drive.sh" > "$R/tt19-$name.txt" 2>&1
  echo "$(date +%T) $name done (rc $?, its last line: $(tail -1 "$R/tt19-$name.txt" | cut -c1-120)); tether fade-ups in it $(( $(fades) - f0 ))"
  grep -E '^TETHER t=' "$L" | tail -3 | cut -c1-220; }
if [ "$PLAN" = edge ]; then
  step tether18 PIN_RUN="$R" TT_NOQUIT=1 PLAN="edge6 flat wall"
  printf '%s' "practice back" > "$R/somvr.trigger"; echo "$(date +%T) word: practice back"; sleep 6
  step climb13 CLIMB_RUN="$R" CLIMB_NOQUIT=1 CLIMB_PLAN=top
elif [ "$PLAN" = play ]; then
  step tether18 PIN_RUN="$R" TT_NOQUIT=1 PLAN="step6 flat wall"
  printf '%s' "practice go $MEADOW" > "$R/somvr.trigger"; echo "$(date +%T) word: practice go $MEADOW"; sleep 6
  F0=$(fades); echo "$(date +%T) ordinary play from here (fade-ups so far $F0)"
  step wstep18 WS_RUN="$R" WS_NOQUIT=1 WS_PLAN="flat orc"
  step parry18 P18_RUN="$R" P18_NOQUIT=1 P18_STEPS="base dodge"
  step strike18 SG_RUN="$R" SG_NOQUIT=1 SG_STEPS=walk SG_AHEAD_OK=1
elif [ "$PLAN" = fight ]; then
  F0=$(fades); echo "$(date +%T) ordinary play from here (fade-ups so far $F0)"
  step strike18 SG_RUN="$R" SG_NOQUIT=1 SG_STEPS=walk
  printf '%s' "practice go $MEADOW" > "$R/somvr.trigger"; echo "$(date +%T) word: practice go $MEADOW"; sleep 6
  step crowd19 CROWD19_RUN="$R" CROWD19_NOQUIT=1 CROWD19_STEPS="guard dodge" CROWD19_GUARD_S=45 CROWD19_DODGE_S=40
fi
sleep 2
[ -n "$F0" ] && echo "TT19 $PLAN: fade-ups in ordinary play $(( $(fades) - F0 ))"
echo "TT19 $PLAN: tether fade-ups $(fades), to black $(grep -cE '^TETHER t=.*: to black' "$L"), back $(grep -cE '^TETHER t=.* the view back to' "$L"), not-measured lines $(grep -cE '^TETHER t=.* not measured' "$L")"
echo quit >> "$R/cmd"; echo "quit $(date +%T)"
sleep 20; grep -E '^TETHER END|^FOLLOW t=' "$L" | tail -2 | cut -c1-400
