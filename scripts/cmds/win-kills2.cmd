# win-kills2.cmd - the kill's half on Windows (winport-combat, 11-03): the meadow, the card, the test hands and the drawn
# sword as win-combat1.cmd, an infantry, then `kills play AI_Bip_PC_Standing_InstaKill_F 6` (the victim's half alone,
# its root/pelvis/neck/head traced every 0.1 s: KILLS play +.. / END), shots through it, a second infantry and
# `kills kill` with the same row (the kill path: the half, then the death at its moment), `kills vt` and status.
trigger
wait 30
key Return
wait 6
key Return
waitlog 150 ctx=world
waitlog 60 arrived at
wait 6
xrtest btn R a 1
wait 1
xrtest btn R a 0
wait 2
trigger hands test L -22 -35 25 0 0 0
wait 1
trigger hands test R 28 -40 20 0 0 0
wait 1
trigger hands test R 10 -10 -15 0 0 0
wait 1
trigger hands squeeze R 1
wait 1
trigger hands test R 30 -25 40 0 -90 0
wait 1
trigger hands squeeze R 0
wait 1
trigger hands test R 28 -40 20 0 0 0
wait 2
mark drawn
xrtest dump
wait 1
shot 07-drawn
trigger practice remove
wait 2
trigger practice spawn infantry
wait 5
trigger kills vt
wait 1
trigger execute face
wait 1
mark play
trigger kills play AI_Bip_PC_Standing_InstaKill_F 6
wait 1
shot p1
wait 1
shot p2
wait 1
shot p3
wait 4
shot p7
xrtest dump
trigger practice remove
wait 2
trigger practice spawn infantry
wait 6
trigger execute face
wait 1
mark kill
trigger kills kill AI_Bip_PC_Standing_InstaKill_F
wait 1
shot q1
wait 1
shot q2
wait 2
shot q4
wait 4
shot q8
trigger kills vt
wait 1
trigger kills status
wait 3
xrtest dump
