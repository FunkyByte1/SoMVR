#!/bin/bash
# bow20-drive.sh - build 20's bow (handoff/HANDOFF-bow.md "build 20", docs/re-xr-pipeline.md 76), headless at the
# practice arrival; bow19-drive.sh's helpers and real bow-hold poses (the fists upright). Boot (sandbox off) with
# scripts/headset-build20.env (SOMVR_PRACTICE_FAR_CM=1000 SOMVR_BOW_HITLOG=1 added for the aim step):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build20.env | tr '\n' ' ')
#     SOMVR_PRACTICE_FAR_CM=1000 SOMVR_BOW_HITLOG=1 SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt
#     SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time. Pictures: RUN/b20-steps.txt (NAME FRAME), compose with scripts/xr_compose.py.
# B20_STEPS (default: menu auto aim ring) = any of:
#   menu  the wrist panel opened by the palm gesture, then the bow taken out of its slot, drawn (the 19b session's line
#         from the right hand): SOMVR_XR_VRMENU_BOW=close shuts it as the bow comes out
#   auto  the quiver off: the arrow resting on the string, a squeeze there nocks and draws, the shot, the next arrow; the
#         trigger squeezed by the hip, held 1 s, brought to the string; none left (the knocks)
#   aim   an unaware orc 10 m ahead (practice far): shots 3.5 deg right of its head (the 5 deg assist took these), at its
#         head, at its chest - each at a fresh orc; the BOW assist / hit lines and the orc's health
#   ring  pictures drawn: at the orc (from the aim step), at the ground ahead, at the sky
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${B20_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${B20_STEPS:-menu auto aim ring}
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
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$L"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$L")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
cnt() { grep -cE "$1" "${2:-$L}"; }
NP=0; NF=0
seen() { local i; for i in $(seq 1 $(($3 * 4))); do userin && bail "the user is in VR"
  [ "$(cnt "$2" "${5:-$L}")" -gt "$4" ] && { NP=$((NP + 1)); echo "$(date +%T) PASS $1: $(grep -E "$2" "${5:-$L}" | tail -1 | cut -c1-200)"; return 0; }
  sleep 0.25; done; NF=$((NF + 1)); echo "$(date +%T) FAIL $1 (no new '$2')"; return 1; }
ex() { local nm=$1 re=$2 sec=$3 n0; shift 3; n0=$(cnt "$re"); T_S=0.05 t "$@"; seen "$nm" "$re" "$sec" "$n0"; }
exw() { local n0; n0=$(cnt "$2"); w "$4" 0.05; seen "$1" "$2" "$3" "$n0"; }
NOCK='^BOW t=[0-9.]+ frame=[0-9]+ nock: '
pass() { NP=$((NP + 1)); echo "$(date +%T) PASS $*"; }
fail() { NF=$((NF + 1)); echo "$(date +%T) FAIL $*"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/b20-steps.txt"; }

REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
rest() { t "$REST_L" "$REST_R"; }
SLOT_L="hand L head -12 -10 -15 90 0 0"
BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0 s 0.5"   # the string seen at rest (31 cm behind the riser)
DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
HIP="hand R head 30 -45 10 90 0 0"
isout() { grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter '; }
bow_out() { t "$SLOT_L"; sleep 0.3; ex "the bow out" '^BOW .* enter ' 3 "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1; }
out_again() { isout || bow_out; }
grep -E '^(WARN|CONFIG bow (aim|auto_arrow|aim_ring)|CONFIG bow quiver)' "$L" | head -8 | cut -c1-260
grep -E 'xr: vrmenu: SOMVR_XR_VRMENU_BOW' "$X" | head -1
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$X" || { t "btn R a 1"; t "btn R a 0"; }
rest
m a00-start 0.5

opens() { grep -c 'vrmenu: opened by' "$X" 2>/dev/null; }
menu_open() { local o=$(opens) k
  for k in 1 2 3 4; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(opens)" -gt "$o" ] && { echo "OPEN by the palm gesture, try $k"; return 0; }
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  echo "NOT OPEN by the palm gesture"; return 1; }
if has menu; then
  echo "== menu: the wrist panel open (the palm gesture), then the bow out of its slot and drawn"
  isout && { t "$REST_R" "$SLOT_L"; sleep 0.4; ex "put back" '^BOW .* exit: ' 3 "btn L squeeze 0"; sleep 0.6; }
  t "hand R head 12 -22 30 60 0 30"
  if menu_open; then
    d menu-0-open
    nb=$(cnt 'vrmenu: closed: the bow came out' "$X")
    ex "menu: the bow out with the panel open" '^BOW .* enter ' 3 "$SLOT_L" "btn L squeeze 1"   # a quick reach (< 0.5 s away)
    T_S=0.3 t "$BOW_STRAIGHT" "hand R head 12 -22 30 60 0 30"; sleep 1.0; d menu-1-bow-out
    grep -E 'xr: vrmenu: (closed|opened)' "$X" | tail -2 | cut -c1-200
    t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.4; t "btn R trigger 1"; sleep 0.3
    t "$BOW_STRAIGHT" "$DRAWN"; sleep 1.2; d menu-2-drawn
    grep -E 'xr: vrmenu frame [0-9]+: .*layers' "$X" | tail -1 | cut -c1-220
    if [ "$(cnt 'vrmenu: closed: the bow came out' "$X")" -gt "$nb" ]; then pass "menu: the panel closed as the bow came out"
    else fail "menu: the panel NOT closed by the bow (build 19b: it stays, its ray from the right hand)"; fi
    t "hand R head 8 -12 30 90 0 0 s 0.4"; sleep 0.5; t "btn R trigger 0"; sleep 0.8   # let down near the string
    echo "-- the panel opened by the menu button with the bow out (kept: a deliberate open) - its ray as the 19b session saw it"
    t "$BOW_STRAIGHT" "hand R head 10 -20 35 90 0 0"; sleep 0.3
    no=$(opens); t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
    [ "$(opens)" -gt "$no" ] && echo "opened by the menu button" || echo "NOT opened by the menu button"
    t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.4; t "btn R trigger 1"; sleep 0.3
    t "$BOW_STRAIGHT" "$DRAWN"; sleep 1.2; d menu-3-drawn-ray
    grep -E 'xr: vrmenu frame [0-9]+: .*layers' "$X" | tail -1 | cut -c1-220
    t "hand R head 8 -12 30 90 0 0 s 0.4"; sleep 0.5; t "btn R trigger 0"; sleep 0.6
    t "btn L menu 1"; sleep 1; t "btn L menu 0"; sleep 0.8
    grep -E 'xr: vrmenu: (closed|opened)' "$X" | tail -2 | cut -c1-200
  fi
fi
if has auto; then
  echo "== auto: the quiver off - the arrow resting on the string, a squeeze at the string nocks and draws"
  out_again; w "bow arrows 20" 0.6
  t "$BOW_STRAIGHT" "$REST_R"; sleep 1.5; d auto-0-resting
  grep -E '^BOW t=.* game arrow: ' "$L" | tail -1 | cut -c1-160
  t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.8; d auto-1-at-string
  ex "auto: a squeeze at the string nocks" "$NOCK" 3 "btn R trigger 1"
  ex "auto: full draw" '^BOW .* full draw' 4 "$BOW_STRAIGHT" "$DRAWN"; sleep 0.6; d auto-2-drawn
  ex "auto: the shot" '^BOW .* release: ' 3 "btn R trigger 0"
  n0=$(cnt '^BOW t=.* game arrow: resting on the string')
  t "$BOW_STRAIGHT" "$REST_R"; sleep 2; d auto-3-next
  seen "auto: the next arrow on the string after the flight" '^BOW t=.* game arrow: resting on the string' 3 "$n0"
  echo "-- squeezed by the hip, held 1 s, then brought to the string"
  t "$BOW_STRAIGHT" "$HIP"; sleep 0.8; t "btn R trigger 1"; sleep 1.0
  n0=$(cnt "$NOCK"); t "$BOW_STRAIGHT" "$AT_STRING"
  seen "auto: squeezed on the way, held into the string: nocked" "$NOCK" 3 "$n0"
  grep -E '^BOW .* (nock|miss): ' "$L" | tail -1 | cut -c1-200
  t "$BOW_STRAIGHT" "hand R head -4 -11 40 90 0 0 s 0.3"; sleep 0.4; t "btn R trigger 0"; sleep 0.8   # let down
  echo "-- none left"
  w "bow arrows -99" 0.8
  t "$BOW_STRAIGHT" "$REST_R"; sleep 1.5; d auto-4-none
  ex "auto: none left, a squeeze at the string: nothing nocked" '^BOW .* (no arrows left|quiver: empty)' 3 "$AT_STRING" "btn R trigger 1"
  sleep 0.4; t "btn R trigger 0"; sleep 0.4
  grep -E "$NOCK" "$L" | tail -1 | cut -c1-120
  w "bow arrows 30" 0.8
fi

# the aim: the bow hand 60 cm from the anchor beside the jaw (16 right, 22 down: the user's own anchor was 19 -25 0; a hand
# inside the head is pushed out) along yaw A pitch P (head frame, deg), the string hand drawn 57 cm back on the same line
# - the shot's ray is that line (the draw's direction: string hand -> nock)
pose() { awk -v a="$1" -v p="$2" -v d="${3:-57}" -v sm="${4:-}" 'BEGIN { r = 3.14159265 / 180; L = 60
  dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 16; ay = -22; az = 0
  printf "hand L head %.1f %.1f %.1f 90 0 0\n", ax + L * dx - 1.2, ay + L * dy, az + L * dz
  printf "hand R head %.1f %.1f %.1f 90 0 0%s\n", ax + (L - d) * dx, ay + (L - d) * dy, az + (L - d) * dz, sm != "" ? " s " sm : "" }'; }
setpose() { local IFS=$'\n'; t $(pose "$@"); }
aimat() { local l; l=$(grep -E '^BOW target ' "$L" | tail -1)
  echo "$l" | sed -nE "s/.*the ${1:-head} at head yaw ([-+][0-9.]+) pitch ([-+][0-9.]+).*/\1 \2/p"; }
# the target right / up of the live ray (deg; bo_target's line: the ray from the nock as it is now)
offray() { grep -E '^BOW target ' "$L" | tail -1 | sed -nE "s/.*its ${1:-head} ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1 \2/p"; }
A=0; P=0
# aim WHAT DYAW: three absolute aims from the target's bearing seen from the string hand (bo_target's line, 2 Hz) plus the
# offset (bow2-drive.sh's way: the test hands sit in the head's yaw frame), then two corrections on the live ray (the nock
# sits 4 cm up the bow's riser and 1 cm beside it: about 4 deg over the hands' line at a full draw), the draw held
aim() { local k b; for k in 1 2 3; do sleep 1.1; b=$(aimat "$1"); [ -n "$b" ] || { echo "no target line"; return 1; }
    A=$(awk -v x="${b% *}" -v o="$2" 'BEGIN { printf "%.2f", x + o }'); P=$(awk -v x="${b#* }" 'BEGIN { printf "%.2f", x }')
    setpose "$A" "$P" 57; done
  for k in 1 2; do sleep 1.1; b=$(offray "$1"); [ -n "$b" ] || break
    A=$(awk -v a="$A" -v x="${b% *}" -v o="$2" 'BEGIN { printf "%.2f", a + x + o }')
    P=$(awk -v a="$P" -v x="${b#* }" 'BEGIN { printf "%.2f", a + x }'); setpose "$A" "$P" 57; done
  sleep 1.1; echo "$(date +%T) aimed: yaw $A pitch $P; the $1 $(offray "$1" | awk '{ printf "%s right %s up of the ray", $1, $2 }')"; }
# the line's latest meeting (the ring's kind; logged on each change)
meets() { grep -E '^BOW t=.* aim: the line meets' "$L" | tail -1 | sed -E 's/.*the line meets (an orc.s head|an orc.s body|the world|nothing).*/\1/'; }
# search KIND: small steps round the aim until the line meets KIND (head|body) - as a player waits for the ring to turn blue
search() { local dy dp; for dy in 0 0.4 -0.4 0.8 -0.8 1.2 -1.2 1.6 -1.6; do for dp in 0 0.5 -0.5 1.0 -1.0; do
    setpose "$(awk -v a="$A" -v o="$dy" 'BEGIN { printf "%.2f", a + o }')" "$(awk -v a="$P" -v o="$dp" 'BEGIN { printf "%.2f", a + o }')" 57
    sleep 0.35; case "$(meets)" in *"$1"*) echo "$(date +%T) the line meets an orc's $1 at +$dy +$dp"; return 0 ;; esac; done; done
  echo "$(date +%T) the line never met its $1 (last: $(meets))"; return 1; }
health() { grep -E '^BOW (watch|hit|t=.* orc .*health)' "$L" | tail -3 | cut -c1-240; }
# shot NAME WHAT DYAW [KIND]: a fresh unaware orc 10 m ahead, drawn, aimed (KIND: then searched until the line meets its
# head|body), a picture, the release, what it hit (SOMVR_BOW_HITLOG's BOW hit line) and the shot's assist line
shot() { local nm=$1 wh=$2 dy=$3 kind=${4:-} n0 nh
  w "practice remove" 1; sleep 1
  exw "$nm: a far orc" '^PRACTICE t=[0-9.]+ spawn far .* ok' 10 "practice far infantry" || return
  sleep 2.5; w "bow orchp 300" 0.6
  setpose 0 -2 4; sleep 0.4; t "btn R trigger 1"; waitfor "$NOCK" 2
  setpose 0 -2 57 0.8; sleep 1.0
  aim "$wh" "$dy" || return
  [ -n "$kind" ] && search "$kind"
  d "aim-$nm-drawn"
  echo "   the ring: $(meets); $(grep -E 'xr: objective frame .*aim ring' "$X" | tail -1 | sed -E 's/.*aim ring \(([^)]*)\).*/\1/')"
  n0=$(cnt '^BOW t=.* assist: '); nh=$(cnt '^BOW hit t=')
  T_S=0.1 t "btn R trigger 0"
  seen "$nm: the shot" '^BOW t=.* assist: ' 3 "$n0"
  sleep 1.5
  if [ "$(cnt '^BOW hit t=')" -gt "$nh" ]; then echo "   HIT: $(grep -E '^BOW hit t=' "$L" | tail -1 | sed -E 's/.*(the hit node record [^,]*).*/\1/')"
  else echo "   no hit (no BOW hit line)"; fi; }
if has aim; then
  echo "== aim: an unaware orc 10 m ahead; 3.5 deg right of its head (the assist, then exact), its head, its chest"
  out_again; w "bow arrows 30" 0.6
  w "bow aim assist" 0.5; shot beside-assist head 3.5
  w "bow aim exact" 0.5; shot beside-exact head 3.5
  shot head head 0 head
  shot chest chest 0 body
  w "practice remove" 1
fi
if has ring; then
  echo "== ring: drawn at the ground ahead, at the sky"
  out_again
  setpose 0 -2 4; sleep 0.4; t "btn R trigger 1"; waitfor "$NOCK" 2; setpose 0 -2 57 0.8; sleep 1
  setpose 0 -25 57; sleep 1.2; d ring-ground
  grep -E '^BOW t=.* aim: the line meets' "$L" | tail -1 | cut -c1-200
  setpose 20 -12 57; sleep 1.2; d ring-ground-right
  setpose 0 30 57; sleep 1.2; d ring-sky
  grep -E '^BOW t=.* aim: the line meets' "$L" | tail -1 | cut -c1-200
  grep -E 'xr: objective frame .*aim ring' "$X" | tail -3 | cut -c1-200
  setpose 0 0 8 0.5; sleep 0.6; t "btn R trigger 0"; sleep 0.8   # let down
fi
echo "steps done $(date +%T): $NP PASS, $NF FAIL"
grep -E '^BOW END' "$L" | cut -c1-260
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
