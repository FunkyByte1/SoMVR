#!/bin/bash
# hr37-drive.sh - b37 hotreload2 (docs/re-xr-pipeline.md 156.9-156.11): one headless Windows boot of this worktree with
# SOMVR_DEV_RELOAD=1; swaps between and during drivers that turn the view, each checked by the host itself (the new
# module's first frame against the old one's last: `RELOAD gen N view` and, past 1 deg / 2 cm, `WARN RELOAD view moved`).
# HR_STEPS (default all, in this order):
#   idle   two swaps standing still
#   turn   snap turns (`turn+`/`turn-`, the right stick), a swap right after each; SOMVR_TURN=smooth by a swap, the right
#          stick's smooth turn, a swap right after; back to snap by a swap
#   sword  sword31 (air two cut weak stab tier exec), a swap with the sword drawn, the orc removed, sword31's air step
#          again: the weighted sword's first swings after a reload against the first run's (no orc in either)
#   fling  fling31 (the wall, the sword low): the climb's hold and fling, a swap after
#   bow    bow33: the bow's draw and shot, a swap after
#   kill   deaths36 (cuts neck stealth: kills by the hand; kills31's KILLS rows have no Windows sites), a swap after
#   knife  polish36's knife: the knife from either hip, a swap after
#   menu   the VR menu opened by the palm, its Test tab, a swap with the menu open (the player's Apply happens there),
#          closed, a swap
#   busy   swaps asked during drivers (fling31, kills31, bow33 in the background): each waits for quiet or is refused
#          with its reason, never half-applied (the host's lines; no `view moved`)
#   ride   ride26 (a caragor mounted, ridden): swaps asked while riding wait or are refused; one after it
#   lost   the check's own proof: a swap with SOMVR_TEST_VK_SKIP=bodymoments,anchor (those parts not taken: the old
#          code's loss) must log `WARN RELOAD view moved` when their offsets are not ~0; then a swap without it
#   player the player's swap path (somvr_host_player_swap) cannot be reached by a word: checked by its refusal lines in
#          the user-session tests (som-vr-headset-win-test.sh), not here
# Results: RUN/hr37.txt (and stdout). Quits at the end, at once when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${HR_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${HR_STEPS:-idle turn sword fling bow kill knife lost menu busy ride}
T0=$(date +%s)
R=${HR_RUN:-}
for i in $(seq 1 720); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -12); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && grep -q 'ShadowOfMordor.exe' "$d/launch-cmd.txt" &&
      [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break # (the game's run: not a stand-in test's)
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
for i in $(seq 1 300); do [ -f "$R/launch-cmd.txt" ] && break; sleep 2; done
R=$(cd "$R" && pwd -P) || { echo "no run folder $R"; exit 1; }
L=$R/somvr.log X=$R/xr-layer.log O=$R/hr37.txt
say() { echo "$(date +%T) $*" | tee -a "$O"; }
say "run $R (the tree $WT, steps: $STEPS)"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { say "STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${TS:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$L}"; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
say "arrived; $(grep -m1 -oE 'LOGIC .* loaded in [0-9.]+ ms; reload: [^;]*' "$L" | sed 's/.*; reload/reload/' | cut -c1-160)"
sleep 8
PASS=0 FAIL=0
pass() { PASS=$((PASS + 1)); say "PASS $*"; }
fail() { FAIL=$((FAIL + 1)); say "FAIL $*"; }
RX='^WIN .*RELOAD gen [0-9]+ (ok|refused)'
# a swap by the word $1 (default `reload`): the host's RELOAD line within 40 s; RLINE = it; GEN; 0 = ok
GEN=0
swap() { local n0 i; n0=$(cnt "$RX"); w "${1:-reload}" 0.1
  for i in $(seq 1 400); do userin && bail "the user is in VR"; [ "$(cnt "$RX")" -gt "$n0" ] && break; sleep 0.1; done
  RLINE=$(grep -E "$RX" "$L" | tail -1); [ "$(cnt "$RX")" -gt "$n0" ] || { RLINE=""; say "  no RELOAD line in 40 s"; return 1; }
  GEN=$(echo "$RLINE" | grep -oE 'RELOAD gen [0-9]+' | grep -oE '[0-9]+$')
  sleep 0.3
  local v; v=$(echo "$RLINE" | grep -oE 'the view (across the swap moved [0-9.]+ cm and turned [0-9.]+ deg|not measured[^;]*)' | head -1)
  say "  ${2:-swap}: $(echo "$RLINE" | grep -oE 'RELOAD gen [0-9]+ [a-z]+[^:]*' | head -1 | cut -c1-120); $v$(echo "$RLINE" | grep -oE 'quiet [0-9]+ ms[^,]*' | head -1 | sed 's/^/; /')$(echo "$RLINE" | grep -oE 'refused[^:]*: .*' | cut -c1-170 | sed 's/^/ /')"
  case $RLINE in *" ok"*) return 0 ;; esac; return 1; }
# a driver against this run: a copy beside it (its relative paths), its final quit disarmed (its bail's kept)
run_driver() { local name=$1 tag=$2 envs=$3 src=$HERE/$1-drive.sh cp=$HERE/.hr37-$1-$2-drive.sh out=$R/hr37-$1-$2.txt pid k EV=()
  [ -n "$envs" ] && IFS='|' read -ra EV <<< "$envs" # KEY=VALUE|KEY=VALUE (values with spaces)
  [ -f "$src" ] || { fail "no $src"; return 1; }
  sed -E '/bail\(\)/! s#echo quit >> "\$R/cmd"#: hr37: no quit#' "$src" > "$cp"
  env S31_WT="$WT" P31_WT="$WT" P31_RUN="$R" PIN_WT="$WT" PIN_RUN="$R" B33_WT="$WT" B33_RUN="$R" KL_WT="$WT" KL_RUN="$R" \
      P36_WT="$WT" P36_RUN="$R" R26_WT="$WT" R26_RUN="$R" DS_WT="$WT" DS_RUN="$R" DS_NOQUIT=1 "${EV[@]}" bash "$cp" > "$out" 2>&1 & pid=$!
  DPID=$pid
  sleep 2; touch "$R" # a driver's own run search: a run folder newer than its start
  [ "${3+bg}" = bg ] && [ "${4:-}" = bg ] && return 0
  for k in $(seq 1 1200); do kill -0 "$pid" 2>/dev/null || break; userin && { kill "$pid"; bail "the user is in VR"; }; sleep 1; done
  kill -0 "$pid" 2>/dev/null && { kill "$pid"; say "$name ($tag): stopped after 1200 s"; }
  rm -f "$cp"
  say "$name ($tag): $(grep -c 'PASS' "$out") PASS, $(grep -c 'FAIL' "$out") FAIL"; }
bg_end() { local k; for k in $(seq 1 1200); do kill -0 "$DPID" 2>/dev/null || break; userin && { kill "$DPID"; bail "the user is in VR"; }; sleep 1; done
  kill -0 "$DPID" 2>/dev/null && kill "$DPID"; rm -f "$HERE"/.hr37-*-bg-drive.sh; }

if has idle; then
  say "== idle: two swaps standing still"
  swap reload "idle 1"; sleep 4; swap reload "idle 2"
  # the double swap's practice area (live2's run 112544: 'in its area' 1 after the first swap, 0 after the second)
  for g in 1 2; do
    a=$(grep -oE "^RELOAD gen $g: this module's init rc=[0-9-]+ .*in its area [0-9] \(the player [a-z ]+, the ambient AI [a-z ]+\)" "$L" |
        grep -oE 'in its area [0-9] \(the player [a-z ]+, the ambient AI [a-z ]+\)')
    case $a in "in its area 1 (the player kept, the ambient AI held off)") pass "gen $g's start: $a" ;; *) fail "gen $g's start: ${a:-no line}" ;; esac
  done
  n0=$(cnt '^PRACTICE t=.* spawn Infantry'); w "practice spawn infantry" 3
  grep -E '^PRACTICE t=.* spawn Infantry' "$L" | tail -n +"$((n0 + 1))" | head -1 | grep -q ' ok' &&
    pass "a practice spawn after the double swap: $(grep -E '^PRACTICE t=.* spawn Infantry' "$L" | tail -1 | grep -oE 'ok: [^;]*' | cut -c1-60)" ||
    fail "the practice spawn after the double swap: $(grep -E '^PRACTICE t=.* spawn Infantry' "$L" | tail -1 | cut -c1-140)"
  w "practice remove" 3
fi

if has turn; then
  say "== turn: snap and smooth turns, a swap right after each"
  w "turn+" 0.6; w "turn+" 0.6; w "turn-" 1.5; swap reload "after 3 snap words"
  w "stick 0 0 1 0.6" 1.6; swap reload "after the right stick's snap"
  swap "reload SOMVR_TURN=smooth" "SOMVR_TURN=smooth"
  w "stick 0 0 1 2" 2.4; swap reload "after a smooth turn right (2 s)"
  w "stick 0 0 -0.6 1.5" 1.8; swap reload "after a smooth turn left"
  w "stick 0 0.8 0.5 2" 2.4; swap reload "after walking and turning"
  swap "reload SOMVR_TURN=snap" "SOMVR_TURN=snap"
  grep -E '^CONFIG input' "$L" | tail -1 | grep -qi 'snap' && pass "SOMVR_TURN back to snap" || say "  (the CONFIG input line: $(grep -E '^CONFIG input' "$L" | tail -1 | cut -c1-120))"
fi

if has sword; then
  say "== sword: sword31, a swap with the sword drawn, its air swings again"
  w "practice remove" 2
  run_driver sword31 warm "S31_STEPS=air" # (the boot's first swings: the sword drawn by them)
  run_driver sword31 pre "S31_STEPS=air two cut weak stab tier exec"
  w "practice remove" 3; sleep 3 # (a calm moment: the swap waits while an orc attacks - boot 123222)
  swap reload "after sword31 (the sword drawn)"
  run_driver sword31 post "S31_STEPS=air"
  for k in flick committed slow; do
    a=$(grep -m1 "AIR $k" "$R/hr37-sword31-pre.txt" | grep -oE 'the sword up to [0-9.]+ deg' | grep -oE '[0-9.]+')
    b=$(grep -m1 "AIR $k" "$R/hr37-sword31-post.txt" | grep -oE 'the sword up to [0-9.]+ deg' | grep -oE '[0-9.]+')
    say "  AIR $k: the sword off the hand ${a:-?} deg (the first run) / ${b:-?} deg (after the reload)"
    [ -n "$a" ] && [ -n "$b" ] && awk -v a="$a" -v b="$b" 'BEGIN { exit !((a - b < 1.5 && b - a < 1.5) || (b / a > 0.85 && b / a < 1.15)) }' &&
      pass "the weighted sword's $k after a reload as before it ($a / $b deg)" || fail "the weighted sword's $k: $a / $b deg"
  done
  say "  the air swings' guard (a blow coming): $(grep -cE 'SWORDPHYS.*swing.*a blow coming' "$L") lines in the run"
fi

if has fling; then
  say "== fling: fling31"
  run_driver fling31 run "PLAN_B31=wall swordlow"
  sleep 3; swap reload "after fling31"
fi

if has bow; then
  say "== bow: bow33"
  run_driver bow33 run ""
  sleep 3; swap reload "after bow33"
fi

if has kill; then
  say "== kill: deaths36 (kills by the hand)"
  w "practice go" 5 # (bow33 ends with a trip back)
  run_driver deaths36 run "DS_STEPS=cuts neck stealth"
  sleep 3; swap reload "after deaths36"
fi

if has knife; then
  say "== knife: polish36's knife"
  run_driver polish36 run "P36_STEPS=knife"
  sleep 3; swap reload "after the knife"
fi

SKIPGEN=-1
if has lost; then
  say "== lost: the check's own proof - two parts not taken"
  w "practice remove" 3; sleep 4
  grep -E '^RELOAD gen [0-9]+ took' "$L" | tail -1 | grep -oE '\(the body moments.*' | cut -c1-120 | sed 's/^/  the last take: /' | tee -a "$O"
  w0=$(cnt 'WARN RELOAD view moved')
  swap "reload SOMVR_TEST_VK_SKIP=bodymoments,anchor" "bodymoments and anchor not taken"; SKIPGEN=$GEN
  sleep 1
  if [ "$(cnt 'WARN RELOAD view moved')" -gt "$w0" ]; then
    pass "the check saw the lost state: $(grep 'WARN RELOAD view moved' "$L" | tail -1 | grep -oE 'swap: [^(]*' | cut -c1-80)"
  else
    yo=$(grep -E "^RELOAD gen $SKIPGEN took" "$L" | tail -1); say "  no WARN: $(echo "$yo" | cut -c1-200)"
    off=$(grep -E '^RELOAD gen [0-9]+ took' "$L" | tail -2 | head -1 | grep -oE "yaw offset -?[0-9.]+" | grep -oE -- '-?[0-9.]+$')
    if [ -n "$off" ] && awk -v o="$off" 'BEGIN { exit !(o < 2 && o > -2) }'; then say "  inconclusive: the yaw offset was only $off deg"
    elif echo "$RLINE" | grep -q refused; then fail "the lost-state swap was refused (busy): $(echo "$RLINE" | grep -oE 'refused[^-]*' | cut -c1-100)"
    else fail "the check did not see the parts not taken (the yaw offset $off deg)"; fi
  fi
  swap "reload SOMVR_TEST_VK_SKIP=" "SOMVR_TEST_VK_SKIP cleared"
fi

if has menu; then
  say "== menu: the VR menu open on its Test tab, a swap"
  PALM="hand L head 5 -16 32 0 -90 50"; PALM_AWAY="hand L head 5 -16 32 0 -90 -130"
  w "hands test R off" 0.3
  o=$(cnt 'vrmenu: opened by' "$X"); TS=0.3 x "$PALM s 0.3"; sleep 2
  [ "$(cnt 'vrmenu: opened by' "$X")" -gt "$o" ] && pass "the menu opened by the palm" || fail "the menu did not open"
  press() { x "hand R touch panel $1 $2 5"; x "hand R touch panel $1 $2 1.0 s 0.3"; x "hand R touch panel $1 $2 -1.0 s 0.15"
            x "hand R touch panel $1 $2 4 s 0.2"; sleep 0.4; }
  nx=$(wc -l < "$X"); press 0.90 0.042
  tail -n +"$((nx + 1))" "$X" | grep -E 'vrmenu: click' | head -2 | cut -c1-140 | sed 's/^/  /' | tee -a "$O"
  swap reload "the menu open on the Test tab" && pass "a swap with the menu open"
  tail -n +"$((nx + 1))" "$X" | grep -qE 'vrmenu: closed' && say "  (the menu closed by itself during the swap)" || pass "the menu stayed open across the swap"
  x "hand R off" "$PALM_AWAY"; sleep 1; x "hand L off"; sleep 1.5
  swap reload "the menu closed"
fi

if has busy; then
  say "== busy: swaps asked during drivers - each waits for quiet or is refused, never half-applied"
  for d in "fling31|PLAN_B31=wall swordlow" "deaths36|DS_STEPS=quick" "bow33|B33_STEPS=aim take"; do
    n=${d%%|*} e=${d#*|}
    w "practice go" 5
    run_driver "$n" bg "$e" bg
    for k in 1 2 3 4; do sleep 9; kill -0 "$DPID" 2>/dev/null || break; swap reload "during $n ($k)"; done
    bg_end
    say "  $n (in the background): $(grep -c 'PASS' "$R/hr37-$n-bg.txt") PASS, $(grep -c 'FAIL' "$R/hr37-$n-bg.txt") FAIL"
    sleep 3
  done
fi

if has ride; then
  say "== ride: ride26 - swaps asked while riding"
  run_driver ride26 bg "" bg
  m=0
  for k in $(seq 1 16); do sleep 8; kill -0 "$DPID" 2>/dev/null || break
    grep -qE '^RIDE t=.*(mounted|on the beast|seated:)' "$L" && m=1; swap reload "during ride26 ($k)"; done
  bg_end
  say "  ride26: $(grep -E 'mounts|leaps|seated' "$R/hr37-ride26-bg.txt" | grep -v '^CONFIG' | tail -2 | cut -c1-160 | tr '\n' ' ')"
  [ $m = 1 ] && say "  mounted: $(grep -m1 -E '^RIDE t=.*(mounted|on the beast|seated:)' "$L" | cut -c1-140)" || say "  NOTE: no mount reached on Windows in ride26 (riding not driven after a swap)"
  grep -E '^WIN .*RELOAD gen [0-9]+ refused.*riding' "$L" | wc -l | sed 's/^/  swaps refused "riding": /' | tee -a "$O"
  w "practice remove" 3; sleep 5; swap reload "after ride26 (the caragor removed)"
fi

say "== summary"
ok=$(cnt '^WIN .*RELOAD gen [0-9]+ ok'); rf=$(cnt '^WIN .*RELOAD gen [0-9]+ refused')
say "swaps: $ok ok, $rf refused; called off: $(cnt 'RELOAD gen [0-9]+ refused.*called off'); view moved: $(cnt 'WARN RELOAD view moved')"
grep -E '^WIN .*RELOAD gen [0-9]+ refused' "$L" | grep -oE 'refused[^-]*- [^;]*|not quiet for [0-9.]+ s \([^)]*\)' | sort | uniq -c | sed 's/^/  /' | tee -a "$O"
grep -E '^WIN .*RELOAD gen [0-9]+ ok' "$L" | grep -oE 'quiet [0-9]+ ms \([0-9]+ polls: [^)]*\)' | sed 's/^/  waited: /' | tee -a "$O"
python3 -I - "$L" <<'EOF' | tee -a "$O"
import re, sys
cm, dg, ccm, cdg, tcm, tdg, n = [], [], [], [], [], [], 0
for l in open(sys.argv[1], errors='replace'):
    if not re.match(r'WIN .*RELOAD gen \d+ ok', l):
        continue
    m = re.search(r'moved ([\d.]+) cm and turned ([\d.]+) deg.*?without a swap just before: ([\d.]+) cm, ([\d.]+) deg\); Talion ([\d.]+) cm, ([\d.]+) deg', l)
    if not m:
        continue
    n += 1
    for a, v in zip((cm, dg, ccm, cdg, tcm, tdg), m.groups()):
        a.append(float(v))
if n:
    print('the view across %d measured swaps: at most %.2f cm, %.3f deg (the same measure before them: %.2f cm, %.3f deg); '
          'Talion at most %.2f cm, %.3f deg' % (n, max(cm), max(dg), max(ccm), max(cdg), max(tcm), max(tdg)))
else:
    print('no measured swap')
EOF
vm=$(grep 'WARN RELOAD view moved' "$L" | grep -vc "at gen $SKIPGEN's swap")
[ "$vm" = 0 ] && pass "no swap moved the view past 1 deg / 2 cm (the test's own swap gen $SKIPGEN aside)" || fail "$vm swap(s) moved the view: $(grep 'WARN RELOAD view moved' "$L" | grep -v "at gen $SKIPGEN's swap" | head -3 | cut -c1-200)"
bm=$(grep 'WARN RELOAD body moved' "$L" | grep -vc "at gen $SKIPGEN's swap")
[ "$bm" = 0 ] && pass "no swap moved or turned Talion past 1 deg / 2 cm" || fail "$bm swap(s) moved Talion: $(grep 'WARN RELOAD body moved' "$L" | head -3 | cut -c1-200)"
np=$(cnt 'refused: not in the practice area')
[ "$np" = 0 ] && pass "the practice area kept across every swap (no 'not in the practice area')" || fail "$np practice requests refused 'not in the practice area' after swaps"
grep -oE "^RELOAD gen [0-9]+: this module's init rc=[0-9-]+ .*in its area [0-9] \(the player [a-z ]+, the ambient AI [a-z ]+\)" "$L" |
  grep -oE 'in its area [0-9] \(the player [a-z ]+, the ambient AI [a-z ]+\)' | sort | uniq -c | sed 's/^/  the inits: /' | tee -a "$O"
[ "$ok" -ge 20 ] && pass "$ok swaps" || fail "only $ok swaps"
grep -E "^RELOAD gen [0-9]+ took" "$L" | grep -c 'NOT ' | sed 's/^/  takes with a part NOT taken: /' | tee -a "$O"
grep -E '^WARN|^WIN .*WARN' "$L" | sed -E 's/[0-9]+(\.[0-9]+)?/N/g' | cut -c1-120 | sort | uniq -c | sed 's/^/  /' | tee -a "$O"
say "== $PASS PASS, $FAIL FAIL"
echo quit >> "$R/cmd"
