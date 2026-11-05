#!/bin/bash
# struggle41-drive.sh - build 41 (som-b41-struggle, docs/re-xr-pipeline.md 180): one VR rule for every struggle, proven
# with the test hands answering as a player would (`struggle auto on`: the mod times them on the game thread - the
# Windows build reads the trigger file every 1-2 s, too slow for a mash). One headless boot (scripts/headless_vr.sh --win
# with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the layer's test file). SG_STEPS, in this order (default
# "captain chain"):
#   captain  Practice > Captain (the practice's captains: Role_Drowned / Role_Executioner in turn), no guard: his blows
#            until Talion kneels - the last chance (a CStruggleAim): the outline at its node, the test hand laid on it
#            (the mod's `STRUGGLE lineup`), a thrust along it; up to SG_TRIES (3) tries; pictures at the outline and
#            after the stab; SG_B27=1 first one try with `struggle rule b27` (the old answer, the same hands)
#   chain    Practice > Caragor: scripts/pcar29-drive.sh's chain (the test blade across each leap, the counter, the stun,
#            the mount by the left hand at the offer) with the struggles it meets answered by the rule: the takedown
#            (CStrugglePressCount: the mash) and the domination (SOMVR_RIDE's grip; the ring runs down) - pictures at
#            each struggle's cue. It quits the game at its end.
# SG_RUN=RUN drives a running boot. Lines: STRUGGLE (b41 ...), RIDE; sg-steps.txt (PASS/FAIL, dump frames).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${SG_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${SG_STEPS:-captain chain}
T0=$(date +%s)
R=${SG_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log; X=$R/xr-layer.log; ST=$R/sg-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1" | tee -a "$ST"; kill $WATCH 2>/dev/null; echo quit >> "$R/cmd"; exit 1; }
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
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); note "dump $1 $f"; }
waitn() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
ui() { grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-90; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
member() { grep -E "^CAPTAIN member [0-9]+: .*$1" "$L" | tail -1 | sed -nE 's/^CAPTAIN member ([0-9]+):.*/\1/p'; }
over() { [ "$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down")" -gt "$1" ]; }
back() { local k ok=0; for k in $(seq 1 20); do inworld || break; sleep 1; done
  for k in $(seq 1 30); do userin && bail "the user is in VR"; sleep 3
    if inworld; then ok=$((ok + 1)); [ $ok -ge 2 ] && break; else ok=0; T_S=0.3 t "btn R b 1"; t "btn R b 0"; fi; done
  w "practice remove" 2; w "practice go 32500 2100 -24800" 8; note "back: $(ui)"; }

# 0. the controls card away; the hands at rest; the rule's test hands
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$X" || { t "btn R a 1"; t "btn R a 0"; }
wa "hands test L -25 -35 25 0 0 0"; wa "hands test R 25 -35 25 0 0 0"; w "front keep 0" 0.3
wa "struggle auto on ${SG_DELAY:-1.0}"; wa "struggle status"
grep -E '^CONFIG struggle' "$L" | cut -c1-420 | tee -a "$ST"

# the pictures at each struggle's moments (a watcher: the cue shown, the outline placed, the stab, the game's counts)
watch_pics() { local seen=$1 pat k n
  while sleep 0.2; do
    n=$(grep -nE '^STRUGGLE t=.*b41 (the cue:|the outline at|the stab along|the game counts [0-9]+ of [0-9]+ - done)|^STRUGGLE t=.*b41 the hold: .* closed' "$L" | tail -1 | cut -d: -f1)
    [ -n "$n" ] && [ "$n" -gt "$seen" ] || continue
    seen=$n; k=$(sed -n "${n}p" "$L" | sed -nE 's/.*b41 (the [a-z]+).*/\1/p' | tr ' ' '-')
    sleep "${SG_PIC_DELAY:-0.15}"; T_S=0.1 t "dump"; sleep 1.2
    f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); echo "$(date +%T) dump auto-$k $f" >> "$ST"
  done; }
watch_pics "$(lines)" & WATCH=$!

# the sword from the right hip's slot (a player fights with it drawn; SG_DRAW=0: never)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { wa "$SLOT"; sleep 0.5; wa "hands squeeze R 1"; sleep 0.6; wa "$OUT"; sleep 0.5; wa "hands squeeze R 0"; sleep 0.5
  wa "hands test R 25 -35 25 0 0 0"; sleep 0.4; grep -E "^SLOTS t=.*sword" "$L" | tail -1 | cut -c1-200 | tee -a "$ST"; }
if has captain; then
  echo "== captain: Practice > Captain - the last chance answered by the outline's stab" | tee -a "$ST"
  [ "${SG_DRAW:-1}" = 1 ] && { draw_slot; DRAWN=1; }
  D=$(member Role_Drowned); E=$(member Role_Executioner); note "members: Drowned ${D:-none}, Executioner ${E:-none}"
  won=0
  for k in $(seq 1 "${SG_TRIES:-3}"); do
    [ $((k % 2)) = 1 ] && m=$D || m=$E
    rule=b41; [ -n "${SG_B27:-}" ] && [ $k = 1 ] && rule=b27
    # the last try with the sword put back (the empty hand at the outline's grip)
    [ -n "${DRAWN:-}" ] && [ $k = "${SG_TRIES:-3}" ] && [ "${SG_TRIES:-3}" -gt 1 ] && { note "the sword put back: the empty hand's try"; draw_slot; DRAWN=; }
    wa "struggle rule $rule"
    note "== try $k (member ${m:-next}, rule $rule)"
    [ -n "$m" ] && w "captain pick $m" 0.4
    w "captain stakes on" 0.3; n0=$(lines); w "practice captain" 3
    since "$n0" '^CAPTAIN t=.* spawn ' | head -1 | cut -c1-200 | tee -a "$ST"
    for i in $(seq 1 60); do [ "$(since "$n0" '^CAPTAIN t=.* the presentation is over' | wc -l)" -gt 0 ] && break; sleep 1; done
    w "execute chase on" 0.4
    R0=$(cnt "PC_PreDeathRecover|^CAPTAIN t=.* Talion is down"); got=0
    for i in $(seq 1 400); do userin && bail "the user is in VR"
      [ "$(since "$n0" '^STRUGGLE t=.* start' | wc -l)" -gt 0 ] && { got=1; break; }
      [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && { T_S=0.3 t "btn R a 1"; t "btn R a 0"; }
      over "$R0" && break; sleep 0.5; done
    w "execute chase off" 0.3
    since "$n0" '^STRUGGLE t=.* start' | head -1 | cut -c1-400 | tee -a "$ST"
    if [ $got = 1 ]; then
      for i in $(seq 1 40); do [ "$(since "$n0" '^STRUGGLE t=.* (over after|b41 over)' | wc -l)" -gt 0 ] && break
        [ "$(since "$n0" "show 'PC_StruggleTutorial'" | wc -l)" -gt 0 ] && [ "$(since "$n0" '^SESSION ui .*paused=1' | wc -l)" -gt 0 ] && { T_S=0.3 t "btn R a 1"; t "btn R a 0"; }
        sleep 0.5; done
      sleep 2.5; d "try$k-after"
      since "$n0" '^STRUGGLE (t=.*(b41|start|over after|result|the finisher|thrust outside)|lineup)' | grep -v 'test hands:' | head -16 | cut -c1-360 | tee -a "$ST"
      since "$n0" "^BODYMOMENT t=.*attack 'PC_[A-Za-z_]*(PCWin|PreDeathRecover)[A-Za-z_]*'" | head -2 | cut -c1-200 | tee -a "$ST"
      if since "$n0" '^STRUGGLE t=.*b41 over: .* WON' | grep -q . || since "$n0" "PCWin" | grep -q .; then
        pass "try $k ($rule): the last chance won"; [ $rule = b41 ] && won=$((won + 1))
      else fail "try $k ($rule): the last chance lost"; fi
    else fail "try $k: no struggle"; fi
    back
  done
  w "practice remove" 2; w "captain stakes off" 0.3; sleep 2
  note "captain: won $won with the rule b41"
fi

# a probe (SG_BEASTS="ghul graug", "" = none): each beast spawned beside Talion, no guard, SG_BEAST_S s - which struggle
# its attacks bring (the rule answers it; the class logged)
for b in ${SG_BEASTS-ghul graug}; do
  wa "struggle rule b41"
  note "== beast probe: $b"; n0=$(lines); R0=$(cnt "^CAPTAIN t=.* Talion is down|^SCREENS t=.* the death")
  w "practice remove" 1.5; w "practice spawn $b" 3
  for i in $(seq 1 $(( ${SG_BEAST_S:-45} * 2 ))); do userin && bail "the user is in VR"
    [ "$(since "$n0" '^STRUGGLE t=.* b41 over' | wc -l)" -gt 0 ] && break
    [ "$(cnt "^CAPTAIN t=.* Talion is down|^SCREENS t=.* the death")" -gt "$R0" ] && break; sleep 0.5; done
  sleep 2
  since "$n0" '^STRUGGLE t=.*(start|b41 (over|the game counts|the hold|a swipe|the stab|the outline))' | head -8 | cut -c1-300 | tee -a "$ST"
  since "$n0" "^BEASTS t=.* attack '[^']*' at Talion" | grep -oE "attack '[^']*'" | sort | uniq -c | sort -rn | head -5 | tee -a "$ST"
  back
done

kill $WATCH 2>/dev/null
grep -E '^STRUGGLE END' "$L" | cut -c1-700 | tee -a "$ST"
if has chain; then
  echo "== chain: Practice > Caragor (pcar29's chain) with the rule's answers" | tee -a "$ST"
  watch_pics "$(lines)" & WATCH=$!
  PC_RUN=$R PC_STEPS=chain PC_SPAWNS=${SG_SPAWNS:-5} bash "$HERE/pcar29-drive.sh" 2>&1 | tail -40
  kill $WATCH 2>/dev/null
  grep -E '^STRUGGLE (t=.*b41 (over|the game counts|the cue)|END)|^RIDE t=.*(struggle|domination)' "$L" | tail -24 | cut -c1-400 | tee -a "$ST"
else
  echo "$(date +%T) quit"; echo quit >> "$R/cmd"
fi
echo "$(date +%T) done"
