#!/bin/bash
# hands31-drive.sh - build 31's free hand on an orc (som-b31-hands; docs/re-xr-pipeline.md 135): drives one headless boot
# (scripts/headless_vr.sh with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the layer's test file). H31_STEPS, in order:
#   shove  the practice infantry shoved by each recoil type in H31_TYPES (`parry shove TYPE`: Talion's push at the nearest
#          orc, 3 m/s) - each type's `COMBAT reel ... after a shove` (how far it went); then the left hand's real pushes
#          into its chest (a soft one 1.8 m/s, a hard one 3.7 m/s: SOMVR_SHOVE_ANY, the far lists); then the shield orc
#   hpush  the left hand's real pushes into a still orc's back (the far unaware infantry, from the stealth stand-off at
#          118 cm, the arm out 78 cm): a soft one (55 cm in 300 ms) and a hard one (55 cm in 150 ms) - SOMVR_SHOVE_ANY, the far lists by the speed
#   grab   the far unaware infantry, the crouched sneak to the stand-off (118 cm), the left hand at rest (the faint glow:
#          the dumps), reached out toward its collar (a real pose, 58 cm ahead), the squeeze and the trigger 50 ms after it
#          (the fist): the hold with no drain, held 12 s (SOMVR_VERBS_GRAB_MAX_S 10: let go at 10 s)
#   kill   the same grab, then a horizontal cut of the sword across the held orc (a fan until it lands): the held kill
#   push   the same grab, then the holding hand pushed at the orc (55 cm in 150 ms): let go, the shove
#   vault  the vault by the hands (SOMVR_VAULT_HANDS): `story invisible on` (the AI blind: the far orc stands still), Talion
#          put 80 cm in front of it (`verbs front`; from behind its crown is 100-113 cm off: out of an arm's reach), the left hand at its crown (where the `OVAULT ... cue` line says it is), the squeeze, a
#          push down and back (the hold: the body up 1:1), then the throw (down 50 and back 50 cm in 150 ms, the grip
#          opened at 80 % of it); the flight's every frame in `OVAULT ... flight` lines, dumps through it; H31_VAULTS times
#   chord  the same grab, the Touch's A (the interrogation), its choices up, then the bug mark's chord B + Y held 0.7 s:
#          a mark and no Eliminate; then the sword's stab = its Eliminate (the player's kill)
# H31_RUN=RUN drives a running boot. fd-steps.txt: the dumps' frames for xr_compose.py.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${H31_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${H31_STEPS:-shove hpush grab kill push chord vault}
TYPES=${H31_TYPES:-Heavy_Straight Knockback_Straight Block_Breaker Character_KnockedBack Knockdown_ShoulderRam}
T0=$(date +%s)
R=${H31_RUN:-}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/fd-steps.txt"; }
last() { grep -E "$1" "$R/somvr.log" | tail -${2:-1} | cut -c1-${3:-330}; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest
LBACK="hands test L -12 -45 25 0 0 0"    # the left hand drawn back low (the belly: not the face's pull)
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6
draw_slot; w "$REST" 0.6; w "front keep 0" 0.3
reel() { waitfor '^COMBAT reel orc=.* after a shove' ${1:-8} && last '^COMBAT reel orc=.* after a shove' 1 420 | sed -E 's/; every 0.25 s.*//'; }
if has shove; then
  echo "== shove"; w "practice spawn infantry" 3
  for T in $TYPES; do
    w "verbs behind 110" 0.8; w "execute face" 1.0
    echo "$(date +%T) TYPE $T"; w "parry shove $T" 0.4; last '^PARRY shove orc=' 1 300; reel 8 || echo "$(date +%T) no reel line"
    sleep 1.5
  done
  dp shove-before; for S in "0 -18 75 0 0 0 300" "0 -18 75 0 0 0 150"; do   # the soft (1.8 m/s) and the hard (3.7 m/s) push
    w "verbs behind 70" 0.8; w "execute face" 1.0; w "$LBACK" 0.5
    n0=$(cnt '^PARRY shove orc='); w "hands swing L $S" 0.6
    [ "$(cnt '^PARRY shove orc=')" -gt "$n0" ] && { last '^PARRY shove orc=' 1 300; dp "shove-push-${S##* }"; sleep 1; dp "shove-push-${S##* }b"; reel 8; } || { echo "$(date +%T) the push met nothing"; last '^PARRY push refused' 1 300; }
    w "$LREST" 1.0
  done
  w "practice remove" 2; w "practice spawn shield" 3
  for T in Heavy_Straight Knockback_Straight Block_Breaker; do
    w "verbs behind 110" 0.8; w "execute face" 1.0
    echo "$(date +%T) SHIELD TYPE $T"; w "parry shove $T" 0.4; last '^PARRY shove orc=' 1 300; reel 8 || echo "$(date +%T) no reel line"
    sleep 1.5
  done
  w "practice remove" 2
fi
# the stealth approach (finish20's): the far unaware orc, behind it at 220 cm, the crouched walk to the stand-off
noticed() { [ "$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log")" -gt "$N0" ]; }
NR=0
round() { echo "== $1"; NR=$((NR + 1)); [ "$NR" -gt 1 ] && { w "practice back" 6; w "practice go" 7; }
  N0=$(grep -cE '^STEALTH t=.* aware orc .*: (0|-2) -> 1' "$R/somvr.log"); w "practice far infantry" 3
  if last '^PRACTICE t=.*spawn ' | grep -q 'FAILED\|refused'; then w "practice back" 6; w "practice go" 7; w "practice far infantry" 3; fi
  w "verbs behind 220" 0.8; w "execute face" 1.0
  if ! last '^EXECUTE where ' | grep -qE 'dist (1[5-9][0-9]|2[0-9][0-9]) cm'; then echo "$(date +%T) placed again"; w "verbs behind 220" 0.8; w "execute face" 1.0; fi; }
sneak_to() { local r0 i; r0=$(cnt '^STEALTH t=.* stand-off: at rest')
  x "head 0 -50 0 s 0.5" 1.2; waitfor '^CROUCH (game|duck) on' 3; w "stealth chase 0.6 60" 0.3
  for i in $(seq 1 120); do userin && bail "the user is in VR"; noticed && { echo "$(date +%T) noticed"; break; }
    [ "$(cnt '^STEALTH t=.* stand-off: at rest')" -gt "$r0" ] && break; sleep 0.25; done
  w "stealth chase 0" 0.3; sleep 1.0; last '^STEALTH t=.* stand-off: at rest' 1 200; }
endr() { w "verbs aim off" 0.3; w "hands trigger L -1" 0.3; w "hands squeeze L 0" 0.4; w "$LREST" 0.3; x "head 0 0 0 s 0.4" 0.6; w "practice remove" 2; }
# the reach: the left hand out toward the collar (58 cm ahead, 20 below the eyes), then the squeeze; the fist's trigger
reach_grab() { local g0; w "$LREST" 1.5; dp "$1-near"; m "$1-near" 0.1
  w "hands test L 0 -20 58 0 0 0" 1.0; dp "$1-reach"; last '^VERBS cue t=.* (on|near):' 2 240
  g0=$(cnt '^VERBS t=.* grab: the left hand'); w "hands squeeze L 1" 0.3; [ "${2:-}" = fist ] && w "hands trigger L 1" 0.6 || sleep 0.6
  if [ "$(cnt '^VERBS t=.* grab: the left hand')" -le "$g0" ]; then echo "$(date +%T) the reach missed: the aid (verbs aim reach)"
    last '^VERBS t=.* squeeze' 1 300; w "hands squeeze L 0" 0.6; w "verbs aim reach" 0.8; w "hands squeeze L 1" 0.3; waitfor '^VERBS t=.* grab: the left hand' 3; w "verbs aim off" 0.3; fi
  [ "$(cnt '^VERBS t=.* grab: the left hand')" -gt "$g0" ] || { echo "$(date +%T) no grab"; return 1; }
  last '^VERBS t=.* grab: the left hand' 1 260; }
if has hpush; then
  for S in 300 150; do
    round "hpush-$S"; sneak_to; w "execute face" 0.6; w "$LBACK" 0.4   # (a teleport into its 108 cm alerts it: it walks off)
    n0=$(cnt '^PARRY shove orc='); w "hands swing L 0 -25 78 0 0 0 $S" 0.6
    if [ "$(cnt '^PARRY shove orc=')" -gt "$n0" ]; then last '^PARRY shove orc=' 1 330; dp "hpush-$S-a"; sleep 0.5; dp "hpush-$S-b"; sleep 0.5; dp "hpush-$S-c"; reel 8
    else echo "$(date +%T) the push met nothing"; last '^PARRY push refused' 2 330; fi
    w "$LREST" 0.5; w "practice remove" 2
  done
fi
if has grab; then
  round grab; sneak_to; reach_grab grab fist || endr
  if [ "$(cnt '^VERBS t=.* grab: the left hand')" -gt 0 ]; then
    last '^HOLD31 t=.* the fist' 1 220; sleep 3; dp grab-held3; sleep 4; dp grab-held7; m grab-held 0.1
    waitfor '^VERBS t=.* release after' 8 && last '^VERBS t=.* release after' 1 260
    grep -E "^VERBS t=.* Talion attack|^VERBS t=.* (wraith|done after|release after)" "$R/somvr.log" | tail -8 | cut -c1-200
  fi
  endr
fi
if has kill; then
  round kill; sneak_to
  if reach_grab kill; then sleep 1.5; dp kill-held
    k0=$(cnt '^HOLD31 kill')
    for xy in 0,-25 10,-35 -10,-20 0,-45; do x0=${xy%,*}; y0=${xy#*,}   # a horizontal cut across the held orc, right to left
      w "hands test R $((x0 + 45)) $y0 40 0 -90 90" 0.6; w "hands swing R $((x0 - 30)) $y0 55 0 90 90 300" 0.3; dp kill-cut; sleep 0.5
      [ "$(cnt '^HOLD31 kill')" -gt "$k0" ] && break; done
    last '^HOLD31 (kill|t=)' 2 330; sleep 0.5; dp kill-after1; sleep 1; dp kill-after2; m kill-after 0.1
  fi
  endr
fi
if has push; then
  round push; sneak_to
  if reach_grab push; then sleep 1.5; w "$LBACK" 0.5; dp push-held
    w "hands swing L 0 -18 75 0 0 0 150" 0.2; waitfor '^HOLD31 t=.* the push' 2; last '^HOLD31 t=.* the push' 1 260
    waitfor '^PARRY shove orc=.*the held orc' 3 && last '^PARRY shove orc=.*the held orc' 1 300; last '^PARRY shove t=.*hold' 1 200
    dp push-after1; sleep 1; dp push-after2; reel 6
  fi
  endr
fi
if has chord; then
  round chord; sneak_to
  if reach_grab chord; then sleep 1.0
    for i in 1 2 3; do waitfor "^VERBS t=.* prompt up: action 121 .*Domination_Subdue" 3; x "btn R a 1" 0.8; x "btn R a 0" 0.5
      waitfor "^VERBS t=.* prompt up: action 16[34] " 6 && { echo "$(date +%T) the choices up"; break; }; done
    sleep 1.5; dp chord-choices; k0=$(cnt "Intrgtn_Kill"); b0=$(cnt "button: the Touch's B"); mk0=$(cnt '^MARK #')
    printf 'btn R b 1\nbtn L b 1\n' > "$R/xrtest.txt"; echo "$(date +%T) the chord B + Y"; sleep 0.8
    printf 'btn R b 0\nbtn L b 0\n' > "$R/xrtest.txt"; sleep 4
    echo "$(date +%T) CHORD: marks +$(( $(cnt '^MARK #') - mk0 )), the Touch's B taps +$(( $(cnt "button: the Touch's B") - b0 )), the game's kill +$(( $(cnt "Intrgtn_Kill") - k0 ))"
    k1=$(cnt 'kill: the blade in the interrogated orc')
    for xy in 25,-25 25,-45 10,-30 40,-30; do x0=${xy%,*}; y0=${xy#*,}
      w "hands test R $((x0 - 10)) $y0 -30 -15 0 0" 0.6; w "hands swing R $x0 $((y0 - 10)) 75 -15 0 0 800" 1.2
      [ "$(cnt 'kill: the blade in the interrogated orc')" -gt "$k1" ] && break; done
    last '^VERBS t=.*(kill: the blade in the interrogated|the blade on the interrogated)' 1 260; sleep 3; dp chord-killed
  fi
  endr
fi
# build 33 (som-b33-knife): the knife from the left hip by the left hand (held while the grip is; let go = back on the
# belt), with the sword out too; the shove from 30 cm further; a sword-out reach for a grab that is no shove
KHIP="hands test L -18 -62 -5 0 0 0"   # the left hand at the left hip (the slot: -18,-62,-5 from the head)
KUP="hands test L -20 -25 40 -60 0 0"  # the knife held up in front, the blade forward
if has knife; then
  echo "== knife"; w "$LREST" 0.8
  for pass in alone sword; do
    if [ "$pass" = alone ]; then w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.6; w "$REST" 0.6   # the sword away
    else echo "== knife with the sword out"; draw_slot; w "$REST" 0.6; fi
    k0=$(cnt '^SLOTS t=.*dagger'); w "$KHIP" 1.0; w "hands squeeze L 1" 1.2
    last '^SLOTS t=.*dagger' 3 260
    w "$KUP" 1.2; dp "knife-$pass-held"; sleep 0.8; dp "knife-$pass-held2"
    w "hands squeeze L 0" 1.2; last '^SLOTS t=.*dagger' 2 260   # let go away from the belt: back on it
    dp "knife-$pass-back"
    w "$KHIP" 1.0; w "hands squeeze L 1" 1.2; w "$KUP" 1.0; w "$KHIP" 1.0; w "hands squeeze L 0" 1.2  # drawn, carried back, let go there
    last '^SLOTS t=.*dagger' 2 260
    echo "$(date +%T) KNIFE $pass: dagger lines +$(( $(cnt '^SLOTS t=.*dagger') - k0 ))"
    w "$LREST" 0.8
  done
fi
if has srange; then   # the shove from the stand-off with the arm out 62 cm (the palm ~60 cm from its axis: past build 31's 50)
  round srange; sneak_to; w "execute face" 0.6; w "$LBACK" 0.4
  n0=$(cnt '^PARRY shove orc='); w "hands swing L 0 -25 62 0 0 0 130" 0.6
  [ "$(cnt '^PARRY shove orc=')" -gt "$n0" ] && { last '^PARRY shove orc=' 1 330; dp srange-a; sleep 0.6; dp srange-b; reel 8; } || { echo "$(date +%T) the push met nothing"; last '^PARRY push' 2 330; }
  endr
fi
if has sgrab; then    # the sword out, the left hand reaches for the orc at ~2.4 m/s and closes: a grab, no shove
  round sgrab; sneak_to; w "execute face" 0.6; w "$LBACK" 0.4
  n0=$(cnt '^PARRY shove orc='); g0=$(cnt '^VERBS t=.* grab: the left hand')
  w "hands swing L 0 -20 60 0 0 0 230" 0.7; w "hands squeeze L 1" 1.0   # (0.7 s: one word at a time)
  echo "$(date +%T) SGRAB: shoves +$(( $(cnt '^PARRY shove orc=') - n0 )), grabs +$(( $(cnt '^VERBS t=.* grab: the left hand') - g0 ))"
  last '^PARRY push t=.*grab cue' 1 260; last '^VERBS t=.* grab: the left hand' 1 260; dp sgrab-held
  endr
fi
# build 33 (som-b33-herbs): the herb near the practice meadow ('Collectible_Flower_O' at 31314 2190 -25167): Talion hurt,
# taken there, crouched, the left hand onto the herb (the cue line's herb and palm: the eye's correction), gripped, to the
# mouth (the whistle's pose) - eaten: the game's pick; the health before/after
HERB_AT=${H31_HERB_AT:-31314 2190 -25117}
if has herb; then
  echo "== herb"; w "practice hurt 50" 1.5; w "practice go $HERB_AT" 1; waitfor '^PRACTICE t=.*(arrived|there)' 15; sleep 2
  x "head 0 -60 0 pitch -60 s 0.5" 1.5; w "$LREST" 1.5   # crouched, looking down at the herb
  waitfor '^HERB t=.* cue: ' 4; c=$(last '^HERB t=.* cue: ' 1 400); echo "$c"
  if [ -z "$c" ]; then echo "$(date +%T) no herb cue"; last '^USE t=.*pick prompt' 2 200
  else
    for k in 1 2 3; do
      H=$(echo "$c" | sed -E 's/.*the herb ([-+][0-9.]+) right ([-+][0-9.]+) up ([-+][0-9.]+) ahead.*/\1 \2 \3/' | tr -d +)
      P=$(echo "$c" | sed -E 's/.*the palm ([-+][0-9.]+) ([-+][0-9.]+) ([-+][0-9.]+).*/\1 \2 \3/' | tr -d +)
      echo "$c" | grep -q 'in reach' && break
      set -- $H $P; [ $k = 1 ] && { X=$1; Y=$2; Z=$3; } || { X=$(printf '%.0f' "$(echo "$X + $1 - $4" | bc)"); Y=$(printf '%.0f' "$(echo "$Y + $2 - $5" | bc)"); Z=$(printf '%.0f' "$(echo "$Z + $3 - $6" | bc)"); }
      X=$(printf '%.0f' "$X"); Y=$(printf '%.0f' "$Y"); Z=$(printf '%.0f' "$Z")
      w "hands test L $X $Y $Z 0 0 0" 1.5; waitfor '^HERB t=.* cue: ' 3; c=$(last '^HERB t=.* cue: ' 1 400); echo "$c"
    done
    dp herb-reach; t0=$(cnt '^HERB t=.* taken'); w "hands squeeze L 1" 0.8
    [ "$(cnt '^HERB t=.* taken')" -gt "$t0" ] && last '^HERB t=.* taken' 1 220 || echo "$(date +%T) not taken"
    dp herb-held; x "head 0 0 0 s 0.5" 1.0; w "hands test L 0 -9 9 0 0 0" 0.4
    for k in 1 2 3; do echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.35; done
    waitfor "^HERB t=.* after the game's pick" 9; grep -E "^HERB t=.* (eaten|after the game's pick|dropped)" "$R/somvr.log" | tail -3 | cut -c1-260
    last '^USE t=.*pick prompt down' 1 200; w "hands squeeze L 0" 0.5; w "$LREST" 0.5; dp herb-after
  fi
fi
if has phase; then   # (build 31, item 4's test: does the wraith phase's way - the stock evade, Combat+0x56c 0 - pass an orc?)
  w "practice spawn infantry" 3
  for N in 1 2 3; do
    w "verbs front 200" 0.8; w "execute face" 1.0; x "head 0 0 0 s 0.4" 0.5
    s0=$(cnt '^STEP t=.* end:'); x "btn R a 1" 0.3; x "btn R a 0" 0.3
    for k in 1 2 3; do echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.3; done
    waitfor '^STEP t=.* end:' 3; sleep 0.5; dp "phase-$N-after"
    grep -E "^STEP t=.*(phase|collision|end:)" "$R/somvr.log" | tail -4 | cut -c1-300
    [ "$(cnt '^STEP t=.* end:')" -gt "$s0" ] || echo "$(date +%T) no phase"
    sleep 2
  done
  w "practice remove" 2
fi
if has vault; then
  for N in $(seq 1 ${H31_VAULTS:-2}); do
    if [ -n "${H31_VAULT_AWARE:-}" ]; then   # an aware orc in a fight (FSM 7: the stock evade there)
      echo "== vault-$N (aware)"; w "practice remove" 1.5; w "practice spawn infantry" 4   # the fight (FSM 7), then the AI blind:
      w "story invisible on" 3; w "verbs front ${H31_VAULT_CM:-80}" 0.8; w "execute face" 1.0   # the orc stands, the fight's FSM stays?
      last '^OVAULT t=.* state: ' 1 200
    else round "vault-$N"; w "story invisible on" 0.6; w "verbs front ${H31_VAULT_CM:-80}" 0.8; w "execute face" 1.0; fi
    x "head 0 0 0 s 0.4" 0.8; w "$LREST" 2.0
    waitfor '^OVAULT t=.* cue: ' 3; c=$(last '^OVAULT t=.* cue: ' 1 300); echo "$c"
    X=$(echo "$c" | grep -oE '[-+][0-9.]+ right' | grep -oE '[-+][0-9.]+' | tr -d +); Y=$(echo "$c" | grep -oE '[-+][0-9.]+ up' | grep -oE '[-+][0-9.]+' | tr -d +)
    Z=$(echo "$c" | grep -oE '[-+][0-9.]+ ahead' | grep -oE '[-+][0-9.]+' | tr -d +)
    [ -n "$Z" ] || { echo "$(date +%T) no vault cue"; w "story invisible off" 0.4; endr; continue; }
    X=$(printf '%.0f' "$X"); Y=$(printf '%.0f' "$(echo "$Y + 3" | bc)"); Z=$(printf '%.0f' "$Z")
    R3=$(echo "sqrt($X*$X + $Y*$Y + $Z*$Z)" | bc); echo "$(date +%T) the crown $R3 cm from the eyes"
    [ "$R3" -le "${H31_ARM_CM:-75}" ] || { echo "$(date +%T) the crown out of an arm's reach ($R3 cm): no vault try"; w "story invisible off" 0.4; endr; continue; }
    w "hands test L $X $Y $Z 0 0 0" 1.3; waitfor '^OVAULT t=.* cue: ' 3   # the eye's correction: the palm onto the crown
    c=$(last '^OVAULT t=.* cue: ' 1 400); echo "$c"
    P=$(echo "$c" | sed -E 's/.*the palm ([-+][0-9.]+) ([-+][0-9.]+) ([-+][0-9.]+).*/\1 \2 \3/' | tr -d +)
    C=$(echo "$c" | sed -E 's/.*crown ([-+][0-9.]+) right ([-+][0-9.]+) up ([-+][0-9.]+) ahead.*/\1 \2 \3/' | tr -d +)
    if [ -n "$P" ] && [ -n "$C" ] && ! echo "$c" | grep -q 'the palm on it'; then
      set -- $P $C; X=$(printf '%.0f' "$(echo "$X + $4 - $1" | bc)"); Y=$(printf '%.0f' "$(echo "$Y + $5 - $2 - 3" | bc)"); Z=$(printf '%.0f' "$(echo "$Z + $6 - $3" | bc)")
      echo "$(date +%T) corrected: $X $Y $Z"; w "hands test L $X $Y $Z 0 0 0" 1.3; waitfor '^OVAULT t=.* cue: ' 3; last '^OVAULT t=.* cue: ' 1 400
    fi
    g0=$(cnt '^OVAULT t=.* grab:'); w "hands squeeze L 1" 0.6
    [ "$(cnt '^OVAULT t=.* grab:')" -gt "$g0" ] || last '^OVAULT t=.* squeeze: ' 1 300
    dp "vault-$N-oncrown"
    [ "$(cnt '^OVAULT t=.* grab:')" -gt "$g0" ] || { echo "$(date +%T) no vault grab"; w "hands squeeze L 0" 0.4; w "story invisible off" 0.4; endr; continue; }
    last '^OVAULT t=.* grab:' 1 300
    w "hands swing L $X $((Y - 30)) $((Z - 15)) 0 0 0 400" 0.1; waitfor '^BODY test swing L end' 3; dp "vault-$N-hold"
    w "hands swing L $X $((Y - 80)) $((Z - 65)) 0 0 0 150 0.8" 0.05   # the throw: 50 down, 50 back in 150 ms
    for k in 1 2 3 4; do echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.3; done
    waitfor '^OVAULT t=.* end:' 4; sleep 0.5; dp "vault-$N-landed"
    grep -E '^OVAULT t=.* (throw|blocked|end):' "$R/somvr.log" | tail -3 | cut -c1-300
    grep -E '^OVAULT t=.* flight:' "$R/somvr.log" | tail -40 | awk 'NR%3==1' | cut -c1-200
    grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -6 | tr '\n' ' ' | tee -a "$R/fd-steps.txt"; echo
    w "hands squeeze L 0" 0.4; w "story invisible off" 0.4; endr
  done
fi
w "verbs status" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
