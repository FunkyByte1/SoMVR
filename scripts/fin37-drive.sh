#!/bin/bash
# fin37-drive.sh - build 37's finisher (som-b37-finisher2, docs/re-xr-pipeline.md 171; fin33-drive.sh's helpers): the
# snap felt (its CLANK, the lock sound, the outline's flash, the pull onto the line), the kill at full depth, the way out
# by the hand at 1 m/s, the flat kill's sounds, the hint when no orc qualifies, Practice > Charge execution's orc.
# FN_STEPS = any of (in this order; default "hint good"):
#   hint     a far orc (10 m, its back turned), the execution charged (`verbs brand charge`): the hint's ring on him and
#            its reason (pictures); then Practice > Charge execution (`practice charge`): no orc within 4 m - one spawns
#            facing Talion 2 m ahead, the ready on it
#   good     build 33's good stab: the line-up, the snap (pictures), a firm thrust 50 cm in at 1 m/s (the kill at full
#            depth: pictures), then 50 cm back out at 1 m/s (the hand's way out: out within 0.4 s), the end
#   mid      the same with a 38 cm thrust at 1 m/s (the point past 6 cm, short of full depth: headless frames lose ~1/4 of
#            a short eased swing): the kill at the hand's turn, the way out from there
#   short    a 30 cm thrust (the point under 6 cm in headless): drawn back with no kill, the stab ends 2 s later - the
#            slow motion off
#   abort    a 17 cm thrust (the point ~2 cm in), drawn back, the hand left at rest: no kill; the stall ends it after
#            4 s - the slow motion off (boot 210501: a resting hand kept the world slow)
# Boot with SOMVR_FINISHER_TIMEOUT_S=10 (the driver's latency) and SOMVR_FINISHER_FLASH_S=1.5 (the flash held for the
# dump's latency) in SOMVR_EXTRA. Lines of build 33's driver kept below (its steps):
#   good     a fighting orc 150 cm ahead, the charge full (`verbs brand charge`): the outline (pictures), the right hand
#            put on it a few cm and 4 deg off (the point ~8 cm off: the generous snap), then the stab along the way in and back out at the
#            finisher's speed (40 cm in 400 ms, 1 m/s): the blood both ways, the orc's half, its death and the credit
#   poor     the same, the stab slow (40 cm in 1.4 s) and 35 deg off the line: the blade slower than the hand's best
#            (FN_POOR_DUMPS=0: no pictures of it - a dump is ~50 MB on disk)
#   timeout  ready, the hand left at rest: cancelled after SOMVR_FINISHER_TIMEOUT_S, the slow motion off, nothing spent
# Boot with SOMVR_FINISHER_TIMEOUT_S=10 in SOMVR_EXTRA (the driver's own latency: ~4 s from the ready to the hand on it).
# Pictures: scripts/xr_compose.py RUN -f FRAME (frames in RUN/fn-steps.txt). Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FN_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${FN_STEPS:-hint good short}
T0=$(date +%s)
R=${FN_RUN:-}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"
[ -n "${FN_SKIP_DRAW:-}" ] || sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $*"; sleep "${TS:-0.8}"; }
d() { [ "${NODUMP:-0}" = 1 ] && return 0; TS=0.1 x dump; sleep 0.5; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/fn-steps.txt"; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; sleep 1; done; }
# a hands word taken for sure (the Windows build reads the trigger file only every ~1-2 s: words written faster are
# lost - boot 210251's stab got 4 of its 8 swings): written, then waited for its TRIGGER line (3 s at most)
wa() { local a0; a0=$(grep -c '^TRIGGER cmd=hands' "$L"); w "$1" 0.02; local i; for i in $(seq 1 30); do
  [ "$(grep -c '^TRIGGER cmd=hands' "$L")" -gt "$a0" ] && break; sleep 0.1; done; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
waitn() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"; LREST="hands test L -22 -35 25 0 0 0"
# build 37: the Windows build reads the trigger file every 1-2 s - the draw's words each waited for (boot 205015 lost
# the grip: the game drew the sword by itself, the weighted sword never held it, the snapped ride never shown)
draw_slot() { wa "$SLOT"; sleep 0.5; wa "hands squeeze R 1"; sleep 0.6; wa "$OUT"; sleep 0.5; wa "hands squeeze R 0"; sleep 0.5; }
fnsay() { since "$1" '^(FINISHER|KILLS (start|kill|death|dead)|EXECUTE (ready|streak)|COMBAT death|STEALTH t=.* blood|PRACTICE|WARN)' | head -${2:-16} |
  cut -c1-460 | sed 's/^/    /'; }
if [ -z "${FN_SKIP_DRAW:-}" ]; then # (FN_SKIP_DRAW=1: a second driver on a run whose sword is drawn)
  x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
  grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
  wa "$LREST"; sleep 0.4; draw_slot; wa "$REST"; sleep 0.4
  grep -E "^SLOTS t=.*sword" "$L" | tail -2 | cut -c1-200 | sed "s/^/    /"; fi
grep -E '^(WARN|CONFIG finisher)' "$L" | head -6 | cut -c1-600
# ready: a fresh fighting orc at FN_GOTO cm, faced, the charge full; the pose line read (the outline's grip, the way in)
P=; WAY=
ready() { local tag=$1; S0=$(cnt '^FINISHER t=.* snap'); w "finisher off" 0.3; w "practice remove" 2; w "practice spawn infantry" 4; w "execute face" 1
  w "fight guard path" 0.4; z 2; w "verbs brand charge" 0.4
  # (the charge first, then Talion put 150 cm short of it and faced: the ready at once - a practice orc backs off to
  # 3 m within a second and the test's still orc too, boot 151121; its attack at the ready would cancel it)
  n0=$(lines); local k ok=0
  for k in 1 2 3; do r0=$(cnt '^FINISHER t=.* ready'); q0=$(cnt '^FINISHER pose'); c0=$(cnt '^FINISHER t=.* cancelled')
    [ $k -gt 1 ] && { echo "  $tag: again ($k)"; w "finisher off" 0.2; w "verbs brand charge" 0.4; }
    w "abil goto ${FN_GOTO:-150}" 0.2; w "execute face" 0.3; w "finisher on" 0.1 # (faced first: the outline is the world's)
    waitn '^FINISHER t=.* ready' "$r0" 6 || continue
    sleep 0.6; [ "$(cnt '^FINISHER t=.* cancelled')" -gt "$c0" ] && continue; ok=1; break; done
  [ $ok = 1 ] || { echo "  $tag: no FINISHER ready"; fnsay "$n0" 8; since "$n0" '^EXECUTE' | tail -3 | cut -c1-300; return 1; }
  waitn '^FINISHER pose' "$q0" 3; local pl; pl=$(grep -E '^FINISHER pose' "$L" | tail -1)
  P=$(echo "$pl" | sed -nE 's/.*hands test R ([-0-9. ]+) \(cm.*/\1/p'); WAY=$(echo "$pl" | sed -nE 's/.*the way in ([-0-9.]+ [-0-9.]+ [-0-9.]+) .*/\1/p')
  LU=$(grep -E '^FINISHER lineup' "$L" | tail -1 | sed -nE 's/.*hands test R ([-0-9. ]+)$/\1/p')
  echo "  $tag: the outline's grip $P, the way in $WAY; the line-up $LU"; d "$tag-outline"; [ -n "$P" ] && [ -n "$WAY" ]; }
# the line-up's miss (the generous snap): the grip 1 cm right and 1 cm low, turned 2 deg right (the point ~5 cm off)
OFS=${FN_OFS:-1 -1 0 2}
# the pose $P moved along the way by $1 cm, its x/y/z offset by $2 $3 $4 and its yaw (ry) by $5
pose() { echo "$P" | awk -v L="$1" -v ox="$2" -v oy="$3" -v oz="$4" -v ob="$5" -v W="$WAY" \
  '{ split(W, w, " "); printf "%.1f %.1f %.1f %.1f %.1f %.1f", $1 + w[1]*L + ox, $2 + w[2]*L + oy, $3 + w[3]*L + oz, $4, $5 + ob, $6 }'; }
# the line-up as a player does it: the hand to the outline's grip, then corrected by what the eye sees (the mod's
# `FINISHER lineup`: the hand that lays the drawn sword on it) less the generous miss
lineup() { local tag=$1 s0=$2 i lu
  [ -n "$LU" ] && P=$LU # (what the eye sees: the drawn sword laid on the outline, less the generous miss)
  w "hands test R $(pose -10 $OFS)" 0.3; w "hands swing R $(pose 0 $OFS) 400" 0.3 # (the hand comes in to it)
  for i in 1 2 3; do
    waitn '^FINISHER t=.* snap' "$s0" $([ $i = 1 ] && echo 3 || echo 2) && return 0
    local l0; l0=$(cnt '^FINISHER lineup'); w "finisher pose" 0.2; waitn '^FINISHER lineup' "$l0" 2 || return 1
    lu=$(grep -E '^FINISHER lineup' "$L" | tail -1 | sed -nE 's/.*hands test R ([-0-9. ]+)$/\1/p')
    grep -E '^FINISHER pose' "$L" | tail -1 | sed -nE 's/.*(the drawn point .*)/    try '"$i"': \1/p'
    [ -n "$lu" ] || return 1; P=$lu; w "hands swing R $(pose 0 $OFS) 300" 0.5; done
  waitn '^FINISHER t=.* snap' "$s0" 2; }
stab() { local tag=$1 ms=$2 off=$3; n0=$(lines); s0=$(cnt '^FINISHER t=.* snap')
  lineup "$tag" "$s0" || { echo "  $tag: no snap"; w "kills still off" 0.1; fnsay "$n0"; return 1; }
  d "$tag-snap"; local sl; sl=$(since "$n0" '^FINISHER t=.* snap' | tail -1); echo "$sl" | cut -c1-300 | sed 's/^/    /'
  w "kills still off" 0.05
  local W2; W2=$(echo "$sl" | sed -nE 's/.*the way in ([-0-9.]+ [-0-9.]+ [-0-9.]+) \(right.*/\1/p'); [ -n "$W2" ] && WAY=$W2
  # the stab from where the hand is: 40 cm along the way in (off: the line turned $off deg about the up axis), then back
  local A B; B=$(pose 0 $OFS); A=$(pose 40 $OFS); if [ "$off" != 0 ]; then
    A=$(echo "$B" | awk -v W="$WAY" -v a="$off" '{ split(W, w, " "); c = cos(a*3.14159/180); s = sin(a*3.14159/180);
      x = w[1]*c + w[3]*s; z = -w[1]*s + w[3]*c; printf "%.1f %.1f %.1f %.1f %.1f %.1f", $1 + x*40, $2 + w[2]*40, $3 + z*40, $4, $5, $6 }'); fi
  local i0 e0 o0 p0; i0=$(cnt '^FINISHER t=.* in:'); p0=$(cnt '^FINISHER t=.* deepest'); o0=$(cnt '^FINISHER t=.* out:'); e0=$(cnt '^FINISHER t=.* end')
  local k
  for k in 1 2 3 4; do
    wa "hands swing R $A $ms"
    [ $k = 1 ] && { waitn '^FINISHER t=.* in:' "$i0" 2 && d "$tag-in"; waitn '^FINISHER t=.* deepest' "$p0" 2 && d "$tag-deep"; }
    sleep "$(awk -v m="$ms" 'BEGIN{ printf "%.2f", m/1000 + 0.1 }')"
    wa "hands swing R $B $ms"
    [ $k = 1 ] && { waitn '^FINISHER t=.* out:' "$o0" 2 && d "$tag-out"; }
    sleep "$(awk -v m="$ms" 'BEGIN{ printf "%.2f", m/1000 + 0.1 }')"
    [ "$(cnt '^FINISHER t=.* end')" -gt "$e0" ] && break; done # (not done: once more, in and out)
  d "$tag-end"
  sleep 0.5; d "$tag-fall1"; sleep 1.0; d "$tag-fall2"; z 3; d "$tag-down"; w "$REST" 0.4; fnsay "$n0" 24
  since "$n0" '^(KILLS|COMBAT) .*(dead|death|credit|XP)' | head -4 | cut -c1-300 | sed 's/^/    /'; }
# build 37: the stab - in at 1 m/s ($3 cm in $2 ms) to the kill, then the hand back out the same way at 1 m/s
stab37() { local tag=$1 ms=${2:-500} cm=${3:-50}; n0=$(lines); s0=${S0:-$(cnt '^FINISHER t=.* snap')}
  # (the snap may come on the ready's first frame - the drawn sword already on the line: boot 205803)
  if [ "$(cnt '^FINISHER t=.* snap')" -gt "$s0" ]; then echo "  $tag: snapped at the ready"; n0=$(( $(grep -nE '^FINISHER t=.* snap' "$L" | tail -1 | cut -d: -f1) - 1 ))
  else lineup "$tag" "$s0" || { echo "  $tag: no snap"; w "kills still off" 0.1; fnsay "$n0"; return 1; }; fi
  d "$tag-snap"; local sl; sl=$(since "$n0" '^FINISHER t=.* snap' | tail -1); echo "$sl" | cut -c1-300 | sed 's/^/    /'
  w "kills still off" 0.05
  local W2; W2=$(echo "$sl" | sed -nE 's/.*the way in ([-0-9.]+ [-0-9.]+ [-0-9.]+) \(right.*/\1/p'); [ -n "$W2" ] && WAY=$W2
  local A B; B=$(pose 0 $OFS); A=$(pose "$cm" $OFS)
  local k0 o0 e0 k; k0=$(cnt '^FINISHER t=.* kill:'); o0=$(cnt '^FINISHER t=.* out:'); e0=$(cnt '^FINISHER t=.* end')
  wa "hands swing R $A $ms"; local early=0
  if waitn '^FINISHER t=.* kill:' "$k0" 2; then early=1; d "$tag-kill"; sleep 0.4
  else echo "  $tag: no kill at the thrust's end - it comes at the hand's turn"; fi
  local tr0; tr0=$(cnt '^TRIGGER cmd=hands'); wa "hands swing R $B $ms" # (the hand back out the same way: 1 m/s)
  [ $early = 1 ] || { waitn '^FINISHER t=.* kill:' "$k0" 2 && d "$tag-kill"; }
  waitn '^FINISHER t=.* end' "$e0" 4; d "$tag-out"
  sleep 0.5; d "$tag-fall1"; z 2; d "$tag-down"; w "$REST" 0.4; fnsay "$n0" 40
  local tt to; tt=$(tail -n +1 "$L" | grep -E '^TRIGGER cmd=hands' | sed -n "$((tr0 + 1))p" | grep -oE 't=[0-9.]+' | head -1 | cut -c3-)
  to=$(since "$n0" '^FINISHER t=.* out:' | tail -1 | grep -oE 't=[0-9.]+' | head -1 | cut -c3-)
  [ -n "$tt" ] && [ -n "$to" ] && echo "  $tag: the pull's word read t=$tt, the blade out t=$to: $(awk -v a="$tt" -v b="$to" 'BEGIN{ printf "%.3f", b - a }') s"
  since "$n0" '^(KILLS|COMBAT) .*(dead|death|credit|XP)' | head -4 | cut -c1-300 | sed 's/^/    /'; }
if has hint; then echo "== hint"; n0=$(lines); w "finisher on" 0.2; w "practice remove" 2; w "practice far infantry" 5
  w "fight guard path" 0.4; h0=$(cnt '^FINISHER t=.* hint on'); w "verbs brand charge" 0.4
  waitn '^FINISHER t=.* hint on' "$h0" 6 && { sleep 1.5; d hint-far; } || echo "  hint: no hint line"
  fnsay "$n0" 12; grep -E "the finisher's hint" "$R/xr-layer.log" | head -3 | cut -c1-300 | sed 's/^/    /'
  n0=$(lines); r0=$(cnt '^FINISHER t=.* ready'); w "practice charge" 1
  waitn '^FINISHER t=.* ready' "$r0" 12 && { sleep 0.8; d hint-ready; } || echo "  hint: no ready on the charge's orc"
  fnsay "$n0" 16; w "finisher off" 0.5; z 2; fi
has good && { echo "== good"; ready good && stab37 good "${FN_MS:-500}" 50; }
has mid && { echo "== mid"; ready mid && stab37 mid 380 38; }
# fast: the 22:09 session's thrust - 45 cm in 150 ms (3 m/s): the blade with the hand, the kill within the thrust
has fast && { echo "== fast"; ready fast && stab37 fast 150 45; }
# stall: 20 cm in (the point ~5 cm in), the hand left still: no kill - the stall ends it (never a kill by itself)
if has stall; then echo "== stall"; if ready stall; then n0=$(lines); s0=$S0
  [ "$(cnt '^FINISHER t=.* snap')" -gt "$s0" ] || lineup stall "$s0" || echo "  stall: no snap"
  k0=$(cnt '^KILLS kill'); wa "hands swing R $(pose 20 $OFS) 200"; z 8
  since "$n0" '^FINISHER t=.* (in:|cancelled|kill:|deepest)' | cut -c1-220 | sed 's/^/    /'
  echo "  stall: kills since $(( $(cnt '^KILLS kill') - k0 ))"; fi; fi
# turn / turn2: 34 / 32 cm (between the short's under-6 cm and the mid's full depth in headless frames): the turn's kill
has turn && { echo "== turn"; ready turn && stab37 turn 340 34; }
has turn2 && { echo "== turn2"; ready turn2 && stab37 turn2 320 32; }
has short && { echo "== short"; ready short && stab37 short 300 30; }
if has abort; then echo "== abort"; if ready abort; then n0=$(lines); s0=$S0
  [ "$(cnt '^FINISHER t=.* snap')" -gt "$s0" ] || lineup abort "$s0" || echo "  abort: no snap"
  B=$(pose 0 $OFS); wa "hands swing R $(pose 17 $OFS) 170"; sleep 1.2; wa "hands swing R $B 170"; z 7
  fnsay "$n0" 20; since "$n0" '^FINISHER t=.* (cancelled|slow motion)' | head -3 | cut -c1-200; fi; fi
w "finisher status" 1; grep -E '^FINISHER status' "$L" | tail -1 | cut -c1-500
w "kills status" 1; grep -E '^KILLS status' "$L" | tail -1 | cut -c1-500
echo "$(date +%T) done"
[ -n "${FN_NOQUIT:-}" ] || echo quit >> "$R/cmd"
