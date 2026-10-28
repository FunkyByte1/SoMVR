#!/bin/bash
# mantle20-drive.sh - build 20 (the som-b20-climb helper): the user's build 19b session (logs/boot-check-20261028-100659)
# mark 2 (hanging over the cliff's top with the eyes 110 cm over it for 16 s: "press A (not hold)") and mark 3 (the
# overhangs: the slab the user hung under 8.7 s). climb2-drive.sh's and pin16-drive.sh's functions (sourced), plans:
#   bank   the arrival's meadow bank (PIN_BANK_FROM): hand over hand up to a hold on its top, that hand pushed down until
#          the eyes are AM_OVER cm over the top, both hands open (the topped hang); `climb mantle off` and an A tap
#          (build 19b: the drop's ring, nothing gets him on), then `climb mantle glide`: the ring on the top (both eyes),
#          an A tap = the glide (both eyes during it), he stands, walked on
#   cliff  the user's cliff from their hang spot (AT2) through the slab to the top (~3532), then as bank with the eyes
#          CLIFF_OVER (100) cm over the top: mark 2's state
#   slab   the session's slab (SLAB_AT: on the narrow ledge where the user hung under it): the right hand on the slab's
#          lip, pulls of 40 cm in 1.2 s (as a player's): the root after each, the climb through, a stall (both eyes)
#   low    the bank again: holding the top with the eyes 15 cm over it, A = the soft "no" (nothing moves); pushed on
#          to AM_OVER and let go: `climb mantle off` + an A tap (19b), then glide with the head looking down at the ring
#   blink  the bank again, the topped hang, `climb mantle blink` + an A tap = the quick blink onto the ring
#   route  the user's own route up the cliff (ROUTE_FROM, the meadow under the session's climb) past the session's slab
#          with a player's pulls to the top 3531 by mark 2's place, then as cliff
# Several in one boot (PLAN="bank cliff slab"). Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
AM_OVER=${AM_OVER:-60}
CLIFF_OVER=${CLIFF_OVER:-100}
SLAB_AT=${SLAB_AT:-33227.6 2575 -28048.0}
ROUTE_FROM=${ROUTE_FROM:-33217.6 2038.2 -28042.0}
# A pressed until the layer has run it (+0.1 s: a tap, under the drop mark's 0.3 s hold), then $1 dumps 0.25 s apart
tapA() { local k n0
  userin && bail "the user is in VR"
  n0=$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")
  printf 'btn R a 1\n' > "$R/xrtest.txt"; echo "$(date +%T) xrtest: A tap"
  for k in $(seq 1 40); do [ "$(grep -c "'btn R a 1': ok" "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.05; done
  sleep 0.1
  if [ "${1:-0}" -gt 0 ]; then printf 'btn R a 0\ndump\n' > "$R/xrtest.txt"
    for k in $(seq 2 "$1"); do sleep 0.25; printf 'dump\n' > "$R/xrtest.txt"; done
  else printf 'btn R a 0\n' > "$R/xrtest.txt"; fi
  sleep 1.2; }
ams() { grep -E '^CLIMB t=.* (mantle|drop mark|end:|stand:|hang:)' "$L" | tail -${1:-4} | cut -c1-360; }
dumps() { grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -${1:-1} | grep -o '[0-9]*$' | tr '\n' ' '; echo; }
yof() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2 | cut -d. -f1; }
# grab with hand $1 at (y $2, z from $3 on, 12 cm deeper each try); GZ = the z it held at
grabz() { local h=$1 y=$2 z=$3 xs n0 i k; [ "$h" = L ] && xs=-20 || xs=20
  w "hands squeeze $h 0" 0.3; w "hands test $h $xs $y $z 0 0 0" 0.5
  for k in 1 2 3 4; do
    n0=$(cnt "^CLIMB t=.* grab $h:"); w "hands squeeze $h 1" 0.3
    for i in 1 2 3 4 5 6; do [ "$(cnt "^CLIMB t=.* grab $h:")" -gt "$n0" ] && { GZ=$z; return 0; }; sleep 0.25; done
    w "hands squeeze $h 0" 0.5; z=$((z + 12)); w "hands test $h $xs $y $z 0 0 0" 0.5
  done; return 1; }
# hand over hand up the face in front (the first hold by $1 at y $2) until a hold on a top at or over $3 (TOP, TY, GZ)
upface() { local first=$1 y0=$2 tmin=$3 k HOLD HX O OX REACH=${UP_REACH:-40} GOT TYv
  TOP=; TY=
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
  grabz "$first" "$y0" $REACH || { echo "$(date +%T) no first grab"; return 1; }
  HOLD=$first
  for k in $(seq 1 30); do
    climbing || { echo "$(date +%T) not climbing"; fl 3; return 1; }
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    w "hands swing $HOLD $HX -45 $GZ 0 0 0 1000" 1.5
    [ $((k % 3)) = 0 ] && echo "$(date +%T) pull $k ($HOLD): $(root)"
    GOT=
    for y in 35 10 -15; do grabz $O $y $REACH && { GOT=$y; break; }; done
    [ -n "$GOT" ] || { echo "$(date +%T) no grab $O"; fl 3; secs 1; return 1; }
    w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX -40 10 0 0 0" 0.4
    TYv=$(grep -E "^CLIMB t=.* grab $O:" "$L" | tail -1 | grep -oE 'edge 1, top [0-9]+' | grep -oE '[0-9]+$')
    if [ -n "$TYv" ] && [ "$TYv" -ge "$tmin" ]; then TOP=$O; TY=$TYv; break; fi
    HOLD=$O
  done
  [ -n "$TOP" ] || { echo "$(date +%T) no top"; return 1; }
  echo "$(date +%T) the top $TY held by $TOP (z $GZ): $(grep -E "^CLIMB t=.* grab $TOP:" "$L" | tail -1 | grep -oE 'edge 1[^;]*')"; }
# the top hand pushed down (slowly: no fling) until the eyes are $1 cm over the hold; tophang: still, then let go
tophold() { local over=$1 TX
  [ "$TOP" = L ] && TX=-20 || TX=20
  w "hands swing $TOP $TX -$over $GZ 0 0 0 2000" 2.6; sleep 0.8
  echo "$(date +%T) pushed up: $(root)"; secs 1 | cut -c1-280; }
tophang() { local TX; [ "$TOP" = L ] && TX=-20 || TX=20
  tophold "$1"
  w "hands squeeze $TOP 0" 1.2; w "hands test $TOP $TX -40 10 0 0 0" 0.4
  fl 2; ams 2; }
# A at the topped hang: off (build 19b), then glide; both eyes before, at the ring, during and after the glide
mantleab() { local tag=$1 n0 k
  climbing || { echo "$(date +%T) not climbing: no hang"; return 1; }
  w "climb mantle off"; sleep 0.5; d "$tag-hang"
  tapA 0; ams 3; echo "$(date +%T) A (mantle off): $(root)"; d "$tag-off-after"
  climbing || { echo "$(date +%T) not climbing after the off tap"; return 1; }
  w "climb mantle glide" 1.0; ams 1
  x "head 0 0 0 pitch -45 s 0.4"; sleep 0.6; d "$tag-ring"; m "am-$tag-ring"; x "head 0 0 0 s 0.4"; sleep 0.4
  n0=$(cnt 'mantle: the glide.s end')
  tapA 3; echo "$(date +%T) the dumps during the glide: $(dumps 3)"
  for k in $(seq 1 16); do [ "$(cnt 'mantle: the glide.s end')" -gt "$n0" ] && break; sleep 0.25; done
  [ "$(cnt 'mantle: the glide.s end')" -gt "$n0" ] || echo "$(date +%T) NO GLIDE END"
  ams 5; echo "$(date +%T) after the glide: $(root)"; climbing && echo "$(date +%T) STILL CLIMBING"
  sleep 1; d "$tag-after"; m "am-$tag-after"
  w "hands test off" 0.5
  w "stick 0 1 0 1.5" 2.2; echo "$(date +%T) walked: $(root)"; lastloco 1; d "$tag-walked"; }
bankam() {
  echo "== bank: the meadow bank's top, the eyes $AM_OVER cm over it"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "${PIN_BANK_FROM:-31700 1830 -24825}"
  probe; NB=$(near -50); [ -n "$NB" ] || NB=$(near +0); [ -n "$NB" ] && turnto "${NB% *}"; probe
  local k d d0 PREV=
  for k in $(seq 1 20); do
    d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
    [ -n "$d" ] || break; [ "$d" -le 62 ] && break
    if [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
    probe; [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && break; PREV=$d
  done
  echo "$(date +%T) at the face: $(root)"
  upface L 0 2100 || return 1
  tophang "$AM_OVER"
  mantleab bank; }
cliffam() {
  echo "== cliff: the user's cliff from $AT2 through the slab to the top, the eyes $CLIFF_OVER cm over it"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "$AT2"; echo "$(date +%T) at: $(root)"; faceto
  w "climb trace 6"
  UP_REACH=40 upface R 35 3500 || { grep -E '^CLIMB t=.*(through|end:|top:)' "$L" | tail -6 | cut -c1-260; return 1; }
  grep -E '^CLIMB t=.*(through|retop)' "$L" | tail -6 | cut -c1-260
  tophang "$CLIFF_OVER"
  mantleab cliff; }
# to the bank's face from PIN_BANK_FROM (the arrival's meadow bank, top ~2148) and up to a hold on its top
bankup() { local k d d0 PREV=
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
  upface L 0 2100; }
# low: holding the top with the eyes 15 cm over it: A = the soft no, nothing moves; then on to AM_OVER, the A/B
lowam() { local y0
  echo "== low: holding the top with the eyes 15 cm over it: A says no; then the eyes $AM_OVER over, off/glide"
  bankup || return 1
  w "climb mantle glide" 0.4
  tophold 15; y0=$(yof); ams 1
  tapA 0; ams 2; echo "$(date +%T) after A (too low): $(root) (before: root y $y0)"
  climbing && echo "$(date +%T) still climbing (ok)"
  tophang "$AM_OVER"
  mantleab low; }
# blink: the topped hang at the bank, `climb mantle blink`, A = the quick blink onto the ring
blinkam() {
  echo "== blink: the topped hang at the bank, A = the quick blink"
  bankup || return 1
  tophang "$AM_OVER"; w "climb mantle blink" 0.8; ams 1
  tapA 2; waitfor '^PRACTICE t=.*(arrived|did not arrive)' 6; sleep 2.5; ams 3
  grep -E '^PRACTICE t=' "$L" | tail -2 | cut -c1-200; echo "$(date +%T) after the blink: $(root)"; d "blink-after"
  w "climb mantle glide" 0.4; w "hands test off" 0.5; climbing && { x "btn R a 1"; x "btn R a 0"; sleep 2; }; }
# route: the user's own way up the cliff (the session's x/z) past the session's slab to the top, then mark 2's A/B
routeam() { local n0
  echo "== route: the user's own climb from $ROUTE_FROM past the session's slab to the top"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "$ROUTE_FROM"; echo "$(date +%T) at: $(root)"; faceto; probe
  for k in $(seq 1 10); do d=$(ahead -50); [ -n "$d" ] && [ "$d" -le 62 ] && break; w "stick 0 0.6 0 0.6" 1.0; probe; done
  echo "$(date +%T) at the face: $(root)"; d "route-start"
  n0=$(cnt 'through: the pull blocked')
  UP_REACH=40 upface L 0 3500 || { grep -E '^CLIMB t=.*(through|end:|top:)' "$L" | tail -6 | cut -c1-260; return 1; }
  echo "$(date +%T) the face climbed: through starts $(( $(cnt 'through: the pull blocked') - n0 ))"
  grep -E '^CLIMB t=.*(through|retop|top:)' "$L" | tail -8 | cut -c1-260
  tophang "$CLIFF_OVER"
  mantleab route; }
# slab: the user's 8.7 s under the slab - the lip from below, pulls as a player's
slabam() { local k n0
  echo "== slab: under the session's slab at $SLAB_AT"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "$SLAB_AT"; echo "$(date +%T) at: $(root)"; faceto
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -1 | cut -c1-300
  d "slab-start"
  GZ=; for y in 25 15 35 5; do grabz R $y 30 && break; done
  [ -n "$GZ" ] || { echo "$(date +%T) no hold on the slab"; return 1; }
  fl 1; w "climb trace 6"
  n0=$(cnt 'through: the pull blocked')
  for k in 1 2 3; do
    w "hands swing R 20 -15 $GZ 0 0 0 1200" 1.8; echo "$(date +%T) pull $k: $(root)"; secs 1 | cut -c1-260
    climbing || break
    [ $k = 1 ] && d "slab-pull1"
    grabz L 30 30 || grabz L 10 30 || break
    w "hands squeeze R 0" 0.5; w "hands test R 20 30 30 0 0 0" 0.4; grabz R 35 30 || grabz R 10 30 || break
    w "hands squeeze L 0" 0.5; w "hands test L -20 -40 10 0 0 0" 0.4
  done
  echo "$(date +%T) through starts $(( $(cnt 'through: the pull blocked') - n0 )); $(root)"
  grep -E '^CLIMB t=.*(through|overhang|pullover|stall|top:)' "$L" | tail -6 | cut -c1-280
  d "slab-after"; m "am-slab"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }; }
card
for p in $PLAN; do [ "$p" = bank ] && bankam; [ "$p" = cliff ] && cliffam; [ "$p" = slab ] && slabam
  [ "$p" = low ] && lowam; [ "$p" = blink ] && blinkam; [ "$p" = route ] && routeam; done
grep -E '^CLIMB END build 20' "$L" | tail -1 | cut -c1-400
echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
