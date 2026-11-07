# nurn41/m16.sh - The Great White Graug (Torvin), after the card's start. Forces (GAME-STUDY-story 2.3, 3.5): dominate
# a Dire Caragor (counter its charge or drop on it), then the Legendary Graug in a cave: its charge dodged, the charged
# head shot -> stunned -> the blade; the ghuls it grabs shot before it eats them; repeat. VR path: the left grip at the
# caragor's offer + held (SOMVR_RIDE: its domination), the stick, the bow drawn full and held, the cuts. Aids: the trips,
# `bow aimhead`, `bow arrows 8`. M16_SKIP="follow caragor".
. "$HERE/nurn41/lib42.sh"
M0=$(lines); SK=" ${M16_SKIP:-} "
w "story invisible off" 0.3
[[ $SK == *" follow "* ]] || follow "Dire Caragor|Dominate|Caragor" "${M16_FOLLOW_MIN:-10}"
ptut; echo "$(date +%T) after the follow: $(obj "$M0" 3 | tr '\n' '|')"
# the dire caragor: the mount at the offer; the domination = the grip held (SOMVR_RIDE)
if [[ $SK != *" caragor "* ]]; then mount 10; z 3; ptut; echo "$(date +%T) the caragor: $(moments "$M0" 'RIDE t=.*(dom|seated|mounted)|STRUGGLE' 6 | tr '\n' '|')"; fi
# on to the graug: follow the objective (riding or on foot) until a graug is near
follow "Graug|graug|ghul|Ghul" "${M16_FOLLOW2_MIN:-8}"
seated && { tap R b 0.4; sleep 3; echo "$(date +%T) NU act: B = off the beast"; }
SHOTS=0; graugfight "${M16_FIGHT_MIN:-18}" "Mission Complete|SUCCESS"
bow_ok && bow_away
echo "$(date +%T) m16: shots $SHOTS, stuns $(cnt '^BOW graugstun t='); $(obj "$M0" 5)"
moments "$M0" 'STRUGGLE|RIDE t=.*(dom|offers)|OBJECTIVE t=.*shown' 12
