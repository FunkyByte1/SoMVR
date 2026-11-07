# nurn41/lib.sh - som-b41-nurn (GAME-STUDY 1.8: the user's Núrn story path in VR, mission by mission): the helpers the
# mission directors (nurn41/m*.sh) and the ctl queue of scripts/nurn41-drive.sh use. Sourced with R = the run.
# Two kinds of act, kept apart in the log (`NU act` = what a VR player does: the test hands' poses and buttons; `NU aid`
# = the harness standing in for a person's travel or skill: teleports, health, the game's own invisibility):
#   VR acts: tap, cut, thrust, brand, rally, grip/ungrip, reach, crouch, look, route (the stick + the sprint latch + A at
#            the edges, story2_route.py), stalk (the left stick steered at an orc)
#   aids:    go (practice go), behind (verbs behind), hp (fight health), invis (story invisible), press (verbs press)
L=$R/somvr.log
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
gone() { grep -qE '^(.* )?(program exit|the hold .* is over|the game was stopped)' "$R/summary.txt" 2>/dev/null; }
cnt() { local c; c=$(grep -caE "$1" "$L" 2>/dev/null); echo "${c:-0}"; }
lines() { wc -l < "$L"; }
since() { tail -n +"$(( $1 + 1 ))" "$L" | grep -aE "$2"; }
# a word taken for sure: written, its TRIGGER line waited for (4 s at most), then the pause
w() { userin && bail "the user is in VR"; local a0 i; a0=$(cnt '^TRIGGER cmd='); printf '%s' "$1" > "$R/somvr.trigger"
  for i in $(seq 1 40); do [ "$(cnt '^TRIGGER cmd=')" -gt "$a0" ] && break; sleep 0.1; done
  echo "$(date +%T) word: $1"; sleep "${2:-0.45}"; }
x() { userin && bail "the user is in VR"; { echo "# $(date +%s%N)"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" &&
      mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${TS:-0.6}"; }
tap() { x "btn $1 $2 1"; sleep "${3:-0.3}"; TS=0.4 x "btn $1 $2 0"; echo "$(date +%T) NU act: tap $1 $2 ${3:-0.3}"; }
hold() { x "btn $1 $2 1"; echo "$(date +%T) NU act: hold $1 $2"; }
release() { x "btn $1 $2 0"; echo "$(date +%T) NU act: release $1 $2"; }
state() { grep -aE '^SESSION ui ' "$L" | tail -1 | cut -c12-80; }
win() { local a b; a=$(grep -naE 'window 0 -> 1' "$L" | tail -1 | cut -d: -f1); b=$(grep -naE 'window 1 -> 0' "$L" | tail -1 | cut -d: -f1)
        [ "${a:-0}" -gt "${b:-0}" ]; }
inworld() { state | grep -q 'paused=0 video=0 ctx=world'; }
shot() { echo "shot st-$1" >> "$R/cmd"; sleep "${2:-1.0}"; }
pics() { TS=0.1 x dump; sleep 0.5; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
         echo "$1 $f" >> "$R/nu-pics.txt"; shot "$1"; echo "$(date +%T) pics $1 (frame $f; $(state))"; }
mark() { w "mark $1" 3; }
z() { local i; for i in $(seq 1 "$1"); do userin && bail "the user is in VR"; gone && bail "the game is gone"; sleep 1; done; }
# the log lines a player's moment leaves (a compact view of what happened since line N)
NU_RE='^(BODYMOMENT t=.*window|SESSION ui|WARN|OBJECTIVE t=.*(shown by the game|marker [0-9]+ m=)|PRACTICE t=.*arrived|HINTS (tutorial|show)|USE t=|STORY t=.*subtitle ch=[01] |PROMPTS show|VERBS t=.*(button|rally|Talion attack|no brand|offers)|STRUGGLE|RIDE t=.*(mount|dom|seat)|CAPTAIN t=.*(intro|last words|finisher|dominat)|KILLS kill|DEATH|GRAB)'
moments() { since "$1" "${2:-$NU_RE}" | cut -c1-230 | head -"${3:-30}"; }
waitn() { local n0=$2 i; for i in $(seq 1 $(( ${3:-10} * 4 ))); do userin && bail "the user is in VR"
  [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
waitre() { local l0=$(lines) i; for i in $(seq 1 $(( ${2:-60} * 2 ))); do userin && bail "the user is in VR"; gone && return 1
  since "$l0" "$1" | grep -q . && { since "$l0" "$1" | head -1 | cut -c1-220; return 0; }; sleep 0.5; done; echo "(no '$1' in ${2:-60} s)"; return 1; }
status() { local n0=$(lines); w "objective status" 1.5 >&2; w "practice status" 1 >&2
  since "$n0" "^OBJECTIVE status|^PRACTICE t=.*status" | cut -c1-230; }
markers() { # markers RE: the nav markers named RE, "x y z dist" by distance
  local n0=$(lines); w "practice status nav" 2 >&2
  since "$n0" "^PRACTICE nav [0-9]+ '$1'" | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\), [0-9.]+ m' | sed -E 's/at \(//; s/\),//; s/ m$//' | sort -k4 -n; }
talion() { local n0=$(lines); w "practice status" 1 >&2; since "$n0" '^PRACTICE t=.*status' | grep -oE 'Talion [^(]*at \(-?[0-9.]+ -?[0-9.]+ -?[0-9.]+' | sed -E 's/.*\(//' | head -1; }
# ---- aids (logged as `NU aid`) ----
go() { local a0 i; a0=$(cnt 'arrived at'); w "practice go $1 $2 $3" 1; echo "$(date +%T) NU aid: go $1 $2 $3"
  for i in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done; sleep 1.5; }
behind() { w "verbs behind $1${2:+ $2 $3}" 1.2; echo "$(date +%T) NU aid: behind $*"; }
hp() { w "fight health $*" 0.6; echo "$(date +%T) NU aid: fight health $*"; }
invis() { w "story invisible $1" 0.5; echo "$(date +%T) NU aid: story invisible $1"; }
press() { w "verbs press $1 ${2:-150}" 1.0; echo "$(date +%T) NU aid: the game's command $1 pressed (no VR act)"; }
# orcs X Z RR: "ptr x y z facing" of every orc within RR cm of X,Z (verbs orcs)
orcs() { local n0=$(lines); w "verbs orcs $1 $2 ${3:-1500}" 1.2 >&2
  since "$n0" '^VERBS orcs t=.* [0-9]+/' | sed -nE 's/.* orc (0x[0-9a-f]+) at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) facing (-?[0-9.]+) deg.*/\1 \2 \3 \4 \5/p'; }
# ---- VR acts: the hands (the poses of scripts/deaths36-drive.sh: OpenXR grip, -Z pinky -> thumb) ----
REST="hands test R 28 -40 20 0 0 0"; LREST="hands test L -22 -35 25 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
RZ=90; EDGE_A="hands test R 55 -8 30 0 -90 $RZ"; EDGE="hands swing R -35 -18 62 0 90 $RZ 110"
PRE="hands test R 20 -27 -18 12 0 0"; THRUST="hands swing R 5 -11 54 12 0 0 180"
UP="hands test R 30 25 0 80 -40 0"; UPCUT="hands swing R -15 -20 55 -20 30 0 160"
hands0() { x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
  grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
  w "$LREST" 0.4; w "$REST" 0.4; }
drawn() { grep -aE '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1 | grep -qE 'in the hand|our keep undone'; }   # (som-b42-nurn: 'our keep undone (the game drew it)' = in the hand: draw() read it as sheathed and reached for the slot - 003146)
draw() { drawn && return 0; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; w "$REST" 0.3
  drawn || { sleep 1; w "$SLOT" 0.7; w "hands squeeze R 1" 0.8; w "$OUT" 0.8; w "hands squeeze R 0" 0.5; w "$REST" 0.3; }   # (a second reach)
  echo "$(date +%T) NU act: the sword drawn over the shoulder ($(drawn && echo in the hand || echo NOT drawn))"; }
slash() { w "hands test R 40 -8 35 0 -90 90" 0.3; w "hands swing R -20 -18 62 0 90 90 300" 0.6; w "hands swing R 40 -8 35 0 -90 90 300" 0.5; w "$REST" 0.3; echo "$(date +%T) NU act: a cut (right to left, 300 ms: a 110 ms swing spans 2 frames at 20 Hz and only glances)"; }
thrust() { w "$PRE" 0.3; w "$THRUST" 0.5; w "$REST" 0.3; echo "$(date +%T) NU act: a thrust"; }
upcut() { w "$UP" 0.4; w "$UPCUT" 0.5; w "$REST" 0.3; echo "$(date +%T) NU act: a cut from over the shoulder"; }
crouch() { w "crouch hold ${1:-10000}" 0.3; echo "$(date +%T) NU act: crouched (the head low, ${1:-10000} ms)"; }
look() { x "head 0 0 0 yaw ${1:-0} pitch ${2:-0} s 0.3"; }
# the stealth brand: crouched, the left palm at the orc's collar (verbs aim head), the left trigger
brand() { crouch; w "verbs aim head" 0.3; tap L trigger 0.2; sleep 2; w "verbs aim off" 0.2
  echo "$(date +%T) NU act: the brand (the left palm at its head + the trigger)"; }
# the rally / activate the branded: the left hand raised 30 cm over the eyes + the left trigger
rally() { w "hands test L -10 30 15 0 0 0" 0.6; tap L trigger 0.3; sleep 1; w "$LREST" 0.3; echo "$(date +%T) NU act: the raised left hand + the trigger"; }
# the grab / the reach: the left palm at the head of the nearest (verbs aim head) + the left grip
grab() { w "verbs aim head" 0.4; hold L squeeze; sleep "${1:-1.5}"; echo "$(date +%T) NU act: the left palm at its head + the grip"; }
ungrab() { release L squeeze; w "verbs aim off" 0.2; w "$LREST" 0.3; }
# stalk X Z CM: the left stick steered at the orc nearest X,Z (crouched first by the caller), up to CM root to root
stalk() { w "verbs stalk $1 $2 ${3:-90}" 0.5; echo "$(date +%T) NU act: the stick steered at the orc near $1,$2"; }
# route "X,Z;X,Z" [SECS] [TAG]: the left stick + the sprint latch + A at the edges toward each point (story2_route.py)
route() { local s0=$(date +%s.%N); python3 "$(dirname "${BASH_SOURCE[0]}")/../story2_route.py" "$R" --secs "${2:-100}" --tag "${3:-nu}" --waypoints "$1" ${4:-} | tail -4
  echo "$(date +%T) NU act: walked $(echo "$(date +%s.%N) - $s0" | bc | cut -c1-6) s"; }
# kill1 X Z [CM]: the orc nearest X,Z: Talion CM (110) behind it (aid), its health 1 (aid), then the real cut and a thrust
kill1() { behind "${3:-110}" "$1" "$2"; w "execute face" 0.4; hp 1; slash; thrust; }
# endphase: videos (A, B), paused pages (A), windows (A then B) until quiet for 6 rounds
endphase() { local q=0 n=0 T=$((SECONDS + ${1:-8} * 60)) s
  while [ $SECONDS -lt $T ]; do n=$((n + 1)); s=$(state); userin && bail "the user is in VR"; gone && return 1
    if echo "$s" | grep -q 'video=1'; then q=0; echo "$(date +%T) end $n video"; sleep 6; shot nu-end-v$n 0.5; tap R a 0.3; sleep 0.7; tap R b 0.3; sleep 2
    elif echo "$s" | grep -q 'paused=1'; then q=0; echo "$(date +%T) end $n paused"; sleep 2; pics nu-end-p$n; tap R a 0.4; sleep 2
    elif win; then q=0; echo "$(date +%T) end $n window"; pics nu-end-w$n; sleep 5; win && { tap R a 0.4; sleep 3; }; win && { tap R b 0.4; sleep 3; }
    else q=$((q + 1)); [ $q -ge 6 ] && { echo "$(date +%T) quiet after $n rounds"; return 0; }; sleep 4; fi
  done; echo "$(date +%T) end: not quiet in ${1:-8} min ($(state))"; }
# the mission's state at a glance: the objective, the lesson / prompt lines since N
obj() { since "${1:-0}" "^OBJECTIVE t=.*shown by the game|HINTS (tutorial|show)|PROMPTS show|Mission (Failed|Complete)|SUCCESS|Out of Time|Detected" | tail -"${2:-6}" | cut -c1-200; }
saveas() { local P; P=$(grep -m1 -oE 'save: a copy of .* as .*/GameData\.sav' "$R/summary.txt" 2>/dev/null | sed -E 's/.* as //')
  [ -f "$P" ] || { echo "no prefix save ($P)"; return 1; }; mkdir -p "$R/saves"; cp --no-preserve=mode "$P" "$R/saves/$1.sav"
  echo "$(date +%T) save $1: $(sha256sum "$R/saves/$1.sav" | cut -c1-16), written $(stat -c %y "$P" | cut -c1-19)"; }
# a tutorial the game paused for (SOMVR_HINTS_PAUSING passed it): both eyes' picture, its text, then A (its button)
PT=$(cnt 'passed \(SOMVR_HINTS_PAUSING\)')
ptut() { local n; n=$(cnt 'passed \(SOMVR_HINTS_PAUSING\)'); [ "$n" -gt "$PT" ] || return 0; PT=$n
  echo "$(date +%T) a pausing tutorial: $(grep -a 'passed (SOMVR_HINTS_PAUSING)' "$L" | tail -1 | cut -c1-200)"; sleep 2.5
  pics nu-ptut-$n; tap R a 0.3; sleep 2.5; echo "$(date +%T) after A: $(state)"; }
# capat: "x y z" of the world's captain now (`captain where`; empty if none)
capat() { local l0=$(lines); w "captain where" 0.8 >&2
  since "$l0" "^CAPTAIN t=.*captain's place \(asked\)" | tail -1 | sed -nE 's/.* m from Talion at (-?[0-9]+) (-?[0-9]+) (-?[0-9]+),.*/\1 \2 \3/p'; }
# brandloop RE DONE_RE [MAX]: the stealth brands at each marker RE until DONE_RE shows since the call: per marker the
# orc there - try 1 crouched 160 cm behind it (aid) + the stick's sneak to 80 cm (VR) + the brand; try 2 100 cm behind
# (aid) + the brand; try 3 in front 150 (aid) + the brand. `NU brand` lines: which try took.
BRRE="Talion attack 'PC_(Archer|Bip|[A-Za-z]+)_StealthBrand"
brandloop() { local RE=$1 DONE=$2 MAX=${3:-24} B0 b0 l0 l2 round pick p kk; declare -A TRIED; l0=$(lines); B0=$(cnt "$BRRE")
  for round in $(seq 1 "$MAX"); do ptut
    since "$l0" "$DONE" | grep -q . && { echo "$(date +%T) brandloop: '$DONE' is up"; break; }
    win && { echo "$(date +%T) brandloop: a window ($(obj "$l0" 2 | tr '\n' '|'))"; break; }
    mapfile -t P < <(markers "$RE")
    pick=""; for p in "${P[@]}"; do set -- $p; kk="$(( $1 / 200 ))_$(( $3 / 200 ))"; [ "${TRIED[$kk]:-0}" -ge 3 ] && continue; pick=$p; break; done
    echo "$(date +%T) brands round $round: ${#P[@]} markers, pick ${pick:-none}, brands $(( $(cnt "$BRRE") - B0 ))"
    [ -z "$pick" ] && { z 3; [ ${#P[@]} -gt 0 ] && TRIED=(); [ ${#P[@]} = 0 ] && [ $round -gt 3 ] && break; continue; }
    set -- $pick; kk="$(( $1 / 200 ))_$(( $3 / 200 ))"; TRIED[$kk]=$(( ${TRIED[$kk]:-0} + 1 )); b0=$(cnt "$BRRE"); l2=$(lines)
    case ${TRIED[$kk]} in
      1) crouch; behind 160 "$1" "$3"; stalk "$1" "$3" 80; z 4 ;;
      2) crouch; behind 100 "$1" "$3" ;;
      3) crouch; w "verbs front 150" 1.2; echo "$(date +%T) NU aid: in front 150" ;;
    esac
    brand; sleep 1
    if [ "$(cnt "$BRRE")" -gt "$b0" ]; then echo "$(date +%T) NU brand: took at try ${TRIED[$kk]}"; TRIED[$kk]=9
    else echo "$(date +%T) NU brand: none at try ${TRIED[$kk]}: $(since "$l2" 'no brand offered|StealthBrand -> none|STEALTH t=' | tail -1 | cut -c1-200)"; fi
  done; echo "$(date +%T) brandloop: $(( $(cnt "$BRRE") - B0 )) brands"; }
