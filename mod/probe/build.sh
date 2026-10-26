#!/usr/bin/env bash
# build.sh - build the runtime-probe library into build/; --check also verifies the patch sites against the game
# binary on disk (read-only), that the probe's sites are disjoint from mod/spike1's and mod/somvr's, that the library
# stays inert outside the game, and runs the selftest against stand-in functions.
#   ./build.sh            build
#   ./build.sh --check    build, then the site checks, the inert tests and selftest.c
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
SOM=$(cd "$D/../.." && pwd)
BIN=${SOM_BIN:-$SOM/reference/bin/linux/ShadowOfMordor}
mkdir -p "$D/build"
CF=(-O2 -g -Wall -Wextra -Wshadow -std=gnu11)
gcc "${CF[@]}" -fPIC -shared -fvisibility=hidden -Wl,-z,defs -o "$D/build/somvr_probe.so" "$D/somvr_probe.c" -lpthread -lm
echo "built $D/build/somvr_probe.so ($(sha256sum "$D/build/somvr_probe.so" | cut -c1-16))"
[ "${1:-}" = --check ] || exit 0

echo "== patch sites (read-only, the game binary on disk)"
python3 -I "$SOM/scripts/check_doc_sites.py" "$BIN" "$D/somvr_probe.c" "$SOM/docs/runtime-probes.md"

echo "== the SITE lines match gen_sites.py's output for this binary"
python3 -I "$D/gen_sites.py" "$BIN" > "$D/build/sites.gen"
python3 -I - "$D/somvr_probe.c" "$D/build/sites.gen" <<'PY'
import re, sys
src, gen = (open(p, encoding="utf-8").read() for p in sys.argv[1:3])
rx = re.compile(r'^SITE\(.*\),$', re.M)
want = [l for l in rx.findall(gen)]
have = [l.strip() for l in rx.findall(src)]
if want != have:
    only = [l for l in want if l not in have] + [l for l in have if l not in want]
    sys.exit("the SITE lines in somvr_probe.c differ from gen_sites.py's output:\n  " + "\n  ".join(only[:8]))
print("%d SITE lines identical to the generator's output" % len(want))
PY

echo "== the probe patches nothing that spike1 or somvr patches"
python3 -I - "$D/somvr_probe.c" "$SOM/mod/spike1/somvr_spike1.c" "$SOM/mod/somvr/somvr.c" <<'PY'
import re, sys
RX = re.compile(r'SITE\("(\w+)", (K_CALL|K_VSLOT|K_ENTRY), (0x[0-9a-fA-F]+)')
def sites(p, patched_only, block=None):
    out = {}
    text = open(p, encoding="utf-8").read()
    # somvr.c's game-side parts (#include "somvr_*_game.c": the step, the finishers, SOMVR_HIDE_HEAD=parts' HP_DRAW_*
    # calls) are read with it
    for inc in re.findall(r'^#include "(somvr_\w+_game\.c)"', text, re.M):
        text += "\n" + open(p.rsplit("/", 1)[0] + "/" + inc, encoding="utf-8").read()
    if block:  # the probe: only its hook table is patched (the FN and console tables are read and called)
        text = text[text.index(block):]
        text = text[:text.index("SITES-END")]
    for name, kind, addr in RX.findall(text):
        if patched_only and kind == "K_ENTRY":
            continue  # the probe only reads these, it never patches them
        out.setdefault(int(addr, 16), []).append((p.split("/")[-1], name, kind))
    return out
mine = sites(sys.argv[1], True, "SITES-BEGIN (hooks")
# known overlaps that cannot meet in one boot: somvr patches these only behind a switch, and the second library to load
# finds the slot changed, so its byte check fails and it skips that hook (logged)
EXCLUSIVE = {
    0x34fc838: "somvr's PM (only with SOMVR_FOLLOW=1) vs the probe's P_PMROOT: pmcount/pmmove need SOMVR_FOLLOW off",
    0xab5ea9: "somvr's LOCO_ROOTDELTA (only with SOMVR_LOCO=1) vs the probe's P_ROOTDELTA: the probe's speed/loco words need SOMVR_LOCO off",
    0x34798c0: "somvr's IN_TICK (only with SOMVR_INPUT=1) vs the probe's P_INPUT: the probe's input words need SOMVR_INPUT off",
    0x3479950: "somvr's IN_VALUE (only with SOMVR_INPUT=1) vs the probe's P_VALUE",
    0x3479958: "somvr's IN_VALUE2 (only with SOMVR_INPUT=1) vs the probe's P_VALUE2",
    0x3479968: "somvr's IN_ISON (only with SOMVR_INPUT=1) vs the probe's P_ISON",
    0x741ceb: "somvr's RES_3D_DESC (only with SOMVR_EYE_SIZE) vs the probe's P_RES3D: restall/res need SOMVR_EYE_SIZE unset",
    0x38df398: "somvr's ARM_TRK_EVAL (only with SOMVR_ARMS=1) vs the probe's P_TRK_EVAL: armpose needs SOMVR_ARMS off",
    0xa9a1d0: "somvr's BOW_FA_CAMXF (only with SOMVR_BOW=1) vs the probe's P_BOW_CAMXF: aimray needs SOMVR_BOW off",
    0x3998748: "somvr's TH_PJ_UPDATE (only with SOMVR_THREAT=1) vs the probe's P_PJ_UPDATE: projlog/projblock/projreflect "
               "need SOMVR_THREAT off (or SOMVR_THREAT_PROJ=0)",
    0x26e5468: "somvr's DF_IMPACT (only with SOMVR_DEFENCE=1|log) vs the probe's P_DEALHIT: the probe's GAMEHIT lines need "
               "SOMVR_DEFENCE off (its APPLYDAMAGE/KEYHIT lines, P_APPLYDMG at 0x26e5af5, stay)",
    # P8's pad-guard calls: somvr patches them only with SOMVR_POINTER (its POINTER_SITES block); the probe's padguard
    # and pointer words (and mouselog's guard lines) need SOMVR_POINTER unset
    0xb4e5d8: "somvr's LP_GUARD_HOVER (only with SOMVR_POINTER) vs the probe's P_LP_GUARD_HOVER: padguard/pointer need "
              "SOMVR_POINTER unset",
    0xb4e62c: "somvr's LP_GUARD_CLICK (only with SOMVR_POINTER) vs the probe's P_LP_GUARD_CLICK: padguard/pointer need "
              "SOMVR_POINTER unset",
}
bad, excl = [], []
for other in sys.argv[2:]:
    for addr, who in sites(other, True).items():
        if addr in mine:
            if addr in EXCLUSIVE and all(w[0] == "somvr.c" for w in who):
                excl.append("%#x: %s" % (addr, EXCLUSIVE[addr]))
                continue
            bad.append("%#x: %s and %s" % (addr, mine[addr], who))
for e in excl:
    print("exclusive (switch-gated in somvr): " + e)
if bad:
    sys.exit("site collision (two libraries would patch the same bytes):\n  " + "\n  ".join(bad))
print("%d patched sites, none of them shared with spike1 or somvr%s" % (len(mine), " (apart from the exclusive ones above)" if excl else ""))
PY

echo "== inert outside the game"
T=$(mktemp -d)
SOMVR_PROBE_LOG=$T/probe.log LD_PRELOAD=$D/build/somvr_probe.so /bin/true
SOMVR_PROBE_LOG=$T/probe.log LD_PRELOAD=$D/build/somvr_probe.so python3 -I -c 'pass'
if [ -e "$T/probe.log" ]; then echo "FAIL: the library wrote a log outside the game"; rm -rf -- "$T"; exit 1; fi
rm -rf -- "$T"
echo "inert: /bin/true and python3 ran with the library preloaded and no log was written"

echo "== selftest (hooks, words and the command runner against stand-ins; non-PIE like the game)"
gcc -O1 -g -no-pie -fno-pie -Wall -Wextra -Wshadow -std=gnu11 -o "$D/build/selftest" "$D/selftest.c" -lpthread -lm
T=$(mktemp -d)
"$D/build/selftest" "$T" || { tail -40 "$T/probe.log"; rm -rf -- "$T"; exit 1; }
grep -cE '^(HOOK|DONE)' "$T/probe.log" | sed 's/^/selftest log: /;s/$/ HOOK\/DONE lines/'
rm -rf -- "$T"
