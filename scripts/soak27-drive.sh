#!/bin/bash
# soak27-drive.sh - build 27 (the som-b27-soak helper, docs/re-xr-pipeline.md 107, handoff/HANDOFF-soak.md): a long
# headless session (scripts/headless_vr.sh with HEADLESS_HARD, boot_check --soak) played over and over the way a real
# session goes, to find what only hours show: a crash, a leak, a creeping slowdown, a state that does not come back.
# One cycle (SK_SEGS, in this order; each one best-effort - a step that does not take is noted, never fatal):
#   walk     the stick forward and back, three snap turns each way
#   fight    a practice infantry, the sword drawn from its slot, the test guard (parries) and the test chase, edge
#            cuts until he is down (at most SK_CUTS), the execution's trigger when the game offers it; the orc removed
#   bow      the test hands off and their squeeze given back (-1); the bow out of its slot (the left grip), two draws
#            and shots, put back
#   vision   the wraith vision held 2.5 s (Y), let go
#   climb    the meadow bank's hang spot: the right hand onto the wall, a pull up, let go (the drop to the ground)
#   stealth  a far infantry, Talion 220 cm behind it, crouched, the thrust; the orc removed
#   menus    the pause menu (a short tap of the left menu button) and B; the VR menu (the palm gesture) open, X shuts
#            it; the map (the right stick's click) and B
#   trip     `practice far` and `practice back`, then `practice go` home
#   death    (every SK_DEATH_EVERY-th cycle, default 3) the practice's Captain with the stakes on, no guard, until
#            Talion is down; the screens after it dismissed (B, then A) until the world is back; home again
# After each cycle, at home: the state lines (soak-state.txt: the UI, the climb, the comfort ring, the VR menu, the bow,
# the body moments, the crouch) and a both-eye dump. Every SK_SAMPLE_S (60) s, beside the cycles (soak-samples.tsv):
# the game's RSS / VmSize / threads / fds / CPU (cores over the last minute), monado's and gamescope's RSS, the GPU's
# VRAM, the stereo frame rate and the display-time error from the layer's 600-frame lines, the layer's gaps, the
# WARNs, the logs' and the run folder's sizes. SK_MIN (default 50) minutes of cycles, then `quit`.
# Attaches to a running boot with SK_RUN=RUNDIR (else waits for one of this worktree's). Stops at once (quit) when the
# user goes into VR. The boot (sandbox off):
#   HEADLESS_HARD=4200 HEADLESS_HOLD=3900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env |
#   tr '\n' ' ') SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w
#   scripts/headless_vr.sh /ABS/WORKTREE/ShadowOfMordor/mod/somvr
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${SK_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
SEGS=${SK_SEGS:-walk fight bow vision climb stealth menus trip death}
SK_MIN=${SK_MIN:-50}
HOME_AT=${SK_HOME:-32500 2100 -24800}     # the practice area (SOMVR_PRACTICE_AT)
HANG_AT=${SK_HANG:-33686.1 2575 -27297.2} # the user's hang spot on the meadow bank (pin16's PIN_AT2)
T0=$(date +%s)
R=${SK_RUN:-}
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
L=$R/somvr.log
X=$R/xr-layer.log
ST=$R/soak-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
SAMPLER=""
bail() { echo "$(date +%T) STOP: $1" | tee -a "$ST"; echo quit >> "$R/cmd"; [ -n "$SAMPLER" ] && kill "$SAMPLER" 2>/dev/null; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 8
has() { case " $SEGS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
tap() { t "btn $1 $2 1"; sleep "${3:-0.25}"; t "btn $1 $2 0"; }
cnt() { grep -cE "$1" "${2:-$L}"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
nsince() { since "$1" "$2" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1" "${3:-$L}"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1" "${3:-$L}")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
note() { echo "$(date +%T) c$C $*" | tee -a "$ST"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$')
      echo "$(date +%T) dump $1 $f" | tee -a "$ST"; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
go() { w "practice go $1" 1; waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-160; sleep 2; }

# the sampler: one line a minute beside the cycles
samp() { local prev_s="" prev_t="" prev_cpu="" pt=""
  echo -e "wall\tmin\tgame_pid\trss_mb\tvsz_mb\tthreads\tfds\tcpu_cores\tmonado_rss_mb\tgamescope_rss_mb\tvram_mb\tstereo_fps\tdisp_err_ms\tlead_ms\tgaps\twarn_somvr\twarn_layer\tsomvr_kb\tlayer_kb\trun_mb" > "$R/soak-samples.tsv"
  while :; do
    local now pid rss vsz thr fds cpu mon gs vram sl s tt fps err lead gaps w1 w2 sz1 sz2 du
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
    err=$(echo "$sl" | sed -nE 's/.*display-time error ([-+0-9.]+) ms.*/\1/p'); lead=$(echo "$sl" | sed -nE 's/.*lead ([-+0-9.]+) ms.*/\1/p')
    gaps=$(grep -cE 'xr: gap [0-9]+:' "$X"); w1=$(grep -c '^WARN' "$L"); w2=$(grep -c 'WARN' "$X")
    sz1=$(( $(stat -c %s "$L") / 1024 )); sz2=$(( $(stat -c %s "$X") / 1024 )); du=$(du -sm "$R" 2>/dev/null | cut -f1)
    echo -e "$(date +%T)\t$(( (now - T0) / 60 ))\t${pid:--}\t$rss\t$vsz\t$thr\t$fds\t$cpu\t${mon:--}\t${gs:--}\t${vram:--}\t$fps\t${err:--}\t${lead:--}\t$gaps\t$w1\t$w2\t$sz1\t$sz2\t$du" >> "$R/soak-samples.tsv"
    [ -z "$pid" ] && [ $(( now - T0 )) -gt 120 ] && echo "$(date +%T) the game is gone" >> "$ST"
    sleep "${SK_SAMPLE_S:-60}"
  done; }
samp & SAMPLER=$!

# the poses (captain24 / kill22 / bow20-drive.sh's)
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
RZ=90; EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
PRE="hands test R 20 -27 -18 12 0 0"; THRUST="hands swing R 5 -11 54 12 0 0 180"
REST_L="hand L head -22 -40 15 0 0 0"; REST_R="hand R head 22 -40 15 0 0 0"
SLOT_L="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0 s 0.5"; DRAWN="hand R head 3 -10 -3 90 0 0 s 0.8"
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
ready() { grep -E '^EXECUTE ready (off )?t=' "$L" | tail -1 | grep -q '^EXECUTE ready t='; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
hands_rest() { w "hands squeeze L 0" 0.2; w "hands squeeze R 0" 0.2; w "$LREST" 0.3; w "$REST" 0.4; }

seg_walk() { w "hands test off" 0.3; t "$REST_L" "$REST_R"
  w "stick 0 1 0 3" 3.6; local k; for k in 1 2 3; do w "turn+" 0.7; done; for k in 1 2 3; do w "turn-" 0.7; done
  w "stick 0 -1 0 2" 2.6; w "stick 1 0 0 1.5" 2.0; w "stick -1 0 0 1.5" 2.0; }
seg_fight() { local n0 dn0 k; n0=$(lines); dn0=$(cnt '^PRACTICE t=.* orc is down')
  hands_rest; w "practice spawn infantry" 2; face
  drawn || draw_slot; w "fight guard learn" 0.4; w "execute chase on" 0.4
  for k in $(seq 1 "${SK_CUTS:-24}"); do
    userin && bail "the user is in VR"
    if ready; then t "btn R trigger 1"; sleep 0.25; ecut; t "btn R trigger 0"; else ecut; fi
    [ "$(cnt '^PRACTICE t=.* orc is down')" -gt "$dn0" ] && break
  done
  note "fight: $(nsince "$n0" '^COMBAT hit ') hits, $(nsince "$n0" '^EXECUTE kill t=') executions, down $(( $(cnt '^PRACTICE t=.* orc is down') - dn0 )) after $k cuts"
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "practice remove" 1.5; w "$REST" 0.4; }
seg_bow() { local n0 k; n0=$(lines)
  # the test hands' squeeze given back to the controllers (-1; `hands squeeze L 0` held the left grip open: soak 1's
  # bow never came out), then the layer's own controller poses
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "hands test off" 0.4; t "$REST_L" "$REST_R"; sleep 0.4
  t "$SLOT_L"; sleep 0.3; t "$SLOT_L" "btn L squeeze 1"; sleep 1.0; t "$BOW_STRAIGHT" "btn L squeeze 1"; sleep 1
  for k in 1 2; do
    t "$BOW_STRAIGHT" "$AT_STRING"; sleep 0.8; t "btn R trigger 1"; sleep 0.3
    t "$BOW_STRAIGHT" "$DRAWN"; sleep 1.4; t "btn R trigger 0"; sleep 1.2; t "$BOW_STRAIGHT" "$REST_R"; sleep 1.2
  done
  t "$SLOT_L" "$REST_R"; sleep 0.6; t "btn L squeeze 0"; sleep 0.8; t "$REST_L" "$REST_R"
  note "bow: $(nsince "$n0" '^BOW .* enter ') out, $(nsince "$n0" '^BOW .* full draw') full draws, $(nsince "$n0" '^BOW .* release: ') shots, $(nsince "$n0" '^BOW .* exit: ') put back"; }
seg_vision() { local n0; n0=$(lines); w "vision y 2500" 0.2; sleep 3.5; w "vision y 1" 0.2; sleep 2
  note "vision: $(nsince "$n0" '^(VISION|WRAITH)') vision lines"; }
seg_climb() { local n0 xs t1 i got=0; n0=$(lines)
  go "$HANG_AT"
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
  note "climb: grab=$got, $(nsince "$n0" '^CLIMB t=.* (start|grab [LR]):') grabs, $(nsince "$n0" '^CLIMB t=.* end:') ends; $(grep -E '^CLIMB state' "$L" | tail -1 | cut -c1-140)"; }
seg_stealth() { local n0; n0=$(lines)
  hands_rest; drawn || draw_slot; w "$REST" 0.3
  w "practice far infantry" 3; w "verbs behind 220" 0.8; w "execute face" 1.0
  w "crouch hold 9000" 0.3; t "head 0 -50 0 s 0.5"; sleep 1.2
  w "$PRE" 0.6; w "$THRUST" 0.8; sleep 1.5; w "$PRE" 0.4; w "$REST" 0.4
  t "head 0 0 0 s 0.4"; sleep 0.6; w "crouch stand" 0.4; w "verbs template off" 0.3; w "practice remove" 2
  note "stealth: $(nsince "$n0" '^STEALTH t=.* (kill|stab|thrust)') stealth kill lines, $(nsince "$n0" '^STEALTH t=.* aware orc .*-> 1') noticed"; }
seg_menus() { local n0 o k pz mp; n0=$(lines); o=$(cnt 'vrmenu: opened by' "$X")
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "hands test off" 0.3; t "$REST_L" "$REST_R"; sleep 0.5
  # the pause menu first (a short tap of the left menu button; soak 1 tapped it with the VR menu still up: no pause)
  inworld && { tap L menu 0.3; sleep 2.5; }
  pz=$(grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-80)
  for k in 1 2 3 4; do inworld && break; tap R b; sleep 1.5; done
  t "hand R head 12 -22 30 60 0 30"
  for k in 1 2 3; do t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 1.6
    [ "$(cnt 'vrmenu: opened by' "$X")" -gt "$o" ] && break; t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 0.8; done
  local vm=$(( $(cnt 'vrmenu: opened by' "$X") - o )); sleep 1.5; d "c$C-vrmenu"
  t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1.2; t "$REST_L" "$REST_R"; sleep 1.0
  grep -E 'xr: vrmenu: (closed|opened)' "$X" | tail -1 | grep -q 'opened' && { tap L x 0.3; sleep 1.2; }
  sleep 1; tap R stick; sleep 3; mp=$(grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-80)
  for k in 1 2 3 4; do inworld && break; tap R b; sleep 1.5; done
  note "menus: vrmenu opens $vm, closed: $(grep -E 'xr: vrmenu: (closed|opened)' "$X" | tail -1 | grep -c closed); pause '$pz'; map '$mp'; back in the world: $(inworld && echo yes || echo NO)"; }
seg_trip() { w "practice far infantry" 3; sleep 2; w "practice remove" 1.5; w "practice back" 6; go "$HOME_AT"; }
seg_death() { local n0 k down=0; n0=$(lines)
  hands_rest; w "execute chase off" 0.3; w "fight guard off" 0.3; w "captain stakes on" 0.3; w "practice captain" 2.5
  waitfor '^CAPTAIN t=.* the presentation is over' 40
  w "execute chase on" 0.4
  for k in $(seq 1 150); do userin && bail "the user is in VR"
    [ "$(nsince "$n0" '^CAPTAIN t=.* Talion is down')" -gt 0 ] && { down=1; break; }; sleep 1; done
  w "execute chase off" 0.3
  local t1=$(date +%s)
  for k in $(seq 1 40); do sleep 3; inworld && [ $(( $(date +%s) - t1 )) -gt 20 ] && break
    [ $((k % 3)) = 0 ] && tap R a || tap R b; done
  note "death: down=$down, the screens $(( $(date +%s) - t1 )) s, back in the world: $(inworld && echo yes || echo NO); $(nsince "$n0" '^SESSION ui ' ) ui lines"
  w "practice remove" 1.5; go "$HOME_AT"; }
state() { local k
  w "climb state" 0.8; w "practice status" 0.8
  { echo "== $(date +%T) cycle $C ($(( ($(date +%s) - T0) / 60 )) min)"
    for k in '^SESSION ui ' '^CLIMB state' '^BODYMOMENT t=' '^CROUCH ' '^BOW .* (enter |exit: )' '^PRACTICE t=.*status' '^SLOTS .*sword: '; do
      grep -E "$k" "$L" | tail -1 | cut -c1-200; done
    for k in 'xr: comfort frame' 'xr: vrmenu: (closed|opened)' 'xr: stereo frame'; do grep -E "$k" "$X" | tail -1 | cut -c1-200; done
  } >> "$R/soak-state.txt"
  d "c$C-home"; }

# 0. the controls card away, the hands at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$X" || { t "btn R a 1"; t "btn R a 0"; }
hands_rest; w "front keep 0" 0.3
C=0; note "start: segments '$SEGS', $SK_MIN min"
while [ $(( $(date +%s) - T0 )) -lt $(( SK_MIN * 60 )) ]; do
  C=$((C + 1)); note "== cycle $C"
  for s in $SEGS; do
    [ "$s" = death ] && [ $((C % ${SK_DEATH_EVERY:-3})) != 0 ] && continue
    pgrep -x ShadowOfMordor > /dev/null || { note "the game is gone"; break 2; }
    # VR lost (soak 1: the headless runtime's own 1800 s limit; XR_ERROR_INSTANCE_LOST): the rest would be flat - stop
    grep -q 'pass-through from now on' "$X" && { note "VR LOST: $(grep 'pass-through from now on' "$X" | head -1 | cut -c1-200)"; break 2; }
    local_t=$(date +%s); "seg_$s"; note "$s took $(( $(date +%s) - local_t )) s"
    inworld || { note "after $s NOT in the world: $(grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-90)"; tap R b; sleep 2; }
  done
  go "$HOME_AT"; state
done
note "done after $C cycles, $(( ($(date +%s) - T0) / 60 )) min"
sleep 65; kill "$SAMPLER" 2>/dev/null
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
