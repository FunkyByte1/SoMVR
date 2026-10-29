#!/usr/bin/env bash
# test_som_vr_headset.sh - offline test of the switches-file check in scripts/som-vr-headset.sh (load_switches: the
# frozen build's headset.env, read line by line and checked, never sourced). Launches nothing: the function and the
# launcher's FIXED list are cut out of the launcher (or of LAUNCHER=FILE, e.g. an older copy) and run in a subshell on
# files in a temp folder.
# Checks: the build's own switches files (scripts/headset-build*.env) pass, with their switch counts; comments, blanks,
# CRLF, a missing final newline, '# ...' after a value; build 23: up to 2000 lines (1999 comments + 1 switch; 2000 ok,
# 2001 refused), values with '|', ';' and ',' (the HUD's tile sets: SOMVR_XR_PROMPTS_RECTS) kept verbatim as one word;
# still refused: a space, quotes, '$', a backtick, '(', '&', '<', '>', '*', '!', '\', a non-SOMVR_ key, the launcher's own
# keys, a key over 64 or a value over 256 characters, a malformed line, a file with no switch, an unreadable file.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
L=${LAUNCHER:-$HERE/../som-vr-headset.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/test_som_vr_headset.XXXXXX")
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
PASS=0 FAIL=0
ok() { PASS=$((PASS + 1)); echo "ok   $*"; }
no() { FAIL=$((FAIL + 1)); echo "FAIL $*"; }
check() { local d=$1; shift; if "$@"; then ok "$d"; else no "$d"; fi; }

# the function and FIXED, cut out of the launcher (from its first line to the line `}` that closes it)
{ grep -m1 '^FIXED=' "$L"; sed -n '/^load_switches() {/,/^}/p' "$L"; } >"$T/fn.sh"
check "the launcher has load_switches and FIXED" bash -c 'grep -q "^load_switches() {" "$1" && grep -q "^FIXED=" "$1"' _ "$T/fn.sh"

# ld FILE: 0 = taken; prints "N" (switches) or the refusal; the words one per line in $T/words
ld() {
	bash -c '. "$1"; if load_switches "$2"; then printf "%s\n" "${SW[@]}" >"$3"; echo "${#SW[@]}"; else echo "BAD: $ENV_BAD"; exit 1; fi' \
		_ "$T/fn.sh" "$1" "$T/words"
}
taken() { local out; out=$(ld "$1") && [ "$out" = "$2" ]; }        # FILE COUNT
refused() { local out; ! out=$(ld "$1") && [[ $out == "BAD: "*"$2"* ]]; } # FILE PART-OF-REASON
word() { grep -qxF -- "$1" "$T/words"; }

# the repo's switches files: every one passes (build 23's has 271 switches at the round's start)
for f in "$HERE"/../headset-build*.env; do
	[ -e "$f" ] || continue
	n=$(grep -cE '^[[:space:]]*SOMVR_[A-Z0-9_]+=' "$f")
	check "$(basename "$f"): taken, $n switches" taken "$f" "$n"
done

# the basics
printf '# a comment\n\nSOMVR_A=1\n  SOMVR_B=on   # why\r\nSOMVR_C=x' >"$T/basic.env"
check "comments, blanks, CRLF, a trailing comment, no final newline: 3 switches" taken "$T/basic.env" 3
check "... the values as given" bash -c 'grep -qx SOMVR_A=1 "$1" && grep -qx SOMVR_B=on "$1" && grep -qx SOMVR_C=x "$1"' _ "$T/words"

# build 23: the line limit 2000 (was 1000; build 22's file 911 lines)
{ for i in $(seq 1 1999); do echo "# line $i"; done; echo "SOMVR_LAST=1"; } >"$T/l2000.env"
check "2000 lines (1999 comments + a switch): taken" taken "$T/l2000.env" 1
{ for i in $(seq 1 1500); do echo "# line $i"; done; echo "SOMVR_LAST=1"; } >"$T/l1501.env"
check "1501 lines: taken (refused before build 23: over 1000)" taken "$T/l1501.env" 1
{ for i in $(seq 1 2000); do echo "# line $i"; done; echo "SOMVR_LAST=1"; } >"$T/l2001.env"
check "2001 lines: refused" refused "$T/l2001.env" "more than 2000 lines"

# build 23: '|' and ';' (and ',') in a value - the HUD's tile sets
R='SOMVR_XR_PROMPTS_RECTS=640,10,640,110|440,600,1040,156|30,390,290,190;1120,940,800,140'
printf '%s\nSOMVR_X=a;b\nSOMVR_Y=a|b\nSOMVR_Z=1,2,3\n' "$R" >"$T/sep.env"
check "'|', ';' and ',' in values: taken (build 22: '|' and ';' refused)" taken "$T/sep.env" 4
check "... the tile set kept verbatim, one word" word "$R"
check "... 'a;b' and 'a|b' kept verbatim" bash -c 'grep -qxF "SOMVR_X=a;b" "$1" && grep -qxF "SOMVR_Y=a|b" "$1"' _ "$T/words"
printf 'SOMVR_R=1,2,3,4|5,6,7,8 # the rects\n' >"$T/sepc.env"
check "a tile set with a trailing comment: taken" taken "$T/sepc.env" 1

# still refused: whatever a shell or a parser could take for more than a word
n=0
for v in 'a b' "a'b" 'a"b' 'a$b' 'a`b`' 'a(b)' 'a&b' 'a<b' 'a>b' 'a*b' 'a!b' 'a\b' '$(id)' 'a{b}' 'a~b' 'a=b?' 'a[b]'; do
	n=$((n + 1))
	printf 'SOMVR_OK=1\nSOMVR_V=%s\n' "$v" >"$T/v$n.env"
	if [ "$v" = 'a b' ]; then
		check "value 'a b' (a space): refused" refused "$T/v$n.env" "line 2 is not KEY=VALUE"
	else
		check "value '$v': refused" refused "$T/v$n.env" "not a plain switch value"
	fi
done
printf 'SOMVR_V=a#b\n' >"$T/hash.env"
check "a '#' ends the value (a comment): 'a#b' -> 'SOMVR_V=a'" bash -c '
	. "$1"; load_switches "$2" && [ "${SW[*]}" = "SOMVR_V=a" ]' _ "$T/fn.sh" "$T/hash.env"
printf 'PATH=/x\n' >"$T/k1.env"
check "a non-SOMVR_ key: refused" refused "$T/k1.env" "is not a SOMVR_ key"
printf 'SOMVR_LOG=/x\n' >"$T/k2.env"
check "the harness's key SOMVR_LOG: refused" refused "$T/k2.env" "is the launcher's own"
printf 'SOMVR_XR_LOG=/x\n' >"$T/k3.env"
check "the launcher's key SOMVR_XR_LOG: refused" refused "$T/k3.env" "is the launcher's own"
printf 'XR_LOADER_DEBUG=all\nVK_LOADER_DEBUG=error\n' >"$T/k4.env"
check "XR_LOADER_DEBUG and VK_LOADER_DEBUG: taken" taken "$T/k4.env" 2
printf 'SOMVR_%s=1\n' "$(printf 'A%.0s' $(seq 1 60))" >"$T/k5.env"
check "a 66-character key: refused" refused "$T/k5.env" "not a plain switch value"
printf 'SOMVR_V=%s\n' "$(printf '1%.0s' $(seq 1 257))" >"$T/v257.env"
check "a 257-character value: refused" refused "$T/v257.env" "not a plain switch value"
printf 'SOMVR_V=%s\n' "$(printf '1;%.0s' $(seq 1 128))" >"$T/v256.env"
check "a 256-character value of '1;' pairs: taken" taken "$T/v256.env" 1
printf 'just words\n' >"$T/m1.env"
check "a line without '=': refused" refused "$T/m1.env" "line 1 is not KEY=VALUE"
printf 'export SOMVR_A=1\n' >"$T/m2.env"
check "'export KEY=VALUE': refused" refused "$T/m2.env" "line 1 is not KEY=VALUE"
printf 'SOMVR_A=1 SOMVR_B=2\n' >"$T/m3.env"
check "two switches on a line: refused" refused "$T/m3.env" "line 1 is not KEY=VALUE"
printf '# only comments\n\n' >"$T/e.env"
check "no switches: refused" refused "$T/e.env" "no switches in it"
check "a missing file: refused" refused "$T/none.env" "cannot read it"

echo "PASS $PASS FAIL $FAIL"
[ "$FAIL" = 0 ]
