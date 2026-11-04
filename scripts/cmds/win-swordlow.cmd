# win-swordlow.cmd - the hit while hanging on Windows (winport-combat, 11-03): fling31-drive.sh's swordlow plan (hanging
# low on the wall, the sword drawn, the far orc comes, the chop at once: Linux boots 125711 / 182258). The ladder plan's
# sword step hangs at the ladder's top with the orc ~3.6 m below (unreachable: Linux 114233 / 115645 FAIL it too).
# Boot with SOMVR_EXTRA="SOMVR_CLIMB_HIT=hold": the far orc often opens from 1.1 m (ReactToSurprised, range 1-2 m), out of the
# chop's reach, and with the env's drop that hit ends the hang before the next cut (run 025133); held, cut 2 lands (030808).
trigger
wait 30
key Return
wait 6
key Return
waitlog 150 ctx=world
waitlog 60 arrived at
wait 8
xrtest btn R a 1
xrtest btn R a 0
xrtest head 0 0 0 yaw 0 pitch 0
wait 2
xrtest dump
driver fling31-drive.sh PLAN_B31=swordlow
