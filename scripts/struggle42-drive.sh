#!/bin/bash
# struggle42-drive.sh - build 42 (som-b42-struggle, docs/re-xr-pipeline.md 184): the game's own struggle widget hidden
# under the rule (SOMVR_STRUGGLE_WIDGET), before and after in one boot (the word `struggle widget show|hide` between
# struggles). One headless boot (scripts/headless_vr.sh --win with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the
# layer's test file). SG_STEPS, in this order (default "captain caragor menu"):
#   captain  Practice > Captain, the sword drawn, no guard: his blows until Talion kneels - the last chance (CStruggleAim),
#            answered by the test hands (`struggle auto on`); try 1 with `widget show` (before), try 2 with `widget hide`
#            (after); pictures at +SG_PIC1/+SG_PIC2 s from each struggle's start
#   caragor  Practice > Caragor 10 m off, no guard: its leap pins Talion (the takedown, CStrugglePressCount), the mash by
#            the test hands; the first pin with `widget show`, the next with `widget hide` (up to SG_SPAWNS spawns each)
#   menu     the pause menu opened after the struggles (a picture: the game's menus still drawn), closed again
# SG_RUN=RUN: the run to drive - set by scripts/struggle42-boot.sh (it starts the boot and pins this driver to the run its
# headless_vr.sh prints). Without SG_RUN the driver refuses unless SG_FIND=1 (the old find by SG_WT: on 11-06 it matched
# SG_WT=build-win10 to another helper's build-win10 boot started a second later and drove that game for 9 minutes).
# Lines: STRUGGLE (b42 ...); sg42-steps.txt (PASS/FAIL, dump frames: pic-<label>-<n>).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${SG_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${SG_STEPS:-captain caragor menu}
T0=$(date +%s)
R=${SG_RUN:-}
[ -n "$R" ] || [ "${SG_FIND:-0}" = 1 ] || { echo "no SG_RUN: start the boot with scripts/struggle42-boot.sh (or SG_FIND=1)"; exit 1; }
for i in $(seq 1 720); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; ST=$R/sg42-steps.txt
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
# a word taken for sure (the Windows build reads the trigger file every 1-2 s): written, then its TRIGGER line waited for
wa() { local a0; a0=$(grep -c '^TRIGGER cmd=' "$L"); w "$1" 0.02; local i; for i in $(seq 1 40); do
  [ "$(grep -c '^TRIGGER cmd=' "$L")" -gt "$a0" ] && break; sleep 0.1; done; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
pass() { note "PASS $*"; }
fail() { note "FAIL $*"; }
d() { T_S=0.1 t "dump"; sleep 1.0; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); note "dump $1 $f"; }
ui() { grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-90; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
member() { grep -E "^CAPTAIN member [0-9]+: .*$1" "$L" | tail -1 | sed -nE 's/^CAPTAIN member ([0-9]+):.*/\1/p'; }
over() { [ "$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down")" -gt "$1" ]; }
back() { local k ok=0; for k in $(seq 1 20); do inworld || break; sleep 1; done
  for k in $(seq 1 30); do userin && bail "the user is in VR"; sleep 3
    if inworld; then ok=$((ok + 1)); [ $ok -ge 2 ] && break; else ok=0; T_S=0.3 t "btn R b 1"; t "btn R b 0"; fi; done
  w "practice remove" 2; w "practice go 32500 2100 -24800" 8; note "back: $(ui)"; }
# the pictures of one struggle: at +SG_PIC1 and +SG_PIC2 s from its start line (the widget slides in; the aim's window
# opens at ~0.65 s, the takedown's mash runs ~2.5 s)
pics() { local lab=$1 n0=$2 i
  for i in $(seq 1 120); do [ "$(since "$n0" '^STRUGGLE t=.* start: ' | wc -l)" -gt 0 ] && break; sleep 0.1; done
  [ "$(since "$n0" '^STRUGGLE t=.* start: ' | wc -l)" -gt 0 ] || return 1
  sleep "${SG_PIC1:-0}"; d "pic-$lab-1"; sleep "${SG_PIC2:-0}"; d "pic-$lab-2"; }
# 0. the controls card away; the hands at rest; the rule's test hands
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$X" || { t "btn R a 1"; t "btn R a 0"; }
wa "hands test L -25 -35 25 0 0 0"; wa "hands test R 25 -35 25 0 0 0"; w "front keep 0" 0.3
wa "struggle auto on ${SG_DELAY:-1.0}"; wa "struggle rule b41"; wa "struggle status"
grep -E '^CONFIG struggle build 4[12]' "$L" | cut -c1-300 | tee -a "$ST"
d "world-start"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { wa "$SLOT"; sleep 0.5; wa "hands squeeze R 1"; sleep 0.6; wa "$OUT"; sleep 0.5; wa "hands squeeze R 0"; sleep 0.5
  wa "hands test R 25 -35 25 0 0 0"; sleep 0.4; grep -E "^SLOTS t=.*sword" "$L" | tail -1 | cut -c1-200 | tee -a "$ST"; }
if has captain; then
  echo "== captain: Practice > Captain - the last chance, the widget shown (1) then hidden (2)" | tee -a "$ST"
  draw_slot
  D=$(member Role_Drowned); E=$(member Role_Executioner); note "members: Drowned ${D:-none}, Executioner ${E:-none}"
  for k in 1 2; do
    [ $k = 1 ] && { m=$D; wv=show; } || { m=$E; wv=hide; }
    wa "struggle widget $wv"
    note "== try $k (member ${m:-next}, widget $wv)"
    [ -n "$m" ] && w "captain pick $m" 0.4
    w "captain stakes on" 0.3; n0=$(lines); w "practice captain" 3
    for i in $(seq 1 60); do [ "$(since "$n0" '^CAPTAIN t=.* the presentation is over' | wc -l)" -gt 0 ] && break; sleep 1; done
    w "execute chase on" 0.4
    R0=$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down"); got=0
    for i in $(seq 1 400); do userin && bail "the user is in VR"
      [ "$(since "$n0" '^STRUGGLE t=.* start' | wc -l)" -gt 0 ] && { got=1; break; }
      [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && { T_S=0.3 t "btn R a 1"; t "btn R a 0"; }
      over "$R0" && break; sleep 0.1; done
    if [ $got = 1 ]; then
      pics "last$k-$wv" "$n0"
      w "execute chase off" 0.3
      for i in $(seq 1 40); do [ "$(since "$n0" '^STRUGGLE t=.* (over after|b41 over)' | wc -l)" -gt 0 ] && break
        [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && [ "$(since "$n0" '^SESSION ui .*paused=1' | wc -l)" -gt 0 ] && { T_S=0.3 t "btn R a 1"; t "btn R a 0"; }
        sleep 0.5; done
      since "$n0" '^STRUGGLE t=.*(start:|b42|b41 over)' | head -5 | cut -c1-300 | tee -a "$ST"
      hid=$(since "$n0" "^STRUGGLE t=.* b42: the game's widget hidden" | wc -l)
      if [ $wv = hide ]; then [ "$hid" -gt 0 ] && pass "try $k: the widget hidden" || fail "try $k: no hide line"
      else [ "$hid" = 0 ] && pass "try $k: the widget left shown (before)" || fail "try $k: hidden with show"; fi
      since "$n0" '^STRUGGLE t=.*b41 over: .* WON' | grep -q . && note "try $k: the last chance won" || note "try $k: the last chance lost"
      sleep 2; d "after-last$k"
    else w "execute chase off" 0.3; fail "try $k: no struggle"; fi
    back
  done
  w "practice remove" 2; w "captain stakes off" 0.3; sleep 2
fi
if has caragor; then
  echo "== caragor: Practice > Caragor - the pin (the takedown), the widget shown then hidden" | tee -a "$ST"
  for wv in show hide; do
    wa "struggle widget $wv"; got=0
    for sp in $(seq 1 "${SG_SPAWNS:-4}"); do
      w "practice remove" 1.5; t "head 0 0 0 yaw 0 pitch -10 s 0.3"
      n0=$(lines); w "practice far caragor" 1.5
      for i in $(seq 1 300); do userin && bail "the user is in VR"
        [ "$(since "$n0" '^STRUGGLE t=.* start: CStrugglePressCount' | wc -l)" -gt 0 ] && { got=1; break; }
        [ "$(since "$n0" '^STRUGGLE t=.* start: ' | wc -l)" -gt 0 ] && break; sleep 0.1; done
      if [ $got = 1 ]; then
        pics "pin-$wv" "$n0"
        for i in $(seq 1 30); do [ "$(since "$n0" '^STRUGGLE t=.* (over after|b41 over)' | wc -l)" -gt 0 ] && break; sleep 0.5; done
        since "$n0" '^STRUGGLE t=.*(start:|b42|b41 (over|the game counts))' | head -6 | cut -c1-300 | tee -a "$ST"
        hid=$(since "$n0" "^STRUGGLE t=.* b42: the game's widget hidden" | wc -l)
        if [ $wv = hide ]; then [ "$hid" -gt 0 ] && pass "pin ($wv): the widget hidden" || fail "pin ($wv): no hide line"
        else [ "$hid" = 0 ] && pass "pin ($wv): the widget left shown (before)" || fail "pin ($wv): hidden with show"; fi
        sleep 2; d "after-pin-$wv"
        break
      fi
      since "$n0" '^STRUGGLE t=.* start: ' | head -1 | cut -c1-200 | tee -a "$ST"
      note "spawn $sp ($wv): no pin"
      back
    done
    [ $got = 1 ] || fail "caragor ($wv): no pin in ${SG_SPAWNS:-4} spawns"
    back
  done
  w "practice remove" 2
fi
if has menu; then
  echo "== menu: the pause menu after the struggles" | tee -a "$ST"
  t "btn L menu 1"; t "btn L menu 0"; sleep 2.5; note "menu: $(ui)"; d "menu"
  T_S=0.3 t "btn R b 1"; t "btn R b 0"; sleep 2; note "closed: $(ui)"; d "world-end"
fi
grep -E '^STRUGGLE END build 4[12]' "$L" | cut -c1-500 | tee -a "$ST"
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
echo "$(date +%T) done"
