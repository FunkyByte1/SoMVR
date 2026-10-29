#!/bin/bash
# ui21-drive.sh - build 21's ui round in one headless boot (docs/re-xr-pipeline.md 82, handoff/HANDOFF-menus.md
# "build 21"): the bow's sight (SOMVR_BOW_AIM_LOOK=sight) against the ground, the sky and the meadow's stones, on an
# orc (blue); the VR menu's Test tab and page (SOMVR_XR_VM_TEST, the build's assets/what-to-test.txt); a bug mark's
# no-voice path (no WiVRn microphone headless; this script never plays audio). bow20-drive.sh's helpers and bow-hold
# poses. Boot (sandbox
# off) with scripts/headset-build21.env:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build21.env | tr '\n' ' ')
#     SOMVR_PRACTICE_FAR_CM=1000 SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
#     setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (U21_WT = a word of the launch command, default this worktree's name). Pictures:
# RUN/u21-steps.txt (NAME FRAME), compose with scripts/xr_compose.py RUN -f FRAME. U21_STEPS (default: sight test mark).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WTD=$(cd "$HERE/.." && pwd -P)
WT=${U21_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${U21_STEPS:-sight test mark}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log
X=$R/xr-layer.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { local x; userin && bail "the user is in VR"; { for x in "$@"; do echo "$x"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$L"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$L")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
cnt() { grep -cE "$1" "${2:-$L}"; }
NP=0; NF=0
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2" "${5:-$L}")" -gt "$4" ] && { pass "$1: $(grep -E "$2" "${5:-$L}" | tail -1 | cut -c1-200)"; return 0; }
  sleep 0.25; done; fail "$1 (no new '$2')"; return 1; }
ex() { local nm=$1 re=$2 sec=$3 n0; shift 3; n0=$(cnt "$re"); T_S=0.05 t "$@"; seen "$nm" "$re" "$sec" "$n0"; }
exw() { local n0; n0=$(cnt "$2"); w "$4" 0.05; seen "$1" "$2" "$3" "$n0"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/u21-steps.txt"; }
NOCK='^BOW t=[0-9.]+ frame=[0-9]+ nock: '
REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
SLOT_L="hand L head -12 -10 -15 90 0 0"
BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
isout() { grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter '; }
bow_out() { t "$SLOT_L"; sleep 0.3; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1; }
bow_away() { isout || return 0; t "$REST_R" "$SLOT_L"; sleep 0.4; ex "the bow put back" '^BOW .* exit: ' 3 "btn L squeeze 0"; sleep 0.6; }
# the bow at yaw A pitch P (head frame, deg) drawn D cm (bow20-drive.sh's pose)
pose() { awk -v a="$1" -v p="$2" -v d="${3:-57}" -v sm="${4:-}" 'BEGIN { r = 3.14159265 / 180; L = 60
  dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 16; ay = -22; az = 0
  printf "hand L head %.1f %.1f %.1f 90 0 0\n", ax + L * dx - 1.2, ay + L * dy, az + L * dz
  printf "hand R head %.1f %.1f %.1f 90 0 0%s\n", ax + (L - d) * dx, ay + (L - d) * dy, az + (L - d) * dz, sm != "" ? " s " sm : "" }'; }
setpose() { local IFS=$'\n'; t $(pose "$@"); }
meets() { grep -E '^BOW t=.* aim: the line meets' "$L" | tail -1 | sed -E 's/.*the line meets (an orc.s head|an orc.s body|the world|nothing).*/\1/'; }
sight() { grep -E 'xr: objective frame .*aim ring' "$X" | tail -1 | sed -E 's/.*aim ring \(([^)]*)\).* m, ([0-9.]+) deg = ([0-9.]+) m across.*/\1, \2 deg = \3 m/'; }
aimat() { grep -E '^BOW target ' "$L" | tail -1 | sed -nE "s/.*the ${1:-body} at head yaw ([-+][0-9.]+) pitch ([-+][0-9.]+).*/\1 \2/p"; }

grep -E '^(WARN|CONFIG bow (aim|aim_ring))' "$L" | head -6 | cut -c1-300
grep -E 'xr: vrmenu: (the Test page|no Test page|SOMVR_XR_VM_TEST)' "$X" | head -2 | cut -c1-300
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$X" || { t "btn R a 1"; t "btn R a 0"; }
t "$REST_L" "$REST_R"; sleep 1

if has sight; then
  echo "== sight: drawn at the ground, around the meadow (its stones), the sky; then on an orc"
  grep -q 'CONFIG bow aim_ring=on .*look sight' "$L" && pass "CONFIG: the sight (look sight, $(grep -o 'a ring [0-9.]* deg' "$L" | head -1))" ||
    fail "CONFIG: no 'look sight' line"
  bow_out; w "bow arrows 30" 0.6
  setpose 0 -2 4; sleep 0.4; t "btn R trigger 1"; waitfor "$NOCK" 2; setpose 0 -2 57 0.8; sleep 1
  for a in "0 -25 ground" "20 -12 ground-right" "0 -6 ahead" "-60 -6 left" "60 -6 right" "0 30 sky" "-30 12 sky-left"; do
    set -- $a
    setpose "$1" "$2" 57; sleep 1.2; d "sight-$3"
    echo "   $(meets): $(sight)"
  done
  grep -q 'aim ring (the world: white)' "$X" && pass "the layer: the world's sight white" || fail "the layer: no 'the world: white' line"
  grep -q 'aim ring (the world: gold)\|aim ring (far: gold)' "$X" && fail "a gold ring line" || pass "no gold ring"
  setpose 0 0 8 0.5; sleep 0.6; t "btn R trigger 0"; sleep 0.8
  echo "-- on an orc 10 m ahead: blue"
  w "practice remove" 1
  exw "a far orc" '^PRACTICE t=[0-9.]+ spawn far .* ok' 10 "practice far infantry"
  sleep 2.5; w "bow orchp 300" 0.6
  setpose 0 -2 4; sleep 0.4; t "btn R trigger 1"; waitfor "$NOCK" 2; setpose 0 -2 57 0.8; sleep 1.2
  b=$(aimat body); A=${b% *}; P=${b#* }; [ -n "$b" ] || { A=0; P=-3; }
  found=0
  for dy in 0 0.5 -0.5 1 -1 1.5 -1.5 2 -2; do for dp in 0 -1 1 -2 2 -3; do
    setpose "$(awk -v a="$A" -v o="$dy" 'BEGIN { printf "%.2f", a + o }')" "$(awk -v a="$P" -v o="$dp" 'BEGIN { printf "%.2f", a + o }')" 57
    sleep 0.35; case "$(meets)" in *"an orc"*) found=1; break 2 ;; esac; done; done
  sleep 1; d sight-orc
  echo "   $(meets): $(sight)"
  [ $found = 1 ] && grep -q "aim ring (an orc's \(body\|head\): blue)" "$X" && pass "on the orc: the sight blue" || fail "on the orc: not met ($(meets))"
  setpose 0 0 8 0.5; sleep 0.6; t "btn R trigger 0"; sleep 0.8
  w "practice remove" 1
fi
if has test; then
  echo "== test: the wrist panel's four tabs, the Test tab, its page"
  bow_away
  AV=$(grep -o 'somvr_vrmenu found at [^ ]* (v[0-9]*, preload pid [0-9]*, avail 0x[0-9a-f]*' "$X" | head -1 | grep -o '0x[0-9a-f]*$')
  [ -n "$AV" ] || bail "no avail in the layer's log"
  AVT=$(printf '0x%x' $((AV | (1 << 63))))
  gcc -std=c11 -D_GNU_SOURCE -I "$WTD/mod/somvr" -o "$R/vm_uv" "$WTD/tools/vm_uv.c" "$WTD/mod/somvr/somvr_vrmenu.c" \
    "$WTD/mod/somvr/somvr_font.c" -lm || bail "vm_uv did not build"
  "$R/vm_uv" "$AVT" > "$R/u21-uv.tsv"
  uv() { awk -F'\t' -v l="$1" -v p="$2" '$1 == l && $2 == p { print $4, $5; exit }' "$R/u21-uv.tsv"; }
  opens() { grep -c 'vrmenu: opened by' "$X" 2>/dev/null; }
  trig() { t "btn R trigger 1"; sleep 0.35; t "btn R trigger 0"; sleep 0.6; }
  o=$(opens)
  t "hand R off"
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6; [ "$(opens)" -gt "$o" ] && break
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  [ "$(opens)" -gt "$o" ] && pass "the panel open" || bail "the panel did not open"
  sleep 2; d test-0-settings
  UT=$(uv Test button); [ -n "$UT" ] || bail "no Test tab in vm_uv's list"
  n0=$(cnt 'xr: vrmenu: click Test button: the test page' "$X")
  t "hand R aim panel $UT 30"; sleep 1.2; trig
  seen "the Test tab: its page" 'xr: vrmenu: click Test button: the test page' 3 "$n0" "$X"
  t "hand R aim panel 0.5 0.45 30"; sleep 1.5; d test-1-page
  UN=$(uv Next button)
  n0=$(cnt 'xr: vrmenu: click Next button' "$X")
  t "hand R aim panel $UN 30"; sleep 1.2; trig
  seen "Next on the Test page" 'xr: vrmenu: click Next button' 3 "$n0" "$X"
  t "hand R aim panel 0.5 0.45 30"; sleep 1.2; d test-2-after-next
  t "hand R off"; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5
  t "$REST_L" "$REST_R"; sleep 1
fi
if has mark; then
  # NEVER play audio here (or anywhere in a test): 2026-10-28 this step's pw-play into a virtual *source* did not link
  # to it and PipeWire sent the user's notes to the default output - their headset. The stop rule is proven offline
  # (scripts/tests/test_voice_rec.py); headless there is no WiVRn microphone, so a mark takes the no-voice path.
  echo "== mark: a bug mark (no WiVRn microphone headless: the note's no-voice path; nothing is played)"
  timeout 5 pactl list short sources 2>/dev/null | grep -qi wivrn && bail "a wivrn source exists: not a headless state"
  w "mark u21-voice" 4; sleep 6
  md=$(ls -dt "$R"/marks/*/ 2>/dev/null | head -1)
  grep -q 'no voice: no WiVRn microphone source' "$md/voice.txt" 2>/dev/null && pass "the mark's voice.txt: $(head -c 120 "$md/voice.txt")" ||
    fail "voice.txt in $md: $(head -c 160 "$md/voice.txt" 2>/dev/null)"
fi
echo "steps done $(date +%T): $NP PASS, $NF FAIL"
grep -E '^BOW END aim' "$L" | cut -c1-260
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
