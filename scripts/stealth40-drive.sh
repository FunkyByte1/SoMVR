#!/bin/bash
# stealth40-drive.sh - build 40 (som-b40-stealth; docs/re-xr-pipeline.md 175): SOMVR_STEALTH_TOUCH in one headless boot
# (scripts/headless_vr.sh --win with scripts/headset-build26.env (its som-b40-stealth block), SOMVR_HANDS_TEST=1,
# SOMVR_XR_TEST=@RUN@/xrtest.txt). The crouch is the player's own duck at the user's depth (the test head 32 cm under
# the standing eyes: their sessions' "duck on" at 31 cm), never the crouch word; the hands are held as real hands hold
# (OpenXR grip: -Z pinky -> thumb). S40_STEPS (default all, in order):
#   old    the A/B's old side (`stealth touch off`: build 39's senses): ducked behind the far unaware orc, placed at
#          160 .. 88 cm (`verbs behind`, 3.5 s still at each) until it notices; which sense passed (the TOUCH lines)
#   near   `stealth touch on`: a new far orc, ducked from 220 cm behind it the stick's sneak (`stealth chase 0.6 40`)
#          all the way in - the game's bodies stop the root at ~85 cm behind an infantry - 5 s there; then leaned in
#          (S40_LEAN_CM, 15) until the eyes are within ~40 cm of its root point; both eyes' pictures; unaware throughout
#   grab   (after near, at 40 cm) the knife from the right hip, the LEFT hand on its shoulder (the knife's hold), the
#          push (deep and steady, 40 cm in 1 s; then lower; then knife37's): the knife's stealth kill; no player or
#          view motion
#   front  a second orc, Talion 10 m in front of it (`verbs front 1000`), ducked, the stick's sneak at it: it notices
#          him at its sight's range (metres, not the touch), no sense of it cut (the walk stuck on the ground: placed
#          4 m in front of it, still)
# S40_RUN=RUN drives a running boot; else S40_OUT=FILE (headless_vr.sh's output: its "windows: RUN" line) or the newest
# run of this worktree. Lines: RUN/s40-steps.txt (PASS / FAIL, the dumps' frames for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${S40_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${S40_STEPS:-old near grab front}
T0=$(date +%s)
R=${S40_RUN:-}
for i in $(seq 1 900); do
  [ -n "$R" ] && break
  if [ -n "${S40_OUT:-}" ]; then
    R=$(grep -o 'windows: [^ ]*' "$S40_OUT" 2>/dev/null | head -1 | cut -d' ' -f2)
  else
    for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
      [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
    done
  fi
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/s40-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$L}" 2>/dev/null; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -E "$2"; }
waitfor_n() { local i; for i in $(seq 1 $((${3:-4} * 4))); do [ "$(cnt "$1")" -gt "$2" ] && return 0; sleep 0.25; done; return 1; }
waitfor() { local n0 i; n0=$(cnt "$1" "${3:-$L}"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1" "${3:-$L}")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$S"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$S"; }
dp() { x "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
       echo "dump $1 $f" | tee -a "$S"; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
for i in $(seq 1 300); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
grep -E '^(WARN|CONFIG knife)' "$L" | cut -c1-300

# ---- the hands (cm right / up / ahead of the head, deg; the head's yaw frame) -------------------------------------
# `execute face` turns Talion by snap steps: the orc is left up to ~23 deg off the head's forward (boot 124028: the
# sword's thrust passed 50 cm beside it). aim: face, then the orc's bearing from `execute where`; hp: a pose given in
# the orc's frame (z toward it) turned onto that bearing (yaw: - = right)
BRG=0
aim() { local d; w "execute face" 0.5; w "execute where" 0.6
  d=$(grep -E '^EXECUTE where ' "$L" | tail -1 | grep -oE 'bearing [-+]?[0-9]+' | grep -oE '[-+]?[0-9]+$'); BRG=${d#+}; BRG=${BRG:-0}
  echo "$(date +%T) the orc's bearing $BRG deg from the head"; }
hp() { local k=$1 s=$2; shift 2
  awk -v b="$BRG" -v k="$k" -v s="$s" -v x="$1" -v y="$2" -v z="$3" -v rx="$4" -v ry="$5" -v rz="$6" -v ms="${7:-}" 'BEGIN {
    r = b * 3.14159265 / 180; printf "hands %s %s %.1f %s %.1f %s %.1f %s%s", k, s, x * cos(r) + z * sin(r), y,
    -x * sin(r) + z * cos(r), rx, ry - b, rz, ms != "" ? " " ms : "" }'; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
RHIP="hands test R 18 -62 -5 0 0 0"
# the knife's blade as the judge has it (calibrated from `knife where` after each draw): its point's offset from the
# grip - along the blade (KL), off its line (KO*) - and the yaw error (KE: its way less the commanded one, deg)
KL=30; KOX=0; KOY=0; KOZ=0; KE=0
poses() { # (after aim) from `knife where`: the orc's torso-to-neck middle in the head's frame, S = its surface toward
  # Talion (15 cm nearer along the line from the eyes); every pose on that line. pk: the knife's POINT at Dt along the
  # line from S (side to its right, up), the blade's way turned by yo (-90 = to the right: along his back), the grip
  # placed from the calibration; ph: a hand's grip there (the left hand, the sword's grip with its 1 m blade straight
  # ahead). (Boots 124727/125435/154041: the eyes sit 13-60 cm off where fixed poses assumed; the blade 5-12 cm off the
  # grip-based guess: both measured instead)
  local n0w; n0w=$(cnt '^KNIFE where t=.* torso '); w "knife where" 0.3
  waitfor_n '^KNIFE where t=.* torso ' "$n0w" 4 || echo "$(date +%T) no fresh knife where line"
  local l; l=$(grep -E '^KNIFE where t=.* torso ' "$L" | tail -1)
  [ -n "$l" ] || { echo "$(date +%T) no knife where line"; l="torso 0 -45 130 neck 0 -15 140"; }
  echo "$(date +%T) $(echo "$l" | grep -oE 'torso [-0-9 ]+neck [-0-9 ]+head [-0-9 ]+' ; echo "$l" | grep -oE 'it faces [-+0-9]+ deg')"
  WHERE=$(echo "$l" | grep -oE 'torso [-0-9]+ [-0-9]+ [-0-9]+ neck [-0-9]+ [-0-9]+ [-0-9]+' | tr -d 'a-z' | tr -s ' ')
  geo='n = split(W, v, " "); mx = (v[1] + v[4]) / 2; my = (v[2] + v[5]) / 2; mz = (v[3] + v[6]) / 2
    h = sqrt(mx * mx + mz * mz); dx = mx / h; dz = mz / h; px = dz; pz = -dx; P = 3.14159265
    sx = mx - 15 * dx; sz = mz - 15 * dz; a = atan2(dx, dz) * 180 / P'
  ph() { awk -v W="$WHERE" -v k="$1" -v s="$2" -v D="$3" -v S="$4" -v U="$5" -v yo="$6" -v ms="${7:-}" "BEGIN { $geo
    printf \"hands %s %s %.1f %.1f %.1f 0 %.1f 0%s\", k, s, sx + D * dx + S * px, my + U, sz + D * dz + S * pz, -a + yo,
      ms != \"\" ? \" \" ms : \"\" }"; }
  pk() { awk -v W="$WHERE" -v k="$1" -v s="$2" -v D="$3" -v S="$4" -v U="$5" -v yo="$6" -v ms="${7:-}" -v KL="$KL" \
      -v ox="$KOX" -v oy="$KOY" -v oz="$KOZ" -v KE="$KE" "BEGIN { $geo
    tx = sx + D * dx + S * px; ty = my + U; tz = sz + D * dz + S * pz; ab = (a - yo) * P / 180
    gx = tx - KL * sin(ab) - ox; gy = ty - oy; gz = tz - KL * cos(ab) - oz
    printf \"hands %s %s %.1f %.1f %.1f 0 %.1f 0%s\", k, s, gx, gy, gz, -a + yo + KE, ms != \"\" ? \" \" ms : \"\" }"; }
  KREADY=$(pk test R -10 0 0 0)            # the point 10 cm short of him
  KPUSH=$(pk swing R 14 0 0 0 600)         # the deliberate push: 24 cm in 0.6 s, the point 14 cm in
  KPULL=$(pk swing R -16 0 0 0 1000)       # drawn back out slowly
  KBRUSH0=$(pk test R 6 22 0 0)            # the point 6 cm in, 22 cm right of the line ...
  KBRUSH1=$(pk swing R 6 -22 0 0 1200)     # ... dragged across his back to the left (0.37 m/s sideways)
  KFLAT0=$(pk test R 1 -5 0 -90)           # the blade flat across him (its point to the right), 1 cm in, from the left ...
  KFLAT1=$(pk swing R 1 35 0 -90 1000)     # ... slid along itself across his back
  LGRAB=$(ph test L -8 -6 10 0); LGRAB2=$(ph test L -4 -4 22 0); LGRAB3=$(ph test L -12 -8 0 0) # his shoulder, his neck
  KSTAB0=$(pk test R -30 0 0 0); KSTAB1=$(pk swing R 15 0 0 0 250) # the fight: quick stabs
  # the sword (1 m, its grip 100 cm behind its point): from 40 cm short to 30 cm in, 0.45 s - the weighted sword
  # (SOMVR_SWORD_WEIGHT) lags a 0.25 s thrust into the body after the hand stopped: "carried" (boots 154041/155214)
  READY2=$(ph test R -140 0 0 0); THRUST=$(ph swing R -70 0 0 0 450)
  echo "$(date +%T) poses: ready $KREADY | push $KPUSH | grab $LGRAB"
}
# after a draw, the knife at KREADY: the judge's blade against the grip -> KL, KO*, KE; the poses again
kcal() { w "$KREADY" 1.0; local l n0b k gx gz
  for k in 1 2 3; do
    n0b=$(cnt "^KNIFE where t=.* the knife's blade"); w "knife where" 0.3
    waitfor_n "^KNIFE where t=.* the knife's blade" "$n0b" 4 || continue
    l=$(grep -E "^KNIFE where t=.* the knife's blade" "$L" | tail -1)
    gx=$(echo "$KREADY" | awk '{print $4}'); gz=$(echo "$KREADY" | awk '{print $6}')
    echo "$l" | awk -v gx="$gx" -v gz="$gz" '{ for (i = 1; i <= NF; i++) if ($i == "R") { x = $(i+1); z = $(i+3) } }
      END { exit !((x - gx) ^ 2 + (z - gz) ^ 2 < 4) }' && break
    echo "$(date +%T) the blade line's right grip is not the ready pose yet: again"; l=""; sleep 0.5
  done
  [ -n "$l" ] || { echo "$(date +%T) no knife blade line at the ready pose"; return 1; }
  local cmd; cmd=$(echo "$KREADY" | awk '{print $8}')
  read KL KOX KOY KOZ KE < <(echo "$l" | awk -v cy="$cmd" '{ for (i = 1; i <= NF; i++) { if ($i == "base") { bx=$(i+1); by=$(i+2); bz=$(i+3) }
      if ($i == "tip") { tx=$(i+1); ty=$(i+2); tz=$(i+3) } if ($i == "R") { rx=$(i+1); ry=$(i+2); rz=$(i+3) } }
    P = 3.14159265; ux = tx - bx; uy = ty - by; uz = tz - bz; ul = sqrt(ux*ux + uy*uy + uz*uz); ux /= ul; uy /= ul; uz /= ul
    vx = tx - rx; vy = ty - ry; vz = tz - rz; L = vx*ux + vy*uy + vz*uz
    am = atan2(ux, uz) * 180 / P; e = am - (-cy); while (e > 180) e -= 360; while (e < -180) e += 360
    printf "%.1f %.1f %.1f %.1f %.1f\n", L, vx - L*ux, vy - L*uy, vz - L*uz, e }')
  echo "$(date +%T) the knife's blade: its point $KL cm along it from the grip, off its line $KOX $KOY $KOZ, the yaw $KE deg off ($l)" | cut -c1-330
  poses; w "$KREADY" 0.8; }
sword_state() { grep -E '^SLOTS t=.* sword: .*now (in the hand|sheathed|on the back)' "$L" | tail -1 | grep -oE 'now (in the hand|sheathed|on the back)'; }
sword_away() { [ "$(sword_state)" = "now in the hand" ] || return 0
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.6; w "$REST" 0.8; }
sword_draw() { [ "$(sword_state)" = "now in the hand" ] && return 0
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 0.6; }
knife_draw() { local n0 k; for k in 1 2; do n0=$(lines); sword_away; w "hands squeeze R 0" 0.4; w "$RHIP" 1.0; w "hands squeeze R 1" 1.2
  since "$n0" '^SLOTS t=.* dagger: (drawn|draw asked)' | grep -q 'R_Weapon' && return 0; done
  echo "$(date +%T) the knife not drawn: $(since "$n0" '^SLOTS t=' | head -2 | cut -c1-200)"; return 1; }
knife_away() { w "hands squeeze R 0" 0.8; w "$REST" 0.5; }
w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3

# ---- the approach (stealth19-drive.sh's) ------------------------------------------------------------------------
hold() { w "crouch hold 9000" 0.3; }
NR=0
reset() { NR=$((NR + 1)); [ "$NR" -gt 1 ] && { w "practice back" 6; w "practice go" 7; }; }
far() { w "practice far" 3
  if grep -E '^PRACTICE t=.*spawn ' "$L" | tail -1 | grep -q 'FAILED\|refused'; then
    echo "$(date +%T) the far spawn failed: back to the start"; w "practice back" 6; w "practice go" 7; w "practice far" 3
  fi; }
noticed() { [ "$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1')" -gt "$N0" ]; }
chase_to() { local n0 i; n0=$(cnt '^STEALTH t=.* chase: stopped'); hold; w "stealth chase $1 $2" 0.3
  for i in $(seq 1 160); do userin && bail "the user is in VR"; [ "$(cnt '^STEALTH t=.* chase: stopped')" -gt "$n0" ] && break
    [ "$(cnt '^STEALTH t=.* stand-off: ')" -gt "$SO0" ] && [ "$2" -lt 200 ] && break
    noticed && { echo "$(date +%T) noticed"; break; }; [ $((i % 12)) = 0 ] && hold; sleep 0.25; done
  sleep 1.5; hold; w "stealth chase 0" 0.3; sleep 2; }
round() { echo "== $1"; reset; N0=$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1'); SO0=$(cnt '^STEALTH t=.* stand-off: '); far; w "stealth status" 0.8; }
endr() { w "stealth status" 0.8; w "crouch stand" 0.4; w "front keep 0" 0.3; w "practice remove" 2; }
# the kill's checks (n0: the line count before the push)
kill_checks() { local n0=$1 tag=$2 k ms tr
  k=$(since "$n0" '^KNIFE t=.* kill: ' | head -1)
  if [ -z "$k" ]; then fail "$tag: no knife kill ($(since "$n0" '^STEALTH t=.* (touch|carried|air|contact)' | tail -2 | cut -c1-220))"; return 1; fi
  ms=$(echo "$k" | grep -oE ', [0-9-]+ ms after' | grep -oE '[0-9-]+'); echo "$k" | cut -c1-400
  local kt st dl; kt=$(echo "$k" | grep -oE 't=[0-9.]+' | head -1 | cut -d= -f2)
  st=$(since "$n0" '^STEALTH kill t=.* dead=0->1' | head -1 | grep -oE 't=[0-9.]+' | head -1 | cut -d= -f2)
  dl=$(awk -v a="${kt:-0}" -v b="${st:-999999}" 'BEGIN { printf "%.0f", (b - a) * 1000 }')
  echo "$k" | grep -q 'dead 0 -> 1' && [ -n "$st" ] && [ "${dl#-}" -le 100 ] \
    && pass "$tag: the kill - dead 0 -> 1 ${dl} ms after the stab passed the rule (Combat_DealHit in that frame); the push ${ms} ms from the point's way in" \
    || fail "$tag: the kill (the death ${dl} ms after the stab): $(echo "$k" | cut -c1-200)"
  sleep 2.5
  tr=$(since "$n0" '^STEALTH t=.* trace .* after the kill' | head -1)
  local rm am ym; rm=$(echo "$tr" | grep -oE 'root moved at most [0-9.]+' | grep -oE '[0-9.]+$'); am=$(echo "$tr" | grep -oE 'anchor [0-9.]+' | grep -oE '[0-9.]+$')
  ym=$(echo "$tr" | grep -oE "view's yaw [0-9.]+" | grep -oE '[0-9.]+$')
  awk -v r="${rm:-99}" -v a="${am:-99}" -v y="${ym:-99}" 'BEGIN { exit !(r <= 1.0 && a <= 1.0 && y <= 0.5) }' \
    && pass "$tag: no player or view motion (root $rm cm, the view's anchor $am cm, yaw $ym deg over 1.5 s)" \
    || fail "$tag: motion: $(echo "$tr" | cut -c1-260)"
  since "$n0" "^STEALTH t=.* blood .*the knife's wound as it went in.*made" | grep -q . && pass "$tag: blood ($(since "$n0" "^STEALTH t=.* blood .*knife's wound" | head -1 | grep -oE '(turned onto|not turned)[^)]*' | head -1))" \
    || fail "$tag: blood: $(since "$n0" '^STEALTH t=.* blood' | head -1 | cut -c1-200)"
}
stick_checks() { local n0=$1 tag=$2
  since "$n0" '^STEALTH t=.* stick hold \(the knife\)' | grep -q . && pass "$tag: the knife sticks ($(since "$n0" '^STEALTH t=.* stick hold \(the knife\)' | tail -1 | grep -oE 'the point [0-9.]+ cm in|pulled [0-9.]+ cm' | tr '\n' ' '))" \
    || fail "$tag: no stick hold"
  since "$n0" '^STEALTH t=.* stick out: .*(the knife)' | grep -qE 'drawn out|fell off' && pass "$tag: out ($(since "$n0" '^STEALTH t=.* stick out' | head -1 | grep -oE '(drawn out|the body fell off)[^(]*after [0-9.]+ s'))" \
    || fail "$tag: the pull-out: $(since "$n0" '^STEALTH t=.* stick out' | head -1 | cut -c1-200)"
}
w "stealth watch 5" 0.5
DUCK=${S40_DUCK_CM:-32}
duck() { x "head 0 -$DUCK 0 s 0.5" 1.0; waitfor '^CROUCH (duck on|game down)' 4 >/dev/null
  echo "$(date +%T) ducked $DUCK cm: $(grep -E '^CROUCH (duck on|duck off|game down|game up)' "$L" | tail -1 | cut -c1-140)"; }
standup() { x "head 0 0 0 s 0.4" 0.8; }
# the test head DN cm down, leaned LN cm toward the bearing B (the tracked head turned B too): stealth19b-drive.sh's
head_at() { local dn=$1 ln=$2 b=${3:-0} hx hz
  hx=$(awk -v l="$ln" -v b="$b" 'BEGIN{printf "%.1f", l*sin(b*3.14159265/180)}')
  hz=$(awk -v l="$ln" -v b="$b" 'BEGIN{printf "%.1f", -l*cos(b*3.14159265/180)}')
  x "head $hx -$dn $hz yaw $b s ${4:-0.6}" 1.0; }
lastwatch() { grep -E '^STEALTH watch t=' "$L" | tail -1; }
wdist() { lastwatch | grep -oE 'orc 0x[0-9a-f]+ [0-9]+ cm' | grep -oE '[0-9]+ cm' | grep -oE '[0-9]+'; }
waware() { lastwatch | grep -oE 'aware -?[0-9]+' | head -1 | grep -oE '[-0-9]+$'; }
tline() { since "$1" "$2" | head -1 | cut -c1-360; }
# the eyes to the nearest orc's torso node, flat (cm; `knife where`: its torso in the head's frame)
tdist() { local n0w l; n0w=$(cnt '^KNIFE where t=.* torso '); w "knife where" 0.3 >/dev/null
  waitfor_n '^KNIFE where t=.* torso ' "$n0w" 4; l=$(grep -E '^KNIFE where t=.* torso ' "$L" | tail -1)
  echo "$l" | grep -oE 'torso [-0-9]+ [-0-9]+ [-0-9]+' | awk '{ printf "%.0f", sqrt($2 * $2 + $4 * $4) }'; }
for s in $STEPS; do
  case $s in
  old)
    round old; w "stealth touch off" 0.4; duck; w "verbs behind 220" 1.0; sleep 3; NO=""
    for d in 160 140 125 118 112 106 100 94 88; do
      n0=$(lines); w "verbs behind $d" 0.5; sleep 3.5
      if noticed; then NO=$d; echo "$(date +%T) noticed at $d (placed)"; break; fi
      echo "$(date +%T) calm at $d (watch: $(wdist) cm, aware $(waware))"
    done
    if [ -n "$NO" ] && [ "$NO" -ge 88 ]; then
      pass "old: the touch off (build 39's senses): ducked behind it, noticed when placed at $NO cm ($(tline "$n0" '^STEALTH t=.* aware orc .*: (0|-2) -> 1' | grep -oE 'at [0-9]+ cm, [-+0-9]+ deg off its facing; Talion [a-z]+'))"
      tl=$(since "$n0" '^TOUCH t=.* passed Talion' | head -3 | grep -oE "its [A-Za-z' -]+ passed Talion's '[^']*'.* root to root[^)]*\)" | cut -c1-200)
      [ -n "$tl" ] && echo "$tl" | while read -r l; do pass "old: the sense that passed: $l"; done || fail "old: no TOUCH pass line"
    else fail "old: not noticed by 88 cm with the touch off (${NO:-never})"; fi
    w "stealth touch on" 0.4; standup; endr ;;
  near)
    round near; w "stealth touch on" 0.4; w "stealth touch status" 0.5; duck; w "verbs behind 220" 1.0; sleep 4
    n0=$(lines); NEAR0=$n0
    w "stealth chase 0.6 40" 0.3
    for i in $(seq 1 120); do userin && bail "the user is in VR"
      since "$n0" '^STEALTH t=.* chase: stopped' | grep -q . && break; noticed && { echo "$(date +%T) noticed"; break; }; sleep 0.25; done
    d=$(wdist); echo "$(date +%T) after the sneak: $d cm ($(since "$n0" '^STEALTH t=.* chase: stopped' | head -1 | cut -c1-120))"
    if ! since "$n0" '^STEALTH t=.* chase: stopped' | grep -q . && ! noticed; then # pressed against it: a slower last push
      w "stealth chase 0.3 40" 0.3; sleep 4; d=$(wdist); echo "$(date +%T) the slow push: $d cm"; fi
    w "stealth chase 0" 0.3; sleep 5
    d=$(wdist)
    noticed && fail "near: noticed on the way in ($(tline "$n0" '^STEALTH t=.* aware orc .*: (0|-2) -> 1' | cut -c1-200))" \
      || pass "near: ducked, sneaked in from 220 cm to ${d} cm behind it and 5 s there: unaware ($(lastwatch | grep -oE 'aware [0-9]+ \(kind [0-9]+, \+0xeb8 [0-9.]+' ))"
    # the game's bodies stop the walk (boot 20261105-081851: 85 cm root to root behind an infantry, the stick still
    # pushing, the body 0 cm of 150 asked): Talion is as near as the game lets a body come; the player leans in
    awk -v d="${d:-999}" 'BEGIN { exit !(d <= 95) }' && pass "near: his root at ${d} cm, pressed against its back (the game's bodies stop the walk there: $(grep -E '^LOCO t=.* last 1 s' "$L" | tail -1 | grep -oE 'asked [0-9.]+ cm, the body [0-9.]+ cm'))" \
      || fail "near: stopped at ${d:-?} cm"
    aim; HD0=$(tdist)
    LEAN=${S40_LEAN_CM:-15}; head_at "$DUCK" "$LEAN" "$BRG"; sleep 2.5
    HD=$(tdist); d=$(wdist)
    noticed && fail "near: noticed at the lean" \
      || pass "near: leaned in $LEAN cm: the eyes ${HD} cm from its torso node, flat (${HD0} before the lean), the root ${d} cm - unaware"
    c=$(since "$n0" '^TOUCH t=.* CUT' | wc -l); cl=$(since "$n0" '^TOUCH t=.* CUT' | grep -oE "its [A-Za-z' -]+ passed" | sort | uniq -c | tr -s ' \n' ' ')
    [ "$c" -gt 0 ] && pass "near: the senses cut on the way in ($cl; nearest cut $(since "$n0" '^TOUCH t=.* CUT' | grep -oE 'the last at [0-9]+ cm' | grep -oE '[0-9]+' | sort -n | head -1) cm)" \
      || fail "near: no TOUCH cut line"
    since "$n0" '^TOUCH t=.* passed Talion.*: the game' | grep -q . && echo "$(date +%T) note: game's passes: $(since "$n0" '^TOUCH t=.* passed Talion.*: the game' | head -2 | cut -c1-260)"
    dp near-40; NEARD=$d ;;
  grab)
    case " $STEPS " in *" near "*) ;; *) fail "grab: needs near first"; continue ;; esac
    noticed && { fail "grab: the orc noticed before the knife"; endr; continue; }
    aim; poses; knife_draw || fail "grab: the knife not drawn"
    noticed && fail "grab: noticed at the knife's draw ($(tline "$NEAR0" '^STEALTH t=.* aware orc .*: (0|-2) -> 1' | cut -c1-200))"
    kcal; n0=$(lines)
    for g in "$LGRAB" "$LGRAB2" "$LGRAB3"; do w "$g" 0.8; w "hands squeeze L 1" 1.2
      since "$n0" '^KNIFE t=.* hold: ' | grep -q . && break; w "hands squeeze L 0" 0.5; done
    since "$n0" '^KNIFE t=.* hold: ' | grep -q . && pass "grab: the off hand on its shoulder: the hold ($(since "$n0" '^KNIFE t=.* hold: ' | head -1 | cut -c14-170))" \
      || fail "grab: no hold: $(since "$n0" '^KNIFE' | head -3 | cut -c1-220)"
    dp grab-held
    # the push: deep and steady (boot 081851: a 24 cm push in 0.6 s put the point 16 cm in between two 20 Hz frames -
    # the judge saw no push of the hand's own in the body: "carried"); then lower on the back; then knife37's
    n0=$(lines)
    for P in 30:0:1000 30:-12:1000 14:0:600; do D=${P%%:*}; U=${P#*:}; U=${U%%:*}; MS=${P##*:}
      poses >/dev/null; w "$(pk test R -10 0 $U 0)" 0.8; nk=$(lines); w "$(pk swing R $D 0 $U 0 $MS)" "$(awk -v m="$MS" 'BEGIN { print m / 1000 + 0.3 }')"
      dp "grab-stab-$D$U"; since "$nk" '^KNIFE t=.* kill: ' | grep -q . && break
      echo "$(date +%T) the push to $D cm (up $U) killed nothing: $(since "$nk" '^STEALTH t=.* (carried|touch|air) \(the dagger\)' | tail -1 | cut -c1-200)"
    done
    sleep 0.3; dp grab-in
    w "$KPULL" 1.2; kill_checks "$n0" grab
    noticed && since "$NEAR0" '^STEALTH t=.* aware orc .*: (0|-2) -> 1' | head -1 | grep -q 'dead' && true
    a1=$(since "$NEAR0" '^STEALTH t=.* aware orc .*: (0|-2) -> 1' | head -1)
    k1=$(since "$n0" '^KNIFE t=.* kill: ' | head -1 | grep -oE 't=[0-9.]+' | cut -d= -f2)
    [ -z "$a1" ] && pass "grab: the orc never noticed him (no aware line from the sneak to the kill)" \
      || echo "$(date +%T) note: an aware line: $(echo "$a1" | cut -c1-200) (the kill at t=$k1)"
    w "hands squeeze L 0" 0.5; w "$LREST" 0.5; knife_away; standup; endr ;;
  front)
    echo "== front"; reset; w "stealth touch on" 0.4; far; duck; n0=$(lines); N0=$(cnt '^STEALTH t=.* aware orc .*: (0|-2) -> 1')
    w "verbs front 1000" 1.0; sleep 3; FD=""
    w "stealth chase 0.6 60" 0.3; last=""; still=0
    for i in $(seq 1 160); do userin && bail "the user is in VR"
      a=$(waware); if [ -n "$a" ] && [ "$a" != 0 ]; then FD=$(wdist); break; fi
      since "$n0" '^STEALTH t=.* chase: stopped' | grep -q . && break
      dd=$(wdist); [ "$dd" = "$last" ] && still=$((still + 1)) || still=0; last=$dd
      [ "$still" -ge 12 ] && { echo "$(date +%T) the walk stuck at $dd cm (the ground): placed 400 cm in front instead"; break; }; sleep 0.25; done
    w "stealth chase 0" 0.3; sleep 1
    if [ -z "$FD" ]; then # boot 082650: a rock stopped the walk 566 cm off, out of its sight - in front of it, still
      w "verbs front 400" 1.0
      for i in $(seq 1 48); do a=$(waware); if [ -n "$a" ] && [ "$a" != 0 ]; then FD=$(wdist); break; fi; sleep 0.25; done
    fi
    fl=$(since "$n0" '^STEALTH watch t=' | grep -E 'aware [1-9]' | head -1 | grep -oE 'orc 0x[0-9a-f]+ [0-9]+ cm [-+0-9]+ deg: aware [0-9]+')
    if [ -n "$FD" ] && [ "$FD" -ge 150 ]; then pass "front: an orc facing him noticed the ducked Talion at $FD cm ($fl): its sight, the game's range"
    else fail "front: noticed at ${FD:-never} cm ($fl)"; fi
    since "$n0" '^TOUCH t=.* CUT' | grep -q . && fail "front: a cut for the orc in front: $(since "$n0" '^TOUCH t=.* CUT' | head -1 | cut -c1-200)" \
      || pass "front: no sense of it cut ($(since "$n0" '^TOUCH t=.* passed Talion' | wc -l) pass line(s), all the game's)"
    standup; endr ;;
  esac
done
w "stealth watch off" 0.3; w "stealth touch status" 0.5; w "knife status" 0.8; w "stealth status" 1; w "endlogs" 2; sleep 2
grep -E '^(TOUCH END|KNIFE END|STEALTH END)' "$L" | cut -c1-500
echo "WARN lines: $(cnt '^WARN')"
echo quit >> "$R/cmd"
echo "quit $(date +%T)"; cat "$S"
