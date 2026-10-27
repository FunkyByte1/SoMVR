#!/bin/bash
# climb2-drive.sh - build 16 round 2 (the climb2 helper): the user's cliff (the meadow bank by the practice area: the
# narrow ledge 2562 under the slab, the top ~3534). pin16-drive.sh's functions (sourced, PIN_LIB=1), new plans:
#   trace  at the user's hang spot (PIN_AT2): our hand climb pulled up into the slab with `climb trace` (the stall's
#          contacts), then the stock wall run (boot with SOMVR_LOCO=0 SOMVR_GAMEMOVES=on SOMVR_CLIMB_LEDGES=game: the
#          stick at the face, the free-run latch, A) with `climb trace`: how the game's capsule passes the slab
# Plans of pin16-drive.sh run too (PLAN="trace lip" ...). Stops at once (quit) when the user goes into VR.
PIN_LIB=1 . "$(dirname "$0")/pin16-drive.sh"
trc() { grep -E '^CLIMB trace t=' "$L" | tail -${1:-30} | sed -E 's/^CLIMB trace //' | cut -c1-${2:-330}; }
faceto() { probe; NB=$(near +0); [ -n "$NB" ] || NB=$(near -50); echo "nearest: ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"; }
traceplan() { local n0
  echo "== trace 1: our hand climb at the user's hang spot $AT2, pulled into the slab"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "$AT2"; echo "$(date +%T) at: $(root)"
  faceto
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -1 | cut -c1-420
  contacts 6
  if onslab R; then
    w "climb trace 4" 0.2
    w "hands swing R 20 -15 25 0 0 0 1500" 2.4
    echo "$(date +%T) pull 1 (R): $(root)"; secs 2; contacts 8
    trc 40
    if onslab L; then
      w "hands squeeze R 0" 0.5
      w "climb trace 4" 0.2
      w "hands swing L -20 -15 25 0 0 0 1500" 2.4
      echo "$(date +%T) pull 2 (L): $(root)"; secs 2; contacts 8
      trc 40
    fi
    m "c2-stall"
  else echo "$(date +%T) no hold on the slab"; fi
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }
  echo "== trace 2: the stock wall run at $AT2"
  go "$AT2"; echo "$(date +%T) at: $(root)"
  faceto
  w "climb watch" 0.3
  w "stick 0 1 0 8" 0.3; x "btn L stick 1"; x "btn L stick 0"
  sleep 3; echo "$(date +%T) pushing: $(root)"
  w "climb trace 9" 0.2
  w "stick 0 1 0 6" 0.3
  for k in 1 2 3 4; do x "btn R a 1"; x "btn R a 0"; echo "$(date +%T) A $k: $(root)"; done
  sleep 4; w "stick 0 0 0 0" 0.5; w "climb watch off" 0.3
  echo "$(date +%T) after: $(root)"
  trc 200 360 > "$R/trace-stock.txt"; wc -l "$R/trace-stock.txt"
  grep -E '^CLIMB state t=.*\(tags\)' "$L" | tail -14 | sed -E 's/.*t=([0-9.]+) .*root ([0-9.,-]+).*tags [0-9]+: ([^;]*);.*/\1 root \2 tags \3/' | uniq | cut -c1-160
  m "c2-stock"; }
# wallrun (boot as trace's second part): the stock wall run at the user's hang spot under `climb trace` - try 1: the
# stick into the face, A pressed while pushing (no free-run latch); try 2: run 025601's order (the latch, 8 s, then A)
wallrun1() { local k
  go "$AT2"; echo "$(date +%T) at: $(root)"; faceto
  w "climb watch" 0.3; w "climb trace 8" 0.2
  w "stick 0 1 0 6" 0.8
  for k in 1 2 3 4; do x "btn R a 1"; x "btn R a 0"; echo "$(date +%T) A $k: $(root)"; done
  sleep 3; w "stick 0 0 0 0" 0.5; w "climb watch off" 0.3; echo "$(date +%T) after: $(root)"; }
wallrun2() { local k
  go "$AT2"; echo "$(date +%T) at: $(root)"; faceto
  w "climb watch" 0.3
  w "stick 0 1 0 8" 0.3; x "btn L stick 1"; x "btn L stick 0"
  for k in 1 2 3 4 5 6; do sleep 0.6; echo "$(date +%T) +$k s: $(root)"; done
  w "climb trace 8" 0.2; w "stick 0 1 0 6" 0.3
  for k in 1 2 3 4; do x "btn R a 1"; x "btn R a 0"; echo "$(date +%T) A $k: $(root)"; done
  sleep 3; w "stick 0 0 0 0" 0.5; w "climb watch off" 0.3; echo "$(date +%T) after: $(root)"; }
wallrunplan() { local t n0
  echo "== the stock wall run at $AT2 (climb trace)"
  x "btn R squeeze 0"; w "hands test off"
  for t in 1 2 1 2; do
    n0=$(grep -cE '^CLIMB state t=.*tags [0-9]+: [^;]*Wll' "$L")
    wallrun$t
    if [ "$(grep -cE '^CLIMB state t=.*tags [0-9]+: [^;]*Wll' "$L")" -gt "$n0" ]; then echo "$(date +%T) WALL RUN (try $t)"; break; fi
    echo "$(date +%T) no wall run (try $t)"
  done
  trc 400 360 > "$R/trace-wallrun.txt"; wc -l "$R/trace-wallrun.txt"
  grep -E '^CLIMB state t=.*\(tags\)' "$L" | tail -14 | sed -E 's/.*t=([0-9.]+) .*root ([0-9.,-]+).*tags [0-9]+: ([^;]*);.*/\1 root \2 tags \3/' | uniq | cut -c1-160
  m "c2-wallrun"; }
# cliff (SOMVR_CLIMB_THROUGH): from the user's hang spot hand over hand up the face (holds 35 cm over the head, CLIFF_REACH
# ahead, pulls of 80 cm) past the slab to the top (~3534): `climb trace` at the first pulls, a dump (both eyes) when
# the climb through starts and after each pull while it is on (4 at most), the root after each pull; at a hold on the
# top's edge the top-out as topatt does it (the top hand down to 90 cm under the eyes, then toward the body; a ring:
# its pull). CLIFF_THROUGH=off: the word `climb through off` first (the stall as round 1)
cliffplan() { local k HOLD HX O OX n0 nd=0 REACH=${CLIFF_REACH:-40} TOP= TX SPOT=${1:-$AT2}
  echo "== the cliff from $SPOT (climb through ${CLIFF_THROUGH:-on})"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  w "climb through ${CLIFF_THROUGH:-on}"
  go "$SPOT"; echo "$(date +%T) at: $(root)"; faceto
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -1 | cut -c1-420
  d "cliff-start"
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
  grabh R 35 $REACH || { echo "no grab R"; return 1; }
  HOLD=R; w "climb trace 8"
  for k in $(seq 1 26); do
    climbing || { echo "$(date +%T) not climbing"; fl 3; break; }
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    n0=$(cnt 'through: the pull blocked')
    if [ $k -le 4 ]; then # the slab's zone: both eyes in the middle of the pull too (the climb through under way)
      w "hands swing $HOLD $HX -5 $REACH 0 0 0 500" 0.9; d "mid-$k"; w "hands swing $HOLD $HX -45 $REACH 0 0 0 500" 0.9
    else w "hands swing $HOLD $HX -45 $REACH 0 0 0 1000" 1.5; fi
    echo "$(date +%T) pull $k ($HOLD): through starts $(( $(cnt 'through: the pull blocked') - n0 )), the root now $(root | grep -oE '[0-9.]+,[0-9.]+,[0-9.-]+' | head -1)"
    [ $k -le 5 ] && d "pull-$k" # the slab's zone: both eyes after each of the first pulls
    w "hands test $O $OX 35 $REACH 0 0 0" 0.5
    if ! grabh $O 35 $REACH; then # over a top's edge the hand finds air: lower, onto the edge (as a player reaches)
      GOT=; for y in 10 -15; do w "hands squeeze $O 0" 0.3; w "hands test $O $OX $y $REACH 0 0 0" 0.5
        grabh $O $y $REACH && { GOT=$y; break; }; done
      [ -n "$GOT" ] || { echo "$(date +%T) no grab $O"; fl 3; secs 1; break; }
      echo "$(date +%T) the $O hand held lower ($GOT)"; fi
    w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX -40 10 0 0 0" 0.4
    TY=$(grep -E "^CLIMB t=.* grab $O:" "$L" | tail -1 | grep -oE 'top [0-9]+' | head -1 | grep -oE '[0-9]+') # (a lip hold names its top twice)
    if [ -n "$TY" ] && [ "$TY" -ge "${CLIFF_TOP_MIN:-3520}" ]; then TOP=$O; break; fi # the cliff's top (not the shelf's)
    HOLD=$O
  done
  grep -E '^CLIMB t=.*(through|end:|top:|pullover)' "$L" | tail -12 | cut -c1-300
  echo "$(date +%T) after the face: $(root)"; m "c2-cliff-face"
  if [ -n "$TOP" ]; then
    [ "$TOP" = L ] && TX=-20 || TX=20
    echo "$(date +%T) the top held by $TOP: $(grep -E "^CLIMB t=.* grab $TOP:" "$L" | tail -1 | grep -oE 'edge [01][^;]*')"
    n0=$(cnt 'pullover armed')
    w "climb trace 10"
    w "hands swing $TOP $TX -90 40 0 0 0 1800" 2.4; secs 1
    for st in 1 2 3; do
      climbing || break
      w "hands swing $TOP $TX -90 -40 0 0 0 2000" 2.6; secs 1; [ $st = 1 ] && d "top-out-1"
      climbing && w "hands swing $TOP $TX -90 -90 0 0 0 1200" 1.6
      if [ "$(cnt 'pullover armed')" -gt "$n0" ]; then echo "$(date +%T) RING at stage $st"; d "cliff-ring"
        w "hands swing $TOP $TX -60 0 0 0 0 1000" 1.4; waitfor 'pullover: the pull done' 3 && echo "$(date +%T) PULLOVER pull"
        waitfor '^PRACTICE t=.*(arrived|did not arrive)' 10; sleep 2; break; fi
      climbing || break
      w "hands squeeze $TOP 0" 0.4; w "hands test $TOP $TX 0 40 0 0 0" 0.4; w "hands squeeze $TOP 1" 0.8
    done
  fi
  sleep 1.5; grep -E '^CLIMB t=.*(end:|top:|pullover|through)' "$L" | tail -5 | cut -c1-300
  echo "$(date +%T) result: $(grep -E '^CLIMB t=.*end:' "$L" | tail -1 | grep -oE 'end: [a-z]+') at $(root)"
  d "cliff-after"; m "c2-cliff-after"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 2; }
  w "stick 0 1 0 1.5" 2.2; echo "$(date +%T) walked: $(root)"; lastloco 1
  w "climb through on" 0.3; }
# slant (SOMVR_CLIMB_FACE_TOWARD; the word `climb face off|on`): on a continuous face (the bank, PIN_BANK_FROM) a hold
# with the right palm turned along the wall (roll 90, a few yaws; 40 cm right, 30 up), the left let go, the right
# pulled 70 cm: off = round 1's toward along the wall (a false edge, `top:` and `end: over` mid-face); on = the face's
# normal (no edge, the climb goes on)
slantatt() { local FO=$1 p n0 got= REACH=45
  echo "== slant, climb face $FO"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"; w "climb face $FO"
  go "${PIN_BANK_FROM:-31700 1830 -24825}"; faceto; probe
  for k in $(seq 1 12); do
    d0=$(ahead +0); [ -n "$d0" ] || break; [ "$d0" -le 60 ] && break
    w "stick 0 0.6 0 0.6" 1.0; probe
  done
  w "hands test L -20 0 $REACH 0 0 0" 0.5; grabh L 0 $REACH || { echo "no grab L"; return 1; }
  w "hands swing L -20 -40 $REACH 0 0 0 1000" 1.5
  for p in "0 30 90" "0 0 90" "0 -30 90" "0 50 90" "0 -50 90"; do
    w "hands squeeze R 0" 0.3; w "hands test R 40 30 30 $p" 0.5
    n0=$(cnt "^CLIMB t=.* grab R:"); w "hands squeeze R 1" 1.0
    if [ "$(cnt "^CLIMB t=.* grab R:")" -gt "$n0" ]; then
      if grep -E '^CLIMB t=.* grab R:' "$L" | tail -1 | grep -qE 'palm ray|fingers ray'; then got="$p"; break; fi
    fi
  done
  grep -E '^CLIMB t=.* grab R:' "$L" | tail -1 | grep -oE 'along \([^)]*\)|edge [01][^;]*' | tr '\n' ' '; echo " (pose ${got:-none})"
  w "hands squeeze L 0" 0.6; w "hands test L -20 -40 10 0 0 0" 0.4
  n0=$(cnt '^CLIMB t=.*end:')
  w "hands swing R 40 -40 30 ${got:-0 0 90} 1400" 2.0
  sleep 0.5; secs 1
  grep -E '^CLIMB t=.*(top:|end:)' "$L" | tail -2 | cut -c1-200
  if [ "$(cnt '^CLIMB t=.*end:')" -gt "$n0" ]; then echo "$(date +%T) slant $FO: ENDED ($(grep -E '^CLIMB t=.*end:' "$L" | tail -1 | grep -oE 'end: [a-z]+')) at $(root)"
  else echo "$(date +%T) slant $FO: still climbing at $(root)"; fi
  m "c2-slant-$FO"
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }
  w "climb face on"; }
slantplan() { slantatt off; slantatt on; slantatt off; slantatt on; }
[ -n "${CLIMB2_LIB:-}" ] && return 0 # sourced by top17-drive.sh for these functions
case " $PLAN " in *" trace "*|*" wallrun "*|*" cliff "*|*" cliff2 "*|*" top "*|*" tops "*|*" hang "*|*" unpin "*|*" noway "*|*" slant "*) ;;
  *) echo "no climb2 plan in PLAN=$PLAN"; exit 1 ;; esac
card
for p in $PLAN; do [ "$p" = trace ] && traceplan; [ "$p" = wallrun ] && wallrunplan; [ "$p" = lip ] && lipplan
  [ "$p" = stock ] && stockplan; [ "$p" = cliff ] && cliffplan; [ "$p" = cliff2 ] && cliffplan "$AT"; [ "$p" = top ] && topplan; [ "$p" = tops ] && topsplan
  [ "$p" = hang ] && hangplan; [ "$p" = unpin ] && unpinplan; [ "$p" = slant ] && slantplan
  [ "$p" = noway ] && { w "climb through off"; noway; w "climb through on"; }; done
echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
