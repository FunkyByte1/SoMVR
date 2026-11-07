# nurn41/m15.sh - The Power of the Wraith (Marwen), after the card's start. Forces (GAME-STUDY-story 2.3): weaken and
# dominate Grûblik, Command him; protect him through 3 groups; a vantage point unseen; activate him; kill warchief Târz
# (Grûblik must live). VR path: the left palm at his head + the grip held (the grab), the Touch's A (the Domination's
# own letter), Y = Command (SOMVR_VERBS_COMMAND), the raised left hand + the trigger = activate (SOMVR_VERBS_RALLY), the
# cuts and the thrust. Aids: the trips (a person walks), `captain hp` (his health low for the grab, Grûblik's high in the
# groups, Târz's low at the end), `story invisible on` on the way to the vantage, the groups' orcs at health 1.
M0=$(lines)
# the Command screens after Y (106: "This Captain is now under your control" A, "infiltrate Târz's squad" A, Târz's card B):
# A while a page, a window or a menu is up, B once A changes nothing; until the world and "Recruitment" or 25 s
cmdscreens() { local k s a=0; for k in $(seq 1 12); do s=$(state)
    if echo "$s" | grep -qE 'paused=1|menus=[1-9]|ctx=menu' || win; then
      if [ $a -lt 3 ]; then tap R a 0.4; a=$((a + 1)); else tap R b 0.4; a=0; fi; sleep 2.5
    else since "$M0" "Recruitment|It is done|PowerStruggle" | grep -q . && break; sleep 2; fi; done
  echo "$(date +%T) after the Command screens: $(state); $(since "$M0" "Recruitment|It is done|PowerStruggle" | head -2 | cut -c1-140 | tr '\n' '|')"; }
draw
if [[ " ${M15_SKIP:-} " == *" dom "* ]]; then   # (a driver taken over in the Domination: its Command by Y now)
  tap L b 0.3; sleep 3; ungrab; cmdscreens
  echo "$(date +%T) the Command by Y: $(since "$M0" "Touch.s Y" | head -1 | cut -c1-160)"
else
# 1. "Locate Grûblik": the slave camp
go 20154 3345 -28472; z 6
for i in $(seq 1 30); do ptut; since "$M0" "Dominate|OrcHealthBar_Marked|Grave-Walker|Dead-Man" | grep -q . && break; z 2; done
echo "$(date +%T) located: $(obj "$M0" 3)"
# 2. Dominate: his health low (aid), the grab by the left palm + the grip, A (the Domination's letter), then Y = Command
DOM=0
for try in 1 2 3 4; do ptut; l1=$(lines)
  w "captain hp 5" 0.6; echo "$(date +%T) NU aid: captain hp 5"; C=$(capat)
  [ -n "$C" ] && { set -- $C; behind 100 "$1" "$3"; }
  grab 1.5; tap R a 0.3; sleep 3
  if since "$l1" "Domination_(Command|Interrogate_StoryOverride|Subdue)|Intrgtn_Into" | grep -q .; then
    z 2; tap L b 0.3; sleep 3; ungrab   # (Y = the left Touch's upper button: `btn L b`; `btn R y` is no button - som-b42-nurn)
    since "$l1" "Touch's Y|Domination_Command|162 " | head -2 | cut -c1-200
    cmdscreens
    DOM=1; break
  fi
  ungrab; echo "$(date +%T) the grab try $try: $(since "$l1" 'GRAB|grab|PC_Grab' | tail -2 | cut -c1-180 | tr '\n' '|')"; z 3
done
echo "$(date +%T) DOM=$DOM: $(obj "$M0" 3)"
fi
# 3. the Recruitment Power Struggle: three groups; Grûblik's health high (aid), each marked orc cut down
go 13200 2550 -33700; z 4
w "practice console Player.CanBeAttacked 0" 0.5; echo "$(date +%T) NU aid: Player.CanBeAttacked 0 (a person's guard; 20261107-000258 lost Talion's last chance to the drill's stray thrusts)"
for g in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do ptut
  since "$M0" "greater vantage|ObjectiveNoMinimum" | grep -q . && break
  win && endphase 2
  w "captain hp 3000" 0.4; echo "$(date +%T) NU aid: Grûblik's health refilled (his max: 976)"   # (one 3000 is capped at his max: 000258's "Grûblik was killed")
  mapfile -t P < <(markers 'ObjectiveGoldAlwaysUp'); [ ${#P[@]} = 0 ] && { z 4; continue; }
  set -- ${P[0]}; kill1 "$1" "$3" 110; z 1
done
echo "$(date +%T) the groups: $(obj "$M0" 3)"
# 4. the vantage (a tower), unseen: the trip there (aid, invisible on the way), then activate him by the raised hand
invis on; go 17525 6250 16205; z 4; invis off
l3=$(lines); rally; z 4; since "$l3" "rally|128 |FlipCaptain|Defeat" | head -3 | cut -c1-200
# 5. Târz: help Grûblik; Târz's health low (aid) then the thrust / cut at him
for k in 1 2 3 4 5 6 7 8; do ptut; win && break; state | grep -q 'video=1' && break
  w "captain hp 5" 0.6; echo "$(date +%T) NU aid: captain hp 5"; C=$(capat)
  [ -n "$C" ] && { set -- $C; behind 110 "$1" "$3"; w "execute face" 0.4; slash; thrust; }; z 3
done
echo "$(date +%T) m15: $(obj "$M0" 4)"
