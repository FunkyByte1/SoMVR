# nurn41/m13a.sh - Big Game (Torvin), part 1 after the card's start: follow Torvin, the caragor's counter, the mount and
# the domination (what scripts/ride27c-steps.txt's bgr6.sh did on build 27c, in lib.sh's acts).
# Forces (GAME-STUDY-story 2.3): the caragor counter, the mount, the domination, riding; archers; catch the runner;
# save Torvin. VR path: the blade held across the caragor's leap (`fight guard learn`: the test blade's guard = the
# game's counter, SOMVR_RIDE_COUNTER), the left hand reached out + the grip closed at the offer = the mount, the grip
# kept closed = the domination won (SOMVR_RIDE_DOM_HOLD_MS). Aids: the follow by trips after Torvin's marker (a person
# walks behind him), the trip to the caragors' ground.
M0=$(lines)
invis off
# 1. follow Torvin: trips after his marker ('ObjectiveGuttered_ALLMaps') until it is gone 4 rounds
miss=0
for i in $(seq 1 75); do
  l0=$(lines); w "objective status" 1.2
  p=$(since "$l0" "^OBJECTIVE status marker .*'ObjectiveGuttered_ALLMaps'" | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
  ptut
  [ -z "$p" ] && { miss=$((miss + 1)); [ $miss -ge 4 ] && break; sleep 2; continue; }; miss=0
  [ $((i % 10)) = 1 ] && echo "$(date +%T) follow $i: Torvin at $p"
  set -- $p; go "$1" $(( $2 + 100 )) "$3"; sleep 1
done
echo "$(date +%T) the follow over: $(obj "$M0" 3)"
# 2. the caragor: the sword drawn, the guard (the test blade blocks what comes), the left hand forward, the beasts' ground
inworld || { tap R a 0.4; sleep 2; }
draw; w "hands test R 30 -10 45 0 -35 0" 0.4; w "fight guard learn" 0.4; w "ride guard on" 0.3; w "ride face on" 0.3
w "hands test L -25 -35 30 0 0 0" 0.4
go ${CARAGOR_AT:--34800 1917 -8452}
MOUNTED=0
for round in 1 2 3 4 5; do
  n0=$(cnt 'DEFENCE countered.*JumpAttack')
  for i in $(seq 1 480); do [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] && break; sleep 0.25; [ $((i % 8)) = 0 ] && ptut; done
  [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] && echo "$(date +%T) the leap countered by the blade (round $round)" || { echo "$(date +%T) no counter in 2 min (round $round)"; continue; }
  l1=$(lines); sleep 3.1
  for k in $(seq 1 10); do since "$l1" "^RIDE t=.* the game offers the mount" | grep -q . && break; w "stick 0 1 0 0.4" 0.05; sleep 0.7; done
  w "stick 0 0 0 0.1" 0.05
  since "$l1" "^RIDE t=.* the game offers the mount" | grep -q . || echo "$(date +%T) no mount offered after the counter (round $round)"
  w "hands squeeze L 1" 0.3; echo "$(date +%T) NU act: the left hand closed (the mount; kept closed = the domination)"
  for i in $(seq 1 100); do since "$l1" "^RIDE t=.* mounted|PC_Warg_Domination_Fail" | grep -q . && break; sleep 0.1; done
  since "$l1" '^RIDE t=.* mounted' | grep -q . && { MOUNTED=1; break; }
  w "hands squeeze L 0" 0.2; echo "$(date +%T) NOT mounted (round $round): $(since "$l1" '^RIDE t=' | tail -2 | cut -c1-200 | tr '\n' '|')"
done
echo "$(date +%T) MOUNTED=$MOUNTED"
w "ride face off" 0.2; w "ride guard off" 0.2
l1=$(lines); for i in $(seq 1 40); do since "$l1" "^RIDE t=.* seated on" | grep -q . && break; sleep 0.25; done; sleep 1.5
w "hands squeeze L 0" 0.3; x "head 0 0 0 yaw 0 pitch -10 s 0.3"; pics m13-seated
since "$M0" '^RIDE t=.*(dom|struggle|mounted|seated)|STRUGGLE' | head -6 | cut -c1-220
