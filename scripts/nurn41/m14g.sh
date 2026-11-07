# nurn41/m14g.sh - som-b42-bleed2 (docs 191.9-191.10): Hunting Partners' pictures for the hint line and the bleed's glow,
# before m14b.sh's bleed the VR way (NU_MISSION="m14g.sh m14b.sh", M14_SKIP=bfollow). After the card's start: the follow
# to the eating graug (m14's); a picture standing where the lesson line "Use [RT] + (X) ..." used to be up; then, under
# the game's invisibility (an aid), crouched behind its right leg at BLG_FAR (345) and BLG_NEAR (215) cm from its root,
# the view turned to it: `verbs bleed where` (the glow's distance) and both-eye pictures; at the near one the drawn
# blade held at the glow (the stab's start pose: the glow full, the tick) and its picture. Then up, the sword put back.
. "$HERE/nurn41/lib42.sh"
M0=$(lines)
w "story invisible off" 0.3
follow "show 'GP_Torvin02_Tut_TrollBleed'|Bleed the Graug|'Troll': shown by the game" "${M14_FOLLOW_MIN:-12}"
ptut
gwait() { local k G TM mx my mz gc gx gy gz gf gd ga; EG=""; invis on
  for k in $(seq 1 70); do ptut; state | grep -q 'paused=1' && { pics nu-blgp$k; tap R a 0.4; sleep 2; }
    G=$(graugs); echo "$G" | grep -q Eating && { EG=$(echo "$G" | grep Eating | head -1); return 0; }
    if [ -z "$G" ]; then TM=$(objm 'Troll'); [ -n "$TM" ] && { read mx my mz <<< "$TM"; go $((mx + 1800)) $((my + 300)) "$mz"; z 2; G=$(graugs); }; fi
    echo "$G" | grep -q Eating && { EG=$(echo "$G" | grep Eating | head -1); return 0; }
    read gc gx gy gz gf gd ga <<< "$(echo "$G" | head -1)"
    [ -n "$gd" ] && [ "$gd" -lt 2800 ] && go $((gx + 3200)) $((gy + 300)) "$gz"
    [ $((k % 5)) = 1 ] && echo "$(date +%T) waiting for it to eat ($(echo "$G" | head -1 | cut -d' ' -f6-))"; z 4; done; return 1; }
# where: the cut's point in the head's frame -> WT ("x y z"), WD (cm from the eyes)
where() { local l i; l=$(lines); w "verbs bleed where" 0.3; WT=""
  for i in $(seq 1 15); do WT=$(since "$l" '^VERBS bleed where t=.*the cut.s point' | tail -1 | sed -nE "s/.*the cut's point (-?[0-9]+) (-?[0-9]+) (-?[0-9]+) knee.*/\1 \2 \3/p"); [ -n "$WT" ] && break; sleep 0.2; done
  WD=$(echo "$WT" | awk '{ printf "%d", sqrt($1*$1 + $2*$2 + $3*$3) }'); echo "$(date +%T) the glow (the cut's point) at ${WT:-?} in the head's frame: ${WD:-?} cm from the eyes"; }
# placebl D: crouched D cm behind its root, 90 cm to its right (behind its right leg), the view turned to it (aids)
placebl() { G=$(graugs "$gx" "$gz" 800 | grep "$gc" | head -1); [ -n "$G" ] && read gc gx gy gz gf gd ga <<< "$G"
  read tx tz < <(python3 -c "import math;f=math.radians($gf);D=$1;S=90;print(round($gx-D*math.sin(f)+S*math.cos(f)),round($gz-D*math.cos(f)-S*math.sin(f)))")
  go "$tx" $((gy - 150)) "$tz"; w "ride face graug" 1.5; w "ride face off" 0.3; crouch 10000; z 2
  echo "$(date +%T) NU aid: crouched $1 cm behind graug $gc, behind its right leg"; }
gwait && G=$EG || { echo "$(date +%T) m14g: no eating graug"; return 0; }
read gc gx gy gz gf gd ga <<< "$G"; echo "$(date +%T) m14g: graug $gc at $gx $gy $gz facing $gf ($ga)"
B0=$(lines); placebl 600; w "crouch stand" 0.3; z 2; pics nu-hint-stand   # (6 m behind it, standing: the lesson line's place)
echo "$(date +%T) the lesson line: $(since "$M0" 'HINTS buttons t=' | head -2 | cut -c1-200 | tr '\n' '|')"
for D in ${BLG_FAR:-345} ${BLG_NEAR:-215}; do
  placebl "$D"; z 1; where; pics nu-glow-$D
  echo "$(date +%T) at $D: $(since "$B0" '^VERBS bleed watch ' | tail -1 | grep -oE 'OFFERED|not offered \([^)]*\)' | head -1)"
done
draw; crouch 10000; z 1; where
if [ -n "$WT" ] && [ "${BLG_BLADE:-1}" = 1 ]; then   # the stab's start pose (the grip on the line from the shoulder, the blade through the glow) - held
  read a1 a2 a3 a4 a5 <<< "$(echo "$WT" | awk '{ sx = 20; sy = -25; sz = 0; dx = $1 - sx; dy = $2 - sy; dz = $3 - sz; d = sqrt(dx*dx + dy*dy + dz*dz)
    ux = dx / d; uy = dy / d; uz = dz / d; L = d - 62; if (L < 40) L = 40; if (L > 85) L = 85; P = 3.14159265
    printf "%.1f %.1f %.1f %.1f %.1f\n", $1 - (L + 30) * ux, $2 - (L + 30) * uy, $3 - (L + 30) * uz, atan2(uy, sqrt(ux*ux + uz*uz)) * 180 / P, -atan2(ux, uz) * 180 / P }')"
  w "hands test R $a1 $a2 $a3 $a4 $a5 0" 1.2; pics nu-glow-blade; echo "$(date +%T) NU act: the blade held at the glow (30 cm short of the stab)"
  w "$REST" 0.6
fi
since "$B0" 'VERBS bleed watch .*OFFERED' | tail -1 | cut -c1-200
w "crouch stand" 0.3; invis on; go $((gx + 900)) $((gy + 100)) "$gz"; z 2
drawn && { w "$REST" 0.3; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.4; w "$REST" 0.4; }
echo "$(date +%T) m14g: done; ticks $(grep -ac 'HAPTICS.*tick' "$L" 2>/dev/null)"
