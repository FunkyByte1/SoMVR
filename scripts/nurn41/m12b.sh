# nurn41/m12b.sh - Queen of the Shore from the Sea Fort on (after m12.sh's 1:30 run): the video, "Ability Unlocked:
# Brand", the six archers, the rally, the fort's orcs and the reinforcements (m12.sh's header has the rest).
M0=$(lines)
draw
# 2. the Sea Fort's video, "Ability Unlocked: Brand" (the pausing page: its text in VR words?)
endphase 3
since "$M0" "HINTS (tutorial|show).*Brand|Ability Unlocked" | head -4 | cut -c1-300
# 3. the six archers: the brand from behind (crouched, the stick to 80 cm, the palm + the trigger), else from the front
invis on
brandloop 'ObjectiveGold' "show 'GP_Marwen2_Tut_ActivateBranded|Activate Branded" 24
# 4. "Activate Branded archers": the raised left hand + the trigger
sleep 2; l3=$(lines); rally; sleep 4; since "$l3" "rally|128 " | head -3 | cut -c1-200; pics m12-rally
# 5. the fort's orcs, the prisoners' video, the reinforcements: each marked orc - behind it (aid), health 1 (aid), the cut
k12() { local MAX=$1 RE=$2 e=0 i p; for i in $(seq 1 "$MAX"); do
    win && { endphase 2; }; state | grep -q 'video=1' && endphase 2
    mapfile -t P < <(markers "$RE"); [ ${#P[@]} = 0 ] && { e=$((e + 1)); [ $e -ge ${3:-5} ] && return 0; sleep 4; continue; }; e=0
    set -- ${P[0]}; kill1 "$1" "$3" 110; sleep 0.5; done; }
k12 60 'ObjectiveGold(NoClutter)?' 6
endphase 4
k12 40 'ObjectiveGold(NoClutter)?' 6
echo "$(date +%T) m12 kills: $(since "$M0" '^KILLS kill' | wc -l); the end: $(obj "$M0" 4)"
