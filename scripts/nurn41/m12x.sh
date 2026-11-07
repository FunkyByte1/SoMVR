# nurn41/m12x.sh - som-b42-archers (docs/re-xr-pipeline.md 185): Queen of the Shore's Sea Fort - the tower and sentry
# archers branded from behind the VR way (crouched, the stick's sneak to ~80 cm, the open left hand reached out at its
# head by the test hands' pose - no `verbs aim` - and the left trigger), then from the front; an ordinary orc of the
# fort as the control. After m12.sh's 1:30 run: NU_MISSION="m12.sh m12x.sh" (lib.sh's helpers).
# Each try opens the game's own attack log for 8 s (SOMVR_VERBS_BRAND_NODELOG=on with
# SOMVR_VERBS_BRAND_NODELOG_FILTER=StealthBrand: "Rejected Attack: ATK - <the test>", "Predicates 'ATK' ... -> ...").
#   X12_INVIS=on|off   the game's invisibility from the fort on (default on, as m12b: the drill's teleports)
#   X12_ORDER          the tries per archer (default "back160 back100 front150")
#   X12_CONTROL=1      an ordinary orc of the fort first, from behind (0 = none)
#   X12_AB=1           each try: the hand on the head with build 41's body yaw, then 42's (SOMVR_VERBS_BRAND_FACE)
#   X12_FRONT_UPTO=N   archers 1..N from the front only (default none)
M0=$(lines)
endphase 3
since "$M0" "HINTS (tutorial|show).*Brand|Ability Unlocked" | head -4 | cut -c1-300
invis "${X12_INVIS:-on}"
# the open left hand reached out at the head (verbs.h: the eyes -> palm line within 25 deg of the eyes -> head, the palm
# 35+ cm out), palm forward (OpenXR grip: -Z pinky -> thumb, finish20's grab pose); the first pose the cue calls "on"
X12_POSES=("-6 -12 62" "-6 -22 60" "-6 -2 60" "-6 8 58")   # (26+ cm over the eyes + the trigger = the rally)
x12reach() { local l p; l=$(lines); for p in "${X12_POSES[@]}"; do w "hands test L $p 180 -90 0" 0.6
    if since "$l" '^VERBS cue t=.* on: orc' | grep -q .; then echo "$(date +%T) NU act: the open left hand reached at its head ($p)"; return 0; fi
  done; echo "$(date +%T) NU act: the left hand reached ($p): no 'on' cue"; return 1; }
# the head's yaw rides in every xrtest write (lib.sh's x writes the whole test state: a button write alone would put
# the head back at yaw 0)
HY=0
xh() { x "head 0 0 0 yaw $HY s 0.3" "$@"; }
tapL() { xh "btn L trigger 1"; sleep 0.2; TS=0.4 xh "btn L trigger 0"; echo "$(date +%T) NU act: tap L trigger 0.2 (the head at yaw $HY)"; }
# x12side: the case the game refuses (boot 20261106-222323): the open hand on the archer at the SIDE - the head turned
# 100 deg away (the body follows it: LOCO's standing turn past 50 deg), then back to 55 deg away (the body stays: its
# 50 deg lag), the left hand reached 55 deg to that side at its head. +yaw's side found by the cue (the other if none).
x12side() { local s p l; for s in ${X12_SIDE_SIGNS:-1 -1}; do HY=$(( 100 * s )); xh; z 1.5; HY=$(( 55 * s )); xh; z 0.6
    l=$(lines); for p in "$(( -51 * s )) -12 36" "$(( -51 * s )) -22 36" "$(( -45 * s )) -5 40"; do w "hands test L $p 180 -90 0" 0.6
      if since "$l" '^VERBS cue t=.* on: orc' | grep -q .; then X12_SIDE_SIGNS=$s
        echo "$(date +%T) NU act: the head 55 deg away from it (yaw $HY), the open left hand reached at its head at the side ($p)"; return 0; fi
    done; done
  HY=0; xh; echo "$(date +%T) NU act: the side reach found no 'on' cue"; return 1; }
# x12try WHAT HOW X Z: one try. back160 = behind 160 + the stick's sneak to 80 cm, back100; then `execute face` (the
# player's snap turn at it) and the crouch. X12_AB=1 (default) from behind: x12side, the hand on its head 2 s with
# build 41's body yaw (`verbs brand face off`: the game's answer logged), then 42's (`face on`), then the trigger.
# front150 = near it (behind 300 X Z), then in front of the nearest (verbs front 150), the hand straight ahead.
x12try() { local what=$1 how=$2 l b0 r side=0; b0=$(cnt "$BRRE"); l=$(lines); w "verbs brand nodelog 0" 0.3; w "verbs brand nodelog 25" 0.3; crouch
  HY=0; xh
  case $how in
    back160) behind 160 "$3" "$4"; stalk "$3" "$4" 80; z 3 ;;
    back100) behind 100 "$3" "$4"; z 1 ;;
    front150) behind 300 "$3" "$4"; z 1; w "verbs front 150" 1.2; echo "$(date +%T) NU aid: in front 150"; z 1 ;;
  esac
  w "execute face" 0.8; echo "$(date +%T) NU act: turned to look at it (the snap turn)"
  crouch   # (crouch hold lasts 10 s at most: again before the reach, as lib.sh's brand does)
  if [ "${X12_AB:-1}" = 1 ] && [ "$how" != front150 ]; then w "verbs brand face off" 0.3
    x12side && side=1 || x12reach; z 2; w "verbs brand face on" 0.3; z 1.5
  else x12reach; sleep 0.4; fi
  tapL; sleep 2.5; HY=0; xh; w "$LREST" 0.3; w "verbs brand face on" 0.2
  if [ "$(cnt "$BRRE")" -gt "$b0" ]; then r="BRANDED $(since "$l" "$BRRE" | head -1 | grep -oE "attack 'PC_[A-Za-z_]+'")"
  else r="none"; fi
  echo "$(date +%T) X12 $what $how$([ $side = 1 ] && echo ' (the side reach)'): $r | $(since "$l" 'no brand offered|offers the stealth brand' | tail -1 | sed -E 's/.*\): //' | cut -c1-170)"
  since "$l" 'VERBS brand (face|t=.*(no brand offered|offers the stealth))|VERBS brand node t=.*(Rejected Attack: PC_(Archer|Bip|LowRail_Archer)_StealthBrand_[BF] |Pool validation)' | sed -E 's/^VERBS brand node t=[0-9.]+ f\+[0-9]+ //' | cut -c1-200 | head -12
  [ "${r%% *}" = BRANDED ]; }
# the control: an ordinary orc of the fort (not an archer), from behind
# (an orc 4 m or more from every archer's marker, the nearest such to Talion within 40 m)
if [ "${X12_CONTROL:-1}" = 1 ]; then MK=$(markers 'ObjectiveGold' | awk '{print $1, $3}' | tr '\n' ' '); set -- $MK
  c=$(orcs "$1" "$2" 3500 | awk -v mk="$MK" '{ n = split(mk, m, " "); far = 1
        for (i = 1; i < n; i += 2) if (($2 - m[i]) ^ 2 + ($4 - m[i + 1]) ^ 2 < 160000) far = 0
        if (far) { print $2, $4, $1; exit } }')
  echo "$(date +%T) X12 control pick: ${c:-none} (markers: $MK)" | cut -c1-240
  if [ -n "$c" ]; then set -- $c; x12try control back160 "$1" "$2" || x12try control back100 "$1" "$2"; fi
fi
# the archers: each 'ObjectiveGold' marker in turn, X12_ORDER's tries until one brands
declare -A X12DONE; X12N=0
for round in $(seq 1 "${X12_MAX:-12}"); do ptut
  since "$M0" "show 'GP_Marwen2_Tut_ActivateBranded|Activate Branded" | grep -q . && { echo "$(date +%T) X12: all branded"; break; }
  win && { echo "$(date +%T) X12: a window ($(obj "$M0" 2 | tr '\n' '|'))"; break; }
  mapfile -t P < <(markers 'ObjectiveGold'); pick=""
  for p in "${P[@]}"; do set -- $p; k="$(( $1 / 300 ))_$(( $3 / 300 ))"; [ -n "${X12DONE[$k]:-}" ] && continue; pick=$p; break; done
  [ -z "$pick" ] && { echo "$(date +%T) X12: no marker left (${#P[@]} up)"; break; }
  set -- $pick; k="$(( $1 / 300 ))_$(( $3 / 300 ))"; X12DONE[$k]=1; X12N=$((X12N + 1))
  echo "$(date +%T) X12 archer $X12N at $1 $3 (${#P[@]} markers up)"
  ord=${X12_ORDER:-back160 back100 front150}; [ "$X12N" -le "${X12_FRONT_UPTO:-0}" ] && ord=front150   # (the front's own:
  # the first archers, while no branded orc stands near for `verbs front`'s nearest)
  for how in $ord; do x12try "archer$X12N" "$how" "$1" "$3" && break; done
done
echo "$(date +%T) X12: $(cnt "$BRRE") brand lines in the log since the start"
