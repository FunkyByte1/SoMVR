#!/bin/bash
# fixes22-drive.sh - build 22 (som-b22-fixes, docs/re-xr-pipeline.md 93): one headless boot (scripts/headless_vr.sh with
# scripts/headset-build22.env, SOMVR_HANDS_TEST=1, the layer's test file) driving, in order:
#   kill22-drive.sh  SD_STEPS (default "walk0 walk"): the sword held forward through the sneak - build 21 (`stealth
#                    carried off`): the touch and the alert; build 22: carried, unaware, then the thrust kills (93.4)
#   ground           at the practice meadow's open grass (where the kill rounds end): a chop into it and the blade lowered
#                    slowly 35 cm past it, `surface ground off` (build 21: no ray meets it - the blade goes in, no sound)
#                    then on (the ray down at the tip: the blade stops on it, the clank, the sound, the sparks) (93.2)
#   finish22-drive.sh FD_STEPS (default "blink tp"): the interrogated orc's Eliminate by the sword's stab, the game's
#                    finisher carrying Talion off the held view - `bmreturn off` (the re-seat's blink) then tp (his body
#                    put under the held view, no blink) (93.3)
#   down22-drive.sh  (step down, last) SOMVR_CLIMB_DOWN_OVER on real walls (93.5)
# F22_STEPS (default "kill ground finish") picks. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")
STEPS=${F22_STEPS:-kill ground finish}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
t() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.6; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/f22-steps.txt"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
DIR=$(cd "$(dirname "$0")" && pwd)
if has kill; then
  SD_RUN=$R NOQUIT=1 SD_STEPS="${SD_STEPS:-walk0 walk}" "$DIR/kill22-drive.sh" || bail "kill22 stopped"
else
  sleep 8; w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6
  w "hands squeeze R 0" 0.5   # the sword drawn (kill22's draw_slot)
fi
if has ground; then
  echo "== ground"; w "practice remove" 1.5; t "head 0 0 0 s 0.4"; w "crouch stand" 0.5; w "practice back" 6; w "practice go" 7
  # (runs 20261029-003708 / -032839: the hand stops ~55 cm under the eyes - its reach - and the blade turns at most ~30-45
  # deg down in these poses, so sword20's poses (and ducked 60 cm) left the tip 30-60 cm over the ground: knelt, the eyes
  # ~70 cm over it, the grip ~35 cm over it, the tip ~20 cm under it)
  t "head 0 -110 0 s 0.6"; sleep 1.2
  for g in off on; do
    w "surface ground $g" 0.4; c0=$(cnt '^SURF t=.* blade pivot (clank|touch|meeting|goes in)')
    w "hands test R 25 -10 40 -30 0 0" 1.2; w "hands swing R 25 -35 40 85 0 0 300" 1.5; d "ground-$g-chop"
    w "hands test R 25 -20 40 40 0 0" 1.2; w "hands swing R 25 -40 40 85 0 0 2000" 2.6; d "ground-$g-pressed"; m "ground-$g" 0.1
    w "hands test R 22 -35 -30 0 180 0" 1
    echo "ground $g: $(( $(cnt '^SURF t=.* blade pivot (clank|touch|meeting|goes in)') - c0 )) blade lines"
    grep -E '^SURF t=.* blade pivot (clank|touch|meeting|goes in)' "$R/somvr.log" | tail -n +$((c0 + 1)) | head -4 | cut -c1-260
  done
  grep -E '^SURF t=.* hit obj=.*(ground|Grass)' "$R/somvr.log" | tail -2 | cut -c1-200
  grep -E '^SURF t=.* ground probe' "$R/somvr.log" | tail -3 | cut -c1-240; t "head 0 0 0 s 0.5"
fi
if has finish; then
  FD_RUN=$R NOQUIT=1 NODRAW=1 FD_STEPS="${FD_STEPS:-blink tp}" "$DIR/finish22-drive.sh" || bail "finish22 stopped"
fi
if has down; then # last: down22-drive.sh quits the game at its end
  PIN_RUN=$R "$DIR/down22-drive.sh"; exit $?
fi
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
