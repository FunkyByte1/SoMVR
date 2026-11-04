# win-kills1.cmd - win-combat1.cmd plus the kills checks (winport-combat, 11-03): `kills vt` (the live vtable pointers
# of the objects whose slots the combat code uses) with the orc there and after its death, shots through the kill; the rest as win-combat1.cmd.
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
wait 4
trigger kills vt
wait 1
trigger execute face
wait 2
xrtest dump
trigger fight guard frame head
wait 1
trigger fight guard learn
wait 20
trigger execute face
wait 1
trigger fight guard path
wait 20
mark guard
xrtest dump
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
xrtest dump
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 2
trigger execute face
wait 1
trigger hands test R 40 -8 35 0 -90 90
wait 1
trigger hands swing R -20 -18 62 0 90 90 300
wait 1
shot k1
wait 1
shot k2
wait 1
shot k3
xrtest dump
wait 2
shot k5
trigger kills vt
wait 1
trigger kills status
wait 4
shot k10
xrtest dump
