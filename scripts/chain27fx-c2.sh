#!/bin/bash
# chain27fx-c2.sh - som-b27-fx's SOMVR_FX_NEAR proof, round 2 (docs/re-xr-pipeline.md 125), one headless boot without
# SOMVR_FX_TEST: fx27-drive.sh nearb (our slashes on an orc with the fold at a test radius of 150,200 cm in eye 0 only:
# his blood gone in eye 0, the game's in eye 1), abil27d-drive.sh's brutalize (one round, the fold 15,30 in eye 0 only),
# fx27-drive.sh nearv (the dagger's FX at the hip at 30,60 in eye 0), the totals, quit.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
D=$(cd "$(dirname "$0")" && pwd -P)
WT=$(basename "$(cd "$D/../.." && pwd -P)")
T0=$(date +%s); R=
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
echo "== nearb"; FX27_RUN=$R FX27_STEPS=nearb FX27_NOQUIT=1 "$D/fx27-drive.sh" || exit 1
printf '%s' "fxnear eye 0" > "$R/somvr.trigger"; sleep 1
echo "== brutal"; ABIL27D_RUN=$R ABIL27D_STEPS=brutal ABIL27D_BRUTAL="${C2_BRUTAL:-brutal1}" ABIL27D_NOQUIT=1 "$D/abil27d-drive.sh"
echo "== nearv"; FX27_RUN=$R FX27_STEPS=nearv FX27_NOQUIT=1 "$D/fx27-drive.sh"
for wd in "fxnear state" "fxnear eye -1" "endlogs"; do printf '%s' "$wd" > "$R/somvr.trigger"; sleep 1.5; done
sleep 1; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
