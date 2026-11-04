#!/bin/bash
# slots35-measure.sh - som-b35-slots' measurement (docs/re-xr-pipeline.md 158.4): the same Windows drivers solo, then 2
# and 3 games at once (scripts/headless_vr.sh --win, HEADLESS_SLOT 1..3), with a sampler (VRAM, GTT, GPU busy, RAM,
# CPU, each game's CPU and RSS every 2 s) and the kernel log watched the whole time: an amdgpu reset / "VRAM is lost" /
# a ring timeout = every game killed at once and the measurement stopped (OUT/STOP). Run it OUTSIDE the sandbox, as the
# holder of every slot (`game_slot.sh take OWNER solo`): it never takes or releases a lock itself.
#   slots35-measure.sh OUT WAVE...      a WAVE = drivers run at once, comma-separated, e.g.  A B C M A,B C,M A,B,C M,C,A
#   gate:A,C = the VR stop's test: when every game of the wave is in the world (+20 s), the file OUT/gate-W appears
#   (GAME_SLOT_GATE_TEST: the harness's VR gate reads it like the user going into VR); OUT/gate-W.txt = when each game
#   was gone
# Drivers (the build-win2 Windows suite's, all PASS there): A polish31 (final.sh 134), B fling31 'wall swordlow' (136),
#   C pcar29 chain (106), M nemesis27 struggle (the Beast Hunt mission's start, 105's first step). The build: SL_BUILD
#   (default build-win2, logs/BUILD frozen: its mod and headset.env). Every boot adds SOMVR_PERF_S=60 SOMVR_XR_PERF_S=60
#   (the frame-time lines; solo and parallel alike). Output: OUT/boot-W-N.txt, drive-W-N.txt, run-W-N.txt, times-W-N.txt,
#   samples.tsv, games.tsv, kernel.log; scripts/slots35_report.py OUT = the table. The raw .ppm/.pgm dumps of each run
#   go after its wave (SL_KEEP_DUMPS=1 keeps them).
set -u
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${SL_BUILD:-build-win2}; B=$SOM/logs/$BUILD; MOD=$B/ShadowOfMordor/mod/somvr; HENV=$B/headset.env
OUT=${1:?usage: slots35-measure.sh OUT WAVE...}; shift
mkdir -p "$OUT" || exit 1
[ -f "$MOD/build/win/xinput9_1_0.dll" ] && [ -f "$HENV" ] || { echo "STOP: $B is not a frozen Windows build"; exit 1; }
for n in 1 2 3; do [ -d "$(bash "$HERE/game_slot.sh" lock $n)" ] || { echo "STOP: hold every slot first (game_slot.sh take OWNER solo)"; exit 1; }; done
ENV="$(grep -E '^SOMVR_[A-Z0-9_]+=' "$HENV" | tr '\n' ' ')"
T="SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_PERF_S=60 SOMVR_XR_PERF_S=60"
say() { echo "[$(date +%T)] $*" | tee -a "$OUT/measure.log"; }
drv() {   # NAME -> H (hold), X (more switches), D (the driver; @R@ = its run)
  case $1 in
    A) H=1200; X=""; D="env P31_WT=$BUILD P31_RUN=@R@ bash $HERE/polish31-drive.sh" ;;
    B) H=1500; X=""; D="env PIN_WT=$BUILD PIN_RUN=@R@ PLAN_B31='wall swordlow' bash $HERE/fling31-drive.sh" ;;
    C) H=1200; X=""; D="env PC_WT=$BUILD PC_RUN=@R@ PC_STEPS=chain PC_SPAWNS=3 bash $HERE/pcar29-drive.sh" ;;
    M) H=1500; X="SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0"
       D="env NM_WT=$BUILD NM_RUN=@R@ NM_STEPS=struggle SCR_DIR=@R@/scr bash $HERE/nemesis27-drive.sh" ;;
    *) return 1 ;;
  esac
}
kill_games() {   # every Windows game at once (the caller holds every slot: they are all ours)
  local p; for p in $(pgrep -f '^[A-Za-z]:\\.*\\ShadowOfMordor\.exe'); do kill -KILL "$p" 2>/dev/null; done
}
# ---- the kernel log and the sampler (the whole measurement) -------------------------------------------------------
journalctl -k -f -n 0 -o short-iso > "$OUT/kernel.log" 2>&1 & KJ=$!
CARD=/sys/class/drm/card1/device
echo -e "epoch\tvram_mib\tgtt_mib\tgpu_busy\tmem_used_mib\tcpu_busy\tgames\tgames_rss_mib" > "$OUT/samples.tsv"
echo -e "epoch\tpid\tcpu_ticks\trss_kib\trun" > "$OUT/games.tsv"
( read -r _ a b c d e f g h _ < /proc/stat; pt=$((a + b + c + d + e + f + g + h)); pi=$((d + e))
  while kill -0 $$ 2>/dev/null; do
    sleep 2
    read -r _ a b c d e f g h _ < /proc/stat; t=$((a + b + c + d + e + f + g + h)); i=$((d + e))
    cpu=$(( (100 * ((t - pt) - (i - pi))) / ((t - pt) > 0 ? (t - pt) : 1) )); pt=$t; pi=$i
    ma=$(awk '/^MemTotal/ {t = $2} /^MemAvailable/ {a = $2} END {print int((t - a) / 1024)}' /proc/meminfo)
    ng=0; rs=0; now=$(date +%s)
    for p in $(pgrep -f '^[A-Za-z]:\\.*\\ShadowOfMordor\.exe'); do
      st=$(cut -d' ' -f14,15 /proc/$p/stat 2>/dev/null) || continue; r=$(awk '/^VmRSS/ {print $2}' /proc/$p/status 2>/dev/null)
      run=$(tr '\0' '\n' < /proc/$p/environ 2>/dev/null | sed -n 's/^SOMVR_LOG=\(.*\)\/somvr\.log$/\1/p')
      ng=$((ng + 1)); rs=$((rs + ${r:-0}))
      echo -e "$now\t$p\t$(( ${st% *} + ${st#* } ))\t${r:-0}\t${run##*/}" >> "$OUT/games.tsv"
    done
    echo -e "$now\t$(( $(cat $CARD/mem_info_vram_used) / 1048576 ))\t$(( $(cat $CARD/mem_info_gtt_used) / 1048576 ))\t$(cat $CARD/gpu_busy_percent)\t$ma\t$cpu\t$ng\t$((rs / 1024))" >> "$OUT/samples.tsv"
    if grep -q -i -E 'amdgpu.*(GPU reset|VRAM is lost|ring .* timeout|GPU recovery|GPU hang)|VRAM is lost' "$OUT/kernel.log"; then
      kill_games; echo "GPU: $(grep -i -m1 -E 'reset|VRAM is lost|timeout|hang' "$OUT/kernel.log")" > "$OUT/STOP"
      echo "[$(date +%T)] !!! GPU trouble in the kernel log: every game killed ($(cat "$OUT/STOP"))" >> "$OUT/measure.log"; exit 0
    fi
  done ) & SMP=$!
trap 'kill $SMP $KJ 2>/dev/null' EXIT
one() {   # NAME SLOT TAG: one boot and its driver
  local n=$1 s=$2 tag=$3 bp dp R="" i k t0
  drv "$n" || { say "unknown driver $n"; return; }
  [ -n "${GAME_SLOT_GATE_TEST:-}" ] && export GAME_SLOT_GATE_TEST   # (the gate wave: the harness reads it)
  t0=$(date +%s.%N)
  HEADLESS_SLOT=$s HEADLESS_HOLD=$H SOMVR_EXTRA="$ENV $X $T" setsid -w "$HERE/headless_vr.sh" --win "$MOD" > "$OUT/boot-$tag.txt" 2>&1 < /dev/null &
  bp=$!
  for i in $(seq 1 120); do R=$(sed -n 's/^windows: \([^ ]*\) .*/\1/p' "$OUT/boot-$tag.txt"); [ -n "$R" ] && break; kill -0 $bp 2>/dev/null || break; sleep 0.5; done
  [ -n "$R" ] || { say "$tag: no run ($(tail -2 "$OUT/boot-$tag.txt" | tr '\n' ' '))"; wait $bp; return; }
  echo "$R" > "$OUT/run-$tag.txt"; say "$tag: driver $n in slot $s, run ${R##*/}"
  bash -c "${D//@R@/$R}" > "$OUT/drive-$tag.txt" 2>&1 < /dev/null &
  dp=$!
  ( for i in $(seq 1 600); do LC_ALL=C grep -a -q '^PRACTICE t=.* arrived' "$R/somvr.log" 2>/dev/null && { date +%s.%N > "$OUT/arrive-$tag.txt"; break; }
      kill -0 $bp 2>/dev/null || break; sleep 0.5; done ) &
  wait $bp
  for k in $(seq 1 60); do kill -0 $dp 2>/dev/null || break; sleep 1; done
  kill -0 $dp 2>/dev/null && { say "$tag: the driver still ran 60 s after the boot: stopped"; pkill -TERM -P $dp; kill $dp 2>/dev/null; }
  wait $dp 2>/dev/null
  printf '%s %s %s\n' "$t0" "$(cat "$OUT/arrive-$tag.txt" 2>/dev/null || echo -)" "$(date +%s.%N)" > "$OUT/times-$tag.txt"
  say "$tag: done - $(grep -c PASS "$OUT/drive-$tag.txt") PASS, $(grep -c FAIL "$OUT/drive-$tag.txt") FAIL; $(grep -h -E 'save after|stopped at once|exited by itself|crash' "$R/summary.txt" 2>/dev/null | cut -c12-90 | tr '\n' ' ')"
}
w=0
for wave in "$@"; do
  w=$((w + 1))
  [ -e "$OUT/STOP" ] && { say "STOP: $(cat "$OUT/STOP")"; break; }
  g=$(bash "$HERE/game_slot.sh" gate) || { say "STOP: the gate is closed: $g"; echo "gate: $g" > "$OUT/STOP"; break; }
  GT=""; case $wave in gate:*) wave=${wave#gate:}; GT=$OUT/gate-$w; rm -f "$GT" ;; esac
  say "== wave $w: $wave${GT:+ (the VR stop test)}"; echo "$(date +%s) $w ${GT:+gate:}$wave" >> "$OUT/waves.txt"
  s=0; ps_=(); for n in ${wave//,/ }; do s=$((s + 1)); GAME_SLOT_GATE_TEST=$GT one "$n" "$s" "$w-$n" & ps_+=($!); sleep 2; done
  if [ -n "$GT" ]; then   # all in the world + 20 s: the gate file; then every 0.2 s which of the wave's games still runs
    nw=$s
    for i in $(seq 1 1200); do [ "$(ls "$OUT"/arrive-$w-*.txt 2>/dev/null | wc -l)" -ge "$nw" ] && break; sleep 0.5; done
    sleep 20; runs=$(cat "$OUT"/run-$w-*.txt | xargs -n1 basename | tr '\n' ' ')
    t0=$(date +%s.%N); touch "$GT"; say "gate test: $GT made (runs $runs)"; echo "made $t0" > "$GT.txt"
    left=$runs
    for i in $(seq 1 600); do
      now=""; for p in $(pgrep -f '^[A-Za-z]:\\.*\\ShadowOfMordor\.exe'); do now+=" $(tr '\0' '\n' < /proc/$p/environ 2>/dev/null | sed -n 's/^SOMVR_LOG=.*\/\([^/]*\)\/somvr\.log$/\1/p')"; done
      for r in $left; do case " $now " in *" $r "*) ;; *) echo "$r gone $(date +%s.%N)" >> "$GT.txt"; left=$(echo " $left " | sed "s/ $r / /") ;; esac; done
      [ -z "${left// /}" ] && break; sleep 0.2
    done
    say "gate test: $(awk -v t=$t0 '$2 == "gone" {printf "%s %.1f s; ", $1, $3 - t}' "$GT.txt")${left// /}"
  fi
  wait "${ps_[@]}"
  echo "$(date +%s) $w end" >> "$OUT/waves.txt"
  for f in "$OUT"/run-$w-*.txt; do
    R=$(cat "$f"); [ "${SL_KEEP_DUMPS:-0}" = 1 ] || find "$R" -maxdepth 3 -type f \( -name '*.ppm' -o -name '*.pgm' \) -delete 2>/dev/null
  done
  sleep 5
done
say "all done"
