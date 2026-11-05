#!/bin/bash
# knife37-drive.sh - build 37 (som-b37-knife; docs/re-xr-pipeline.md 168): SOMVR_KNIFE_KILL in one headless boot
# (scripts/headless_vr.sh --win with scripts/headset-build26.env + SOMVR_KNIFE_KILL=on, SOMVR_HANDS_TEST=1,
# SOMVR_XR_TEST=@RUN@/xrtest.txt). The hands are held as real hands hold (OpenXR grip: -Z pinky -> thumb = the knife's
# blade out of the fist, the thumb up; `hands test` 0 0 0 = that fist level, the blade ahead).
# K37_STEPS (default all, in order):
#   grab   the far unaware orc (its back to Talion), crouched; the chase to 10 m, a second orc (the witness) spawned 18 m
#          ahead - 10 m past it, its back to both; the sneak to the stand-off; the knife from the RIGHT hip; the LEFT
#          hand on its shoulder, the squeeze (the hold); a slow brush across its back with the point (no kill), a slow
#          brush along it with the blade flat (no kill); then the slow deliberate push in (0.3-0.4 m/s): the kill; the
#          knife held in, pulled out; the checks (kill within 0.1 s, no motion, blood, the stick, silence)
#   nograb the same kill without the hold (the knife alone)
#   sword  the sword's thrust into an unaware orc's back: the stealth kill as build 36 (no KNIFE kill line)
#   fight  a fighting orc (spawned ahead, standing): knife stabs - light hits, not a kill
# K37_RUN=RUN drives a running boot; else K37_OUT=FILE (headless_vr.sh's output: its "windows: RUN" line) or the newest
# run of this worktree. Lines: RUN/k37-steps.txt (PASS / FAIL, the dumps' frames for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${K37_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${K37_STEPS:-grab nograb sword fight}
T0=$(date +%s)
R=${K37_RUN:-}
for i in $(seq 1 900); do
  [ -n "$R" ] && break
  if [ -n "${K37_OUT:-}" ]; then
    R=$(grep -o 'windows: [^ ]*' "$K37_OUT" 2>/dev/null | head -1 | cut -d' ' -f2)
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
L=$R/somvr.log; X=$R/xr-layer.log; S=$R/k37-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
# build 40 (som-b40-fixes, re-xr-pipeline.md 175.3): a hands word taken for sure - the Windows build reads the trigger file
# every 1-2 s and a word written sooner after the last is lost: written, then its TRIGGER line waited for (3 s at most)
wa() { local a0 i; a0=$(grep -c '^TRIGGER cmd=hands' "$L"); w "$1" 0.02; for i in $(seq 1 30); do
  [ "$(grep -c '^TRIGGER cmd=hands' "$L")" -gt "$a0" ] && break; sleep 0.1; done; sleep "${2:-0.2}"; }
# build 40: the push measured and started in one frame (the mod's `knife push D MS [sword]`, a test aid: the hand by the
# point's way onto his back + D cm on; a measure and a word built from it came 2-3 s apart and the orc swayed 10-30 cm)
wk() { local a0 i; a0=$(grep -c '^TRIGGER cmd=knife push' "$L"); w "$1" 0.02; for i in $(seq 1 30); do
  [ "$(grep -c '^TRIGGER cmd=knife push' "$L")" -gt "$a0" ] && break; sleep 0.1; done; sleep "${2:-0.2}"
  grep -E '^KNIFE push t=' "$L" | tail -1 | cut -c1-220; }
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
  BACK=$(echo "$l" | grep -oE 'back [-0-9]+ [-0-9]+ [-0-9]+' | cut -d' ' -f2-) # (build 40: the judge's own back point - the
  # torso-neck middle 16 cm toward its back along its facing; boot 084104's bent orc put the eye line's S 15 cm inside him)
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
  # build 40: the thrust along the line from the right shoulder (the `hands swing` pivot, 18 -25 -8 of the head) through
  # S at its height: `hands swing` arcs about that shoulder, so a thrust whose two ends lie on one ray from it is straight
  # and the blade moves along itself (suite 372: the thrust from behind the head swept 108 deg round the shoulder - "in the
  # body but no thrust", cos 0.65). The grip at G cm along that ray (never under 3: both ends ahead of the shoulder, the
  # ray's own side), the point SWL cm further along the blade; READY2 the point 15 cm short of S, THRUST 30 cm in, 0.4 s
  # (1.6 m/s as the point goes in, 2.1 at the middle); SWL_GAP = how far the ready's point is from S (cm; under 5: too
  # close for the sword - the step backs off first)
  sw() { awk -v W="$WHERE" -v BK="$BACK" -v D="$1" -v k="$2" -v ms="${3:-}" -v SWL="${SWL:-108}" "BEGIN { $geo
    if (split(BK, bk, \" \") == 3) { sx = bk[1]; my = bk[2]; sz = bk[3] }
    ox = 18; oy = -25; oz = -8; tx = sx - ox; ty = my - oy; tz = sz - oz; l = sqrt(tx * tx + ty * ty + tz * tz)
    ux = tx / l; uy = ty / l; uz = tz / l; g = l + D - SWL; if (g < 3) g = 3
    printf \"hands %s R %.1f %.1f %.1f %.1f %.1f 0%s\", k, ox + g * ux, oy + g * uy, oz + g * uz, atan2(uy, uz) * 180 / P,
      -atan2(ux, sqrt(1 - ux * ux)) * 180 / P, ms != \"\" ? \" \" ms : \"\" }"; } # (q = Rx Ry: the blade (sin b', cos b' sin a, cos b' cos a))
  SWL_GAP=$(awk -v W="$WHERE" -v BK="$BACK" -v SWL="${SWL:-108}" "BEGIN { $geo
    if (split(BK, bk, \" \") == 3) { sx = bk[1]; my = bk[2]; sz = bk[3] }
    tx = sx - 18; ty = my + 25; tz = sz + 8; printf \"%.0f\", sqrt(tx * tx + ty * ty + tz * tz) - SWL - 3 }")
  READY2=$(sw -15 test); THRUST=$(sw 30 swing 400)
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

for s in $STEPS; do
  case $s in
  grab)
    round grab; hold; chase_to 0.6 500
    w "front keep 1" 0.3; far; WIT=$(grep -E '^PRACTICE t=.*spawn ' "$L" | tail -1 | cut -c1-200); echo "the witness: $WIT"
    chase_to 0.6 60; noticed && echo "$(date +%T) noticed before the knife"
    aim; poses; knife_draw || fail "grab: the knife not drawn"
    # (build 40: the crouch hold is 9 s - boot 083134's draw and calibration outlasted it, Talion stood and the orc
    # turned on him before the grab: renewed before each step that needs him unaware)
    hold; kcal; hold; n0=$(lines)
    for g in "$LGRAB" "$LGRAB2" "$LGRAB3"; do w "$g" 0.8; w "hands squeeze L 1" 1.2
      since "$n0" '^KNIFE t=.* hold: ' | grep -q . && break; w "hands squeeze L 0" 0.5; done
    since "$n0" '^KNIFE t=.* hold: ' | grep -q . && pass "grab: the hold ($(since "$n0" '^KNIFE t=.* hold: ' | head -1 | cut -c14-160))" \
      || fail "grab: no hold: $(since "$n0" '^KNIFE' | head -3 | cut -c1-220)"
    dp grab-held
    nb=$(lines); w "$KBRUSH0" 0.4; w "$KBRUSH1" 1.4
    bi=$(since "$nb" '^STEALTH t=.* (touch|carried) \(the dagger\)' | wc -l)
    if since "$nb" '^KNIFE t=.* kill: ' | grep -q .; then fail "brush: a brush killed: $(since "$nb" '^KNIFE t=.* kill: ' | head -1 | cut -c1-200)"
    elif [ "$bi" -lt 1 ]; then fail "brush: the point never in its body ($(since "$nb" '^STEALTH t=.* air \(the dagger\)' | tail -1 | grep -oE 'the blade [0-9.]+ cm off'))"
    else pass "brush: the point dragged slowly across his back, no kill ($(since "$nb" '^STEALTH t=.* (touch|carried|air) \(the dagger\)' | grep -oE ' (touch|carried|air) ' | sort | uniq -c | tr -s ' \n' ' '); $(since "$nb" '^STEALTH t=.* touch \(the dagger\)' | grep -oE 'along [0-9.-]+ m/s cos [0-9.-]+' | head -2 | tr '\n' ';'))"; fi
    since "$nb" '^STEALTH t=.* alert: ' | grep -q . && fail "brush: the held orc alerted" || pass "brush: held, no alert"
    # build 40: the push from a fresh measure of the held orc (boot 084104: the poses measured before the hold and the
    # brushes, the push 3.4 cm short of his body: no kill), the same tries as nograb's
    for k in 1 2 3; do
      hold; poses; wa "$KREADY" 0.6
      n0=$(lines); k0=$(cnt '^KNIFE t=.* kill: '); wk "knife push 14 600" 0.1; dp grab-stab; sleep 0.2; dp grab-in
      waitfor_n '^KNIFE t=.* kill: ' "$k0" 2 && break
      if since "$n0" '^STEALTH t=.* (touch|contact) \(the dagger\)' | grep -q .; then echo "$(date +%T) grab try $k: a touch, no kill"; break; fi
      echo "$(date +%T) grab try $k: the push met the air ($(since "$n0" '^STEALTH t=.* air \(the dagger\)' | tail -1 | grep -oE 'the blade [0-9.]+ cm off')): again"
      wa "$KPULL" 1.0
    done
    wa "$KPULL" 1.2; dp grab-after; kill_checks "$n0" grab; stick_checks "$n0" grab
    w "hands squeeze L 0" 0.5; w "$LREST" 0.5
    sleep 1.5
    a3=$(since "$n0" '^STEALTH t=.* after the kill \+3 s' | head -1); echo "$a3" | cut -c1-300
    echo "$a3" | grep -qE 'other live orcs within 40 m, 0 aware' && echo "$a3" | grep -qE ' [1-9][0-9]* other live' \
      && pass "silence: the witness unaware 3 s after ($(echo "$a3" | grep -oE '[0-9]+ other live orcs within 40 m, [0-9]+ aware'))" \
      || fail "silence: $(echo "$a3" | grep -oE '[0-9]+ other live orcs within 40 m, [0-9]+ aware' || echo 'no +3 s line')"
    since "$n0" '^KNIFE t=.* kill: .*the seen stimuli registered' | grep -q . && pass "silence: the silent record's stimuli registered" \
      || echo "$(date +%T) note: the seen stimuli: $(since "$n0" '^KNIFE t=.* kill: ' | grep -oE 'the seen stimuli [^;]*' | head -1)"
    knife_away; endr ;;
  nograb)
    round nograb; chase_to 0.6 60; noticed && echo "$(date +%T) noticed before the knife"
    aim; poses; knife_draw || fail "nograb: the knife not drawn"
    hold; kcal
    # build 40: the orc (not held) sways and turns as it stands - suite 372's push came 1.5 s after the measure, 23 cm off
    # its body. Each try: a fresh measure, the ready, the point checked outside him, the push at once; a push that met
    # only the air (no touch: he never felt it) is tried again from a fresh measure, up to 3 times
    for k in 1 2 3; do
      hold; poses; nb=$(lines); wa "$KREADY" 0.6
      if since "$nb" '^STEALTH t=.* (touch|carried|contact) \(the dagger\)' | tail -1 | grep -q .; then
        echo "$(date +%T) nograb try $k: the point in his body at the ready: back 15 cm"; wa "$(pk test R -25 0 0 0)" 0.5; continue; fi
      # (the unheld orc: the fresh poses' push - 6/6 in boots 083134, 084104, 085331; the push aid met the air beside a
      # turned orc in 090833 and its turn in 093501)
      n0=$(lines); k0=$(cnt '^KNIFE t=.* kill: '); wa "$KPUSH" 0.1; dp nograb-stab
      waitfor_n '^KNIFE t=.* kill: ' "$k0" 2 && break
      if since "$n0" '^STEALTH t=.* (touch|contact) \(the dagger\)' | grep -q .; then echo "$(date +%T) nograb try $k: a touch, no kill"; break; fi
      echo "$(date +%T) nograb try $k: the push met the air ($(since "$n0" '^STEALTH t=.* air \(the dagger\)' | tail -1 | grep -oE 'the blade [0-9.]+ cm off')): again"
      wa "$KPULL" 1.0
    done
    wa "$KPULL" 1.2; kill_checks "$n0" nograb
    since "$n0" '^KNIFE t=.* kill: .*not held' | grep -q . && pass "nograb: killed without the hold" || fail "nograb: held?"
    stick_checks "$n0" nograb
    knife_away; endr ;;
  sword)
    round sword; sword_draw; chase_to 0.6 60; aim
    # build 40: the blade outside his body before the thrust (a blade already in him is a touch, never a thrust): a fresh
    # measure, the ready on the shoulder's ray, the judge's lines since it read - in him: 15 cm further back, again
    for k in 1 2 3; do
      hold; poses; echo "$(date +%T) sword try $k: the closest ready's point ${SWL_GAP:-?} cm short of his back: $READY2"
      # (under 8 cm: too close for a 1 m blade - half a step back, crouched, as a player would make room)
      if [ "${SWL_GAP:-99}" -lt 8 ]; then echo "$(date +%T) sword try $k: too close for the sword: half a step back"; w "stick 0 -0.6 0 0.4" 1.2; continue; fi
      nb=$(lines); wa "$READY2" 0.5; sleep 0.5
      if since "$nb" '^STEALTH (kill )?t=.* kill by ' | grep -q .; then fail "sword: the kill came at the ready's placement, not the thrust"; break; fi
      if since "$nb" '^STEALTH t=.* (touch|carried|contact) \(the sword\)' | grep -q .; then
        echo "$(date +%T) sword try $k: the blade in his body at the ready: $(since "$nb" '^STEALTH t=.* (touch|carried|contact) \(the sword\)' | tail -1 | cut -c1-160)"
        READY2=$(sw -30 test); THRUST=$(sw 30 swing 450); wa "$READY2" 0.6; continue; fi
      break; done
    echo "$(date +%T) sword: at the ready $(since "$nb" '^STEALTH t=.* air \(the sword\)' | tail -1 | grep -oE 'the blade [0-9.]+ cm off its body' || echo '(no air line: out of the judge's reach)')"
    n0=$(lines); wk "knife push 30 400 sword" 0.1; dp sword-kill; sleep 2
    since "$n0" '^STEALTH kill t=.* by the sword' | grep -q . && pass "sword: the stealth kill by the sword as build 36 ($(since "$n0" '^STEALTH t=.* kill by ' | head -1 | grep -oE 'kill by [a-z ]+:' ))" \
      || fail "sword: $(since "$n0" '^STEALTH (kill )?t=.* (contact|touch|kill)' | head -2 | cut -c1-220)"
    since "$n0" '^KNIFE t=.* kill: ' | grep -q . && fail "sword: a knife kill line" || pass "sword: no knife line"
    wa "$REST" 0.5; sword_away; endr ;;
  fight)
    echo "== fight"; reset; w "crouch stand" 0.4; w "verbs ahead 400" 0.3; w "practice spawn infantry" 4; w "verbs ahead 300" 0.3
    knife_draw || fail "fight: the knife not drawn"
    # build 40: the fighting orc dodged, fell and circled through the stabs (suite 372: his torso 116 cm under the eyes,
    # then 95, 67 ...; each pose a second old) - `kills still on` (a test aid, build 31b: in his fight, his own attacks and
    # reactions refused, his walk held) keeps him where the hand can reach him; the knife's judge unchanged
    w "kills still on" 0.8; aim; poses; kcal
    n0=$(lines)
    for j in 1 2 3 4 5 6; do
      d=$(w "execute where" 0.5 >/dev/null; grep -E '^EXECUTE where ' "$L" | tail -1 | grep -oE 'dist [0-9]+' | head -1 | cut -d' ' -f2)
      echo "$(date +%T) the orc at ${d:-?} cm"; aim; poses
      wa "$KSTAB0" 0.4; wa "$KSTAB1" 0.5
      [ "$(since "$n0" '^KNIFE t=.* light: ' | wc -l)" -ge 2 ] && break
    done
    dp fight-stab
    lt=$(since "$n0" '^KNIFE t=.* light: '); echo "$lt" | head -3 | cut -c1-300
    [ -n "$lt" ] && ! since "$n0" '^KNIFE t=.* kill: ' | grep -q . && echo "$lt" | grep -q 'dead 0 -> 0' \
      && pass "fight: the knife on a fighting orc - $(echo "$lt" | wc -l) light hit(s), no kill ($(echo "$lt" | head -1 | grep -oE 'health [0-9.]+ -> [0-9.]+'))" \
      || fail "fight: $(since "$n0" '^KNIFE' | head -3 | cut -c1-220)"
    w "kills still off" 0.5; knife_away; w "practice remove" 2 ;;
  esac
done
w "stealth watch off" 0.3; w "knife status" 0.8; w "stealth status" 1; w "endlogs" 2; sleep 2
grep -E '^(KNIFE END|STEALTH END)' "$L" | cut -c1-400
echo "WARN lines: $(cnt '^WARN')"
echo quit >> "$R/cmd"
echo "quit $(date +%T)"; cat "$S"
