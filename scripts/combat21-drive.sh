#!/bin/bash
# combat21-drive.sh - build 21's combat round (handoff/HANDOFF-combat.md "build 21", docs/re-xr-pipeline.md 85): drives one
# headless boot (scripts/headless_vr.sh with scripts/headset-build21.env, SOMVR_HANDS_TEST=1, SOMVR_COMBAT_REACT=force,
# the layer's test file). C21_STEPS, in this order (default all):
#   shield  a shield orc from the front, the palm on its collar: no cue (its shield bashes a grab: the squeeze shows the
#           bash); then from behind it (`verbs behind 130`): the cue and a real grab, or its counter
#   cue     the grab cue: per orc kind (C21_KINDS, default infantry shield berserker) the game's own answers (`cue
#           probe`), the hand at rest = no cue, the left palm on its collar with the grip open (`verbs aim head`) = the
#           cue as the game answers (on, off in Talion's hit recoils), a real grab under it (`PC_Grab_Into`) for all but
#           the first kind; the palm away = off; a stealth target (the far orc, Talion 130 cm behind it): the cue and the
#           stealth grab; an infantry at 15 health probed
#   strike  the gaze Shadow Strike at a far unaware infantry with the sword on the back: `ssword off` (build 20c: the
#           game's draw kept on the back - the streak with no sword), then `ssword hand` (the sword to the hand, kept)
#   strikebow  the bow out (the test hands off, the layer's left hand at the shoulder), X = the strike: the bow to its
#           slot, the sword to the hand
#   turtle  a shield orc, 16 s each, no swing: no notify (the control), REACT's own notify (`react21 notify`), then the
#           cuts at it with `react21 defender 0.6` / `0`: the Block_Into starts against its own blows
# Ends with cue21_check.py over the log (the cue against every answer of the game).
# C21_RUN=RUN drives a running boot. Lines to read: VERBS cue / STRIKE sword / SLOTS / REACT / VERBS orc ... attack,
# c21-steps.txt (PASS/FAIL, the dumps' frames for xr_compose.py).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${C21_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${C21_STEPS:-shield cue strike strikebow turtle}
T0=$(date +%s)
R=${C21_RUN:-}
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
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -E "$2"; }
nsince() { since "$1" "$2" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
pass() { echo "$(date +%T) PASS $*" | tee -a "$R/c21-steps.txt"; }
fail() { echo "$(date +%T) FAIL $*" | tee -a "$R/c21-steps.txt"; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "dump $1 $f" | tee -a "$R/c21-steps.txt"; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest, low (out of every orc's reach)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
RZ=90
EDGE_A="hands test R 40 -8 35 0 -90 $RZ"; EDGE="hands swing R -20 -18 62 0 90 $RZ 300"; EDGE_B="hands swing R 40 -8 35 0 -90 $RZ 300"
ecut() { w "$EDGE_A" 0.3; w "$EDGE" 0.6; w "$EDGE_B" 0.6; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
odist() { tail -300 "$L" | grep -E '^FRONT score' | tail -1 | grep -oE ' dist=[0-9.]+' | head -1 | awk -F= '{printf "%d", $2 * 100}'; }
near() { local k d; for k in $(seq 1 8); do d=$(odist); [ -z "$d" ] && return 1; [ "$d" -le "${1:-120}" ] && return 0; face; w "stick 0 1 0 0.30" 0.5; done; return 1; }
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
spawn() { w "practice spawn $1" 2.5; face; }
# 0. the controls card away, the hands at rest, the sword on the back
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6; w "front keep 0" 0.3
grep -E '^(WARN|CONFIG verbs_cue|CONFIG strike_sword|REACT config)' "$L" | cut -c1-240
m c21-start 0.2

# the test head toward the nearest orc's chest (the gaze's test word; the strike's lookups)
head_to() { t "head 0 0 0 yaw $1 pitch $2 s ${3:-0.3}"; sleep 0.3; }
HY=0; HP=0
orc0() { local l n0; n0=$(cnt '^STRIKE gaze orc'); w "bow strike gaze orcs" 0.2
  for i in $(seq 1 12); do [ "$(cnt '^STRIKE gaze orc')" -gt "$n0" ] && break; sleep 0.25; done
  l=$(grep -E '^STRIKE gaze orc 0:' "$L" | tail -1)
  OB_Y=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\1/p')
  OB_P=$(echo "$l" | sed -nE 's/.*chest ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up.*/\2/p'); [ -n "$OB_Y" ]; }
look() { orc0 || return 1; HY=$(awk -v a="$HY" -v b="$OB_Y" 'BEGIN{printf "%.1f", a+b}'); HP=$(awk -v a="$HP" -v b="$OB_P" -v u="${1:-0}" 'BEGIN{v=a+b+u; if (v>40) v=40; if (v<-40) v=-40; printf "%.1f", v}'); head_to "$HY" "$HP" 0.25; }
cuestate() { grep -E '^VERBS cue t=.* (on|off):' "$L" | tail -1 | grep -q ' on:'; }
# a real grab while the cue is on: the left grip closed; the game's grab (Talion's attack PC_Grab_*) proves the cue
grab_check() { local n0 k; for k in $(seq 1 16); do cuestate && break; sleep 0.25; done
  cuestate || { echo "$(date +%T) $1: the cue not on: no grab tried"; return 1; }
  n0=$(lines); w "hands squeeze L 1" 1.2
  if since "$n0" "^VERBS t=.* Talion attack 'PC_Grab_Into" >/dev/null; then
    pass "$1: squeezed with the cue on: the game grabbed ($(since "$n0" "^VERBS t=.* Talion attack 'PC_Grab" | grep -oE "attack '[^']*'" | head -2 | tr '\n' ' '))"
  else fail "$1: squeezed with the cue on: no grab ($(since "$n0" '^VERBS t=.* (grab|squeeze)' | head -1 | cut -c1-160))"; fi
  w "hands squeeze L 0" 1.0; }
if has shield; then
  echo "== shield: the cue in front of a raised shield, then from behind it"
  w "practice remove" 2; HY=0; HP=0; head_to 0 0; spawn shield; near 140; face
  n0=$(lines); look -10; w "verbs aim head" 0.2; sleep 3
  echo "$(date +%T) front: answers $(since "$n0" '^VERBS cue t=.*in reach' | grep -oE 'would (not )?grab it: [^>]*-> [^(]*' | sort | uniq -c | tr '\n' '|')"
  [ "$(nsince "$n0" '^VERBS cue t=.* on:')" -eq 0 ] && pass "shield front: no cue ($(since "$n0" 'its shield is up' | head -1 | grep -oE 'its shield is up[^)]*'))" \
    || fail "shield front: a cue: $(since "$n0" '^VERBS cue t=.* on:' | head -1 | cut -c1-160)"
  m "shield-front" 0
  n0=$(lines); w "hands squeeze L 1" 1.5
  since "$n0" "attack 'AI_Defender_ShieldBash_GrabAttempt'|^VERBS cue t=.*a counter|Talion attack 'PC_(Grab|Recoil)" | head -4 | cut -c1-200
  w "hands squeeze L 0" 1.0; w "verbs aim off" 0.2; w "$LREST" 2.0
  echo "== shield: from behind"
  n0=$(lines); w "verbs behind 130" 1.2; w "execute face" 0.8; HY=0; HP=0; head_to 0 0; look -10; w "verbs aim head" 0.2
  for i in $(seq 1 12); do cuestate && break; sleep 0.25; done
  echo "$(date +%T) behind: answers $(since "$n0" '^VERBS cue t=.*in reach' | grep -oE 'would (not )?grab it: [^>]*-> [^(]*' | sort | uniq -c | tr '\n' '|')"
  if cuestate; then d "cue-shield-behind"; m "cue-shield-behind" 0; grab_check "shield behind"
  else echo "$(date +%T) shield behind: no cue"; n0=$(lines); w "hands squeeze L 1" 1.5
    since "$n0" "attack 'AI_Defender_ShieldBash_GrabAttempt'|^VERBS cue t=.*a counter|Talion attack 'PC_(Grab|Recoil)" | head -4 | cut -c1-200; w "hands squeeze L 0" 1.0; fi
  w "verbs aim off" 0.2; w "$LREST" 1.0; HY=0; HP=0; head_to 0 0
  w "practice remove" 2
fi

if has cue; then
  for kind in ${C21_KINDS:-infantry shield berserker}; do
    echo "== cue: $kind"
    w "practice remove" 2; HY=0; HP=0; head_to 0 0; spawn "$kind"; near 140; face
    w "cue probe" 1.2; since 0 '^VERBS cue probe' | tail -1 | cut -c1-900
    n0=$(lines); w "$LREST" 0.3; sleep 1.5
    [ "$(nsince "$n0" '^VERBS cue t=.* on:')" -eq 0 ] && pass "cue $kind: the hand at rest (out of reach): no cue" \
      || fail "cue $kind: a cue with the hand at rest: $(since "$n0" '^VERBS cue t=.* on:' | head -1 | cut -c1-200)"
    n0=$(lines); look -10; w "verbs aim head" 0.2
    for i in $(seq 1 12); do cuestate && break; sleep 0.25; done
    cuestate && { d "cue-$kind-on"; m "cue-$kind-on" 0; }
    sleep 2
    echo "$(date +%T) $kind: answers yes $(nsince "$n0" '^VERBS cue t=.*would grab it') no $(nsince "$n0" '^VERBS cue t=.*would not grab') ($(since "$n0" '^VERBS cue t=.*would not grab' | grep -oE 'refuses the node now|no attack of its pools[^)]*|a fail move' | sort | uniq -c | tr '\n' ' ')), cue on $(nsince "$n0" '^VERBS cue t=.* on:') off $(nsince "$n0" '^VERBS cue t=.* off:')"
    [ "$kind" != infantry ] && grab_check "cue $kind"
    n0=$(lines); w "verbs aim off" 0.2; w "$LREST" 1.0
    cuestate && fail "cue $kind: still on with the palm away" || pass "cue $kind: the palm away: no cue"
    HY=0; HP=0; head_to 0 0
  done
  echo "== cue: a stealth target (the far unaware infantry, Talion 130 cm behind it)"
  w "practice remove" 2; w "practice far infantry" 3; sleep 1.5; w "verbs behind 130" 1.5; w "execute face" 1.0
  w "cue probe" 1.2; since 0 '^VERBS cue probe' | tail -1 | cut -c1-900
  n0=$(lines); look -15; w "verbs aim head" 0.3
  for i in $(seq 1 12); do cuestate && break; sleep 0.25; done
  cuestate && { pass "cue stealth: on: $(since "$n0" '^VERBS cue t=.* on:' | head -1 | cut -c1-200)"; d "cue-stealth-on"; m "cue-stealth-on" 0; } || fail "cue stealth: no cue ($(since "$n0" '^VERBS cue t=.*in reach' | tail -1 | cut -c1-220))"
  grab_check "cue stealth"
  w "verbs aim off" 0.2; w "$LREST" 1.0; HY=0; HP=0; head_to 0 0
  echo "== cue: an infantry at 15 health"
  w "practice remove" 2; spawn infantry; near 140; face; w "fight health 15" 0.6; w "cue probe" 1.2
  since 0 '^VERBS cue probe' | tail -1 | cut -c1-900
  w "practice remove" 2
fi

if has strike; then
  w "bow arrows 40" 1
  markednow() { since "$1" '^STRIKE t=.* gaze: (marked|the mark moved|unmarked)' | tail -1 | grep -qv 'unmarked'; }
  lookm() { local i k n0=$1; markednow "$n0" && return 0
    for i in 1 2 3 4; do look || sleep 0.5; for k in 1 2 3 4; do markednow "$n0" && return 0; sleep 0.25; done; done; return 1; }
  for mode in off hand; do
    echo "== strike: ssword $mode"
    w "ssword $mode" 0.5
    if drawn; then draw_slot; w "$REST" 0.5; fi # the sword put back (the slot again): on the back as in the user's mark 6
    w "practice remove" 2; HY=0; HP=0; head_to 0 0; w "practice far infantry" 3; sleep 1.5
    n0=$(lines); t "btn L b 1"; waitfor '^VISION t=.* the flag 0 -> 1' 3; sleep 0.3
    lookm "$n0" || fail "strike $mode: no gaze mark"
    n1=$(lines); T_S=0.3 t "btn R a 1"; T_S=0.05 t "btn R a 0"
    waitfor "^VERBS t=.* Talion attack '[^']*WraithDash" 4; T_S=0.05 d "strike-$mode-start"
    sleep 0.6; T_S=0.05 d "strike-$mode-kd"; m "strike-$mode-kd" 0
    t "btn L b 0"; sleep 2.5; m "strike-$mode-after" 0
    st=$(since "$n1" "^VERBS t=.* Talion attack '[^']*WraithDash" | head -1)
    [ -n "$st" ] && pass "strike $mode: the game's Shadow Strike ($(echo "$st" | grep -oE "attack '[^']*'"))" || fail "strike $mode: no WraithDash attack"
    since "$n1" '^(STRIKE sword|SLOTS t=.* sword|BOW t=.* the Shadow Strike)' | head -6 | cut -c1-230
    if [ "$mode" = off ]; then
      since "$n1" '^SLOTS t=.* sword: the game drew it: kept on the back' >/dev/null && ! since "$n1" '^STRIKE sword t=.* after the strike' >/dev/null \
        && pass "strike off (build 20c): the game's draw kept on the back" || fail "strike off: not kept on the back"
    else
      since "$n1" '^STRIKE sword t=.* after the strike' >/dev/null && pass "strike hand: $(since "$n1" '^STRIKE sword t=.* after the strike' | head -1 | cut -c1-200)" \
        || fail "strike hand: the sword not taken"
      sleep 3; drawn && pass "strike hand: the sword still in the hand 6 s after" || fail "strike hand: not in the hand after"
    fi
    HY=0; HP=0; head_to 0 0
  done
  HY=0; HP=0; head_to 0 0
  w "practice remove" 2
fi

if has strikebow; then
  echo "== strike: with the bow out (X), ssword hand"
  w "ssword hand" 0.4; w "bow arrows 40" 0.6; w "hands test off" 0.6
  SLOT_L="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
  if drawn; then draw_slot; w "$REST" 0.5; fi
  w "practice remove" 2; HY=0; HP=0; head_to 0 0; w "practice far infantry" 3; sleep 1.5
  t "$SLOT_L"; sleep 0.3; T_S=0.4 t "btn L squeeze 1"; t "$BOW_STRAIGHT"; sleep 1
  if grep -E '^BOW .* (enter |exit: )' "$L" | tail -1 | grep -q 'enter '; then
    look 0; sleep 1; n1=$(lines); T_S=0.3 t "btn L a 1"; T_S=0.05 t "btn L a 0"
    waitfor "^VERBS t=.* Talion attack '[^']*WraithDash" 4; T_S=0.05 d "strike-bow-start"; sleep 0.6; T_S=0.05 d "strike-bow-kd"
    m "strike-bow-kd" 0; T_S=0.3 t "btn L squeeze 0"; sleep 2
    since "$n1" '^(STRIKE (t=.* press|sword)|BOW t=.* (the Shadow Strike|exit)|SLOTS t=.* sword)' | head -8 | cut -c1-220
    since "$n1" '^BOW t=.* the Shadow Strike: the bow to its slot' >/dev/null && pass "strike bow: the bow to its slot" || fail "strike bow: the bow not put away by the strike"
    since "$n1" '^STRIKE sword t=.* after the strike' >/dev/null && pass "strike bow: $(since "$n1" '^STRIKE sword t=.* after the strike' | head -1 | cut -c1-160)" || fail "strike bow: the sword not taken"
    sleep 2; drawn && pass "strike bow: the sword in the hand after" || fail "strike bow: not in the hand after"
  else fail "strike bow: the bow did not come out (the staging)"; T_S=0.3 t "btn L squeeze 0"; fi
  t "hand L head -22 -40 15 0 0 0"; HY=0; HP=0; head_to 0 0
  w "practice remove" 2; w "$LREST" 0.4; w "$REST" 0.4
fi

if has turtle; then
  drawn || draw_slot
  w "$REST" 0.6
  watch() { local n1=$(lines) ts=$(date +%s); while [ $(( $(date +%s) - ts )) -lt 16 ]; do sleep 4; w "practice health" 0.2; done
    local bi at bl
    bi=$(since "$n1" "^VERBS t=.* orc .*attack 'AI_Defender_Block_Into'" | wc -l)
    at=$(since "$n1" "^VERBS t=.* orc .*attack 'AI_Defender_(Cone|Radial|Heavy|Directional|ShieldBash)" | wc -l)
    echo "$(date +%T) RESULT turtle $1: in 16 s with no swing: Block_Into starts $bi, its blows $at" | tee -a "$R/c21-steps.txt"; }
  echo "== turtle: a shield orc, no notify (the control)"
  w "practice health" 0.3; w "react21 defender 0" 0.4; w "practice remove" 2; spawn shield; sleep 1; watch "control (no notify)"
  echo "== turtle: REACT's own notify on it (the call build 20c made at a won roll)"
  w "practice remove" 2; spawn shield; n0=$(lines); w "react21 notify" 0.3; waitfor '^REACT21 t=.* notify' 5
  since "$n0" '^(REACT21|REACT notify)' | head -3 | cut -c1-240; watch "notified (build 20c's call)"
  for v in 0.6 0; do
    echo "== turtle: cuts at it during its approach, react21 defender $v (REACT force)"
    w "practice health" 0.3; w "react21 defender $v" 0.4; w "practice remove" 2; spawn shield; n0=$(lines)
    for k in $(seq 1 6); do
      since "$n0" '^REACT (notify|direct (roll|toward) .*(out of its own attack|chance is 0))' >/dev/null && break
      near 150; face; ecut; w "$REST" 0.3; sleep 0.3
    done
    since "$n0" '^REACT (direct|notify)' | grep -v 'in its own attack' | tail -2 | cut -c1-240
    w "stick 0 -1 0 0.6" 0.8; w "$REST" 0.4
    watch "the cuts, defender chance $v: notifies $(nsince "$n0" '^REACT notify')"
  done
  w "practice remove" 2
fi
w "cue status" 0.5
python3 "$(dirname "$0")/cue21_check.py" "$L" | tee -a "$R/c21-steps.txt"
echo "$(date +%T) done"
echo "$(date +%T) quit"; echo quit >> "$R/cmd"
