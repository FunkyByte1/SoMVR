#!/bin/bash
# chain27fx-a2.sh - som-b27-fx's second audit boot (docs/re-xr-pipeline.md 125): three drivers on one headless boot -
# abil27d-drive.sh's brutalize (one round; eye 0 without the CPU particles: `fx hideeye 0` + `fx hide 1`, the blood's
# renderer), fx27-drive.sh's exec (eye 0 without the GPU particles), ride26-drive.sh's Shadow Mount and ride (it quits).
# Boot as fx27-drive.sh's (SOMVR_FX_TEST=1).
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
sleep 6
printf '%s' "fx hideeye 0" > "$R/somvr.trigger"; sleep 1; printf '%s' "fx hide 1" > "$R/somvr.trigger"; sleep 1
echo "== brutal"; ABIL27D_RUN=$R ABIL27D_STEPS=brutal ABIL27D_BRUTAL=brutal1 ABIL27D_NOQUIT=1 "$D/abil27d-drive.sh"
printf '%s' "fx hide 0" > "$R/somvr.trigger"; sleep 1
grep -q '^quit\|STOP' "$R/cmd" 2>/dev/null && exit 1
echo "== exec"; FX27_RUN=$R FX27_STEPS=exec FX27_NOQUIT=1 FX_HIDEEYE="0 2" "$D/fx27-drive.sh"
grep -q 'quit' "$R/cmd" 2>/dev/null && exit 1
echo "== ride"; R26_RUN=$R R26_STEPS="grant smount ride" "$D/ride26-drive.sh"
