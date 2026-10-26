#!/usr/bin/env bash
# build.sh - build somvr_spike1.so into build/; --check also verifies the patch sites against the game binary on disk
# (read-only) and that the library stays inert outside the game.
#   ./build.sh            build
#   ./build.sh --check    build, then check_sites.py, the inert tests and selftest.c
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$D/build"
gcc -O2 -g -fPIC -shared -fvisibility=hidden -Wall -Wextra -std=gnu11 -Wl,-z,defs \
    -o "$D/build/somvr_spike1.so" "$D/somvr_spike1.c" -lpthread -lm
echo "built $D/build/somvr_spike1.so ($(sha256sum "$D/build/somvr_spike1.so" | cut -c1-16))"
[ "${1:-}" = --check ] || exit 0

python3 -I "$D/check_sites.py" "$D/somvr_spike1.c" "${SOM_BIN:-$D/../../reference/bin/linux/ShadowOfMordor}"

# inert outside the game: even with SOMVR_LOG set, other programs must not write a log
T=$(mktemp -d)
SOMVR_LOG=$T/somvr.log LD_PRELOAD=$D/build/somvr_spike1.so /bin/true
SOMVR_LOG=$T/somvr.log LD_PRELOAD=$D/build/somvr_spike1.so python3 -I -c 'pass'
if [ -e "$T/somvr.log" ]; then echo "FAIL: the library wrote a log outside the game"; rm -rf -- "$T"; exit 1; fi
rm -rf -- "$T"
echo "inert: /bin/true and python3 ran with the library preloaded and no log was written"

# the hook machinery against stand-in functions (non-PIE like the game, so the stub page is in reach)
gcc -O2 -g -no-pie -fno-pie -Wall -Wextra -std=gnu11 -o "$D/build/selftest" "$D/selftest.c" -lpthread -lm
T=$(mktemp -d)
"$D/build/selftest" "$T" || { cat "$T/somvr.log"; rm -rf -- "$T"; exit 1; }
grep -E '^(HOOK|CONFIG|TRIGGER|COMFORT|GUARD|SAVE)' "$T/somvr.log"
rm -rf -- "$T"
