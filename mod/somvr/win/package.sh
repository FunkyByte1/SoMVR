#!/usr/bin/env bash
# win/package.sh - a native Windows player's package (W1, docs/re-winport.md 3.2): build/win/package/x64/, whose files go
# next to ShadowOfMordor.exe (the game's x64 folder). win/build.sh first; the third-party DLLs from
# scripts/fetch_win_redist.sh (third_party/win-redist). Contents:
#   xinput9_1_0.dll          the mod's host (the game loads it as its XInput; it forwards XInput to Windows' own)
#   somvr_logic.dll          the mod's game side (the host loads it: docs/re-xr-pipeline.md 156)
#   d3d11.dll, dxgi.dll      DXVK 2.7.1: the game's D3D11 on Vulkan, so the mod's Vulkan layer sees its frames
#   vulkan-1.dll             the Khronos Vulkan loader 1.4.341 (loads the layer below, then the GPU's driver)
#   openxr_loader.dll        the Khronos OpenXR loader 1.1.61 (finds the PC VR runtime: SteamVR, Quest Link, ...)
#   somvr/VkLayer_somvr_openxr.dll + .json   the mod's Vulkan layer (the XR session, the eyes, the menus, the hands)
#   somvr/somvr.env          the switches (scripts/headset-build26.env's, the test-only ones out); somvr/README.txt;
#   somvr/licenses/          DXVK's, the Vulkan loader's, OpenXR's, MinHook's, the VR hands' model's (Amazon, MIT: built
#                            into the layer from assets/generic-hand)
set -euo pipefail
W=$(cd "$(dirname "$0")" && pwd)
D=$(cd "$W/.." && pwd)
B=$D/build/win
R=${WIN_REDIST:-$D/../../third_party/win-redist}
P=$B/package/x64
[ -f "$B/xinput9_1_0.dll" ] && [ -f "$B/somvr_logic.dll" ] && [ -f "$B/layer/VkLayer_somvr_openxr.dll" ] || { echo "win/build.sh first"; exit 2; }
for f in dxvk-2.7.1/x64/d3d11.dll dxvk-2.7.1/x64/dxgi.dll vulkan-1.4.341.0/x64/vulkan-1.dll openxr-1.1.61/x64/openxr_loader.dll; do
  [ -f "$R/$f" ] || { echo "no $R/$f (scripts/fetch_win_redist.sh)"; exit 2; }
done
rm -rf "$B/package"
mkdir -p "$P/somvr/licenses"
cp "$B/xinput9_1_0.dll" "$B/somvr_logic.dll" "$R/dxvk-2.7.1/x64/d3d11.dll" "$R/dxvk-2.7.1/x64/dxgi.dll" "$R/vulkan-1.4.341.0/x64/vulkan-1.dll" \
   "$R/openxr-1.1.61/x64/openxr_loader.dll" "$P/"
cp "$B/layer/VkLayer_somvr_openxr.dll" "$B/layer/VkLayer_somvr_openxr.json" "$P/somvr/"
cp "$R/dxvk-2.7.1/LICENSE.txt" "$P/somvr/licenses/DXVK.txt"
cp "$R/vulkan-1.4.341.0/LICENSE.txt" "$P/somvr/licenses/Vulkan-Loader.txt"
cp "$R/openxr-1.1.61/LICENSE.txt" "$P/somvr/licenses/OpenXR-SDK.txt"
cp "$D/../../third_party/minhook-1.3.4/LICENSE.txt" "$P/somvr/licenses/MinHook.txt"
cp "$D/assets/generic-hand/LICENSE.md" "$P/somvr/licenses/generic-hand-model.txt"
# the switches: the headset build's, without the headless-test and Linux-session ones
{ echo "# somvr.env - Shadow of Mordor VR's switches (K=V; '#' = off). From scripts/headset-build26.env."
  grep -E '^SOMVR_[A-Z0-9_]+=' "$D/../../scripts/headset-build26.env" |
    grep -vE '^SOMVR_(XR_DUMP|XR_VM_TEST|XR_MARK_LAYERS|MARK_VOICE|MARK_VOICE_S|LOAD_VOICES|QUIT|HANDS_TEST|XR_TEST)=' |
    grep -vE '^SOMVR_(LIVE|LIVE_TTS)=|@RUN@' # b37: the live loop (this PC's lane and inbox) and a run folder's paths
} > "$P/somvr/somvr.env"                     # (@RUN@: only our sessions and the harness fill it in) are not a player's
cp "$W/README-Windows.txt" "$P/somvr/README.txt"
sed -i 's/$/\r/' "$P/somvr/README.txt" "$P/somvr/somvr.env"   # Windows line ends
(cd "$P" && find . -type f | sort | xargs sha256sum) > "$B/package/SHA256SUMS"
echo "package: $P ($(find "$P" -type f | wc -l) files, $(du -sh "$P" | cut -f1))"
