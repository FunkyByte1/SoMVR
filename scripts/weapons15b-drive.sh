#!/bin/bash
# weapons15b-drive.sh - build 15b's weapons fixes (handoff/HANDOFF-weapons15b.md, docs/re-xr-pipeline.md 46), headless at
# the practice arrival. Boot (sandbox off) with scripts/headset-build16.env's switches:
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build16.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# and this script at the same time (W15_WT = a word of the launch command, default this worktree's name). The hands and
# buttons are the layer's test controllers (RUN/xrtest.txt: `hand L|R head X Y Z PITCH YAW ROLL [s S]`, `btn ...`,
# `stick R X Y`). Stops at once (quit) when the user goes into VR.
# W15_STEPS = any of (in this order):
#   sword    the sword drawn at the right shoulder, then W15_N (4) rounds of: the bow out (W15_BOW: trigger = the left
#            trigger held, slot = the left grip behind the left shoulder), round 2 a nock and a let-down, round 3 the
#            right stick's stealth crouch latched while aiming (build 15's session), round 4 a shot and the crouch; the
#            exit by apart; the sword back in the hand (the restore), a picture, a swing. Then the user's #9: the sword
#            put back, the stick's crouch, a draw at the shoulder. PASS/FAIL lines: no DENIED, no refused draw.
#   slot     (SOMVR_BOW_DRAW=slot) the bow from behind the left shoulder: the left grip there, held = the bow out; the
#            grip let go there puts it back; the left trigger alone brings no bow; a climb-like left trigger press
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${W15_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${W15_STEPS:-sword}
BOWBY=${W15_BOW:-trigger}
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
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$L"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$L")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
# ok NAME CMD...: PASS/FAIL by the command's status
ok() { local n=$1; shift; if "$@"; then echo "$(date +%T) PASS $n"; else echo "$(date +%T) FAIL $n"; fi; }
cnt() { grep -cE "$1" "$L"; }
# the bow's poses (bow2-drive.sh): the bow hand 60 cm ahead of a jaw anchor, the string hand D cm back from it
pose() { awk -v a="$1" -v p="$2" -v d="$3" -v sm="${4:-}" -v L=60 'BEGIN { r = 3.14159265 / 180
  dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 7; ay = -13; az = 0
  bx = ax + L * dx; by = ay + L * dy; bz = az + L * dz; k = L - d
  printf "hand L head %.1f %.1f %.1f %.1f %.1f 0\n", bx, by, bz, p, -a
  printf "hand R head %.1f %.1f %.1f %.1f %.1f 0%s\n", ax + k * dx + 1.5, ay + k * dy, az + k * dz, p, -a, sm != "" ? " s " sm : "" }'; }
setpose() { local IFS=$'\n'; t $(pose "$@"); }
REST_L="hand L head -22 -40 15 0 0 0"
REST_R="hand R head 22 -40 15 0 0 0"
rest() { t "$REST_L" "$REST_R"; }
SLOT_R="hand R head 10 -10 -15 0 0 0"        # the sword's slot over the right shoulder (SOMVR_SLOTS)
SLOT_L="hand L head -12 -10 -15 0 0 0"       # the bow's slot behind the left shoulder (SOMVR_BOW_DRAW=slot)
OUT_R="hand R head 30 -25 40 0 0 0"
# d NAME: a stereo dump now (both eyes + our hands' layer; compose with scripts/xr_compose.py RUN)
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/w15-steps.txt"; }
held() { grep -E '^SLOTS t=.* sword: (done|DENIED|the game put it away: kept|our keep undone)' "$L" | tail -1 |
         grep -qE 'now in the hand|kept in the hand'; }
draw_sword() { t "$SLOT_R"; sleep 0.4; t "btn R squeeze 1"; sleep 0.3; t "$OUT_R"; t "btn R squeeze 0"; sleep 0.5; }
put_sword() { t "$SLOT_R"; sleep 0.4; t "btn R squeeze 1"; sleep 0.3; t "btn R squeeze 0"; t "$REST_R"; sleep 0.5; }
swing() { t "hand R head 40 -5 30 0 -60 0"; T_S=0.5 t "hand R head -25 -30 55 0 60 0 s 0.25"; d "$1-swing"; t "$OUT_R"; }
bow_out() {
  if [ "$BOWBY" = slot ]; then t "$SLOT_L"; sleep 0.4; t "btn L squeeze 1"; sleep 0.3; setpose 0 0 30
  else setpose 0 0 30; t "btn L trigger 1"; fi
  waitfor '^BOW .* enter ' 3 && echo "$(date +%T) bow out" || echo "$(date +%T) FAIL the bow did not come out"; sleep 1; }
bow_apart() {
  if [ "$BOWBY" = slot ]; then t "btn L squeeze 0"; else t "btn L trigger 0"; fi
  rest; waitfor '^BOW .* exit: ' 4 && echo "$(date +%T) $(grep -E '^BOW .* exit: ' "$L" | tail -1 | cut -c1-90)" ||
    echo "$(date +%T) FAIL no bow exit"; }
crouch_stick() { t "stick R 0 -1"; sleep 0.4; t "stick R 0 0"; sleep 0.3; }
# 0. the controls card away (the right grip, else A), the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
rest
grep -E '^(WARN|CONFIG slots: SOMVR_SLOTS_STEALTH|CONFIG bow=)' "$L" | head -5 | cut -c1-200
m a00-start 0.5
if has sword; then
  den0=$(cnt 'sword: DENIED'); ref0=$(cnt 'sword: the draw call refused')
  draw_sword; ok "the sword drawn at the shoulder" held
  for i in $(seq 1 "${W15_N:-4}"); do
    echo "== round $i"
    bow_out
    case $i in
      2) setpose 0 0 4; sleep 0.3; t "btn R squeeze 1"; setpose 0 0 13 0.3; sleep 0.4; t "btn R squeeze 0"; sleep 1 ;;
      3) crouch_stick; waitfor '^CROUCH button' 2; sleep 1.5 ;;
      4) crouch_stick; setpose 0 0 4; sleep 0.3; t "btn R squeeze 1"; setpose 0 0 57 0.8; sleep 1; T_S=0.1 t "btn R squeeze 0"
         waitfor '^BOW .* (fire:|dropped:|let down:)' 3; sleep 1 ;;
    esac
    bow_apart
    sleep 1.5
    grep -E '^(SLOTS|CROUCH) t=' "$L" | tail -4 | cut -c1-200
    ok "round $i: the sword back in the hand" held
    t "$OUT_R"; sleep 0.5; d "r$i-after-bow"; m "r$i-after-bow" 0.2
    swing "r$i"
  done
  echo "== the user's #9: the sword put back, the stick's crouch, a draw"
  held || draw_sword
  put_sword; if held; then echo "$(date +%T) FAIL put back"; else echo "$(date +%T) PASS put back"; fi
  crouch_stick; waitfor '^CROUCH game down' 4 && echo "$(date +%T) Talion down (stealth)"; sleep 1
  grep -E '^SLOTS t=.*active slot' "$L" | tail -1 | cut -c1-160
  d "c1-crouched-sheathed"; m c1-crouched-sheathed 0.2
  draw_sword; sleep 0.5; ok "drawn from the stealth crouch" held
  grep -E '^(SLOTS|CROUCH) t=' "$L" | tail -5 | cut -c1-200
  d "c2-drawn"; m c2-drawn 0.2; swing c2
  echo "DENIED $(( $(cnt 'sword: DENIED') - den0 )) refused draws $(( $(cnt 'sword: the draw call refused') - ref0 ))"
  [ "$(cnt 'sword: DENIED')" -eq "$den0" ] && echo "PASS no DENIED" || echo "FAIL DENIED"
fi
if has slot; then
  echo "== the bow's left-shoulder slot"
  # the left trigger alone: no bow
  rest; t "btn L trigger 1"; sleep 1.5; t "btn L trigger 0"
  n=$(cnt '^BOW .* enter '); sleep 0.5
  [ "$(grep -cE '^BOW .* enter ' "$L")" -eq "$n" ] && echo "PASS the left trigger alone: no bow" || echo "FAIL the left trigger brought the bow"
  draw_sword; ok "the sword drawn" held
  BOWBY=slot
  for i in 1 2 3; do
    echo "== slot round $i"
    bow_out; d "s$i-bow"; m "s$i-bow" 0.2
    [ "$i" = 2 ] && { setpose 0 0 4; sleep 0.3; t "btn R squeeze 1"; setpose 0 0 57 0.8; sleep 1; T_S=0.1 t "btn R squeeze 0"
                      waitfor '^BOW .* (fire:|dropped:|let down:)' 3; sleep 1; }
    if [ "$i" = 3 ]; then bow_apart
    else t "$SLOT_L"; sleep 0.5; t "btn L squeeze 0"; waitfor '^BOW .* exit: ' 3 &&
           echo "$(date +%T) $(grep -E '^BOW .* exit: ' "$L" | tail -1 | cut -c1-90)" || echo "$(date +%T) FAIL no exit at the slot"
         rest; fi
    sleep 1.5; ok "slot round $i: the sword back in the hand" held
  done
  # the left trigger on the hilt behind the right hand: the two hands' grab (the trigger is a grab now)
  held || draw_sword
  n=$(cnt '^TWOHAND t=.* grab'); t "$OUT_R" "hand L head 30 -27 30 0 0 0"; sleep 0.5; t "btn L trigger 1"; sleep 1
  [ "$(cnt '^TWOHAND t=.* grab')" -gt "$n" ] && echo "$(date +%T) PASS the left trigger on the hilt: the two hands' grab" ||
    echo "$(date +%T) INFO no TWOHAND grab by the trigger: $(grep -E '^TWOHAND' "$L" | tail -1 | cut -c1-160)"
  d t1-twohand; m t1-twohand 0.2; t "btn L trigger 0"; rest; sleep 0.5
fi
echo "steps done $(date +%T)"
grep -E '^(SLOTS END|CROUCH END|BOW END)' "$L" | cut -c1-200
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
