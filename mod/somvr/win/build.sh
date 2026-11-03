#!/usr/bin/env bash
# win/build.sh - the Windows DLL build/win/xinput9_1_0.dll (win/README.md 5): the tables (wintables.py), the game-side
# code (somvr.c + the CORE files) and the files here, clang --target=x86_64-pc-windows-msvc + lld-link against the
# x64 CRT/SDK in shared/toolchain/xwin-sdk64. Then its checks: exports, imports, no TLS callbacks, the tables.
#   win/build.sh            build
#   win/build.sh --check    + the checks and the Windows unit tests' build (they run under Proton: scripts/headless_win.sh)
#   win/build.sh --syntax   only compile (no link), all errors shown
set -euo pipefail
W=$(cd "$(dirname "$0")" && pwd)
D=$(cd "$W/.." && pwd)
FLAT=$(cd "$D/../../.." && pwd)
X=${XWIN64:-$FLAT/shared/toolchain/xwin-sdk64}
[ -d "$X/crt/lib/x86_64" ] || X=/home/bara/Projects/flat2vr/shared/toolchain/xwin-sdk64
[ -d "$X/crt/lib/x86_64" ] || { echo "no x64 xwin splat at $X (shared/toolchain/xwin-sdk64)"; exit 2; }
TP=$(cd "$D/../../third_party" 2>/dev/null && pwd) || { echo "no ShadowOfMordor/third_party"; exit 2; }
XRH=$TP/OpenXR-SDK-1.1.61/include
VKH=$TP/Vulkan-Headers-1.4.341/include
MH=$TP/minhook-1.3.4
[ -f "$MH/include/MinHook.h" ] || { echo "no $MH (scripts/fetch_third_party.sh)"; exit 2; }
B=$D/build/win
mkdir -p "$B/obj"
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
CF=(--target=x86_64-pc-windows-msvc -std=gnu11 -O2 -gcodeview -g -fno-strict-aliasing -mcx16
    -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unknown-pragmas
    -Wno-ignored-attributes -Wno-unused-function -Wno-sign-compare -Wno-microsoft-anon-tag
    -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_WIN32_WINNT=0x0A00 -DSOMVR_WIN=1
    "${SYS[@]}" -include somvr_compat.h -I"$B" -I"$D" -I"$W" -I"$XRH" -I"$VKH" -I"$MH/include" -ffunction-sections -fdata-sections)
# the game-side code: the Linux preload's sources (somvr.c includes the *_game.c files) and the CORE files
CORE=$(sed -n '/^CORE=(/,/)/p' "$D/build.sh" | grep -o '"\$D/[a-z0-9_]*\.c"' | tr -d '"' | sed "s|\$D|$D|")
WIN=("$W/somvr_win_main.c" "$W/somvr_win_compat.c" "$W/somvr_win_sites.c" "$W/somvr_win_bridge.c" "$W/somvr_win_d3d.c"
     "$W/somvr_win_hook.c" "$W/sha256.c")
MHSRC=("$MH/src/hook.c" "$MH/src/buffer.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c") # MinHook 1.3.4 (BSD-2-Clause)
SRCS=("$D/somvr.c" $CORE "${WIN[@]}")
objs=()
rc=0
for s in "${SRCS[@]}"; do
  o="$B/obj/$(basename "${s%.c}").obj"
  if [ $SYNTAX = 1 ]; then
    clang "${CF[@]}" -fsyntax-only -ferror-limit=0 "$s" || rc=1
    continue
  fi
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ "$D/somvr.c" -nt "$o" ] || [ "$W/compat/somvr_compat.h" -nt "$o" ] ||
     [ "$B/somvr_wintables.inc" -nt "$o" ]; then
    clang "${CF[@]}" -c -o "$o" "$s"
  fi
  objs+=("$o")
done
for s in "${MHSRC[@]}"; do
  [ $SYNTAX = 1 ] && continue
  o="$B/obj/mh_$(basename "${s%.c}").obj"
  [ -f "$o" ] && [ "$o" -nt "$s" ] || clang --target=x86_64-pc-windows-msvc -O2 -gcodeview -g -w "${SYS[@]}" \
      -D_CRT_SECURE_NO_WARNINGS -c -o "$o" "$s"
  objs+=("$o")
done
[ $SYNTAX = 1 ] && exit $rc
LIBS=(libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib kernel32.lib user32.lib advapi32.lib)
lld-link /nologo /dll /debug /pdb:"$B/xinput9_1_0.pdb" /out:"$B/xinput9_1_0.dll" /def:"$W/xinput9_1_0.def" \
    /machine:x64 /ignore:4099 /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat /guard:no \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "${objs[@]}" "${LIBS[@]}"
echo "built $B/xinput9_1_0.dll ($(sha256sum "$B/xinput9_1_0.dll" | cut -c1-16))"
[ $CHECK = 1 ] || exit 0

echo "== win: the DLL's exports and imports"
exp=$(llvm-objdump -p "$B/xinput9_1_0.dll" | awk '/^Export Table:/ {e=1} e && /^ +[0-9]+ +0x/ {print $3}' | sort | tr '\n' ' ')
want="XInputGetCapabilities XInputGetDSoundAudioDeviceGuids XInputGetState XInputSetState somvr_win_bridge "
[ "$exp" = "$want" ] || { echo "FAIL: exports '$exp', want '$want'"; exit 1; }
imp=$(llvm-objdump -p "$B/xinput9_1_0.dll" | awk '/DLL Name:/ {print $3}' | sort -u | tr '\n' ' ')
case " $imp " in *" XINPUT"*|*" xinput"*|*" d3d11"*|*" dxgi"*|*" VCRUNTIME"*|*" api-ms-win-crt"*)
  echo "FAIL: imports $imp (xinput/d3d are loaded at run time, the CRT is static)"; exit 1 ;; esac
echo "exports: $exp; imports: $imp"
echo "== win: the tables against the exe"
python3 -I "$W/wintables.py" --check
echo "== win: the harness's test program (build/win/d3dtest.exe)"
clang --target=x86_64-pc-windows-msvc -std=gnu11 -O2 -Wall -Wno-unused-parameter "${SYS[@]}" -D_CRT_SECURE_NO_WARNINGS \
    -c -o "$B/obj/d3dtest.obj" "$W/tests/d3dtest.c"
lld-link /nologo /out:"$B/d3dtest.exe" /subsystem:console /machine:x64 /ignore:4099 \
    /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" /libpath:"$X/sdk/lib/ucrt/x86_64" \
    "$B/obj/d3dtest.obj" libcmt.lib libvcruntime.lib libucrt.lib kernel32.lib user32.lib d3d11.lib dxgi.lib xinput9_1_0.lib
llvm-objdump -p "$B/d3dtest.exe" | grep -q "DLL Name: xinput9_1_0.dll\|DLL Name: XINPUT9_1_0.dll" || { echo "FAIL: d3dtest imports no xinput9_1_0"; exit 1; }
echo "built $B/d3dtest.exe"
