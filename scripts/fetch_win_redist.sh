#!/bin/bash
# fetch_win_redist.sh [DIR] - the third-party Windows DLLs a native Windows player's package ships next to
# ShadowOfMordor.exe (W1, docs/re-winport.md 3.2, mod/somvr/win/package.sh), pinned by the archive's sha256, into
# ShadowOfMordor/third_party/win-redist (git-ignored):
#   dxvk-2.7.1        doitsujin/dxvk v2.7.1 release (zlib licence): x64/d3d11.dll, x64/dxgi.dll - D3D11 on Vulkan, so our
#                     Vulkan layer sees the game's frames on Windows too
#   vulkan-1.4.341.0  LunarG's VulkanRT-X64-1.4.341.0-Components (the Khronos Vulkan loader, Apache-2.0): x64/vulkan-1.dll
#   openxr-1.1.61     KhronosGroup/OpenXR-SDK-Source release-1.1.61 openxr_loader_windows (Apache-2.0): x64 openxr_loader.dll
# A folder already there is kept. Needs curl, tar, unzip, sha256sum and access to github.com and sdk.lunarg.com.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../third_party/win-redist}
mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/somvr-win-redist.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

get() {   # NAME URL SHA256 FILE
  [ -f "$TMP/$4" ] && return 0
  echo "$1: downloading $2"
  curl -fsSL --retry 2 -o "$TMP/$4" "$2"
  local got; got=$(sha256sum "$TMP/$4" | cut -d' ' -f1)
  [ "$got" = "$3" ] || { echo "$1: sha256 $got, want $3: not installed" >&2; exit 1; }
}

if [ ! -d "$OUT/dxvk-2.7.1" ]; then
  get dxvk https://github.com/doitsujin/dxvk/releases/download/v2.7.1/dxvk-2.7.1.tar.gz \
    d85ce7c79f57ecd765aaa1b9e7007cb875e6fde9f6d331df799bce73d513ce87 dxvk.tar.gz
  mkdir -p "$TMP/dxvk" && tar -xzf "$TMP/dxvk.tar.gz" -C "$TMP/dxvk"
  mkdir -p "$OUT/dxvk-2.7.1/x64"
  cp "$TMP/dxvk/dxvk-2.7.1/x64/d3d11.dll" "$TMP/dxvk/dxvk-2.7.1/x64/dxgi.dll" "$OUT/dxvk-2.7.1/x64/"
  printf 'DXVK 2.7.1 (https://github.com/doitsujin/dxvk), zlib/libpng licence.\n' > "$OUT/dxvk-2.7.1/LICENSE.txt"
  echo "dxvk-2.7.1: installed"
fi
if [ ! -d "$OUT/vulkan-1.4.341.0" ]; then
  get vulkan https://sdk.lunarg.com/sdk/download/1.4.341.0/windows/VulkanRT-X64-1.4.341.0-Components.zip \
    20571dcc8292b80a47d68bbbfa4094d87044041f50f4e2e1f326a01e3732b226 vkrt.zip
  mkdir -p "$TMP/vk" && unzip -q "$TMP/vkrt.zip" -d "$TMP/vk"
  mkdir -p "$OUT/vulkan-1.4.341.0/x64"
  cp "$TMP/vk/VulkanRT-X64-1.4.341.0-Components/x64/vulkan-1.dll" "$OUT/vulkan-1.4.341.0/x64/"
  cp "$TMP/vk/VulkanRT-X64-1.4.341.0-Components/VulkanRT-License.txt" "$OUT/vulkan-1.4.341.0/LICENSE.txt"
  echo "vulkan-1.4.341.0: installed"
fi
if [ ! -d "$OUT/openxr-1.1.61" ]; then
  get openxr https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/download/release-1.1.61/openxr_loader_windows-1.1.61.zip \
    cfdca34b8cb4c2bef6dea05273daff987e58c97d5767c04f0a20b1029eaabe7f oxr.zip
  mkdir -p "$TMP/oxr" && unzip -q "$TMP/oxr.zip" -d "$TMP/oxr"
  mkdir -p "$OUT/openxr-1.1.61/x64"
  cp "$TMP/oxr/x64/bin/openxr_loader.dll" "$OUT/openxr-1.1.61/x64/"
  cp "$TMP/oxr/share/doc/openxr/LICENSE" "$OUT/openxr-1.1.61/LICENSE.txt"
  echo "openxr-1.1.61: installed"
fi
echo "win-redist ready: $OUT"
