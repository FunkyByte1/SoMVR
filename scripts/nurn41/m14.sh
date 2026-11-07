# nurn41/m14.sh - Hunting Partners (Torvin), after the card's start. Forces (GAME-STUDY-story 2.3, 3.5): bleed the
# graug from behind, flee its charge, mount a caragor, lure the graug to Torvin, stun it with a charged head shot, hit it
# while stunned (repeat), dominate it (a struggle), ride it (feed it 3 orcs, optional), kill it. VR path: the blade's low
# thrust crouched behind it (SOMVR_GRAUG_BLEED), the left grip closed at the caragor's offer (SOMVR_RIDE), the stick
# (`ride steer` = the stick toward a point), the bow drawn full and held (SOMVR_GRAUG_STUN), the cuts, the left grip held
# on the beast for its domination (SOMVR_RIDE), the struggle rule (SOMVR_STRUGGLE_RULE=b41). Aids: the trips, the
# placement behind the graug, `bow aimhead` (the aim), `bow arrows 8`. M14_SKIP="follow bleed mount" skips phases.
. "$HERE/nurn41/lib42.sh"
M0=$(lines); SK=" ${M14_SKIP:-} "; TV="22100 2350 -26650"
w "story invisible off" 0.3
# 1. follow Torvin to the eating graug
[[ $SK == *" follow "* ]] || follow "show 'GP_Torvin02_Tut_TrollBleed'|Bleed the Graug|'Troll': shown by the game" "${M14_FOLLOW_MIN:-12}"
ptut
# 2. the bleed (the game's own reach: 140-150 cm behind it)
if [[ $SK != *" bleed "* ]]; then invis on; HS=1
  for DA in 140:0 150:0 145:-20 145:20 135:0 160:0 150:-35 150:35; do bleed ${DA%:*} ${DA#*:} && { HS=0; break; }; z 6; done
  echo "$(date +%T) the bleed: $([ $HS = 0 ] && echo HAMSTRUNG || echo not offered)"; w "crouch stand" 0.3; invis off
  [ $HS = 1 ] && [ "${M14_GO_ON:-0}" = 0 ] && { echo "$(date +%T) m14: no bleed - the director stops here"; return 0; }; fi
ptut
# 3. the mount, then the lure to Torvin (ride steer = the stick), round him at ~15 m until "Step three"
STEP3=0; THROWN=0; ANG=0; LB=$(lines)
[[ $SK == *" mount "* ]] || mount 9
T=$((SECONDS + ${M14_LURE_MIN:-12} * 60)); n=0
while [ $SECONDS -lt $T ] && [ $STEP3 = 0 ]; do n=$((n + 1)); ptut; gone && break
  win && { sleep 1.5; pics nu-lw$n; tap R a 0.4; sleep 2; continue; }
  since "$LB" "TrollStun|Shoot it in the head|Stun the Graug" | grep -q . && { STEP3=1; break; }
  if ! seated; then THROWN=$((THROWN + 1)); echo "$(date +%T) off the beast ($THROWN)"; [ $THROWN -gt 4 ] && break; mount 6 || continue; fi
  read px py pz <<< "$(tpos)"; O=$(objm 'Objective'); [ -n "$O" ] && TV=$O; read ox oy oz <<< "$TV"
  od=$(d2 "$ox" "$oz" "${px:-0}" "${pz:-0}"); G=$(graugs "${px:-0}" "${pz:-0}" 6000 | head -1); gd=99999; [ -n "$G" ] && gd=$(echo "$G" | cut -d' ' -f6)
  if [ "$od" -gt 2000 ]; then w "ride steer $ox $oz" 0.8
  else read cx cz < <(python3 -c "import math;print(round($ox+1500*math.sin(math.radians($ANG))), round($oz+1500*math.cos(math.radians($ANG))))")
    [ "$(d2 "$cx" "$cz" "${px:-0}" "${pz:-0}")" -lt 700 ] && ANG=$(( (ANG + 60) % 360 )); w "ride steer $cx $cz" 0.5; fi
  [ $((n % 4)) = 1 ] && echo "$(date +%T) lure #$n: Torvin $od cm, the graug $gd cm, $(state)"
  z 2
done
w "ride steer off" 0.3; echo "$(date +%T) the lure: Step Three $STEP3 ($(obj "$M0" 3 | tr '\n' '|'))"
# 4. the stun: off the beast (B), the bow's charged shots at its head; once stunned, the cuts at it
seated && { tap R b 0.4; sleep 3; echo "$(date +%T) NU act: B = off the beast"; }
T=$((SECONDS + ${M14_STUN_MIN:-14} * 60)); SHOTS=0; ST0=$(cnt '^BOW graugstun t='); n=0
while [ $SECONDS -lt $T ]; do n=$((n + 1)); ptut; gone && break
  win && { sleep 1.5; pics nu-sw$n; tap R a 0.4; sleep 2; continue; }
  state | grep -q 'video=1' && break
  since "$M0" "Dominate|Domination|UI_.*Dominat" | grep -q . && { echo "$(date +%T) a domination shown"; break; }
  state | grep -q 'ctx=struggle' && break
  G=$(graugs | head -1); [ -z "$G" ] && { z 3; continue; }; read gc gx gy gz gf gd ga <<< "$G"
  if echo "$ga" | grep -q 'Stun'; then   # stunned: up to it (aid) and the cuts (act)
    bow_ok && bow_away; draw; w "practice go $((gx + 200)) $((gy + 50)) $gz" 1.5; w "ride face graug" 1.5; w "ride face off" 0.3
    echo "$(date +%T) NU aid: beside the stunned graug ($ga)"; slash; slash; upcut; thrust
  elif [ "$gd" -lt 4500 ]; then shoot "$gx" "$gy" "$gz"
  else w "practice go $((gx + 2000)) $((gy + 100)) $gz" 1.5; echo "$(date +%T) NU aid: 20 m from the graug"; fi
  [ "$(cnt '^BOW graugstun t=')" -gt "$ST0" ] && { ST0=$(cnt '^BOW graugstun t='); pics nu-stun$n; }
  [ $((n % 5)) = 1 ] && echo "$(date +%T) stun #$n: graug $gd cm ($ga), $(obj "$M0" 2 | tr '\n' '|')"
done
bow_ok && bow_away
echo "$(date +%T) m14: shots $SHOTS, stuns $(( $(cnt '^BOW graugstun t=') )); $(obj "$M0" 4)"
moments "$M0" 'STRUGGLE|RIDE t=.*(dom|offers)|OBJECTIVE t=.*shown' 12
