# nurn41/m14b.sh - som-b42-bleed: Hunting Partners' bleed the VR way (docs 191), then m14.sh's later phases
# (NU_MISSION="m14b.sh m14.sh" M14_SKIP="follow bleed"). After the card's start: the follow (m14's), the eating graug,
# a trip to BLB_FROM cm behind it (aid, under the game's invisibility, which is then turned off), then the VR acts: the
# sword drawn over the shoulder, the crouch (the head low), the stick's sneak in toward its legs until the bleed's glow
# shows (the watch's OFFERED = what the player sees), the view on it, and the blade's thrust aimed at the glow
# (`verbs bleed where`: the cut's point in the head's frame; the grip on the line from the shoulder, the blade through
# the point - a real hand's stab). Up to BLB_TRIES tries. BLB_DRAWS="after before": per try, the sword drawn before or after the crouch; BLB_HANDS="on on": per try, `verbs bleed hands` (SOMVR_GRAUG_BLEED_HANDS; off = build 41 against on in one boot).
. "$HERE/nurn41/lib42.sh"
M0=$(lines); SK=" ${M14_SKIP:-} "
w "story invisible off" 0.3
[[ $SK == *" bfollow "* ]] || follow "show 'GP_Torvin02_Tut_TrollBleed'|Bleed the Graug|'Troll': shown by the game" "${M14_FOLLOW_MIN:-12}"
ptut
# (sets EG = "ptr x y z facing dist attack" of the eating graug; no subshell: the tutorial count and the aids stay)
ewait() { local k G TM mx my mz gc gx gy gz gf gd ga; EG=""; invis on
  for k in $(seq 1 70); do ptut; state | grep -q 'paused=1' && { pics nu-blbp$k; tap R a 0.4; sleep 2; }
    G=$(graugs); echo "$G" | grep -q Eating && { EG=$(echo "$G" | grep Eating | head -1); return 0; }
    if [ -z "$G" ]; then TM=$(objm 'Troll'); [ -n "$TM" ] && { read mx my mz <<< "$TM"; go $((mx + 1800)) $((my + 300)) "$mz"; echo "$(date +%T) NU aid: 18 m from the graug's marker"; z 2; G=$(graugs); }; fi
    echo "$G" | grep -q Eating && { EG=$(echo "$G" | grep Eating | head -1); return 0; }
    read gc gx gy gz gf gd ga <<< "$(echo "$G" | head -1)"
    [ -n "$gd" ] && [ "$gd" -lt 2800 ] && { go $((gx + 3200)) $((gy + 300)) "$gz"; echo "$(date +%T) NU aid: 32 m off the graug ($ga): out of its sight, so it goes to its meal"; }
    [ $((k % 5)) = 1 ] && echo "$(date +%T) waiting for it to eat ($(echo "$G" | head -1 | cut -d' ' -f6-))"; z 4; done; return 1; }
# aimed stab: the cut's point T (head frame) -> the grip H = T - L*u (u from the right shoulder to T), drawn back 45 cm
stab() { local l T i; l=$(lines); w "verbs bleed where" 0.3
  for i in $(seq 1 15); do T=$(since "$l" '^VERBS bleed where t=.*the cut.s point' | tail -1 | sed -nE "s/.*the cut's point (-?[0-9]+) (-?[0-9]+) (-?[0-9]+) knee.*/\1 \2 \3/p"); [ -n "$T" ] && break; sleep 0.2; done   # (the log comes a moment later: 033243-1)
  [ -n "$T" ] || { echo "$(date +%T) stab: no cut's point"; return 1; }
  read PRE_S STAB_S <<< "$(echo "$T" | awk '{ sx = 20; sy = -25; sz = 0; dx = $1 - sx; dy = $2 - sy; dz = $3 - sz; d = sqrt(dx*dx + dy*dy + dz*dz)
    ux = dx / d; uy = dy / d; uz = dz / d; L = d - 62; if (L < 40) L = 40; if (L > 85) L = 85
    hx = $1 - L * ux; hy = $2 - L * uy; hz = $3 - L * uz; P = 3.14159265; rx = atan2(uy, sqrt(ux*ux + uz*uz)) * 180 / P; ry = -atan2(ux, uz) * 180 / P
    printf "%.1f,%.1f,%.1f,%.1f,%.1f %.1f,%.1f,%.1f,%.1f,%.1f\n", hx - 45*ux, hy - 45*uy, hz - 45*uz, rx, ry, hx, hy, hz, rx, ry }')"
  local a b; IFS=, read -r a1 a2 a3 a4 a5 <<< "$PRE_S"; IFS=, read -r b1 b2 b3 b4 b5 <<< "$STAB_S"
  echo "$(date +%T) stab: the cut's point $T (the head's frame): the grip from $a1 $a2 $a3 to $b1 $b2 $b3, the blade pitched $b4 yawed $b5"
  w "hands test R $a1 $a2 $a3 $a4 $a5 0" 0.8; [ "${BLB_PREPIC:-0}" = 1 ] && pics nu-blb-pre   # (the blade lined up at the glow)
  w "hands swing R $b1 $b2 $b3 $b4 $b5 0 150" 0.9
  echo "$(date +%T) NU act: the blade's stab at the glow on its leg"; sleep 1.5; w "hands test R $a1 $a2 $a3 $a4 $a5 0" 0.6; w "$REST" 0.4; }
ewait && G=$EG || { echo "$(date +%T) m14b: no eating graug"; return 0; }
read gc gx gy gz gf gd ga <<< "$G"; echo "$(date +%T) m14b: graug $gc at $gx $gy $gz facing $gf ($ga)"
HB=1; [ "${BLB_PROBE:-0}" = 1 ] && w "verbs bleed probe 1500" 0.5
sheathe() { drawn || return 0; w "$REST" 0.3; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.4; w "$REST" 0.4
  echo "$(date +%T) NU act: the sword put back over the shoulder ($(drawn && echo still in the hand || echo away))"; }
# offnear B CM: the watch's newest line since B says OFFERED with its right foot within CM (the glow on that leg, in reach)
offnear() { since "$1" '^VERBS bleed watch ' | tail -1 | grep 'OFFERED' | grep -oE 'its feet R [0-9]+' | grep -oE '[0-9]+$' | awk -v m="${2:-120}" '$1 <= m {f=1} END {exit !f}'; }
for k in $(seq 1 "${BLB_TRIES:-2}"); do
  G=$(graugs "$gx" "$gz" 800 | grep "$gc" | head -1); [ -n "$G" ] && read gc gx gy gz gf gd ga <<< "$G"
  DR=$(echo ${BLB_DRAWS:-after before} | cut -d' ' -f"$k"); DR=${DR:-before}
  HA=$(echo ${BLB_HANDS:-on on} | cut -d' ' -f"$k"); HA=${HA:-on}
  CU=$(echo ${BLB_CUTS:-1 1} | cut -d' ' -f"$k"); CU=${CU:-1}   # (0: the offer watched only, no cut)
  echo "$(date +%T) m14b try $k: the draw $DR the crouch, the stealth's hands $HA"; w "verbs bleed hands $HA" 0.3
  B0=$(lines); invis on
  read tx tz < <(python3 -c "import math;f=math.radians($gf);D=${BLB_FROM:-300};S=${BLB_LAT:-90};print(round($gx-D*math.sin(f)+S*math.cos(f)),round($gz-D*math.cos(f)-S*math.sin(f)))")   # (behind its right leg: S cm to its right)
  go "$tx" $((gy - 150)) "$tz"; w "ride face graug" 1.5; w "ride face off" 0.3; echo "$(date +%T) NU aid: ${BLB_FROM:-300} cm behind graug $gc"
  [ "$DR" = before ] && draw
  crouch 10000; z 2; [ "${BLB_INVIS:-keep}" = keep ] || invis off   # (BLB_INVIS=keep: the game's invisibility through the approach - an aid for the sneak, as m17's)
  [ "$DR" = after ] && { draw; crouch 10000; }   # (drawn out of the blade's reach of its legs)
  B1=$(lines); echo "$(date +%T) after the draw: $(since "$B0" '^VERBS bleed watch ' | tail -1 | grep -oE 'OFFERED|not offered \([^)]*\)' | head -1)"
  stalk "$gx" "$gz" "${BLB_STALK_CM:-140}"   # (the stick's sneak in, crouched)
  for i in $(seq 1 30); do offnear "$B1" "${BLB_FOOT:-120}" && break; [ "$i" = 14 ] && crouch 10000; sleep 0.5; done
  OF=$(since "$B1" '^VERBS bleed watch ' | tail -1 | grep OFFERED | cut -c1-330); echo "$(date +%T) the glow: ${OF:-none (not offered near)}"
  since "$B0" 'bleed watch .*stealth.s hands asked|SLOTS .*stealth.s hands' | head -2 | cut -c1-280
  w "verbs stalk 0 0 0" 0.3; crouch 10000; w "ride face graug" 1.0; w "ride face off" 0.3
  if [ -n "$OF" ] && [ "$CU" = 1 ]; then pics nu-blb$k; stab; elif [ -n "$OF" ]; then pics nu-blbw$k; echo "$(date +%T) try $k: the glow seen - no cut this try (BLB_CUTS)"; z 2
  else echo "$(date +%T) no glow: no cut (a player waits for it)"; z 2; fi
  since "$B0" 'VERBS t=.*bleed' | head -3 | cut -c1-240
  since "$B0" "Talion attack 'PC_Hamstring" | grep -q . && { HB=0; echo "$(date +%T) m14b: HAMSTRUNG by the blade ($k)"; sleep 1; pics nu-blb-after; sleep 1.5; pics nu-blb-after2; break; }
  invis on; go $((gx + 900)) $((gy + 100)) "$gz"; w "crouch stand" 0.3; z 2; sheathe; z 4   # (away from it before the sword goes back)
done
w "crouch stand" 0.3; invis off
echo "$(date +%T) m14b: the bleed $([ $HB = 0 ] && echo DONE || echo not done); $(obj "$M0" 3 | tr '\n' '|')"
[ $HB = 1 ] && [ "${M14_GO_ON:-0}" = 0 ] && { M14_SKIP="follow bleed mount"; M14_LURE_MIN=0; M14_STUN_MIN=0; }   # (no bleed: m14.sh after it does nothing)
