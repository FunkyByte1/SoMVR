#!/usr/bin/env bash
# win/build.sh - the Windows DLL build/win/xinput9_1_0.dll (win/README.md 5): the tables (wintables.py), the game-side
# code (somvr.c + the CORE files) and the files here, clang --target=x86_64-pc-windows-msvc + lld-link against the
# x64 CRT/SDK in shared/toolchain/xwin-sdk64. Then its checks: exports, imports, no TLS callbacks, the tables.
#   win/build.sh            build
#   win/build.sh --check    + the checks, the harness's test program, and the platform-neutral unit tests of build.sh
#                           and vklayer/build.sh built for Windows and run under Proton's wine in a throwaway prefix
#                           copy (win/wintests.py; win/README.md 7)
#   win/build.sh --syntax   only compile (no link), all errors shown
# The commands run in parallel (../buildplan.sh/.py: the object cache - a source, its flags and every header it read -,
# the links skipped when their inputs are unchanged, the log in this file's order): docs/re-xr-pipeline.md 161
set -euo pipefail
W=$(cd "$(dirname "$0")" && pwd)
D=$(cd "$W/.." && pwd)
FLAT=$(cd "$D/../../.." && pwd)
# the x64 CRT/SDK splat (not in git): XWIN64, else shared/toolchain/xwin-sdk64 in this tree or a folder above it (a
# frozen build under ShadowOfMordor/logs/), else in the main checkout of a git worktree
X=${XWIN64:-}
if [ -z "$X" ]; then
  d=$FLAT
  while [ "$d" != / ] && [ ! -d "$d/shared/toolchain/xwin-sdk64/crt/lib/x86_64" ]; do d=$(dirname "$d"); done
  X=$d/shared/toolchain/xwin-sdk64
  if [ ! -d "$X/crt/lib/x86_64" ] && c=$(git -C "$W" rev-parse --path-format=absolute --git-common-dir 2>/dev/null); then
    X=$(dirname "$c")/shared/toolchain/xwin-sdk64
  fi
fi
[ -d "$X/crt/lib/x86_64" ] || { echo "no x64 xwin splat (shared/toolchain/xwin-sdk64 here, above, or in the main checkout; or set XWIN64): $X"; exit 2; }
TP=$(cd "$D/../../third_party" 2>/dev/null && pwd) || { echo "no ShadowOfMordor/third_party"; exit 2; }
XRH=$TP/OpenXR-SDK-1.1.61/include
VKH=$TP/Vulkan-Headers-1.4.341/include
MH=$TP/minhook-1.3.4
[ -f "$MH/include/MinHook.h" ] || { echo "no $MH (scripts/fetch_third_party.sh)"; exit 2; }
B=$D/build/win
mkdir -p "$B/obj"
# the checks of more than one command: planned as `run ... -- win/build.sh --step NAME` (../buildplan.sh)
step_exports() {
  exp=$(llvm-objdump -p "$B/xinput9_1_0.dll" | awk '/^Export Table:/ {e=1} e && /^ +[0-9]+ +0x/ {print $3}' | sort | tr '\n' ' ')
  want="XInputGetCapabilities XInputGetDSoundAudioDeviceGuids XInputGetState XInputSetState somvr_win_bridge "
  [ "$exp" = "$want" ] || { echo "FAIL: exports '$exp', want '$want'"; exit 1; }
  imp=$(llvm-objdump -p "$B/xinput9_1_0.dll" | awk '/DLL Name:/ {print $3}' | sort -u | tr '\n' ' ')
  case " $imp " in *" XINPUT"*|*" xinput"*|*" d3d11"*|*" dxgi"*|*" VCRUNTIME"*|*" api-ms-win-crt"*)
    echo "FAIL: imports $imp (xinput/d3d are loaded at run time, the CRT is static)"; exit 1 ;; esac
  echo "exports: $exp; imports: $imp"
}
step_d3dtest() {
  llvm-objdump -p "$B/d3dtest.exe" | grep -q "DLL Name: xinput9_1_0.dll\|DLL Name: XINPUT9_1_0.dll" || { echo "FAIL: d3dtest imports no xinput9_1_0"; exit 1; }
}
if [ "${1:-}" = --step ]; then "step_$2"; exit 0; fi
CHECK=0 SYNTAX=0
for a in "$@"; do case $a in --check) CHECK=1 ;; --syntax) SYNTAX=1 ;; *) echo "unknown option $a"; exit 2 ;; esac; done

# other builds of the exe: reference/bin/<tag>/ShadowOfMordor.exe (a read-only copy of that build's exe) and its map
# win/exe-<tag>.tsv (scripts/winport_exe_map.py: the Steam-Win rows carried to it); each gets its own tables
EXTRA=()
for m in "$W"/exe-*.tsv; do
  [ -f "$m" ] || continue
  tag=$(basename "$m" .tsv); tag=${tag#exe-}
  ex=$D/../../reference/bin/$tag/ShadowOfMordor.exe
  [ -f "$ex" ] && EXTRA+=(--extra-exe "$tag=$ex:$m") || echo "win: $m without $ex: no $tag tables"
done
python3 -I "$W/wintables.py" --out "$B" --quiet "${EXTRA[@]}"

SYS=(-isystem "$W/compat" -isystem "$X/crt/include" -isystem "$X/sdk/include/ucrt" -isystem "$X/sdk/include/um"
     -isystem "$X/sdk/include/shared")
# b34 (re-xr-pipeline.md 151): -Wall -Wextra as the Linux build (the code is clean of the sign/size, unused-parameter and
# field-initializer classes the -Wno's here hid); a format mismatch is an error - on Windows that is the LLP64 class (%lx
# with a 64-bit value: long is 32 bits), and win/llp64.py --check below the casts that cut a 64-bit value to long
CF=(--target=x86_64-pc-windows-msvc -std=gnu11 -O2 -gcodeview -g -fno-strict-aliasing -mcx16
    -Wall -Wextra -Wshadow -Werror=format -Wno-unknown-pragmas -Wno-unused-function -Wno-microsoft-anon-tag
    -Werror=microsoft-enum-value
    -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_WIN32_WINNT=0x0A00 -DSOMVR_WIN=1
    "${SYS[@]}" -include somvr_compat.h -I"$B" -I"$D" -I"$W" -I"$XRH" -I"$VKH" -I"$MH/include" -ffunction-sections -fdata-sections)
# the game-side code: the Linux preload's sources (somvr.c includes the *_game.c files) and the CORE files
CORE=$(sed -n '/^CORE=(/,/)/p' "$D/build.sh" | grep -o '"\$D/[a-z0-9_]*\.c"' | tr -d '"' | sed "s|\$D|$D|")
WIN=("$W/somvr_win_main.c" "$W/somvr_win_compat.c" "$W/somvr_win_sites.c" "$W/somvr_win_bridge.c" "$W/somvr_win_d3d.c"
     "$W/somvr_win_hook.c" "$W/sha256.c")
MHSRC=("$MH/src/hook.c" "$MH/src/buffer.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c") # MinHook 1.3.4 (BSD-2-Clause)
SRCS=("$D/somvr.c" $CORE "${WIN[@]}")
if [ $SYNTAX = 1 ]; then # every file, every error (no plan: it goes on after an error)
  rc=0
  for s in "${SRCS[@]}"; do
    command clang "${CF[@]}" -fsyntax-only -ferror-limit=0 "$s" || rc=1
  done
  exit $rc
fi
. "$D/buildplan.sh"
plan_open "$B"
objs=()
for s in "${SRCS[@]}"; do
  o="$B/obj/$(basename "${s%.c}").obj"
  clang "${CF[@]}" -c -o "$o" "$s"
  objs+=("$o")
done
for s in "${MHSRC[@]}"; do
  o="$B/obj/mh_$(basename "${s%.c}").obj"
  clang --target=x86_64-pc-windows-msvc -O2 -gcodeview -g -w "${SYS[@]}" -D_CRT_SECURE_NO_WARNINGS -c -o "$o" "$s"
  objs+=("$o")
done
LIBS=(libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib kernel32.lib user32.lib advapi32.lib)
run -k -d "$W/xinput9_1_0.def" -o "$B/xinput9_1_0.dll" -o "$B/xinput9_1_0.pdb" -- \
  lld-link /nologo /dll /debug /pdb:"$B/xinput9_1_0.pdb" /out:"$B/xinput9_1_0.dll" /def:"$W/xinput9_1_0.def" \
    /machine:x64 /ignore:4099 /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat /guard:no \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "${objs[@]}" "${LIBS[@]}"
built "$B/xinput9_1_0.dll"

# W1 (docs/re-winport.md 3.2): the Vulkan layer as a Windows DLL for native Windows players (the Khronos loader loads it
# by build/win/layer/VkLayer_somvr_openxr.json; the proxy points VK_ADD_LAYER_PATH at it). The same sources as
# vklayer/build.sh's library, through win/compat.
V=$D/vklayer
LSRC=("$V/somvr_vklayer.c" "$V/somvr_logic.c" "$D/somvr_pointer.c" "$D/somvr_comfort.c" "$D/somvr_menus.c" "$V/somvr_env.c"
      "$D/somvr_vrmenu.c" "$D/somvr_font.c" "$D/somvr_threat.c" "$V/somvr_handdraw.c" "$D/somvr_vrhands.c" "$V/somvr_void.c"
      "$D/somvr_objective.c")
mkdir -p "$B/layer" "$B/obj/layer"
lobjs=()
for s in "${LSRC[@]}"; do
  o="$B/obj/layer/$(basename "${s%.c}").obj"
  clang "${CF[@]}" -I"$V" -Wno-missing-prototypes -c -o "$o" "$s"
  lobjs+=("$o")
done
printf 'LIBRARY VkLayer_somvr_openxr\nEXPORTS\n\tvkNegotiateLoaderLayerInterfaceVersion\n\tvkGetInstanceProcAddr\n\tvkGetDeviceProcAddr\n' > "$B/obj/layer/layer.def"
run -k -d "$B/obj/layer/layer.def" -o "$B/layer/VkLayer_somvr_openxr.dll" -o "$B/layer/VkLayer_somvr_openxr.pdb" -- \
  lld-link /nologo /dll /debug /pdb:"$B/layer/VkLayer_somvr_openxr.pdb" /out:"$B/layer/VkLayer_somvr_openxr.dll" \
    /def:"$B/obj/layer/layer.def" /machine:x64 /ignore:4099 /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "${lobjs[@]}" "$B/obj/somvr_win_compat.obj" libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib kernel32.lib \
    user32.lib advapi32.lib
sed -e 's|"library_path": "[^"]*"|"library_path": ".\\\\VkLayer_somvr_openxr.dll"|' \
    -e 's|"description": "|"description": "(Windows) |' "$V/VkLayer_somvr_openxr.json" > "$B/layer/VkLayer_somvr_openxr.json"
built "$B/layer/VkLayer_somvr_openxr.dll" " + its json"
[ $CHECK = 1 ] || plan_go

say "== win: the DLL's exports and imports"
run -d "$B/xinput9_1_0.dll" -- "$W/build.sh" --step exports
say "== win: the tables against the exe"
run -- python3 -I "$W/wintables.py" --check
say "== win: LLP64 - no 64-bit value cast to the 32-bit long (win/llp64.py; a log's: llp64.py --fix FILE)"
run -- python3 -I "$W/llp64.py" --check --cache "$B/llp64" "${CF[@]}" -I"$V" -- "${SRCS[@]}" "${LSRC[@]}"
say "== win: the harness's test program (build/win/d3dtest.exe)"
clang --target=x86_64-pc-windows-msvc -std=gnu11 -O2 -Wall -Wno-unused-parameter "${SYS[@]}" -D_CRT_SECURE_NO_WARNINGS \
    -c -o "$B/obj/d3dtest.obj" "$W/tests/d3dtest.c"
run -k -o "$B/d3dtest.exe" -- lld-link /nologo /out:"$B/d3dtest.exe" /subsystem:console /machine:x64 /ignore:4099 \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "$B/obj/d3dtest.obj" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib user32.lib advapi32.lib d3d11.lib dxgi.lib xinput9_1_0.lib
run -d "$B/d3dtest.exe" -- "$W/build.sh" --step d3dtest
say "built $B/d3dtest.exe"
say "== win: the compat stand-ins' test program (build/win/test_compat.exe; run: scripts/headless_win.sh MOD --test compat)"
clang "${CF[@]}" -c -o "$B/obj/test_compat.obj" "$W/tests/test_compat.c"
run -k -o "$B/test_compat.exe" -- lld-link /nologo /out:"$B/test_compat.exe" /subsystem:console /machine:x64 /ignore:4099 \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "$B/obj/test_compat.obj" "$B/obj/somvr_win_compat.obj" libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib \
    kernel32.lib user32.lib advapi32.lib
say "built $B/test_compat.exe"
say "== win: the unit tests under wine (win/wintests.py: build.sh's and vklayer/build.sh's, built for Windows)"
run -- python3 -I "$W/wintests.py" --xwin "$X" -- "${CF[@]}"
plan_go
