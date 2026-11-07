# nurn41/m18.sh - The Rescue (Lithariel), after the card's start. Forces (GAME-STUDY-story 2.3, 3.8): brand 6 without
# an alarm in 6:40; her 5 captors; lift her (the grab) and carry her; endless Uruks; put her down, fight, lift again;
# "The Black Hand is near!". VR path: the stealth brand (the left palm at the collar + the trigger, crouched), the 300 ms
# cut and the thrust, her grab = the left palm at her head + the left grip (SOMVR_USE_GRAB), the carry = the grip held +
# the left stick (story2_route.py), put down = the grip opened. Aids: the trips and placements, `fight health 1 not HER`
# at a captor / an Uruk on her (the blow is then the VR cut), her health 9999 (a person's guard, 131.2),
# `Player.CanBeAttacked 0` in the carry, M18_INVIS=1: `story invisible on` in the carry. M18_SKIP="brands captors".
. "$HERE/nurn41/lib42.sh"
M0=$(lines); SK=" ${M18_SKIP:-} "
w "struggle auto on" 0.5; echo "$(date +%T) NU aid: struggle auto on (the test hands answer each struggle as a player would, at the game's speed)"
LX=${LITH_X:--28266}; LZ=${LITH_Z:-33695}
nwh() { since "$M0" "Talion attack 'PC_[A-Za-z]+_StealthBrand_[A-Z]'" | wc -l; }   # (the brands Talion made: the white-hand markers are not shown under the invisibility - 012227)
draw
# 1. the camp's 6 brands: each live orc near the camp, crouched 100 cm behind it (aid), the brand (act)
if [[ $SK != *" brands "* ]]; then declare -A BT
  for round in 1 2 3 4; do [ "$(nwh)" -ge 6 ] && break
    mapfile -t O < <(orcs "${M18_CX:--27100}" "${M18_CZ:-26000}" 4000)
    echo "$(date +%T) brands round $round: ${#O[@]} orcs, branded $(nwh)"
    for o in "${O[@]}"; do [ "$(nwh)" -ge 6 ] && break; ptut; win && break 2
      set -- $o; [ "${BT[$1]:-0}" -ge 2 ] && continue; BT[$1]=$(( ${BT[$1]:-0} + 1 ))
      l1=$(lines); invis on; crouch
      # (under the game's invisibility: a teleport in alerts them - m17's 003146; the camp's sentries ('AI_Bip_Sentry_Low_*',
      # 'AI_Class_Defender_Sentry_Low_*') offer no brand from behind - "StealthBrand -> none" x18 in 011015, the same as
      # QotS's tower archers: from the front at 150 cm, as build 27 branded them)
      if [ "${BT[$1]}" = 2 ]; then behind 100 "$2" "$4"; else behind 100 "$2" "$4"; w "verbs front 150" 1.2; echo "$(date +%T) NU aid: in front 150"; fi; brand
      if since "$l1" "$BRRE" | grep -q .; then BT[$1]=9; echo "$(date +%T) NU brand: $1 (branded $(nwh))"
      else echo "$(date +%T) NU brand: none at $1: $(since "$l1" 'no brand offered|StealthBrand -> none' | tail -1 | cut -c1-160)"; fi
      since "$M0" "Alarm|alarm|Mission Failed|Out of Time" | head -1 | cut -c1-160
    done
  done; invis off; w "crouch stand" 0.3; echo "$(date +%T) brands: $(nwh) ($(obj "$M0" 3 | tr '\n' '|'))"
  # "Find Lithariel": a trip to her marker, the video
  O=$(objm 'Objective'); set -- ${O:--28058 1850 33551}; go "$1" 1400 "$3"; z 4
  for i in 1 2 3 4 5 6; do state | grep -q 'video=1' && { sleep 4; tap R a 0.3; tap R b 0.3; sleep 3; }; inworld && break; z 3; done
fi
# 2. her captors: Talion 100 cm beyond each on the side away from her (aid), its health 1 `not HER` (aid), the cut (act)
l0=$(lines); w "verbs orcs $LX $LZ 400" 1.2
HER=$(since "$l0" '^VERBS orcs t=.* [0-9]+/' | sed -nE 's/.* orc (0x[0-9a-f]+) at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing.*/\1 \2 \4/p' | awk -v X=$LX -v Z=$LZ '(($2-X)^2+($3-Z)^2) <= 120^2 {print $1; exit}')
echo "$(date +%T) Lithariel: ${HER:-not found} near $LX $LZ"
if [[ $SK != *" captors "* ]]; then
  for pass in 1 2 3 4 5 6; do ptut
    mapfile -t O < <(orcs "$LX" "$LZ" 3000 | awk -v X=$LX -v Z=$LZ '(($2-X)^2+($4-Z)^2) > 120^2')
    echo "$(date +%T) captors pass $pass: ${#O[@]}"; [ ${#O[@]} = 0 ] && break
    since "$M0" "help her up|Help Lithariel|GrabLithariel" | grep -q . && [ $pass -gt 1 ] && break
    for o in "${O[@]}"; do set -- $o
      read tx tz < <(python3 -c "import math;dx=$2-($LX);dz=$4-($LZ);d=math.hypot(dx,dz) or 1;print(round($2+100*dx/d),round($4+100*dz/d))")
      go "$tx" $(( $3 + 30 )) "$tz"; w "execute face" 0.3; l1=$(lines); hp 1 ${HER:+not $HER}
      since "$l1" '^FIGHT health .* 1000\.0 -> 1\.0' | grep -q . && echo "$(date +%T) !!! the aid took her"
      slash; thrust
    done; z 2
  done; echo "$(date +%T) captors: $(obj "$M0" 3 | tr '\n' '|')"
fi
# her health 9999 (aid: Talion at her, the `fight health` aimed at her only)
aidher() { [ -n "$HER" ] || return 0; w "practice go $(( ${1:-$LX} + 60 )) ${3:-1400} ${2:-$LZ}" 1.5
  l1=$(lines); w "fight health 9999" 0.6; echo "$(date +%T) NU aid: her health 9999 ($(since "$l1" '^FIGHT health' | tail -1 | cut -c1-110))"; }
# lift: the left palm at her head + the grip (act), up to 4 tries; the trip beside her (aid)
lift() { local t l1; for t in 1 2 3 4; do l1=$(lines); w "verbs aim head" 0.3; [ -n "${1:-}" ] && go $(( $1 + 60 )) "${3:-1400}" "$2"
    hold L squeeze; sleep 2.5
    # (the grip kept closed by the test hand's own squeeze: story2_route.py rewrites the layer's test file with the stick
    # only, which opened the xrtest grip 6 s into the carry - 012227's 'PC_GrabLithariel_Release'; build 27's way)
    since "$l1" "Talion attack 'PC_GrabLithariel" | grep -q . && { w "hands squeeze L 1" 0.3; w "verbs aim off" 0.3; echo "$(date +%T) NU act: she is lifted (try $t)"; return 0; }
    w "hands squeeze L 0" 0.3; release L squeeze; sleep 1.5; done; w "verbs aim off" 0.3; return 1; }
herat() { local l0 p; l0=$(lines); w "verbs orcs ${1:-$LX} ${2:-$LZ} 2500" 1.0 >&2
  since "$l0" "orc ${HER:-none} at" | tail -1 | sed -nE 's/.* at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing.*/\1 \3 \2/p'; }
w "practice console Player.CanBeAttacked 0" 0.5; echo "$(date +%T) NU aid: Player.CanBeAttacked 0"
aidher; lift "$LX" "$LZ" || echo "$(date +%T) no lift"
[ "${M18_INVIS:-0}" = 1 ] && invis on
# 3. the carry: the grip held, the walk by the stick along boot 090100's line; she hurt or dropped: put down, cut, lift
PATHW=${PATHW:--28367,33729;-28421,33023;-28502,32338;-28641,31738;-28979,31154;-29007,30449;-28963,29763;-28755,29060;-28536,28361;-28308,27601;-28117,26960;-27943,26186;-27872,25587;-27404,23277}
T=$((SECONDS + ${M18_CARRY_MIN:-20} * 60)); n=0; DOWNS=0
while [ $SECONDS -lt $T ]; do n=$((n + 1)); gone && break; ptut
  state | grep -q 'video=1' && { echo "$(date +%T) a video: the end?"; break; }
  since "$M0" "Mission Complete|SUCCESS" | grep -q . && break
  if win; then pics nu-cw$n; echo "$(date +%T) a window: $(obj "$M0" 2 | tr '\n' '|')"; tap R a 0.4; sleep 4; read hx hz hy <<< "$(herat)"; aidher "$hx" "$hz" "$hy"; lift "$hx" "$hz" "$hy"; continue; fi
  l2=$(lines)
  read px py pz <<< "$(tpos)"; WP=$(echo "$PATHW" | tr ';' '\n' | awk -F, -v x="${px:-0}" -v z="${pz:-0}" '{d=($1-x)^2+($2-z)^2; a[NR]=$0; if (NR==1 || d<b) {b=d; k=NR}} END {for (i=k+1; i<=NR; i++) printf "%s;", a[i]}')
  WP=${WP%;}; [ -z "$WP" ] && WP=$(echo "$PATHW" | tr ';' '\n' | tail -1)
  route "$WP" 30 c$n "--nosprint --noedge --learn 0.3"
  # (she is put down by the game when hurt; the grab held then takes an Uruk: 'PC_Grab_Attempt_*' / 'PC_Grab_Into_*' -
  # 020135, "Lithariel is taking damage!" is only on the HUD)
  if since "$l2" "release after|PC_GrabLithariel_Release|no stereo frame read the left hand|Grab_Release|Talion attack 'PC_Grab_(Attempt|Into)" | grep -q .; then
    DOWNS=$((DOWNS + 1)); echo "$(date +%T) down #$DOWNS: $(since "$l2" 'taking damage|release' | tail -1 | cut -c1-140)"
    w "hands squeeze L 0" 0.3; release L squeeze; sleep 1; read hx hz hy <<< "$(herat "${px:-$LX}" "${pz:-$LZ}")"
    for k in 1 2 3; do mapfile -t U < <(orcs "${hx:-${px:-0}}" "${hz:-${pz:-0}}" 600); [ ${#U[@]} -le 1 ] && break
      for u in "${U[@]}"; do set -- $u; [ "$1" = "$HER" ] && continue; behind 110 "$2" "$4"; w "execute face" 0.3; hp 1 ${HER:+not $HER}; slash; thrust; break; done; done
    aidher "$hx" "$hz" "$hy"; lift "$hx" "$hz" "$hy" || echo "$(date +%T) no lift after down #$DOWNS"
  fi
  [ $((n % 3)) = 1 ] && echo "$(date +%T) carry #$n: Talion ${px:-?} ${pz:-?}, downs $DOWNS, $(obj "$M0" 2 | tr '\n' '|')"
done
w "hands squeeze L 0" 0.3; release L squeeze; invis off
echo "$(date +%T) m18: downs $DOWNS; $(obj "$M0" 5)"
