#!/bin/bash
# slots37-measure.sh - som-b37-slotsvram's measurement (docs/re-xr-pipeline.md 158.5): the render modes' VRAM and GTT
# with 1-3 headless Windows games at once (scripts/headless_vr.sh --win, HEADLESS_SLOT 1..3, WIN_RENDER per game) and a
# behaviour driver's verdicts at light against vr. Run it OUTSIDE the sandbox as the holder of every slot
# (`game_slot.sh take OWNER solo`): it never takes or releases a lock itself.
#   slots37-measure.sh OUT WAVE...   a WAVE = games at once, comma-separated, each NAME:MODE[:SET] in slot order (the
#                                    first game slot 1 ...): MODE vr|light; SET = a light set to try instead of
#                                    game_slot.sh's GS_RENDER_LIGHT (WIN_RENDER_LIGHT; '+' for its commas), e.g.
#                                    P:vr  P:vr,P:light,P:light  S:light:TextureQuality:0+ShadowQuality:1
# Drivers: P polish36 (the suite's 363: the bow and A, the knife at either hip, wraith hands, the draw sound, the
#   listener), F fling31 'wall swordlow' (136), S nothing (the meadow for SL_SIT_S s, default 240: memory only).
#   The build: SL_BUILD (default build-win3, logs/BUILD frozen: its mod and headset.env); the scripts: this worktree's.
# Every 2 s: the card's VRAM / GTT / busy (OUT/samples.tsv) and each game's own memory from /proc/PID/fdinfo (OUT/games.tsv:
# resident and asked VRAM and GTT, MiB); the kernel log watched (an amdgpu reset / VRAM lost / ring timeout = every game
# killed, OUT/STOP). scripts/slots37_report.py OUT = the table. The raw .ppm/.pgm dumps go after each wave.
set -u
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${SL_BUILD:-build-win3}; B=$SOM/logs/$BUILD; MOD=$B/ShadowOfMordor/mod/somvr; HENV=$B/headset.env
OUT=${1:?usage: slots37-measure.sh OUT WAVE...}; shift
mkdir -p "$OUT" || exit 1
[ -f "$MOD/build/win/xinput9_1_0.dll" ] && [ -f "$HENV" ] || { echo "STOP: $B is not a frozen Windows build"; exit 1; }
for n in 1 2 3; do [ -d "$(bash "$HERE/game_slot.sh" lock $n)" ] || { echo "STOP: hold every slot first (game_slot.sh take OWNER solo)"; exit 1; }; done
. "$HERE/game_slot.sh"   # (gs_pid_mem, gs_card)
ENV="$(grep -E '^SOMVR_[A-Z0-9_]+=' "$HENV" | tr '\n' ' ')"
T="SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
say() { echo "[$(date +%T)] $*" | tee -a "$OUT/measure.log"; }
drv() {   # NAME -> H (hold), D (the driver; @R@ = its run; empty = none)
  case $1 in
    P) H=1200; D="env P36_WT=$BUILD P36_RUN=@R@ bash $HERE/polish36-drive.sh" ;;
    F) H=1500; D="env PIN_WT=$BUILD PIN_RUN=@R@ PLAN_B31='wall swordlow' bash $HERE/fling31-drive.sh" ;;
    S) H=$(( ${SL_SIT_S:-240} + 60 )); D="" ;;
    *) return 1 ;;
  esac
}
EXE='^[A-Za-z]:\\.*\\ShadowOfMordor\.exe'
kill_games() { local p; for p in $(pgrep -f "$EXE"); do kill -KILL "$p" 2>/dev/null; done; }   # (every slot is ours)
journalctl -k -f -n 0 -o short-iso > "$OUT/kernel.log" 2>&1 & KJ=$!
CARD=$(gs_card) || { echo "STOP: no card information"; exit 1; }
[ -f "$OUT/samples.tsv" ] || echo -e "epoch\tvram_mib\tgtt_mib\tgpu_busy\tgames" > "$OUT/samples.tsv"
[ -f "$OUT/games.tsv" ] || echo -e "epoch\tpid\trun\trv_mib\ttv_mib\trg_mib\ttg_mib" > "$OUT/games.tsv"
( while kill -0 $$ 2>/dev/null; do
    sleep 2; now=$(date +%s); ng=0
    for p in $(pgrep -f "$EXE"); do
      run=$(tr '\0' '\n' < /proc/$p/environ 2>/dev/null | sed -n 's/^SOMVR_LOG=\(.*\)\/somvr\.log$/\1/p')
      [ -n "$run" ] || continue; ng=$((ng + 1))
      echo -e "$now\t$p\t${run##*/}\t$(gs_pid_mem "$p" | tr ' ' '\t')" >> "$OUT/games.tsv"
    done
    echo -e "$now\t$(gs_mib $CARD/mem_info_vram_used)\t$(gs_mib $CARD/mem_info_gtt_used)\t$(cat $CARD/gpu_busy_percent)\t$ng" >> "$OUT/samples.tsv"
    if grep -q -i -E 'amdgpu.*(GPU reset|VRAM is lost|ring .* timeout|GPU recovery|GPU hang)|VRAM is lost' "$OUT/kernel.log"; then
      kill_games; echo "GPU: $(grep -i -m1 -E 'reset|VRAM is lost|timeout|hang' "$OUT/kernel.log")" > "$OUT/STOP"
      echo "[$(date +%T)] !!! GPU trouble in the kernel log: every game killed ($(cat "$OUT/STOP"))" >> "$OUT/measure.log"; exit 0
    fi
  done ) & SMP=$!
trap 'kill $SMP $KJ 2>/dev/null' EXIT
one() {   # NAME:MODE[:SET] SLOT TAG: one boot and its driver
  local spec=$1 s=$2 tag=$3 n m ls bp dp R="" i k t0
  n=${spec%%:*}; m=${spec#*:}; ls=""; case $m in *:*) ls=${m#*:}; m=${m%%:*} ;; esac; ls=${ls//+/,}
  drv "$n" || { say "unknown driver $n"; return; }
  t0=$(date +%s.%N)
  env WIN_RENDER="$m" ${ls:+WIN_RENDER_LIGHT="$ls"} HEADLESS_SLOT=$s HEADLESS_HOLD=$H SOMVR_EXTRA="$ENV $T" \
    setsid -w "$HERE/headless_vr.sh" --win "$MOD" > "$OUT/boot-$tag.txt" 2>&1 < /dev/null &
  bp=$!
  for i in $(seq 1 120); do R=$(sed -n 's/^windows: \([^ ]*\) .*/\1/p' "$OUT/boot-$tag.txt"); [ -n "$R" ] && break; kill -0 $bp 2>/dev/null || break; sleep 0.5; done
  [ -n "$R" ] || { say "$tag: no run ($(tail -2 "$OUT/boot-$tag.txt" | tr '\n' ' '))"; wait $bp; return; }
  echo "$R" > "$OUT/run-$tag.txt"; say "$tag: $n at $m${ls:+ ($ls)} in slot $s, run ${R##*/}"
  ( for i in $(seq 1 600); do LC_ALL=C grep -a -q '^PRACTICE t=.* arrived' "$R/somvr.log" 2>/dev/null && { date +%s.%N > "$OUT/arrive-$tag.txt"; break; }
      kill -0 $bp 2>/dev/null || break; sleep 0.5; done ) &
  if [ -n "$D" ]; then
    bash -c "${D//@R@/$R}" > "$OUT/drive-$tag.txt" 2>&1 < /dev/null & dp=$!
  else   # S: the meadow for SL_SIT_S s after the arrival, then quit
    ( for i in $(seq 1 600); do [ -f "$OUT/arrive-$tag.txt" ] && break; kill -0 $bp 2>/dev/null || exit 0; sleep 0.5; done
      sleep "${SL_SIT_S:-240}"; echo quit >> "$R/cmd" ) > "$OUT/drive-$tag.txt" 2>&1 & dp=$!
  fi
  wait $bp
  for k in $(seq 1 60); do kill -0 $dp 2>/dev/null || break; sleep 1; done
  kill -0 $dp 2>/dev/null && { say "$tag: the driver still ran 60 s after the boot: stopped"; pkill -TERM -P $dp; kill $dp 2>/dev/null; }
  wait $dp 2>/dev/null
  printf '%s %s %s\n' "$t0" "$(cat "$OUT/arrive-$tag.txt" 2>/dev/null || echo -)" "$(date +%s.%N)" > "$OUT/times-$tag.txt"
  say "$tag: done - $(grep -c PASS "$OUT/drive-$tag.txt") PASS, $(grep -c FAIL "$OUT/drive-$tag.txt") FAIL; $(grep -h -E 'render.cfg:|vram \(|save after|stopped at once|exited by itself|crash' "$R/summary.txt" 2>/dev/null | cut -c12-150 | tr '\n' ' ')"
}
w=$(ls "$OUT"/waves.txt > /dev/null 2>&1 && awk '$3 != "end" {n = $2} END {print n + 0}' "$OUT/waves.txt" || echo 0)   # (a later call goes on numbering)
for wave in "$@"; do
  w=$((w + 1))
  [ -e "$OUT/STOP" ] && { say "STOP: $(cat "$OUT/STOP")"; break; }
  g=$(bash "$HERE/game_slot.sh" gate) || { say "STOP: the gate is closed: $g"; echo "gate: $g" > "$OUT/STOP"; break; }
  say "== wave $w: $wave"; echo "$(date +%s) $w $wave" >> "$OUT/waves.txt"
  s=0; ps_=(); for spec in ${wave//,/ }; do s=$((s + 1)); one "$spec" "$s" "$w-$s" & ps_+=($!); sleep 2; done
  wait "${ps_[@]}"
  echo "$(date +%s) $w end" >> "$OUT/waves.txt"
  for f in "$OUT"/run-$w-*.txt; do
    R=$(cat "$f"); find "$R" -maxdepth 3 -type f \( -name '*.ppm' -o -name '*.pgm' \) -delete 2>/dev/null
  done
  sleep 5
done
say "all done"
