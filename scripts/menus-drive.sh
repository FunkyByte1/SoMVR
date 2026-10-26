#!/bin/bash
# menus-drive.sh - the menus helper's test hands, dumps and keys for one headless boot (build 6: the card, the wrist
# menu, the watch, the pause menu's board, touch and the laser, the minimap's turn). Needs the layer's SOMVR_XR_TEST:
#   HEADLESS_LAUNCH=old SOMVR_EXTRA="SOMVR_XR_MENU=front SOMVR_XR_MINIMAP=wrist SOMVR_MINIMAP=head SOMVR_XR_CARD=on
#     SOMVR_XR_TOUCH=on SOMVR_XR_TEST=@RUN@/xr-test.txt" scripts/headless_vr.sh <worktree>/ShadowOfMordor/mod/somvr
# and this script at the same time (both outside the sandbox). It finds the newest run of that worktree (WT, default
# this script's worktree), waits for the practice arrival, writes RUN/xr-test.txt one step at a time (the layer runs a
# changed file's lines once; ../mod/somvr/somvr_menus.h 4), appends keys to RUN/cmd and ends with `quit`.
# Afterwards: scripts/xr_compose.py RUN (both eyes with the quads, per dump).
HERE=$(cd "$(dirname "$0")" && pwd)
LOGS=$(cd "$HERE/../logs" 2>/dev/null && pwd || echo /home/bara/Projects/flat2vr/ShadowOfMordor/logs)
[ -d "$LOGS" ] || LOGS=/home/bara/Projects/flat2vr/ShadowOfMordor/logs
WT=${WT:-$(basename "$(cd "$HERE/../.." && pwd)")}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt "$LOGS"/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run of $WT"; exit 1; }
echo "run $R"
for i in $(seq 1 150); do grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3; done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || { echo "no arrival"; echo quit >> "$R/cmd"; exit 1; }
echo "arrived $(date +%T)"
sleep 4
N=0
# t LINE...: one step (the file replaced whole: its lines run once, in order)
t() { N=$((N + 1)); { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xr-test.tmp" && mv "$R/xr-test.tmp" "$R/xr-test.txt"
      echo "$(date +%T) step $N: $*"; sleep 0.5; }
k() { echo "key $1" >> "$R/cmd"; echo "$(date +%T) key $1"; }
d() { t "dump"; sleep "${1:-1.5}"; }
# 1. the arrival card: as it comes, the fingertip hovering its X, a press on it (a front approach, 0.47 m/s)
d 2
t "hand R touch panel 0.93 0.042 6"; sleep 0.6
t "hand R touch panel 0.93 0.042 1.2 s 0.3"; sleep 0.6
d
t "hand R touch panel 0.93 0.042 -1.0 s 0.15"; sleep 0.8
t "hand R touch panel 0.93 0.042 4 s 0.2"; sleep 0.6
t "hand R off"; sleep 1
# 2. the watch: the left forearm across the chest, the back of the wrist toward the eyes
t "hand L head 5 -16 32 0 -90 -130 s 0.4"; sleep 1.5
d
# 3. the minimap turns with the head: three snap/smooth turns to the right (the watch held), a dump after each
for i in 1 2 3; do t "stick R 1 0"; sleep 0.6; t "stick R 0 0"; sleep 1.2; d; done
# 4. the wrist menu: the palm toward the eyes 1 s (the gesture), the panel; the right fingertip on its Controls tab
t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 2
d
t "hand R touch panel 0.723 0.042 5"; sleep 0.6
t "hand R touch panel 0.723 0.042 1.0 s 0.3"; sleep 0.6
t "hand R touch panel 0.723 0.042 -1.0 s 0.15"; sleep 0.6
t "hand R touch panel 0.723 0.042 4 s 0.2"; sleep 1
d
# ... the Settings tab back; a swipe across the rows (fast, sideways: no press); then from behind (no press)
t "hand R touch panel 0.16 0.042 4 s 0.3"; sleep 0.6
t "hand R touch panel 0.16 0.042 -1.0 s 0.15"; sleep 0.6
t "hand R touch panel 0.16 0.042 4 s 0.2"; sleep 0.8
t "hand R touch panel 0.1 0.5 0.8 s 0.3"; sleep 0.6
t "hand R touch panel 0.9 0.5 -0.8 s 0.12"; sleep 0.6
t "hand R touch panel 0.5 0.6 -4 s 0.3"; sleep 0.6
t "hand R touch panel 0.5 0.6 -0.8 s 0.3"; sleep 0.6
t "hand R touch panel 0.5 0.6 6 s 0.3"; sleep 0.6
d
# ... the fingertip resting near the Recentre button (the hover marker), then away
t "hand R touch panel 0.18 0.917 1.5 s 0.3"; sleep 1
d
t "hand R off"; sleep 0.5
# 5. the watch and the open menu: the wrist turned back over (does the panel go, does the watch come?)
t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.5
d 1
sleep 1.5
d
t "hand L off"; sleep 1.8
# 6. the pause menu on its board: Escape, the board; the fingertip hover at its centre, a press on its empty top
#    right; the laser (the trigger touched) at the centre; Escape again
k Escape; sleep 3
d 2
t "hand R touch board 0.5 0.5 5"; sleep 0.6
t "hand R touch board 0.5 0.5 1.0 s 0.3"; sleep 1
d
t "hand R touch board 0.92 0.08 4 s 0.3"; sleep 0.6
t "hand R touch board 0.92 0.08 -1.0 s 0.15"; sleep 0.6
t "hand R touch board 0.92 0.08 4 s 0.2"; sleep 0.6
t "hand R head 12 -18 20 11 17 0 s 0.3" "btn R trigger 0.3"; sleep 1
d
t "btn R trigger 0" "hand R off"; sleep 0.5
k Escape; sleep 3
d
sleep 2
echo quit >> "$R/cmd"
echo "$(date +%T) quit; dumps in $R (xr-layer.log.stereoN / hudN / vrmenuN)"
