#!/bin/bash
# tether18-drive.sh - build 18 (the tether18 helper): the user's 17b session (logs/boot-check-20261027-174757) mark 6,
# the view off the plateau's edge (re-xr-pipeline.md 69). pin16-drive.sh's / climb2-drive.sh's functions (sourced). Plans:
#   edge6  mark 6's spot (E6_AT, the root at the mark), the view turned toward the drop (E6_DIR, where the session's view
#          went): the stick held into the edge with `tether off` + `tether measure old` (build 17: the view walks on
#          off the edge), `tether on` (the fade catches it, back at his eyes), `tether measure along` (the view stays);
#          then the test head stepped 100 cm off the edge (25 cm/s): the fade, black, back on the plateau; both eyes
#   flat   the practice meadow: the test head 100 cm ahead and back (the follow takes the body: no fade)
#   wall   mark 4's spot by the platform's face (H4_AT): the test head 80 cm into the face (HEAD_ROCK and the tether)
# Several in one boot (PLAN="edge6 flat wall"). Stops at once (quit) when the user goes into VR.
CLIMB2_LIB=1 . "$(dirname "$0")/climb2-drive.sh"
E6_AT=${E6_AT:-33791.8 3638.8 -27312.0}
E6_DIR=${E6_DIR:--0.87 0.50}
H4_AT=${H4_AT:-33985.5 2016.6 -27131.8}
tt() { grep -E '^TETHER t=' "$L" | tail -${1:-4} | cut -c1-300; }
fol() { grep -E '^FOLLOW t=' "$L" | tail -${1:-1} | grep -oE 'off=[^;]*\(([0-9.]+) cm\)|clipped [0-9]+ \(against the request [0-9]+'; }
fwd() { grep -E '^FRONT camcopy' "$L" | tail -1 | grep -oE 'the head [^ ]+ fwd [^ ]+' | sed -E 's/.*fwd ([-+0-9.]+),[-+0-9.]+,([-+0-9.]+)/\1 \2/'; }
ang() { local f; f=$(fwd); awk -v f="$f" -v t="$E6_DIR" 'BEGIN{split(f,a," ");split(t,b," ");
  printf "%d\n", atan2(a[1]*b[2]-a[2]*b[1], a[1]*b[1]+a[2]*b[2])*57.3}'; }
headoff() { grep -E '^FRONT camcopy' "$L" | tail -1 | grep -oE 'the head [-0-9.]+,[-0-9.]+,[-0-9.]+' | sed 's/the head //'; }
faceworld() { local i a a0 dir=+; a0=$(ang); echo "$(date +%T) the view $a0 deg from the drop's direction"
  for i in $(seq 1 12); do a=$(ang); [ "${a#-}" -lt 16 ] && break
    w "turn$dir" 1.2; local b; b=$(ang); [ "${b#-}" -gt "${a#-}" ] && { [ $dir = + ] && dir=- || dir=+; }; done
  echo "$(date +%T) faced: $(ang) deg off (fwd $(fwd))"; }
edge6plan() {
  echo "== mark 6: the plateau's edge at $E6_AT"
  x "btn R squeeze 0"; w "hands test off"; x "head 0 0 0"
  go "$E6_AT"; echo "$(date +%T) at: $(root)"; faceworld; d "e6-start"
  w "tether off"; w "tether measure old"
  w "stick 0 1 0 3" 3.6; echo "$(date +%T) OLD stick into the edge 3 s: $(root) view $(headoff) $(fol 1)"; d "e6-old"
  grep -E '^CLIMB t=.*edge cue' "$L" | tail -1 | cut -c1-200; m "t18-e6-old"
  w "tether on" 3.5; echo "$(date +%T) tether on (old measure's offset): $(fol 1)"; tt 4; d "e6-old-tether"
  w "tether measure along"
  w "stick 0 1 0 3" 3.6; echo "$(date +%T) ALONG stick into the edge 3 s: $(root) view $(headoff) $(fol 1)"; d "e6-along"
  sleep 1; echo "$(date +%T) the head off the edge (100 cm at 25 cm/s)"
  x "head 0 0 -100 s 4"; sleep 1.0; d "e6-step-a"; sleep 0.2; d "e6-step-b"; sleep 0.2; d "e6-step-c"
  sleep 1.5; d "e6-step-d"; tt 5; echo "$(date +%T) $(root) view $(headoff) $(fol 1)"; m "t18-e6-step"
  x "head 0 0 0 s 3"; sleep 4; echo "$(date +%T) the head back: $(root) $(fol 1)"; tt 2; d "e6-back"; }
flatplan() {
  echo "== flat ground: the head 100 cm ahead and back"
  x "head 0 0 0"; go "${FLAT_AT:-32500 2100 -24800}"; echo "$(date +%T) at: $(root)"
  local n0; n0=$(cnt '^TETHER t=.*fades')
  x "head 0 0 -100 s 2"; sleep 1.0; d "fl-mid"; sleep 2; echo "$(date +%T) 100 cm ahead: $(root) $(fol 1)"; d "fl-ahead"
  x "head 30 0 40 s 2"; sleep 3; echo "$(date +%T) back and right: $(root) $(fol 1)"
  x "head 0 0 0 s 2"; sleep 3; echo "FLAT tether fades $(( $(cnt '^TETHER t=.*fades') - n0 ))"; }
wallplan() {
  echo "== mark 4's face: the head 80 cm into it"
  x "head 0 0 0"; go "$H4_AT"; echo "$(date +%T) at: $(root)"; faceto
  local n0 h0; n0=$(cnt '^TETHER t=.*fades'); h0=$(cnt '^HEADROCK t=.*fades')
  x "head 0 0 -80 s 1.5"; sleep 1.2; d "w4-a"; sleep 1.3; d "w4-b"; sleep 1.5; d "w4-c"; hr 3; tt 3
  x "head 0 0 0 s 1.5"; sleep 2.5; d "w4-back"
  echo "WALL headrock fades $(( $(cnt '^HEADROCK t=.*fades') - h0 )), tether fades $(( $(cnt '^TETHER t=.*fades') - n0 ))"; }
case " $PLAN " in *" edge6 "*|*" flat "*|*" wall "*) ;; *) echo "no tether18 plan in PLAN=$PLAN"; exit 1 ;; esac
card
for p in $PLAN; do [ "$p" = edge6 ] && edge6plan; [ "$p" = flat ] && flatplan; [ "$p" = wall ] && wallplan; done
grep -E '^TETHER END|^FOLLOW t=' "$L" | tail -2 | cut -c1-300
echo "steps done $(date +%T)"; sleep 2; echo quit >> "$R/cmd"; echo "quit $(date +%T)"
