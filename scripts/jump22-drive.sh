#!/bin/bash
# jump22-drive.sh - build 22 (the som-b22-move helper, docs/re-xr-pipeline.md 91): the jump and the vault, headless.
# climb2-drive.sh's and pin16-drive.sh's functions (sourced). Plans (PLAN="stand run ...", in order):
#   stand   on the meadow (the arrival): `vault scan`, a standing jump (A tapped: the climb's edge look first, then the
#           jump), with `jump trace` (the root and the FSM per tick); both eyes before, at the apex, after
#   run     the stick forward along a free heading, A after 1 s: the running jump (the stick's 230 cm/s carried)
#   sprint  the same with the left stick's click (the sprint latch, 640 cm/s)
#   bank    onto the meadow bank's top (PIN_BANK_TOP), back from its edge, the sprint and our jump off it (the gap our jump
#           clears: the horizontal before he drops under the top's height); then `jump stock on` and the game's own A
#           (Jump, FreeRun) at the same edge for the comparison (the root per tick), `jump stock off`
#   vault   `vault scan` at VAULT_SPOTS until a vault; face it, walk at it until the ring shows (both eyes: the ring),
#           A = over it (both eyes during and after), walk on
#   wall    at the bank's face (PIN_BANK_FROM): the right hand up the face, A = the jump, the grip at the apex = the climb
#           catches the hold (the jump's end "caught by the climb"), then A = the drop
# Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
PIN_BANK_TOP=${PIN_BANK_TOP:-31420 2150 -24840}
VAULT_SPOTS=${VAULT_SPOTS:-"32500 1878 -24800;30749 1754 -27088;33300 2030 -27700;32900 1900 -25600;31900 1830 -24300"}
# A pressed until the layer has run it (+0.1 s: a tap), then $1 dumps 0.2 s apart
tapA() { local k n0
  userin && bail "the user is in VR"
  n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")
  printf 'btn R a 1\n' > "$R/xrtest.txt"; echo "$(date +%T) xrtest: A tap"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  sleep 0.1
  if [ "${1:-0}" -gt 0 ]; then printf 'btn R a 0\ndump\n' > "$R/xrtest.txt"
    for k in $(seq 2 "$1"); do sleep 0.2; printf 'dump\n' > "$R/xrtest.txt"; done
  else printf 'btn R a 0\n' > "$R/xrtest.txt"; fi
  sleep 1.0; }
# A pressed now and held until the layer has run it (a raw write of 1 then 0 can be overwritten before it is read)
tapnow() { local k n0; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.02; done
  sleep 0.08; printf 'btn R a 0\n' > "$R/xrtest.txt"; }
jl() { grep -E '^(JUMP t=|VAULT t=)' "$L" | tail -${1:-4} | cut -c1-400; }
jend() { grep -E '^JUMP t=.* end:' "$L" | tail -1 | cut -c1-420; }
hv() { w "jump state" 0.6 >/dev/null; grep -E '^JUMP state' "$L" | tail -1 | grep -oE "heading -?[0-9.]+" | grep -oE -- '-?[0-9.]+$'; }
# the view's path of the last jump: the frames, the largest eye step a frame, any y step against the next
viewsum() { awk '/^JUMP view/ {n++; for (i=1;i<=NF;i++) if ($i=="step") { s=$(i+1); if (s>m) m=s } } END { printf "view frames %d, the largest eye step a frame %.2f cm\n", n, m }' "$L"; }
# turn (snap turns) until the head's heading is within 16 deg of $1 (deg, 0 = +z, 90 = +x)
SIGN=1
faceh() { local want=$1 h d k
  for k in $(seq 1 14); do
    h=$(hv); [ -n "$h" ] || return 1
    d=$(awk -v a="$want" -v b="$h" 'BEGIN { d = a - b; while (d > 180) d -= 360; while (d < -180) d += 360; printf "%d", d }')
    [ "${d#-}" -le 16 ] && return 0
    if [ $(( d * SIGN )) -gt 0 ]; then w "turn+" 0.7; else w "turn-" 0.7; fi
    local h2; h2=$(hv)
    local d2; d2=$(awk -v a="$want" -v b="$h2" 'BEGIN { d = a - b; while (d > 180) d -= 360; while (d < -180) d += 360; printf "%d", d }')
    [ "${d2#-}" -gt "${d#-}" ] && SIGN=$(( -SIGN )) # the turn went the other way: flip
  done; return 1; }
scan() { w "vault scan ${1:-800}" 1.2; grep -E '^VAULT scan' "$L" | tail -1 | cut -c1-900; }
freeh() { grep -E '^VAULT scan' "$L" | tail -1 | sed -E 's/.*free:([0-9 ]*).*/\1/' | tr ' ' '\n' | grep -E '^[0-9]+$' | head -${1:-1} | tail -1; }
trace_fsm() { grep -E '^JUMP trace' "$L" | tail -${1:-200} | grep -oE 'fsm [0-9]+ mode [0-9]+' | sort | uniq -c | tr '\n' ';'; echo; }
jstand() {
  echo "== stand: a standing jump on the meadow"
  w "hands test off"; scan 800
  local h; h=$(freeh 1); [ -n "$h" ] && faceh "$h"
  d "stand-before"
  w "jump trace 3" 0.2; tapA 3; sleep 1.5
  jl 4; jend; echo "fsm/mode while traced: $(trace_fsm)"; viewsum
  d "stand-after"; }
# the same standing jump through each way the rise reaches the body (`jump via`): the highest rise of each
jvias() { local v
  echo "== vias: the standing jump's rise through LOCO's delta, the extra move, the proxy set up"
  for v in delta extra setpos; do
    w "jump via $v" 0.4; w "jump trace 2" 0.1; tapA 0; sleep 1.0
    echo "via $v: $(jend | grep -oE 'the highest [+-][0-9.]+ cm|the landing [a-z]+ \(from [0-9]+ cm/s\)|after [0-9.]+ s in the air' | tr '\n' ' ')"
  done
  w "jump via ${VIA:-setpos}" 0.4; }
jrun() { local h
  echo "== run: the running jump along a free heading"
  scan 1000; h=$(freeh 2); [ -n "$h" ] && faceh "$h"
  w "stick 0 1 0 3.0" 1.2; w "jump trace 2.5" 0.1; tapA 2; sleep 2.2
  jl 3; jend; echo "fsm/mode: $(trace_fsm)"; lastloco 2; }
jsprint() { local h
  echo "== sprint: the sprint latch, then the jump"
  scan 1200; h=$(freeh 3); [ -n "$h" ] && faceh "$h"
  w "stick 0 1 0 4.0" 0.4; x "btn L stick 1"; x "btn L stick 0"; sleep 0.8
  w "jump trace 2.5" 0.1; tapA 2; sleep 2.2
  jl 3; jend; echo "fsm/mode: $(trace_fsm)"; lastloco 2; }
jbank() { local y
  echo "== bank: off the meadow bank's top: ours, then the game's own A"
  go "$PIN_BANK_TOP"
  y=$(grep -E '^PRACTICE t=.*arrived at' "$L" | tail -1 | grep -oE 'at \([0-9.-]+ [0-9.-]+' | awk '{print $3}' | cut -d. -f1)
  echo "$(date +%T) at the top? root y $y" 
  [ -n "$y" ] && [ "$y" -gt 2200 ] || { echo "$(date +%T) not on the top ($y): skipped"; return 1; }
  faceh 90; scan 600
  d "bank-top"
  w "jump trace 5" 0.1; w "stick 0 1 0 3.0" 0.3; printf 'btn L stick 1\n' > "$R/xrtest.txt"; sleep 0.3
  printf 'btn L stick 0\n' > "$R/xrtest.txt"
  # A while still running, ~170 cm before the edge the game's edge keeper holds him at (x ~31640, run 003247)
  local k rx
  for k in $(seq 1 60); do rx=$(grep -E '^JUMP trace' "$L" | tail -1 | grep -oE 'root [0-9.]+' | grep -oE '[0-9]+' | head -1)
    [ -n "$rx" ] && [ "$rx" -ge 31470 ] && break; sleep 0.03; done
  tapnow; echo "$(date +%T) A at x $rx"
  sleep 2.5; jl 4; jend
  grep -E '^JUMP trace' "$L" | tail -60 | awk 'NR%4==1' | cut -c1-160
  d "bank-landed"
  echo "-- the game's own A at the same edge"
  go "$PIN_BANK_TOP"; faceh 90
  w "jump stock on" 0.5
  w "jump trace 6" 0.1; w "stick 0 1 0 4.0" 0.3; printf 'btn L stick 1\n' > "$R/xrtest.txt"; sleep 0.3
  printf 'btn L stick 0\n' > "$R/xrtest.txt"; sleep 0.3
  printf 'btn R a 1\n' > "$R/xrtest.txt"; sleep 2.0; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 2.5
  grep -E '^JUMP trace' "$L" | tail -100 | awk 'NR%3==1' | cut -c1-160
  w "jump stock off" 0.5; }
# the vault on real ground: `vault scan 400` on a grid of spots (GRID), the highest top found (OVER first), then from
# 130 cm before its face facing it: the ring (both eyes), A = over it (both eyes during, after), walked on
GRID=${GRID:-"30800 1800 -24400;31600 1800 -24400;32400 1850 -24400;33200 1900 -24400;30800 1800 -25400;31600 1800 -25400;32400 1850 -25400;33200 1900 -25400;30800 1750 -26400;31600 1800 -26400;32400 1850 -26400;33200 1950 -26400;30800 1750 -27400;31600 1800 -27400;32400 1900 -27400;33200 2000 -27400"}
jvgrid() { local spot best= bt=0 bk= line ent h d top kind sx sy sz n0 k IFS_=$IFS
  echo "== vgrid: the vault's scans on a grid"
  IFS=';'; for spot in $GRID; do IFS=$IFS_
    go "$spot" >/dev/null
    grep -E '^PRACTICE t=.*arrived at' "$L" | tail -1 | grep -q 'arrived' || { IFS=';'; continue; }
    w "vault scan 400" 1.0 >/dev/null; line=$(grep -E '^VAULT scan' "$L" | tail -1)
    for ent in $(echo "$line" | grep -oE '[0-9]+ deg: (OVER|ONTO) [0-9]+ cm at [0-9]+' | tr ' ' '_'); do
      h=$(echo "$ent" | cut -d_ -f1); kind=$(echo "$ent" | cut -d_ -f3); top=$(echo "$ent" | cut -d_ -f4); d=$(echo "$ent" | cut -d_ -f7)
      local sc=$top; [ "$kind" = OVER ] && sc=$((top + 100))
      if [ "$sc" -gt "$bt" ]; then bt=$sc; best="$(echo "$line" | grep -oE 'from [0-9.-]+,[0-9.-]+,[0-9.-]+' | cut -d' ' -f2) $h $d $kind $top"; fi
    done
    echo "$(date +%T) $spot: $(echo "$line" | grep -oE 'ok [0-9]+' ) $(echo "$line" | grep -oE '\| [0-9]+ deg: (OVER|ONTO) [0-9]+ cm at [0-9]+' | tr '\n' ' ')"
    IFS=';'
  done; IFS=$IFS_
  [ -n "$best" ] || { echo "$(date +%T) no vault on the grid"; return 1; }
  echo "$(date +%T) the best: $best"
  set -- $best; sx=$(echo "$1" | cut -d, -f1); sy=$(echo "$1" | cut -d, -f2); sz=$(echo "$1" | cut -d, -f3)
  local ax az; ax=$(awk -v x="$sx" -v a="$2" -v d="$3" 'BEGIN { printf "%.0f", x + sin(a * 3.14159265 / 180) * (d - 130) }')
  az=$(awk -v z="$sz" -v a="$2" -v d="$3" 'BEGIN { printf "%.0f", z + cos(a * 3.14159265 / 180) * (d - 130) }')
  go "$ax $sy $az"; faceh "$2"; sleep 0.6
  n0=$(cnt '^VAULT t=.* ring:')
  for k in $(seq 1 8); do [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ] && break; w "stick 0 0.4 0 0.3" 0.6; done
  grep -E '^VAULT t=' "$L" | tail -3 | cut -c1-330
  [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ] || { echo "$(date +%T) no ring"; return 1; }
  d "vgrid-ring"; m "vgrid-ring"
  w "jump trace 3" 0.1; tapA 4; sleep 1.5; jl 5; jend; viewsum
  d "vgrid-after"; m "vgrid-after"
  w "stick 0 1 0 1.5" 2.0; echo "$(date +%T) walked on: $(root)"; }
# the vault at known spots (VSPOTS "x y z heading;..." from run 025133's grid): there, facing it, small steps until the
# ring shows (both eyes), A = the vault (both eyes during and after), the landing against the ring, walked on
VSPOTS=${VSPOTS:-"32400 1850 -25400 105;31600 1800 -26400 90;30800 1750 -27400 150"}
jvspot() { local spot n0 k got= IFS_=$IFS
  echo "== vspot: the vault at the grid's spots"
  IFS=';'; for spot in $VSPOTS; do IFS=$IFS_
    set -- $spot; go "$1 $2 $3" >/dev/null; sleep 0.5
    # walk toward the heading $4 with the stick turned to it (no snap turns: the ring follows the way he moves)
    local hh sx sy; hh=$(hv)
    sx=$(awk -v a="$4" -v b="$hh" 'BEGIN { printf "%.2f", 0.4 * sin((a - b) * 3.14159265 / 180) }')
    sy=$(awk -v a="$4" -v b="$hh" 'BEGIN { printf "%.2f", 0.4 * cos((a - b) * 3.14159265 / 180) }')
    echo "$(date +%T) the head at $hh: the stick $sx $sy toward $4"
    n0=$(cnt '^VAULT t=.* ring:')
    for k in $(seq 1 12); do [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ] && break; w "stick $sx $sy 0 0.3" 0.6 >/dev/null; done
    grep -E '^VAULT t=' "$L" | tail -2 | cut -c1-330
    if [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ]; then
      d "vspot-ring"; m "vspot-ring"
      w "jump trace 3" 0.1; tapA 4; sleep 1.5; jl 4; jend; viewsum
      d "vspot-after"; m "vspot-after"; got=1; break
    fi
    echo "$(date +%T) no ring at $spot"; IFS=';'
  done; IFS=$IFS_
  [ -n "$got" ] && { w "stick 0 1 0 1.5" 2.0; echo "$(date +%T) walked on: $(root)"; }; }
jvault() { local spot h n0 k ok=
  echo "== vault: a low obstacle from the scans"
  local IFS_=$IFS; IFS=';'
  for spot in $VAULT_SPOTS; do IFS=$IFS_
    [ "$spot" = here ] || go "$spot"
    scan 500
    h=$(grep -E '^VAULT scan' "$L" | tail -1 | grep -oE '\| [0-9]+ deg: (OVER|ONTO)' | head -1 | grep -oE '[0-9]+')
    [ -n "$h" ] && { ok=1; break; }
    IFS=';'
  done; IFS=$IFS_
  [ -n "$ok" ] || { echo "$(date +%T) no vault in the scans"; return 1; }
  faceh "$h"; sleep 0.5
  n0=$(cnt '^VAULT t=.* ring:')
  for k in $(seq 1 12); do [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ] && break; w "stick 0 0.5 0 0.4" 0.7; done
  jl 2
  [ "$(cnt '^VAULT t=.* ring:')" -gt "$n0" ] || { echo "$(date +%T) no ring"; return 1; }
  d "vault-ring"; m "vault-ring"
  tapA 4; sleep 1.5; jl 4; jend; viewsum
  d "vault-after"; m "vault-after"
  w "stick 0 1 0 1.5" 2.0; echo "$(date +%T) walked on: $(root)"; }
jwall() { local k d0 d PREV= n0
  echo "== wall: the jump at the bank's face, the grip at the apex"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "${PIN_BANK_FROM:-31700 1830 -24825}"
  probe; NB=$(near -50); [ -n "$NB" ] || NB=$(near +0); [ -n "$NB" ] && turnto "${NB% *}"; probe
  for k in $(seq 1 20); do
    d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
    [ -n "$d" ] || break; [ "$d" -le 62 ] && break
    if [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
    probe; [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && break; PREV=$d
  done
  echo "$(date +%T) at the face: $(root)"
  w "hands test R 20 75 50 0 0 0" 0.6
  n0=$(cnt '^CLIMB t=.* grab R:')
  tapnow; sleep 0.12
  printf 'hands squeeze R 1' > "$R/somvr.trigger"; sleep 0.1 # (polled every 250 ms: it lands while he is in the air)
  for k in $(seq 1 12); do [ "$(cnt '^CLIMB t=.* grab R:')" -gt "$n0" ] && break; sleep 0.1; done
  sleep 0.6; jl 3; jend; fl 3; d "wall-held"; m "wall-held"
  climbing && echo "$(date +%T) climbing: the catch" || echo "$(date +%T) NOT climbing"
  w "hands squeeze R 0" 0.6; climbing && { tapA 0; sleep 2; }
  w "hands test off" 0.4; echo "$(date +%T) after: $(root)"; }
card
for p in $PLAN; do [ "$p" = stand ] && jstand; [ "$p" = vias ] && jvias; [ "$p" = run ] && jrun; [ "$p" = sprint ] && jsprint
  [ "$p" = bank ] && jbank; [ "$p" = vault ] && jvault; [ "$p" = vgrid ] && jvgrid; [ "$p" = vspot ] && jvspot; [ "$p" = wall ] && jwall; done
grep -E '^JUMP END' "$L" | tail -1 | cut -c1-700
echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
