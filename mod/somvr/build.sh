#!/usr/bin/env bash
# build.sh - Shadow of Mordor VR: the head-tracking / per-eye pipeline (docs/re-xr-pipeline.md).
#   ./build.sh            build build/somvr.so (LD_PRELOAD) and vklayer/build/libVkLayer_somvr_openxr.so
#   ./build.sh --check    + the unit tests (pose math against the run-1 samples, the somvr_shared ABI, the XR views
#                         provider against a mock runtime, the combat geometry/speed/tiers, the controller controls,
#                         the grip locate against a mock xrLocateSpace, the arm IK on run 3's skeleton, the laser
#                         pointer's geometry and state machine, SOMVR_BODY=hands' keep set and skin edit, the sword
#                         angle and SOMVR_TWOHAND on run 3's skeleton, SOMVR_FINGERS' joints, targets, ease and
#                         pose on run 3's skeleton, the head bone subtree and collapse on run 3's
#                         skeleton, the third-person fallback's detector, switch and fade, the comfort module:
#                         speeds, snap blink, seated height, vignette; SOMVR_CLIMB's grab, delta integration,
#                         clamp, release, stand-downs, timeout and top; SOMVR_BOW's ray math and gesture state
#                         machine; the VR menu's settings file, items, layout, picture, font, palm gesture and
#                         panel pose; SOMVR_LOCO's 1:1 core and SOMVR_ANCHOR=root's anchor; SOMVR_THREAT's
#                         angles, levels, cue selection, projectile test, mapping, quads and texture; SOMVR_BODYMOMENTS'
#                         names, policy, holds, re-seat, dim, window and yaw; SOMVR_SLOTS'
#                         heading, torso yaw, slots and gesture state machine; SOMVR_DEFENCE's guard, parry zone,
#                         verdicts, dodge, two attackers, the press, arrows, haptics; SOMVR_FINISH's
#                         names, zones, judges, glow, latch and claim; SOMVR_HIDE_HEAD=parts' slot list, node set,
#                         per-draw rule and game-to-render ring; SOMVR_PRACTICE's words, trip (the blink there
#                         and back), keep-clear rule, gate, spawn spot and facing; build 6's menu board, wrist minimap
#                         and touch (somvr_menus.c); build 14's front-first veto, damage scale and AI camera copy
#                         (somvr_front.c; its game side somvr_front_game.c, SOMVR_FRONT)),
#                         check_sites.py
#                         on the game
#                         binary (read-only), the inert
#                         tests, selftest.c (the hooks against stand-ins), and vklayer/build.sh --check (layer tests)
# Headers: ShadowOfMordor/third_party (git-ignored), see vklayer/build.sh. Nothing here launches the game or a runtime.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
TP=$(cd "$D/../../third_party" 2>/dev/null && pwd) || { echo "no ShadowOfMordor/third_party (run scripts/fetch_third_party.sh)"; exit 2; }
XRH=$TP/OpenXR-SDK-1.1.61/include
[ -f "$XRH/openxr/openxr.h" ] || { echo "OpenXR headers missing in $TP (run scripts/fetch_third_party.sh)"; exit 2; }
CF=(-std=gnu11 -O2 -g -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror -I"$XRH")
CORE=("$D/somvr_pose.c" "$D/somvr_eyes.c" "$D/somvr_views.c" "$D/somvr_combat.c" "$D/somvr_ctl.c" "$D/somvr_hands.c"
      "$D/somvr_arms.c" "$D/somvr_pointer.c" "$D/somvr_headbone.c" "$D/somvr_fallback.c" "$D/somvr_comfort.c"
      "$D/somvr_climb.c" "$D/somvr_bow.c" "$D/somvr_vrmenu.c" "$D/somvr_font.c"
      "$D/somvr_loco.c" "$D/somvr_threat.c" "$D/somvr_bodymoments.c" "$D/somvr_slots.c" "$D/somvr_defence.c"
      "$D/somvr_step.c" "$D/somvr_finish.c" "$D/somvr_headview.c" "$D/somvr_headparts.c" "$D/somvr_practice.c"
      "$D/somvr_session.c" "$D/somvr_body.c" "$D/somvr_fingers.c" "$D/somvr_size.c" "$D/somvr_vrhands.c"
      "$D/somvr_strike.c" "$D/somvr_gaze.c" "$D/somvr_execute.c" "$D/somvr_surface.c" "$D/somvr_verbs.c" "$D/somvr_crouch.c" "$D/somvr_front.c"
      "$D/somvr_objective.c" "$D/somvr_smooth.c" "$D/somvr_vision.c" "$D/somvr_wstep.c" "$D/somvr_stealth.c"
      "$D/somvr_screens.c" "$D/somvr_beasts.c" "$D/somvr_captain.c" "$D/somvr_jump.c" "$D/somvr_above.c" "$D/somvr_ride.c"
      "$D/somvr_map.c" "$D/somvr_abil.c")
mkdir -p "$D/build/tests"
CHECK=0
for a in "$@"; do case $a in --check) CHECK=1 ;; *) echo "unknown option $a"; exit 2 ;; esac; done

if [ -f "$D/somvr.c" ]; then
  gcc "${CF[@]}" -Wno-missing-prototypes -fPIC -shared -fvisibility=hidden -Wl,-z,defs \
      -o "$D/build/somvr.so" "$D/somvr.c" "${CORE[@]}" -lpthread -lm
  echo "built $D/build/somvr.so ($(sha256sum "$D/build/somvr.so" | cut -c1-16))"
fi
[ -x "$D/vklayer/build.sh" ] && "$D/vklayer/build.sh"
[ $CHECK = 1 ] || exit 0

echo "== unit tests"
gcc "${CF[@]}" -o "$D/build/tests/test_pose" "$D/tests/test_pose.c" "$D/somvr_pose.c" "$D/somvr_eyes.c" -lm
"$D/build/tests/test_pose"
gcc "${CF[@]}" -o "$D/build/tests/test_shared" "$D/tests/test_shared.c" -lpthread
"$D/build/tests/test_shared"
gcc "${CF[@]}" -o "$D/build/tests/test_input" "$D/tests/test_input.c" -lpthread
"$D/build/tests/test_input"
gcc "${CF[@]}" -o "$D/build/tests/test_views" "$D/tests/test_views.c" "$D/somvr_views.c" "$D/somvr_pose.c" -lpthread -lm
"$D/build/tests/test_views"
gcc "${CF[@]}" -o "$D/build/tests/test_combat" "$D/tests/test_combat.c" "$D/somvr_combat.c" -lm
"$D/build/tests/test_combat"
gcc "${CF[@]}" -o "$D/build/tests/test_ctl" "$D/tests/test_ctl.c" "$D/somvr_ctl.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_ctl"
gcc "${CF[@]}" -o "$D/build/tests/test_hands" "$D/tests/test_hands.c" "$D/somvr_hands.c" "$D/somvr_pose.c" "$D/somvr_eyes.c" \
    -lpthread -lm
"$D/build/tests/test_hands"
gcc "${CF[@]}" -o "$D/build/tests/test_smooth" "$D/tests/test_smooth.c" "$D/somvr_smooth.c" -lm
"$D/build/tests/test_smooth"
gcc "${CF[@]}" -o "$D/build/tests/test_arms" "$D/tests/test_arms.c" "$D/somvr_arms.c" "$D/somvr_combat.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_arms"
gcc "${CF[@]}" -o "$D/build/tests/test_pointer" "$D/tests/test_pointer.c" "$D/somvr_pointer.c" -lm
"$D/build/tests/test_pointer"
gcc "${CF[@]}" -o "$D/build/tests/test_body" "$D/tests/test_body.c" "$D/somvr_body.c" "$D/somvr_arms.c" "$D/somvr_combat.c" \
    "$D/somvr_pose.c" -lm
"$D/build/tests/test_body"
gcc "${CF[@]}" -o "$D/build/tests/test_fingers" "$D/tests/test_fingers.c" "$D/somvr_fingers.c" "$D/somvr_arms.c" \
    "$D/somvr_combat.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_fingers"
gcc "${CF[@]}" -o "$D/build/tests/test_headbone" "$D/tests/test_headbone.c" "$D/somvr_headbone.c" "$D/somvr_arms.c" \
    "$D/somvr_combat.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_headbone"
gcc "${CF[@]}" -o "$D/build/tests/test_fallback" "$D/tests/test_fallback.c" "$D/somvr_fallback.c" -lm
"$D/build/tests/test_fallback"
gcc "${CF[@]}" -o "$D/build/tests/test_comfort" "$D/tests/test_comfort.c" "$D/somvr_comfort.c" "$D/somvr_ctl.c" \
    "$D/somvr_eyes.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_comfort"
gcc "${CF[@]}" -o "$D/build/tests/test_climb" "$D/tests/test_climb.c" "$D/somvr_climb.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_climb"
gcc "${CF[@]}" -o "$D/build/tests/test_bow" "$D/tests/test_bow.c" "$D/somvr_bow.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_bow"
gcc "${CF[@]}" -o "$D/build/tests/test_vrmenu" "$D/tests/test_vrmenu.c" "$D/somvr_vrmenu.c" "$D/somvr_font.c" -lm
"$D/build/tests/test_vrmenu"
gcc "${CF[@]}" -o "$D/build/tests/test_captain" "$D/tests/test_captain.c" "$D/somvr_captain.c" -lm
"$D/build/tests/test_captain"
gcc "${CF[@]}" -o "$D/build/tests/test_loco" "$D/tests/test_loco.c" "$D/somvr_loco.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_loco"
gcc "${CF[@]}" -o "$D/build/tests/test_threat" "$D/tests/test_threat.c" "$D/somvr_threat.c" "$D/somvr_eyes.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_threat"
gcc "${CF[@]}" -o "$D/build/tests/test_bodymoments" "$D/tests/test_bodymoments.c" "$D/somvr_bodymoments.c" \
    "$D/somvr_fallback.c" "$D/somvr_loco.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_bodymoments"
gcc "${CF[@]}" -o "$D/build/tests/test_screens" "$D/tests/test_screens.c" "$D/somvr_screens.c" \
    "$D/somvr_bodymoments.c" "$D/somvr_fallback.c" "$D/somvr_loco.c" "$D/somvr_pose.c" -lm # build 22: death, Mouse1
"$D/build/tests/test_screens"
gcc "${CF[@]}" -o "$D/build/tests/test_map" "$D/tests/test_map.c" "$D/somvr_map.c" -lm # build 27: the map by the pointer
"$D/build/tests/test_map"
gcc "${CF[@]}" -o "$D/build/tests/test_slots" "$D/tests/test_slots.c" "$D/somvr_slots.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_slots"
gcc "${CF[@]}" -o "$D/build/tests/test_defence" "$D/tests/test_defence.c" "$D/somvr_defence.c" "$D/somvr_combat.c" -lm
"$D/build/tests/test_defence"
gcc "${CF[@]}" -o "$D/build/tests/test_step" "$D/tests/test_step.c" "$D/somvr_step.c" "$D/somvr_loco.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_step"
gcc "${CF[@]}" -o "$D/build/tests/test_wstep" "$D/tests/test_wstep.c" "$D/somvr_wstep.c" -lm # build 18: the wraith step
"$D/build/tests/test_wstep"
gcc "${CF[@]}" -o "$D/build/tests/test_jump" "$D/tests/test_jump.c" "$D/somvr_jump.c" "$D/somvr_pose.c" -lm # build 22: the jump, the vault
"$D/build/tests/test_jump"
gcc "${CF[@]}" -o "$D/build/tests/test_above" "$D/tests/test_above.c" "$D/somvr_above.c" -lm # build 25: from above
"$D/build/tests/test_above"
gcc "${CF[@]}" -o "$D/build/tests/test_abil" "$D/tests/test_abil.c" "$D/somvr_abil.c" -lm # build 27: the abilities
"$D/build/tests/test_abil"
gcc "${CF[@]}" -o "$D/build/tests/test_headview" "$D/tests/test_headview.c" "$D/somvr_headview.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_headview"
gcc "${CF[@]}" -o "$D/build/tests/test_headparts" "$D/tests/test_headparts.c" "$D/somvr_headparts.c" -lpthread -lm
"$D/build/tests/test_headparts"
gcc "${CF[@]}" -o "$D/build/tests/test_finish" "$D/tests/test_finish.c" "$D/somvr_finish.c" "$D/somvr_combat.c" "$D/somvr_pose.c" -lm
"$D/build/tests/test_finish"
gcc "${CF[@]}" -o "$D/build/tests/test_practice" "$D/tests/test_practice.c" "$D/somvr_practice.c" -lm
"$D/build/tests/test_practice"
gcc "${CF[@]}" -o "$D/build/tests/test_session" "$D/tests/test_session.c" "$D/somvr_session.c" -lm
"$D/build/tests/test_session"
gcc "${CF[@]}" -o "$D/build/tests/test_menus" "$D/tests/test_menus.c" "$D/somvr_menus.c" -lm # build 6: the boards, the watch, touch
"$D/build/tests/test_menus"
gcc "${CF[@]}" -o "$D/build/tests/test_size" "$D/tests/test_size.c" "$D/somvr_size.c" "$D/somvr_comfort.c" "$D/somvr_eyes.c" \
    "$D/somvr_hands.c" "$D/somvr_combat.c" "$D/somvr_arms.c" "$D/somvr_climb.c" "$D/somvr_threat.c" "$D/somvr_pose.c" \
    -lpthread -lm # the player size: the rig, the hands, the blade, the arms, the climb, the glows, the blink
"$D/build/tests/test_size"
python3 -I "$D/../../scripts/handmesh_gen.py" --check # build 10: somvr_handmesh.h is the generator's
gcc "${CF[@]}" -o "$D/build/tests/test_vrhands" "$D/tests/test_vrhands.c" "$D/somvr_vrhands.c" -lm # build 10: our VR hands
"$D/build/tests/test_vrhands"
gcc "${CF[@]}" -o "$D/build/tests/test_strike" "$D/tests/test_strike.c" "$D/somvr_strike.c" -lm # build 12: the wraith dash
"$D/build/tests/test_strike"
gcc "${CF[@]}" -o "$D/build/tests/test_gaze" "$D/tests/test_gaze.c" "$D/somvr_gaze.c" -lm # build 17: the Shadow Strike by gaze
"$D/build/tests/test_gaze"
gcc "${CF[@]}" -o "$D/build/tests/test_execute" "$D/tests/test_execute.c" "$D/somvr_execute.c" -lm # build 13: the execution
"$D/build/tests/test_execute"
gcc "${CF[@]}" -o "$D/build/tests/test_stealth" "$D/tests/test_stealth.c" "$D/somvr_stealth.c" "$D/somvr_combat.c" -lm # build 18: the stealth kill (20c: + the tiers)
"$D/build/tests/test_stealth"
gcc "${CF[@]}" -o "$D/build/tests/test_beasts" "$D/tests/test_beasts.c" "$D/somvr_beasts.c" -lm # build 22: the beasts
"$D/build/tests/test_beasts"
gcc "${CF[@]}" -o "$D/build/tests/test_ride" "$D/tests/test_ride.c" "$D/somvr_ride.c" -lm # build 26: riding, Seat mode
"$D/build/tests/test_ride"
gcc "${CF[@]}" -o "$D/build/tests/test_surface" "$D/tests/test_surface.c" "$D/somvr_surface.c" "$D/somvr_pose.c" -lm # build 13: hands at surfaces
"$D/build/tests/test_surface"
gcc "${CF[@]}" -o "$D/build/tests/test_verbs" "$D/tests/test_verbs.c" "$D/somvr_verbs.c" -lm # build 13: the Nemesis verbs
"$D/build/tests/test_verbs"
gcc "${CF[@]}" -o "$D/build/tests/test_crouch" "$D/tests/test_crouch.c" "$D/somvr_crouch.c" -lm # build 13: crouch by ducking
"$D/build/tests/test_crouch"
gcc "${CF[@]}" -o "$D/build/tests/test_vision" "$D/tests/test_vision.c" "$D/somvr_vision.c" "$D/somvr_session.c" \
    "$D/somvr_ctl.c" "$D/somvr_pose.c" -lm # build 16: SOMVR_VISION's wish and taps
"$D/build/tests/test_vision"
gcc "${CF[@]}" -o "$D/build/tests/test_front" "$D/tests/test_front.c" "$D/somvr_front.c" -lm # build 14: front-first enemies (SOMVR_FRONT)
"$D/build/tests/test_front"
gcc "${CF[@]}" -o "$D/build/tests/test_objective" "$D/tests/test_objective.c" "$D/somvr_objective.c" "$D/somvr_font.c" -lm # build 15: the objective marker
"$D/build/tests/test_objective"
[ -f "$D/somvr.c" ] || exit 0

echo "== patch sites (read-only, the game binary on disk)"
python3 -I "$D/check_sites.py" "$D/somvr.c" "${SOM_BIN:-$D/../../reference/bin/linux/ShadowOfMordor}"

echo "== inert outside the game"
T=$(mktemp -d)
SOMVR_LOG=$T/somvr.log SOMVR_STEREO=A LD_PRELOAD=$D/build/somvr.so /bin/true
SOMVR_LOG=$T/somvr.log SOMVR_STEREO=A LD_PRELOAD=$D/build/somvr.so python3 -I -c 'pass'
if [ -e "$T/somvr.log" ]; then echo "FAIL: the library wrote a log outside the game"; rm -rf -- "$T"; exit 1; fi
rm -rf -- "$T"
exp=$(nm -D --defined-only "$D/build/somvr.so" | awk '$2 ~ /[TtBbDd]/ { print $3 }' | sort | tr '\n' ' ')
[ "$exp" = "somvr_comfort somvr_hud somvr_input somvr_objective somvr_session somvr_shared somvr_threat somvr_vrhands somvr_vrmenu " ] ||
    { echo "FAIL: somvr.so exports: $exp (want somvr_comfort somvr_hud somvr_input somvr_objective somvr_session somvr_shared somvr_threat somvr_vrhands somvr_vrmenu)"; exit 1; }
echo "inert: /bin/true and python3 ran with the library preloaded and no log was written; exports: $exp"

echo "== selftest (the hooks against stand-in functions)"
gcc "${CF[@]}" -Wno-missing-prototypes -O2 -no-pie -fno-pie -rdynamic -o "$D/build/selftest" "$D/selftest.c" \
    "${CORE[@]}" -lpthread -lm
T=$(mktemp -d)
"$D/build/selftest" "$T" || { tail -40 "$T/somvr.log"; rm -rf -- "$T"; exit 1; }
awk '/^(HOOK|CONFIG|GUARD|XR)/ { print; if (++n == 20) exit }' "$T/somvr.log" # no pipe: no SIGPIPE under pipefail
rm -rf -- "$T"

if [ -x "$D/vklayer/build.sh" ]; then
  echo "== vklayer"
  "$D/vklayer/build.sh" --check
fi
