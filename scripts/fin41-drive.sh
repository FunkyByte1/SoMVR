#!/bin/bash
# fin41-drive.sh - build 41's finisher library (som-b41-finlib, docs/re-xr-pipeline.md 179; fin40-drive.sh's helpers):
# Practice > Finisher test with each type in turn (`finisher type NAME`, FN_TYPES, default all seven): the type's orc
# (the back stab's with its back turned; the downed and stunned orc set up by the test's own hit, then Talion put near
# it by `abil goto` - a test aid, the player is never moved; the counter's orc parried by the test guard), its ready and
# kind line, the real-hand line-up and the snap (a picture), the motion - a stab: a thrust along its way past its full
# depth, then back; a cut: the line-up's hand swung along the arc (the mod's `FINISHER cutpath`) - the kill (a picture),
# the try's readout (a picture). Boot with SOMVR_FINISHER_FLASH_S=1.5 in SOMVR_EXTRA.
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

TYPES=${FN_TYPES:-neck behead face back downed stunned counter}
wt() { local a0; a0=$(grep -c '^TRIGGER cmd=' "$L"); w "$1" 0.02; local i; for i in $(seq 1 40); do
  [ "$(grep -c '^TRIGGER cmd=' "$L")" -gt "$a0" ] && break; sleep 0.1; done; sleep "${2:-0.3}"; }
tready() { local tag=$1 r0=$2 tmo=${3:-14}; waitn '^FINISHER t=.* ready' "$r0" "$tmo" || { echo "  $tag: no ready"; return 1; }
  sleep 0.8; local pl; pl=$(grep -E '^FINISHER pose' "$L" | tail -1)
  P=$(echo "$pl" | sed -nE 's/.*hands test R ([-0-9. ]+) \(cm.*/\1/p'); WAY=$(echo "$pl" | sed -nE 's/.*the way in ([-0-9.]+ [-0-9.]+ [-0-9.]+) .*/\1/p')
  LU=$(grep -E '^FINISHER lineup' "$L" | tail -1 | sed -nE 's/.*hands test R ([-0-9. ]+)$/\1/p')
  grep -E '^FINISHER t=.* (kind|arc|place):' "$L" | tail -3 | cut -c1-330 | sed 's/^/    /'
  echo "  $tag: the outline's grip $P, the way in $WAY; the line-up $LU"; [ -n "$P" ] && [ -n "$WAY" ]; }
# one try of the current type: the line-up, the snap (a picture), the motion, the kill (a picture), the readout (a picture)
try41() { local tag=$1 n0 s0 t0 k0; n0=$(lines); s0=$(cnt '^FINISHER t=.* snap'); t0=$(cnt '^FINTEST t=.* try '); k0=$(cnt '^FINISHER t=.* kill:')
  lineup "$tag" "$s0" || { echo "  $tag: no snap"; fnsay "$n0" 8; return 1; }
  d "$tag-snap"; w "kills still off" 0.05
  local kl; kl=$(grep -E '^FINISHER t=.* kind:' "$L" | tail -1)
  if echo "$kl" | grep -q 'a cut along'; then
    local cp p1 p2; cp=$(grep -E '^FINISHER cutpath' "$L" | tail -1 | sed -nE 's/.*\): (.*)$/\1/p')
    p1=$(echo "$cp" | awk -F' [|] ' '{print $2}'); p2=$(echo "$cp" | awk -F' [|] ' '{print $3}')
    echo "  $tag: the cut through $p1, on to $p2"
    [ -n "$p1" ] || { echo "  $tag: no cutpath"; return 1; }
    wa "hands swing R $p1 ${FN_CUT_MS:-300}"
    waitn '^FINISHER t=.* kill:' "$k0" 4 && d "$tag-kill"
    wa "hands swing R $p2 150"
  else
    local tr cm A B; tr=$(grep -E '^FINISHER t=.* place:' "$L" | tail -1 | sed -nE 's/.* the stab ([0-9]+) cm of travel.*/\1/p')
    cm=$(( ${tr:-37} + 8 )); B=$(pose 0 $OFS); A=$(pose "$cm" $OFS)
    echo "  $tag: a thrust of $cm cm (the stab's travel ${tr:-?})"
    wa "hands swing R $A ${FN_STAB_MS:-180}"
    waitn '^FINISHER t=.* kill:' "$k0" 4 && d "$tag-kill"
    sleep 0.6; wa "hands swing R $B ${FN_STAB_MS:-180}"
  fi
  waitn '^FINTEST t=.* try ' "$t0" 12 || { echo "  $tag: the try did not end"; fnsay "$n0" 24; return 1; }
  sleep 1.0; d "$tag-readout"
  since "$n0" '^(FINTEST|FINISHER t=.* (kind|snap|swing|in:|deepest|kill:|back|out:|end|cancelled)|KILLS kill|ABIL t=.* goto)' | cut -c1-330 | sed 's/^/    /'
  grep -E "finisher test's readout" "$R/xr-layer.log" | tail -1 | cut -c1-260 | sed 's/^/    /'; }
echo "== fintest"; n0=$(lines)
wt "practice remove" 1.0
first=1
for ty in $TYPES; do
  echo "== $ty $(date +%T)"; n0=$(lines); r0=$(cnt '^FINISHER t=.* ready')
  wt "finisher type $ty" 0.4
  [ $first = 1 ] && { wt "practice fintest" 0.5; first=0; }
  since "$n0" '^(FINTEST|PRACTICE t=.* (spawn|refused))' | head -4 | cut -c1-260 | sed 's/^/    /'
  case $ty in
    downed|stunned)
      waitn '^FINTEST t=.* set-up' "$(cnt '^FINTEST t=.* set-up')" 14 || echo "  $ty: no set-up line"
      sleep 2.0; since "$n0" '^FINTEST t=.* set-up' | tail -1 | cut -c1-300 | sed 's/^/    /'
      [ $ty = downed ] && gd=${FN_GOTO_DOWN:-70} || gd=${FN_GOTO_STUN:-100}
      wt "abil goto $gd" 0.5; wt "execute face" 0.6
      since "$n0" '^ABIL t=.* goto' | tail -1 | cut -c1-260 | sed 's/^/    /' ;;
    counter) wt "fight guard path" 0.3; p0=$(cnt '^PARRY open'); k=0
      # (the orc circles: the head faced to it until the guard's block is a parry; then Talion put in reach of the
      # reeled orc - a test aid - and faced again: the counter's 3 s)
      while [ $k -lt 10 ]; do wt "execute face" 0.2; waitn '^PARRY open' "$p0" 2.5 && break; k=$((k + 1)); done
      grep -E '^PARRY open' "$L" | tail -1 | cut -c1-240 | sed 's/^/    /'
      wt "abil goto ${FN_GOTO_CTR:-115}" 0.2; wt "execute face" 0.2 ;;
  esac
  if tready "$ty" "$r0" 25; then [ $ty = counter ] && wt "fight guard off" 0.3; try41 "$ty"; fi
  [ $ty = counter ] && wt "fight guard off" 0.3
  sleep 1.5
done
n0=$(lines); x "btn R b 1"; sleep 0.4; x "btn R b 0"; sleep 2
since "$n0" '^FINTEST t=.* ended' | cut -c1-260 | sed 's/^/    /'
[ "$(cnt '^FINTEST t=.* ended')" -gt 0 ] || { echo "  B did not end it: the word"; w "practice fintest" 1; }
w "finisher status" 1; grep -E '^FINISHER status' "$L" | tail -1 | cut -c1-400
echo "$(date +%T) done"
[ -n "${FN_NOQUIT:-}" ] || echo quit >> "$R/cmd"
