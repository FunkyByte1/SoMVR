#!/bin/bash
# reload35-drive.sh - b35's hot reload (docs/re-xr-pipeline.md 156): one headless Windows boot of this worktree with
# SOMVR_DEV_RELOAD=1 (the host swaps somvr_logic.dll in the world: the `reload` word, or a new build/win/somvr_logic.dll).
# RL_STEPS = any of, run in the order given (default "drivers code smoke switch": the first driver run on a fresh boot):
#   smoke    `reload` with the same code: the host's RELOAD line (ok, init rc 0, the swap's times, the view across it,
#            the memory), no new WARN, the new module's own lines (a trigger word answered by its writer thread)
#   switch   `reload SOMVR_SWORD_WEIGHT=light` (its CONFIG line), `reload SOMVR_HUD_FX=game` (its tail jump no longer
#            claimed: back to the game's), `reload SOMVR_HUD_FX=hide` (claimed again), `reload SOMVR_SWORD_WEIGHT=heavy`
#   drivers  RL_DRIVERS (default "sword31 polish31 fling31"): each run before a reload and again after it (a copy whose
#            final quit is disarmed; attached to this run): their PASS/FAIL lines compared
#   code     RL_CODE_N (default 20) real code changes: build/win/somvr_reload_note.h rewritten, win/build.sh --logic, the
#            host's swap by the new file's stamp; each: the build's and the swap's seconds, the note in the init's line
# Results: RUN/rl35.txt (also stdout). Quits at the end, at once when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
MOD=$(cd "$HERE/../mod/somvr" && pwd)
WT=${RL_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${RL_STEPS:-drivers code smoke switch}
DRIVERS=${RL_DRIVERS:-sword31 polish31 fling31}
NCODE=${RL_CODE_N:-20}
T0=$(date +%s)
R=${RL_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -12); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
for i in $(seq 1 300); do [ -f "$R/launch-cmd.txt" ] && break; sleep 2; done # RL_RUN: made by the harness a little later
R=$(cd "$R" && pwd -P) || { echo "no run folder $R"; exit 1; }
L=$R/somvr.log
O=$R/rl35.txt
say() { echo "$(date +%T) $*" | tee -a "$O"; }
say "run $R (the tree $WT, steps: $STEPS)"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { say "STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
nwarn() { grep -c '^WARN' "$L"; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
say "arrived; reload: $(grep -m1 -oE 'LOGIC .*reload (on|off)[^;]*' "$L" | cut -c1-200)"
sleep 8
PASS=0 FAIL=0
pass() { PASS=$((PASS + 1)); say "PASS $*"; }
fail() { FAIL=$((FAIL + 1)); say "FAIL $*"; }
GEN=0
# a reload by the word $1: the host's RELOAD line within $2 s (default 40); RLINE = it (cut), RSEC = word -> line
reload_by() { local n0 t0 i; n0=$(cnt '^WIN .*RELOAD gen [0-9]+ (ok|refused)'); t0=$(date +%s.%N); w "$1" 0.1
  for i in $(seq 1 $((${2:-40} * 10))); do userin && bail "the user is in VR"
    [ "$(cnt '^WIN .*RELOAD gen [0-9]+ (ok|refused)')" -gt "$n0" ] && break; sleep 0.1; done
  RSEC=$(awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN { printf "%.1f", b - a }')
  RLINE=$(grep -E '^WIN .*RELOAD gen [0-9]+ (ok|refused)' "$L" | tail -1)
  [ "$(cnt '^WIN .*RELOAD gen [0-9]+ (ok|refused)')" -gt "$n0" ] || { RLINE=""; return 1; }
  GEN=$(echo "$RLINE" | grep -oE 'RELOAD gen [0-9]+' | grep -oE '[0-9]+$')
  case $RLINE in *" ok"*) return 0 ;; esac; return 1; }
# the numbers of a RELOAD line, one short line
rsum() { echo "$RLINE" | grep -oE 'RELOAD gen [0-9]+ [a-z]+|init rc=[0-9-]+|copy\+load [0-9]+ ms|quiet [0-9]+ ms|stop [0-9]+ ms|init [0-9]+ ms|swap [0-9.]+ ms \([0-9]+ threads held [0-9.]+ ms|[0-9]+ slots claimed, [0-9]+ back to the game.s, [0-9]+ kept, [0-9]+ new, [0-9]+ failed|the longest frame over the swap [0-9.]+ ms|before it [0-9.]+ ms|the view across the swap moved [0-9.]+ cm and turned [0-9.]+ deg|no stereo frame on [a-z ]+side|memory \+[0-9.-]+ MB private \([0-9.]+ MB\), \+[0-9.-]+ MB image \([0-9.]+ MB\)' | tr '\n' ';'; echo; }

step_smoke() {
  say "== smoke: reload with the same code"
  w0=$(nwarn); n0=$(wc -l < "$L")
  if reload_by "reload"; then pass "reload gen $GEN in $RSEC s (word -> RELOAD line)"; else fail "reload: ${RLINE:-no RELOAD line in 40 s}"; fi
  rsum | tee -a "$O"
  grep -E "^RELOAD gen $GEN: this module's init" "$L" | tail -1 | cut -c1-300 | tee -a "$O"
  case $RLINE in *"init rc=0"*) pass "the new module's init rc 0" ;; *) fail "the new module's init: $(echo "$RLINE" | grep -oE 'init rc=[0-9-]+')" ;; esac
  case $RLINE in *" 0 failed"*) pass "no slot failed" ;; *) fail "a slot failed" ;; esac
  sleep 2
  w "schema" 1.5 # a word only a writer thread answers: the new module's (the old one's was joined)
  awk -v a="$n0" 'NR > a' "$L" | grep -qE '^TRIGGER cmd=schema' && pass "the new module's writer thread answers words" || fail "no TRIGGER line after the reload"
  awk -v a="$n0" 'NR > a' "$L" | grep -cE '^(RATE|H[0-9]|HANDJIT|COMBAT|SWORDPHYS) ' | sed 's/^/  the new module'"'"'s periodic lines since: /' | tee -a "$O"
  [ "$(nwarn)" = "$w0" ] && pass "no new WARN ($w0 before)" || fail "WARN lines $w0 -> $(nwarn): $(awk -v a="$n0" 'NR > a' "$L" | grep '^WARN' | head -3 | cut -c1-200)"
}

step_switch() {
  say "== switch: a switch change is a reload with the same code"
  reload_by "reload SOMVR_SWORD_WEIGHT=light" && rsum | tee -a "$O"
  grep -E '^CONFIG smooth hands=' "$L" | tail -1 | grep -q 'sword weight=light' && pass "SOMVR_SWORD_WEIGHT=light in the new module's CONFIG" ||
    fail "the CONFIG smooth line: $(grep -E '^CONFIG smooth hands=' "$L" | tail -1 | grep -oE 'sword weight=[a-z]+')"
  reload_by "reload SOMVR_HUD_FX=game" && rsum | tee -a "$O"
  b=$(echo "$RLINE" | grep -oE '[0-9]+ back to the game.s' | grep -oE '^[0-9]+')
  [ "${b:-0}" -ge 1 ] && pass "SOMVR_HUD_FX=game: $b slot(s) back to the game's (the UI FX tail jump not claimed)" || fail "SOMVR_HUD_FX=game: none back to the game's"
  reload_by "reload SOMVR_HUD_FX=hide" && rsum | tee -a "$O"
  b=$(echo "$RLINE" | grep -oE '[0-9]+ back to the game.s' | grep -oE '^[0-9]+')
  [ "${b:-1}" = 0 ] && pass "SOMVR_HUD_FX=hide: claimed again (0 back to the game's)" || fail "SOMVR_HUD_FX=hide: $b back to the game's"
  grep -E '^HOOK UF_' "$L" | tail -2 | cut -c1-200 | tee -a "$O"
  reload_by "reload SOMVR_SWORD_WEIGHT=heavy" && rsum | tee -a "$O"
  grep -E '^CONFIG smooth hands=' "$L" | tail -1 | grep -q 'sword weight=heavy' && pass "SOMVR_SWORD_WEIGHT=heavy back" || fail "SOMVR_SWORD_WEIGHT=heavy not back"
}

# a driver against this run (a copy next to it: its relative paths; its final quit disarmed, its bail's kept)
run_driver() { local name=$1 tag=$2 src=$HERE/$1-drive.sh cp=$HERE/.rl35-$1-drive.sh out=$R/rl35-$1-$2.txt pid k
  [ -f "$src" ] || { fail "no $src"; return 1; }
  sed -E '/bail\(\)/! s#echo quit >> "\$R/cmd"#: reload35: no quit#' "$src" > "$cp"
  case $name in
    sword31) env S31_WT="$WT" S31_STEPS="${RL_S31_STEPS:-air two cut weak stab tier exec force32 down32}" bash "$cp" > "$out" 2>&1 & pid=$! ;; # (not parry: parry18 quits the game)
    polish31) env P31_WT="$WT" P31_RUN="$R" bash "$cp" > "$out" 2>&1 & pid=$! ;;
    fling31) env PIN_WT="$WT" PIN_RUN="$R" PLAN_B31='wall swordlow' bash "$cp" > "$out" 2>&1 & pid=$! ;;
    *) env PIN_RUN="$R" bash "$cp" > "$out" 2>&1 & pid=$! ;;
  esac
  sleep 2; touch "$R" # its run search: a run folder newer than its start
  for k in $(seq 1 900); do kill -0 "$pid" 2>/dev/null || break; userin && { kill "$pid"; bail "the user is in VR"; }; sleep 1; done
  kill -0 "$pid" 2>/dev/null && { kill "$pid"; say "$name ($tag): stopped after 900 s"; }
  rm -f "$cp"
  grep -E 'PASS|FAIL' "$out" | sed -E 's/^[0-9:]+ //' > "$out.verdicts"
  say "$name ($tag): $(grep -c '^PASS' "$out.verdicts") PASS, $(grep -c '^FAIL' "$out.verdicts") FAIL ($(head -1 "$out" | cut -c1-60) .. $(tail -1 "$out" | cut -c1-40))"; }
# the verdicts without their numbers
vkey() { sed -E 's/[0-9]+(\.[0-9]+)?/N/g' "$1" | sort -u; } # (distinct: a driver may print its run folder's verdicts again at its end)
step_drivers() {
  for d in $DRIVERS; do
    say "== driver $d: before a reload, then after it"
    run_driver "$d" pre
    sleep 5 # the driver's last moves settled (the view's move at the swap is measured)
    reload_by "reload" && pass "reload gen $GEN before $d's second run" || fail "reload before $d: $RLINE"
    rsum | tee -a "$O"
    run_driver "$d" post
    if diff <(vkey "$R/rl35-$d-pre.txt.verdicts") <(vkey "$R/rl35-$d-post.txt.verdicts") > "$R/rl35-$d.diff"; then
      pass "$d: the same verdicts after the reload ($(grep -c '^PASS' "$R/rl35-$d-post.txt.verdicts") PASS, $(grep -c '^FAIL' "$R/rl35-$d-post.txt.verdicts") FAIL)"
    else
      fail "$d: other verdicts after the reload: $(head -6 "$R/rl35-$d.diff" | tr '\n' '|' | cut -c1-400)"
    fi
    paste -d'|' "$R/rl35-$d-pre.txt.verdicts" "$R/rl35-$d-post.txt.verdicts" | head -24 | sed 's/^/  pre|post: /' | cut -c1-220 | tee -a "$O"
  done
}

step_code() {
  say "== code: $NCODE real changes (the note in build/win/somvr_reload_note.h), each built and swapped by its file stamp"
  ok=0; tb=0; ts=0
  for k in $(seq 1 "$NCODE"); do
    note="change $k of $NCODE at $(date +%T)"
    n0=$(cnt '^WIN .*RELOAD gen [0-9]+ (ok|refused)')
    printf '#define SOMVR_RELOAD_NOTE "%s"\n' "$note" > "$MOD/build/win/somvr_reload_note.h"
    t0=$(date +%s.%N)
    (cd "$MOD" && win/build.sh --logic > "$R/rl35-build-$k.txt" 2>&1) || { fail "change $k: the build failed ($(tail -1 "$R/rl35-build-$k.txt"))"; continue; }
    t1=$(date +%s.%N)
    for i in $(seq 1 400); do userin && bail "the user is in VR"
      [ "$(cnt '^WIN .*RELOAD gen [0-9]+ (ok|refused)')" -gt "$n0" ] && break; sleep 0.1; done
    t2=$(date +%s.%N)
    RLINE=$(grep -E '^WIN .*RELOAD gen [0-9]+ (ok|refused)' "$L" | tail -1)
    gl=$(grep -F "note '$note'" "$L" | tail -1)
    b=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", b - a }'); s=$(awk -v a="$t1" -v b="$t2" 'BEGIN { printf "%.1f", b - a }')
    if [ -n "$gl" ] && case $RLINE in *" ok"*"init rc=0"*) true ;; *) false ;; esac; then
      ok=$((ok + 1)); tb=$(awk -v a="$tb" -v b="$b" 'BEGIN { print a + b }'); ts=$(awk -v a="$ts" -v b="$s" 'BEGIN { print a + b }')
      say "change $k: built in $b s, swapped $s s later (the note in the log); $(rsum | tr -s ' ' | cut -c1-260)"
    else
      fail "change $k: built in $b s, no RELOAD with its note in 40 s: $(echo "$RLINE" | cut -c1-200)"
    fi
    sleep 3
  done
  [ "$ok" = "$NCODE" ] && pass "$ok of $NCODE code changes live; mean build $(awk -v a="$tb" -v n="$ok" 'BEGIN { printf "%.1f", a / n }') s + swap $(awk -v a="$ts" -v n="$ok" 'BEGIN { printf "%.1f", a / n }') s" ||
    fail "$ok of $NCODE code changes live"
  rm -f "$MOD/build/win/somvr_reload_note.h"
}
for st in $STEPS; do # in the order given
  case $st in smoke|switch|drivers|code) "step_$st" ;; *) say "unknown step $st" ;; esac
done
say "== the RELOAD lines: $(cnt '^WIN .*RELOAD gen [0-9]+ ok') ok, $(cnt '^WIN .*RELOAD gen [0-9]+ refused') refused"
python3 -I - "$L" <<'PY' | tee -a "$O"
import re, sys
mem, held, frame, frame0, swap, view = [], [], [], [], [], []
for l in open(sys.argv[1], errors='replace'):
    if not re.match(r'WIN .*RELOAD gen \d+ ok', l):
        continue
    m = re.search(r'memory \+([\d.-]+) MB private \(([\d.]+) MB\), \+([\d.-]+) MB image', l); m and mem.append((float(m[1]), float(m[2]), float(m[3])))
    m = re.search(r'swap ([\d.]+) ms \((\d+) threads held ([\d.]+) ms', l); m and (swap.append(float(m[1])), held.append(float(m[3])))
    m = re.search(r'the longest frame over the swap ([\d.]+) ms \(\d+ presents; before it ([\d.]+) ms', l)
    m and (frame.append(float(m[1])), frame0.append(float(m[2])))
    m = re.search(r'moved ([\d.]+) cm and turned ([\d.]+) deg.*?without a swap just before: ([\d.]+) cm, ([\d.]+) deg', l)
    m and view.append((float(m[1]), float(m[2]), float(m[3]), float(m[4])))
if mem:
    print('memory: %d reloads, +%.1f MB private (%.1f .. %.1f) and +%.1f MB image per reload on average, %.0f MB private at the end' % (
        len(mem), sum(d for d, _, _ in mem) / len(mem), min(d for d, _, _ in mem), max(d for d, _, _ in mem),
        sum(i for _, _, i in mem) / len(mem), mem[-1][1]))
if held:
    print('the swaps: %.1f ms on average (at most %.1f), the threads held %.2f ms on average (at most %.2f)' % (
        sum(swap) / len(swap), max(swap), sum(held) / len(held), max(held)))
if frame:
    print('the longest frame over a swap: %.1f ms on average, at most %.1f (the second before: %.1f on average)' % (
        sum(frame) / len(frame), max(frame), sum(frame0) / len(frame0)))
if view:
    print('the view across a swap: at most %.1f cm, %.2f deg; over 250 ms just before (no swap): at most %.1f cm, %.2f deg' % (
        max(v[0] for v in view), max(v[1] for v in view), max(v[2] for v in view), max(v[3] for v in view)))
PY
say "WARN lines: $(nwarn); $PASS PASS, $FAIL FAIL"
say "done"; echo quit >> "$R/cmd"
