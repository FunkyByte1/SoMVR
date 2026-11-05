#!/bin/bash
# climb40-drive.sh - som-b40-climb3 (build 40, docs/re-xr-pipeline.md 178): the 11-05 09:20 session
# (logs/win-session-20261105-092046). PLAN40 words, in order (default "beam throw"):
#   beam   the long wall's beam and coping where the walk stalled: marks 093154_17 and 093135_16 (11-05) and 222603_18
#          (11-04) - Talion at the user's spot, the view at the user's heading, the stick forward 3 s: first with the
#          game's navmesh clip (`climb navmesh off`), then SOMVR_WALK_NAVMESH's (`climb navmesh on`): the root's way and rise,
#          LOCO's share, WALK / UNPIN lines, both eyes.
#   throw  mark 092717_9's wall (the user's spot): a hold at the chest, 2 pulls up, then a push of the holding hand
#          toward the wall (40 cm in 150 ms) with the grip opened 80 % in (`hands swing ... 150 0.8`): thrown off the
#          wall (SOMVR_CLIMB_FLING_OFF) - the jump's arc, its landing; both eyes in flight. A_B40=1: again with
#          `climb throw off` (the push dropped: the hang).
# Attaches to the newest boot of this worktree (PIN_RUN=RUN to pin). Stops at once (quit) when the user goes into VR.
CLIMB39_LIB=1 . "$(dirname "$0")/climb39-drive.sh"
PLAN40=${PLAN40:-"beam throw"}
ST=$R/climb40-steps.txt
SLOPE=${SLOPE40:-55}
rootxyz() { root | grep -oE 'root [0-9.-]+,[0-9.-]+,[0-9.-]+' | sed 's/root //'; }
walk3() { local tag=$1 n0 a b
  a=$(rootxyz); n0=$(wc -l < "$L")
  w "stick 0 0.7 0 3" 3.6; b=$(rootxyz); dd "$tag"
  python3 -c "
a=[float(v) for v in '$a'.split(',')]; b=[float(v) for v in '$b'.split(',')]
import math; print('WALK3 $tag: %.0f cm level, %+.0f cm up (from %s to %s)' % (math.hypot(b[0]-a[0], b[2]-a[2]), b[1]-a[1], '$a', '$b'))
" | tee -a "$ST"
  tail -n +"$n0" "$L" | grep -E '^(LOCO t=.* last 1 s|WALK (lip|edge|slope)|UNPIN)' | sed -E 's/frame=[0-9]+ //; s/; follow in our delta.*//' |
    cut -c1-200 | tail -6 | sed 's/^/    /'; }
spot() { local nm=$1 at=$2 yw=$3
  echo "=== beam $nm at $at, the view $yw deg"
  for nv in off on; do   # (the navmesh clip the game's / off over a real floor: SOMVR_WALK_NAVMESH)
    reset; w "climb navmesh $nv" 0.6
    go "$at"; faceyaw "$yw"; echo "$(date +%T) at: $(rootxyz)"
    walk3 "beam-$nm-nav$nv"
  done; }
# to within 70 cm of the face with short steps (the grab's tries reach 40-64 cm out; with the navmesh clip off a 0.5 s
# step carried him past the face's end - boots 103056, 103636)
nearwall() { local k a
  for k in 1 2 3 4 5 6 7 8; do probe; a=$(ahead +0); echo "$(date +%T) the face ahead: ${a:-none} cm"
    [ -n "$a" ] && [ "$a" -le 70 ] && return 0; w "stick 0 0.4 0 0.25" 0.6; done; return 1; }
throw() { local tag=$1 n0 a b
  echo "=== throw ($tag)"; reset
  go "30713.7 1980.5 -26217.0"; faceyaw 75; nearwall || echo "$(date +%T) no face within 70 cm"
  climbup 2 "throw-$tag" || return 1
  climbing || { echo "$(date +%T) throw $tag: not climbing"; return 1; }
  local H=${LASTHOLD:-R} hx=20; [ "$H" = L ] && hx=-20
  a=$(rootxyz); n0=$(wc -l < "$L")
  # (the headless game's ~20 Hz: a 150 ms swing gave the release 2-3 samples - boot 100104; 80 cm in 400 ms, open at 90 %)
  # (the swing eases in and out: at 90 % the hand was nearly still - boot 100859 read 86 cm/s; open at its middle, its
  # fastest)
  w "hands swing $H $hx -80 $(( ${GZ:-40} + 80 )) 0 0 0 400 0.5" 0.45
  dd "throw-$tag-flight1"; sleep 0.2; dd "throw-$tag-flight2"; sleep 2
  b=$(rootxyz); dd "throw-$tag-after"
  python3 -c "
a=[float(v) for v in '$a'.split(',')]; b=[float(v) for v in '$b'.split(',')]
import math; print('THROW $tag: %.0f cm level, %+.0f cm (from %s to %s)' % (math.hypot(b[0]-a[0], b[2]-a[2]), b[1]-a[1], '$a', '$b'))
" | tee -a "$ST"
  tail -n +"$n0" "$L" | grep -E '^(CLIMB t=.* (thrown|fling|hang:|end:|let go)|JUMP (t=.* (go|refused|the throw|landed|end)|END))' |
    sed -E 's/frame=[0-9]+ //' | cut -c1-230 | head -10 | sed 's/^/    /'
  reset; }
# beamtrace: `climb trace 3` while the stick walks 3 s at the spot (the per-tick root, the asked move, the proxy, the
# slope's cosine as the tick left it, its contacts)
beamtrace() { local nm=$1 at=$2 yw=$3 n0
  echo "=== beamtrace $nm at $at, the view $yw deg"; reset; w "climb slope $SLOPE" 0.6
  go "$at"; faceyaw "$yw"; echo "$(date +%T) at: $(rootxyz)"
  n0=$(wc -l < "$L"); w "climb trace 4" 0.3; walk3 "beamtrace-$nm"
  tail -n +"$n0" "$L" | grep -E '^CLIMB trace t=' | sed -E 's/^CLIMB trace //; s/frame=[0-9]+: //; s/ grav [0-9]+ rm [0-9]+ layer [0-9]+//' |
    cut -c1-260 > "$R/beamtrace-$nm.txt"
  echo "    $(wc -l < "$R/beamtrace-$nm.txt") trace lines -> $R/beamtrace-$nm.txt"; head -3 "$R/beamtrace-$nm.txt" | sed 's/^/    /'; }
card
for p in $PLAN40; do
  case $p in
    beam) spot m17 "31122.0 4810.1 -30710.6" 50; spot m16 "30766.9 4810.3 -30479.0" -7; spot m18 "31237.2 4816.5 -30684.4" 48 ;;
    beamtrace) beamtrace m18 "31237.2 4816.5 -30684.4" 48; beamtrace m16 "30766.9 4810.3 -30479.0" -7 ;;
    throw) throw on; [ "${A_B40:-0}" = 1 ] && { w "climb throw off" 0.6; throw off; w "climb throw on" 0.6; } ;;
  esac
done
echo "steps done $(date +%T)"; [ "${NOQUIT:-0}" = 1 ] && exit 0
sleep 2; w "endlogs" 3
grep -E '^(WALK END build 40|CLIMB END build 40|JUMP END build 40)' "$L" | tail -3 | cut -c1-300
echo quit >> "$R/cmd"; echo "quit $(date +%T)"
