#!/bin/bash
# edge42-drive.sh - som-b42-edgestop (build 42, docs/re-xr-pipeline.md 183): mark 17's coping - the walk ran ~4 deg off
# the top's edge line and the capsule rolled over it beside the walk (SOMVR_WALK_EDGE_RIDE). PLAN42 words, in order
# (default "lip17ab survey hillwalk hold40r"):
#   lip17ab   boot 180118's route at mark 17 (31551.4 4818.7 -30574.2) with the stick's heading FIXED at 64 deg from the
#             measured head yaw (no turn words: at 20 Hz they spin the body): `climb edgeride off` (the old path - the
#             per-tick `WALK edge guard` lines say which exit it takes), then `climb edgeride on` (held at the lip)
#   lip17     the same once, the rule as the boot has it
#   survey    `climb heights $SURVEY42_GRID` (default 400 41: a 160 m grid, 4 m apart) at two meadow spots (SURVEY42);
#             scripts/hill42_pick.py picks open 10-25 deg downhill lines (5 samples) -> $R/hill-spots.txt
#   hillwalk  the stick 1.6 s down each picked line (HILL42="x y z heading;..." gives them instead): no edge stop hold, no
#             fall, the descent measured (PASS needs one walk of 10+ deg)
#   hold40r   climb41-drive.sh's (A held at a real edge = the climb down)
# Hands as a player holds them (at the waist pointing ahead, the grip frame's -Z forward); the walk by the stick's
# direction only. Attaches to the newest boot of this worktree (PIN_RUN=RUN to pin). Stops at once (quit) when the user
# goes into VR. NOQUIT=1: leaves the game running at the end.
CLIMB41_LIB=1 . "$(dirname "$0")/climb41-drive.sh"
PLAN42=${PLAN42:-"lip17ab survey hillwalk hold40r"}
ST=$R/edge42-steps.txt
LIP17="31551.4 4818.7 -30574.2"
SURVEY42=${SURVEY42:-"34415 3628 -25894;32500 1878 -24800"}
SURVEY42_GRID=${SURVEY42_GRID:-"400 41"}   # (STEP N: boot 192731's 100 x 25 at the two meadows found only 1-5 deg and cliffs)
lip17f() { local tag=$1 k y0 y1 s held fell
  echo "=== lip17 ($tag): the coping's edge line, the stick's heading fixed at 64"; reset; hands
  go "$LIP17"; sleep 1.5; y0=$(ry); s=$(sdir 64); k=$(nl)
  w "climb trace 5" 0.3; w "stick $s 0 4" 4.6; w "stick 0 0 0 0.1" 0.5; y1=$(ry); dd "lip17-$tag"
  tail -n +"$k" "$L" | grep -E '^WALK edge guard t=' | sed -E 's/^WALK edge guard //' | cut -c1-480 > "$R/guard-lip17-$tag.txt"
  tail -n +"$k" "$L" | grep -E '^CLIMB trace t=' | sed -E 's/^CLIMB trace //; s/frame=[0-9]+: //' | cut -c1-220 > "$R/trace-lip17-$tag.txt"
  tail -n +"$k" "$L" | grep -E '^(WALK (edge stop|ground)|JUMP t=.*(go|end|refused))' | sed -E 's/frame=[0-9]+ //' | cut -c1-260 | head -4 | sed 's/^/    /'
  held=$(tail -n +"$k" "$L" | grep -cE '^WALK edge stop t=.*held at a drop')
  fell=$(tail -n +"$k" "$L" | grep -cE '^(JUMP t=.*go: the fall from here|WALK ground t=.*no ground under him)')
  echo "LIP17 $tag: $y0 -> $y1 ($(rootxyz)); holds $held, falls/no-ground $fell; guard lines $(wc -l < "$R/guard-lip17-$tag.txt")" | tee -a "$ST"
  if [ "$fell" = 0 ] && [ $(( ${y0:-0} - ${y1:-0} )) -lt 60 ] && [ "$held" -gt 0 ]; then
    PASS "lip17 $tag: held at the coping's edge ($y0 -> $y1; $(tail -n +"$k" "$L" | grep -E '^WALK edge stop t=.*held at a drop' | head -1 | grep -oE 'riding an edge|no floor within [0-9]+ cm under the feet'))"
  else FAIL "lip17 $tag: $y0 -> $y1 (holds $held, falls $fell)"; fi
  reset; }
lip17ab() { w "climb edgeride off" 0.5; lip17f off; w "climb edgeride on" 0.5; lip17f on; }
lip17() { lip17f rule; }
survey() { local at k f
  : > "$R/hill-spots.txt"
  echo "$SURVEY42" | tr ';' '\n' | while read -r at; do
    [ -n "$at" ] || continue
    reset; go "$at"; sleep 1.5; k=$(nl); w "climb heights $SURVEY42_GRID" 1
    waitfor "^CLIMB heights row $(( ${SURVEY42_GRID#* } - 1 )) " 8
    f="$R/heights-$(echo "$at" | tr ' ' '_').txt"
    tail -n +"$k" "$L" | grep -E '^CLIMB heights' > "$f"
    echo "survey at $at: $(wc -l < "$f") lines" | tee -a "$ST"
    python3 -I "$(dirname "$0")/hill42_pick.py" "$f" 3 | tee -a "$R/hill-spots.txt" "$ST"
  done; }
hillwalk() { local spots line x y z hd a b k held fell ok=0 bad=0 steep=0 res
  if [ -n "${HILL42:-}" ]; then spots=$(echo "$HILL42" | tr ';' '\n')
  else spots=$(grep -E '^HILLSPOT [-0-9]' "$R/hill-spots.txt" 2>/dev/null | awk '{print $2, $3, $4, $6}' | head -${HILL42_MAX:-6}); fi
  [ -n "$spots" ] || { FAIL "hillwalk: no hillside spot"; return; }
  echo "=== hillwalk: open 10-25 deg downhills are not stopped"
  while read -r x y z hd; do
    [ -n "$hd" ] || continue
    reset; hands; go "$x $y $z"; sleep 1.5; a=$(rootxyz); s=$(sdir "$hd"); k=$(nl)
    w "stick $s 0 1.6" 2.0; w "stick 0 0 0 0.1" 0.4; b=$(rootxyz)
    held=$(tail -n +"$k" "$L" | grep -cE '^WALK edge stop t=.*held at a drop')
    fell=$(tail -n +"$k" "$L" | grep -cE '^(JUMP t=.*go: the fall from here|WALK ground t=.*no ground under him)')
    res=$(python3 -c "
import math
a=[float(v) for v in '$a'.split(',')]; b=[float(v) for v in '$b'.split(',')]
lv=math.hypot(b[0]-a[0], b[2]-a[2]); dn=a[1]-b[1]; deg=math.degrees(math.atan2(dn, lv)) if lv > 1 else 0
print('%.0f %.0f %.0f' % (lv, dn, deg))")
    set -- $res
    echo "HILL42 $x $y $z heading $hd: $1 cm level, $2 cm down ($3 deg), holds $held, falls $fell" | tee -a "$ST"
    [ "$held" = 0 ] && [ "$fell" = 0 ] && ok=$((ok + 1)) || bad=$((bad + 1))   # (a short walk - a tree, a rock - is no stop)
    [ "${3:-0}" -ge 10 ] && [ "${1:-0}" -ge 150 ] && steep=$((steep + 1))
  done <<< "$spots"
  [ $bad = 0 ] && [ $steep -gt 0 ] && PASS "hillwalk: all $ok downhill walks unstopped ($steep of 10+ deg; no hold, no fall)" ||
    FAIL "hillwalk: $bad walk(s) held or fell; $steep of 10+ deg (1.5 m+)"
  reset; }
card
for p in $PLAN42; do
  case $p in lip17ab|lip17|survey|hillwalk|hold40r) $p ;; *) echo "unknown plan word $p" ;; esac
done
echo "steps done $(date +%T)"
grep -E ' (PASS|FAIL) ' "$ST" 2>/dev/null
[ "${NOQUIT:-0}" = 1 ] && exit 0
sleep 2; w "endlogs" 3
grep -E '^(WALK END build 4[012])' "$L" | tail -4 | cut -c1-300
echo quit >> "$R/cmd"; echo "quit $(date +%T)"
