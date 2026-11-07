#!/bin/bash
# knife37-run42.sh [TAG] - one boot of suite entry 372 (knife37-drive.sh, this worktree's) on build-win10, as final.sh
# boots it (WIN_RENDER=vr, winebus.sys off, build-win10's headset.env), in a game slot of its own (HEADLESS_SLOT=auto).
# Output: logs/coord/b37/knifedrive42-<TAG>/{drive,boot}.txt. Run with the sandbox off.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")/.." && pwd -P)            # this worktree's ShadowOfMordor/
TAG=${1:-run}; O=$SOM/logs/coord/b37/knifedrive42-$TAG; mkdir -p $O
B=$SOM/logs/build-win10; WT=build-win10; SC=$HERE/scripts
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
userin && { echo "the user is in VR: not run"; exit 1; }
export WIN_RENDER=${WIN_RENDER:-vr}
WDO="WINEDLLOVERRIDES=xinput9_1_0=n,b;d3d11,d3d10core,dxgi,d3d9=n;winepulse.drv=d;wineoss.drv=d;winealsa.drv=b;winebus.sys=d"
ENV="$(grep -E '^SOMVR_[A-Z0-9_]+=' $B/headset.env | tr '\n' ' ')"
T="SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 $WDO"
cd $SOM
env K37_WT=$WT K37_OUT=$O/boot.txt bash $SC/knife37-drive.sh > $O/drive.txt 2>&1 &
DP=$!
HEADLESS_SLOT=auto HEADLESS_OWNER=som-b42-knifedrive HEADLESS_SLOT_WAIT=3600 HEADLESS_HOLD=1200 SOMVR_EXTRA="$ENV $T" \
  setsid -w $SC/headless_vr.sh --win $B/ShadowOfMordor/mod/somvr > $O/boot.txt 2>&1 < /dev/null
for k in $(seq 1 60); do kill -0 $DP 2>/dev/null || break; sleep 1; done
kill -0 $DP 2>/dev/null && { pkill -TERM -P $DP; kill $DP 2>/dev/null; }
wait $DP 2>/dev/null
R=$(grep -h -m1 -oE '/[^ ]*logs/boot-check-[0-9-]+' $O/drive.txt $O/boot.txt | head -1)
echo "run: $R"; grep -E "PASS|FAIL|STOP|grab try" $O/drive.txt | cut -c1-200
[ -n "$R" ] && find "$R" -maxdepth 3 -type f \( -name '*.ppm' -o -name '*.pgm' \) -delete 2>/dev/null
