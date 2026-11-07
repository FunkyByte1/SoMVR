# nurn41/m13view.sh - som-b42-mountview (docs/re-xr-pipeline.md 188): the view on a caragor while the VR bow comes out
# and goes away (standing, running, turning), the beast's own bites, then Big Game's archers shot from the saddle -
# standing, and ridden round the archer (an orbit ~26 m out: the beast runs, the archer stays in range and in view).
# Boot: rung 13 after m13a.sh (seated on the caragor):
#   scripts/ladder_boot.sh 13 nurn41-drive.sh NU_CTL=0 NU_END=0 NU_MISSION="m13a.sh m13view.sh" (the archers' stage save of
#   boot 230319 restarts the mission at the Forge Tower: no use)
#   with LADDER_EXTRA="SOMVR_BOW_HITLOG=1 SOMVR_RIDE_TRACE=60000" (every seated frame's RIDE view line: the analysis is
#   scripts/ride_steps.py RUN). MV_STEPS (default "stand run bite archers"). Phase stamps: `ride status` words (their
#   TRIGGER lines carry the game time) and RUN/mountview42.txt.
# The functions from spose to mount2 are m13bow3.sh's (som-b42-saddle): a real bow hold (OpenXR grip: -Z pinky ->
# thumb; `90 0 0` = the fist upright), the string hand at the cheek, the bow hand 63 cm out along the aim.
MV=${MV_STEPS:-stand run bite archers}; SB=ride; SB_TRACED=1 # (sshot's `ride trace 160` would cut SOMVR_RIDE_TRACE short)
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
    [ -n "${AIMAT:-}" ] && w "bow aimhead $AIMAT" 0.2 # (122's: the shot's ray onto the Head node of the orc nearest that place)
    nh=$(cnt '^BOW hit t='); n0=$(lines)
    TS=0.2 x "btn L squeeze 1" "btn R trigger 0"; sleep 2.2
    sbr "  aimhead: $(since "$n0" '^BOW t=.* test: the shot.s ray turned' | tail -1 | sed -E 's/^BOW t=[0-9.]+ test: //' | cut -c1-160)"
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
SBF=$R/mountview42.txt
mvhas() { case " $MV " in *" $1 "*) return 0 ;; esac; return 1; }
stamp() { sbr "MV $*"; w "ride status" 0.1; }
# the beast from the newest RIDE view line (the trace): x z heading(deg)
bpos() { grep -a '^RIDE view ' "$L" | tail -1 | sed -nE 's/.* beast (-?[0-9.]+),-?[0-9.]+,(-?[0-9.]+) hdg (-?[0-9.]+) .*/\1 \2 \3/p'; }
bspeed() { grep -aE '^RIDE .*the beast at [0-9.]+ m/s now' "$L" | tail -1 | grep -oE 'the beast at [0-9.]+' | grep -oE '[0-9.]+$'; }
# a point D cm from the beast at its heading + DEG
ahead() { read bx bz bh < <(bpos); awk -v x="$bx" -v z="$bz" -v h="$bh" -v a="${2:-0}" -v d="$1" 'BEGIN { r = (h + a) * 3.14159265 / 180; printf "%.0f %.0f\n", x + d * sin(r), z + d * cos(r) }'; }
nock() { TS=0.4 sset 0 0 4 "" 0; TS=0.6 sset 0 0 4 "" 1; TS=1.0 sset 0 0 63 0.6 1; }
loose() { TS=0.2 x "btn L squeeze 1" "btn R trigger 0"; sleep 1.0; }
cycle() { # TAG [DRAW]: the bow out, (nocked, drawn, held, loosed), away
  stamp "$1: the bow out"; bow_out || { bow_away; return 1; }
  if [ "${2:-1}" = 1 ]; then stamp "$1: nock + draw"; nock; sleep 1.5; stamp "$1: loose"; loose; fi
  stamp "$1: the bow away"; bow_away; sleep 1.5; stamp "$1: done"; }
steer_for() { # X Z S: re-steered to the point each 0.5 s for S s (the beast's frame stick; the thumb on it)
  local i; for i in $(seq 1 $(( $3 * 2 ))); do w "ride steer $1 $2" 0.05; done; }
echo "PHASE mountview42 $(date +%T) steps: $MV" | tee -a "$SBF"
seated || { sbr "not seated at the start: mount2"; mount2 || sbr "mount2: NOT mounted"; }
w "ride steer off" 0.3; w "ride face off" 0.2; w "ride guard off" 0.2
w "hands test off" 0.3; w "hands squeeze L -1" 0.3; w "hands squeeze R -1" 0.3 # the layer's controllers (real poses)
hd 0 0; bow_away
seated || sbr "not seated: $(grep -aE '^RIDE t=' "$L" | tail -1 | cut -c1-160)"
# exp (MV_EXP=1; default 0 - boot 005410 ran it right after the mount, when the beast took no stick at all): the game's aim command 124 alone (`ride press 124 MS`, no VR bow) on a running
# caragor - x1 held while the stick stays pushed (does the beast brake? surge at the end?); x2 the stick let go and
# pushed again while 124 is held (does it run again?); x3 the stick turned 90 deg while 124 is held
xrun() { read gx gz < <(ahead 6000 0); stamp "$1: run at $gx $gz"; for i in 1 2 3 4 5 6; do w "ride steer $gx $gz" 0.4; done; stamp "$1: running $(bspeed2) m/s"; }
if [ "${MV_EXP:-0}" = 1 ] && seated; then sbr "== exp: the game's aim command 124 on a running caragor (no VR bow)"
  stopbeast; xrun x1; w "ride press 124 4500" 0.05; stamp "x1: 124 held 4.5 s, the stick pushed"
  for i in $(seq 1 9); do w "ride steer $gx $gz" 0.4; done; stamp "x1: 124 off ($(bspeed2) m/s)"
  for i in 1 2 3 4; do w "ride steer $gx $gz" 0.4; done; stamp "x1: after ($(bspeed2) m/s)"; w "ride steer off" 0.2; stopbeast
  xrun x2; w "ride press 124 4500" 0.05; stamp "x2: 124 held"; for i in 1 2 3; do w "ride steer $gx $gz" 0.4; done
  w "ride steer off" 0.6; stamp "x2: the stick let go ($(bspeed2) m/s)"; for i in 1 2 3 4; do w "ride steer $gx $gz" 0.4; done
  stamp "x2: pushed again ($(bspeed2) m/s)"; for i in 1 2 3; do w "ride steer $gx $gz" 0.4; done; stamp "x2: after ($(bspeed2) m/s)"
  w "ride steer off" 0.2; stopbeast
  xrun x3; w "ride press 124 4500" 0.05; read gx gz < <(ahead 4000 90); stamp "x3: 124 held, steered 90 deg right ($gx $gz)"
  for i in $(seq 1 8); do w "ride steer $gx $gz" 0.4; done; stamp "x3: done ($(bspeed2) m/s)"; w "ride steer off" 0.2; stopbeast
fi
if mvhas stand && seated; then sbr "== stand: the beast standing, the bow out and away"
  stopbeast; cycle stand0 0; stopbeast; cycle stand1; stopbeast; cycle stand2; stopbeast; cycle stand3
fi
if mvhas run && seated; then sbr "== run: the beast running straight, then turning, the bow out and away"
  read gx gz < <(ahead 9000 0); stamp "run: steered at $gx $gz"; steer_for "$gx" "$gz" 2
  stamp "run1: the bow out (running)"; ( steer_for "$gx" "$gz" 9 ) & SP=$!; cycle run1; wait $SP
  read gx gz < <(ahead 4000 90); stamp "turn: steered right at $gx $gz"; ( steer_for "$gx" "$gz" 6 ) & SP=$!; cycle turn1; wait $SP
  read gx gz < <(ahead 4000 -100); stamp "turn: steered left at $gx $gz"; ( steer_for "$gx" "$gz" 6 ) & SP=$!; cycle turn2 0; wait $SP
  w "ride steer off" 0.2; stopbeast
fi
if mvhas bite && seated; then sbr "== bite: the beast's own bite, standing then running"
  stopbeast; for k in 1 2; do stamp "bite$k standing"; w "ride press 186 300" 2.5; done
  read gx gz < <(ahead 9000 0); stamp "bite: running at $gx $gz"; steer_for "$gx" "$gz" 2
  for k in 3 4; do stamp "bite$k running"; w "ride press 186 300" 0.1; steer_for "$gx" "$gz" 2; done
  w "ride steer off" 0.2; stopbeast
fi
# bspeed2: the beast's speed now (m/s) from the newest two consecutive RIDE view lines (the trace)
bspeed2() { grep -a '^RIDE view f=' "$L" | tail -8 | sed -nE 's/^RIDE view f=([0-9]+) t=([0-9.]+) seated beast (-?[0-9.]+),-?[0-9.]+,(-?[0-9.]+) hdg .*/\1 \2 \3 \4/p' |
  awk 'NR == 1 { t = $2; x = $3; z = $4 } { T = $2; X = $3; Z = $4 } END { if (NR > 1 && T > t) printf "%.1f\n", sqrt((X-x)^2 + (Z-z)^2) / (T - t) / 100; else print 0 }'; }
# face AX AZ: the beast turned to face that point (within 15 deg; the stick toward it - a caragor turns as it moves), stopped
face() { local i bx bz bh d; for i in $(seq 1 10); do read bx bz bh < <(bpos)
    d=$(awk -v x="$bx" -v z="$bz" -v h="$bh" -v ax="$1" -v az="$2" 'BEGIN { b = atan2(ax - x, az - z) * 57.29578; d = b - h; while (d > 180) d -= 360; while (d < -180) d += 360; printf "%.0f", d }')
    sbr "  face: the archer ${d} deg off the beast's heading"; [ "${d#-}" -le 15 ] && break; w "ride steer $1 $2" 0.4; done; stopbeast; }
# the beast's distance to a point (cm, horizontal)
bdist() { read bx bz bh < <(bpos); awk -v x="$bx" -v z="$bz" -v ax="$1" -v az="$2" 'BEGIN { printf "%d\n", sqrt((ax-x)^2 + (az-z)^2) }'; }
# rshot TAG AX AY AZ: the archer shot from a running beast, as a rider charging it: the beast ridden straight at it
# (`ride steer`: the stick toward it, latched) from 50 m or more (closer: first ridden out to ~70 m); the bow out while
# it runs (SOMVR_RIDE_BOW_PACE keeps the pace under the game's aim), nocked and drawn; the aim from where the archer is
# (straight ahead along the run - the bearing holds when running at it; the pitch from its height and distance: a
# player looks at it); within 33 m at 3 m/s or more, `bow aimhead` (122: the shot's ray onto its Head node - the last few
# degrees; its turn logged) and loosed while the beast runs; the hit, the health. No target lines (build-win10 has no
# `bow target`)
aimp() { read bx bz bh < <(bpos); by=$(grep -a '^RIDE view f=' "$L" | tail -1 | sed -nE 's/.* seat -?[0-9.]+,(-?[0-9.]+),.*/\1/p')
  awk -v x="$bx" -v z="$bz" -v y="${by:-0}" -v ax="$1" -v ay="$2" -v az="$3" 'BEGIN { d = sqrt((ax-x)^2 + (az-z)^2); printf "%.1f\n", atan2(ay + 150 - y, d) * 57.29578 }'; }
# the archer's own place: the orc nearest its 'Objective' marker within 20 m (`verbs orcs X Z R`; the marker is not where
# it stands - 012206: 15 m off) -> AX AY AZ (unchanged when none)
archer_at() { local l0 o; l0=$(lines); w "verbs orcs $1 $3 2000" 1.2
  o=$(since "$l0" '^VERBS orcs t=.* orc 0x[0-9a-f]+ at ' | sed -nE 's/.* at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing.*/\1 \2 \3/p' |
      awk -v x="$1" -v y="$2" -v z="$3" '{ d = sqrt(($1-x)^2 + ($2-y)^2 + ($3-z)^2); if (d < 2000 && (b == "" || d < b)) { b = d; p = $0 } } END { if (p != "") print p }')
  [ -n "$o" ] && { sbr "  the archer at $o (the marker $1 $2 $3)"; echo "$o"; } || { sbr "  no orc within 20 m of the marker: aimed at it"; echo "$1 $2 $3"; }; }
rshot() { local tag=$1 ax=$2 ay=$3 az=$4 k d v m ok=1 nh n0 l0 i gx gz
  read ax ay az < <(archer_at "$ax" "$ay" "$az" | tail -1)
  d=$(bdist $ax $az)
  if [ "$d" -lt 5000 ]; then read px pz ph < <(bpos)
    read gx gz < <(awk -v cx=$ax -v cz=$az -v x=$px -v z=$pz 'BEGIN { dx = x - cx; dz = z - cz; l = sqrt(dx*dx + dz*dz) + 1; printf "%.0f %.0f\n", cx + 7000 * dx / l, cz + 7000 * dz / l }')
    stamp "SHOT $tag: $d cm from it - ridden out to 70 m first ($gx $gz)"; w "ride steer $gx $gz" 0.2
    for i in $(seq 1 30); do sleep 0.5; [ "$(bdist $gx $gz)" -lt 600 ] && break; seated || break; done; fi
  l0=$(lines); w "ride steer $ax $az" 0.3; stamp "SHOT $tag: ridden at it from $(bdist $ax $az) cm"
  for i in $(seq 1 12); do sleep 0.3; awk -v v="$(bspeed2)" 'BEGIN{exit !(v >= 4)}' && break; done
  stamp "SHOT $tag: the bow out at $(bspeed2) m/s, $(bdist $ax $az) cm"; hd 0 0
  bow_out || { bow_away; w "ride steer off" 0.1; return 1; }
  stamp "SHOT $tag: the bow out - the beast $(bspeed2) m/s"; A=0; P=$(aimp $ax $ay $az); nock; TS=0.1 sset 0 "$P" 63 "" 1
  stamp "SHOT $tag: drawn - the beast $(bspeed2) m/s, $(bdist $ax $az) cm"
  for k in $(seq 1 40); do v=$(bspeed2); m=$(bdist $ax $az); P=$(aimp $ax $ay $az); TS=0.05 sset 0 "$P" 63 "" 1
    if awk -v v="${v:-0}" -v m="${m:-9999}" 'BEGIN{exit !(v >= 3 && m < 3300)}'; then ok=0; break; fi
    [ "${m:-9999}" -lt 1500 ] && break; sleep 0.15; done
  sbr "  $([ $ok = 0 ] || echo 'NOT ')at 3 m/s within 33 m: the beast $(bspeed2) m/s, $(bdist $ax $az) cm from the marker, the aim's pitch $P deg (round $k)"
  w "bow aimhead $ax $ay $az" 0.05
  nh=$(cnt '^BOW hit t='); n0=$(lines); stamp "SHOT $tag: loose (the beast $(bspeed2) m/s)"
  TS=0.2 x "btn L squeeze 1" "btn R trigger 0"; sbr "  after the loose: the beast $(bspeed2) m/s"; sleep 1.4
  sbr "  aimhead: $(since "$n0" '^BOW t=.* test: the shot.s ray turned' | tail -1 | sed -E 's/^BOW t=[0-9.]+ test: //' | cut -c1-160)"
  sbr "  release: $(since "$n0" '^BOW t=.* release: ' | tail -1 | sed -E 's/^BOW t=//' | cut -c1-200)"
  sbr "  assist: $(since "$n0" '^BOW t=.* assist: ' | tail -1 | sed -E 's/^BOW t=[0-9.]+ assist: //' | cut -c1-150)"
  since "$n0" '^BOW hit t=' | head -3 | cut -c1-260 | sed 's/^/  /' | tee -a "$SBF"
  sleep 1.0; since "$n0" '^BOW hit check|^PRACTICE t=.*(dead|kill)' | head -4 | cut -c1-240 | sed 's/^/  /' | tee -a "$SBF"
  sbr "SHOT $tag: $(( $(cnt '^BOW hit t=') - nh )) game hit line(s)"
  pics "mv-$tag"; bow_away; sleep 1.5; stamp "SHOT $tag: the bow away - the beast $(bspeed2) m/s"; w "ride steer off" 0.2; stopbeast; return 0; }
if mvhas archers && seated; then sbr "== archers: m13b.sh's ride with its bow replaced (at \"You take 'em up high\"): standing, then ridden round"
  ARCH=0; MVN=0
  bowtry() { local k M d px pz ph l0 g i gx gz; ARCH=$((ARCH + 1)); [ $ARCH -gt 2 ] && return 0
    stamp "the archers' stage ($ARCH): $(grep -aE "subtitle ch=1 '" "$L" | tail -1 | cut -d"'" -f2 | cut -c1-80)"
    wd "ride steer off" 0.4; wd "hands test off" 0.3; wd "hands squeeze L -1" 0.3; wd "hands squeeze R -1" 0.3
    for k in $(seq 1 ${MV_ROUNDS:-8}); do
      seated || { sbr "not seated: $(grep -aE '^RIDE t=.*(dismounted|seated on|off: standing)' "$L" | tail -1 | cut -c1-120)"; break; }
      read px pz ph < <(bpos); [ -n "$pz" ] || { sbr "no beast"; sleep 1; continue; }
      l0=$(lines); w "objective status" 1.2
      M=$(since "$l0" "^OBJECTIVE status marker .*'Objective' " | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4- |
          awk -v x="$px" -v z="$pz" '!s[$0]++ { printf "%d %s\n", sqrt(($1-x)^2 + ($3-z)^2), $0 }' | sort -n | head -1)
      [ -n "$M" ] || { sbr "no archer marker (round $k): the stage over?"; break; }
      set -- $M; d=$1; ax=$2; ay=$3; az=$4; sbr "round $k: the nearest archer marker $ax $ay $az, $d cm from the beast $px $pz"
      RID=0; { [ $(( (MVN + 1) % 2 )) = 0 ] || [ "${MV_RIDDEN_ONLY:-0}" = 1 ]; } && RID=1
      lo=1500; hi=3800; to=2800; [ $RID = 1 ] && { lo=0; hi=12000; to=7000; } # (rshot rides out and in itself)
      if [ "$d" -gt $hi ] || [ "$d" -lt $lo ]; then
        read gx gz < <(awk -v cx=$ax -v cz=$az -v x=$px -v z=$pz -v r=$to 'BEGIN { dx = x - cx; dz = z - cz; l = sqrt(dx*dx + dz*dz) + 1; printf "%.0f %.0f\n", cx + r * dx / l, cz + r * dz / l }')
        stamp "ridden to $((to / 100)) m of it ($gx $gz)"
        if [ "$d" -gt 6000 ] && [ "${RB:-0}" -lt 3 ]; then RB=$((${RB:-0} + 1)) # the bow out and away while it runs there
          ( steer_for "$gx" "$gz" 9 ) & SP=$!; sleep 2; stamp "runbow$RB: running ($(bspeed2) m/s)"; cycle "runbow$RB" $(( RB % 2 )); wait $SP; fi
        for i in $(seq 1 30); do w "ride steer $gx $gz" 0.2; read px pz ph < <(bpos)
          g=$(awk -v a=$gx -v b=$gz -v x=${px:-0} -v z=${pz:-0} 'BEGIN{print int(sqrt((a-x)^2+(b-z)^2))}')
          [ -n "$pz" ] && [ "$g" -lt 500 ] && break; seated || break; done
        [ "$d" -lt $lo ] && { w "ride steer $ax $az" 0.6; } # (ridden away from it: turned back to face it)
        stopbeast; continue; fi
      w "bow target $ax $ay $az" 0.3; MVN=$((MVN + 1)); AIMAT="$ax $ay $az"
      if [ $((MVN % 2)) = 0 ] || [ "${MV_RIDDEN_ONLY:-0}" = 1 ]; then rshot "archer$MVN-ridden" "$ax" "$ay" "$az"; else stopbeast; sshot "archer$MVN"; fi
      w "ride steer off" 0.3; w "bow target off" 0.2; AIMAT=""; sleep 1
    done
    hd 0 -10; [ "${SB_RIDE_ON:-0}" = 1 ] || { sbr "== the archers' stage done: the ride ends here"; T0=$((SECONDS - 100000)); }; }
  sed -E -e 's/^bowtry\(\) \{/bowtry_m13b() {/' -e 's/can you shoot from one\|Tut_CaragorRangedAttacks\|Care to double down/take .em up high|Tut_CaragorRangedAttacks/' \
      "$HERE/nurn41/m13b.sh" > "$R/m13b-mountview42.sh"
  grep -c 'take .em up high' "$R/m13b-mountview42.sh" | sed 's/^/m13b copy: the stage words /'
  BOW0=0 AID_AFTER=${AID_AFTER:-0} . "$R/m13b-mountview42.sh"   # (the beast's health aid from the first seat: m13b's own)
fi
hd 0 0
sbr "== mountview42 done: $(grep -cE 'SHOT .*: [0-9]* game hit' "$SBF") shots, $(grep -cE 'SHOT .*: [1-9][0-9]* game hit' "$SBF") with a game hit"
