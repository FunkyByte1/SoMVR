#!/bin/bash
# beam42-drive.sh - som-b42-beam (build 42, docs/re-xr-pipeline.md 187): the long wall's beam walk back on
# (SOMVR_WALK_BEAM: the context-action clip off + climb3's 55 deg slope for a step whose floor goes on 65 cm ahead) with
# the edge ride (SOMVR_WALK_EDGE_RIDE) - mark 16 onto its patch, mark 17 held at its lip, never a slide or a fall.
# PLAN_B42 words, in order (default "beam16 beam17f beam17 lip17 m18 hold40r hill hillwalk"):
#   beam16   climb41-drive.sh's (the 11-05 mark 16's patch: the clip the game's, then off - want 53 cm+ with it off)
#   beam17f  mark 17's beam (31122.0 4810.1 -30710.6) at the user's heading 50, the stick's heading FIXED from the measured
#            head yaw (no turn words: at 20 Hz they spin the body), 6 s, the switches as the boot has them (no ctxclip
#            word): PASS = no fall and the root less than 60 cm under the start (held or not)
#   beam17   climb41-drive.sh's (faceyaw 50, the stick 0.7 forward 5 s, `climb ctxclip on`): held, no fall
#   lip17    edge42-drive.sh's (the coping's edge line at heading 64): held riding the edge
#   m18      the 11-04 mark 222603_18 on the coping (31237.2 4816.5 -30684.4, view 48, the clip off): walks on, no fall
#   hold40r  climb41-drive.sh's (A held at a real edge = the climb down)
#   hill     climb41-drive.sh's (8 meadow walks 1-9 deg up and down: no hold, no fall)
#   hillwalk edge42-drive.sh's (HILL42's 14-16 deg open downhills: no hold, no fall)
# Run 376 first in the fresh boot (edge38-drive.sh NOQUIT=1 PLAN_E38='hold40 ring40 up40 hold40b'), then this with
# PIN_RUN. Hands as a player holds them (at the waist pointing ahead); the walk by the stick's direction only. Stops at
# once (quit) when the user goes into VR. NOQUIT=1: leaves the game running at the end.
EDGE42_LIB=1 . "$(dirname "$0")/edge42-drive.sh"
PLAN_B42=${PLAN_B42:-"beam16 beam17f beam17 lip17 m18 hold40r hill hillwalk"}
HILL42=${HILL42:-"31215 2196 -18694 -90;31700 2474 -17200 -90;31300 2336 -17600 -90"}
ST=$R/beam42-steps.txt
fallsince() { tail -n +"$1" "$L" | grep -cE '^(JUMP t=.*go: the fall from here|WALK ground t=.*no ground under him)'; }
heldsince() { tail -n +"$1" "$L" | grep -cE '^WALK edge stop t=.*held at a drop'; }
beam17f() { local k a b y0 y1 s held fell
  echo "=== beam17f: mark 17's beam at heading 50, the stick's heading fixed, 6 s"; reset; hands
  go "31122.0 4810.1 -30710.6"; sleep 1.5; a=$(rootxyz); y0=$(ry); s=$(sdir 50); k=$(nl)
  w "climb trace 7" 0.3; w "stick $s 0 6" 6.6; w "stick 0 0 0 0.1" 0.5; b=$(rootxyz); y1=$(ry); dd beam17f
  tail -n +"$k" "$L" | grep -E '^WALK edge guard t=' | sed -E 's/^WALK edge guard //' | cut -c1-480 > "$R/guard-beam17f.txt"
  tail -n +"$k" "$L" | grep -E '^CLIMB trace t=' | sed -E 's/^CLIMB trace //; s/frame=[0-9]+: //' | cut -c1-220 > "$R/trace-beam17f.txt"
  tail -n +"$k" "$L" | grep -E '^(WALK (edge stop|ground|lip)|JUMP t=.*(go|end|refused))' | sed -E 's/frame=[0-9]+ //' | cut -c1-260 | tail -5 | sed 's/^/    /'
  held=$(heldsince "$k"); fell=$(fallsince "$k")
  echo "BEAM17F: $a -> $b ($y0 -> $y1); holds $held, falls/no-ground $fell" | tee -a "$ST"
  if [ "$fell" = 0 ] && [ $(( ${y0:-0} - ${y1:-0} )) -lt 60 ]; then PASS "beam17f: no slide, no fall ($y0 -> $y1; holds $held)"
  else FAIL "beam17f: $y0 -> $y1 (holds $held, falls $fell)"; fi
  reset; }
m18() { local k y0 y1 fell
  k=$(nl); y0=$(ry); CX="on" spot m18 "31237.2 4816.5 -30684.4" 48; y1=$(ry); fell=$(fallsince "$k")
  [ "$fell" = 0 ] && [ -n "$(lvl m18-ctxon)" ] && PASS "m18: $(lvl m18-ctxon) cm along the coping, no fall" ||
    FAIL "m18: $(lvl m18-ctxon) cm, falls $fell"
  w "stick 0 0 0 0.1" 0.3; reset; }
card
grep -E '^CONFIG walk (beam|clip far|ctxclip|slope)=' "$L" | cut -c1-160 | tee -a "$ST"
for p in $PLAN_B42; do
  case $p in beam16|beam17|hold40r|hill|hillwalk|beam17f|m18) $p ;; lip17) lip17f rule ;; *) echo "unknown plan word $p" ;; esac
done
echo "steps done $(date +%T)"
grep -E ' (PASS|FAIL) ' "$ST" 2>/dev/null
[ "${NOQUIT:-0}" = 1 ] && exit 0
sleep 2; w "endlogs" 3
grep -E '^(WALK END build 4[012])' "$L" | tail -4 | cut -c1-300
echo quit >> "$R/cmd"; echo "quit $(date +%T)"
