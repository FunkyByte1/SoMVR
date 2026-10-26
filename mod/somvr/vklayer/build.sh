#!/usr/bin/env bash
# build.sh - build VK_LAYER_SOMVR_openxr into build/libVkLayer_somvr_openxr.so (the manifest VkLayer_somvr_openxr.json
# next to this script points there, so VK_LAYER_PATH=<this dir>). Design and switches: somvr_vklayer.c header.
#   ./build.sh                build (warnings are errors)
#   ./build.sh --check        + manifest check, the library's dependencies, unit tests (tests/test_logic.c; the loading
#                             environment's tests/test_env.c) and layer
#                             tests (tests/test_layer.c: fake driver below the layer, tests/mock_xr.c as OpenXR loader +
#                             runtime, plus the real OpenXR loader with no runtime it can find; the test program also
#                             plays the LD_PRELOAD side: it exports somvr_shared and runs ../somvr_views.c; the input
#                             scenarios load tests/input_obj.so, the preload's somvr_input; the HUD scenarios
#                             tests/hud_obj.so, its somvr_hud; the comfort scenarios tests/comfort_obj.so, its
#                             somvr_comfort; the VR menu scenarios tests/vrmenu_obj.so, its somvr_vrmenu; the threat
#                             scenarios tests/threat_obj.so, its somvr_threat). No GPU, no XR runtime.
#                             (From mod/vklayer; this copy adds the per-eye path, docs/re-xr-pipeline.md.)
#   ./build.sh --vulkaninfo   + `vulkaninfo --summary` through the real Vulkan loader with the layer enabled (touches the
#                             GPU driver, never an XR runtime: not the game's engine name and SOMVR_XR_LOADER=none)
# Headers (ShadowOfMordor/third_party, git-ignored; not installed system-wide):
#   Vulkan-Headers-1.4.341  github.com/KhronosGroup/Vulkan-Headers tag v1.4.341 (codeload tarball sha256
#                           8876aad926b2e72bdefddc34885472d5332e2d20aa3ed57313466f0bc982cf3f), include/ only
#   OpenXR-SDK-1.1.61       include/openxr/*.h copied from Dishonored/third_party/Dishonored-VR/third_party/OpenXR-SDK
#                           ("OpenXR SDK 1.1.61 (2026-07-02)", commit 5267613edf3d937e3d77556a106a65c2f82b25c6)
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
TP=$(cd "$D/../../../third_party" 2>/dev/null && pwd) || { echo "no ShadowOfMordor/third_party (run scripts/fetch_third_party.sh)"; exit 2; }
VKH=$TP/Vulkan-Headers-1.4.341/include
XRH=$TP/OpenXR-SDK-1.1.61/include
[ -f "$VKH/vulkan/vk_layer.h" ] && [ -f "$XRH/openxr/openxr_platform.h" ] || { echo "headers missing in $TP (run scripts/fetch_third_party.sh)"; exit 2; }
CF=(-std=gnu11 -O2 -g -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Werror
    -I"$VKH" -I"$XRH")
B=$D/build
LIB=$B/libVkLayer_somvr_openxr.so
mkdir -p "$B/tests"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -Wl,-Bsymbolic -Wl,-soname,libVkLayer_somvr_openxr.so \
    -o "$LIB" "$D/somvr_vklayer.c" "$D/somvr_logic.c" "$D/../somvr_pointer.c" "$D/../somvr_comfort.c" "$D/../somvr_menus.c" \
    "$D/somvr_env.c" "$D/../somvr_vrmenu.c" "$D/../somvr_font.c" "$D/../somvr_threat.c" "$D/somvr_handdraw.c" \
    "$D/../somvr_vrhands.c" "$D/somvr_void.c" "$D/../somvr_objective.c" -lpthread -ldl -lm
echo "built $LIB ($(sha256sum "$LIB" | cut -c1-16))"
CHECK=0 VI=0
for a in "$@"; do case $a in --check) CHECK=1 ;; --vulkaninfo) CHECK=1 VI=1 ;; *) echo "unknown option $a"; exit 2 ;; esac; done
[ $CHECK = 1 ] || exit 0

python3 -I - "$D/VkLayer_somvr_openxr.json" "$LIB" <<'EOF'
import json, os, sys
m = json.load(open(sys.argv[1]))
l = m["layer"]
assert l["name"] == "VK_LAYER_SOMVR_openxr" and l["type"] == "GLOBAL", l
assert "instance_extensions" not in l and "enable_environment" not in l, "explicit layer only"
lib = os.path.normpath(os.path.join(os.path.dirname(sys.argv[1]), l["library_path"]))
assert lib == os.path.normpath(sys.argv[2]), (lib, sys.argv[2])
print("manifest: valid JSON, explicit layer %s -> %s" % (l["name"], os.path.relpath(lib, os.path.dirname(sys.argv[1]))))
EOF
need=$(readelf -d "$LIB" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')
case " $need " in *libstdc++*|*libvulkan*|*libopenxr*) echo "FAIL: unexpected dependencies: $need"; exit 1 ;; esac
exp=$(nm -D --defined-only "$LIB" | awk '$2 == "T" { print $3 }' | sort | tr '\n' ' ')
[ "$exp" = "vkGetDeviceProcAddr vkGetInstanceProcAddr vkNegotiateLoaderLayerInterfaceVersion " ] ||
    { echo "FAIL: exported functions: $exp"; exit 1; }
echo "library: NEEDED $need; exports $exp"

gcc "${CF[@]}" -o "$B/tests/test_logic" "$D/tests/test_logic.c" "$D/somvr_logic.c" -lm
"$B/tests/test_logic"
gcc "${CF[@]}" -o "$B/tests/test_env" "$D/tests/test_env.c" "$D/somvr_env.c" -lm # SoM loading env: the sky, the box
"$B/tests/test_env"
gcc "${CF[@]}" -o "$B/tests/test_void" "$D/tests/test_void.c" "$D/somvr_void.c" -lm # never a void: the floor, the lights
"$B/tests/test_void"
# build 10: the VR hands' shaders are the committed SPIR-V; the hands drawn for real on lavapipe (Mesa's CPU Vulkan)
"$D/shaders/build_spv.sh" --check
gcc "${CF[@]}" -o "$B/tests/hands_lvp" "$D/tests/hands_lvp.c" "$D/somvr_handdraw.c" "$D/../somvr_vrhands.c" -ldl -lm
LVP=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
if [ -f "$LVP" ]; then
    mkdir -p "$B/hands"
    VK_DRIVER_FILES=$LVP VK_ICD_FILENAMES=$LVP "$B/tests/hands_lvp" "$B/hands" | tail -1
else
    echo "hands_lvp: no lavapipe ICD ($LVP): skipped"
fi
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -Wl,-soname,libvulkan.so.1 \
    -o "$B/tests/mock_xr.so" "$D/tests/mock_xr.c" -lm
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/input_obj.so" "$D/tests/input_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/hud_obj.so" "$D/tests/hud_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/comfort_obj.so" "$D/tests/comfort_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/vrmenu_obj.so" "$D/tests/vrmenu_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/threat_obj.so" "$D/tests/threat_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/vrhands_obj.so" "$D/tests/vrhands_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/session_obj.so" "$D/tests/session_obj.c"
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$B/tests/objective_obj.so" "$D/tests/objective_obj.c"
gcc "${CF[@]}" -rdynamic -o "$B/tests/test_layer" "$D/tests/test_layer.c" "$D/../somvr_views.c" "$D/../somvr_pose.c" \
    "$D/somvr_env.c" "$D/../somvr_vrmenu.c" "$D/../somvr_font.c" "$D/../somvr_threat.c" "$D/../somvr_menus.c" \
    "$D/somvr_void.c" "$D/../somvr_objective.c" -ldl -lpthread -lm # the expected env and menu pictures, the threat cues' quads and texture, the HUD's tiles
T=$(mktemp -d)
mkdir -p "$T/empty"
rc=0
"$B/tests/test_layer" "$LIB" "$B/tests/mock_xr.so" "$T" || rc=$?
rm -rf -- "$T"
[ $rc = 0 ] || exit $rc
[ $VI = 1 ] || exit 0

T=$(mktemp -d)
rc=0
env VK_LAYER_PATH="$D" VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr VK_LOADER_DEBUG=layer SOMVR_XR_LOG="$T/vi.log" \
    SOMVR_XR_LOADER="$T/none" XR_RUNTIME_JSON="$T/none.json" vulkaninfo --summary >"$T/out" 2>"$T/err" || rc=$?
grep -m3 -E 'Insert instance layer.*SOMVR|VK_LAYER_SOMVR_openxr' "$T/err" | sed 's/^/  loader: /' || true
grep -m2 -E 'deviceName|GPU id' "$T/out" | sed 's/^/  vulkaninfo: /' || true
sed 's/^/  layer log: /' "$T/vi.log" 2>/dev/null | head -8
ok=1
[ $rc = 0 ] || { echo "FAIL: vulkaninfo exit $rc"; ok=0; }
grep -q 'Insert instance layer "VK_LAYER_SOMVR_openxr"' "$T/err" || { echo "FAIL: the loader did not insert the layer"; ok=0; }
grep -q 'pass-through (not Feral3D)' "$T/vi.log" 2>/dev/null || { echo "FAIL: no gate line in the layer log"; ok=0; }
! grep -q ' xr: ' "$T/vi.log" 2>/dev/null || { echo "FAIL: the layer tried OpenXR"; ok=0; }
rm -rf -- "$T"
[ $ok = 1 ] && echo "vulkaninfo: OK with the layer in the chain, no OpenXR attempted" || exit 1
