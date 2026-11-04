# win-move1.cmd - locomotion and the jump on the Windows build: jump22-drive.sh's stand / run / sprint plans (the stick's
# walk and run, the sprint latch, the snap turns that face him, the standing and running jumps) on this run
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
wait 4
driver jump22-drive.sh PLAN=stand~run~sprint
