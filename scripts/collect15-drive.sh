#!/bin/bash
# collect15-drive.sh - the open world's interactions a VR player meets (handoff/HANDOFF-collect.md, docs/re-xr-pipeline.md
# 45): artifacts (and their inspect view), Ithildin glyphs, quest givers, slaves, doors and gates - reached on foot as a
# player would and tried with our buttons. One headless boot (the save's own place), run as
#   HEADLESS_HOLD=1500 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build15.env | tr '\n' ' ')
#     SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217" setsid -w scripts/headless_vr.sh <wt>/ShadowOfMordor/mod/somvr
# with this script at the same time (a copy of it: never edit a running bash script). The places are the game's own
# nav markers (the probe's navdump, logs/boot-check-20261020-001117/probe-out/nav-4.tsv, and `practice status nav` live).
# At each place (DC_PLACES, NAME:X:Y:Z:KIND[:STOP], KIND artifact|glyph|giver|npc|any, STOP the walk's stop in m):
#   1. the landing: collect15_land.py probes the floors (`practice status ground X Y Z`, a ray down) and picks open ground
#      2.5-6 m from the place at the place's floor (no roof, no ledge: the trip lands on the ground, never a fall);
#   2. `practice go` there (the trip's blink), `practice status nav` (what is near), the eyes and the flat picture;
#   3. collect15_walk.py walks up to the place (the stick, the test head turned toward it) until a prompt shows (a
#      `PROMPTS show` / `USE ... pick prompt up` line) or DC_STOP m (1.0), then looks at it (the head pitched to it); an
#      artifact or a glyph: the wraith vision (Y: `btn L b`) pressed there and held through B (their prompt shows
#      only in it), up to 10 s for the prompt;
#   4. the prompt's line, B tapped, then held 2.5 s, the result watched (the eyes, the flat picture, the moments);
#   5. a menu that opens (an artifact's inspect view: `ctx=menu`): the sticks turned (each way 1.2 s), A, then B out.
# DC_TRY="CMD[:MS] ..." (e.g. "123:400 34:400"): the game's commands tried at each place after our buttons (`crouch
# try`: which command a place wants). DC_PLAN (words in order; default "nav places"): nav = `practice status nav` at the arrival; places = DC_PLACES in turn;
# giver = the resistance giver where the live nav dump has him (his marker walks); gates = the nav dump's LockedGate /
# NPC markers with a place, the nearest visited. DR_NOQUIT=1 leaves the boot running; DR_RUN=RUN drives a running boot.
# Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${DR_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
PLAN=${DC_PLAN:-nav places}; PLAN=${PLAN//_/ }
PLACES=${DC_PLACES:-art3:28596:5553:-33884:artifact gly3:57272:1919:-10603:glyph art2:34776:2448:-27463:artifact gly1:34345:3900:-19589:glyph}
T0=$(date +%s)
R=${DR_RUN:-}
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
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
ARR='^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world'
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  [ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge 2 ] && break; sleep 3
done
[ "$(grep -cE "$ARR" "$R/somvr.log" 2>/dev/null)" -ge 2 ] || bail "no arrival"
echo "arrived $(date +%T)"
sleep 6
N=0; S=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
d() { t "dump"; echo "shot dc-$1" >> "$R/cmd"; echo "$(date +%T) DUMP $1 ($(state))"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
# tap HAND BUTTON [SECONDS]: pressed and let go (the file written directly: t's 0.6 s wait would make a tap a long press)
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        echo "$(date +%T) tap $1 $2 ${3:-0.3}"; sleep 0.5; }
inworld() { state | grep -q 'ctx=world'; }
inmenu() { state | grep -q 'ctx=menu'; }
step() { S=$((S + 1)); echo "$(date +%T) STEP $S $*"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
moments() { tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT t|SESSION ui|PROMPTS (show|hold|hide)|USE t|WARN|FBSITE|FALLBACK t|PRACTICE t=.*(arrived|waits|game is back|ground)|HINTS|MENUPASS|VRMENU .*card)' |
  sed -E 's/ key 0x[0-9a-f]+//; s/h4=[0-9]+ //' | cut -c1-210 | head -${2:-40}
  grep -E 'xr: (theatre|loadspace|cover|space)' "$R/xr-layer.log" | tail -n +"${TH0:-1}" | cut -c1-200 | head -8; }
TH0=1
# placep LINE: a place's prompt since LINE (not the fight's or the wraith's: they come and go everywhere)
IGN="'(UI_Wraith_|UI_Ranged_|Domination_|Combat_Grab|UI_Combat_|UI_Attract|Combat_Struggle|UI_Prompt_Interrogate)"
placep() { tail -n +"$1" "$R/somvr.log" | grep -E '^PROMPTS show|^USE t=.* pick prompt up' | grep -qvE "$IGN"; }
LOW="hand L head -25 -35 30 0 0 0"
REST="hands test R 28 -40 20 0 0 0"
# 0. the controls card away, the left hand low, the right hand at rest
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"; t "$LOW"; w "$REST" 2
d start 1
L0=$(wc -l < "$R/somvr.log")
# nav: the live nav dump (the markers with a place now) into RUN/nav-N.txt
NV=0
nav() { NV=$((NV + 1)); local n0=$(wc -l < "$R/somvr.log"); w "practice status nav" 2.5
  tail -n +"$n0" "$R/somvr.log" | grep -E "^PRACTICE nav " > "$R/nav-$NV.txt"
  echo "$(date +%T) nav $NV: $(wc -l < "$R/nav-$NV.txt") markers logged;" \
    "$(grep -vE "'(Collectible_Flower|InfantryNormal|ArcherNormal|RessurrectionStone)" "$R/nav-$NV.txt" | cut -c14-90 | head -14 | tr '\n' ';')"; }
# trip X Y Z: the trip's blink there, its arrival (or not)
trip() { local a0=$(cnt 'arrived at') f0=$(cnt 'did not arrive'); TH0=$(($(wc -l < "$R/xr-layer.log") + 1))
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170; sleep 2.5; }
# visit NAME X Y Z KIND: the landing, the trip, the walk up, the prompt, B, the result, a menu
visit() { local nm=$1 x=$2 y=$3 z=$4 kind=$5 stop=${6:-${DC_STOP:-1.0}} ml land
  step "visit $nm ($kind) at $x $y $z"
  land=$(python3 "$HERE/collect15_land.py" "$R" "$x" "$y" "$z" | tee "$R/land-$nm.txt" | tail -1)
  grep -E '^PRACTICE|floor' "$R/land-$nm.txt" | cut -c1-230 | head -3; echo "$land"
  if [ "${land%% *}" != LAND ]; then
    # not loaded from here (or no open spot): a first hop over the place's floor-to-be, high (Teleport snaps down; the
    # trip's 3 m margin = a WARN if the ground is lower: only when DC_HOP=1)
    [ "${DC_HOP:-0}" = 1 ] || { echo "$(date +%T) $nm: no landing - skipped"; return; }
    trip "$x" $((y + 50)) "$z"
    land=$(python3 "$HERE/collect15_land.py" "$R" "$x" "$y" "$z" | tee "$R/land-$nm-2.txt" | tail -1); echo "$land"
    [ "${land%% *}" = LAND ] || { echo "$(date +%T) $nm: still no landing - skipped"; return; }
  fi
  set -- $land; trip "$2" "$3" "$4"
  nav; d "$nm-at" 0.5; m "$nm-at" 2
  ml=$(wc -l < "$R/somvr.log")
  [ "$kind" = glyph ] && [ "${DC_WRAITH:-fresh}" = walk ] && { t "btn L b 1"; sleep 1.5; d "$nm-wraith" 0.5; }
  python3 "$HERE/collect15_walk.py" "$R" "$x" "$z" --stop "$stop" --secs 35 --prompt --look "$y" | tail -9 || bail "walk"
  sleep 1
  if ! placep "$ml"; then
    # the last metre: rings of short steps (the place may want him facing it or nearer)
    for s in "0 1" "1 0" "0 -1" "-1 0" "0 1"; do w "stick $s 0 0.5" 1.2
      placep "$ml" && break; done
  fi
  # an artifact's and an Ithildin's prompt shows only in the wraith vision near it (run 20261026-135310: the artifact's
  # 'Combat_Activate_Collectable' ~3 s after Y was pressed there, gone at Y's release): Y pressed fresh here and held
  # (through B) while the prompt is waited for (DC_WRAITH=walk: held from the walk's start, round 1's way)
  if [ "$kind" = artifact ] || [ "$kind" = glyph ]; then
    [ "${DC_WRAITH:-fresh}" = fresh ] && { t "btn L b 0"; sleep 0.8; t "btn L b 1"; }
    for i in $(seq 1 10); do placep "$ml" && break; sleep 1; done
    echo "$(date +%T) $nm: the wraith vision held $i s: $(placep "$ml" && echo 'a prompt' || echo 'no prompt')"
    d "$nm-wraith-held" 0.5
  fi
  if [ "${DC_SWEEP:-1}" = 1 ] && ! placep "$ml"; then   # (DC_SWEEP=0: the head stays on the place, as the walk left it)
    # the head turned round (8 headings, 1 s each): a prompt that wants him facing the place
    for yw in 0 45 90 135 180 225 270 315; do t "head 0 0 0 yaw $yw s 0.3"; sleep 0.5; placep "$ml" && break; done
    placep "$ml" || t "head 0 0 0 yaw 0 s 0.3"
  fi

  echo "$(date +%T) $nm: the prompts since the walk:"; tail -n +"$ml" "$R/somvr.log" | grep -E '^PROMPTS (show|hide)|^USE t' |
    sed -E 's/ key 0x[0-9a-f]+//' | cut -c1-200 | awk '!s[$0]++' | head -8
  d "$nm-near" 0.5; m "$nm-near" 2
  ml=$(wc -l < "$R/somvr.log"); w0=$(cnt 'window 0 -> 1')
  tap R b 0.3; sleep 2; d "$nm-tap" 0.5
  if inworld; then t "btn R b 1"; sleep 2.5; t "btn R b 0"; sleep 1; d "$nm-hold" 0.5; fi
  for i in 1 2 3 4; do sleep 1.5; d "$nm-$i" 0.5; inmenu && break; done
  # DC_TRY="CMD[:MS] ...": the game's commands tried after our buttons (the crouch probe's `crouch try`, past the deny
  # list: which command the place really wants) while nothing opened; for a glyph with the wraith vision still held
  for c in ${DC_TRY:-}; do inworld || break; [ "$(cnt 'window 0 -> 1')" -gt "$w0" ] && break
    w "crouch try ${c%%:*} $([ "${c#*:}" != "$c" ] && echo "${c#*:}" || echo 400)" 2; d "$nm-try${c%%:*}" 0.5
    for i in 1 2 3; do sleep 1.5; inmenu && break; [ "$(cnt 'window 0 -> 1')" -gt "$w0" ] && break; done; d "$nm-try${c%%:*}-after" 0.5
  done
  if inmenu; then
    echo "$(date +%T) $nm: a menu ($(state)): the sticks, A, B"; m "$nm-menu" 2
    for s in "1 0" "-1 0" "0 1" "0 -1"; do t "stick L $s"; sleep 1.2; t "stick L 0 0"; done; d "$nm-turnL" 0.5
    for s in "1 0" "-1 0" "0 1" "0 -1"; do t "stick R $s"; sleep 1.2; t "stick R 0 0"; done; d "$nm-turnR" 0.5
    tap R a 0.4; sleep 3; d "$nm-a" 0.5; m "$nm-a" 2
    for i in 1 2 3; do d "$nm-mem$i" 2; done
    tap R b 0.4; sleep 3; d "$nm-b" 0.5
    inworld || { echo "still not the world: B again"; tap R b 0.4; sleep 3; d "$nm-b2" 0.5; }
    inworld || { echo "still not the world: the menu button"; tap L menu 0.3; sleep 3; d "$nm-b3" 0.5; }
  fi
  t "btn L b 0"; sleep 0.5   # (the wraith vision let go)
  echo "-- $nm: the moments since B:"; moments "$ml" 40
  w "practice status" 1; grep '^PRACTICE t=.* status' "$R/somvr.log" | tail -1 | grep -o 'Talion [^;]*' | head -1; }
navpos() { grep -E "'$1'" "$R/nav-$NV.txt" | head -1 | sed -E 's/.* at \((-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\).*/\1 \2 \3/'; }
for p in $PLAN; do
  L1=$(wc -l < "$R/somvr.log")
  case $p in
  nav) step "nav: the markers with a place at the arrival"; nav ;;
  places) for spot in $PLACES; do IFS=: read -r nm x y z kind stop <<< "$spot"; visit "$nm" "$x" "$y" "$z" "$kind" "$stop"; done ;;
  giver) step "giver: the resistance giver where the nav dump has him now"; nav; xyz=$(navpos resistance_QuestGiver)
    [ -n "$xyz" ] && { set -- $xyz; visit giver "$1" "$2" "$3" giver; } || echo "no giver in the nav dump" ;;
  gates) step "gates: a LockedGate / NPC marker with a place"; nav
    for k in LockedGate NPC; do xyz=$(navpos $k); [ -n "$xyz" ] && { set -- $xyz; visit "$k" "$1" "$2" "$3" any; } done ;;
  esac
done
echo "steps done $(date +%T)"
echo "---- the whole run's prompts:"; tail -n +"$L0" "$R/somvr.log" | grep -E '^PROMPTS show' | sed -E 's/^PROMPTS show #[0-9]+ t=[0-9.]+: //; s/ key 0x[0-9a-f]+//' | sort | uniq -c | head -20
[ "${DR_NOQUIT:-0}" = 1 ] && { echo "the boot left running (DR_NOQUIT=1)"; exit 0; }
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
