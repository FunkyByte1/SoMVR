#!/usr/bin/env bash
# test_render_by_hz.sh - offline test of render_by_hz in scripts/som-vr-headset-win.sh (som-b37-looks, docs/re-winport.md
# 7.15): the session's VR graphics preset from the refresh rate - SOMVR_RENDER_BY_HZ=on, the VR menu's saved XR_REFRESH
# (else SOMVR_XR_REFRESH), SOMVR_RENDER_SET_90 / SOMVR_RENDER_SET_120 over SOMVR_RENDER_SET. Launches nothing: the
# function is cut out of the launcher (or LAUNCHER=FILE) and run in a subshell on settings files in a temp folder.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
L=${LAUNCHER:-$HERE/../som-vr-headset-win.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/test_render_by_hz.XXXXXX")
trap 'rm -rf -- "$T"' EXIT
PASS=0 FAIL=0
sed -n '/^render_by_hz() {/,/^}/p' "$L" >"$T/fn.sh"
grep -q '^render_by_hz() {' "$T/fn.sh" || { echo "FAIL the launcher has no render_by_hz"; exit 1; }

# rb SETTINGS_TEXT|- SWITCH... : prints "RSV|RSV_NOTE" (- = no settings file)
rb() {
	local f=$T/settings.conf
	rm -f "$f"
	[ "$1" = - ] || printf '%s\n' "$1" >"$f"
	shift
	bash -c '. "$1"; shift; f=$1; shift; SW=("$@"); render_by_hz "$f"; printf "%s|%s\n" "$RSV" "$RSV_NOTE"' _ "$T/fn.sh" "$f" "$@"
}
is() { # NAME WANT_RSV WANT_NOTE_PART -- rb args
	local name=$1 want=$2 part=$3 out; shift 4
	out=$(rb "$@")
	if [ "${out%%|*}" = "$want" ] && [[ ${out#*|} == *"$part"* ]]; then PASS=$((PASS + 1)); echo "ok   $name"
	else FAIL=$((FAIL + 1)); echo "FAIL $name: got '$out', want '$want' with '$part'"; fi
}
B=SOMVR_RENDER_SET=VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1
LO=SOMVR_RENDER_SET_90=MeshQuality:3,ShadowQuality:3,LightingQuality:2
HI=SOMVR_RENDER_SET_120=FrameRateLimit:3
is "off (the code default): the preset alone" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1" "" -- \
	"XR_REFRESH=90" "$B" "$LO" "$HI"
is "off by value" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1" "" -- \
	"XR_REFRESH=90" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=off
is "90 Hz saved: the 90 overlay over the preset, its names replaced" \
	"VegetationQuality:3,MeshQuality:3,ShadowQuality:3,LightingQuality:2" "90 Hz (the VR menu's saved rate): SOMVR_RENDER_SET_90" -- \
	"$(printf 'TURN=smooth\nXR_REFRESH=90')" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=80
is "72 Hz saved: the 90 overlay" "VegetationQuality:3,MeshQuality:3,ShadowQuality:3,LightingQuality:2" "72 Hz" -- \
	"XR_REFRESH=72" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on
is "120 Hz saved: the 120 overlay added" \
	"VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1,FrameRateLimit:3" "120 Hz (the VR menu's saved rate): SOMVR_RENDER_SET_120" -- \
	"XR_REFRESH=120" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=80
is "no settings file: SOMVR_XR_REFRESH's 80" "VegetationQuality:3,MeshQuality:3,ShadowQuality:3,LightingQuality:2" "80 Hz (SOMVR_XR_REFRESH)" -- \
	- "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=80
is "the file without XR_REFRESH: the switch" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1,FrameRateLimit:3" "120 Hz (SOMVR_XR_REFRESH)" -- \
	"TURN=smooth" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=120
is "a bad saved value: the switch" "VegetationQuality:3,MeshQuality:3,ShadowQuality:3,LightingQuality:2" "(SOMVR_XR_REFRESH)" -- \
	"XR_REFRESH=fast" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=90
is "a saved rate out of range (500): the switch" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1,FrameRateLimit:3" "(SOMVR_XR_REFRESH)" -- \
	"XR_REFRESH=500" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=120
is "the rate unknown (the runtime's own, 0): the preset alone" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1" "not known" -- \
	- "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on SOMVR_XR_REFRESH=0
is "the last XR_REFRESH line wins" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1,FrameRateLimit:3" "120 Hz" -- \
	"$(printf 'XR_REFRESH=90\nXR_REFRESH=120')" "$B" "$LO" "$HI" SOMVR_RENDER_BY_HZ=on
is "an overlay off: the preset alone" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1" "SOMVR_RENDER_SET_90 not set" -- \
	"XR_REFRESH=90" "$B" SOMVR_RENDER_SET_90=off "$HI" SOMVR_RENDER_BY_HZ=on
is "an overlay missing: the preset alone" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1" "SOMVR_RENDER_SET_120 not set" -- \
	"XR_REFRESH=120" "$B" "$LO" SOMVR_RENDER_BY_HZ=on
is "no preset, an overlay: the overlay alone" "FrameRateLimit:3" "120 Hz" -- \
	"XR_REFRESH=120" "$HI" SOMVR_RENDER_BY_HZ=on
is "a name not in the preset: added at the end" "VegetationQuality:3,MeshQuality:2,ShadowQuality:2,LightingQuality:1,AmbientOcclusionQuality:2" "90 Hz" -- \
	"XR_REFRESH=90" "$B" SOMVR_RENDER_SET_90=AmbientOcclusionQuality:2 SOMVR_RENDER_BY_HZ=on
# the repo's switches file as it is: whatever it sets, the result is one of its values and only NAME:0..5 entries
ENVF=$HERE/../headset-build26.env
if [ -r "$ENVF" ]; then
	mapfile -t SWF < <(grep -E '^SOMVR_(RENDER_SET|RENDER_SET_90|RENDER_SET_120|RENDER_BY_HZ|XR_REFRESH)=' "$ENVF")
	for hz in 72 90 120; do
		out=$(rb "XR_REFRESH=$hz" "${SWF[@]}")
		r=${out%%|*}
		if [ -n "$r" ] && [[ ,$r, =~ ^(,[A-Za-z]+:[0-5])+,$ ]]; then PASS=$((PASS + 1)); echo "ok   headset-build26.env at $hz Hz: $r"
		else FAIL=$((FAIL + 1)); echo "FAIL headset-build26.env at $hz Hz: '$out'"; fi
	done
	# the coordinator's flip (SOMVR_RENDER_BY_HZ=on after the file's lines): its preset at 90 Hz, + its 120 overlay at 120
	base=$(grep -E '^SOMVR_RENDER_SET=' "$ENVF" | tail -1); base=${base#*=}
	hi=$(grep -E '^SOMVR_RENDER_SET_120=' "$ENVF" | tail -1); hi=${hi#*=}
	is "headset-build26.env switched on, 90 Hz: its preset" "$base" "90 Hz" -- "XR_REFRESH=90" "${SWF[@]}" SOMVR_RENDER_BY_HZ=on
	[ -n "$hi" ] && is "headset-build26.env switched on, 120 Hz: + SOMVR_RENDER_SET_120" "$base,$hi" "120 Hz" -- \
		"XR_REFRESH=120" "${SWF[@]}" SOMVR_RENDER_BY_HZ=on
fi
echo "test_render_by_hz: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
