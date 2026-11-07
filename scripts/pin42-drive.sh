#!/bin/bash
# pin42-drive.sh - build 42 (som-b42-pin, docs/re-xr-pipeline.md 189): the caragor pin's second stage
# ('PC_Warg_Struggle_StruggleB', a CStruggleAim after a lost takedown) answered as a domination: the white ring at the
# caragor's head, the left hand kept closed (SOMVR_STRUGGLE_PIN=grip). One headless Windows boot (scripts/pin42-boot.sh
# pins this driver to its run), Practice > Caragor 10 m off, no guard: its leap pins Talion; the takedown is mashed
# by the test hands (`struggle auto on`: 7 of its 8 presses in every boot so far - lost, so the second stage comes; boot
# 234902-1: with no presses at all there is none, the takedown just ends). PG_TRIES, in this order (default "before lose grip"):
#   before  `struggle pin outline` (build 41's aim, the 184 state), no answer: pictures, the game's outcome
#   lose    `struggle pin grip`, the left hand left open: the ring up, the game's widget hidden, the game's own loss
#   grip    `struggle pin grip`, PG_REACH s after the stage's start the left hand reaches toward the beast (a real hand:
#           the grip's -Z along the barrel, pinky -> thumb, ahead) and closes, kept closed: won as the game's own
#   fist    `struggle pin grip`, the left fist closed as the takedown ends (a player who shoves with fists): held from
#           the stage's start, won before its window opens - the completion waits for the window
# PG_RUN=RUN: the run to drive (set by scripts/pin42-boot.sh; never found by a name). Pictures at +PG_PIC1 / +PG_PIC2 s
# from the stage's start (its window opens at ~1.1-1.2 s). Lines: pin42-steps.txt (PASS/FAIL, dump frames pic-<try>-<n>).
HERE=$(cd "$(dirname "$0")" && pwd)
TRIES=${PG_TRIES:-before lose grip}
R=${PG_RUN:-}
[ -n "$R" ] && [ -d "$R" ] || { echo "no PG_RUN: start the boot with scripts/pin42-boot.sh"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; ST=$R/pin42-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1" | tee -a "$ST"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
# a word taken for sure (the Windows build reads the trigger file every 1-2 s): written, then its TRIGGER line waited for
wa() { local a0; a0=$(grep -c '^TRIGGER cmd=' "$L"); w "$1" 0.02; local i; for i in $(seq 1 40); do
  [ "$(grep -c '^TRIGGER cmd=' "$L")" -gt "$a0" ] && break; sleep 0.1; done; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
pass() { note "PASS $*"; }
fail() { note "FAIL $*"; }
d() { T_S=0.1 t "dump"; sleep 1.0; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); note "dump $1 $f"; }
ui() { grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-90; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
back() { local k ok=0; for k in $(seq 1 20); do inworld || break; sleep 1; done
  for k in $(seq 1 30); do userin && bail "the user is in VR"; sleep 3
    if inworld; then ok=$((ok + 1)); [ $ok -ge 2 ] && break; else ok=0; T_S=0.3 t "btn R b 1"; t "btn R b 0"; fi; done
  w "practice remove" 2; w "practice go 32500 2100 -24800" 8; note "back: $(ui)"; }
REST_L="hands test L -25 -35 25 0 0 0"
# 0. the controls card away; the hands at rest (the left one open); the test hands mash the takedown (7 of 8: lost)
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$X" || { t "btn R a 1"; t "btn R a 0"; }
wa "$REST_L"; wa "hands test R 25 -35 25 0 0 0"; wa "hands squeeze L 0"; w "front keep 0" 0.3
wa "struggle auto on 1.0"; wa "struggle rule b41"; wa "struggle widget hide"; wa "struggle status"
grep -E '^CONFIG struggle build 42' "$L" | cut -c1-260 | tee -a "$ST"
d "world-start"
for try in $TRIES; do
  case $try in before) pv=outline ;; *) pv=grip ;; esac
  note "== try $try (struggle pin $pv)"
  wa "struggle pin $pv"; wa "$REST_L"; wa "hands squeeze L 0"
  got=0
  for sp in $(seq 1 "${PG_SPAWNS:-4}"); do
    w "practice remove" 1.5; t "head 0 0 0 yaw 0 pitch -10 s 0.3"
    n0=$(lines); w "practice far caragor" 1.5
    fisted=0
    for i in $(seq 1 400); do userin && bail "the user is in VR"
      [ $try = fist ] && [ $fisted = 0 ] && [ "$(since "$n0" '^STRUGGLE t=.* b41 over: CStrugglePressCount' | wc -l)" -gt 0 ] && {
        fisted=1; T_S=0.05 t "btn L squeeze 1"; w "hands squeeze L 1" 0.02; note "the left fist closed as the takedown ended"; }
      [ "$(since "$n0" "^STRUGGLE t=.* b41: CStruggleAim \(Talion's 'PC_Warg_Struggle_StruggleB'\)" | wc -l)" -gt 0 ] && { got=1; break; }
      [ "$(since "$n0" "attack 'PC_Warg_Struggle_PlayerWin_Perfect'" | wc -l)" -gt 0 ] && break
      sleep 0.1; done
    [ $got = 1 ] && break
    since "$n0" '^STRUGGLE t=.* (start|b41 over)' | head -3 | cut -c1-200 | tee -a "$ST"
    note "spawn $sp ($try): no second stage"
    back
  done
  if [ $got = 0 ]; then fail "$try: no second stage in ${PG_SPAWNS:-4} spawns"; back; continue; fi
  ts=$(date +%s.%N)
  [ $try = before ] && wa "struggle auto off" # (the outline's stab not answered by the test hands: no answer)
  if [ $try = grip ]; then
    sleep "${PG_REACH:-0.5}"
    w "hands swing L -15 -5 45 0 0 0 300" 0.02; T_S=0.05 t "btn L squeeze 1"; note "the left hand reaches out and closes"
    wa "hands squeeze L 1"
  fi
  sleep "$(python3 -I -c "import sys; print(max(0.0, float(sys.argv[1]) - (float(sys.argv[2]) - float(sys.argv[3]))))" \
    "${PG_PIC1:-1.6}" "$(date +%s.%N)" "$ts")"
  d "pic-$try-1"; sleep "${PG_PIC2:-0.6}"; d "pic-$try-2"
  for i in $(seq 1 40); do [ "$(since "$n0" "attack 'PC_Warg_Struggle_Player(Win_Normal|Lose)'" | wc -l)" -gt 0 ] && break; sleep 0.25; done
  T_S=0.1 t "btn L squeeze 0"; wa "hands squeeze L 0"; wa "$REST_L"; [ $try = before ] && wa "struggle auto on 1.0"
  since "$n0" "^(STRUGGLE t=.*(start: CStruggleAim|b41: CStruggleAim|b41 the cue|b41 the outline at|b42|b41 over)|RIDE t=.*(pin|held on|window|completed))" \
    | cut -c1-280 | tee -a "$ST"
  outc=$(since "$n0" "attack 'PC_Warg_Struggle_Player(Win_Normal|Lose)'" | head -1 | grep -oE "PC_Warg_Struggle_Player(Win_Normal|Lose)")
  note "$try: the game's outcome ${outc:-none}"
  hid=$(since "$n0" "^STRUGGLE t=.* b42: the game's widget hidden - CStruggleAim" | wc -l)
  ring=$(since "$n0" "^STRUGGLE t=.* b41 the cue: the timer ring at .*(\(Head\)|the pin.s)" | wc -l)
  case $try in
    before) [ "$ring" = 0 ] && pass "before: no ring of ours (build 41's aim)" || fail "before: a ring with pin outline"
            [ "$outc" = PC_Warg_Struggle_PlayerLose ] && pass "before: no answer - the game's own loss" || fail "before: outcome ${outc:-none}" ;;
    lose)   [ "$ring" -gt 0 ] && pass "lose: the white ring up (ahead of the eyes)" || fail "lose: no ring"
            [ "$hid" -gt 0 ] && pass "lose: the game's widget hidden" || fail "lose: the game's widget not hidden"
            [ "$outc" = PC_Warg_Struggle_PlayerLose ] && pass "lose: no grip - the game's own loss" || fail "lose: outcome ${outc:-none}" ;;
    fist)   [ "$ring" -gt 0 ] && pass "fist: the white ring up (ahead of the eyes)" || fail "fist: no ring"
            since "$n0" "^RIDE t=.* held on .*completed as the game's own" | grep -q . && pass "fist: completed as the game's own" \
              || fail "fist: no completion"
            since "$n0" "^RIDE t=.* its window is not open yet" | grep -q . && note "fist: the completion waited for the window" \
              || note "fist: won after the window opened (no wait)"
            [ "$outc" = PC_Warg_Struggle_PlayerWin_Normal ] && pass "fist: won (the game's PlayerWin_Normal)" || fail "fist: outcome ${outc:-none}" ;;
    grip)   [ "$ring" -gt 0 ] && pass "grip: the white ring up (ahead of the eyes)" || fail "grip: no ring"
            [ "$hid" -gt 0 ] && pass "grip: the game's widget hidden" || fail "grip: the game's widget not hidden"
            since "$n0" "^RIDE t=.* held on .*completed as the game's own" | grep -q . && pass "grip: completed as the game's own" \
              || fail "grip: no completion"
            [ "$outc" = PC_Warg_Struggle_PlayerWin_Normal ] && pass "grip: won (the game's PlayerWin_Normal)" || fail "grip: outcome ${outc:-none}" ;;
  esac
  sleep 2; d "after-$try"
  back
done
w "practice remove" 2
wa "struggle status"
grep -E '^(STRUGGLE END build 42|RIDE END pin)' "$L" | cut -c1-300 | tee -a "$ST"
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
echo "$(date +%T) done"
