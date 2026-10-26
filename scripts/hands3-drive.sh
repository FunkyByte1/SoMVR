#!/bin/bash
# hands3-drive.sh - round 3 of the hands (the fingers, SOMVR_FINGERS=1): the live words and both-eye marks for one
# headless boot (SOMVR_HANDS_TEST=1, SOMVR_MARK=1). Copied from hands-drive.sh (round 2): waits for the newest boot-check
# run of this worktree (HANDS_WT, default: the worktree this script is in), then for the practice arrival, then runs the
# steps of HANDS3_STEPS (default: all): each `m NAME` is a bug mark (RUN/marks/<time>_<n>/eyes.jpg = both eyes from the
# layer; the note = NAME). Ends with a quit.
# Grips: `hands test L|R x y z rx ry rz` = cm right/up/ahead of the head's yaw frame, deg about the OpenXR grip's X Y Z
# (0 -90 0 = the right fist axis pointing right, 90 0 0 = up, 45 0 0 = ahead and up 45, 0 0 90 = turned about the fist
# axis; 0 90 0 = the right palm toward the eyes, 0 -90 0 the left's). Fingers: `hands squeeze L|R V` (middle/ring/pinky; also the slots and two hands), `hands trigger L|R V` (the
# index), `hands thumb L|R 0|1` (the thumb's touch); -1 = the controller's again. `hands fingers` = a dump in somvr.log.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
WT=${HANDS_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${HANDS3_STEPS:-empty mirror palms sword two}
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
fing() { # fing L|R squeeze trigger thumb
  w "hands squeeze $1 $2"; w "hands trigger $1 $3"; w "hands thumb $1 $4"
}
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w "hands fingers" # the idle's fingers and the bind pose in somvr.log
if has empty; then
  # 1. empty hands in front, the fist axes ahead (palms facing each other): open, a fist, pointing, a thumb up
  w "hands test R 14 -24 34 0 0 0"
  w "hands test L -14 -24 34 0 0 0"
  fing R 0 0 0; fing L 0 0 0; m f01-open 2
  fing R 1 1 1; fing L 1 1 1; m f02-fist
  fing R 1 0 1; fing L 1 0 1; m f03-point
  fing R 1 1 0; fing L 1 1 0; m f04-thumbup
  fing R 0 0.5 1; fing L 0 0.5 1; m f05-half
fi
if has mirror; then
  # 2. the palms toward the eyes (turned about the fist axes, mirrored): a fist, open; the game's own fingers to compare
  w "hands test R 10 -20 30 0 0 90"
  w "hands test L -10 -20 30 0 0 -90"
  fing R 1 1 1; fing L 1 1 1; m f06-fist-palms 2
  fing R 0 0 0; fing L 0 0 0; m f07-open-palms
  w "hands fingers off"; m f08-game-fingers
  w "hands fingers on"
  w "hands test R 10 -20 30 0 0 -90"
  w "hands test L -10 -20 30 0 0 90"
  fing R 1 1 1; fing L 1 1 1; m f09-fist-backs 2
fi
if has palms; then
  # 2b. the mirror proof: both hands raised, the palms toward the eyes (R turned +90 about the grip's Y, L -90: mirrored),
  # then the backs; a fist, open, pointing, the thumb resting on an open hand
  w "hands test R 9 -12 30 0 90 0"
  w "hands test L -9 -12 30 0 -90 0"
  fing R 1 1 1; fing L 1 1 1; m p01-palms-fist 2
  fing R 0 0 0; fing L 0 0 0; m p02-palms-open
  fing R 1 0 1; fing L 1 0 1; m p03-palms-point
  fing R 0 0 1; fing L 0 0 1; m p04-palms-thumbrest
  w "hands test R 9 -12 30 0 -90 0"
  w "hands test L -9 -12 30 0 90 0"
  fing R 1 1 1; fing L 1 1 1; m p05-backs-fist 2
  fing R 0 0 0; fing L 0 0 0; m p06-backs-open
  # the side of the right hand (fist axis up, the palm to the left), closing in three steps
  w "hands test L -30 -40 20 0 0 0"
  w "hands test R 0 -14 28 90 0 0"
  fing R 0 0 0; m p07-side-open 2
  fing R 0.5 0.5 1; m p08-side-half
  fing R 1 1 1; m p09-side-fist
fi
# release the test fingers (the controller's: none headless = open)
fing R -1 -1 -1; fing L -1 -1 -1
if has angle; then
  # 3b. the sword angle: the settings file's SWORD_ANGLE (20) at the start, then the live word back to 0, then 20 again
  w "hands test L -18 -28 38 0 0 0"
  w "hands test R 2 -20 45 0 -90 0"
  w "hands draw"; sleep 3
  m a01-file-angle 1
  w "hands angle 0"; m a02-angle0
  w "hands angle 20"; m a03-angle20
  w "hands test R 8 -16 28 0 -90 180"; m a04-angle20-under
  w "hands angle 0"; m a05-angle0-under
fi
if has sword; then
  # 3. the sword: the quick draw with the fist axis pointing right; the right fist on the hilt whatever the buttons
  w "hands test L -18 -28 38 0 0 0"
  w "hands test R 2 -20 45 0 -90 0"
  w "hands draw"; sleep 3
  w "hands fingers" # the game's grip on the hilt
  m f10-sword-right 1
  w "hands test R 15 -25 40 45 0 0"; m f11-sword-natural
  w "hands test R 8 -16 28 0 -90 0"; m f12-sword-close
  w "hands test R 8 -16 28 0 -90 180"; m f13-sword-close-under
fi
if has two; then
  # 4. two hands: the left 12 cm behind the right on the blade line (fist axes right: behind = to the left), a fist
  w "hands test R 2 -20 45 0 -90 0"
  w "hands test L -10 -20 45 0 -90 0"
  w "hands squeeze L 1"; m f14-twohand 1.5
  w "hands test L -8 -32 45 0 -90 0"; m f15-twohand-low
  w "hands test R 8 -14 32 0 -90 0"; w "hands test L -4 -14 32 0 -90 0"; m f16-twohand-close
  w "hands squeeze L 0"; m f17-released
fi
if has stub; then
  # 5. the wrist's end: line (build 6) / cuff (a forearm stub rigid with the hand) / forearm, live in the same poses
  hide3() { for hm in ${HANDS3_HIDES:-line cuff forearm}; do w "hands hide $hm"; m "$1-$hm" 1; done; }
  w "hands test R 15 -25 40 0 0 0"; w "hands test L -15 -25 40 0 0 0"
  fing R 0.3 0 1; fing L 0.3 0 1
  hide3 s01-forward
  w "hands test R 9 -12 30 0 90 0"; w "hands test L -9 -12 30 0 -90 0"; hide3 s02-raised
  w "hands test L -30 -40 20 0 0 0"; w "hands test R 6 -14 26 0 -90 0"; hide3 s03-close-behind
  w "hands test R 0 -14 28 90 0 0"; hide3 s04-side
  fing R -1 -1 -1; fing L -1 -1 -1
  w "hands test L -18 -28 38 0 0 0"; w "hands test R 2 -20 45 0 -90 0"
  w "hands draw"; sleep 3
  w "hands test R 15 -25 40 45 0 0"; hide3 s05-sword
  w "hands test R 2 -20 45 0 -90 0"; w "hands test L -10 -20 45 0 -90 0"; w "hands squeeze L 1"; sleep 1
  hide3 s06-twohand
  w "hands squeeze L 0"
  w "hands hide line"
fi
if has dagger; then
  # 6. the dagger (the left hip slot): the left hand there, open, then a squeeze draws it; up into view: a left fist
  w "hands hide cuff"
  w "hands test L -18 -62 -5 0 0 0"; w "hands squeeze L 0"; sleep 1.5
  w "hands squeeze L 1"; sleep 1.5
  w "hands test L -12 -20 32 0 0 0"; m d01-dagger 1.5
  w "hands squeeze L 0"; m d02-dagger-released
  w "hands test L -10 -14 28 0 -90 0"; m d03-dagger-back
  w "hands hide line"
fi
echo "steps done $(date +%T)"
sleep 3
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
