#!/bin/bash
# chain27fx-c1.sh - som-b27-fx's SOMVR_FX_NEAR proof (docs/re-xr-pipeline.md 125), one headless boot without
# SOMVR_FX_TEST (the player's path): fx27-drive.sh nearv (the dagger's FX folded at a test radius in eye 0), then
# abil27d-drive.sh's brutalize (two rounds) with the fold in eye 0 only (`fxnear eye 0`: the kill's blood as the game
# draws it in eye 1, folded within 15..30 cm in eye 0 - one dump shows both), then the totals and quit.
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
echo "== nearv"; FX27_RUN=$R FX27_STEPS=nearv FX27_NOQUIT=1 "$D/fx27-drive.sh" || exit 1
printf '%s' "fxnear eye 0" > "$R/somvr.trigger"; sleep 1
echo "== brutal"; ABIL27D_RUN=$R ABIL27D_STEPS=brutal ABIL27D_BRUTAL="${C1_BRUTAL:-brutal1 brutal2}" ABIL27D_NOQUIT=1 "$D/abil27d-drive.sh"
for wd in "fxnear state" "fxnear eye -1" "endlogs"; do printf '%s' "$wd" > "$R/somvr.trigger"; sleep 1.5; done
sleep 1; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
