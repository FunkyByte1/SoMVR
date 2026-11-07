# nurn41/m17.sh - The Cure (Lithariel), after the card's start. Forces (GAME-STUDY-story 2.3): clear 3 search areas
# without an alarm; a caged caragor; a captain; 5 stealth brands (the bonus). VR path: ducked behind an unaware orc,
# the stab into its back (the silent kill from behind) or the cut; the stealth brand (the left palm + the trigger) for
# the bonus on the first five. Aids: the trip near each area (`story invisible on` while travelling, off at the orcs),
# the placement behind each orc.
M0=$(lines)
# an alarm runner (the game's 'RaiseAlarmNPC' marker since the last look): cut down before he lights it (aid: the trip
# behind him, his health 1; the act: the cut and the thrust)
AL0=$(cnt "marker [0-9]+ m=.*'RaiseAlarmNPC': shown")
alarmrun() { local n p; n=$(cnt "marker [0-9]+ m=.*'RaiseAlarmNPC': shown"); [ "$n" -gt "$AL0" ] || return 0; AL0=$n
  p=$(grep -aE "marker [0-9]+ m=.*'RaiseAlarmNPC': shown" "$L" | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
  [ -n "$p" ] || return 0; set -- $p; echo "$(date +%T) an alarm runner at $1 $3"; kill1 "$1" "$3" 110; z 1; }
draw
AREAS="${M17_AREAS:--11857 2154 -28171;-22654 2940 -31778;-13142 4031 -35426}"
NB=0; declare -A DONE
IFS=';' read -ra AR <<< "$AREAS"
for a in "${AR[@]}"; do set -- $a; ax=$1; ay=$2; az=$3; ptut
  echo "$(date +%T) ==== area $ax $az"; invis on; go $(( ax + 1500 )) $(( ay + 300 )) "$az"; z 2
  for k in $(seq 1 14); do ptut; win && { endphase 2; since "$M0" "Mission (Failed|Complete)|SUCCESS|IdentifySkip. pressed|questfail_in" | grep -q . && break 2; }   # (a rune window after a captain is no end - 011014)
    mapfile -t O < <(orcs "$ax" "$az" 2500); [ ${#O[@]} = 0 ] && { echo "$(date +%T) area $ax $az: no orc within 25 m"; break; }
    pick=""; for o in "${O[@]}"; do set -- $o; [ "${DONE[$1]:-0}" -ge 2 ] && continue; pick=$o; break; done
    [ -z "$pick" ] && { echo "$(date +%T) area $ax $az: every orc tried"; break; }
    set -- $pick; DONE[$1]=$(( ${DONE[$1]:-0} + 1 )); l1=$(lines)
    alarmrun
    # (som-b42-nurn: `behind 100` alone teleports in at "9058 cm/s" - the orc turned and fought, the alarm ran: 003146's
    # "Mission Failed! Reason: Alarm raised!") - placed 160 cm behind it under the game's invisibility (aid), then the
    # stick's sneak to 80 cm, crouched (the VR act)
    # 2nd take (the same boot): off at 160 cm it reacted at once ('ReactToBaseLow_FromSusp', the alarm ran) - so the game's
    # invisibility stays on through the area (aid: a person's unseen approach), 100 cm behind (aid); the acts are ours
    invis on; crouch; behind 100 "$2" "$4"
    if [ $NB -lt ${M17_BRANDS:-5} ]; then brand; since "$l1" "$BRRE" | grep -q . && { NB=$((NB + 1)); DONE[$1]=9; echo "$(date +%T) NU brand $NB"; z 1; continue; }; fi
    # (the VR stealth kill needs the blade laid into its back as stealth40-drive.sh aims it - not ported here: the cut and
    # the thrust from 100 cm touched its front half or missed, "no the attack stimulus" x30, 1 kill in 55 (011014); so the
    # game's own X stands in, as build 27's drill: an aid, logged as one)
    hp 1; press 118 150; z 1
    since "$l1" '^KILLS kill|STEALTH (kill|t=.*kill)' | head -1 | cut -c1-200
    since "$l1" "Alarm|alarm|Mission Failed" | head -2 | cut -c1-200
  done
  invis off; echo "$(date +%T) after area $ax $az: $(obj "$M0" 3 | tr '\n' '|')"
done
echo "$(date +%T) m17: brands $NB; $(obj "$M0" 5)"
