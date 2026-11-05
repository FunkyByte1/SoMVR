# nurn41/m13b.sh - Big Game (Torvin), part 2 once seated on the caragor (after m13a.sh): the ride's fight, the bow from
# the saddle at "can you shoot from one", the runner's chase (the stick's click = the sprint, the bite = the right
# trigger, the leap = A), "Save Torvin" and the end. The body below is som-b27-ride3's bgr6.sh (scripts/ride27c-steps.txt,
# boot 20261030-234922: Big Game to its end on build 27c) from its ride phase on, unchanged but for this shim (lib.sh's
# run, words and buttons; its `since` -> sinceb, its endphase -> bg_end, remount.sh -> remount). Aids it keeps: the
# beast's steering by `ride steer` (a player's thumb on the stick, the head free), the caragor's health 9999 after a failed
# screen (a rider bites and dodges archers), the trips to the end's marker on foot. `RIDE_S` (1800) caps the ride.
TAG=nu; Q=:
wd() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.45}"; }
xt() { local a; shift; { echo "# bg $(date +%s%N)"; for a in "$@"; do echo "$a"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.3; }
dump() { pics "$1"; }
ev() { tail -n +$1 $L | grep -aE "^STORY t=.*subtitle ch=[01] '|^HINTS tutorial #.*show|^USE t=|window 0 -> 1|Mission|^RIDE t=.*(mounted|dismounted|seated on|not a beast)|^WARN" | cut -c1-170 | awk '!s[$0]++' | head -${2:-10}; }
sinceb() { tail -c +"$B0" "$L"; }
# sync CMD...: story22-drive.sh's commands run here at once (go, wait, word, status, tap, pics, world)
sync() { local c a; for c in "$@"; do set -f; set -- $c; set +f; a=$1; shift
    case $a in go) go "$1" "$2" "$3" ;; wait) sleep "$1" ;; word) w "$*" 0.5 ;; status) status ;; tap) tap "$@" ;;
      pics) pics "$1" ;; world) for i in $(seq 1 "${1:-60}"); do inworld && break; sleep 1; done ;; *) echo "sync: $a?" ;; esac; done; }
remount() { local try C px pz; for try in 1 2 3; do read px pz < <(talion | awk '{printf "%d %d\n",$1,$3}')
    l0=$(lines); w "verbs orcs $px $pz 4000" 1.2
    C=$(since "$l0" '^VERBS orcs t=' | grep -a "${PICK:-Beckon}" | sed -E 's/.* at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing.* ([0-9]+) cm from Talion.*/\1 \2 \3 \4/' | sort -k4 -n | head -1)
    echo "$(date +%T) remount try $try: Talion $px $pz; the nearest ${PICK:-Beckon}: ${C:-none}"; [ -n "$C" ] || { sleep 3; continue; }
    set -- $C; l0=$(lines); go $(( $1 + 200 )) $(( $2 + 80 )) $3; w "ride face on" 0.3; w "hands test L -25 -35 30 0 0 0" 2
    for i in $(seq 1 16); do since "$l0" "the game offers the mount" | grep -q . && break; sleep 0.5; done
    w "hands squeeze L 1" 3; echo "$(date +%T) NU act: the left hand closed (the remount)"
    if since "$l0" "^RIDE t=.*(mounted|seated on|dismounted|off: standing)" | tail -1 | grep -q 'seated on'; then
      w "ride face off" 0.3; w "hands squeeze L 0" 0.3; w "hands test off" 0.5; echo "$(date +%T) seated"; return 0; fi
    w "hands squeeze L 0" 1; done; echo "$(date +%T) not remounted"; return 1; }
PT=$(cnt 'passed \(SOMVR_HINTS_PAUSING\)')
ptut() { local n; n=$(cnt 'passed \(SOMVR_HINTS_PAUSING\)'); [ "$n" -gt "$PT" ] || return 0; PT=$n
  echo "$(date +%T) a pausing tutorial: $(grep -a 'passed (SOMVR_HINTS_PAUSING)' $L | tail -1 | cut -c1-140)"; sleep 2.5
  dump bg-ptut-$n; xt a1 "btn R a 1"; sleep 0.3; xt a0 "btn R a 0"; sleep 2.5; echo "$(date +%T) after A: $(state)"; }
B0=$(stat -c %s "$L")
# ---- bgr6.sh from its ride phase (som-b27-ride3) ----
echo "PHASE ride $(date +%T)"
X=$R/xr-layer.log; NOCK='^BOW t=[0-9.]+ frame=[0-9]+ nock: '
# the bow (bow20-drive.sh's poses): the bow hand 60 cm out along yaw A pitch P (head frame), the string hand D cm back
pose() { awk -v a="$1" -v p="$2" -v d="${3:-57}" -v sm="${4:-}" 'BEGIN { r = 3.14159265 / 180; L = 60
  dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 16; ay = -22; az = 0
  printf "hand L head %.1f %.1f %.1f 90 0 0\n", ax + L * dx - 1.2, ay + L * dy, az + L * dz
  printf "hand R head %.1f %.1f %.1f 90 0 0%s\n", ax + (L - d) * dx, ay + (L - d) * dy, az + (L - d) * dz, sm != "" ? " s " sm : "" }'; }
setpose() { local IFS=$'\n'; xt pose $(pose "$@"); }
aimat() { grep -aE '^BOW target ' "$L" | tail -1 | sed -nE "s/.*the head at head yaw ([-+][0-9.]+) pitch ([-+][0-9.]+).*/\1 \2/p"; }
BOWN=0
bowtry() { local n0 nh b A=0 P=-2 k c l0; BOWN=$((BOWN + 1))
  echo "PHASE bow from the saddle #$BOWN $(date +%T)"; wd "ride steer off" 0.3
  wd "hands test off" 0.3; wd "hands squeeze L -1" 0.3; wd "hands squeeze R -1" 0.3 # the layer's controllers again
  n0=$(cnt '^BOW .* enter ')
  xt sq00 "btn L squeeze 0" "btn R squeeze 0" "btn R trigger 0"; sleep 0.5
  xt slot "hand L head -12 -10 -15 90 0 0"; sleep 0.6; xt sq "btn L squeeze 1"; sleep 1.5
  grep -aE '^BOW t=.* (slot|denied|enter)' $L | tail -3 | cut -c1-200
  [ "$(cnt '^BOW .* enter ')" -gt "$n0" ] && echo "the bow out" || echo "NO bow out: $(grep -a '^BOW' $L | tail -1 | cut -c1-160)"
  xt straight "hand L head -8 -15 55 90 0 0"; sleep 1
  setpose 0 -2 4; sleep 0.4; xt tr1 "btn R trigger 1"; sleep 1.5
  echo "nock: $(grep -aE "$NOCK" $L | tail -1 | cut -c1-140)"
  setpose 0 -2 57 0.8; sleep 1.0
  HY=0; for k in 1 2 3 4; do sleep 1.1; b=$(aimat); [ -n "$b" ] || break; A=${b% *}; P=${b#* }
    if awk -v a="$A" 'BEGIN{exit !(a > 15 || a < -15)}'; then HY=$(awk -v h=$HY -v a="$A" 'BEGIN{printf "%.1f", h + a}') # the head turned to it (a rider looks)
      xt head "head 0 0 0 yaw $HY pitch 0 s 0.3"; sleep 0.6; setpose 0 "$P" 57; else setpose "$A" "$P" 57; fi; done
  echo "aimed yaw $A pitch $P: $(grep -a '^BOW target ' $L | tail -1 | cut -c1-200)"
  dump bg-bow$BOWN-drawn; printf 'shot st-bg-bow%s\n' $BOWN >> $R/cmd; sleep 1
  n0=$(cnt '^BOW t=.* assist: '); nh=$(cnt '^BOW hit t=')
  xt tr0 "btn R trigger 0"; sleep 2.5
  echo "the release: $(( $(cnt '^BOW t=.* assist: ') - n0 )) shots, $(( $(cnt '^BOW hit t=') - nh )) hits"
  grep -a '^BOW' $L | tail -4 | cut -c1-230
  xt slot2 "hand L head -12 -10 -15 90 0 0"; sleep 0.4; xt sq0 "btn L squeeze 0"; sleep 0.6
  xt rest "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0" "head 0 0 0 yaw 0 pitch -10 s 0.3"; sleep 0.5
  if [ "$(cnt '^BOW t=.* assist: ')" -le "$n0" ]; then # no shot: what the game's mounted commands do (a probe)
    for c in 124 187 185; do l0=$(wc -l < $L); wd "ride press $c 1500" 2.2
      echo "ride press $c: $(tail -n +$l0 $L | grep -aoE "'[A-Za-z_]*([Bb]ow|Aim|Ranged|Shoot|Arrow)[A-Za-z_]*'" | sort -u | head -5 | tr '\n' ' ')"; done
  fi; }
# the sprint's probe (som-b27-ride3): each spec = `ride run` args; before each run 2.5 s steered at Torvin's marker
probe() { local spec TV n0 i l0
  echo "PHASE probe $(date +%T)"
  for spec in "$@"; do
    userin && bail "the user is in VR"
    grep -aE "^RIDE t=.*(dismounted|off: standing|seated on|mounted)" $L | tail -1 | grep -q 'seated on' || { echo "probe: not seated"; return 1; }
    l0=$(wc -l < $L); wd "objective status" 1.0
    TV=$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'ObjectiveGuttered_ALLMaps'" | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
    [ -n "$TV" ] && { read tvx tvy tvz <<< "$TV"; wd "ride steer $tvx $tvz" 2.5; wd "ride steer off" 0.2; }
    n0=$(cnt '^RIDE run t=.* over')
    wd "ride run $spec" 0.2
    for i in $(seq 1 60); do [ "$(cnt '^RIDE run t=.* over')" -gt "$n0" ] && break; sleep 0.25; done
    echo "PROBE [$spec] $(grep -a '^RIDE run t=.* over' $L | tail -1 | cut -c1-360)"
  done
  wd "ride steer off" 0.2; }
# the pick: the fastest spec after its first second (full stick, a command set), if 15 % over the plain stick's
pick() { local f=$R/nu-probe.txt base best bs
  grep -a '^PROBE \[' $R/ladder-driver.log 2>/dev/null > $f || grep -a '^PROBE \[' /dev/null > $f
  base=$(grep -a '^PROBE \[5000 0\]' $f | sed -nE 's/.*after its first second [0-9.]+ m in [0-9.]+ s = ([0-9.]+) m\/s.*/\1/p' | sort -n | tail -1)
  IFS='|' read bs bc bp < <(grep -a '^PROBE \[5000 [0-9,]*[1-9][0-9,]*\( [0-9]*\)\?\]' $f | sed -nE 's/^PROBE \[5000 ([0-9,]+)( ([0-9]+))?\].*after its first second [0-9.]+ m in [0-9.]+ s = ([0-9.]+) m\/s.*/\4|\1|\3/p' | sort -t'|' -k1 -n | tail -1)
  best="$bc${bp:+ $bp}"
  echo "PICK base ${base:-?} m/s; the fastest: [$best] ${bs:-?} m/s"
  if [ -n "$best" ] && [ -n "$base" ] && awk -v a="$bs" -v b="$base" 'BEGIN{exit !(a > 1.15 * b)}'; then wd "ride sprint $best" 0.4; echo "PICK the sprint = $best"
  else echo "PICK none faster: the sprint stays ${SPRINT0:-184}"; fi; }
# the A tests (som-b27-ride3, boot 2): the leap and the sprint by the VR controls, the RIDE view trace on (positions, y)
tv() { local l0; l0=$(wc -l < $L); wd "objective status" 1.0
  tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'ObjectiveGuttered_ALLMaps'" | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-; }
at_head() { local p; p=$(tv); [ -n "$p" ] && { read tvx tvy tvz <<< "$p"; wd "ride steer $tvx $tvz" ${1:-2.5}; }; }
amark() { echo "ATEST $1 $(date +%T) t=$(grep -a '^RATE t=' $L | tail -1 | grep -oE 't=[0-9.]+' | cut -c3-)"; }
atest() {
  echo "PHASE atest $(date +%T)"; wd "ride trace 6000" 0.2
  at_head 2.5; wd "ride steer off" 0.2; amark "1 the run word 7 s, at +2 s the pad's three tapped 250 ms (ride tap)"
  wd "ride run 7000 0" 2.0; wd "ride tap 16,121,184 250" 5.5; grep -a '^RIDE run t=.* over' $L | tail -1 | cut -c1-200
  at_head 2.5; wd "ride steer off" 0.2; amark "2 the run word 7 s, at +2 s build 27's pair held 1.5 s"
  wd "ride run 7000 0" 2.0; wd "ride tap 16,121 1500" 5.5; grep -a '^RIDE run t=.* over' $L | tail -1 | cut -c1-200
  at_head 2.5; amark "3 steered, the VR A (btn R a) held 4 s"
  xt a1 "btn R a 1"; for i in 1 2 3 4; do at_head 0.4; done; xt a0 "btn R a 0"; at_head 1.5
  amark "4 steered, the VR click (btn L stick) tapped: the latch; at +2.5 s the VR A tapped 0.25 s"
  xt c1 "btn L stick 1"; sleep 0.2; xt c0 "btn L stick 0"; at_head 1.2; at_head 1.2
  xt a1 "btn R a 1"; sleep 0.25; xt a0 "btn R a 0"; at_head 1.2; at_head 1.2; at_head 1.0
  amark "5 steered, the VR A tapped 0.25 s (no sprint)"; wd "ride steer off" 1.5; at_head 2.0
  xt a1 "btn R a 1"; sleep 0.25; xt a0 "btn R a 0"; at_head 1.5; at_head 1.5; wd "ride steer off" 0.3
  amark "end"; grep -aE "^RIDE t=.*(press A|the sprint by|tap \(the test)" $L | tail -8 | cut -c1-200
}
[ "${ATEST:-0}" = 1 ] && atest
if [ "${PROBE:-0}" = 1 ]; then
  probe "5000 0" "5000 184" "5000 16,121" "5000 16,121,184" "5000 184 400" "5000 16,121 600" "5000 18" "5000 0 0 0.5" "5000 184 0 0.5" "5000 0"
  pick
fi
[ "${BOW0:-1}" = 1 ] && bowtry
# the chase (som-b27-ride3, boot 2): a tight loop - the runner's marker every ~0.8 s, steered at where he will be in
# 0.8 s, the click's sprint latched (tapped again every 4th round), the bite within 9 m, the leap (A) at 5-13 m
chase() { local i l0 OB px pz ox oz ot dist tx tz miss=0 n0
  echo "PHASE chase $(date +%T)"; n0=$(cnt "Mission Failed|Target Escaped")
  xt c1 "btn L stick 1"; sleep 0.15; xt c0 "btn L stick 0"
  for i in $(seq 1 400); do
    userin && bail "the user is in VR"
    win && { echo "$(date +%T) chase: a window"; return 0; }
    l0=$(wc -l < $L); wd "objective status" 0.35; wd "practice status" 0.35
    OB=$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'Objective' " | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
    read px pz < <(tail -n +$l0 $L | grep -aoE 'Talion known at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tail -1 | grep -oE -- '-?[0-9.]+' | awk 'NR==1{x=$1} NR==3{printf "%d %d\n",x,$1}')
    [ -n "$OB" ] && [ -n "$pz" ] || { miss=$((miss + 1)); [ $miss -ge 12 ] && { echo "$(date +%T) chase: no runner marker for 12 rounds"; return 0; }; continue; }; miss=0
    read ox oy oz <<< "$OB"
    if [ -n "${pox:-}" ]; then read tx tz < <(awk -v x=$ox -v z=$oz -v a=$pox -v b=$poz -v dt=$((SECONDS - pot)) 'BEGIN{ if (dt < 1) dt = 1; vx=(x-a)/dt; vz=(z-b)/dt; if (vx*vx+vz*vz > 1500*1500) {vx=0; vz=0}; printf "%d %d\n", x+vx*0.8, z+vz*0.8 }'); else tx=$ox; tz=$oz; fi
    pox=$ox; poz=$oz; pot=$SECONDS
    dist=$(awk -v a=$ox -v b=$oz -v x=$px -v z=$pz 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')
    [ "$dist" -lt 1500 ] && { tx=$ox; tz=$oz; } # near: straight at him (run into him at a sprint: knocked over)
    # stuck (boot 225457: the beast stood 6 s against a rock while he ran on): moved < 150 cm over 2 rounds = 1.2 s aside
    mv=$(awk -v a=${qx:-$px} -v b=${qz:-$pz} -v x=$px -v z=$pz 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}'); qx=${rx:-$px}; qz=${rz:-$pz}; rx=$px; rz=$pz
    if [ $i -gt 3 ] && [ "$mv" -lt 150 ] && [ "$dist" -gt 600 ]; then side=$((-${side:-1}))
      read ux uz < <(awk -v a=$tx -v b=$tz -v x=$px -v z=$pz -v s=$side 'BEGIN{dx=a-x; dz=b-z; l=sqrt(dx*dx+dz*dz)+1; dx/=l; dz/=l; c=cos(70*3.14159/180); n=s*sin(70*3.14159/180); printf "%d %d\n", x+2500*(dx*c-dz*n), z+2500*(dz*c+dx*n)}')
      echo "$(date +%T) CHASE $i stuck (moved $mv cm): 1.2 s aside"; wd "ride steer $ux $uz" 1.2; fi
    wd "ride steer $tx $tz" 0.05
    [ $((i % 4)) = 0 ] && [ "$dist" -gt 800 ] && { xt c1 "btn L stick 1"; sleep 0.12; xt c0 "btn L stick 0"; }
    [ "$dist" -lt 700 ] && { xt b1 "btn R trigger 1"; sleep 0.2; xt b0 "btn R trigger 0"; }
    echo "$(date +%T) CHASE $i gap $dist cm, Talion $px $pz, the runner $ox $oz (steered at $tx $tz)"
    tail -n +$l0 $L | grep -aE "^STORY t=.*subtitle ch=1 '|Mission|^RIDE t=.*(dismounted|seated on)" | cut -c1-160 | awk '!s[$0]++'
    [ "$(cnt "Mission Failed|Target Escaped")" -gt "$n0" ] && { echo "$(date +%T) chase: failed"; return 0; }
  done; }
# after the runner (boot 225457: "Torvin is too quiet... Go to him, Talion. Perhaps we should track the hunter."; the
# marker at Torvin's; mounted the game offers 'UI_Prompt_Dismount'): near it off the beast (B held), on foot to the
# marker (trips), the wraith vision held there (the left Y), every window answered (A; B for a result's Back), pictures
bg_end() { local i l0 OB px pz dist seated n=0 k0
  echo "PHASE end $(date +%T)"; wd "ride steer off" 0.2
  for i in $(seq 1 260); do
    userin && bail "the user is in VR"
    if win; then n=$((n + 1)); echo "$(date +%T) end: a window ($n)"; dump bg-end-win-$n; printf 'shot st-bg-end-win-%s\n' $n >> $R/cmd; sleep 1.5
      ev $l0 8; k0=$(cnt '^USE t=')
      for a in 1 2 3; do win || break; xt a1 "btn R a 1"; sleep 0.3; xt a0 "btn R a 0"; sleep 2.5; done
      if win; then printf 'shot st-bg-end-win-%s-b\n' $n >> $R/cmd; sleep 1.5; xt bb "btn R b 1"; sleep 0.3; xt b0 "btn R b 0"; sleep 3; fi
      echo "$(date +%T) end: after the window: $(tail -n +${l0:-1} $L | grep -a '^USE t=' | tail -2 | cut -c1-120 | tr '\n' ' ')"; continue; fi
    ptut
    l0=$(wc -l < $L); wd "objective status" 0.6; wd "practice status" 0.5; wd "ride status" 0.4
    OB=$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'(Objective|ObjectiveGold[A-Za-z]*|ObjectiveGuttered_ALLMaps)' " | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
    read px py pz < <(tail -n +$l0 $L | grep -aoE 'Talion known at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tail -1 | grep -oE -- '-?[0-9.]+' | awk 'NR==1{x=$1} NR==2{y=$1} NR==3{printf "%d %d %d\n",x,y,$1}')
    seated=0; tail -n +$l0 $L | grep -aq '^RIDE status: seated' && seated=1
    tail -n +$l0 $L | grep -aE "^STORY t=.*subtitle ch=[01] '[A-Za-z]|Mission|^HINTS tutorial #.*show" | cut -c1-160 | awk '!s[$0]++'
    [ -n "$OB" ] && [ -n "$pz" ] || { [ $((i % 10)) = 0 ] && echo "$(date +%T) end #$i: no marker (seated $seated)"; sleep 1; continue; }
    read ox oy oz <<< "$OB"; dist=$(awk -v a=$ox -v b=$oz -v x=$px -v z=$pz 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')
    [ $((i % 5)) = 1 ] && echo "$(date +%T) end #$i: the marker $OB, $dist cm off, seated $seated"
    if [ $seated = 1 ]; then
      if [ "$dist" -gt 2500 ]; then wd "ride steer $ox $oz" 1.5
      else wd "ride steer off" 0.3; echo "$(date +%T) end: off the beast near the marker (B held)"; xt bb "btn R b 1"; sleep 0.8; xt b0 "btn R b 0"; sleep 3; fi
    elif [ "$dist" -gt 600 ]; then SYNC_S=40 sync "go $ox $((oy + 100)) $oz" "wait 2" >/dev/null
    else echo "$(date +%T) end: at the marker: the vision held 4 s"; xt v1 "btn L b 1"; sleep 4; xt v0 "btn L b 0"; sleep 1
      wd "stick 0 1 0 1.5" 1.8; fi
  done; }
# the fight / the follow: the nearest Uruk (verbs orcs; not the ridden beast, not Torvin's pair), else Torvin's marker,
# else an objective; the bite near (and every 4th round), the leap 6-15 m; stuck (moved < 4 m in 12 s, the target > 15 m
# off) = 3 s steered 75 deg aside (sides alternate) with a leap - as a rider goes round a rock; a window: A (a failed
# screen's Retry), still open after 6 s: B; the bow from the saddle once at "shoot from one" (and once more later)
T0=$SECONDS; k=0; LB=$(wc -l < $L); fails=0; empty=0; side=1; HX=(); HZ=(); HT=(); SHOOT=${SHOOT0:-0}
while [ $((SECONDS - T0)) -lt ${RIDE_S:-1800} ]; do k=$((k + 1)); l0=$(wc -l < $L)
  wd "practice status" 0.35; wd "objective status" 1.2
  read px pz < <(tail -n +$l0 $L | grep -aoE 'Talion known at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tail -1 | grep -oE -- '-?[0-9.]+' | awk 'NR==1{x=$1} NR==3{printf "%d %d\n",x,$1}')
  TV=$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'ObjectiveGuttered_ALLMaps'" | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
  OB=$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'(Objective|ObjectiveGold[A-Za-z]*|Objective_Squiller)' " | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
  [ -n "$TV" ] && TVL=$TV; TV=${TV:-$TVL}
  tgt=""; dist=""
  if [ -n "$pz" ]; then l1=$(wc -l < $L); wd "verbs orcs $px $pz 4000" 0.6
    read tvx tvy tvz <<< "${TV:-0 0 0}"
    tgt=$(tail -n +$l1 $L | grep -aE '^VERBS orcs t=.* [0-9]+/' | sed -E 's/.* at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing.* ([0-9]+) cm from Talion.*/\1 \3 \4/' | awk -v X=$tvx -v Z=$tvz '(($1-X)^2+($2-Z)^2) > 700^2 && $3 > 400' | sort -k3 -n | head -1)
  fi
  [ -n "$tgt" ] && [ "$(echo "$tgt" | awk '{print $3}')" -gt 3000 ] && [ -n "$TV" ] && tgt=""
  [ -n "$OB" ] && tail -n +$LB $L | grep -aqE "Stop him before he gets reinforcements" && tgt="" # the chase: the runner's marker
  if [ -z "$tgt$OB" ] && [ -z "$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'ObjectiveGuttered_ALLMaps'")" ]; then empty=$((empty + 1))
    [ $empty -ge 40 ] && { echo "$(date +%T) no marker and no Uruk for 40 rounds: the end?"; break; }; else empty=0; fi
  if [ -z "$tgt" ] && [ -n "$OB" ]; then set -- $OB; tgt="$1 $3 $(awk -v a=$1 -v b=$3 -v x=${px:-0} -v z=${pz:-0} 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')"; fi # the objective first
  if [ -z "$tgt" ] && [ -n "$TV" ]; then set -- $TV; tgt="$1 $3 $(awk -v a=$1 -v b=$3 -v x=${px:-0} -v z=${pz:-0} 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')"; fi
  if [ -z "$tgt" ] && [ -n "$OB" ]; then set -- $OB; tgt="$1 $3 $(awk -v a=$1 -v b=$3 -v x=${px:-0} -v z=${pz:-0} 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')"; fi
  # stuck?
  if [ -n "$pz" ]; then HX+=($px); HZ+=($pz); HT+=($SECONDS); fi
  stuck=0; n=${#HT[@]}
  if [ -n "$tgt" ] && [ $n -ge 2 ]; then for ((j = n - 1; j >= 0; j--)); do [ $((SECONDS - HT[j])) -ge 12 ] && break; done
    if [ $j -ge 0 ] && [ $((SECONDS - HT[j])) -lt 20 ]; then
      mv=$(awk -v a=${HX[j]} -v b=${HZ[j]} -v x=$px -v z=$pz 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')
      [ "$mv" -lt 400 ] && [ "${tgt##* }" -gt 1500 ] && stuck=1; fi; fi
  if [ $stuck = 1 ]; then read tx tz dist <<< "$tgt"; side=$((-side))
    read ux uz < <(awk -v a=$tx -v b=$tz -v x=$px -v z=$pz -v s=$side 'BEGIN{dx=a-x; dz=b-z; l=sqrt(dx*dx+dz*dz); dx/=l; dz/=l; c=cos(75*3.14159/180); n=s*sin(75*3.14159/180); printf "%d %d\n", x+2500*(dx*c-dz*n), z+2500*(dx*n+dz*c)}')
    echo "$(date +%T) #$k stuck at $px $pz (moved $mv cm in 12 s, the target $dist cm off): 3 s aside to $ux $uz"
    wd "ride steer $ux $uz" 1.2; xt a1 "btn R a 1"; sleep 0.25; xt a0 "btn R a 0"; sleep 1.6; SPR=0; HX=(); HZ=(); HT=(); continue; fi
  if [ -n "$tgt" ]; then read tx tz dist <<< "$tgt"; wd "ride steer $tx $tz" 0.1
    if tail -n +$LB $L | grep -aqE "Go to him, Talion|track the hunter"; then bg_end; break; fi
    if tail -n +$LB $L | grep -aqE "Stop him before he gets reinforcements" && ! win; then chase; continue; fi; fi
  if [ -n "$dist" ] && [ "$dist" -lt 900 ] || [ $((k % 4)) = 0 ]; then xt b1 "btn R trigger 1"; sleep 0.2; xt b0 "btn R trigger 0"; fi
  if [ "${SPR:-0}" = 0 ] && [ -n "$dist" ] && [ "$dist" -ge 600 ] && [ "$dist" -lt 1500 ] && [ $((k % 5)) = 2 ]; then xt a1 "btn R a 1"; sleep 0.25; xt a0 "btn R a 0"; fi
  [ $((k % 10)) = 1 ] && echo "$(date +%T) #$k Talion ${px:-?} ${pz:-?}, target ${tgt:-none} (Torvin ${TV:-?}) | $(grep -aE "subtitle ch=1 '" $L | tail -1 | cut -d"'" -f2 | cut -c1-70)"
  [ $((k % 40)) = 0 ] && dump bg-ride-$k
  if [ $SHOOT = 0 ] && tail -n +$LB $L | grep -aqE "can you shoot from one|Tut_CaragorRangedAttacks|Care to double down"; then SHOOT=1; bowtry; fi
  if [ $SHOOT = 1 ] && tail -n +$LB $L | grep -aqE "a fresh batch"; then SHOOT=2; bowtry; fi
  CH=0; tail -n +$LB $L | grep -aqE "Stop him before he gets reinforcements" && CH=1
  if [ $CH = 1 ] && win; then WT0=${WT0:-$SECONDS}; [ $((SECONDS - WT0)) -lt 4 ] && { echo "$(date +%T) a window in the chase: riding on"; continue; }; else WT0=""; fi
  if win; then echo "$(date +%T) a window (chase $CH)"; ev $l0 6
    [ $CH = 1 ] || { wd "ride steer off" 0.1; dump bg-win-$k; sleep 2; }
    printf 'shot st-bg-win-%s\n' $k >> $R/cmd; u0=$(cnt "USE t=.* 285 'QuestFailedRetry'")
    for a in 1 2 3; do sleep 1.5; win || break; xt a1 "btn R a 1"; sleep 0.3; xt a0 "btn R a 0"; sleep 2.5
      [ "$(cnt "USE t=.* 285 'QuestFailedRetry'")" -gt "$u0" ] && { fails=$((fails + 1)); echo "$(date +%T) a failed screen: Retry ($fails)"; break; }; done
    if win && [ "$(cnt "USE t=.* 285 'QuestFailedRetry'")" = "$u0" ]; then sleep 6; if win; then printf 'shot st-bg-win-%s-b\n' $k >> $R/cmd; sleep 1.5
      echo "$(date +%T) a window three A's did not close and no Retry: B (a result's Back)"; xt bb "btn R b 1"; sleep 0.3; xt b0 "btn R b 0"; sleep 4; fi; fi
    ev $l0 8; WT0=""; HX=(); HZ=(); HT=(); [ $fails -ge 30 ] && break
  fi
  ML=$(cnt "show 'GP_Torvin01_Tut_MountSprint'"); if [ "$ML" -gt "${MLS:-0}" ]; then MLS=$ML; dump bg-mountsprint; printf 'shot st-bg-mountsprint-%s\n' $ML >> $R/cmd; fi
  ptut
  # the caragor's health aid (a test aid, `fight health 9999` on the nearest AI = the ridden beast) at each seat once a
  # failed screen has come (the drill cannot bite and dodge archers as a rider does: "Your Caragor was killed")
  NS=$(cnt '^RIDE t=.* seated on'); if [ $fails -ge ${AID_AFTER:-1} ] && [ "$NS" -gt "${NSA:-0}" ] && grep -aE "^RIDE t=.*(dismounted|off: standing|seated on|mounted)" $L | tail -1 | grep -q 'seated on'; then NSA=$NS; sleep 1
    l1=$(wc -l < $L); wd "fight health 9999" 0.8; echo "$(date +%T) the aid: $(tail -n +$l1 $L | grep -a '^FIGHT' | tail -1 | cut -c1-170)"; fi
  LW=$(grep -an 'window 1 -> 0' $L | tail -1 | cut -d: -f1); [ -n "$LW" ] && [ "$LW" != "${LWP:-}" ] && { LWP=$LW; LWT=$SECONDS; }
  if [ $((SECONDS - ${LWT:-0})) -ge 8 ] && grep -aE "^RIDE t=.*(dismounted|off: standing|seated on|mounted)" $L | tail -1 | grep -qE "off: standing|dismounted" &&
     { l1=$(wc -l < $L); wd "ride status" 0.8; tail -n +$l1 $L | grep -aq '^RIDE status: off'; }; then # (a Retry re-seats Talion itself within ~5 s)
    echo "$(date +%T) off the beast: the remount"; wd "ride steer off" 0.1; sleep 2
    PICK=Beckon remount || PICK=Warg_ remount || PICK=' orc ' remount
    HX=(); HZ=(); HT=()
  fi
  ev $l0 4 | grep -v '^$'
done
wd "ride steer off" 0.2; wd "ride status" 0.5; dump bg-end
SYNC_S=60 sync "status" "word practice status nav" >/dev/null; grep -aE "^PRACTICE nav [0-9]+ '(golden|Objective|power|quest|Quest|mission|resistance)" $L | tail -8 | cut -c1-120
echo "DIRECTOR END $(date +%T)"

