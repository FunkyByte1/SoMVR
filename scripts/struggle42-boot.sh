#!/bin/bash
# struggle42-boot.sh - som-b42-struggle: one headless Windows boot of MOD with scripts/struggle42-drive.sh PINNED to the
# run this boot's headless_vr.sh prints (never found by a name: on 11-06 a driver that matched SG_WT=build-win10 drove
# another helper's build-win10 boot that started a second later). Usage (outside the sandbox, rus.sh in front for rus):
#   [SG_* ...] struggle42-boot.sh MOD      (HEADLESS_* / WIN_* / SOMVR_EXTRA / LIVE_ROOT as for headless_vr.sh)
HERE=$(cd "$(dirname "$0")" && pwd); MOD=${1:?MOD}
O=$(mktemp /tmp/sg42-boot.XXXXXX)
HEADLESS_SLOT=${HEADLESS_SLOT:-auto} HEADLESS_OWNER=${HEADLESS_OWNER:-som-b42-struggle} HEADLESS_SLOT_WAIT=${HEADLESS_SLOT_WAIT:-3600} \
  setsid -w bash "$HERE/headless_vr.sh" --win "$MOD" > "$O" 2>&1 &
BP=$!
trap 'kill -TERM -- -$BP 2>/dev/null; kill $DP 2>/dev/null' TERM INT
R=""
while kill -0 $BP 2>/dev/null; do
  R=$(grep -m1 -oE '^windows: [^ ]+' "$O" | cut -c10-); [ -n "$R" ] && [ -d "$R" ] && break; R=""; sleep 2
done
if [ -n "$R" ]; then echo "$(date +%T) run $R (the driver pinned to it)"
  ( cd "$HERE" && SG_RUN="$R" bash ./struggle42-drive.sh ) > "$R/sg42-driver.log" 2>&1 & DP=$!
fi
wait $BP; RC=$?
[ -n "${DP:-}" ] && { sleep 5; kill $DP 2>/dev/null; wait $DP 2>/dev/null; }
grep -vE 'environ: Permission|^\[..:..:..\] shot ' "$O" | tail -30; rm -f "$O"
exit $RC
