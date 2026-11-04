#!/usr/bin/env bash
# win/build.sh - the Windows DLLs (win/README.md 5, 8): build/win/xinput9_1_0.dll (the host: the proxy, the gate,
# MinHook, the patch engine's stubs, the bridge, D3D's discovery - never reloaded) and build/win/somvr_logic.dll (the
# game-side code: somvr.c + the CORE files + the tables; the host loads it, SOMVR_DEV_RELOAD=1 swaps it in the world:
# docs/re-xr-pipeline.md 156). The tables (wintables.py), clang --target=x86_64-pc-windows-msvc + lld-link against the
# x64 CRT/SDK in shared/toolchain/xwin-sdk64. Then its checks: exports, imports, the tables.
#   win/build.sh            build (the objects whose sources or headers changed: clang's .d files)
#   win/build.sh --logic    only somvr_logic.dll (the changed objects, the link; no layer, no checks): the reload's
#                           build, written as .tmp then renamed (a watching host never reads half a file)
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
LIBS=(libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib kernel32.lib user32.lib advapi32.lib)
LP=(/libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64")
# the checks of more than one command: planned as `run ... -- win/build.sh --step NAME` (../buildplan.sh)
step_exports() {
  exp=$(llvm-objdump -p "$B/xinput9_1_0.dll" | awk '/^Export Table:/ {e=1} e && /^ +[0-9]+ +0x/ {print $3}' | sort | tr '\n' ' ')
  want="XInputGetCapabilities XInputGetDSoundAudioDeviceGuids XInputGetState XInputSetState g_cft g_hud g_in g_obj g_sh g_ssn g_tht g_vho g_vmo somvr_host_gen somvr_host_keep somvr_host_persist somvr_host_player_swap somvr_host_reload_request somvr_host_view somvr_host_write somvr_win_bridge somvr_win_hook_entry somvr_win_hook_mid somvr_win_hook_remove somvr_win_log somvr_win_name_game_thread somvr_win_tex_vkimage "
  [ "$exp" = "$want" ] || { echo "FAIL: host exports '$exp', want '$want'"; exit 1; }
  imp=$(llvm-objdump -p "$B/xinput9_1_0.dll" | awk '/DLL Name:/ {print $3}' | sort -u | tr '\n' ' ')
  case " $imp " in *" XINPUT"*|*" xinput"*|*" d3d11"*|*" dxgi"*|*" VCRUNTIME"*|*" api-ms-win-crt"*|*" somvr_logic"*)
    echo "FAIL: host imports $imp (xinput/d3d/the logic are loaded at run time, the CRT is static)"; exit 1 ;; esac
  lexp=$(llvm-objdump -p "$B/somvr_logic.dll" | awk '/^Export Table:/ {e=1} e && /^ +[0-9]+ +0x/ {print $3}' | sort | tr '\n' ' ')
  lwant="somvr_logic_abi somvr_logic_abitest somvr_logic_com somvr_logic_idle somvr_logic_init somvr_logic_present somvr_logic_stop "
  [ "$lexp" = "$lwant" ] || { echo "FAIL: logic exports '$lexp', want '$lwant'"; exit 1; }
  limp=$(llvm-objdump -p "$B/somvr_logic.dll" | awk '/DLL Name:/ {print $3}' | sort -u | tr '\n' ' ')
  case " $limp " in *" xinput9_1_0.dll "*) ;; *) echo "FAIL: the logic imports '$limp': not the host (xinput9_1_0.dll)"; exit 1 ;; esac
  case " $limp " in *" d3d11"*|*" dxgi"*|*" VCRUNTIME"*|*" api-ms-win-crt"*|*" XINPUT1"*|*" xinput1"*)
    echo "FAIL: logic imports $limp"; exit 1 ;; esac
  echo "host exports: $exp; imports: $imp"
  echo "logic exports: $lexp; imports: $limp"
}
step_d3dtest() {
  llvm-objdump -p "$B/d3dtest.exe" | grep -q "DLL Name: xinput9_1_0.dll\|DLL Name: XINPUT9_1_0.dll" || { echo "FAIL: d3dtest imports no xinput9_1_0"; exit 1; }
}
# the logic: linked as .tmp, then renamed over (a host watching it - SOMVR_DEV_RELOAD - copies a whole file only); the
# plan skips it when no object changed (-k: a new file would be a reload)
step_logic_link() { # the objects and the host's import library
  lld-link /nologo /dll /debug /pdb:"$B/somvr_logic.pdb" /out:"$B/somvr_logic.dll.tmp" /implib:"$B/obj/somvr_logic.lib" \
      /machine:x64 /ignore:4099 /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat /guard:no "${LP[@]}" \
      "$@" "${LIBS[@]}"
  mv -f "$B/somvr_logic.dll.tmp" "$B/somvr_logic.dll"
}
if [ "${1:-}" = --step ]; then s=$2; shift 2; "step_$s" "$@"; exit 0; fi
CHECK=0 SYNTAX=0 LOGIC=0
for a in "$@"; do case $a in --check) CHECK=1 ;; --syntax) SYNTAX=1 ;; --logic) LOGIC=1 ;; *) echo "unknown option $a"; exit 2 ;; esac; done

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
# the game-side code (the logic): the Linux preload's sources (somvr.c includes the *_game.c files), the CORE files, the
# site tables; the host: the proxy and its start, the patch engine (+ MinHook), the reload, the bridge, D3D's discovery
# (+ the two CORE files its stand-in test uses); the POSIX shim in both (each DLL its own CRT)
CORE=$(sed -n '/^CORE=(/,/)/p' "$D/build.sh" | grep -o '"\$D/[a-z0-9_]*\.c"' | tr -d '"' | sed "s|\$D|$D|")
LOGIC_SRC=("$D/somvr.c" $CORE "$W/somvr_win_sites.c" "$W/somvr_win_compat.c")
HOST_SRC=("$W/somvr_win_main.c" "$W/somvr_win_hook.c" "$W/somvr_win_host.c" "$W/somvr_win_bridge.c" "$W/somvr_win_d3d.c"
          "$W/sha256.c")
MHSRC=("$MH/src/hook.c" "$MH/src/buffer.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c") # MinHook 1.3.4 (BSD-2-Clause)
# the reload test's note (scripts/reload35-drive.sh writes it; somvr_reload_game.c includes it): always there, so the object
# cache sees its change - a header that appears later is in no depfile (an empty one = no note)
[ -f "$B/somvr_reload_note.h" ] || echo '/* the reload note of scripts/reload35-drive.sh: none */' > "$B/somvr_reload_note.h"
SRCS=("${LOGIC_SRC[@]}" "${HOST_SRC[@]}")
if [ $SYNTAX = 1 ]; then # every file, every error (no plan: it goes on after an error)
  rc=0
  for s in "${SRCS[@]}"; do
    command clang "${CF[@]}" -fsyntax-only -ferror-limit=0 "$s" || rc=1
  done
  exit $rc
fi
[ $LOGIC = 0 ] || [ -f "$B/xinput9_1_0.lib" ] || { echo "no $B/xinput9_1_0.lib (the host's import library): win/build.sh first"; exit 2; }
. "$D/buildplan.sh"
plan_open "$B"
obj_of() { echo "$B/obj/$(basename "${1%.c}").obj"; }
# somvr.c (the unity file: every _game.c, ~80 k lines, one optimiser run of ~8 s and a codegen of ~8 s) is optimised once
# into bitcode (cached as any object), split into NSPLIT parts (llvm-split) and its code made from the parts at once
# (llc), with line tables only (code identical to -g): a one-file change of the game side builds in ~11 s instead of ~18
# (docs 156)
NSPLIT=8
CFL=(); for a in "${CF[@]}"; do [ "$a" = -g ] && CFL+=(-gline-tables-only) || CFL+=("$a"); done
clang "${CFL[@]}" -emit-llvm -c -o "$B/obj/somvr.bc" "$D/somvr.c"
parts=(); for ((i = 0; i < NSPLIT; i++)); do parts+=(-o "$B/obj/somvr.p$i"); done
run -k "${parts[@]}" -- llvm-split -j "$NSPLIT" -o "$B/obj/somvr.p" "$B/obj/somvr.bc"
lobjs=()
for ((i = 0; i < NSPLIT; i++)); do
  run -k -o "$B/obj/somvr.p$i.obj" -- llc -O2 -filetype=obj -function-sections -data-sections \
      -o "$B/obj/somvr.p$i.obj" "$B/obj/somvr.p$i"
  lobjs+=("$B/obj/somvr.p$i.obj")
done
for s in "${LOGIC_SRC[@]}"; do
  [ "$s" = "$D/somvr.c" ] && continue
  o=$(obj_of "$s")
  clang "${CF[@]}" -c -o "$o" "$s"
  lobjs+=("$o")
done
# the host's objects (the CORE files its D3D stand-in test uses and the POSIX shim: the logic's objects, compiled once)
hobjs=()
for s in "${HOST_SRC[@]}"; do
  [ $LOGIC = 1 ] && break
  o=$(obj_of "$s")
  clang "${CF[@]}" -c -o "$o" "$s"
  hobjs+=("$o")
done
for s in "$D/somvr_eyes.c" "$D/somvr_views.c" "$D/somvr_pose.c" "$W/somvr_win_compat.c"; do hobjs+=("$(obj_of "$s")"); done
for s in "${MHSRC[@]}"; do
  [ $LOGIC = 1 ] && break
  o="$B/obj/mh_$(basename "${s%.c}").obj"
  clang --target=x86_64-pc-windows-msvc -O2 -gcodeview -g -w "${SYS[@]}" -D_CRT_SECURE_NO_WARNINGS -c -o "$o" "$s"
  hobjs+=("$o")
done
if [ $LOGIC = 0 ]; then
  run -k -d "$W/xinput9_1_0.def" -o "$B/xinput9_1_0.dll" -o "$B/xinput9_1_0.pdb" -o "$B/xinput9_1_0.lib" -- \
    lld-link /nologo /dll /debug /pdb:"$B/xinput9_1_0.pdb" /out:"$B/xinput9_1_0.dll" /def:"$W/xinput9_1_0.def" \
      /implib:"$B/xinput9_1_0.lib" /machine:x64 /ignore:4099 /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat \
      /guard:no "${LP[@]}" "${hobjs[@]}" "${LIBS[@]}"
  built "$B/xinput9_1_0.dll" ": the host"
fi
run -k -d "$B/xinput9_1_0.lib" -o "$B/somvr_logic.dll" -o "$B/somvr_logic.pdb" -- \
  "$W/build.sh" --step logic_link "${lobjs[@]}" "$B/xinput9_1_0.lib"
built "$B/somvr_logic.dll" ": the logic"
[ $LOGIC = 1 ] && plan_go

# W1 (docs/re-winport.md 3.2): the Vulkan layer as a Windows DLL for native Windows players (the Khronos loader loads it
# by build/win/layer/VkLayer_somvr_openxr.json; the proxy points VK_ADD_LAYER_PATH at it). The same sources as
# vklayer/build.sh's library, through win/compat.
V=$D/vklayer
LSRC=("$V/somvr_vklayer.c" "$V/somvr_logic.c" "$D/somvr_pointer.c" "$D/somvr_comfort.c" "$D/somvr_menus.c" "$V/somvr_env.c"
      "$D/somvr_vrmenu.c" "$D/somvr_font.c" "$D/somvr_threat.c" "$V/somvr_handdraw.c" "$D/somvr_vrhands.c" "$V/somvr_void.c"
      "$D/somvr_objective.c" "$D/somvr_live.c")
mkdir -p "$B/layer" "$B/obj/layer"
layobjs=()
for s in "${LSRC[@]}"; do
  o="$B/obj/layer/$(basename "${s%.c}").obj"
  clang "${CF[@]}" -I"$V" -Wno-missing-prototypes -c -o "$o" "$s"
  layobjs+=("$o")
done
printf 'LIBRARY VkLayer_somvr_openxr\nEXPORTS\n\tvkNegotiateLoaderLayerInterfaceVersion\n\tvkGetInstanceProcAddr\n\tvkGetDeviceProcAddr\n' > "$B/obj/layer/layer.def"
run -k -d "$B/obj/layer/layer.def" -o "$B/layer/VkLayer_somvr_openxr.dll" -o "$B/layer/VkLayer_somvr_openxr.pdb" -- \
  lld-link /nologo /dll /debug /pdb:"$B/layer/VkLayer_somvr_openxr.pdb" /out:"$B/layer/VkLayer_somvr_openxr.dll" \
    /def:"$B/obj/layer/layer.def" /machine:x64 /ignore:4099 /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "${layobjs[@]}" "$B/obj/somvr_win_compat.obj" libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib kernel32.lib \
    user32.lib advapi32.lib
sed -e 's|"library_path": "[^"]*"|"library_path": ".\\\\VkLayer_somvr_openxr.dll"|' \
    -e 's|"description": "|"description": "(Windows) |' "$V/VkLayer_somvr_openxr.json" > "$B/layer/VkLayer_somvr_openxr.json"
built "$B/layer/VkLayer_somvr_openxr.dll" " + its json"
[ $CHECK = 1 ] || plan_go

say "== win: the DLLs' exports and imports"
run -d "$B/xinput9_1_0.dll" -d "$B/somvr_logic.dll" -- "$W/build.sh" --step exports
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
