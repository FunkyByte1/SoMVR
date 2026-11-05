# nurn41/m12.sh - Queen of the Shore (Marwen), after the card's start (nurn41-drive.sh sources it; lib.sh's helpers).
# What it forces (GAME-STUDY-story 2.3): 1:30 to the Sea Fort unseen; Brand unlocked; brand 6 archers unseen; activate the
# branded; kill the fort's orcs and 20 reinforcements. The VR path: the stick + the sprint latch + A at the edges
# (route), the stealth brand by the left palm + the trigger, the rally by the raised left hand + the trigger, the cuts.
# Aids: the teleport per archer / per orc (a person's sneaking and fighting), `story invisible on` from the fort on
# (the drill's teleports would alert the sentries), the orcs' health 1 before the real cut.
M0=$(lines)
# a failed screen up (a restarted driver): A = Retry, a fresh 1:30 at the start
win && { tap R a 0.4; for i in $(seq 1 40); do inworld && ! win && break; sleep 0.5; done; echo "$(date +%T) retried: $(state)"; }
grep -aqE "OBJECTIVE t=.*'Objective' .*shown by the game" "$L" || waitre "OBJECTIVE t=.*shown by the game" 30
echo "$(date +%T) the objective: $(obj 0 2 | tail -1)"
# 1. the 1:30 run, a person's way (story2_route.py: no teleport); a Retry + the run from 26202 -11431 if Out of Time
WPS="38500,-21300;35100,-21450;35300,-19100;33960,-17938;33812,-17563;33217,-16953;33350,-16400;32646,-16108;31354,-15976;30693,-15937;28444,-14841;26202,-11431;23434,-8684;23100,450"
TRY=0; FORT=0
while [ $TRY -lt ${M12_TRIES:-2} ]; do TRY=$((TRY + 1)); l1=$(lines)
  route "$( [ $TRY = 1 ] && echo "$WPS" || echo "${WPS#*28444,-14841;}")" 100 run$TRY
  sleep 3; pics m12-run$TRY
  if since "$l1" "video=1|Sea Fort|tut_BrandUnlocked|Brand" | grep -q .; then FORT=1; echo "$(date +%T) RUN $TRY: the Sea Fort reached"; break; fi
  if win; then echo "$(date +%T) RUN $TRY: a window (the failed screen?): $(obj "$l1" 4)"; pics m12-fail$TRY; tap R a 0.4; sleep 6
     for i in $(seq 1 20); do inworld && ! win && break; sleep 1; done
     [ $TRY -lt ${M12_TRIES:-2} ] && { go 28444 2000 -14841; echo "$(date +%T) RUN $((TRY + 1)) from 28444 -14841 (aid: the start of the run skipped)"; }
  else echo "$(date +%T) RUN $TRY: neither ($(state))"; fi
done
