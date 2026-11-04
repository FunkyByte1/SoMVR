#!/bin/bash
# polish36-drive.sh - build 36's polish round (som-b36-polish; docs/re-xr-pipeline.md 167): drives one headless boot
# (scripts/headless_vr.sh --win with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt).
# P36_STEPS (default all, in order):
#   bowjump  the bow out (the left grip at the left shoulder); the right hand at the string with the trigger and A
#            pressed together (the 11-03 Windows session's marks 12-13): no jump (`JUMP ... press while drawing the bow`);
#            A while drawn: nothing; let down, the hand away from the string: A jumps as ever
#   knife    the sword put away; the right hand at the RIGHT hip, the squeeze: Acharn drawn into the right hand
#            (R_Weapon), held up (dumps), let go: back on the belt; the left hip as ever; the sword in the right hand: its
#            hip draws nothing
#   wraith   both hands up in front (dumps): plain, Y held (the wraith vision: wraith hands), `hands wraith 0.5` (the
#            fade's middle), Y let go: plain again
#   spawn    the sword sheathed, `slsnd log`, an orc spawned ahead: the game draws Talion's sword (kept on the back) and
#            its own draw sound shows in SLSND lines; again with `slsnd drop`: DROPPED (build 35's Windows DLL never
#            saw a game object's post)
#   listen   the game's audio listener (AUDIO lines) against the head: straight, turned 90 deg, moved 40 cm right
# P36_RUN=RUN drives a running boot; else P36_OUT=FILE (headless_vr.sh's output: its "windows: RUN" line) or the newest
# run of this worktree. Lines: RUN/p36-steps.txt (PASS / FAIL, the dumps' frames for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${P36_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${P36_STEPS:-bowjump knife wraith spawn listen}
T0=$(date +%s)
R=${P36_RUN:-}
for i in $(seq 1 900); do
  [ -n "$R" ] && break
  if [ -n "${P36_OUT:-}" ]; then
    R=$(grep -o 'windows: [^ ]*' "$P36_OUT" 2>/dev/null | head -1 | cut -d' ' -f2)
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
L=$R/somvr.log; X=$R/xr-layer.log
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
last() { grep -E "$1" "${3:-$L}" | tail -${2:-1} | cut -c1-330; }
waitfor() { local n0 i; n0=$(cnt "$1" "${3:-$L}"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1" "${3:-$L}")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/p36-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/p36-steps.txt"; }
dp() { x "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
       echo "dump $1 $f" | tee -a "$R/p36-steps.txt"; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
for i in $(seq 1 300); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$X" || { x "btn R a 1"; x "btn R a 0"; }
grep -E '^(WARN|CONFIG (bow a_jump|slots: SOMVR_SLOTS_KNIFE|vrhands|slots gamesound|practice))' "$L" | cut -c1-260

# ---- the bow and A (the layer's test hands: real controller poses, as bow33-drive.sh) ------------------------------
BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
bow_out() { x "$BOW_SLOT" "$R_REST" "btn L squeeze 0" "btn L a 0" "btn L b 0" "btn R trigger 0" "btn R a 0"; sleep 0.3
  x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8; x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1; }
bow_away() { x "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; x "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 1; }
if has bowjump; then
  echo "== bowjump: the trigger and A together at the string"
  w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.5
  bow_out
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter && pass "the bow out" || fail "the bow not out: $(last '^BOW t=' 2)"
  n0=$(lines); g0=$(cnt '^JUMP t=.* go:')
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.5
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1" "btn R a 1"; sleep 0.5
  x "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1" "btn R a 0"; sleep 1.4
  dp bowjump-drawn
  l=$(since "$n0" 'press while drawing the bow: nothing'); g1=$(cnt '^JUMP t=.* go:')
  [ -n "$l" ] && [ "$g1" = "$g0" ] && pass "trigger + A at the string: no jump ($(echo "$l" | head -1 | cut -c1-200))" \
    || fail "trigger + A at the string: jumps $g0 -> $g1; $(since "$n0" '^JUMP t=' | head -2 | cut -c1-200)"
  since "$n0" '^BOW t=.* (hover|nock)' | head -2 | cut -c1-200
  n0=$(lines)
  x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "btn R a 1"; sleep 0.4
  x "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 1" "btn R a 0"; sleep 1
  l=$(since "$n0" 'press while drawing the bow: nothing'); g2=$(cnt '^JUMP t=.* go:')
  [ -n "$l" ] && [ "$g2" = "$g1" ] && pass "A while drawn: nothing ($(echo "$l" | head -1 | cut -c1-160))" || fail "A while drawn: $(since "$n0" '^JUMP t=' | head -2 | cut -c1-200)"
  # let down at the string, the hand away, the trigger up: A jumps as ever with the bow out
  x "$BOW_STRAIGHT" "$AT_STRING s 0.5" "btn L squeeze 1" "btn R trigger 1"; sleep 0.8
  x "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 0"; sleep 0.8
  x "$BOW_STRAIGHT" "$R_REST s 0.4" "btn L squeeze 1" "btn R trigger 0"; sleep 1.2
  n0=$(lines)
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn R a 1"; sleep 0.3
  x "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1" "btn R a 0"; sleep 2
  l=$(since "$n0" '^JUMP t=.* (go:|press:)')
  echo "$l" | grep -q 'go:' && pass "the hand away from the string: A jumps ($(echo "$l" | head -1 | cut -c1-160))" \
    || fail "the hand away: $(since "$n0" '^JUMP t=' | head -2 | cut -c1-200)"
  bow_away
  grep -E '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q exit || bow_away
fi

# ---- the knife (the preload's test hands, as hands31-drive.sh) ----------------------------------------------------
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
RHIP="hands test R 18 -62 -5 0 0 0"; LHIP="hands test L -18 -62 -5 0 0 0"
RUP="hands test R 20 -25 40 -60 0 0"; LUP="hands test L -20 -25 40 -60 0 0"
sword_state() { grep -E '^SLOTS t=.* sword: .*now (in the hand|sheathed|on the back)' "$L" | tail -1 | grep -oE 'now (in the hand|sheathed|on the back)'; }
sword_away() { [ "$(sword_state)" = "now in the hand" ] || return 0
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.6; w "$REST" 0.8; }
sword_draw() { [ "$(sword_state)" = "now in the hand" ] && return 0
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 0.6; }
if has knife; then
  echo "== knife: the left hip (as ever; its fit measured: SOMVR_SLOTS_KNIFE_FIT)"
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2
  w "$LREST" 0.6; w "$REST" 0.6; x "btn R squeeze 0" "btn L squeeze 0" 0.4
  sword_away; echo "the sword: $(sword_state)"
  n0=$(lines); w "$LHIP" 1.0; w "hands squeeze L 1" 1.2
  since "$n0" '^SLOTS t=.* dagger: drawn' | grep -q 'L_Weapon' && pass "the left hip: the left hand" \
    || fail "the left hip: $(since "$n0" '^SLOTS t=' | head -3 | cut -c1-200)"
  w "$LUP" 1.5; dp knife-left-held; since "$n0" '^SLOTS knife fit' | head -1 | cut -c1-260
  w "hands squeeze L 0" 1.2; w "$LREST" 0.6
  for pass_ in 1 2; do
    echo "== knife: the right hip ($pass_)"
    n0=$(lines)
    w "$RHIP" 1.0; w "hands squeeze R 1" 1.2
    l=$(since "$n0" '^SLOTS t=.* dagger: (drawn|draw asked)')
    echo "$l" | grep -q 'R_Weapon' && pass "the right hip: drawn into the right hand ($(echo "$l" | grep R_Weapon | head -1 | cut -c1-200))" \
      || fail "the right hip: $(since "$n0" '^SLOTS t=' | head -3 | cut -c1-220)"
    since "$n0" '^SLOTS dagger entries' | head -1 | cut -c1-200
    w "$RUP" 1.5; dp knife-right-held-$pass_; sleep 0.8; dp knife-right-held2-$pass_
    since "$n0" '^SLOTS knife fit' | head -1 | cut -c1-260
    n0=$(lines); w "hands squeeze R 0" 1.2
    since "$n0" '^SLOTS t=.* dagger: (put-back asked \(hold|put back)' | head -2 | grep -q . && pass "let go: back on the belt" \
      || fail "let go: $(since "$n0" '^SLOTS t=' | head -2 | cut -c1-200)"
    w "$REST" 0.6
  done
  echo "== knife: the sword in the right hand - its hip is no knife"
  sword_draw; echo "the sword: $(sword_state)"
  n0=$(lines); w "$RHIP" 1.0; w "hands squeeze R 1" 1.0; w "hands squeeze R 0" 0.6
  [ -z "$(since "$n0" '^SLOTS t=.* dagger')" ] && pass "the sword's hand at its hip: no knife" || fail "with the sword: $(since "$n0" '^SLOTS t=.* dagger' | head -2 | cut -c1-200)"
  w "$REST" 0.6; sword_away; echo "the sword: $(sword_state)"
fi

# ---- the wraith hands ---------------------------------------------------------------------------------------------
if has wraith; then
  echo "== wraith: the hands in the wraith vision"
  w "hands test L -14 -22 32 -40 0 0" 0.4; w "hands test R 14 -22 32 -40 0 0" 0.4
  w "hands squeeze L 0.3" 0.2; w "hands squeeze R 0.3" 0.6
  dp wraith-plain
  n0=$(lines); x "btn L b 1"; waitfor 'VRHANDS t=.* the wraith vision on' 5 && sleep 1.2
  l=$(since "$n0" 'VRHANDS t=.* the wraith vision on'); lx=$(grep 'wraith hands full' "$X" | tail -1)
  [ -n "$l" ] && [ -n "$lx" ] && pass "Y held: the vision on, wraith hands ($(echo "$lx" | cut -c1-140))" \
    || fail "Y held: $(since "$n0" '^VISION|VRHANDS' | head -3 | cut -c1-200); layer: $(grep 'wraith hands' "$X" | tail -1)"
  dp wraith-full; sleep 0.6; dp wraith-full2
  w "hands wraith 0.5" 0.8; dp wraith-half; w "hands wraith auto" 0.4
  n0=$(lines); x "btn L b 0"; waitfor 'VRHANDS t=.* the wraith vision off' 5 && sleep 1.2
  grep 'wraith hands gone' "$X" | tail -1 | grep -q . && pass "Y let go: plain hands again" || fail "Y let go: $(since "$n0" '^VISION|VRHANDS' | head -3 | cut -c1-200)"
  dp wraith-after
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "$LREST" 0.4; w "$REST" 0.4
fi

# ---- the game's draw sound of Talion's sword at an orc's spawn ----------------------------------------------------
if has spawn; then
  echo "== spawn: the game's draw and its sound"
  sword_away; echo "the sword: $(sword_state)"
  w "slsnd state" 0.6; last '^SLSND state' 1
  for mode in log drop; do
    w "slsnd $mode" 0.6
    n0=$(lines); w "practice spawn infantry" 5
    since "$n0" '^PRACTICE t=.*spawn' | head -1 | cut -c1-200
    since "$n0" '^SLOTS t=.* sword: (the game drew it|.*changed by the game)' | head -2 | cut -c1-200
    s=$(since "$n0" '^SLSND t='); echo "$s" | head -6 | cut -c1-220
    if [ "$mode" = log ]; then echo "$s" | grep -q "fol_ply_cbt_swd_unsheath' .*passed" && pass "log: the game's own draw sound posted (named)" || fail "log: no unsheath post seen"
    else echo "$s" | grep -q "fol_ply_cbt_swd_unsheath' .*DROPPED" && pass "drop: Talion's draw sound DROPPED" || fail "drop: $(echo "$s" | head -2 | cut -c1-200)"; fi
    w "practice remove" 2; sleep 2
    sword_away
  done
  w "slsnd state" 0.6; last '^SLSND state' 1
fi

# ---- the audio listener against the head ---------------------------------------------------------------------------
au() { local n0; n0=$(cnt '^AUDIO n='); for i in $(seq 1 16); do [ "$(cnt '^AUDIO n=')" -gt "$n0" ] && break; sleep 0.25; done
       last '^AUDIO n=' 1; last '^FRONT camcopy' 1; }
if has listen; then
  echo "== listen: the listener follows the head"
  x "head 0 0 0 s 0.3"; sleep 1.5; A0=$(au)
  x "head 0 0 0 yaw 90 s 0.5"; sleep 1.5; A1=$(au)
  x "head 40 0 0 yaw 90 s 0.5"; sleep 1.5; A2=$(au)
  x "head 0 0 0 s 0.5"; sleep 1
  printf '%s\n%s\n%s\n' "$A0" "$A1" "$A2" | cut -c1-260 | tee -a "$R/p36-steps.txt"
  P36_A="$A0
$A1
$A2" python3 - <<'PYEOF' | tee -a "$R/p36-steps.txt"
import os, re, math
L = os.environ["P36_A"].splitlines()
au = [l for l in L if l.startswith("AUDIO")]; cc = [l for l in L if l.startswith("FRONT camcopy")]
def v(s, k): m = re.search(k + r"=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)", s); return [float(x) for x in m.groups()] if m else None
def hv(s): m = re.search(r"the head ([-+]?[\d.]+),([-+]?[\d.]+),([-+]?[\d.]+) fwd ([-+]?[\d.]+),([-+]?[\d.]+),([-+]?[\d.]+)", s); return [float(x) for x in m.groups()] if m else None
if len(au) < 3 or len(cc) < 3:
    print("FAIL listen: lines missing (%d AUDIO, %d camcopy)" % (len(au), len(cc))); raise SystemExit
f = [v(a, "front") for a in au]; p = [v(a, "head pos") for a in au]; h = [hv(c) for c in cc]
yaw = lambda q: math.degrees(math.atan2(q[0], q[2]))
d = lambda a, b: (a - b + 180) % 360 - 180
turn = d(yaw(f[1]), yaw(f[0])); agree = max(abs(d(yaw(f[i]), yaw(h[i][3:]))) for i in range(3))
mv = math.dist(p[2], p[1]); mvh = math.dist(h[2][:3], h[1][:3])
ok = 80 <= abs(turn) <= 100 and agree < 8 and 30 <= mv <= 70 and abs(mv - mvh) < 3
print("%s listen: the listener's facing turned %.0f deg with the head's 90; it agrees with the eyes' head within %.1f deg; "
      "moved %.1f cm when the head moved 40 cm (the eyes' head %.1f cm)" % ("PASS" if ok else "FAIL", turn, agree, mv, mvh))
PYEOF
fi
w "slsnd state" 0.5
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
