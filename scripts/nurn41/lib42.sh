# nurn41/lib42.sh - som-b42-nurn (missions 14-18 on build 41): more VR acts and aids for the directors, sourced after
# lib.sh (the graug, the caragor's mount at the offer, the bow's charged shot, the follow). Ported from
# scripts/story33-steps.txt's graug2.sh (som-b33-story6) into lib.sh's words; `NU act` / `NU aid` as in lib.sh.
d2() { awk -v a="$1" -v b="$2" -v x="$3" -v z="$4" 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}'; }
tpos() { local l0; l0=$(lines); w "practice status" 1.0 >&2; since "$l0" 'Talion known at' | grep -aoE 'Talion known at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tail -1 |
  grep -oE -- '-?[0-9.]+' | awk 'NR==1{x=$1} NR==2{y=$1} NR==3{printf "%d %d %d\n",x,y,$1}'; }
# objm RE: "x y z" of the newest objective-status marker whose kind matches RE
# (the status lists a marker only while it is "seen": none since the word = the newest place the log has, OBJM_OLD=0: none)
objm() { local l0 m; l0=$(lines); w "objective status" 1.0 >&2
  m=$(since "$l0" "^OBJECTIVE status marker .*'($1)'" | tail -1)
  [ -z "$m" ] && [ "${OBJM_OLD:-1}" = 1 ] && m=$(grep -aE "^OBJECTIVE (status marker|t=.*marker [0-9]+ m=).*'($1)'.* at \(" "$L" | tail -1)
  echo "$m" | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-; }
seated() { grep -aE "^RIDE t=.*(seated on|dismounted)" "$L" | tail -1 | grep -q 'seated on'; }
# follow STOP_RE MIN: each round a trip (aid) beside Torvin's marker (else the newest objective marker) when over 4 m off;
# videos, windows and pausing pages by A (then B); until STOP_RE shows since the call or MIN minutes
follow() { local l0 T n=0 s G px py pz gx gy gz d; l0=$(lines); T=$((SECONDS + ${2:-12} * 60))
  while [ $SECONDS -lt $T ]; do n=$((n + 1)); userin && bail "the user is in VR"; gone && return 1; ptut
    since "$l0" "$1" | grep -q . && { echo "$(date +%T) follow: '$1' after $n rounds"; return 0; }
    s=$(state)
    if echo "$s" | grep -q 'video=1'; then sleep 5; tap R a 0.3; sleep 0.6; tap R b 0.3; sleep 2; continue; fi
    if echo "$s" | grep -q 'paused=1' || win; then sleep 2; pics nu-fw$n; tap R a 0.4; sleep 2.5; { win || state | grep -q 'paused=1'; } && { tap R b 0.4; sleep 2.5; }; continue; fi
    G=$(objm 'ObjectiveGuttered_ALLMaps'); [ -n "$G" ] || G=$(objm 'Objective|ObjectiveGold[A-Za-z]*')
    read px py pz <<< "$(tpos)"
    if [ -n "$G" ]; then read gx gy gz <<< "$G"; d=$(d2 "$gx" "$gz" "${px:-0}" "${pz:-0}")
      [ $((n % 5)) = 1 ] && echo "$(date +%T) follow #$n: Talion ${px:-?} ${pz:-?}, the marker $gx $gz ($d cm)"
      [ "$d" -gt 400 ] && go $((gx + 150)) $((gy + 100)) "$gz" || z 3
    else z 3; fi
  done; echo "$(date +%T) follow: no '$1' in ${2:-12} min"; return 1; }
# ---- the graug ----
# graugs [X Z RR]: "ptr x y z facing dist attack" of the graugs (the trolls) near X,Z (default Talion), nearest first
graugs() { local px py pz l0 TM mx my mz; read px py pz <<< "$(tpos)"; l0=$(lines)
  if [ $# -ge 2 ]; then mx=$1; mz=$2; else TM=$(objm 'Troll'); read mx my mz <<< "${TM:-${GRAUG_AT:-32281 2209 -27717}}"; fi
  w "verbs orcs $mx $mz ${3:-3500}" 1.4 >&2; w "verbs orcs ${px:-0} ${pz:-0} 3500" 1.4 >&2
  since "$l0" "^VERBS orcs t=.*attack '(Trl|AI_Trl|AI_Troll|Troll|AI_Graug|Graug)" |
    sed -nE "s/.* orc (0x[0-9a-f]+) at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing (-?[0-9.]+) deg, ([0-9]+) cm from Talion.*attack '([^']*)'.*/\1 \2 \3 \4 \5 \6 \7/p" | sort -u -k1,1 | sort -k6 -n; }
PRE_L="hands test R 20 -45 -18 -30 0 0"; THRUST_L="hands swing R 10 -85 50 -30 0 0 180"   # the blade's low thrust (story6's)
# bleed D: crouched D cm behind the eating graug (aid: the trip), the low thrust of the drawn blade (act)
bleed() { local D=${1:-140} G gc gx gy gz gf gd ga tx tz B0 k TM mx my mz
  for k in $(seq 1 30); do ptut; state | grep -q 'paused=1' && { pics nu-bleedp$k; tap R a 0.4; sleep 2; }; G=$(graugs); echo "$G" | grep -q Eating && { G=$(echo "$G" | grep Eating | head -1); break; }
    # not eating: a graug near Talion is wary of him - back off 25 m from it (aid) and let it go back to its meal
    if [ -z "$G" ]; then   # no graug in the list (it lists only those near Talion): 18 m from the 'Troll' marker (aid)
      TM=$(objm 'Troll'); [ -n "$TM" ] && { read mx my mz <<< "$TM"; go $((mx + 1800)) $((my + 300)) "$mz"; echo "$(date +%T) NU aid: 18 m from the graug's marker"; z 2; G=$(graugs); }; fi
    echo "$G" | grep -q Eating && { G=$(echo "$G" | grep Eating | head -1); break; }
    read gc gx gy gz gf gd ga <<< "$(echo "$G" | head -1)"
    [ -n "$gd" ] && [ "$gd" -lt 1500 ] && { go $((gx + 2500)) $((gy + 300)) "$gz"; echo "$(date +%T) NU aid: 25 m off the wary graug ($ga)"; }
    [ $((k % 5)) = 1 ] && echo "$(date +%T) bleed: waiting for it to eat ($(echo "$G" | head -1 | cut -d' ' -f6-))"; G=""; z 4; done
  [ -n "$G" ] || { echo "$(date +%T) bleed: no eating graug"; return 1; }; read gc gx gy gz gf gd ga <<< "$G"
  local A=${2:-0}   # (the bearing off straight behind it, deg)
  read tx tz < <(python3 -c "import math;f=math.radians($gf+$A);print(round($gx-$D*math.sin(f)),round($gz-$D*math.cos(f)))")
  B0=$(lines); go "$tx" $((gy - 150)) "$tz"; w "ride face graug" 2.5; w "ride face off" 0.3; echo "$(date +%T) NU aid: $D cm behind graug $gc ($A deg off), faced at it"
  crouch 12000; stalk "$gx" "$gz" "${BLEED_STALK_CM:-120}"; z 3; w "ride face graug" 1.0; w "ride face off" 0.3   # (the eating graug's root moves: the stick's sneak in, a VR act)
  if [ "${BLEED_PROBE:-0}" = 1 ]; then   # (a probe, not a VR act: the game's own X while its bleed prompt is up)
    for k in $(seq 1 20); do grep -aE "prompt (up|down): action 118 .*ElfBleed" "$L" | tail -1 | grep -q 'prompt up' && break; sleep 0.5; done
    echo "$(date +%T) probe: the prompt $(grep -aE "prompt (up|down): action 118 .*ElfBleed" "$L" | tail -1 | grep -oE 'prompt (up|down)'), the graug $(graugs "$gx" "$gz" 600 | grep "$gc" | cut -d' ' -f6-)"
    press 118 150; sleep 3; since "$B0" "Talion attack '[A-Za-z_]+" | grep -oE "Talion attack '[A-Za-z_0-9]+'" | sort -u | head -4 | tr '\n' ' '; echo
    since "$B0" "Talion attack 'PC_(TrlCave_)?Hamstring" | grep -q .; return; fi
  draw; w "$PRE_L" 0.8; pics nu-bleed-$D; echo "$(date +%T) the graug now: $(graugs "$gx" "$gz" 600 | grep "$gc" | cut -d' ' -f6-)"; w "$THRUST_L" 1.0; echo "$(date +%T) NU act: the low thrust at its legs"; sleep 2.5; w "$PRE_L" 0.6; sleep 1.5
  since "$B0" 'VERBS t=.*bleed' | head -1 | cut -c1-200
  since "$B0" "Talion attack 'PC_Hamstring" | grep -q .; }
# mount MIN: the left hand out, the grip closed AT the game's offer beside a stunned caragor (act); a trip beside a
# knocked-down beast over 2.2 m off (aid); a struggle by hard thrusts (act); windows by A
mount() { local T B0 B1 K kd C P cx cy cz lastgo=0 got=0 px py pz l1 i k
  w "ride steer off" 0.2; draw
  w "hands test R 30 -10 45 0 -35 0" 0.4; w "fight guard learn" 0.4; w "ride guard on" 0.3; w "ride face on" 0.3
  w "hands test L -25 -35 30 0 0 0" 0.5; w "hands squeeze L 0" 0.5
  T=$((SECONDS + ${1:-8} * 60)); B0=$(lines)
  while [ $SECONDS -lt $T ]; do userin && bail "the user is in VR"; gone && return 1
    if since "$B0" 'the game offers the mount' | grep -q .; then
      printf '%s' "hands squeeze L 1" > "$R/somvr.trigger"; B1=$(lines); echo "$(date +%T) NU act: the offer - the left grip closed"
      for i in $(seq 1 40); do since "$B1" "^RIDE t=.* mounted:" | grep -q . && break; sleep 0.1; done
      since "$B1" "^RIDE t=.* mounted:" | grep -q . && { got=1; break; }
      sleep 1; w "hands squeeze L 0" 0.4; B0=$(lines); continue; fi
    win && { sleep 1.5; tap R a 0.3; sleep 2; B0=$(lines); }
    if state | grep -q 'ctx=struggle'; then echo "$(date +%T) a struggle: hard thrusts"
      for k in $(seq 1 10); do w "hands test R 20 -27 -18 12 0 0" 0.25; w "hands swing R 5 -11 54 12 0 0 150" 0.35; state | grep -q 'ctx=struggle' || break; done
      w "hands test R 30 -10 45 0 -35 0" 0.3; fi
    K=$(tail -c 60000 "$L" | grep -aE "^BEASTS t=.*caragor '[A-Za-z_]+' Combat=0x[0-9a-f]+ attack 'AI_Warg_Knockdown_Idle" | tail -1)
    if [ -n "$K" ] && [ $((SECONDS - lastgo)) -gt 6 ]; then kd=$(echo "$K" | grep -oE 'at [0-9.]+ m' | grep -oE '[0-9.]+'); C=$(echo "$K" | grep -oE 'Combat=0x[0-9a-f]+' | cut -c8-)
      if awk -v d="${kd:-9}" 'BEGIN{exit !(d > 2.2)}'; then read px py pz <<< "$(tpos)"; l1=$(lines); w "verbs orcs $px $pz 4000" 1.0 >/dev/null
        P=$(since "$l1" "orc $C at" | tail -1 | sed -E 's/.* at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing.*/\1 \2 \3/')
        [ -n "$P" ] && { read cx cy cz <<< "$P"; B0=$(lines); w "practice go $(( cx + 130 )) $(( cy + 60 )) $cz" 0.3; echo "$(date +%T) NU aid: beside the stunned $C (${kd} m off)"; lastgo=$SECONDS; }
      fi; fi
    sleep 0.1
  done
  w "ride face off" 0.2; w "ride guard off" 0.2
  for i in $(seq 1 40); do since "${B1:-$B0}" "^RIDE t=.* seated on" | grep -q . && break; sleep 0.25; done; sleep 1.5
  w "hands squeeze L 0" 0.3; w "$LREST" 0.3; w "$REST" 0.3; echo "$(date +%T) mounted $got"; [ $got = 1 ]; }
# ---- the bow (the poses of story6's graug2.sh: the left hand holds the bow out, the right draws to the cheek) ----
BOW_SLOT="hand L head -12 -10 -15 90 0 0"; BOW_STRAIGHT="hand L head -8 -15 55 90 0 0"
AT_STRING="hand R head -5 -13 30 90 0 0"; DRAWN="hand R head 3 -10 -3 90 0 0"; R_REST="hand R head 22 -40 15 0 0 0"
xl() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; echo "head 0 0 0 yaw ${HY:-0} pitch ${HP:-0}"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
       mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${TS:-0.8}"; }
bow_ok() { grep -aE '^BOW t=.* (enter |exit: )' "$L" | tail -1 | grep -q ' enter '; }
bow_out() { w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3; w "hands test off" 0.4; xl "$BOW_SLOT" "$R_REST"; sleep 0.3
  xl "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.8; xl "$BOW_STRAIGHT" "$R_REST" "btn L squeeze 1"; sleep 1; }
# bow_draw: to full draw, held ~2 s (a charged shot: over SOMVR_GRAUG_STUN_HOLD_S 0.75)
bow_draw() { bow_ok || { xl "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 0.6; bow_out; }
  xl "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1"; sleep 0.4; xl "$BOW_STRAIGHT" "$AT_STRING" "btn L squeeze 1" "btn R trigger 1"; sleep 0.5
  xl "$BOW_STRAIGHT" "$DRAWN s 0.8" "btn L squeeze 1" "btn R trigger 1"; sleep 1.3; }
bow_loose() { TS=0.1 xl "$BOW_STRAIGHT" "$DRAWN" "btn L squeeze 1" "btn R trigger 0"; }
bow_away() { xl "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; sleep 0.5; xl "$BOW_SLOT" "$R_REST" "btn L squeeze 1"; sleep 0.6; xl "$BOW_SLOT" "$R_REST" "btn L squeeze 0"; xl; w "$LREST" 0.3; w "$REST" 0.3; }
# shoot X Y Z: a charged shot at a head at X Y Z (`bow aimhead` = the test's aim: a person's aim, an aid) - prints the hit
shoot() { local n1; [ "$(( ${SHOTS:-0} % 6 ))" = 0 ] && w "bow arrows 8" 0.3; bow_draw; n1=$(lines); w "bow aimhead $1 $2 $3" 0.2; bow_loose; SHOTS=$(( ${SHOTS:-0} + 1 )); sleep 2.5
  echo "$(date +%T) NU act: charged shot $SHOTS (aimed by the aid): $(since "$n1" '^BOW (hit t=|graugstun t=)' | grep -oE "the attack '[^']+'|record '[^']+'|graugstun" | tr '\n' ' ')"; }
# graugfight MIN [STOP_RE]: on foot, the bow's charged shots at the graug's head when it is within 45 m (act, aimed by
# `bow aimhead`), the cuts beside it while it is stunned (aid: the trip; act: the cuts); a ghul held by it (its attack
# '*Grab*|*Eat*') gets a charged shot at its own head; until STOP_RE, a struggle, a video or MIN minutes
graugfight() { local T n=0 G gc gx gy gz gf gd ga ST0 l0 Gh; T=$((SECONDS + ${1:-14} * 60)); ST0=$(cnt '^BOW graugstun t='); l0=$(lines)
  while [ $SECONDS -lt $T ]; do n=$((n + 1)); ptut; gone && return 1
    win && { sleep 1.5; pics nu-gw$n; tap R a 0.4; sleep 2; continue; }
    state | grep -qE 'video=1|ctx=struggle' && return 0
    [ -n "${2:-}" ] && since "$l0" "$2" | grep -q . && return 0
    G=$(graugs | head -1); [ -z "$G" ] && { z 3; continue; }; read gc gx gy gz gf gd ga <<< "$G"
    if echo "$ga" | grep -qE 'Grab|Eat|Feed'; then   # it holds a ghul: shoot the nearest ghul's head
      Gh=$(orcs "$gx" "$gz" 600 | grep -v "$gc" | head -1); [ -n "$Gh" ] && { set -- $Gh; echo "$(date +%T) the graug holds ($ga): a shot at $1"; shoot "$2" $(( $3 + 120 )) "$4"; continue; }; fi
    if echo "$ga" | grep -q 'Stun'; then
      bow_ok && bow_away; draw; w "practice go $((gx + 200)) $((gy + 50)) $gz" 1.5; w "ride face graug" 1.5; w "ride face off" 0.3
      echo "$(date +%T) NU aid: beside the stunned graug ($ga)"; slash; slash; upcut; thrust
    elif [ "$gd" -lt 4500 ]; then shoot "$gx" "$gy" "$gz"
    else w "practice go $((gx + 2000)) $((gy + 100)) $gz" 1.5; echo "$(date +%T) NU aid: 20 m from the graug"; fi
    [ "$(cnt '^BOW graugstun t=')" -gt "$ST0" ] && { ST0=$(cnt '^BOW graugstun t='); pics nu-stun$n; }
    [ $((n % 5)) = 1 ] && echo "$(date +%T) graugfight #$n: graug $gd cm ($ga), $(obj "$l0" 2 | tr '\n' '|')"
  done; return 1; }
