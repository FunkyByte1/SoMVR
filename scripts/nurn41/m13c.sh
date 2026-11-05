# nurn41/m13c.sh - Big Game's end (after the runner): off the beast (B held), to Torvin's marker (aid), the wraith
# vision held (left B), his captors cut down (behind them (aid), health 1 (aid), the real cut), the end's screens.
M0=$(lines)
w "ride steer off" 0.3; x "btn R b 1"; sleep 0.9; TS=0.4 x "btn R b 0"; echo "$(date +%T) NU act: B held (off the beast)"; z 3
l0=$(lines); w "objective status" 1.2
OB=$(since "$l0" "^OBJECTIVE status marker .*'Objective' " | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
echo "$(date +%T) the marker: ${OB:-none}"; [ -n "$OB" ] && { set -- $OB; go "$1" $(( $2 + 100 )) "$3"; }
draw; x "btn L b 1"; sleep 4; TS=0.4 x "btn L b 0"; echo "$(date +%T) NU act: the wraith vision held 4 s"
for k in $(seq 1 20); do ptut; win && { endphase 3; break; }; state | grep -q 'video=1' && { endphase 3; break; }
  read px py pz < <(talion); [ -n "$pz" ] || { z 2; continue; }
  mapfile -t O < <(orcs "${px%.*}" "${pz%.*}" 1500); [ ${#O[@]} = 0 ] && { echo "$(date +%T) no orc within 15 m"; z 3; continue; }
  set -- ${O[0]}; kill1 "$2" "$4" 110; z 2
done
endphase 4
echo "$(date +%T) m13c: $(obj "$M0" 5)"
