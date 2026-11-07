# nurn41/m13bow.sh - som-b42-saddle (docs/re-xr-pipeline.md 186): the bow from the saddle in Big Game by a real aim,
# after m13a.sh (seated on the caragor). Boot: scripts/ladder_boot.sh 13 nurn41-drive.sh NU_MISSION="m13a.sh m13bow.sh"
# NU_CTL=0 NU_END=0, LADDER_EXTRA="SOMVR_BOW_HITLOG=1". SB_STEPS (default "still move ride", in that order):
#   still  the beast standing: an Uruk put 18 m ahead (`practice put`, Talion unseen: `story invisible on` - an aid, so
#          it stands), marked (`bow target X Y Z`: the BOW target lines on it), the bow out at the left shoulder, nocked,
#          drawn to the cheek, aimed by the hands alone at its head (the lines' error fed back into the poses), loosed
#   move   the same with the beast ridden straight at a second one from ~30 m (`ride steer`: a thumb on the stick), loosed
#          at ~15-20 m while it runs
#   ride   m13b.sh's ride (the fight, the stages) with its bow replaced: at "You take 'em up high" (the archers' stage)
#          each archer's marker marked and shot the same way (the beast standing, then ridden at the next one); the
#          ride ends after the archers' stage (SB_RIDE_ON=1: on to the chase and the end, as m13b.sh)
# The hands are a real bow hold (OpenXR grip: -Z runs pinky -> thumb; `90 0 0` = the fist upright, bow37/abil27c's):
# the string hand anchored at the cheek (3 right, 10 down, 3 back of the eyes: bow37's DRAWN), the bow hand 63 cm out
# from it along the aim, 1.2 cm left (the arrow beside the riser). No `bow aimhead`: the shot's line is the bow's own.
# Each shot: `SHOT tag ...` lines in RUN/saddle42.txt (the mod's miss line, the hit, the health) and pictures.
SB=${SB_STEPS:-still move ride}; SBF=$R/saddle42.txt; X=$R/xr-layer.log
sbr() { echo "$(date +%T) $*" | tee -a "$SBF"; }
sbhas() { case " $SB " in *" $1 "*) return 0 ;; esac; return 1; }
# the pose: the aim's yaw A / pitch P (deg; the head's yaw frame / the horizon), the string hand D cm back from the bow
spose() { awk -v a="$1" -v p="$2" -v d="${3:-63}" -v sm="${4:-}" 'BEGIN { r = 3.14159265 / 180; L = 63
  dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); cx = 3; cy = -10; cz = -3
  printf "hand L head %.1f %.1f %.1f 90 0 0\n", cx + L * dx - 1.2, cy + L * dy, cz + L * dz
  printf "hand R head %.1f %.1f %.1f 90 0 0%s\n", cx + (L - d) * dx, cy + (L - d) * dy, cz + (L - d) * dz, sm != "" ? " s " sm : "" }'; }
sset() { local IFS=$'\n'; x $(spose "$1" "$2" "$3" "$4") "btn L squeeze 1" "btn R trigger ${5:-1}"; }
TRE='^BOW target t=.*: the marked orc'
tline() { grep -aE "$TRE" "$L" | tail -1; }
terr() { tline | sed -nE 's/.*its head ([-+][0-9.]+) deg right ([-+][0-9.]+) deg up;.*/\1 \2/p'; }
tdir() { tline | sed -nE 's/.*the head at head yaw ([-+][0-9.]+) pitch ([-+][0-9.]+).*/\1 \2/p'; }
tm() { tline | sed -nE 's/.* ([0-9.]+) m from the nock.*/\1/p'; }
tnew() { local n0=$1 i; for i in $(seq 1 ${2:-15}); do [ "$(cnt "$TRE")" -gt "$n0" ] && return 0; sleep 0.1; done; return 1; }
isout() { grep -aE '^BOW t=.* (exit|enter)' "$L" | tail -1 | grep -q enter; }
HY=0; HP=0
hd() { HY=$1; HP=${2:-0}; TS=0.5 x "head 0 0 0 yaw $HY pitch $HP s 0.3"; }
bow_away() { x "hand L head -12 -10 -15 90 0 0" "hand R head 22 -40 15 0 0 0" "btn L squeeze 1" "btn R trigger 0"; sleep 0.3
  x "hand L head -12 -10 -15 90 0 0" "btn L squeeze 0"; sleep 0.6; x "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"; }
# the bow out at the left shoulder (SOMVR_BOW_TAKE=shoulder: the grip there), held straight out
bow_out() { local n0; n0=$(cnt '^BOW t=.* enter ')
  x "btn L squeeze 0" "btn R squeeze 0" "btn R trigger 0"; sleep 0.3
  x "hand L head -12 -10 -15 90 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.6; x "hand L head -12 -10 -15 90 0 0" "btn L squeeze 1"; sleep 1.2
  [ "$(cnt '^BOW t=.* enter ')" -gt "$n0" ] || { sbr "  no bow out: $(grep -a '^BOW' "$L" | tail -1 | cut -c1-160)"; return 1; }
  x "hand L head -8 -15 55 90 0 0" "btn L squeeze 1"; sleep 0.5; }
# saim N TOL: the head's error from the lines fed back into the aim (A, P) N times at most, until both under TOL deg
saim() { local k e dx dy n0
  for k in $(seq 1 ${1:-7}); do n0=$(cnt "$TRE"); tnew "$n0" 30 || { sbr "  no marked line in 3 s"; return 1; }
    e=$(terr); [ -n "$e" ] || return 1; dx=${e% *}; dy=${e#* }
    awk -v a="$dx" -v b="$dy" -v t="${2:-0.35}" 'BEGIN{exit !(a < t && a > -t && b < t && b > -t)}' && { AIMK=$k; return 0; }
    [ -n "${SAIM_NEAR:-}" ] && awk -v m="$(tm)" -v n="$SAIM_NEAR" 'BEGIN{exit !(m != "" && m < n)}' && { AIMK=$k; sbr "  under $SAIM_NEAR m: loosed as it is"; return 1; }
    read A P < <(awk -v a="$A" -v p="$P" -v x="$dx" -v y="$dy" 'BEGIN{ if (x > 8) x = 8; if (x < -8) x = -8; if (y > 8) y = 8; if (y < -8) y = -8
      printf "%.2f %.2f\n", a + x, p + y }')
    TS=0.15 sset "$A" "$P" 63 "" 1
  done; AIMK=$k; return 1; }
# sshot TAG [STEER_X STEER_Z]: one shot at the marked orc (marked before): out, nocked, drawn, the head turned to it when
# it is more than 12 deg off, aimed, loosed; with STEER the beast is ridden at that point from the draw on
sshot() { local tag=$1 sx=${2:-} sz=${3:-} n0 nh d e t0 ok l0 try
  for try in 1 2; do
    l0=$(lines); sbr "SHOT $tag (try $try): the bow out$([ -n "$sx" ] && echo ", ridden at $sx $sz")"
    [ "${SB_TRACED:-0}" = 0 ] && { SB_TRACED=1; w "ride trace 160" 0.1; } # the first: the view frame by frame at the enter
    bow_out || { bow_away; continue; }
    t0=$SECONDS; A=0; P=0
    TS=0.4 sset 0 0 4 "" 0; TS=0.6 sset 0 0 4 "" 1
    since "$l0" '^BOW t=.* nock: ' | grep -q . || { sleep 0.6; since "$l0" '^BOW t=.* nock: ' | grep -q . || sbr "  no nock: $(since "$l0" '^BOW t=' | tail -1 | cut -c1-160)"; }
    TS=1.0 sset 0 0 63 0.6 1
    [ -n "$sx" ] && w "ride steer $sx $sz" 0.1
    n0=$(cnt "$TRE"); tnew "$n0" 30 || { sbr "  no marked line in 3 s (the orc gone?): $(grep -aE '^BOW (target|t=.* (enter|exit|nock|full))' "$L" | tail -2 | cut -c1-140 | tr '\n' '|')"; bow_away; return 1; }
    d=$(tdir); A=0; P=${d#* }; e=${d% *}
    if awk -v a="$e" 'BEGIN{exit !(a > 12 || a < -12)}'; then # the head (and the aim) to it: a person looks first
      hd "$(awk -v h=$HY -v a="$e" 'BEGIN{printf "%.1f", h + a}')" "$(awk -v p="$P" 'BEGIN{printf "%.0f", p > 0 ? p : 0}')"
      if [ $try = 1 ]; then sbr "  the head turned $e deg to it: the bow away, then again (the game's mounted aim lasts ~12 s)"; [ -n "$sx" ] && w "ride steer off" 0.1; bow_away; sleep 1; continue; fi
    fi
    TS=0.2 sset "$A" "$P" 63 "" 1
    if [ -n "$sx" ]; then SAIM_NEAR=9 saim 10 0.35; else saim 8 0.35; fi; ok=$?
    sbr "  aimed (rounds $AIMK, $((SECONDS - t0)) s since the draw$([ $ok = 0 ] || echo ', NOT within 0.35 deg')): $(tline | sed -E 's/^BOW target t=([0-9.]+): /t=\1 /' | cut -c1-260)"
    pics "sb-$tag"
    sbr "  the beast: $(grep -aE '^RIDE .*the beast at [0-9.]+ m/s now' "$L" | tail -1 | grep -oE 'the beast at [0-9.]+ m/s now' | head -1)"
    nh=$(cnt '^BOW hit t='); n0=$(lines)
    TS=0.2 x "btn L squeeze 1" "btn R trigger 0"; sleep 2.2
    [ -n "$sx" ] && w "ride steer off" 0.1
    sbr "  the shot: $(since "$n0" '^BOW t=.* shot at the marked orc' | tail -1 | sed -E 's/^BOW t=[0-9.]+ //' | cut -c1-230)"
    sbr "  assist: $(since "$n0" '^BOW t=.* assist: ' | tail -1 | sed -E 's/^BOW t=[0-9.]+ assist: //' | cut -c1-150)"
    since "$n0" '^BOW hit t=' | head -3 | cut -c1-260 | sed 's/^/  /' | tee -a "$SBF"
    sleep 1.2; since "$n0" '^BOW hit check|^PRACTICE t=.*(dead|kill)' | head -4 | cut -c1-240 | sed 's/^/  /' | tee -a "$SBF"
    HIT=$(( $(cnt '^BOW hit t=') - nh ))
    sbr "SHOT $tag: $HIT game hit line(s)"
    bow_away; return 0
  done; bow_away; return 1; }
seated() { grep -aE "^RIDE t=.*(dismounted|off: standing|seated on|mounted)" "$L" | tail -1 | grep -q 'seated on'; }
# the beast stopped: the stick centred, its speed under 0.5 m/s (RIDE motion), 6 s at most
stopbeast() { local i v; w "ride steer off" 0.2; w "stick 0 0 0 0.1" 0.2
  for i in $(seq 1 12); do v=$(grep -aE '^RIDE .*the beast at [0-9.]+ m/s now' "$L" | tail -1 | grep -oE 'the beast at [0-9.]+' | grep -oE '[0-9.]+$')
    awk -v v="${v:-9}" 'BEGIN{exit !(v < 0.5)}' && break; sleep 0.5; done; sbr "  the beast at ${v:-?} m/s"; }
# put an Uruk N cm ahead of the beast (the mount = the orc within 1.5 m of Talion: its facing; else Torvin's bearing),
# turned S deg; the spawn retried 25 deg over (4 tries); marked where the game put it (the spawn check's place)
putorc() { local N=$1 px py pz f k p try s0=${2:-0} PP
  for try in 1 2 3 4; do
    read px py pz < <(talion); [ -n "$pz" ] || { sleep 1; continue; }
    f=$(orcs "${px%.*}" "${pz%.*}" 400 | awk -v x="$px" -v z="$pz" '{ d = sqrt(($2-x)^2 + ($4-z)^2); if (d < 150) { print $5; exit } }')
    p=$(awk -v x="$px" -v y="$py" -v z="$pz" -v f="${f:-0}" -v n="$N" -v s="$(( s0 + (try - 1) * 25 ))" 'BEGIN { r = (f + s) * 3.14159265 / 180; printf "%.0f %.0f %.0f", x + n * sin(r), y, z + n * cos(r) }')
    k=$(lines); w "practice put $p infantry" 0.3
    for i in $(seq 1 20); do since "$k" '^PRACTICE t=.*spawn put' | grep -q . && break; sleep 0.2; done
    PP=$(since "$k" '^PRACTICE t=.*spawn put' | tail -1)
    sbr "put $try: the beast facing ${f:-?} deg, Talion $px $py $pz, asked $p: $(echo "$PP" | cut -c1-190)"
    echo "$PP" | grep -q ' ok: ' || continue
    for i in $(seq 1 20); do since "$k" '^PRACTICE t=.*spawn check 1: .* at \(' | grep -q . && break; sleep 0.2; done
    POS=$(since "$k" '^PRACTICE t=.*spawn check 1: .* at \(' | tail -1 | grep -oE 'at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tr -d '()' | cut -c4-)
    [ -n "$POS" ] || POS=$(echo "$PP" | grep -oE 'at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tr -d '()' | cut -c4-)
    case "${POS:-0.0 0.0 0.0}" in "0.0 0.0 0.0") continue ;; esac
    sbr "  marked where the game put it: $POS"; w "bow target $POS" 0.3; return 0
  done; return 1; }
# not seated after m13a.sh (boot 20261106-222636-1: the caragor kept its light attacks close, no leap to counter): Talion
# put 14 m back from it on the line through him (an aid, as m13a's trip to the beasts' ground), so it charges - Torvin's
# "Let the bastard charge you!" - then m13a's own counter and mount (the blade held across, the left hand closed)
mount2() { local k l1 o px py pz C i
  for k in 1 2 3 4 5 6; do seated && return 0
    read px py pz < <(talion); [ -n "$pz" ] || { sleep 2; continue; }
    o=$(grep -aoE '^(THREAT reach|DEFENCE hit) orc=0x[0-9a-f]+ attack .AI_Warg' "$L" | tail -1 | grep -oE '0x[0-9a-f]+')
    C=$(orcs "${px%.*}" "${pz%.*}" 3000 | awk -v o="$o" '$1 == o' | head -1); [ -n "$C" ] || C=$(orcs "${px%.*}" "${pz%.*}" 3000 | head -1)
    [ -n "$C" ] || { sbr "mount2 $k: no beast within 30 m"; sleep 3; continue; }
    set -- $C; read gx gz < <(awk -v cx=$2 -v cz=$4 -v x=$px -v z=$pz 'BEGIN { dx = x - cx; dz = z - cz; l = sqrt(dx*dx + dz*dz) + 1; printf "%.0f %.0f\n", cx + 1400 * dx / l, cz + 1400 * dz / l }')
    sbr "mount2 $k: the beast $1 at $2 $4 (Talion $px $pz): to 14 m from it ($gx $gz)"; go "$gx" "$(( ${py%.*} + 100 ))" "$gz"
    w "hands test R 30 -10 45 0 -35 0" 0.3; w "ride guard on" 0.2; w "ride face on" 0.2; w "hands test L -25 -35 30 0 0 0" 0.3
    n0=$(cnt 'DEFENCE countered.*JumpAttack')
    for i in $(seq 1 160); do [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] && break; sleep 0.25; done
    [ "$(cnt 'DEFENCE countered.*JumpAttack')" -gt "$n0" ] || { sbr "mount2 $k: no counter in 40 s"; continue; }
    l1=$(lines); sleep 3.1
    for i in $(seq 1 10); do since "$l1" "^RIDE t=.* the game offers the mount" | grep -q . && break; w "stick 0 1 0 0.4" 0.05; sleep 0.7; done
    w "stick 0 0 0 0.1" 0.05; w "hands squeeze L 1" 0.3
    for i in $(seq 1 100); do since "$l1" "^RIDE t=.* mounted|PC_Warg_Domination_Fail" | grep -q . && break; sleep 0.1; done
    if since "$l1" '^RIDE t=.* mounted' | grep -q .; then sbr "mount2 $k: mounted"; w "ride face off" 0.2; w "ride guard off" 0.2
      for i in $(seq 1 40); do since "$l1" "^RIDE t=.* seated on" | grep -q . && break; sleep 0.25; done; sleep 1.5; w "hands squeeze L 0" 0.3; return 0; fi
    w "hands squeeze L 0" 0.2; sbr "mount2 $k: not mounted"
  done; return 1; }
echo "PHASE saddle42 $(date +%T) steps: $SB" | tee -a "$SBF"
seated || { sbr "not seated after m13a.sh: mount2"; mount2 || sbr "mount2: NOT mounted"; }
w "ride steer off" 0.3; w "ride face off" 0.2; w "ride guard off" 0.2
w "hands test off" 0.3; w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3 # the layer's controllers (real poses)
hd 0 0
seated || sbr "not seated at the start: $(grep -aE '^RIDE t=' "$L" | tail -1 | cut -c1-160)"
w "bow target off" 0.2; hd 0 0; w "ride steer off" 0.3; w "hands test off" 0.3; w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3
if ! seated && type remount > /dev/null 2>&1; then sbr "off the beast after the ride: the remount (m13b's)"; PICK=Beckon remount || PICK=Warg_ remount; fi
seated || sbr "NOT seated for the spawned orcs' shots: $(grep -aE '^RIDE t=' "$L" | tail -1 | cut -c1-160)"
if sbhas still; then sbr "== still: the beast standing"; invis on; sleep 1
  for k in 1 2 3; do stopbeast; putorc 1800 $(( (k - 1) * 25 )) && { sleep 1.5; stopbeast; sshot "still$k"; }; done
  invis off; fi
if sbhas move; then sbr "== move: ridden straight at it"; invis on; sleep 1
  for k in 1 2 3; do stopbeast; putorc 4000 $(( (k - 1) * -20 )) && { sleep 1.5; set -- $POS; sshot "move$k" "$1" "$3"; }; w "ride steer off" 0.3; sleep 1; done
  invis off; fi
w "bow target off" 0.2; hd 0 -10
if sbhas ride; then sbr "== ride: Big Game's archers"
  # m13b.sh with its bow (`bowtry`) replaced by the archers' shots below: called at "You take 'em up high"
  ARCH=0
  bowtry() { local l0 M k n=0 px pz; ARCH=$((ARCH + 1)); [ $ARCH -gt 2 ] && return 0
    sbr "== the archers' stage ($ARCH): $(grep -aE "subtitle ch=1 '" "$L" | tail -1 | cut -d"'" -f2 | cut -c1-80)"
    wd "ride steer off" 0.4; wd "hands test off" 0.3; wd "hands squeeze L -1" 0.3; wd "hands squeeze R -1" 0.3
    for k in 1 2 3 4 5 6; do
      l0=$(lines); wd "objective status" 1.2; wd "practice status" 0.4
      read px pz < <(tail -n +$l0 $L | grep -aoE 'Talion known at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+\)' | tail -1 | grep -oE -- '-?[0-9.]+' | awk 'NR==1{x=$1} NR==3{printf "%d %d\n",x,$1}')
      M=$(tail -n +$l0 $L | grep -aE "^OBJECTIVE status marker .*'Objective' " | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4- |
          awk -v x="${px:-0}" -v z="${pz:-0}" '!s[$0]++ { printf "%d %s\n", sqrt(($1-x)^2 + ($3-z)^2), $0 }' | sort -n | head -1)
      [ -n "$M" ] || { sbr "  no archer marker (round $k)"; break; }
      set -- $M; sbr "  the nearest archer marker $2 $3 $4, $1 cm off"
      if [ "$1" -gt 3800 ]; then sbr "  over 38 m: ridden to 25 m of it"; wd "ride steer $2 $4" 0.1
        for i in $(seq 1 40); do read px pz < <(talion | awk '{printf "%d %d\n",$1,$3}'); [ -n "$pz" ] && [ "$(awk -v a=$2 -v b=$4 -v x=$px -v z=$pz 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')" -lt 2500 ] && break; done
        wd "ride steer off" 0.6; continue; fi
      w "bow target $2 $3 $4" 0.3; n=$((n + 1))
      if [ $((n % 2)) = 0 ] && [ "$1" -gt 2400 ]; then sshot "archer$n-ridden" "$2" "$4"; else sshot "archer$n"; fi
      w "ride steer off" 0.3; w "bow target off" 0.2
    done
    hd 0 -10; [ "${SB_RIDE_ON:-0}" = 1 ] || { sbr "== the archers' stage done: the ride ends here"; T0=$((SECONDS - 100000)); }; }
  sed -E -e 's/^bowtry\(\) \{/bowtry_m13b() {/' -e 's/can you shoot from one\|Tut_CaragorRangedAttacks\|Care to double down/take .em up high|Tut_CaragorRangedAttacks/' \
      "$HERE/nurn41/m13b.sh" > "$R/m13b-saddle42.sh"
  grep -c 'take .em up high' "$R/m13b-saddle42.sh" | sed 's/^/m13b copy: the stage words /'
  BOW0=0 AID_AFTER=${AID_AFTER:-0} . "$R/m13b-saddle42.sh"   # (the beast's health aid from the first seat: m13b's own)
fi
sbr "== saddle42 done: $(grep -c '^.* SHOT .*: [0-9]* game hit' "$SBF") shots, $(grep -cE 'SHOT .*: [1-9][0-9]* game hit' "$SBF") with a game hit"
