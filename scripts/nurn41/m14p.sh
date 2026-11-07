# nurn41/m14p.sh - som-b42-bleed: Hunting Partners' bleed - where the game offers it (a probe, docs 191). After the
# card's start: the follow to the eating graug (m14's), then `abil rec` of the hamstring node (its pools, attacks and
# predicates), the watch's every ask logged (`verbs bleed probe`, with the brand's attack log open: run with
# SOMVR_VERBS_BRAND_NODELOG=on SOMVR_VERBS_BRAND_NODELOG_FILTER=Hamstring), and a sweep of placements round it (aids:
# the trips under the game's invisibility; the view turned to it as a player looks): standing, crouched, the head turned
# away; then the draw and the blade's low thrust where it was offered (M14P_CUT=0: no cut). BLP_SWEEP="D:A ..." the
# placements (cm behind its root : deg off straight behind).
. "$HERE/nurn41/lib42.sh"
M0=$(lines)
w "story invisible off" 0.3
[ "${BLP_FOLLOW:-1}" = 0 ] || follow "show 'GP_Torvin02_Tut_TrollBleed'|Bleed the Graug|'Troll': shown by the game" "${M14_FOLLOW_MIN:-12}"
ptut
# the eating graug (bleed()'s wait: the aids that let it go back to its meal)
# (sets EG = "ptr x y z facing dist attack" of the eating graug; no subshell: the tutorial count and the aids stay)
eatwait() { local k G TM mx my mz gc gx gy gz gf gd ga; EG=""; invis on
  for k in $(seq 1 70); do ptut; state | grep -q 'paused=1' && { pics nu-blpp$k; tap R a 0.4; sleep 2; }
    G=$(graugs); echo "$G" | grep -q Eating && { EG=$(echo "$G" | grep Eating | head -1); return 0; }
    if [ -z "$G" ]; then TM=$(objm 'Troll'); [ -n "$TM" ] && { read mx my mz <<< "$TM"; go $((mx + 1800)) $((my + 300)) "$mz"; echo "$(date +%T) NU aid: 18 m from the graug's marker"; z 2; G=$(graugs); }; fi
    echo "$G" | grep -q Eating && { EG=$(echo "$G" | grep Eating | head -1); return 0; }
    read gc gx gy gz gf gd ga <<< "$(echo "$G" | head -1)"
    [ -n "$gd" ] && [ "$gd" -lt 2800 ] && { go $((gx + 3200)) $((gy + 300)) "$gz"; echo "$(date +%T) NU aid: 32 m off the graug ($ga): out of its sight, so it goes to its meal"; }
    [ $((k % 5)) = 1 ] && echo "$(date +%T) waiting for it to eat ($(echo "$G" | head -1 | cut -d' ' -f6-))"; z 4; done; return 1; }
# place D A: Talion D cm behind its root, A deg off straight behind (aid), the view turned to it (as a player looks)
place() { read tx tz < <(python3 -c "import math;f=math.radians($gf+$2);print(round($gx-$1*math.sin(f)),round($gz-$1*math.cos(f)))")
  go "$tx" $((gy - 150)) "$tz"; w "ride face graug" 1.5; w "ride face off" 0.3; echo "$(date +%T) NU aid: placed $1 cm behind graug $gc ($2 deg off)"; }
eatwait && G=$EG || { echo "$(date +%T) probe: no eating graug"; return 0; }
read gc gx gy gz gf gd ga <<< "$G"; echo "$(date +%T) probe: graug $gc at $gx $gy $gz facing $gf ($ga)"
invis on; place 160 0; w "abil rec PC_TrlCave_Hamstring" 2.5; w "verbs bleed probe 1500" 0.5
for DA in ${BLP_SWEEP:-140:0 120:0 100:0 80:0 160:0 190:0 120:30 120:-30 120:60 120:-60 100:90 100:-90 150:30 150:-30}; do
  D=${DA%:*}; A=${DA#*:}; B0=$(lines)
  G=$(graugs "$gx" "$gz" 800 | grep "$gc" | head -1); [ -n "$G" ] && read gc gx gy gz gf gd ga <<< "$G"   # (its root moves as it eats)
  place "$D" "$A"; echo "$(date +%T) BLP $D:$A standing"; z 2
  crouch 9000; z 3; echo "$(date +%T) BLP $D:$A crouched"
  look 100; z 2; echo "$(date +%T) BLP $D:$A crouched, the head 100 deg away"; look 0; z 1
  w "ride face graug" 1.0; w "ride face off" 0.3; z 1
  echo "$(date +%T) BLP $D:$A: $(since "$B0" 'VERBS bleed watch' | grep -c OFFERED) offered of $(since "$B0" 'VERBS bleed watch' | grep -c .) asks"
  w "crouch stand" 0.3; z 1
done
# the cut where it was offered (the drawn blade's low thrust at its legs: a VR act)
if [ "${M14P_CUT:-1}" = 1 ]; then
  BEST=$(grep -aE "BLP [0-9-]+:[0-9-]+: [1-9][0-9]* offered" "$R/ladder-driver.log" 2>/dev/null | head -1 | grep -oE '[0-9-]+:[0-9-]+' | head -1)
  BEST=${BEST:-${M14P_AT:-140:0}}; echo "$(date +%T) probe: the cut at $BEST"
  for k in 1 2 3; do G=$(graugs "$gx" "$gz" 800 | grep "$gc" | head -1); [ -n "$G" ] && read gc gx gy gz gf gd ga <<< "$G"
    B0=$(lines); place "${BEST%:*}" "${BEST#*:}"; crouch 10000; z 2; draw; z 1; w "ride face graug" 1.0; w "ride face off" 0.3
    pics nu-blcut$k; w "$PRE_L" 0.8; w "$THRUST_L" 1.0; echo "$(date +%T) NU act: the low thrust at its legs"; sleep 2.5; w "$PRE_L" 0.6; sleep 1.5
    since "$B0" 'VERBS t=.*bleed' | head -3 | cut -c1-220
    since "$B0" "Talion attack 'PC_Hamstring" | grep -q . && { echo "$(date +%T) probe: HAMSTRUNG ($k)"; pics nu-blafter; break; }
    w "crouch stand" 0.3; z 6; done
fi
invis off; w "verbs bleed probe 0" 0.3
echo "$(date +%T) m14p: $(since "$M0" 'VERBS bleed watch' | grep -c OFFERED) offered asks of $(since "$M0" 'VERBS bleed watch' | grep -c .)"
