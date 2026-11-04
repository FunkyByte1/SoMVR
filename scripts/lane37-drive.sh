#!/bin/bash
# lane37-drive.sh - the lane's A/B (som-b37-lane2, docs/re-xr-pipeline.md 163.8): can the live loop's CPU jobs run beside
# the user's VR session without costing it frames? A headless Windows game at the VR preset paced at 90 Hz by Monado's
# real compositor (headless_win.sh WIN_MONADO_RATE=90; the null compositor holds a game at 20 Hz, and
# U_PACING_APP_IMMEDIATE_WAIT_FRAME_RETURN=1 broke the pacing: ~30 fps, boot 20261104-085718), held in the practice meadow; a stand-in for WiVRn's CPU side beside it (lane/lane_wivrn.c: 90 Hz, 100 Mbit/s of
# loopback UDP, ~30 % of a core); the windows idle / busy in the order LANE_ORDER (default I1 B1 B2 I2 I3 B3: ABBA, a
# slow drift cancels), LANE_WS seconds each (600), a 10 s gap after each (jobs stopped, not counted). Busy = the real job
# mix in the lane, both at once for the whole window: `voice` (STT base.en + small.en of a 15 s note, a Kokoro line,
# back to back) and `builds` (the hot reload's logic DLL, then the full check pair, each after a real one-line change);
# the closed loop OFF (the jobs' own cost; the pins on as in a session). Then the knobs, LANE_TAIL (default
# L1 I4 Q1 V1 I5 U1 N1 I6), LANE_XS seconds each (180): L = the mix with the closed loop ON, Q = the mix at one CPU's
# quota (100 %), V = voice only, U = builds only, N = the mix with the game not pinned, O = the mix the old way (nice 19,
# every CPU, no lane), I = idle. The voice knobs (b37 boot 2): T = voice at 2 threads (the live loop's STT default),
# W = a note every 20 s (base.en + a Kokoro line, 2 threads), X = W at 1 thread, P = T at one CPU's quota, M = W + the builds (a live
# session: notes and a worker's builds).
# LANE_WARM (90 s): a settle window before the first. Boot (solo: it measures frame times):
#   WIN_MONADO_RATE=90 HEADLESS_HOLD=5700 (the default windows: ~5300 s) SOMVR_EXTRA="$(grep -E
#   '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ') SOMVR_PERF_RAW=@RUN@/perf-raw.txt SOMVR_HANDS_TEST=1
#   SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh --win TREE/ShadowOfMordor/mod/somvr
# with this driver beside it (BG_RUN=RUN pins it to one run). Needs scripts/lane_install.sh install and lane_loads.py
# prepare. Then: python3 scripts/lane_ab.py RUN (the table). No sound plays (the loads only compute samples).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${BG_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
RTD=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
WS=${LANE_WS:-600}; XS=${LANE_XS:-180}; TAIL=${LANE_TAIL-L1 I4 Q1 V1 I5 U1 N1 I6}; TAIL=${TAIL//_/ }
ORDER=${LANE_ORDER:-I1 B1 B2 I2 I3 B3}; ORDER=${ORDER//_/ }
WORK=/tmp/somvr-lane35
L=$HERE/lane_loads.py
T0=$(date +%s)
R=${BG_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
bash "$(dirname "$0")/render_need.sh" "$R" lane37-drive.sh || exit 1   # b37: a Windows run at WIN_RENDER=vr (re-xr-pipeline.md 158.5)
echo "run $R"
PROT=$RTD/somvr-lane/protect.d/lane37-$(basename "$R").conf
JOBS=(); BG=()
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' "$RTD/vr-session/status.json" 2>/dev/null && return 0
  return 1
}
# our own jobs only: a lane job's whole scope (its build's compilers too) by its cgroup.kill, an old-way job's process
# group (setsid: its compilers too)
stop_jobs() {
  local p cg
  for p in "${JOBS[@]}"; do
    cg=$(cut -d: -f3 "/proc/$p/cgroup" 2>/dev/null)
    case $cg in */somvr-lane.slice/lane-*.scope) echo 1 > "/sys/fs/cgroup$cg/cgroup.kill" 2>/dev/null ;;
      *) kill -TERM -- "-$p" 2>/dev/null ;; esac
  done
  for p in "${JOBS[@]}"; do wait "$p" 2>/dev/null; done
  JOBS=()
}
finish() { stop_jobs; for p in "${BG[@]}"; do kill "$p" 2>/dev/null; done; rm -f "$PROT"; }
trap finish EXIT
bail() { echo "$(date +%T) STOP: $1"; stop_jobs; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
[ -s "$R/perf-raw.txt" ] || bail "no $R/perf-raw.txt (SOMVR_PERF_RAW=@RUN@/perf-raw.txt and SOMVR_PERF=on)"
sleep 8
# a window's mark: the word into both sides' perf windows; waits (up to 20 s) for its line in perf-raw.txt (the Windows
# trigger file is read every few seconds)
mark() {
  userin && bail "the user is in VR"
  printf '%s' "perf $1" > "$R/somvr.trigger"; printf '%s\n' "perf $1" > "$R/xrtest.txt"
  local i; for i in $(seq 1 40); do grep -q "^# perf $1 " "$R/perf-raw.txt" && break; sleep 0.5; done
  grep -q "^# perf $1 " "$R/perf-raw.txt" || bail "the mark $1 never reached perf-raw.txt"
  echo "$(date +%T) $(date +%s.%N) window $1" | tee -a "$R/lane-steps.txt"
}
hold() { local e=$(( $(date +%s) + $1 )); while [ "$(date +%s)" -lt $e ]; do userin && bail "the user is in VR"; sleep 1; done; }
protect() {   # on|off|nopin: lane_watch.py's protection of this run (the closed loop on/off; its processes pinned to
              # GAME_CPUS while jobs run, except nopin: the loop off, no pins)
  mkdir -p "$(dirname "$PROT")"
  { echo "raw=$R/perf-raw.txt"; [ "$1" = nopin ] && echo "loop=off" || { echo "loop=$1"; echo "pin=$R"; }; } > "$PROT.tmp" &&
    mv -f "$PROT.tmp" "$PROT"
  echo "$(date +%T) protect: $1"
}
lane() { local n=$1; shift; bash "$HERE/lane.sh" run "$n" -- "$@" > "$R/lane-$n.log" 2>&1 & JOBS+=($!); echo "$(date +%T) lane job $n pid $!"; }
old() { local n=$1; shift; setsid nice -n 19 "$@" > "$R/lane-$n.log" 2>&1 & JOBS+=($!); echo "$(date +%T) old-way job $n pid $!"; }
gap() { stop_jobs; mark "gap-$1"; hold 10; }
NW=0; for w in $ORDER; do NW=$((NW + 1)); done
NT=0; for w in $TAIL; do NT=$((NT + 1)); done
TOTAL=$(( NW * (WS + 25) + NT * (XS + 25) + ${LANE_WARM:-90} + 120 ))
# the pressure log and the WiVRn stand-in (its environment names the run: the watcher pins it with the game)
python3 -I "$HERE/lane_sample.py" "$R" "$TOTAL" & BG+=($!)
LANE_STANDIN_RUN=$R/ "$WORK/lane_wivrn" "$R/wivrn-standin.txt" "$TOTAL" 90 100 2.5 & BG+=($!)
echo "$(date +%T) sampler and WiVRn stand-in (${BG[*]}), up to $TOTAL s"
python3 -I "$HERE/lane_watch.py" placement "$R" 6 > "$R/lane-placement.txt" 2>&1; head -8 "$R/lane-placement.txt" | cut -c1-200
systemctl --user is-active -q somvr-lane-watch || bash "$HERE/lane_run.sh" true
protect off; sleep 3
# the lane's CPU limit for the quota100 window: its cgroup's cpu.max written directly, the slice's own put back after
LCG=/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/somvr.slice/somvr-lane.slice
QBACK=""
quota() {   # quota PERCENT|back (100 = one CPU's time: the quota = the period)
  if [ "$1" = back ]; then [ -n "$QBACK" ] && echo "$QBACK" > "$LCG/cpu.max" && echo "$(date +%T) cpu.max back: $QBACK"; QBACK=""
  else QBACK=$(cat "$LCG/cpu.max"); local per=${QBACK#* }
    echo "$(( per * $1 / 100 )) $per" > "$LCG/cpu.max" && echo "$(date +%T) cpu.max $(cat "$LCG/cpu.max") (was $QBACK)"; fi
}
finish2() { quota back; finish; }
trap finish2 EXIT
# one window: TOKEN SECONDS. I<n> idle; B<n> the mix in the lane (the closed loop off); L<n> the mix, the loop on;
# Q<n> the mix at one CPU's quota (100 %); V<n> voice only; U<n> builds only; N<n> the mix, the game not pinned;
# O<n> the mix the old way (nice 19, every CPU, 6 STT threads, the build at every CPU)
window() {
  local w=$1 s=$2 k=${1:0:1} n=${1:1}
  case $k in
    I) mark "idle-$n"; hold "$s"; mark "gap-$w"; hold 10; return ;;
    B) mark "busy-$n" ;;
    L) protect on; sleep 2; mark "loop-$n" ;;
    Q) mark "quota100-$n" ;;
    V) mark "voice-$n" ;;
    U) mark "builds-$n" ;;
    N) protect nopin; sleep 2; mark "nopin-$n" ;;
    O) mark "old-$n"
       old "old-voice-$n" python3 -I "$L" voice --threads 6 --seconds "$((s + 60))"
       old "old-builds-$n" python3 -I "$L" builds --seconds "$((s + 60))"
       hold "$s"; gap "$w"; return ;;
    T) mark "voice2t-$n"; lane "voice-$w" python3 -I "$L" voice --threads 2 --seconds "$((s + 60))"; hold "$s"; gap "$w"; return ;;
    W) mark "notes2t-$n"; lane "voice-$w" python3 -I "$L" voice --threads 2 --models base.en --every 20 --seconds "$((s + 60))"
       hold "$s"; gap "$w"; return ;;
    X) mark "notes1t-$n"; lane "voice-$w" python3 -I "$L" voice --threads 1 --models base.en --every 20 --seconds "$((s + 60))"
       hold "$s"; gap "$w"; return ;;
    P) mark "voice2t-q100-$n"; lane "voice-$w" python3 -I "$L" voice --threads 2 --seconds "$((s + 60))"
       sleep 2; quota 100; hold "$s"; gap "$w"; quota back; return ;;
    M) mark "live-$n"; lane "voice-$w" python3 -I "$L" voice --threads 2 --models base.en --every 20 --seconds "$((s + 60))"
       lane "builds-$w" python3 -I "$L" builds --seconds "$((s + 60))"; hold "$s"; gap "$w"; return ;;
    *) echo "unknown window $w"; return ;;
  esac
  [ "$k" = U ] || lane "voice-$w" python3 -I "$L" voice --seconds "$((s + 60))"
  [ "$k" = V ] || lane "builds-$w" python3 -I "$L" builds --seconds "$((s + 60))"
  [ "$k" = Q ] && { sleep 2; quota 100; }   # (after the jobs' scopes: systemd writes the slice's cpu.max as it starts them)
  hold "$s"
  [ "$k" = Q ] && echo "$(date +%T) cpu.max at the end: $(cat "$LCG/cpu.max")"
  gap "$w"
  [ "$k" = Q ] && quota back
  case $k in L|N) protect off ;; esac
}
# a settle window first (not in the rule): the meadow's streaming and the first lane start-up done
[ "${LANE_WARM:-90}" -gt 0 ] && { mark warm; hold "${LANE_WARM:-90}"; }
for w in $ORDER; do window "$w" "$WS"; done
for w in $TAIL; do window "$w" "$XS"; done
mark end
rm -f "$PROT"
grep -E 'FREEZE|THAW|PIN|UNPIN|PROTECT|AFFINITY' "$SOM/logs/lane/lane-$(date +%Y%m%d).log" 2>/dev/null | tail -60 > "$R/lane-events.txt"
echo "$(date +%T) done: $(wc -l < "$R/perf-raw.txt") raw lines; lane events in $R/lane-events.txt"
echo quit >> "$R/cmd"
echo PASS
