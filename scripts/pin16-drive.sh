#!/bin/bash
# pin16-drive.sh - build 16: the pin of build 15b's session (233334 marks 2/3: Talion's root held at x/z 33761.8 -27236.1
# on the meadow bank's narrow ledge, every walk and pull undone between ticks). PLAN (default probe):
#   probe  on the meadow (a free walk with `climb writes 2`: the normal writers), then `practice go` onto the ledge at
#          the user's spot (PIN_AT, default 33761.8 2575 -27236.1) and the user's hang (33686 2575 -27297): `climb
#          state`, the probes, the stick in four directions (the second one under `climb writes 2`), the root after each.
#   unpin  (PLAN=unpin, build 16's SOMVR_CLIMB_UNPIN) the contacts on the meadow, at the user's spot with the word
#          `climb unpin off` (the pin), a climb's side pulls there off then on, the stick on, walking on, the meadow
#          bank's edge (31587 2252 -24919: the game's own edge keeper must stay).
#   pull   (PLAN=pull, build 16's SOMVR_CLIMB_PULLOVER) at the user's hang, the lip15b spot, the user's corner and the
#          pillars: face the nearest wall, the right test hand on it 45..115 cm over the head until a hold arms the
#          pullover; then the ring (a dump), the cancel (the hand opens), armed again, the pull (35 cm down) = the
#          blink onto the ring, a dump there, walking on.
# Attaches to a running boot with PIN_RUN=RUNDIR (else waits for one of this worktree's). Stops at once (quit) when the
# user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${PIN_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
AT=${PIN_AT:-33761.8 2575 -27236.1}
AT2=${PIN_AT2:-33686.1 2575 -27297.2}
PLAN=${PLAN:-probe}
T0=$(date +%s)
R=${PIN_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
[ -n "$PIN_RUN" ] || sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
lastloco() { grep -E '^LOCO t=.*last 1 s' "$L" | tail -${1:-2} | sed -E 's/.*t=([0-9.]+) .*asked ([0-9.]+) cm, the body ([0-9.]+) cm.*between ticks ([0-9.]+ cm [0-9.]+ deg).*/t=\1 asked \2 body \3 between \4/'; }
root() { w "climb state" 0.6; grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+ yaw [0-9.-]+'; }
go() { w "practice go $1" 1; waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-200; sleep 3; }
walk4() { local k=0 d
  for d in "0 1" "1 0" "0 -1" "-1 0"; do
    k=$((k + 1))
    [ "$k" = 2 ] && w "climb writes 2" 0.3
    echo "$(date +%T) before stick $d: $(root)"
    w "stick $d 0 1.5" 2.6
    echo "$(date +%T) after stick $d: $(root)"; lastloco 2
  done
  waitfor '^CLIMB writes t=.* end:' 6; grep -E '^CLIMB writes' "$L" | tail -8 | cut -c1-260; }
# the controls card (shown at the arrival) away: the right grip (the layer's test button), else A
card() { grep -q 'card: put away' "$R/xr-layer.log" 2>/dev/null && return 0
  x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
  grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }; }
contacts() { w "climb contacts" 1; grep -E '^CLIMB contacts' "$L" | tail -${1:-6} | cut -c1-330; }
unpins() { grep -E '^UNPIN t=' "$L" | tail -${1:-4} | cut -c1-330; }
probe() { local n0 i; n0=$(cnt '^CLIMB probe surfaces'); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(cnt '^CLIMB probe surfaces')" -gt "$n0" ] && break; sleep 0.25; done; }
near() { grep "^CLIMB probe t=.*at head $1 cm" "$L" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
fl() { grep -E '^CLIMB t=.* (end:|hang:|resume:|grab [LR]|start:)' "$L" | tail -${1:-4} | cut -c1-260; }
secs() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -${1:-2} | cut -c1-250; }
grabwall() { local h=$1 xs n0 i t; [ "$h" = L ] && xs=-25 || xs=25
  for t in "30 30" "50 30" "20 25" "60 40" "40 20"; do
    w "hands squeeze $h 0" 0.3; w "hands test $h $xs ${t% *} ${t#* } 0 0 0" 0.5
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { fl 1; return 0; }; sleep 0.25; done
  done; return 1; }
d() { x "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
  echo "$1 $f" | tee -a "$R/pin16-steps.txt"; }
pos() { grep -E '^CLIMB t=.*(pullover|grab R:)' "$L" | tail -${1:-2} | cut -c1-330; }
# armed: the ring (a dump), the cancel (the hand opens), armed again, the pull (35 cm down in 1.2 s) = the blink, walking
armed() { local up=$1 z=$2 n0
  pos 1; d "ring-$3"; m "p-ring-$3"
  n0=$(cnt 'pullover cancelled'); w "hands squeeze R 0" 1.2
  [ "$(cnt 'pullover cancelled')" -gt "$n0" ] && echo "$(date +%T) CANCEL ok" || echo "$(date +%T) no cancel line"
  pos 2; fl 2
  n0=$(cnt 'pullover armed'); w "hands squeeze R 1" 1.0
  [ "$(cnt 'pullover armed')" -gt "$n0" ] && echo "$(date +%T) ARMED again" || echo "$(date +%T) not armed again"
  d "ring2-$3"
  n0=$(cnt 'pullover: the pull done')
  w "hands swing R 15 $((up - 35)) $z 0 0 0 1200" 1.6
  if waitfor 'pullover: the pull done' 3; then pos 1; else echo "$(date +%T) NO PULL DONE"; secs 2; fi
  waitfor '^PRACTICE t=.*(arrived|did not arrive)' 10; grep -E '^PRACTICE t=' "$L" | tail -3 | cut -c1-200; sleep 2.5
  echo "$(date +%T) landed: $(root)"; d "landed-$3"; m "p-landed-$3"
  w "hands squeeze R 0" 0.5; w "hands test off" 0.5
  w "stick 0 1 0 1.5" 2.2; echo "$(date +%T) walked: $(root)"; lastloco 1; }
pulltry() { local t up z n0 n1
  probe; NB=$(near +0); [ -z "$NB" ] && NB=$(near -50); echo "nearest wall: ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -1 | cut -c1-300
  for t in "60 30" "80 30" "100 30" "115 35" "70 45" "90 40" "45 30"; do
    up=${t% *}; z=${t#* }
    n0=$(cnt 'pullover armed'); n1=$(cnt 'pullover: a hold on a top')
    w "hands squeeze R 0" 0.4; w "hands test R 15 $up $z 0 0 0" 0.5; w "hands squeeze R 1" 1.0
    if [ "$(cnt 'pullover armed')" -gt "$n0" ]; then echo "$(date +%T) ARMED at up $up ahead $z"; armed "$up" "$z" "$1"; return 0; fi
    [ "$(cnt 'pullover: a hold on a top')" -gt "$n1" ] && pos 1
    fl 1
  done
  w "hands squeeze R 0" 0.5; climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }
  return 1; }
climbing() { grep -E '^CLIMB t=.* (start|end):' "$L" | tail -1 | grep -q ' start:'; }
pullplan() {
  x "btn R squeeze 0"; w "hands test off"; w "climb pullover on"; w "climb unpin on"
  for spot in "$AT2" "33660 2575 -27345" "$AT" "33350 2000 -27900" "33500 2560 -28000"; do
    echo "== the pullover at $spot"
    go "$spot"; echo "$(date +%T) at: $(root)"
    if pulltry "$(echo $spot | cut -d' ' -f1 | cut -c1-5)"; then echo "PULLOVER DONE at $spot"; fi
    w "hands squeeze R 0" 0.3; w "hands test off" 0.3
  done
  grep -cE '^CLIMB t=.*pullover armed' "$L"; grep -cE '^CLIMB t=.*pullover: the pull done' "$L"; }
unpinplan() {
  x "btn R squeeze 0"; w "hands test off"
  echo "== the meadow (free): the contacts standing"
  contacts 4
  echo "== the user's spot: $AT, unpin off"
  w "climb unpin off"
  go "$AT"
  contacts 8
  echo "$(date +%T) before: $(root)"
  w "stick 0 1 0 2" 1; contacts 8; sleep 1.5
  echo "$(date +%T) after stick 0 1 (unpin off): $(root)"; lastloco 2
  echo "== the climb's pull sideways while pinned (unpin off)"
  probe; NB=$(near +0); echo "nearest wall: ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"
  if grabwall R; then
    for k in 1 2; do w "hands swing R -25 30 30 0 0 0 1200" 1.8; secs 1; done
    echo "$(date +%T) after the side pulls (unpin off): $(root)"
    w "climb unpin on" 0.5
    for k in 1 2; do w "hands swing R 25 30 30 0 0 0 1200" 1.8; w "hands swing R -25 30 30 0 0 0 1200" 1.8; secs 1; done
    echo "$(date +%T) after the side pulls (unpin on): $(root)"; unpins 4
    w "hands squeeze R 0" 1.5; fl 2
  else echo "no grab"; w "climb unpin on" 0.5; fi
  echo "== the user's spot again, unpin on: the stick"
  go "$AT"
  echo "$(date +%T) before: $(root)"
  d "unpin-corner"
  for sd in "0 1" "1 0" "0 -1" "-1 0"; do w "stick $sd 0 1.5" 2.4; echo "$(date +%T) after stick $sd: $(root)"; lastloco 1
    d "unpin-after-${sd// /_}"; done
  unpins 6; contacts 4
  m u01-after-unpin
  echo "== walking on: 3 s ahead, 3 s back"
  w "stick 0 1 0 3" 3.5; echo "$(date +%T) $(root)"; lastloco 2
  w "stick 0 -1 0 3" 3.5; echo "$(date +%T) $(root)"; lastloco 2
  echo "== the meadow's bank edge and a wall: no push through (unpin on)"
  go "31587 2252 -24919"
  w "stick 0 1 0 3" 3.5; echo "$(date +%T) at the bank's edge: $(root)"; lastloco 2; unpins 2
  grep -cE '^UNPIN t=.*pinned' "$L"; }
# stick (build 16's SOMVR_CLIMB_HANG_STICK_CM): at the user's hang spot a hand on the slab, pulled up as far as the
# game lets him, the hand opened slowly (the hang, the feet ~25 cm over the ledge), then the stick sideways
stickplan() { local n0
  echo "== the stick in a low hang at $AT2"
  x "btn R squeeze 0"; w "hands test off"; w "climb unpin on"
  go "$AT2"; echo "$(date +%T) at: $(root)"
  probe; NB=$(near +0); echo "nearest wall: ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"
  if grabwall R; then
    w "hands swing R 25 -5 30 0 0 0 1500" 2.2; secs 1
    n0=$(cnt '^CLIMB t=.* hang:'); w "hands squeeze R 0" 1.5
    [ "$(cnt '^CLIMB t=.* hang:')" -gt "$n0" ] && echo "$(date +%T) HANGING" || { echo "$(date +%T) no hang"; fl 3; }
    w "hands test off" 0.5
    n0=$(cnt 'hang: the stick'); w "stick 1 0 0 2" 2.5
    [ "$(cnt 'hang: the stick')" -gt "$n0" ] && echo "$(date +%T) STICK DROP" || echo "$(date +%T) no stick drop"
    grep -E '^CLIMB t=.*(hang: the stick|end:)' "$L" | tail -2 | cut -c1-260
    echo "$(date +%T) after the stick: $(root)"; lastloco 2
    w "stick 1 0 0 2" 2.5; echo "$(date +%T) walked on: $(root)"; lastloco 1
  else echo "no grab"; fi; }
# bank (build 16's SOMVR_CLIMB_PULLOVER on a real top): from the practice arrival (PIN_BANK_FROM) find the nearest wall
# (the meadow's stone bank, its top ~2149, build 9's first climb), walk to it, hand over hand up; after each grab the
# pullover armed? then the ring (a dump), the cancel (the hand opens), armed again, the pull = the blink, walking on
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$L" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
grabh() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  for k in 1 2 3 4; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && return 0; sleep 0.25; done
    w "hands squeeze $h 0" 0.5; z=$((z + 12)); w "hands test $h $xs $y $z 0 0 0" 0.5
  done; return 1; }
bankplan() { local k d d0 PREV= HOLD HX O OX n0 REACH=50
  echo "== the bank: a real top for the pullover"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  w "climb pullover on"; w "climb unpin on"
  go "${PIN_BANK_FROM:-32500 2100 -24800}"; echo "$(date +%T) at: $(root)"
  probe; NB=$(near -50); [ -n "$NB" ] || NB=$(near +0); echo "nearest: ${NB:-none}"
  [ -n "$NB" ] || return 1
  turnto "${NB% *}"; probe
  for k in $(seq 1 25); do
    d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
    [ -n "$d" ] || { NB=$(near -50); [ -n "$NB" ] && turnto "${NB% *}"; probe; continue; }
    echo "$(date +%T) ahead $d cm"; [ "$d" -le 62 ] && break
    if [ "$d" -gt 500 ]; then w "stick 0 1 0 2" 2.4; elif [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
    probe
    [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && break
    PREV=$d
  done
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -1 | cut -c1-300
  m b01-at-wall
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
  grabh L 0 $REACH || { echo "no grab L"; return 1; }
  HOLD=L
  for k in $(seq 1 10); do
    climbing || { echo "$(date +%T) not climbing"; fl 2; return 1; }
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    w "hands swing $HOLD $HX -45 $REACH 0 0 0 1000" 1.4
    w "hands test $O $OX 35 $REACH 0 0 0" 0.5
    n0=$(cnt 'pullover armed')
    grabh $O 35 $REACH || { echo "$(date +%T) no grab $O"; fl 3; secs 1; return 1; }
    pos 2
    if [ "$(cnt 'pullover armed')" -gt "$n0" ]; then
      echo "$(date +%T) ARMED by $O after $k pulls"
      w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX -40 10 0 0 0" 0.4
      d "ring-bank"; m b02-ring
      n0=$(cnt 'pullover cancelled'); w "hands squeeze $O 0" 1.2
      [ "$(cnt 'pullover cancelled')" -gt "$n0" ] && echo "$(date +%T) CANCEL ok" || echo "$(date +%T) no cancel line"
      fl 2; d "cancelled-bank"
      n0=$(cnt 'pullover armed'); w "hands squeeze $O 1" 1.0
      [ "$(cnt 'pullover armed')" -gt "$n0" ] && echo "$(date +%T) ARMED again" || { echo "$(date +%T) not armed again"; fl 2; }
      d "ring2-bank"
      w "hands swing $O $OX 0 $REACH 0 0 0 1000" 1.4
      if waitfor 'pullover: the pull done' 3; then pos 1; else echo "$(date +%T) NO PULL DONE"; secs 2; fi
      waitfor '^PRACTICE t=.*(arrived|did not arrive)' 10; grep -E '^PRACTICE t=' "$L" | tail -3 | cut -c1-200; sleep 2.5
      echo "$(date +%T) landed: $(root)"; d "landed-bank"; m b03-landed
      w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
      w "stick 0 1 0 1.5" 2.2; echo "$(date +%T) walked: $(root)"; lastloco 1
      return 0
    fi
    w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX 20 $REACH 0 0 0" 0.4
    HOLD=$O; secs 1
  done
  echo "$(date +%T) never armed"; return 1; }
# lip (build 16: SOMVR_CLIMB_OVERHANG=step with the unpin): lipledge15b-drive.sh's attempt at the user's lip (15b's
# mark 8; the narrow ledge, the slab ~2812 out over the face): a hand on the slab, pulled to the waist, the other on it,
# three pulls; the root's height after each (off, then step)
onslab() { local h=$1 xs n0 i t up z; [ "$h" = L ] && xs=-20 || xs=20
  for t in "75 35" "85 30" "65 40" "90 45" "70 25"; do
    up=${t% *}; z=${t#* }
    w "hands squeeze $h 0" 0.3; w "hands test $h $xs $up $z 0 0 0" 0.5
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { fl 1; return 0; }; sleep 0.25; done
  done; return 1; }
lipatt() { local OH=$1 H=R O HX k
  echo "== the lip, overhang $OH (unpin on)"
  w "hands squeeze L 0"; w "hands squeeze R 0"; w "climb overhang $OH" 0.5; w "climb unpin on" 0.3
  go "33660 2575 -27345"
  probe; NB=$(near +0); [ -n "$NB" ] || NB=$(near -50); echo "nearest: ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"
  onslab $H || { echo "$(date +%T) no hold on the slab"; return 1; }
  for k in 1 2 3; do
    [ "$H" = L ] && { O=R; HX=-20; } || { O=L; HX=20; }
    w "hands swing $H $HX -15 25 0 0 0 1500" 2.2
    echo "$(date +%T) pull $k ($H): $(grep -E '^CLIMB t=.*last 1 s' "$L" | tail -1 | grep -oE 'now [0-9.,-]+')"; secs 1
    grep -E '^(CLIMB t=.*(overhang|retop|pullover)|UNPIN)' "$L" | tail -3 | cut -c1-240
    climbing || break
    onslab $O || echo "$(date +%T) no second hold"
    w "hands squeeze $H 0" 0.5
    H=$O
  done
  m "l-$OH"
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 4; }; }
lipplan() { lipatt off; lipatt step; }
# hang (build 16's SOMVR_CLIMB_HANG_STICK_CM=60 at the bank's face): from the gravel one hold, pulled 40 cm, the hand
# opened (the hang, the feet ~40 cm up), the stick = released and walking; again two pulls up (the feet ~80 cm up):
# the stick does nothing, A drops
hangplan() { local n0 REACH=50 up
  echo "== the stick in a low hang at the bank's face"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  for up in 40 85; do
    go "${PIN_BANK_FROM:-32500 2100 -24800}"
    probe; NB=$(near -50); [ -n "$NB" ] || NB=$(near +0); [ -n "$NB" ] && turnto "${NB% *}"; probe
    for k in $(seq 1 25); do
      d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
      [ -n "$d" ] || { NB=$(near -50); [ -n "$NB" ] && turnto "${NB% *}"; probe; continue; }
      [ "$d" -le 62 ] && break
      if [ "$d" -gt 500 ]; then w "stick 0 1 0 2" 2.4; elif [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
      probe
    done
    w "hands test L -20 0 $REACH 0 0 0" 0.5
    grabh L 0 $REACH || { echo "no grab"; continue; }
    w "hands swing L -20 -$up $REACH 0 0 0 1500" 2.0; secs 1
    n0=$(cnt '^CLIMB t=.* hang:'); w "hands squeeze L 0" 1.5
    [ "$(cnt '^CLIMB t=.* hang:')" -gt "$n0" ] && echo "$(date +%T) HANGING ($up)" || { echo "$(date +%T) no hang ($up)"; fl 2; }
    w "hands test off" 0.5
    n0=$(cnt 'hang: the stick'); w "stick 0 -1 0 1.5" 2.0
    [ "$(cnt 'hang: the stick')" -gt "$n0" ] && echo "$(date +%T) STICK DROP ($up)" || echo "$(date +%T) no stick drop ($up)"
    grep -E '^CLIMB t=.*(hang: the stick|end:)' "$L" | tail -2 | cut -c1-260
    echo "$(date +%T) after the stick: $(root)"; lastloco 2
    climbing && { echo "$(date +%T) still on the wall: A"; x "btn R a 1"; x "btn R a 0"; sleep 3; fl 2; }
  done; }
# stock (the coordinator's 4: what the flat game does at the user's lip; boot with SOMVR_LOCO=0 SOMVR_GAMEMOVES=on
# SOMVR_CLIMB_LEDGES=game SOMVR_CLIMB_DESCENT=off SOMVR_CLIMB_UNPIN=off: the game's own moves): from the gravel under
# the corner, then on the ledge at the user's two spots - face the bank, the free-run latch (the left stick's click)
# with the stick at the wall for 8 s, then A (the stock jump) pressed while pushing; `climb watch` (the state tags), the
# root each second. Then the terrain: the top surface over a grid round the bank (`practice status ground`)
stocktry() { local k
  probe; NB=$(near +0); [ -n "$NB" ] || NB=$(near -50); echo "nearest: ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"
  w "climb watch" 0.3
  echo "$(date +%T) start: $(root)"
  w "stick 0 1 0 8" 0.3; x "btn L stick 1"; x "btn L stick 0"
  for k in 1 2 3 4 5 6; do sleep 0.6; echo "$(date +%T) +$k s: $(root)"; done
  w "stick 0 1 0 6" 0.3
  for k in 1 2 3 4; do x "btn R a 1"; x "btn R a 0"; echo "$(date +%T) A $k: $(root)"; done
  w "stick 0 0 0 0" 0.5; w "climb watch off" 0.3
  grep -E '^CLIMB state t=.*\(tags\)' "$L" | tail -12 | sed -E 's/.*fsm ([0-9]+).*root ([0-9.,-]+).*tags [0-9]+: ([^;]*);.*/fsm \1 root \2 tags \3/' | uniq | cut -c1-200
  grep -E '^LOCO t=.*last 1 s' "$L" | tail -3 | sed -E 's/.*ticks=([0-9]+) ours=([0-9]+) game=([0-9]+).*asked ([0-9.]+) cm, the body ([0-9.]+) cm.*/ticks \1 ours \2 game \3 asked \4 body \5/'
  grep -E '^INPUT|FreeRun' "$L" | grep -i 'freerun\|latch' | tail -3 | cut -c1-200; }
stockplan() { local x z
  echo "== the stock game at the user's lip (LOCO $(grep -o 'CONFIG loco=[a-z0-9]*' "$L" | head -1), the game's moves)"
  x "btn R squeeze 0"; w "hands test off"
  for spot in "33740 1990 -27560" "33660 2575 -27345" "33761.8 2575 -27236.1" "33686.1 2575 -27297.2"; do
    echo "== stock at $spot"; go "$spot"; stocktry; m "s-$(echo $spot | cut -c1-5)"
  done
  echo "== the terrain round the bank: the top surface (practice status ground)"
  for z in -27100 -27150 -27200 -27250 -27300 -27350 -27400 -27450 -27500; do
    for x in 33500 33550 33600 33650 33700 33750 33800 33850 33900 33950; do w "practice status ground $x 2600 $z" 0.15; done
  done
  sleep 1
  grep -E '^PRACTICE t=.* ground at ' "$L" | tail -90 | sed -E 's/.*ground at \(([0-9-]+) [0-9]+ ([0-9-]+)\): ([0-9]+) surface\(s\)[^:]*:? ?([^;]*).*/\1 \2 n\3 \4/' | cut -c1-120; }
# top (the follow-up's 1: the 1:1 top-out first): from PIN_BANK_FROM up the bank hand over hand until a hold on its top
# (the grab line's `edge 1` or a down ray), then the mantle as a player does it (climb-drive.sh plan t): the other hand
# down by the side, the top hand pushed down to 90 cm under the eyes, then 90 cm toward the body; with `climb ramp R`
# (1: the game's edge step on, as shipped; 0: off - a top the 1:1 push cannot get over). R=1: `end: over`, no ring;
# R=0: blocked = the ring, the pull = the quick blink
topatt() { local RP=$1 k d d0 PREV= HOLD HX O OX TOP= TX n0 REACH=50
  echo "== the top-out with climb ramp $RP"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  w "climb ramp $RP"
  go "${PIN_BANK_FROM:-31700 1830 -24825}"
  probe; NB=$(near -50); [ -n "$NB" ] || NB=$(near +0); [ -n "$NB" ] && turnto "${NB% *}"; probe
  for k in $(seq 1 20); do
    d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
    [ -n "$d" ] || break; [ "$d" -le 62 ] && break
    if [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
    probe; [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && break; PREV=$d
  done
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
  grabh L 0 $REACH || { echo "no grab L"; return 1; }
  HOLD=L
  for k in $(seq 1 8); do
    climbing || { echo "$(date +%T) not climbing"; fl 2; return 1; }
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    w "hands swing $HOLD $HX -45 $REACH 0 0 0 1000" 1.4
    w "hands test $O $OX 35 $REACH 0 0 0" 0.5
    grabh $O 35 $REACH || { echo "$(date +%T) no grab $O"; return 1; }
    w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX -40 10 0 0 0" 0.4
    if grep -E "^CLIMB t=.* grab $O:" "$L" | tail -1 | grep -qE 'edge 1|down ray'; then TOP=$O; break; fi
    HOLD=$O
  done
  [ -n "$TOP" ] || { echo "$(date +%T) no top reached"; return 1; }
  [ "$TOP" = L ] && { TX=-20; OT=R; OX=20; } || { TX=20; OT=L; OX=-20; }
  echo "$(date +%T) the top held by $TOP: $(grep -E "^CLIMB t=.* grab $TOP:" "$L" | tail -1 | grep -oE 'edge [01][^;]*')"
  n0=$(cnt 'pullover armed'); n1=$(cnt 'pullover: the pull done')
  w "hands swing $TOP $TX -90 40 0 0 0 1800" 2.4; secs 1
  # forward hand over hand as build 9's climb-drive.sh: the top hand 80 cm toward the body, the other onto the top
  # again (palm down a little over the top's height), the first lets go, forward again (3 stages at most)
  TYH=-90
  for st in 1 2 3; do
    climbing || break
    w "hands swing $TOP $TX $TYH -40 0 0 0 2000" 2.6; secs 1
    climbing && w "hands swing $TOP $TX $TYH -90 0 0 0 1200" 1.6
    [ "$(cnt 'pullover armed')" -gt "$n0" ] && { echo "$(date +%T) RING (armed) at stage $st"; d "ring-top-$RP"; }
    climbing || break
    [ $st = 3 ] && break
    TY=$(grep -E '^CLIMB t=.* grab [LR]:.*top ' "$L" | tail -1 | grep -oE 'top [0-9.]+' | cut -d' ' -f2)
    HY=$(grep -E '^CLIMB t=.*(last 1 s|top:)' "$L" | tail -1 | grep -oE 'head [0-9.]+' | tail -1 | cut -d' ' -f2)
    [ -n "$TY" ] && [ -n "$HY" ] || break
    GOT=""; [ "$OT" = L ] && OR=-90 || OR=90
    for dy in 10 -5 -20; do
      REL=$(awk "BEGIN{printf \"%d\", $TY - $HY + $dy}")
      w "hands squeeze $OT 0" 0.3; w "hands test $OT $OX $REL 40 0 0 $OR" 0.4
      k=$(cnt "^CLIMB t=.* grab $OT:"); w "hands squeeze $OT 1" 1.0
      [ "$(cnt "^CLIMB t=.* grab $OT:")" -gt "$k" ] && { GOT=$REL; break; }
    done
    echo "$(date +%T) stage $st: top $TY head $HY: the $OT hand ${GOT:+held at $GOT}${GOT:-found nothing}"
    [ -n "$GOT" ] || break
    w "hands squeeze $TOP 0" 0.5; w "hands test $TOP $TX -40 10 0 0 0" 0.4
    X=$TOP; TOP=$OT; OT=$X; X=$TX; TX=$OX; OX=$X; TYH=$((GOT - 10))
  done
  [ "$(cnt 'pullover armed')" -gt "$n0" ] && echo "$(date +%T) RING shown ($RP)" || echo "$(date +%T) no ring ($RP)"
  [ "$(cnt 'pullover: the pull done')" -gt "$n1" ] && echo "$(date +%T) PULLOVER blink ($RP)"
  sleep 1.5; grep -E '^CLIMB t=.*(end:|top:|pullover)' "$L" | tail -4 | cut -c1-260; echo "$(date +%T) result ($PIN_BANK_FROM): $(grep -E '^CLIMB t=.*end:' "$L" | tail -1 | grep -oE 'end: [a-z]+')"
  grep -E '^PRACTICE t=' "$L" | tail -3 | cut -c1-200
  echo "$(date +%T) after: $(root)"; d "after-top-$RP"; m "t-after-$RP"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 2; }
  w "stick 0 1 0 1.5" 2.2; echo "$(date +%T) walked: $(root)"; lastloco 1; }
topplan() { topatt 1; topatt 0; w "climb ramp 1"; }
# tops: the top-out as shipped (climb ramp 1) from several places along the arrival's bank (PIN_TOPS)
topsplan() { local sp
  for sp in "31700 1830 -24760" "31700 1830 -24880" "31700 1830 -24960" "31720 1830 -25050"; do
    PIN_BANK_FROM="$sp" topatt 1; done; }
# noway (the follow-up's 4): the user's lip without the overhang step: the pulls on the slab blocked = a soft double buzz
noway() { lipatt off; grep -E '^CLIMB t=.*no way up here' "$L" | tail -2 | cut -c1-280; }
[ -n "${PIN_LIB:-}" ] && return 0 # sourced by climb2-drive.sh for these functions
case " $PLAN " in *" unpin "*|*" pull "*|*" stick "*|*" bank "*|*" lip "*|*" hang "*|*" stock "*|*" top "*|*" noway "*|*" tops "*)
  card
  for p in $PLAN; do [ "$p" = unpin ] && unpinplan; [ "$p" = pull ] && pullplan; [ "$p" = stick ] && stickplan
    [ "$p" = bank ] && bankplan; [ "$p" = lip ] && lipplan; [ "$p" = hang ] && hangplan; [ "$p" = stock ] && stockplan
    [ "$p" = top ] && topplan; [ "$p" = noway ] && noway; [ "$p" = tops ] && topsplan; done
  echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"; exit 0 ;;
esac
x "btn R squeeze 0"
w "hands test off"
echo "== the meadow (free)"
w "climb state" 1; grep '^CLIMB state' "$L" | tail -1 | cut -c1-400
walk4
m p01-meadow
echo "== the user's spot: $AT"
go "$AT"
w "climb state" 1; grep '^CLIMB state' "$L" | tail -1 | cut -c1-500
w "climb probe" 1.5; grep '^CLIMB probe' "$L" | tail -5 | cut -c1-300
w "climb probe edge" 1.5; grep '^CLIMB probe' "$L" | tail -4 | cut -c1-400
m p02-spot
walk4
w "climb state" 1; grep '^CLIMB state' "$L" | tail -1 | cut -c1-500
m p03-spot-after
echo "== the user's hang: $AT2"
go "$AT2"
w "climb state" 1; grep '^CLIMB state' "$L" | tail -1 | cut -c1-500
w "climb probe edge" 1.5; grep '^CLIMB probe' "$L" | tail -4 | cut -c1-400
walk4
m p04-hang-spot
echo "steps done $(date +%T)"
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
