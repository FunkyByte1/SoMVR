#!/usr/bin/env bash
# tests/analyze.sh - clang's static analyzer (clang --analyze, its default checkers) over the mod's translation units: the
# preload (somvr.c with its *_game.c, the CORE files), the layer (somvr_vklayer.c with its *_layer.c, the files it links)
# for Linux, and somvr.c, win/*.c and the layer again for the Windows target (win/build.sh's flags). Prints each warning
# once (file:line:col: message [checker]) and a count; the run of 2026-11-03 and its triage: docs/re-xr-pipeline.md 151.
# Nothing is built or changed; ~2-4 minutes on 8 cores.
#   tests/analyze.sh [-j N] [-v]    (-v: each warning's path notes too)
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd)
TP=$(cd "$D/../../third_party" && pwd)
XRH=$TP/OpenXR-SDK-1.1.61/include VKH=$TP/Vulkan-Headers-1.4.341/include V=$D/vklayer W=$D/win
J=8 VERB=0
while [ $# -gt 0 ]; do case $1 in -j) J=$2; shift ;; -v) VERB=1 ;; *) echo "unknown option $1"; exit 2 ;; esac; shift; done
CORE=$(sed -n '/^CORE=(/,/)/p' "$D/build.sh" | grep -o '"\$D/[a-z0-9_]*\.c"' | tr -d '"' | sed "s|\$D|$D|")
O=$(mktemp -d)
trap 'rm -rf -- "$O"' EXIT
LIN=(-std=gnu11 -O2 -D_GNU_SOURCE -I"$XRH" -I"$VKH" -I"$D" -I"$V")
X=${XWIN64:-$(cd "$D/../../.." && pwd)/shared/toolchain/xwin-sdk64}
[ -d "$X/crt/include" ] || X=/home/bara/Projects/flat2vr/shared/toolchain/xwin-sdk64
WINF=(--target=x86_64-pc-windows-msvc -std=gnu11 -O2 -fno-strict-aliasing -mcx16 -D_CRT_SECURE_NO_WARNINGS
      -D_CRT_NONSTDC_NO_DEPRECATE -D_WIN32_WINNT=0x0A00 -DSOMVR_WIN=1 -isystem "$W/compat" -isystem "$X/crt/include"
      -isystem "$X/sdk/include/ucrt" -isystem "$X/sdk/include/um" -isystem "$X/sdk/include/shared" -include somvr_compat.h
      -I"$D/build/win" -I"$D" -I"$W" -I"$XRH" -I"$VKH" -I"$TP/minhook-1.3.4/include" -I"$V")
TUS=()
for s in "$D/somvr.c" $CORE "$D/somvr_menus.c" "$V/somvr_vklayer.c" "$V/somvr_logic.c" "$V/somvr_env.c" "$V/somvr_handdraw.c" \
         "$V/somvr_void.c"; do TUS+=("lin:$s"); done
if [ -d "$X/crt/include" ] && [ -f "$D/build/win/somvr_wintables.inc" ]; then
  for s in "$D/somvr.c" "$W"/somvr_win_*.c "$W/sha256.c" "$V/somvr_vklayer.c"; do TUS+=("win:$s"); done
else
  echo "analyze: no xwin SDK or no build/win tables (run win/build.sh once): the Windows target skipped"
fi
i=0
for tu in "${TUS[@]}"; do
  t=${tu%%:*} s=${tu#*:} i=$((i + 1))
  if [ "$t" = lin ]; then a=("${LIN[@]}"); else a=("${WINF[@]}"); fi
  ( clang --analyze --analyzer-output text "${a[@]}" -Xclang -analyzer-config -Xclang max-nodes=400000 "$s" -o /dev/null \
        > "$O/$i.txt" 2>&1 || echo "analyze: clang failed on ${s#$D/} ($t)" ) &
  while [ "$(jobs -rp | wc -l)" -ge "$J" ]; do wait -n || true; done
done
wait
python3 -I - "$D" "$VERB" "$O"/*.txt <<'PY'
import re, sys
d, verb, files = sys.argv[1].rstrip('/') + '/', sys.argv[2] == '1', sys.argv[3:]
seen, order = {}, []
for f in files:
    cur = None
    for line in open(f, errors='replace'):
        m = re.match(r'^(/\S+?):(\d+):(\d+): (warning|note): (.*)$', line.rstrip('\n'))
        if not m:
            continue
        where = '%s:%s:%s' % (m.group(1).replace(d, ''), m.group(2), m.group(3))
        if m.group(4) == 'warning':
            cur = where + ': ' + m.group(5)
            if cur not in seen:
                seen[cur] = []
                order.append(cur)
            else:
                cur = None
        elif cur and verb:
            seen[cur].append('    ' + where + ': ' + m.group(5))
for w in order:
    print(w)
    for n in seen[w]:
        print(n)
print('analyze: %d warnings in %d translation units (triage: docs/re-xr-pipeline.md 151)' % (len(order), len(files)))
PY
