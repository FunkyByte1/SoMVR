# nurn41/m17.sh - The Cure (Lithariel), after the card's start. Forces (GAME-STUDY-story 2.3): clear 3 search areas
# without an alarm; a caged caragor; a captain; 5 stealth brands (the bonus). VR path: ducked behind an unaware orc,
# the stab into its back (the silent kill from behind) or the cut; the stealth brand (the left palm + the trigger) for
# the bonus on the first five. Aids: the trip near each area (`story invisible on` while travelling, off at the orcs),
# the placement behind each orc.
M0=$(lines)
draw
AREAS="${M17_AREAS:--11857 2154 -28171;-22654 2940 -31778;-13142 4031 -35426}"
NB=0; declare -A DONE
IFS=';' read -ra AR <<< "$AREAS"
for a in "${AR[@]}"; do set -- $a; ax=$1; ay=$2; az=$3; ptut
  echo "$(date +%T) ==== area $ax $az"; invis on; go $(( ax + 1500 )) $(( ay + 300 )) "$az"; invis off; z 2
  for k in $(seq 1 14); do ptut; win && { endphase 2; break 2; }
    mapfile -t O < <(orcs "$ax" "$az" 2500); [ ${#O[@]} = 0 ] && { echo "$(date +%T) area $ax $az: no orc within 25 m"; break; }
    pick=""; for o in "${O[@]}"; do set -- $o; [ "${DONE[$1]:-0}" -ge 2 ] && continue; pick=$o; break; done
    [ -z "$pick" ] && { echo "$(date +%T) area $ax $az: every orc tried"; break; }
    set -- $pick; DONE[$1]=$(( ${DONE[$1]:-0} + 1 )); l1=$(lines)
    crouch; behind 100 "$2" "$4"
    if [ $NB -lt ${M17_BRANDS:-5} ]; then brand; since "$l1" "$BRRE" | grep -q . && { NB=$((NB + 1)); DONE[$1]=9; echo "$(date +%T) NU brand $NB"; z 1; continue; }; fi
    thrust; z 1
    since "$l1" '^KILLS kill|STEALTH (kill|t=.*kill)' | head -1 | cut -c1-200
    since "$l1" "Alarm|alarm|Mission Failed" | head -2 | cut -c1-200
  done
  echo "$(date +%T) after area $ax $az: $(obj "$M0" 3 | tr '\n' '|')"
done
echo "$(date +%T) m17: brands $NB; $(obj "$M0" 5)"
