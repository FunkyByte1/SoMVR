#!/bin/bash
# soak29-drive.sh - build 29c (the som-b29-soak2 helper, docs/re-xr-pipeline.md 129, handoff/HANDOFF-soak.md "build 29"):
# the long session again, on everything builds 27-29 added - soak27-drive.sh's cycle (walk, fight, bow, vision, climb,
# menus, trip, death) interleaved with the Test page's new items, each run by the driver that proved it alone (as
# rehearsal28-drive.sh runs them: a copy with its `quit` and `endlogs` taken out, the test tools put back after it, the
# player's `recenter` and the sword back in its slot before it, every trip waiting out the practice area's pause):
#   flash (abil27: the wraith flash, the downed orc's stab), fire (abil27c: the whistle, the ignite shot), drop (above25:
#   the drop kill), captain (captain24: Practice > Captain's fight), brutal (abil27d), pcar (pcar29: Practice > Caragor's
#   counter, the mount, the ride), tower (climb27b: a forge tower down and up by hand, the drop), struggle (struggle27c:
#   the last chance, one try = a death), stealth (kill22: the thrust, the blade pulled out), crowd (crowd27: still, vault)
# plus this script's map (the click, a press beside the cross and on it = the waypoint, B) and death. S29_ITEMS = the
# order (looped until S29_MIN minutes after the start); each item at most its minutes (then its driver is killed).
# After each item: rh-state.txt's lines (soak-state.txt) and the item's line in soak-steps.txt (WARNs, log lines, the
# world back or not); every 60 s soak-samples.tsv: soak27's columns (RSS, VmSize, threads, fds, CPU, monado, gamescope,
# VRAM, the stereo rate, the gaps, WARNs, the logs' sizes) + the last 10-s PERF window (the game thread's busy and our
# work, our peeks a frame, the render thread's frame work; the layer's GPU total) and the VERBS root-node lines so far
# (build 29's thrash, 129.2). scripts/soak29_report.py RUN. Stops at once (quit) when the user goes into VR.
# The boot (sandbox off; announce it in the lock's owner file; a frozen copy of the built mod, so builds can go on):
#   HEADLESS_HARD=4500 HEADLESS_HOLD=4300 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env |
#   tr '\n' ' ') SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217
#   SOMVR_SETTINGS=/tmp/somvr-soak29/settings.conf" setsid -w scripts/headless_vr.sh /ABS/COPY/somvr
# and this script at the same time from this worktree's scripts/ (S29_WT = a word of the copy's path, default the
# worktree's name; S29_RUN=RUNDIR attaches to a running boot).
SOM=${S29_SOM:-$HOME/Projects/flat2vr/ShadowOfMordor}
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${S29_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
S29_MIN=${S29_MIN:-70}
S29_HARD=${S29_HARD:-4500}                # the boot's HEADLESS_HARD (s): an item that cannot end before it is skipped
ITEMS=${S29_ITEMS:-walk fight bow flash fire map menus drop captain death vision climb brutal pcar tower struggle stealth crowd trip death}
HOME_AT="32500 2100 -24800"               # the practice area (SOMVR_PRACTICE_AT)
HANG_AT="33686.1 2575 -27297.2"           # the user's hang spot on the meadow bank (pin16's PIN_AT2)
T0=$(date +%s)
declare -A DRV MIN EV
DRV[flash]=abil27;    MIN[flash]=5;    EV[flash]='ABIL27_STEPS="flash ground" ABIL27_NOQUIT=1'
DRV[fire]=abil27c;    MIN[fire]=5;     EV[fire]='ABIL27C_STEPS="whistle ignite" ABIL27C_NOQUIT=1'
DRV[drop]=above25;    MIN[drop]=5;     EV[drop]='STEPS=drop'
DRV[captain]=captain24; MIN[captain]=6; EV[captain]='KP_STEPS="measure fight"'
DRV[brutal]=abil27d;  MIN[brutal]=4;   EV[brutal]='ABIL27D_STEPS=brutal ABIL27D_BRUTAL=brutal1 ABIL27D_NOQUIT=1'
DRV[pcar]=pcar29;     MIN[pcar]=6;     EV[pcar]='PC_STEPS=chain PC_SPAWNS=3'
DRV[tower]=climb27b;  MIN[tower]=9;    EV[tower]='TOPS="tower:15790 6237 -3110||2"'
DRV[struggle]=struggle27c; MIN[struggle]=5; EV[struggle]='SC_TRIES=1 SC_NOQUIT=1'
DRV[stealth]=kill22;  MIN[stealth]=4;  EV[stealth]='SD_STEPS="thrust out" NOQUIT=1'
DRV[crowd]=crowd27;   MIN[crowd]=4;    EV[crowd]='CROWD27_STEPS="still vault" CROWD27_STILL_S=20 CROWD27_VAULTS=1 CROWD27_NOQUIT=1'
R=${S29_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
bash "$(dirname "$0")/render_need.sh" "$R" soak29-drive.sh || exit 1   # b37: a Windows run at WIN_RENDER=vr (re-xr-pipeline.md 158.5)
echo "run $R"
L=$R/somvr.log
X=$R/xr-layer.log
ST=$R/soak-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
CUR=""; SAMPLER=""
cleanup() { rm -f "$HERE"/.s29-*-drive.sh; [ -n "$SAMPLER" ] && kill "$SAMPLER" 2>/dev/null; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$ST"; [ -n "$CUR" ] && kill -- -"$CUR" 2>/dev/null; echo quit >> "$R/cmd"; cleanup; exit 1; }
# the watcher: the user in VR = the game stopped at once (every 2 s), the running driver's process group first
( while sleep 2; do userin && { echo "$(date +%T) STOP: the user is in VR" >> "$ST"; g=$(cat "$R/.s29-cur" 2>/dev/null)
    [[ $g =~ ^[0-9]+$ ]] && [ "$g" -gt 1 ] && kill -- -"$g" 2>/dev/null; echo quit >> "$R/cmd"; exit 0; }
    [ -e "$R/.s29-done" ] && exit 0; done ) &
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${T_S:-0.6}"; }
tap() { t "btn $1 $2 1"; sleep "${3:-0.25}"; t "btn $1 $2 0"; }
cnt() { grep -cE "$1" "${2:-$L}"; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -E "$2"; }
nsince() { since "$1" "$2" "${3:-$L}" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1" "${3:-$L}"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1" "${3:-$L}")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); note "dump $1 $f"; }
ui() { grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-110; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
go() { local a0 i; a0=$(cnt '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)'); w "practice go $1" 0.3
  for i in $(seq 1 120); do [ "$(cnt '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)')" -gt "$a0" ] && break; sleep 0.25; done
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-140
  sleep 7; }   # (rehearsal 1: the arrival's weather change pauses the game up to 5.5 s; words written in it are lost)
REST_L="hand L head -22 -40 15 0 0 0"; REST_R="hand R head 22 -40 15 0 0 0"
tools_back() { local h b ls=()
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "hands test off" 0.3
  for h in L R; do for b in menu a b x y stick trigger squeeze; do ls+=("btn $h $b 0"); done; ls+=("stick $h 0 0"); done
  t "${ls[@]}" "head 0 0 0" "$REST_L" "$REST_R"
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "ride guard off" 0.3; w "verbs template off" 0.3; w "crouch stand" 0.3
  w "captain stakes off" 0.3; w "practice remove" 1.0; }
perfline() { local g b; g=$(grep -E 'xr: perf: 10\.0 s' "$X" | tail -1 | sed -nE 's/.*GPU total ([0-9.]+)\/[0-9.]+\/([0-9.]+)\/([0-9.]+).*/gpu \1 p95 \2 max \3/p')
  b=$(grep -E '^PERF t=.* game thread: busy' "$L" | tail -1 | sed -nE 's/.*game thread: busy ([0-9.]+)\/[0-9.]+\/([0-9.]+)\/([0-9.]+).*/busy \1 p95 \2 max \3/p')
  echo "$g; game $b; gaps $(grep -cE 'xr: gap [0-9]+:' "$X"); WARN $(grep -c '^WARN' "$L")/$(grep -c 'WARN' "$X"); log $(( $(stat -c %s "$L") / 1024 )) KB"; }
state() { w "climb state" 0.8; w "practice status" 0.8
  { echo "== $(date +%T) after $1 ($(( ($(date +%s) - T0) / 60 )) min)"
    local k
    for k in '^SESSION ui ' '^CLIMB state' '^BODYMOMENT t=' '^CROUCH ' '^BOW .* (enter |exit: )' '^PRACTICE t=.*status' '^SLOTS .*sword: ' '^RIDE t=' '^STRUGGLE t=' '^VIEW (hold|held)'; do
      grep -E "$k" "$L" | tail -1 | cut -c1-200; done
    for k in 'xr: comfort frame' 'xr: vrmenu: (closed|opened)' 'xr: stereo frame' 'xr: card:'; do grep -E "$k" "$X" | tail -1 | cut -c1-200; done
    echo "perf: $(perfline)"
  } >> "$R/soak-state.txt"; }
results() { grep -E '(^|[ :])(PASS|FAIL|NOTE|STOP)\b' "$1" | grep -vE '^(\S+ )?(word|xrtest):' | cut -c1-200 | head -${2:-12} | sed 's/^/    /' >> "$ST"; }
sword_in_hand() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
sheathe() { sword_in_hand || return 0
  w "hands test R 22 -35 -30 0 180 0" 0.3; w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6
  w "hands squeeze R 0" 0.4; w "hands test R 22 -35 -30 0 180 0" 0.4; w "hands squeeze R -1" 0.2; w "hands test off" 0.4; }
mkcopy() {   # scripts/.s29-NAME-drive.sh: the driver without its quits and endlogs (its sourced libraries too)
  local n=$1 f
  for f in "$n" climb2 pin16; do
    [ -f "$HERE/$f-drive.sh" ] || continue
    sed -e 's|echo quit >> "\$R/cmd"|echo "(soak: no quit)" >\&2|g' -e 's|w "endlogs" [0-9.]*|true|g' \
        -e 's|/climb2-drive\.sh"|/.s29-climb2-drive.sh"|g; s|/pin16-drive\.sh"|/.s29-pin16-drive.sh"|g' \
        "$HERE/$f-drive.sh" > "$HERE/.s29-$f-drive.sh"
    grep -nE 'quit.*>>|>>.*cmd.*quit|"endlogs"' "$HERE/.s29-$f-drive.sh" | grep -v 'soak: no quit' && { echo "STOP: a quit left in .s29-$f"; exit 1; }
  done; }

# ---- the sampler: one line a minute (soak27's columns + the PERF window + the VERBS root-node lines) ------------------
samp() { local prev_s="" prev_t="" prev_cpu="" pt=""
  echo -e "wall\tmin\tgame_pid\trss_mb\tvsz_mb\tthreads\tfds\tcpu_cores\tmonado_rss_mb\tgamescope_rss_mb\tvram_mb\tstereo_fps\tgaps\twarn_somvr\twarn_layer\tsomvr_kb\tlayer_kb\trun_mb\tbusy_ms\tbusy_p95\tours_ms\tpeeks\trender_ms\trender_p95\tgpu_ms\tgpu_p95\tvb_root_lines\titem" > "$R/soak-samples.tsv"
  while :; do
    local now pid rss vsz thr fds cpu mon gs vram sl s tt fps gaps w1 w2 sz1 sz2 du pl busy bp ours pk rw rp xp gm gp vr it
    now=$(date +%s); pid=$(pgrep -x ShadowOfMordor | head -1)
    if [ -n "$pid" ] && [ -r /proc/$pid/status ]; then
      rss=$(awk '/^VmRSS/ {printf "%.0f", $2/1024}' /proc/$pid/status); vsz=$(awk '/^VmSize/ {printf "%.0f", $2/1024}' /proc/$pid/status)
      thr=$(awk '/^Threads/ {print $2}' /proc/$pid/status); fds=$(ls /proc/$pid/fd 2>/dev/null | wc -l)
      local c; c=$(awk '{print $14 + $15}' /proc/$pid/stat)
      if [ -n "$prev_cpu" ]; then cpu=$(awk -v a="$c" -v b="$prev_cpu" -v dt=$((now - pt)) 'BEGIN{printf "%.2f", (a-b)/100/dt}'); else cpu=-; fi
      prev_cpu=$c; pt=$now
    else rss=-; vsz=-; thr=-; fds=-; cpu=-; fi
    mon=$(for p in $(pgrep -x monado-service); do awk '/^VmRSS/ {printf "%.0f\n", $2/1024}' /proc/$p/status; done | head -1)
    gs=$(for p in $(pgrep gamescope); do awk '/^VmRSS/ {printf "%.0f\n", $2/1024}' /proc/$p/status; done | sort -rn | head -1)
    vram=$(cat /sys/bus/pci/devices/0000:0d:00.0/mem_info_vram_used 2>/dev/null | awk '{printf "%.0f", $1/1048576}')
    sl=$(grep 'xr: stereo frame' "$X" | tail -1)
    s=$(echo "$sl" | sed -nE 's/.*\(stereo ([0-9]+),.*/\1/p'); tt=$(echo "$sl" | awk '{print $1}')
    if [ -n "$prev_s" ] && [ -n "$s" ] && [ "$tt" != "$prev_t" ]; then fps=$(awk -v a="$s" -v b="$prev_s" -v x="$tt" -v y="$prev_t" 'BEGIN{printf "%.1f", (a-b)/(x-y)}'); else fps=-; fi
    [ -n "$s" ] && { prev_s=$s; prev_t=$tt; }
    gaps=$(grep -cE 'xr: gap [0-9]+:' "$X"); w1=$(grep -c '^WARN' "$L"); w2=$(grep -c 'WARN' "$X")
    sz1=$(( $(stat -c %s "$L") / 1024 )); sz2=$(( $(stat -c %s "$X") / 1024 )); du=$(du -sm "$R" 2>/dev/null | cut -f1)
    pl=$(grep -E '^PERF t=.* game thread: busy' "$L" | tail -1)
    busy=$(echo "$pl" | sed -nE 's/.*game thread: busy ([0-9.]+)\/[0-9.]+\/([0-9.]+)\/.*/\1/p'); bp=$(echo "$pl" | sed -nE 's/.*game thread: busy [0-9.]+\/[0-9.]+\/([0-9.]+)\/.*/\1/p')
    ours=$(echo "$pl" | sed -nE 's/.*\(over: [0-9]+\); ours ([0-9.]+)\/.*/\1/p'); pk=$(echo "$pl" | sed -nE 's/.*our peeks a frame \(process_vm_readv, the game thread\) ([0-9.]+)\/.*/\1/p')
    rw=$(echo "$pl" | sed -nE 's/.*its frame work \(eye 0.s start to the present: [0-9]+ frames\) cpu ([0-9.]+)\/[0-9.]+\/([0-9.]+)\/.*/\1/p')
    rp=$(echo "$pl" | sed -nE 's/.*its frame work \(eye 0.s start to the present: [0-9]+ frames\) cpu [0-9.]+\/[0-9.]+\/([0-9.]+)\/.*/\1/p')
    xp=$(grep -E 'xr: perf: 10\.0 s' "$X" | tail -1); gm=$(echo "$xp" | sed -nE 's/.*GPU total ([0-9.]+)\/.*/\1/p'); gp=$(echo "$xp" | sed -nE 's/.*GPU total [0-9.]+\/[0-9.]+\/([0-9.]+)\/.*/\1/p')
    vr=$(grep -c "^VERBS the root node" "$L"); it=$(cat "$R/.s29-item" 2>/dev/null)
    echo -e "$(date +%T)\t$(( (now - T0) / 60 ))\t${pid:--}\t$rss\t$vsz\t$thr\t$fds\t$cpu\t${mon:--}\t${gs:--}\t${vram:--}\t$fps\t$gaps\t$w1\t$w2\t$sz1\t$sz2\t$du\t${busy:--}\t${bp:--}\t${ours:--}\t${pk:--}\t${rw:--}\t${rp:--}\t${gm:--}\t${gp:--}\t$vr\t${it:--}" >> "$R/soak-samples.tsv"
    [ -z "$pid" ] && [ $(( now - T0 )) -gt 120 ] && echo "$(date +%T) the game is gone" >> "$ST"
    [ -e "$R/.s29-done" ] && break
    sleep "${S29_SAMPLE_S:-60}"
  done; }

# ---- soak27's segments (its poses) -------------------------------------------------------------------------------------
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
RZ=90; EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
SLOT_L="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0 s 0.5"; DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
ready() { grep -E '^EXECUTE ready (off )?t=' "$L" | tail -1 | grep -q '^EXECUTE ready t='; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
hands_rest() { w "hands squeeze L 0" 0.2; w "hands squeeze R 0" 0.2; w "$LREST" 0.3; w "$REST" 0.4; }
seg_walk() { w "hands test off" 0.3; t "$REST_L" "$REST_R"
  w "stick 0 1 0 3" 3.6; local k; for k in 1 2 3; do w "turn+" 0.7; done; for k in 1 2 3; do w "turn-" 0.7; done
  w "stick 0 -1 0 2" 2.6; w "stick 1 0 0 1.5" 2.0; w "stick -1 0 0 1.5" 2.0; note "walk: done"; }
seg_fight() { local n0 dn0 k; n0=$(lines); dn0=$(cnt '^PRACTICE t=.* orc is down')
  go "$HOME_AT" > /dev/null; hands_rest; w "practice spawn infantry" 2; face
  sword_in_hand || draw_slot; w "fight guard learn" 0.4; w "execute chase on" 0.4
  for k in $(seq 1 24); do
    userin && bail "the user is in VR"
    if ready; then t "btn R trigger 1"; sleep 0.25; ecut; t "btn R trigger 0"; else ecut; fi
    [ "$(cnt '^PRACTICE t=.* orc is down')" -gt "$dn0" ] && break
  done
  note "fight: $(nsince "$n0" '^COMBAT hit ') hits, $(nsince "$n0" '^EXECUTE kill t=') executions, down $(( $(cnt '^PRACTICE t=.* orc is down') - dn0 )) after $k cuts"; }
seg_bow() { local n0 k; n0=$(lines)
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "hands test off" 0.4; t "$REST_L" "$REST_R"; sleep 0.4
  t "$SLOT_L"; sleep 0.3; t "$SLOT_L" "btn L squeeze 1"; sleep 1.0; t "$BOW_STRAIGHT" "btn L squeeze 1"; sleep 1
  for k in 1 2; do
    t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.8; t "btn R trigger 1"; sleep 0.3
    t "$BOW_STRAIGHT" "$DRAWN"; sleep 4; t "btn R trigger 0"; sleep 1.2; t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2
  done
  t "$SLOT_L" "$REST_R"; sleep 0.6; t "btn L squeeze 0"; sleep 0.8; t "$REST_L" "$REST_R"
  note "bow: $(nsince "$n0" '^BOW .* enter ') out, $(nsince "$n0" '^BOW .* full draw') full draws, $(nsince "$n0" '^BOW .* release: ') shots, $(nsince "$n0" '^BOW .* exit: ') put back; root-node lines $(nsince "$n0" '^VERBS the root node')"; }
seg_vision() { local n0; n0=$(lines); w "vision y 2500" 0.2; sleep 3.5; w "vision y 1" 0.2; sleep 2
  note "vision: $(nsince "$n0" '^(VISION|WRAITH)') vision lines"; }
seg_climb() { local n0 t1 i got=0; n0=$(lines)
  go "$HANG_AT" > /dev/null
  w "climb probe" 1.2; local nb; nb=$(grep "^CLIMB probe t=.*at head +0 cm" "$L" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
    grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1)
  if [ -n "$nb" ]; then local b=${nb% *} n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
    while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; fi
  for t1 in "30 30" "50 30" "20 25" "60 40"; do
    w "hands squeeze R 0" 0.3; w "hands test R 25 ${t1% *} ${t1#* } 0 0 0" 0.5
    i=$(cnt '^CLIMB t=.* grab R:'); w "hands squeeze R 1" 1.0
    [ "$(cnt '^CLIMB t=.* grab R:')" -gt "$i" ] && { got=1; break; }
  done
  if [ $got = 1 ]; then w "hands swing R 20 -15 25 0 0 0 1500" 2.6; sleep 1; w "hands squeeze R 0" 2.5; fi
  hands_rest; w "climb state" 0.6
  note "climb: grab=$got, $(nsince "$n0" '^CLIMB t=.* (start|grab [LR]):') grabs, $(nsince "$n0" '^CLIMB t=.* end:') ends; $(grep -E '^CLIMB state' "$L" | tail -1 | cut -c1-120)"; }
seg_menus() { local n0 o k pz mp vm; n0=$(lines); o=$(cnt 'vrmenu: opened by' "$X")
  t "$REST_L" "$REST_R"; sleep 0.5
  inworld && { tap L menu 0.3; sleep 2.5; }
  pz=$(ui | cut -c1-80)
  for k in 1 2 3 4; do inworld && break; tap R b; sleep 1.5; done
  t "hand R head 12 -22 30 60 0 30"
  for k in 1 2 3; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(cnt 'vrmenu: opened by' "$X")" -gt "$o" ] && break; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  vm=$(( $(cnt 'vrmenu: opened by' "$X") - o )); sleep 1.5
  t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1.2; t "$REST_L" "$REST_R"; sleep 1.0
  grep -E 'xr: vrmenu: (closed|opened)' "$X" | tail -1 | grep -q 'opened' && { tap L x 0.3; sleep 1.2; }
  note "menus: vrmenu opens $vm, closed: $(grep -E 'xr: vrmenu: (closed|opened)' "$X" | tail -1 | grep -c closed); pause '$pz'; back in the world: $(inworld && echo yes || echo NO)"; }
seg_trip() { go "$HOME_AT" > /dev/null; w "practice far infantry" 3; sleep 2; w "practice remove" 1.5; w "practice back" 6; note "trip: $(go "$HOME_AT")"; }
seg_map() { local n0 k; n0=$(lines); inworld || { tap R b; sleep 1.5; }
  t "$REST_L" "$REST_R"; T_S=0.05 t "btn R stick 1"; sleep 0.12; T_S=0.05 t "btn R stick 0"; sleep 3.5; local m1; m1=$(ui | cut -c1-80)
  t "$REST_L" "hand R aim board 0.66 0.40 30"; sleep 0.8; t "btn R trigger 1"; sleep 0.25; t "btn R trigger 0"; sleep 3
  t "$REST_L" "hand R aim board 0.50 0.50 30"; sleep 0.8; t "btn R trigger 1"; sleep 0.25; t "btn R trigger 0"; sleep 2.5
  t "$REST_L" "$REST_R"; sleep 0.5
  for k in 1 2 3 4; do inworld && break; tap R b; sleep 1.8; done; sleep 2
  note "map: '$m1'; $(nsince "$n0" '^MAP t=.*a press') presses; waypoint lines $(nsince "$n0" "UserWaypoint"); back in the world: $(inworld && echo yes || echo NO)"; }
seg_death() { local n0 k down=0 t1; n0=$(lines); go "$HOME_AT" > /dev/null
  hands_rest; w "execute chase off" 0.3; w "fight guard off" 0.3
  w "captain stakes on" 0.3; w "practice captain" 2.5; waitfor '^CAPTAIN t=.* the presentation is over' 40
  w "execute chase on" 0.4
  # (soak 3: a captain's first plain struggle brings the game's struggle tutorial, a paused card: A, as a player does;
  # then the last chance - the hands' thrusts, struggle27c's - for at most 40 s; the screens after it as before)
  local tut=0 sg=0
  for k in $(seq 1 150); do userin && bail "the user is in VR"
    [ "$(nsince "$n0" '^CAPTAIN t=.* Talion is down')" -gt 0 ] && { down=1; break; }
    [ $tut = 0 ] && [ "$(nsince "$n0" "show 'PC_StruggleTutorial'")" -gt 0 ] && { sleep 2; tap R a 0.3; tut=1; }
    [ "$(nsince "$n0" '^STRUGGLE t=.* start')" -gt 0 ] && { sg=1; break; }; sleep 1; done
  if [ $sg = 1 ]; then local h=R
    for k in $(seq 1 100); do
      [ "$(nsince "$n0" '^STRUGGLE t=.* over after|PC_PreDeathRecover|^CAPTAIN t=.* Talion is down')" -gt 0 ] && break
      [ $tut = 0 ] && [ "$(nsince "$n0" "show 'PC_StruggleTutorial'")" -gt 0 ] && { sleep 2; tap R a 0.3; tut=1; }
      if [ $h = L ]; then w "hands swing L -10 -12 70 0 0 0 110" 0.15; w "hands test L -25 -35 25 0 0 0" 0.2; h=R
      else w "hands swing R 10 -12 70 0 0 0 110" 0.15; w "hands test R 25 -35 25 0 0 0" 0.2; h=L; fi
    done
    note "death: the last chance: $(since "$n0" '^STRUGGLE t=.* over after' | tail -1 | cut -c1-120)$(nsince "$n0" 'PC_PreDeathRecover') recovers; the tutorial $tut"
    w "hands test off" 0.3; sleep 3
    [ "$(nsince "$n0" '^CAPTAIN t=.* Talion is down')" -gt 0 ] && down=1
  fi
  w "execute chase off" 0.3; sleep 2; t1=$(date +%s)
  for k in $(seq 1 40); do sleep 3; inworld && [ $(( $(date +%s) - t1 )) -gt 20 ] && break
    [ $((k % 3)) = 0 ] && tap R a || tap R b; done
  note "death: down=$down, the screens $(( $(date +%s) - t1 )) s, back in the world: $(inworld && echo yes || echo NO); $(nsince "$n0" '^SESSION ui ') ui lines"
  w "captain stakes off" 0.3; w "practice remove" 1.5; }

# ---- the run ---------------------------------------------------------------------------------------------------------
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
sleep 8
# (S29_NOSAMP=1: a driver attached to a run whose sampler still goes - soak 3's restart after this script's own stop)
[ -n "${S29_NOSAMP:-}" ] || { samp & SAMPLER=$!; }
# the controls card away (A, as the card says), the hands at rest
grep -q 'card: put away' "$X" || { tap R a 0.3; sleep 2; }
hands_rest; w "front keep 0" 0.3
note "start: items '$ITEMS', $S29_MIN min; the card: $(grep -E 'xr: card:' "$X" | tail -1 | cut -c1-80)"
C=0
while [ $(( $(date +%s) - T0 )) -lt $(( S29_MIN * 60 )) ]; do
  C=$((C + 1)); note "== pass $C"; ran=0
  for name in $ITEMS; do
    [ $(( $(date +%s) - T0 )) -lt $(( S29_MIN * 60 )) ] || break 2
    left=$(( S29_HARD - 190 - ($(date +%s) - T0) )); m=${MIN[$name]:-4}
    [ -n "${S29_CAP:-}" ] && [ "$m" -gt "$S29_CAP" ] && { m=$S29_CAP; MIN[$name]=$m; }   # (S29_CAP: a test of this script)
    [ $(( m * 60 )) -gt "$left" ] && { note "-- $name: not in this boot ($m min > $(( left / 60 )) min left)"; continue; }
    pgrep -x ShadowOfMordor > /dev/null || { note "the game is gone before $name"; break 2; }
    grep -q 'pass-through from now on' "$X" && { note "VR LOST: $(grep 'pass-through from now on' "$X" | head -1 | cut -c1-160)"; break 2; }
    echo "$name" > "$R/.s29-item"; ran=$((ran + 1))
    ts=$(date +%s); n0=$(lines); w0=$(cnt '^WARN'); x0=$(cnt 'WARN' "$X"); rss0=$(awk '/^VmRSS/ {printf "%.0f", $2/1024}' /proc/$(pgrep -x ShadowOfMordor | head -1)/status 2>/dev/null)
    drv=${DRV[$name]:-}
    if [ -z "$drv" ]; then note "-- $name"; "seg_$name"; rc=$?
    else
      note "-- $name ($drv, at most ${MIN[$name]} min) [${EV[$name]}]"
      case $name in tower) ;; *) go "$HOME_AT" > /dev/null ;; esac
      w "recenter" 1.5; sheathe
      mkcopy "$drv"
      RUNS="SD_RUN=$R C21_RUN=$R CROWD27_RUN=$R SK_RUN=$R ABIL27_RUN=$R ABIL27C_RUN=$R ABIL27D_RUN=$R KP_RUN=$R NM_RUN=$R PIN_RUN=$R R26_RUN=$R PC_RUN=$R"
      ( cd "$HERE" && eval "exec env $RUNS ${EV[$name]} setsid bash \"$HERE/.s29-$drv-drive.sh\"" ) > "$R/s29-$name-$C.out" 2>&1 &
      CUR=$!; echo "$CUR" > "$R/.s29-cur"
      k9=0; while kill -0 "$CUR" 2>/dev/null; do sleep 2; [ $k9 = 0 ] && [ $(( $(date +%s) - ts )) -gt $(( ${MIN[$name]} * 60 )) ] && { note "TIMEOUT: $drv after ${MIN[$name]} min"; kill -- -"$CUR"; k9=1; }; done
      wait "$CUR"; rc=$?; CUR=""; echo "" > "$R/.s29-cur"; results "$R/s29-$name-$C.out"
    fi
    rss1=$(awk '/^VmRSS/ {printf "%.0f", $2/1024}' /proc/$(pgrep -x ShadowOfMordor | head -1)/status 2>/dev/null)
    note "$name: exit $rc, $(( $(date +%s) - ts )) s, RSS ${rss0:-?} -> ${rss1:-?} MB, WARN +$(( $(cnt '^WARN') - w0 ))/+$(( $(cnt 'WARN' "$X") - x0 )), $(nsince "$n0" '.') log lines, root-node lines +$(nsince "$n0" '^VERBS the root node')"
    [ "$(cnt '^WARN')" -gt "$w0" ] && since "$n0" '^WARN' | head -3 | cut -c1-200 | sed 's/^/    /' >> "$ST"
    note "$name left: $(ui); $(grep -E '^CLIMB state' "$L" | tail -1 | cut -c1-90)"
    tools_back
    inworld || { note "after $name NOT in the world: $(ui) - B"; tap R b; sleep 2; inworld || { tap R b; sleep 2; }; note "  then: $(ui)"; }
    state "$name"
  done
  [ "$ran" = 0 ] && break   # (soak 3: nothing fits in the time left - not a spin of empty passes)
done
note "done after $C passes, $(( ($(date +%s) - T0) / 60 )) min; $(perfline)"
d end
touch "$R/.s29-done"; sleep 3; w "endlogs" 2; sleep 3; echo quit >> "$R/cmd"; cleanup; echo "$(date +%T) quit"
