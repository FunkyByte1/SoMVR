#!/bin/bash
# top17-drive.sh - build 17 (the climb17 helper): the user's build 16 session (logs/boot-check-20261027-090356) marks 3-5
# and their "keeps pushing me away from the wall". pin16-drive.sh's and climb2-drive.sh's functions (sourced), plans:
#   head4  mark 4's spot (H4_AT, the user's root by the platform's face): the test head moved 80 cm ahead into the face
#          with `climb headrock off` (the view in the rock, as the session), then on (the fade, black, back to Talion's
#          eyes), the head back; both eyes at each step
#   stand5 mark 5's platform (S5_FROM on the meadow under its face): hand over hand up the face to a hold on its top
#          (S5_TOP_MIN), the push over it; the let go at the top (SOMVR_CLIMB_STAND: he stands), the stick walks on
#   hug    the platform's face (S5_FROM): one pull off the ground, the hand pushed 30 cm toward the wall with `climb hug
#          off` then on (the root's move away from the face), then a fling (the face after it)
#   cliff  climb2-drive.sh's cliff from the user's hang spot to the top (SOMVR_CLIMB_STAND and HUG on): the top-out
#   follow4 mark 4's climb with the test head 70 cm off Talion, let go: SOMVR_FOLLOW brings his body under the head;
#          then an infantry orc's strikes (the THREAT reach lines: the head against the root)
#   orc17  an orc's body under the right test hand's grip: `climb orcbody hold` (build 16) vs skip (build 17)
# Several in one boot (PLAN="head4 stand5 hug"). Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
H4_AT=${H4_AT:-33985.5 2016.6 -27131.8}
S5_FROM=${S5_FROM:-33960 2017 -27110}
S5_TOP_MIN=${S5_TOP_MIN:-2330}
hr() { grep -E '^HEADROCK t=' "$L" | tail -${1:-3} | cut -c1-300; }
hugs() { grep -E '^CLIMB t=.*last 1 s' "$L" | tail -${1:-2} | grep -oE '(the root [0-9.]+ cm \(now [^)]*\)|hug: .*)' | tr '\n' ' '; echo; }
stands() { grep -E '^CLIMB t=.* (stand:|end:)' "$L" | tail -${1:-3} | cut -c1-320; }
head4plan() {
  echo "== mark 4: the head 80 cm ahead into the face at $H4_AT"
  x "btn R squeeze 0"; w "hands test off"
  go "$H4_AT"; echo "$(date +%T) at: $(root)"; faceto
  w "climb probe up" 1.2; grep '^CLIMB probe' "$L" | tail -1 | cut -c1-300
  d "h4-start"
  for mode in off on; do
    w "climb headrock $mode"
    x "head 0 0 -40 s 1.2"; sleep 1.6; d "h4-$mode-40"
    x "head 0 0 -80 s 1.0"; sleep 0.3; d "h4-$mode-80a"; sleep 1.2; d "h4-$mode-80b"; hr 3
    sleep 1; d "h4-$mode-80c"; m "t17-h4-$mode"
    x "head 0 0 0 s 1.2"; sleep 2; d "h4-$mode-back"; hr 2
  done
  x "head 0 0 0"; sleep 0.5; }
s5climb() { local k HOLD HX O OX TOP= REACH=40 TY
  w "hands test L -20 0 $REACH 0 0 0"; w "hands test R 20 35 $REACH 0 0 0" 1
  grabh R 35 $REACH || { echo "no grab R"; return 1; }
  HOLD=R
  for k in $(seq 1 8); do
    climbing || { echo "$(date +%T) not climbing"; fl 3; return 1; }
    [ "$HOLD" = L ] && { HX=-20; OX=20; O=R; } || { HX=20; OX=-20; O=L; }
    w "hands swing $HOLD $HX -45 $REACH 0 0 0 1000" 1.5
    echo "$(date +%T) pull $k ($HOLD): $(root)"; hugs 1
    w "hands test $O $OX 35 $REACH 0 0 0" 0.5
    GOT=35
    if ! grabh $O 35 $REACH; then GOT=; for y in 10 -15; do w "hands squeeze $O 0" 0.3; w "hands test $O $OX $y $REACH 0 0 0" 0.5
        grabh $O $y $REACH && { GOT=$y; break; }; done
      [ -n "$GOT" ] || { echo "$(date +%T) no grab $O"; fl 3; return 1; }; fi
    w "hands squeeze $HOLD 0" 0.6; w "hands test $HOLD $HX -40 10 0 0 0" 0.4
    TY=$(grep -E "^CLIMB t=.* grab $O:" "$L" | tail -1 | grep -oE 'top [0-9]+' | head -1 | grep -oE '[0-9]+')
    if [ -n "$TY" ] && [ "$TY" -ge "$S5_TOP_MIN" ]; then TOP=$O; TOPHY=$GOT; break; fi
    HOLD=$O
  done
  [ -n "$TOP" ] || { echo "$(date +%T) no top"; return 1; }
  echo "$(date +%T) the top held by $TOP: $(grep -E "^CLIMB t=.* grab $TOP:" "$L" | tail -1 | grep -oE 'edge [01][^;]*')"
  TOPH=$TOP; TOPY=$TY; return 0; }
# the push over the top: the top hand pushed down and back (the body up and in), re-gripped on the top each stage, until
# the climb ends (SOMVR_CLIMB_STAND: over) or the feet reach the top (then the let go: STAND = he stands); the stick
y_of() { root | grep -oE 'root [0-9.-]+,[0-9.-]+' | cut -d, -f2 | cut -d. -f1; }
s5top() { local st TX OX O y hy oy
  w "climb trace 10"
  hy=${TOPHY:-35}
  for st in 1 2 3 4 5; do
    climbing || break
    [ "$TOPH" = L ] && { TX=-20; OX=20; O=R; } || { TX=20; OX=-20; O=L; }
    w "hands swing $TOPH $TX $((hy - 40)) 15 0 0 0 1200" 1.7; echo "$(date +%T) push $st ($TOPH from $hy): $(root)"
    secs 1 | cut -c1-230
    climbing || break
    y=$(y_of); [ -n "$y" ] || break
    if [ $((y - 102)) -ge $((TOPY - 12)) ]; then echo "$(date +%T) the feet at the top (root $y, top $TOPY)"; break; fi
    # the other hand onto the top surface (5 cm over it, 45 ahead), then this one lets go: hand over hand over the edge
    oy=$((TOPY + 5 - y - 72)); [ $oy -lt -110 ] && oy=-110; [ $oy -gt 20 ] && oy=20
    w "hands test $O $OX $oy 45 0 0 0" 0.4
    grabh $O $oy 45 || { echo "$(date +%T) no grip on the top at $oy"; break; }
    w "hands squeeze $TOPH 0" 0.5; w "hands test $TOPH $TX -40 10 0 0 0" 0.3
    TOPH=$O; hy=$oy
  done
  d "s5-pushed"; m "t17-s5-pushed"; stands 3
  if climbing; then
    w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.8; stands 3; d "s5-letgo"; echo "$(date +%T) let go: $(root)"
    if climbing; then w "stick 0 1 0 1.5" 3.5; echo "$(date +%T) the stick in the hang: $(root)"; stands 3; fi
  fi
  w "stick 0 1 0 1.5" 3.5; echo "$(date +%T) the stick on foot: $(root)"; lastloco 1; d "s5-walked"; m "t17-s5-walked"; }
stand5plan() {
  echo "== mark 5: the platform's face from $S5_FROM"
  x "btn R squeeze 0"; w "hands squeeze L 0"; w "hands squeeze R 0"; w "hands test off"
  go "$S5_FROM"; echo "$(date +%T) at: $(root)"; faceto; d "s5-start"
  s5climb && s5top
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "hands test off" 0.5
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }; }
# hug5: on the platform's face (S5_FROM) one pull off the ground, then the hand 30 cm toward the wall (`climb hug off`,
# then on): the root's move out; then a fling (the last hold let go while its hand pulls fast): the face after it
lvl() { root | grep -oE 'root [0-9.-]+,[0-9.-]+,[0-9.-]+' | cut -c6-; }
hugatt() { local mode=$1 a b
  w "climb hug $mode"
  w "hands test R 20 35 40 0 0 0" 0.5; grabh R 35 40 || { echo "no grab"; return 1; }
  w "hands swing R 20 -10 40 0 0 0 900" 1.4; a=$(lvl); echo "$(date +%T) hug $mode, one pull up: $a"
  w "hands swing R 20 -10 70 0 0 0 800" 1.4; b=$(lvl); echo "$(date +%T) hug $mode, the hand 30 cm toward the wall: $b"; hugs 1
  echo "HUGAB $mode from $a to $b"
  d "hug-$mode"; m "t17-hug-$mode"
  w "hands swing R 20 -10 40 0 0 0 800" 1.2
  w "hands test L -20 35 40 0 0 0" 0.4; grabh L 35 40 || { echo "no grab L"; return 1; }
  w "hands squeeze R 0" 0.6; a=$(lvl)
  w "hands swing L -20 -45 30 0 0 0 300" 0.25; w "hands squeeze L 0" 1.5
  fl 4; echo "$(date +%T) hug $mode, after a fling: $(lvl) (from $a)"; hugs 1; grep -E '^CLIMB t=.*fling' "$L" | tail -3 | cut -c1-260
  w "hands test off" 0.4
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }; }
hugplan() {
  echo "== hug on the platform's face from $S5_FROM"
  x "btn R squeeze 0"; w "hands test off"
  for mode in off on; do go "$S5_FROM"; echo "$(date +%T) at: $(root)"; faceto; hugatt $mode; done
  w "climb hug on"; }
# follow4: mark 4's climb with the tracked head moved off Talion (the room's drift: the test head 70 cm back and left
# while climbing), let go: SOMVR_FOLLOW brings his body under the head (the view does not move); then an infantry orc's
# strikes: the THREAT reach lines' head against the root (the session: the head 34-55 cm behind his root)
hoff() { grep -E '^AUDIO .*head pos=' "$L" | tail -1 | sed -E 's/.*game_pos=([-0-9.]+),[-0-9.]+,([-0-9.]+) -> head pos=([-0-9.]+),[-0-9.]+,([-0-9.]+).*/\1 \2 \3 \4/' |
  awk '{dx=$3-$1; dz=$4-$2; printf "the head %.0f cm from the root (level)\n", sqrt(dx*dx+dz*dz)}'; }
BEHIND="hands test R 22 -35 -30 0 180 0"
cface() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$L" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
follow4plan() { local i R0 miss=0
  echo "== mark 4's climb, the head off Talion, the follow, an orc's strikes"
  x "btn R squeeze 0"; w "hands test off"
  go "$H4_AT"; echo "$(date +%T) at: $(root)"; faceto; echo "$(date +%T) $(hoff)"
  w "hands test R 20 35 40 0 0 0" 0.5; grabh R 35 40 || { echo "no grab"; return 1; }
  w "hands swing R 20 -10 40 0 0 0 900" 1.4; echo "$(date +%T) one pull up: $(root)"
  x "head -40 0 55 s 1.5"; sleep 2.5; echo "$(date +%T) climbing, the test head 68 cm off: $(hoff)"; d "f4-climbing"
  w "hands squeeze R 0" 1.0; fl 3
  climbing && { x "btn R a 1"; x "btn R a 0"; sleep 3; }
  sleep 1; echo "$(date +%T) 1 s after the climb: $(hoff)"; sleep 2; echo "$(date +%T) 3 s after: $(hoff)"
  grep -E '^FOLLOW t=' "$L" | tail -3 | cut -c1-260; d "f4-after"; m "t17-f4-after"
  w "practice spawn infantry" 1; waitfor "^PRACTICE t=.* spawn check 1" 6; sleep 1.5
  w "$BEHIND" 0.5; w "hands test L -25 -35 30 0 0 0" 0.5
  R0=$(cnt '^THREAT reach'); i=0
  while [ $i -lt 4 ] && [ $miss -lt 4 ]; do
    if waitfor '^THREAT reach' 10; then i=$((i + 1)); cface; else miss=$((miss + 1)); cface; fi
  done
  echo "$(date +%T) REACH $(( $(cnt '^THREAT reach') - R0 )) lines; $(hoff)"
  grep -E '^THREAT reach' "$L" | tail -n 4 | sed -E 's/^THREAT reach orc=\S+ //' | cut -c1-300
  grep -E '^FOLLOW t=' "$L" | tail -2 | cut -c1-260
  w "practice remove" 1; x "head 0 0 0 s 1"; sleep 1.5; w "hands test off" 0.4; }
# orc17: an infantry orc on the practice meadow; the right test hand pressed into its body at four spots with `climb
# orcbody hold` (build 16: its body may be a hold), then `skip` (build 17): the grab / press lines, a climb or none
orcatt() { local mode=$1 t g0 n=0 i k=0
  w "climb orcbody $mode"; w "climb fsm7 allow" # (a test aid: the grip may start a climb while the orc is engaged)
  go "32500 2100 -24800"; echo "$(date +%T) at: $(root)"
  w "practice spawn infantry" 0.3
  # while it walks in (the FSM still 6: a new climb allowed), grips pressed into it every 0.5 s until it engages
  for i in $(seq 1 28); do
    t="$(( (i % 3) * 8 + 5 )) $(( -15 - (i % 4) * 8 )) $(( 50 + (i % 2) * 15 ))"
    g0=$(cnt '^CLIMB t=.* start:')
    w "hands squeeze R 0" 0.15; w "hands test R $t 0 0 0" 0.15; w "hands squeeze R 1" 0.35
    l=$(grep -E '^CLIMB t=.* (grab R:|press R:)' "$L" | tail -1)
    case "$l" in *"orc's body"*|*"grab R"*) k=$((k + 1)); echo "$(date +%T) $mode at $t: $(echo "$l" | cut -c1-300)";; esac
    [ "$(cnt '^CLIMB t=.* start:')" -gt "$g0" ] && { n=$((n + 1)); echo "CLIMB STARTED ($mode)"; secs 1 | cut -c1-200
      w "hands squeeze R 0" 0.3; x "btn R a 1"; x "btn R a 0"; sleep 2; }
    cface
  done
  w "hands squeeze R 0" 0.4; climbing && { x "btn R a 1"; x "btn R a 0"; sleep 2; }
  echo "ORCAB $mode: presses on its body $k, climbs started $n"
  w "practice remove" 1; sleep 2; }
orc17plan() {
  echo "== orc17: grips on an orc's body"
  x "btn R squeeze 0"; w "hands test off"; w "hands test L -25 -60 10 0 0 0" 0.4
  orcatt hold; orcatt skip
  grep -E 'rays on an orc' "$L" | tail -1 | cut -c1-200
  w "climb fsm7 block"; w "hands test off" 0.4; }
case " $PLAN " in *" head4 "*|*" stand5 "*|*" hug "*|*" cliff "*|*" follow4 "*|*" orc17 "*) ;; *) echo "no top17 plan in PLAN=$PLAN"; exit 1 ;; esac
card
for p in $PLAN; do [ "$p" = head4 ] && head4plan; [ "$p" = stand5 ] && stand5plan; [ "$p" = hug ] && hugplan
  [ "$p" = cliff ] && cliffplan; [ "$p" = follow4 ] && follow4plan; [ "$p" = orc17 ] && orc17plan; done
echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
