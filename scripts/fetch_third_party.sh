#!/bin/bash
# fetch_third_party.sh [DIR] - download the C headers the build needs into ShadowOfMordor/third_party (git-ignored), so
# a fresh clone builds (mod/somvr/build.sh, mod/somvr/vklayer/build.sh). Headers and licences only, from the Khronos
# repositories on GitHub, pinned:
#   Vulkan-Headers-1.4.341  KhronosGroup/Vulkan-Headers tag v1.4.341 (matches the system loader 1.4.341):
#                           include/, LICENSE.md, LICENSES/, README.md
#   OpenXR-SDK-1.1.61       KhronosGroup/OpenXR-SDK tag release-1.1.61: include/openxr/*.h, LICENSE
# Each folder is checked against a pinned hash of its files (names and contents, so a re-packed tarball still passes)
# before it is moved into place; the hashes are those of the copies the builds were made with (2026-10-18), and the
# downloads were checked against them 2026-10-25. A folder that is already there is checked, never replaced.
# Needs curl, tar, sha256sum and access to codeload.github.com. DIR (tests): fetch there instead of third_party/.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TP=${1:-$HERE/../third_party}
mkdir -p "$TP" && TP=$(cd "$TP" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/somvr-third-party.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

tree_hash() { (cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) | sha256sum | cut -d' ' -f1; }

fetch() {   # NAME URL HASH PATH... (paths in the archive's top folder; globs allowed)
  local name=$1 url=$2 want=$3 top got p k; shift 3
  if [ -d "$TP/$name" ]; then
    [ "$(tree_hash "$TP/$name")" = "$want" ] && { echo "$name: already there, checked"; return 0; }
    echo "$name: already there, but its files differ from the pinned set; move it away and run again" >&2; return 1
  fi
  echo "$name: downloading $url"
  curl -fsSL --retry 2 -o "$TMP/$name.tar.gz" "$url"
  mkdir -p "$TMP/x-$name" "$TMP/$name" && tar -xzf "$TMP/$name.tar.gz" -C "$TMP/x-$name"
  top=$(echo "$TMP/x-$name"/*/)   # the archive's one top folder
  for k in "$@"; do
    for p in "$top"/$k; do
      [ -e "$p" ] || { echo "$name: $k missing in the download" >&2; return 1; }
      mkdir -p "$TMP/$name/$(dirname "$k")" && cp -R "$p" "$TMP/$name/$(dirname "$k")/"
    done
  done
  got=$(tree_hash "$TMP/$name")
  [ "$got" = "$want" ] || { echo "$name: the download differs from the pinned set (hash $got); not installed" >&2; return 1; }
  mv "$TMP/$name" "$TP/$name" && echo "$name: installed in $TP/$name"
}

fetch Vulkan-Headers-1.4.341 https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.4.341 \
  d440df3b2b2d025148038e16e861450837f5d725f77e900e9ba1b5178ad2ca25 include LICENSE.md LICENSES README.md
fetch OpenXR-SDK-1.1.61 https://codeload.github.com/KhronosGroup/OpenXR-SDK/tar.gz/refs/tags/release-1.1.61 \
  049df0f2d0cf910746fa10a82ff8bcb576e8742c6c275017bbd24902052830ae 'include/openxr/*.h' LICENSE
echo "third_party ready: $TP"
