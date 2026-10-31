#!/bin/bash
# climb27-drive.sh - build 27 (the som-b27-climb3 helper, docs/re-xr-pipeline.md 117): climbing the real world by hand,
# end to end - a forge tower to its top, a fort wall, a cliff, and down again - the way a player does it, timed, every
# stall logged, both eyes at each stage. climb2/pin16-drive.sh's functions (sourced) and move24-drive.sh's ways (copied).
# A site = "name:x y z[|EDGE[|LEVELS[|PLANS]]]" (a spot on its top (the foot for an up only), the edge if known, 2 =
# down a level more: a tower on a crag, the site's own plans: comma-separated; TOPS="a:..;b:..", in order). Per site, PLAN words (in order):
#   survey  on the top: `vault edges` round him; the chosen edge (EDGE "lipx lipy lipz out drop sx sy sz" overrides):
#           at his own level (the lip within 60 cm of his feet) first, a WALL with room on the top and the largest drop
#           (at most DROP_MAX), else the largest SLOPE, else an OVERHANG
#   down    at the edge's stand point faced out: the right hand reaching down over the lip = the glide out and down to
#           hang (SOMVR_CLIMB_DOWN_OVER); turned to the face; DOWN_N holds hand over hand down; both open = the hang;
#           A held (the timer) = the blink down to the foot (or the plain fall)
#   up      from where he stands (the foot after `down`, else FOOT "x y z out"): turned to the face, walked to it,
#           hand over hand up (STYLE pull|fling|mix: every FLING_EVERY-th move a fling, caught at its apex), a
#           ledge's mantle (A at the ring) on the way, the top's lip, the mantle onto the top; each move one line:
#           the rise, the time, a stall (under 15 cm) with the climb's lines since the move
# Several sites in one boot. Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
[ "$PLAN" = probe ] && PLAN="survey down up"
TOPS=${TOPS:-"tower:15790 6237 -3110||2;fort:25500 3649 2000|25522 3547 2000 90 907 25500 3649 2000 WALL"}
STYLE=${STYLE:-mix}
FLING_EVERY=${FLING_EVERY:-3}
DOWN_N=${DOWN_N:-3}
UP_MAX=${UP_MAX:-45}
DROP_MAX=${DROP_MAX:-3000}
ST=$R/climb27-steps.txt
yaw() { local k v; for k in 1 2 3; do w "climb state" 0.6 >/dev/null
  v=$(grep -E '^CLIMB state' "$L" | tail -1 | grep -oE "the head's [0-9.-]+" | grep -oE '[0-9.-]+$'); [ -n "$v" ] && { echo "$v"; return 0; }; done; }
faceyaw() { local want=$1 k y d
  for k in 1 2 3 4 5 6 7 8 9 10 11 12; do
    sleep 0.4; y=$(yaw); [ -n "$y" ] || { echo "no yaw"; return 1; }
    d=$(python3 -c "print(int(round(((float('$want')-float('$y'))+180)%360-180)))")
    [ "${d#-}" -le 16 ] && { echo "$(date +%T) the head's yaw $y (want $want)"; return 0; }
    [ "$d" -gt 0 ] && w "turn+" 0.9 >/dev/null || w "turn-" 0.9 >/dev/null
  done; echo "$(date +%T) the head's yaw $y (want $want): not reached"; }
aon() { local n0 k; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  echo "$(date +%T) A down"; }
aoff() { printf 'btn R a 0\n' > "$R/xrtest.txt"; echo "$(date +%T) A up"; sleep 0.6; }
tapA() { local k n0; n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log"); printf 'btn R a 1\n' > "$R/xrtest.txt"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  sleep 0.1; printf 'btn R a 0\n' > "$R/xrtest.txt"; sleep 1.2; }
# both eyes: a dump named $1 (the frame into $ST)
dd() { local f; printf 'dump\n' > "$R/xrtest.txt"; sleep 1.2
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$ST"; }
dumpsn() { local k f; for k in $(seq 1 "$3"); do printf 'dump\n' > "$R/xrtest.txt"; sleep "$2"; done; sleep 1
  f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -"$3" | grep -o '[0-9]*$' | tr '\n' ' '); echo "$1 $f" | tee -a "$ST"; }
root() { local n0 k; n0=$(grep -c '^CLIMB state' "$L"); w "climb state" 0.25
  for k in $(seq 1 25); do [ "$(grep -c '^CLIMB state' "$L")" -gt "$n0" ] && break; sleep 0.1; done
  grep -E '^CLIMB state' "$L" | tail -1 | grep -oE 'root [0-9.,-]+ yaw [0-9.-]+'; }
ry() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2 | cut -d. -f1; }
unclimb() { climbing || return 0; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; aon; sleep 1.4; aoff; sleep 2
  climbing && { aon; sleep 1.4; aoff; sleep 2; }; climbing && echo "$(date +%T) STILL climbing"; }
# hand $1 at (x $4, y $2 to the eyes, z $3 ahead) squeezed until a grab (z 8 cm deeper each try): GZ = where it held
grabat() { local h=$1 y=$2 z=$3 xs=${4:-} n0 i k; [ -n "$xs" ] || { [ "$h" = L ] && xs=-20 || xs=20; }
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.4
  for k in 1 2 3 4; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.2
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.2; done
    w "hands squeeze $h 0" 0.3; z=$((z + 8)); w "hands test $h $xs $y $z 0 0 0" 0.3
  done; return 1; }
# the climb's lines since log line $1 that tell what happened in a move (short)
since() { tail -n +"$1" "$L" | grep -E '^CLIMB t=.* (through|top:|top-out|hang:|resume:|end:|fling (launch|none|top|glide|catch|apex)|mantle:|round lip|pullover|hug|blocked|down:|drop mark)' |
  sed -E 's/^CLIMB t=[0-9.]+ frame=[0-9]+ //' | cut -c1-${2:-200} | tail -${3:-4}; }
# the survey on a top: the edge to go down
survey() { local nm=$1 n0 f
  n0=$(wc -l < "$L"); w "vault edges ${SV_R:-1500} ${SV_STEP:-100}" 0.5; waitfor '^VAULT survey .* edges done' 150
  grep -E '^VAULT survey .* edges done' "$L" | tail -1 | cut -c1-260
  f=$R/climb27-edges-$nm.txt; tail -n +"$n0" "$L" | grep -E '^VAULT edge t=' | cut -c1-400 > "$f"
  echo "edges: $(grep -c ' WALL:' "$f") walls, $(grep -c ' SLOPE:' "$f") slopes, $(grep -c ' OVERHANG:' "$f") overhangs"
  [ -n "${EDGE:-}" ] && { echo "EDGE given: $EDGE"; return 0; }
  local fy; fy=$(( $(ry) - 103 ))
  EDGE=$(python3 - "$f" "$DROP_MAX" "$fy" <<'EOF'
import re, sys
best = None
fy = float(sys.argv[3])
for l in open(sys.argv[1]):
    m = re.search(r' (WALL|SLOPE|OVERHANG): the lip at ([-0-9]+) ([-0-9]+) ([-0-9]+) heading ([0-9]+).*the drop ([-0-9]+) cm; room on the top ([01]); stand at ([-0-9]+) ([-0-9]+) ([-0-9]+)', l)
    if not m: continue
    k, lx, ly, lz, h, d, room, sx, sy, sz = m.groups()
    d = int(d)
    if room != '1' or d < 300 or d > int(sys.argv[2]): continue
    s = (abs(int(ly) - fy) <= 60, 2 if k == 'WALL' else 1 if k == 'SLOPE' else 0, d)  # his own level's edges first
    if best is None or s > best[0]: best = (s, f'{lx} {ly} {lz} {h} {d} {sx} {sy} {sz} {k}')
print(best[1] if best else '')
EOF
)
  echo "the edge: ${EDGE:-none}"; [ -n "$EDGE" ]; }
# down: from the edge's stand point faced out, the reach down over the lip and the grip
down() { local nm=$1 lx ly lz out drop sx sy sz kind n0 n1 k HOLD O HX OX t0 nm0
  set -- $EDGE; lx=$1 ly=$2 lz=$3 out=$4 drop=$5 sx=$6 sy=$7 sz=$8 kind=$9
  unclimb; echo "== $nm down: the lip $lx $ly $lz out $out ($kind, the drop $drop cm)"
  x "btn R squeeze 0"; w "hands squeeze L 0" 0.3; w "hands test off"; go "$sx $((sy + 10)) $sz"; faceyaw "$out"
  echo "$(date +%T) at: $(root)"; dd "$nm-down-at"; t0=$(date +%s.%N)
  n0=$(cnt '^CLIMB t=.* down:'); n1=$(cnt '^CLIMB t=.* down: the glide.s end'); nm0=$(wc -l < "$L")
  w "hands squeeze R 0" 0.3; w "hands test R 0 -115 60 0 0 0" 0.6; w "hands squeeze R 1" 0.05; dumpsn "$nm-down-glide" 0.25 4
  if [ "$(cnt '^CLIMB t=.* down:')" -le "$n0" ]; then sleep 1; echo "$(date +%T) DOWN: no down line"; since "$nm0" 300
    w "hands squeeze R 0" 0.6; climbing && { aon; sleep 1.4; aoff; sleep 3; }; return 2; fi
  waitfor 'down: the glide.s end' 4; since "$nm0" 360 3
  if [ "$(cnt '^CLIMB t=.* down: the glide.s end')" -le "$n1" ]; then echo "$(date +%T) DOWN: no glide"
    w "hands squeeze R 0" 0.6; climbing && { aon; sleep 1.4; aoff; sleep 3; }; return 1; fi
  echo "$(date +%T) hanging: $(root)"; dd "$nm-down-hang"
  for k in 1 2 3 4 5 6; do w "turn+" 0.7; done; facewall; dd "$nm-down-turned"; HOLD=R
  for k in $(seq 1 "$DOWN_N"); do
    [ "$HOLD" = L ] && { HX=-20; O=R; OX=20; } || { HX=20; O=L; OX=-20; }
    nm0=$(wc -l < "$L")
    grabat $O -60 35 || { echo "$(date +%T) down $k: no grab $O"; since "$nm0"; break; }
    w "hands squeeze $HOLD 0" 0.3; w "hands test $HOLD $HX -40 10 0 0 0" 0.2
    w "hands swing $O $OX -5 $GZ 0 0 0 1000" 1.3
    echo "$(date +%T) down $k ($O): $(root)"; HOLD=$O
  done
  dd "$nm-down-face"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.6; w "hands test off" 0.3
  x "head 0 0 0 pitch -50 s 0.4"; sleep 0.4; nm0=$(wc -l < "$L")
  aon; sleep 0.35; dd "$nm-timer"; sleep 0.3; aoff; x "head 0 0 0 s 0.3"
  waitfor 'PRACTICE t=.*arrived|the blink down|the plain fall|end:' 8; sleep 2; since "$nm0" 300 3
  echo "$(date +%T) DOWN $nm: at the foot after $(python3 -c "import time; print(round(time.time()-$t0, 1))") s: $(root)"; dd "$nm-foot"; }
# the mantle's ring shown now: the climb's own state (a fresh `climb state` line: "the mantle's ring shown"; the
# layer's objective lines are not written at each change, and an empty grep made the old test true)
ringnow() { root >/dev/null; grep -E '^CLIMB state' "$L" | tail -1 | grep -q "the mantle's ring shown"; }
waitring() { local k; for k in $(seq 1 "${1:-6}"); do ringnow && return 0; sleep 0.1; done; return 1; }
# the nearest wall by the probe, turned to (30 deg snaps)
facewall() { local nb; probe; nb=$(near +0); [ -n "$nb" ] || nb=$(near -50); echo "$(date +%T) the nearest face: ${nb:-none}"
  [ -n "$nb" ] && turnto "${nb% *}"; }
# a ledge or the top reached by a hand: pull it down (the eyes over), the ring = A (the mantle's glide); $1 = the hand
mantle() { local H=$1 HX n0 k; [ "$H" = L ] && HX=-20 || HX=20; n0=$(wc -l < "$L")
  for k in now -60 -95; do
    climbing || break
    [ "$k" = now ] || w "hands swing $H $HX $k ${GZ:-40} 0 0 0 1200" 1.4
    if waitring 6; then
      echo "$(date +%T) the ring shown ($k): A"; dd "$UPNM-ring-$k"; tapA; waitfor 'mantle: the glide.s end' 3; sleep 0.8
      climbing || break; fi
  done
  since "$n0" 330 5
  climbing && return 1
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.3; return 0; }
# walk to the face ahead (the probe's +0 within 45 cm), at most 6 steps
tothewall() { local k a
  for k in 1 2 3 4 5 6; do probe; a=$(ahead +0); echo "$(date +%T) the face ahead: ${a:-none} cm"
    [ -n "$a" ] && [ "$a" -le 45 ] && return 0; w "stick 0 0.5 0 0.5" 0.9; done; return 1; }
# which hand holds now: the last grab / let go line per hand since log line $1 (else $2 = the one held before)
holds() { local h last out=; for h in L R; do
    last=$(tail -n +"$1" "$L" | grep -E "^CLIMB t=.* (grab|let go) $h[: ]" | tail -1)
    if [ -n "$last" ]; then echo "$last" | grep -q " grab $h" && out="$out$h"; elif [ "$h" = "$2" ]; then out="$out$h"; fi
  done; echo "$out"; }
# up: hand over hand from the foot to a hold whose top is $2+ (the site's top), a mantle onto it
up() { local nm=$1 tmin=$2 out=$3 k HOLD HX O OX y0 y1 rise GOT t0 tm n0 n1 stall=0 flings=0 caught=0 pulls=0 mant=0 nextpic TYv lost=0 HY=35 hs kind
  UPNM=$nm; echo "== $nm up: to the top $tmin+ (the face at heading $(( (out + 180) % 360 )), $STYLE)"
  x "btn R squeeze 0"; w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.3
  facewall; tothewall || { facewall; tothewall; } || echo "$(date +%T) no face within 45 cm"
  y0=$(ry); echo "$(date +%T) at the foot: $(root)"; dd "$nm-up-foot"; nextpic=$((y0 + 500)); t0=$(date +%s.%N)
  w "hands test L -20 0 40 0 0 0" 0.3; w "hands test R 20 35 40 0 0 0" 0.5
  if grabat R 35 40; then HY=35; elif grabat R 10 40; then HY=10; else echo "$(date +%T) UP: no first hold"; probe; return 1; fi
  HOLD=R; n0=$(wc -l < "$L")
  for k in $(seq 1 "$UP_MAX"); do
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    climbing || { echo "$(date +%T) UP move $k: not climbing"; since "$n0" 300 4; lost=1; break; }
    n0=$(wc -l < "$L"); tm=$(date +%s.%N); y0=$(ry); GOT=
    if [ "$STYLE" = fling ] || { [ "$STYLE" = mix ] && [ $((k % FLING_EVERY)) = 0 ]; }; then
      # the fling: the held hand pulled down 90 cm in 0.3 s and let go on the way (70 %: 4 frames at 20 Hz in its window); the other hand up, its grip
      # at the arc's top (~0.6 s), then the first hand's too if that one found nothing (the catch)
      kind=fling; flings=$((flings + 1))
      w "hands squeeze $O 0" 0.2; w "hands test $O $OX 35 $(( ${GZ:-40} + 8 )) 0 0 0" 0.2; n1=$(wc -l < "$L")
      printf 'hands swing %s %s %s %s 0 0 0 300 0.7' "$HOLD" "$HX" "$((HY - 90))" "${GZ:-40}" > "$R/somvr.trigger"; sleep 0.75
      printf 'hands squeeze %s 1' "$O" > "$R/somvr.trigger"; sleep 0.6
      hs=$(holds "$n1" ""); if [ -z "$hs" ]; then w "hands test $HOLD $HX 35 ${GZ:-40} 0 0 0" 0.2; w "hands squeeze $HOLD 1" 0.8; fi
      h=$(tail -n +"$n1" "$L" | grep -oE '^CLIMB t=.* grab [LR]:' | tail -1 | grep -oE '[LR]:$' | tr -d :)
      if [ -n "$h" ]; then HOLD=$h; GOT=caught; caught=$((caught + 1)); fi
      for h in L R; do if [ "$h" != "$HOLD" ]; then w "hands squeeze $h 0" 0.3; fi; done; HY=35
      if [ -z "$GOT" ]; then echo "$(date +%T) UP move $k: the fling caught nothing"; since "$n1" 300 5
        # a hang in the air (letting go hangs): the hands on the face again, else the end
        if grabat R 35 40; then HOLD=R; HY=35; GOT=regrab; elif grabat R 10 40; then HOLD=R; HY=10; GOT=regrab; else lost=1; break; fi
      fi
    else
      kind=pull; pulls=$((pulls + 1))
      w "hands swing $HOLD $HX -45 ${GZ:-40} 0 0 0 1000" 1.3
      for y in 35 10 -15; do grabat $O $y ${GZ:-40} && { GOT=$y; break; }; done
      if [ -z "$GOT" ]; then
        # no hold above: a ledge or the top with the ring - the mantle
        if waitring 6; then
          echo "$(date +%T) UP move $k: no hold above; the ring: A"; mantle "$HOLD" && mant=$((mant + 1))
          y1=$(ry); echo "$(date +%T) UP move $k: mantled onto $y1"
          [ "$y1" -ge $((tmin - 20)) ] && break
          facewall; tothewall
          w "hands test L -20 0 40 0 0 0" 0.3; w "hands test R 20 35 40 0 0 0" 0.5
          if grabat R 35 40; then HY=35; elif grabat R 10 40; then HY=10; else echo "$(date +%T) UP: no hold after the ledge"; break; fi
          HOLD=R; continue
        fi
        echo "$(date +%T) UP move $k: no grab $O (no ring)"; since "$n0" 300 5; stall=$((stall + 1)); break
      fi
      w "hands squeeze $HOLD 0" 0.3; w "hands test $HOLD $HX -40 10 0 0 0" 0.2; HOLD=$O; HY=$GOT
    fi
    y1=$(ry); rise=$(( ${y1:-0} - ${y0:-0} ))
    TYv=$(tail -n +"$n0" "$L" | grep -E "^CLIMB t=.* grab $HOLD:" | tail -1 | grep -oE 'edge 1, top [0-9]+' | grep -oE '[0-9]+$')
    printf '%s UP move %d (%s %s, %s): %+d cm in %.1f s, the root %s %s\n' "$(date +%T)" "$k" "$kind" "$GOT" "$HOLD" "$rise" \
      "$(python3 -c "import time; print(time.time()-$tm)")" "$y1" "${TYv:+(the top $TYv in the hand)}"
    if [ "$rise" -lt 15 ]; then stall=$((stall + 1)); echo "  STALL:"; since "$n0" 260 5 | sed 's/^/    /'; fi
    [ "${y1:-0}" -ge "$nextpic" ] && { dd "$nm-up-$y1"; nextpic=$((y1 + 500)); }
    if [ -n "$TYv" ] && [ "$TYv" -ge "$tmin" ] && [ "$TYv" != "${TRIED:-}" ]; then
      echo "$(date +%T) the top's lip in the $HOLD hand ($TYv)"; dd "$nm-up-lip"
      if mantle "$HOLD"; then mant=$((mant + 1)); break; fi
      TRIED=$TYv; echo "$(date +%T) no way onto $TYv: climbing on"
      w "hands swing $HOLD $HX 10 ${GZ:-40} 0 0 0 600" 0.8; HY=10; fi
  done
  climbing && { echo "$(date +%T) UP: still climbing at the end"; since "$n0" 300 4; }
  y1=$(ry); TRIED=
  echo "$(date +%T) UP $nm: $(python3 -c "import time; print(round(time.time()-$t0, 1))") s, pulls $pulls flings $flings (caught $caught) mantles $mant stalls $stall lost $lost; the root now $y1 (the top $tmin): $(root)"
  if ! climbing && tail -n +"$n0" "$L" | grep -qE '^CLIMB t=.* end: over'; then echo "UP $nm: ON TOP ($(grep -E '^CLIMB t=.* end: over' "$L" | tail -1 | grep -oE 'end: over: [^(]*' | cut -c1-80))"
  else echo "UP $nm: NOT on top"; fi
  dd "$nm-up-end"; w "stick 0 0.6 0 1.0" 1.5; dd "$nm-up-walked"; }
# on foot at the edge faced out: A held = the descent's blink down (when the reach down over it found no way)
footdown() { local nm=$1 n0; n0=$(wc -l < "$L"); w "stick 0 0.3 0 0.5" 0.9; aon; sleep 0.35; dd "$nm-footA"; sleep 0.4; aoff
  waitfor 'PRACTICE t=.*arrived|the blink down|the plain fall' 8; sleep 2; since "$n0" 300 3; echo "$(date +%T) FOOTDOWN $nm: $(root)"; }
site() { local nm=${1%%:*} at=${1#*:} lip
  EDGE=; LEVELS=1; local SP=$PLAN; IFS='|' read -r at EDGE LEVELS SP2 <<< "$at"; [ -n "$LEVELS" ] || LEVELS=1
  [ -n "$SP2" ] && SP=${SP2//,/ }; local PLAN=$SP
  echo "=== site $nm ($at${EDGE:+; the edge given: $EDGE})"
  case " $PLAN " in *" survey "*|*" down "*|*" up "*) unclimb; go "$at"; echo "$(date +%T) at the site: $(root)"
    set -- $at; [ "$(ry)" -lt $(( ${2%.*} - 300 )) ] && { echo "(not on the top: again)"; go "$at"; echo "$(date +%T) on the top: $(root)"; }
    dd "$nm-top" ;; esac
  [ -n "$EDGE" ] || case " $PLAN " in *" survey "*|*" down "*) survey "$nm" || { echo "no edge"; return 1; } ;; esac
  case " $PLAN " in *" down "*) down "$nm" || footdown "$nm"
    # a level more (a tower on a crag): the edges round the landing, the deepest one walked to (a trip), A at it
    if [ "${LEVELS:-1}" -ge 2 ]; then local TOPEDGE=$EDGE ly
      EDGE=; SV_R=${SV_R2:-900} survey "$nm-2" && { set -- $EDGE; ly=$2
        echo "== $nm down 2: the edge $EDGE"; go "$6 $(( $7 + 10 )) $8"; faceyaw "$4"; dd "$nm-down2-at"; footdown "$nm-2"; }
      EDGE=$TOPEDGE; fi ;; esac
  case " $PLAN " in *" up "*) [ -n "$EDGE" ] || { echo "no edge for up"; return 1; }
    if [ -n "${FOOT:-}" ]; then set -- $FOOT; go "$1 $2 $3"; fi
    set -- $EDGE; up "$nm" "$(( $2 - 40 ))" "$4" ;; esac
  # down again from the top just climbed: turned round, at the edge, A held (the timer) = the blink down
  case " $PLAN " in *" blink "*) climbing && unclimb; for k in 1 2 3 4 5 6; do w "turn+" 0.7; done; footdown "$nm-top" ;; esac; }
card
w "climb unstuck on"; w "climb timer on"; w "climb down over"; w "climb ohmantle on"
IFS_=$IFS; IFS=';'; for s in $TOPS; do IFS=$IFS_; site "$s"; IFS=';'; done; IFS=$IFS_
grep -E '^CLIMB END' "$L" | tail -3 | cut -c1-400
echo "steps done $(date +%T)"; sleep 2; w "endlogs" 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
