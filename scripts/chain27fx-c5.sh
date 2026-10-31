#!/bin/bash
# chain27fx-c5.sh - som-b27-fx's last proof (docs/re-xr-pipeline.md 125), one headless boot without SOMVR_FX_TEST:
# abil27d-drive.sh's brutalize (one round, up to 8 thrusts) with SOMVR_FX_NEAR's fold at 15,30 cm in eye 0 only (the
# kill's blood as the game draws it in eye 1, folded in eye 0: one dump shows both), the totals, quit.
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
for i in $(seq 1 150); do grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3; done
sleep 6; printf '%s' "fxnear eye 0" > "$R/somvr.trigger"; sleep 1
echo "== brutal"; ABIL27D_RUN=$R ABIL27D_STEPS=brutal ABIL27D_BRUTAL="brutal1" ABIL27D_THRUSTS="180 260 340 180 260 340 220 300" ABIL27D_NOQUIT=1 "$D/abil27d-drive.sh"
for wd in "fxnear state" "fxnear eye -1" "endlogs"; do printf '%s' "$wd" > "$R/somvr.trigger"; sleep 1.5; done
sleep 1; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
