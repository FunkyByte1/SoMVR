#!/bin/bash
# hands-drive.sh - the hands/sword helper's live words and both-eye marks for one headless boot (SOMVR_HANDS_TEST=1,
# SOMVR_MARK=1). Waits for the newest boot-check run of this worktree (HANDS_WT, default: the worktree this script is
# in), then for the practice arrival, then runs the steps: each `m NAME` is a bug mark (RUN/marks/<time>_<n>/eyes.jpg =
# both eyes from the layer, flat.jpg, the log tails; the note = NAME, listed in RUN/marks.log). Ends with a quit.
# Grips: `hands test L|R x y z rx ry rz` = cm right/up/ahead of the head's yaw frame, deg about the OpenXR grip's X Y Z
# (0 -90 0 = the right fist axis pointing right, 90 0 0 = up, 45 0 0 = ahead and up 45).
# Run 1 (logs/boot-check-20261023-161800) compared the hide modes; this is the line mode's run.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${HANDS_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
for i in $(seq 1 150); do grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || { echo "no arrival"; echo quit >> "$R/cmd"; exit 1; }
echo "arrived $(date +%T)"
sleep 8
w() { printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep 0.8; }
m() { sleep "${2:-1.5}"; w "mark $1"; sleep 4; }
# 1. both hands in view, the fist axes ahead (the default hide mode: line)
w "hands test R 18 -28 38 0 0 0"
w "hands test L -18 -28 38 0 0 0"
m h01-line-both 2
# 2. close: the right wrist's seam (fist axis right), the back of the left hand (its glow)
w "hands test R 6 -14 26 0 -90 0"; m h02-right-close
w "hands test R 18 -28 38 0 0 0"
w "hands test L -8 -20 30 0 90 0"; m h03-left-back
w "hands test L -12 -8 32 90 0 0"; m h04-left-up
# 3. no hands (the IK lets go: the hands hidden too)
w "hands test off"; m h05-no-hands 2.5
# 4. the sword: the quick draw with the fist axis pointing right (the blade should run to the right from the fist)
w "hands test L -18 -28 38 0 0 0"
w "hands test R 2 -20 45 0 -90 0"
w "hands draw"; m h06-drawn-right 3
w "hands test R 12 -18 40 90 0 0"; m h07-fist-up
w "hands test R 15 -25 40 45 0 0"; m h08-natural
w "hands test R -4 -20 45 0 90 0"; m h09-fist-left
w "hands test R 2 -20 45 0 -90 0"; w "hands angle 25"; m h10-angle25
w "hands angle 0"
# 5. two hands: the left 12 cm behind the right on the blade line (fist axes right: behind = to the left)
w "hands test L -10 -20 45 0 -90 0"
w "hands squeeze L 1"; m h11-twohand-grab 1.5
w "hands test L -8 -32 45 0 -90 0"; m h12-twohand-left-low
w "hands test R 10 -8 45 0 -90 0"; m h13-twohand-right-up
w "hands squeeze L 0"; m h14-twohand-released
echo "steps done $(date +%T)"
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
