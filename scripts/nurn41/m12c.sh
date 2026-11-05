# nurn41/m12c.sh - Queen of the Shore: the fort's orcs and the reinforcements alone (m12b.sh's step 5).
M0=$(lines)
# 5. the fort's orcs, the prisoners' video, the reinforcements: each marked orc - behind it (aid), health 1 (aid), the cut
k12() { local MAX=$1 RE=$2 e=0 i p; for i in $(seq 1 "$MAX"); do
    win && { endphase 2; }; state | grep -q 'video=1' && endphase 2
    mapfile -t P < <(markers "$RE"); [ ${#P[@]} = 0 ] && { e=$((e + 1)); [ $e -ge ${3:-5} ] && return 0; sleep 4; continue; }; e=0
    set -- ${P[0]}; kill1 "$1" "$3" 110; sleep 0.5; done; }
k12 60 'ObjectiveGold(NoClutter)?' 6
endphase 4
k12 40 'ObjectiveGold(NoClutter)?' 6
echo "$(date +%T) m12 kills: $(since "$M0" '^KILLS kill' | wc -l); the end: $(obj "$M0" 4)"
