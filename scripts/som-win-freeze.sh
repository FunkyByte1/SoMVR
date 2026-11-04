#!/bin/bash
# som-win-freeze.sh - freeze a Windows VR build for the user's launcher (scripts/som-vr-headset-win.sh, WIN_BUILD=):
# logs/NAME/ = the committed HEAD of a checkout (git archive: ShadowOfMordor/mod + scripts/handmesh_gen.py, the
# reference and third_party links), built there (build.sh --check: the Linux layer and every check -> check.txt;
# win/build.sh --check: the DLL -> win-check.txt), its switches (headset.env: a copy of ENV, checked by the launcher's
# rules at the next start), a frozen copy of Proton's files (proton/, cp --reflink: no extra space on this btrfs disk;
# the version in proton.version) and FROZEN.txt (the commit, the sha256 of the DLL, the layer, the env).
#   som-win-freeze.sh NAME [--from CHECKOUT] [--env FILE]
#     CHECKOUT  a flat2vr checkout (default: the one this script is in); its HEAD is frozen, uncommitted changes are not
#     FILE      the switches (default: CHECKOUT/ShadowOfMordor/scripts/headset-build26.env, the headset build's)
# Then: WIN_BUILD=NAME in scripts/som-vr-headset-win.sh (main), and the launcher's dry run:
#   SOMVR_HEADSET_DRYRUN=1 ShadowOfMordor/scripts/som-vr-headset-win.sh
# Run it OUTSIDE the Bash sandbox (a few minutes).
set -uo pipefail
N=${1:?usage: som-win-freeze.sh NAME [--from CHECKOUT] [--env FILE]}; shift
[[ $N =~ ^[A-Za-z0-9._-]+$ ]] || { echo "STOP: NAME $N"; exit 1; }
FROM=$(cd "$(dirname "$0")/../.." && pwd); ENVF=""
while [ $# -gt 0 ]; do
  case $1 in --from) FROM=$(cd "${2:?}" && pwd) || exit 1; shift 2 ;; --env) ENVF=${2:?}; shift 2 ;; *) echo "STOP: $1"; exit 1 ;; esac
done
[ -n "$ENVF" ] || ENVF=$FROM/ShadowOfMordor/scripts/headset-build26.env
[ -r "$ENVF" ] || { echo "STOP: no switches file $ENVF"; exit 1; }
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
PROTON_FILES="/home/bara/.local/share/Steam/steamapps/common/Proton - Experimental/files"
B=$SOM/logs/$N
[ -e "$B" ] && { echo "STOP: $B exists"; exit 1; }
mkdir -p "$B" || exit 1
git -C "$FROM" archive HEAD ShadowOfMordor/mod ShadowOfMordor/scripts/handmesh_gen.py | tar -x -C "$B" || { echo "STOP: git archive"; exit 1; }
ln -s "$SOM/reference" "$B/ShadowOfMordor/reference"
ln -s "$SOM/third_party" "$B/ShadowOfMordor/third_party"
git -C "$FROM" log -1 --format='%H %s' > "$B/HEAD.txt"
cp "$ENVF" "$B/headset.env"
echo "frozen $(cut -c1-12 "$B/HEAD.txt" | head -1) into $B; switches: $(grep -cE '^SOMVR_' "$B/headset.env") from $ENVF"
cd "$B/ShadowOfMordor/mod/somvr" || exit 1
timeout 1500 ./build.sh --check > "$B/check.txt" 2>&1; rc=$?
echo "build.sh --check exit $rc: $(tail -1 "$B/check.txt")"
timeout 900 ./win/build.sh --check > "$B/win-check.txt" 2>&1; wrc=$?
echo "win/build.sh --check exit $wrc: $(tail -1 "$B/win-check.txt")"
DLL=$B/ShadowOfMordor/mod/somvr/build/win/xinput9_1_0.dll
SO=$B/ShadowOfMordor/mod/somvr/vklayer/build/libVkLayer_somvr_openxr.so
[ -f "$DLL" ] && [ -f "$SO" ] || { echo "STOP: no DLL or layer after the builds (see check.txt, win-check.txt)"; exit 1; }
LDLL=$(dirname "$DLL")/somvr_logic.dll # a split build (docs/re-xr-pipeline.md 156): the host and its logic module
[ -f "$B/ShadowOfMordor/mod/somvr/win/somvr_win_host.c" ] && [ ! -f "$LDLL" ] &&
  { echo "STOP: a split build without its somvr_logic.dll (see win-check.txt)"; exit 1; }
if cp -a --reflink=always "$PROTON_FILES" "$B/proton" 2> "$B/proton-copy.err"; then
  cp "$(dirname "$PROTON_FILES")/version" "$B/proton.version" 2>/dev/null
  echo "Proton frozen: $(cat "$B/proton.version" 2>/dev/null) (reflink copy of $PROTON_FILES)"
  rm -f "$B/proton-copy.err"
else
  rm -rf "$B/proton"
  echo "!!! Proton not frozen ($(head -c 200 "$B/proton-copy.err")): the sessions use Steam's Proton - Experimental as it is"
fi
{ cat "$B/HEAD.txt"
  echo "build.sh --check exit $rc; win/build.sh --check exit $wrc"
  echo "dll $(sha256sum < "$DLL" | cut -c1-64)"
  [ -f "$LDLL" ] && echo "logic $(sha256sum < "$LDLL" | cut -c1-64)"
  echo "layer $(sha256sum < "$SO" | cut -c1-64)"
  echo "env $(sha256sum < "$B/headset.env" | cut -c1-64) ($(grep -cE '^SOMVR_' "$B/headset.env") switches, from $ENVF)"
  echo "proton $(cat "$B/proton.version" 2>/dev/null || echo "Steam's, not frozen")"
} > "$B/FROZEN.txt"
cat "$B/FROZEN.txt"
[ $rc = 0 ] && [ $wrc = 0 ] || { echo "!!! a check failed: do not point WIN_BUILD at $N before it is understood"; exit 1; }
echo "next: WIN_BUILD=$N in scripts/som-vr-headset-win.sh, then SOMVR_HEADSET_DRYRUN=1 scripts/som-vr-headset-win.sh"
