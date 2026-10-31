#!/bin/bash
# rehearsal28-drive.sh - build 28b (som-b28-rehearsal, docs/re-xr-pipeline.md 127, handoff/HANDOFF-rehearsal.md): the
# user's session rehearsed - the VR menu's Test page (mod/somvr/assets/what-to-test.txt) played through in its order in
# ONE headless boot, to find what only a sequence shows: a state one item leaves behind that breaks the next (a held
# view, a blink, a climb, a menu, the vignette, a grip, the bow left out, a body moment, a struggle), logs that stop,
# frame times that creep, a WARN. Each item is the driver that proved it alone (its own scripted real-hand poses, its
# own staging aids: an orc spawned, a place reached by `practice go`), run against this boot one after another; between
# items only the TEST TOOLS are put back (the test hands' squeezes, buttons, sticks, the test head, the test chase and
# guard, a spawned orc) - never the game's own state - so what the next item meets is what the last one left; then a
# state line (rh-state.txt), a both-eye dump, and whether the world is back (a screen left up = a finding).
# The drivers run as copies (scripts/.reh-NAME-drive.sh, made here, removed at the end) with every `echo quit >>
# "$R/cmd"` taken out - one driver's end or bail must not end the session; this script quits the boot itself (at the
# end, and at once when the user goes into VR: the current driver's process group is killed first).
# RH_PLAN = a (default): the launch's way in (the main menu, Continue, the practice meadow's arrival card) by hla27,
#   then FIGHTING and BOW in the Test page's order, the map, dying; b: the world - MOVING (the jump, the ruin's vault, a
#   forge tower up and down), BEASTS (the caragor's leap, riding), the world's captains, the map, dying; c: the items
#   rehearsal 1 left unproven for the harness's own reasons (docs 127.2), again in order after the card; d: an A/B -
#   the caragor's leap fresh, then the forge tower, then the leap again (rehearsal 3 met only uncounterable leaps); e:
#   the caragor with the save's own skills (no `beasts grant`); f: the same at RH_FAR_AT (default ride26's proof's place).
# Between items (rehearsal 1's lessons): the player's `recenter` and the sword back in its slot before each driver, and
# every trip waits out the practice area's own pause.
# RH_ONLY="name name" = only those items of the plan; an item longer than the boot has left (RH_HARD = its HEADLESS_HARD,
# default 4200) is skipped with a note. RH_SETTINGS = the settings copy (default /tmp/somvr-reh/
# settings.conf: a COPY of the user's ~/.local/share/somvr/settings.conf made before the boot, as hla27 wants).
# The boot (sandbox off; announce a long boot in the lock's owner file):
#   HEADLESS_HARD=4200 HEADLESS_HOLD=3900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env |
#   tr '\n' ' ') SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217
#   SOMVR_SETTINGS=/tmp/somvr-reh/settings.conf" setsid -w scripts/headless_vr.sh /ABS/WORKTREE/ShadowOfMordor/mod/somvr
# and this script at the same time, from this worktree's scripts/ (started before the boot: hla27 looks for a run newer
# than its own start). Output: RUN/rh-steps.txt (each item's start, end, exit, its PASS/FAIL/NOTE lines, the state),
# RUN/rh-state.txt, RUN/reh-NAME.out (each driver's full output).
SOM=${RH_SOM:-$HOME/Projects/flat2vr/ShadowOfMordor}   # (RH_SOM: a test of this script on a fake run)
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${RH_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
PLAN=${RH_PLAN:-a}
SET=${RH_SETTINGS:-/tmp/somvr-reh/settings.conf}
HOME_AT="32500 2100 -24800"   # the practice area (SOMVR_PRACTICE_AT)
RH_HARD=${RH_HARD:-4200}      # the boot's HEADLESS_HARD (s)
T0=$(date +%s)

# ---- the items: "name|driver|minutes|the Test page's line(s)" (driver "-" = a segment of this script); each driver's
# environment in EV[plan.name] (bash words, eval'd)
declare -A EV
ITEMS_A=(
 "start|hla27|14|The arrival card (A, no jump); Menu > Controls; Settings > Walk toward: Head or Hand; the first fight"
 "stealth|kill22|9|Stealth kill: a hard point-first stab sticks, pull it out; a slow push only turns him"
 "grab|combat21|8|Grab: the empty left hand at an orc's neck - the blue glow and tick, a squeeze"
 "crowd|crowd27|10|Big fights: warned from behind, shield orcs attack; vault over an orc, cut his back"
 "struggle|struggle27c|9|Last chance: the red ring's cursor steered in by the hand, then stab"
 "flash|abil27|7|Wraith flash (right trigger + the open left palm pushed); a downed orc stabbed straight down"
 "drop|above25|7|Drop kill: crouch on a wall top above an unaware orc, stab hard down"
 "brutal|abil27d|7|Brutal kill: crouched behind an unaware orc, the left trigger held, a hard stab"
 "captain|captain24|12|Captains: Practice > Captain; real cuts bite, an execution; immune to stealth; his rune (B)"
 "strike|combat21|5|The bow's sight; Shadow Strike lands with the sword in the hand"
 "fire|abil27c|8|Whistle: a hand at the mouth; Fire arrow: bow drawn, B, the tip glows"
 "distract|abil27d|6|Distract: bow drawn, X - orcs look where you aim"
 "map|-|4|Map: the right stick's click; point and pull; on the cross = a waypoint; B"
 "death|-|5|Dying: in your own eyes; the screens after it answered"
)
EV[a.start]='H27_FILE="$SET" H27_WT="$WT"'
EV[a.stealth]='SD_STEPS="slow thrust out" NOQUIT=1'
EV[a.grab]='C21_STEPS=cue C21_KINDS="infantry berserker"'
EV[a.crowd]='CROWD27_STEPS="look still behind vault" CROWD27_STILL_S=30 CROWD27_BEHIND_S=20 CROWD27_VAULTS=2 CROWD27_NOQUIT=1'
EV[a.struggle]='SC_TRIES=2 SC_NOQUIT=1'
EV[a.flash]='ABIL27_STEPS="flash ground" ABIL27_NOQUIT=1'
EV[a.drop]='STEPS=drop'
EV[a.brutal]='ABIL27D_STEPS=brutal ABIL27D_BRUTAL=brutal1 ABIL27D_NOQUIT=1'
EV[a.captain]='KP_STEPS="measure fight stealth" KP_STEALTH_KINDS=immune'
EV[a.strike]='C21_STEPS=strike'
EV[a.fire]='ABIL27C_STEPS="whistle ignite" ABIL27C_NOQUIT=1'
EV[a.distract]='ABIL27D_STEPS=distract ABIL27D_DISTRACT=1 ABIL27D_NOQUIT=1'
ITEMS_B=(
 "start|-|3|The arrival card (A, no jump)"
 "brutal|abil27d|7|(the A/B: brutalize first in a fresh session) Brutal kill: crouched behind an unaware orc, the left trigger held, a hard stab"
 "immune|captain24|6|(the A/B, fresh) Captains: immune to stealth - the blade glances off"
 "jump|-|2|Jump: A"
 "ruin|move24|10|Vault: a low wall at the ruin by the campfire; the gold ring: A"
 "tower|climb27b|16|Forge tower: down from its top by hand and the hold-A drop, up again, the ring at the top: A"
 "beast|ride26|12|A caragor's leap: the sword across its line; grip it; riding"
 "nemesis|nemesis27|14|Captains in the world: a power struggle at its marker; a beaten captain held by the game"
 "map|-|4|Map: the right stick's click; point and pull; on the cross = a waypoint; B"
 "death|-|5|Dying: in your own eyes; the screens after it answered"
)
ITEMS_C=(
 "start|-|3|The arrival card (A, no jump)"
 "stealth|kill22|9|Stealth kill: a hard point-first stab sticks, pull it out; a slow push only turns him"
 "flash|abil27|6|Wraith flash (right trigger + the open left palm pushed at the orc)"
 "drop|above25|7|Drop kill: crouch on a wall top above an unaware orc, stab hard down"
 "brutal|abil27d|7|Brutal kill: crouched behind an unaware orc, the left trigger held, a hard stab"
 "immune|captain24|6|Captains: immune to stealth - the blade glances off"
 "distract|abil27d|6|Distract (once bought: granted on the run's save copy): bow drawn, X - orcs look where you aim"
 "map|-|4|Map: the right stick's click; point and pull; on the cross = a waypoint; B"
 "death|-|5|Dying: in your own eyes; the screens after it answered"
)
EV[c.distract]='ABIL27D_STEPS="info distract" ABIL27D_DISTRACT=1 ABIL27D_NOQUIT=1'
ITEMS_D=(
 "start|-|3|The arrival card (A, no jump)"
 "beast|ride26|7|(the A/B, fresh) A caragor's leap: the sword across its line - stunned"
 "tower|climb27b|16|Forge tower: down from its top by hand, up again"
 "beast2|ride26|7|(the A/B, after the tower) A caragor's leap: the sword across its line - stunned"
)
ITEMS_E=(
 "start|-|3|The arrival card (A, no jump)"
 "beast|ride26|12|(the save as it is: no grant) A caragor's leap: the sword across its line - stunned; mount; ride"
)
ITEMS_F=(
 "start|-|3|The arrival card (A, no jump)"
 "far|-|2|(a place test) the trip to where ride26's proof met counterable leaps (boot 20261029-224258)"
 "beastfar|ride26|10|(there, the save as it is) A caragor's leap: the sword across its line - stunned; mount; ride"
)
EV[f.beastfar]='R26_STEPS="leap smount ride"'
EV[e.beast]='R26_STEPS="leap smount ride"'
EV[d.beast]='R26_STEPS="grant leap"'
EV[d.tower]='TOPS="tower:15790 6237 -3110||2"'
EV[d.beast2]='R26_STEPS=leap'
EV[c.stealth]='SD_STEPS="slow thrust out" NOQUIT=1'
EV[c.flash]='ABIL27_STEPS=flash ABIL27_NOQUIT=1'
EV[c.drop]='STEPS=drop'
EV[c.brutal]='ABIL27D_STEPS=brutal ABIL27D_BRUTAL=brutal1 ABIL27D_NOQUIT=1'
EV[c.immune]='KP_STEPS=stealth KP_STEALTH_KINDS=immune'
EV[b.brutal]='ABIL27D_STEPS=brutal ABIL27D_BRUTAL=brutal1 ABIL27D_NOQUIT=1'
EV[b.immune]='KP_STEPS=stealth KP_STEALTH_KINDS=immune'
EV[b.ruin]='PLAN="survey vtry" SPOTS="camp:30873 3843 -32974"'
EV[b.tower]='TOPS="tower:15790 6237 -3110||2"'
EV[b.beast]='R26_STEPS="grant leap smount ride"'
EV[b.nemesis]='NM_STEPS="survey struggle fight"'

# ---- the run --------------------------------------------------------------------------------------------------------
R=""
start_hla() {   # plan a: hla27 starts with the boot (it finds the run itself, newer than its own start)
  mkcopy hla27; HOUT=/tmp/somvr-reh/reh-start-$T0.out; mkdir -p /tmp/somvr-reh
  ( cd "$HERE" && eval "exec env ${EV[a.start]} setsid bash \"$HERE/.reh-hla27-drive.sh\"" ) > "$HOUT" 2>&1 &
  HLA=$!; }
mkcopy() {   # scripts/.reh-NAME-drive.sh: the driver without its quits (its sourced libraries too)
  local n=$1 f
  for f in "$n" climb2 pin16; do
    [ -f "$HERE/$f-drive.sh" ] || continue
    sed -e 's|echo quit >> "\$R/cmd"|echo "(rehearsal: no quit)" >\&2|g' \
        -e 's|/climb2-drive\.sh"|/.reh-climb2-drive.sh"|g; s|/pin16-drive\.sh"|/.reh-pin16-drive.sh"|g' \
        "$HERE/$f-drive.sh" > "$HERE/.reh-$f-drive.sh"
    grep -nE 'quit.*>>|>>.*cmd.*quit' "$HERE/.reh-$f-drive.sh" | grep -v 'rehearsal: no quit' && { echo "STOP: a quit left in .reh-$f"; exit 1; }
  done; }
SOM_LOGS=$SOM/logs
[ "$PLAN" = a ] && start_hla
for i in $(seq 1 120); do
  for d in $(ls -dt $SOM_LOGS/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log
X=$R/xr-layer.log
ST=$R/rh-steps.txt
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
CUR=""
cleanup() { rm -f "$HERE"/.reh-*-drive.sh; }
bail() { echo "$(date +%T) STOP: $1" | tee -a "$ST"; [ -n "$CUR" ] && kill -- -"$CUR" 2>/dev/null; echo quit >> "$R/cmd"; cleanup; exit 1; }
# the watcher: the user in VR = the game stopped at once (every 2 s)
( while sleep 2; do userin && { echo "$(date +%T) STOP: the user is in VR" >> "$ST"; g=$(cat "$R/.reh-cur" 2>/dev/null); [[ $g =~ ^[0-9]+$ ]] && [ "$g" -gt 1 ] && kill -- -"$g" 2>/dev/null
    echo quit >> "$R/cmd"; exit 0; }; [ -e "$R/.reh-done" ] && exit 0; done ) &
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${T_S:-0.6}"; }
tap() { t "btn $1 $2 1"; sleep "${3:-0.25}"; t "btn $1 $2 0"; }
cnt() { grep -cE "$1" "${2:-$L}"; }
lines() { wc -l < "${1:-$L}"; }
since() { tail -n +"$(( $1 + 1 ))" "${3:-$L}" | grep -E "$2"; }
nsince() { since "$1" "$2" "${3:-$L}" | wc -l; }
waitfor() { local n0 i; n0=$(cnt "$1" "${3:-$L}"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1" "${3:-$L}")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$X" | tail -1 | grep -o '[0-9]*$'); note "dump $1 $f"; }
ui() { grep -E '^SESSION ui ' "$L" | tail -1 | cut -c1-110; }
inworld() { grep -E '^SESSION ui ' "$L" | tail -1 | grep -qE 'paused=0 .*ctx=world'; }
go() { local a0; a0=$(cnt '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)'); w "practice go $1" 0.3
  for i in $(seq 1 120); do [ "$(cnt '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)')" -gt "$a0" ] && break; sleep 0.25; done
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$L" | tail -1 | cut -c1-140
  # (rehearsal 1: the arrival's weather change pauses the game up to 5.5 s - `the black held past its 1.00 s for the
  # game's pause after our ChangeWeather`; the next driver's first words, written in it, overwrote each other: above25's
  # own trip was lost)
  sleep 7; }
REST_L="hand L head -22 -40 15 0 0 0"; REST_R="hand R head 22 -40 15 0 0 0"
# the test tools put back (never the game's state): the mod's test hands and squeezes given back, the layer's buttons
# and sticks let go, the test head level, the test chase, guard, template and crouch hold off, a spawned orc removed
tools_back() { local h b
  w "hands squeeze L -1" 0.2; w "hands squeeze R -1" 0.2; w "hands test off" 0.3
  local ls=(); for h in L R; do for b in menu a b stick trigger squeeze; do ls+=("btn $h $b 0"); done; ls+=("stick $h 0 0"); done
  t "${ls[@]}" "head 0 0 0" "$REST_L" "$REST_R"
  w "execute chase off" 0.3; w "fight guard off" 0.3; w "verbs template off" 0.3; w "crouch stand" 0.3
  w "practice remove" 1.0; }
perfline() {   # the last 10-s window's GPU and the game thread's busy time (means, ms), the layer's gaps, the WARNs
  local g b; g=$(grep -E 'xr: perf: 10\.0 s' "$X" | tail -1 | sed -nE 's/.*GPU total ([0-9.]+)\/[0-9.]+\/([0-9.]+)\/([0-9.]+).*/gpu \1 p95 \2 max \3/p')
  b=$(grep -E '^PERF .* game thread: busy' "$L" | tail -1 | sed -nE 's/.*game thread: busy ([0-9.]+)\/[0-9.]+\/([0-9.]+)\/([0-9.]+).*/busy \1 p95 \2 max \3/p')
  echo "$g; game $b; gaps $(grep -cE 'xr: gap [0-9]+:' "$X"); WARN $(grep -c '^WARN' "$L")/$(grep -c 'WARN' "$X"); log $(( $(stat -c %s "$L") / 1024 )) KB"; }
state() {   # after each item: what it left behind
  w "climb state" 0.8; w "practice status" 0.8
  { echo "== $(date +%T) after $1 ($(( ($(date +%s) - T0) / 60 )) min)"
    local k
    for k in '^SESSION ui ' '^CLIMB state' '^BODYMOMENT t=' '^CROUCH ' '^BOW .* (enter |exit: )' '^PRACTICE t=.*status' '^SLOTS .*sword: ' '^RIDE t=' '^STRUGGLE t=' '^VIEW (hold|held)'; do
      grep -E "$k" "$L" | tail -1 | cut -c1-200; done
    for k in 'xr: comfort frame' 'xr: vrmenu: (closed|opened)' 'xr: stereo frame' 'xr: card:'; do grep -E "$k" "$X" | tail -1 | cut -c1-200; done
    echo "perf: $(perfline)"
  } >> "$R/rh-state.txt"
  d "after-$1"; }
results() {   # the driver's own verdicts (PASS / FAIL / NOTE / STOP lines), short
  grep -E '(^|[ :])(PASS|FAIL|NOTE|STOP)\b' "$1" | grep -vE '^(\S+ )?(word|xrtest):' | cut -c1-220 | head -${2:-40} | sed 's/^/    /' >> "$ST"; }

# the sword: in the hand (SLOTS' last word; soak27's test) / back in its slot by the player's own move (the right hand
# at the shoulder's slot, squeezed: the test hands as the drivers' draw_slot), before a driver that draws it there
sword_in_hand() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$L" | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
sheathe() { sword_in_hand || return 0
  w "hands test R 22 -35 -30 0 180 0" 0.3; w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6
  w "hands squeeze R 0" 0.4; w "hands test R 22 -35 -30 0 180 0" 0.4; w "hands squeeze R -1" 0.2; w "hands test off" 0.4
  sword_in_hand && note "  the sword did not go back: $(grep -E '^SLOTS .*sword: ' "$L" | tail -1 | cut -c1-120)" || note "  the sword put back in its slot"; }

# ---- this script's own segments --------------------------------------------------------------------------------------
seg_start() {   # plan b: the arrival card away by A (as the card says) - never a jump
  local j0; j0=$(cnt '^JUMP t=.* go'); sleep 3; d start-card
  grep -q 'card: put away' "$X" || { tap R a 0.3; sleep 2; }
  note "card: $(grep -E 'xr: card:' "$X" | tail -1 | cut -c1-120); jumps from it: $(( $(cnt '^JUMP t=.* go') - j0 ))"; }
seg_jump() {   # on the practice meadow: A = a jump (the controllers' own A, no test word)
  local n0; n0=$(lines); go "$HOME_AT"; t "$REST_L" "$REST_R"; tap R a 0.15; sleep 2.5; tap R a 0.15; sleep 2.5
  note "jump: $(nsince "$n0" '^JUMP t=.* go') go, $(nsince "$n0" '^JUMP t=.* (land|end)') landings; $(since "$n0" '^JUMP t=.* (go|land|end)' | tail -1 | cut -c1-150)"; }
seg_far() { note "far: $(go "${RH_FAR_AT:--41458 1767 -22655}")"; d far-arrived; }
seg_map() {   # the map by the right stick's click; a press beside the cross = the place brought there; a press on the
  # cross = the waypoint; B shuts it; the waypoint shown in the world
  local n0 k; n0=$(lines); inworld || { tap R b; sleep 1.5; }
  # a click, not the hold (rehearsal 1: t's own 0.6 s + 0.2 s = 0.8 s held, past SOMVR_ARMY's 0.5 s: Sauron's Army
  # came up instead of the map); the dumps wait for the world again (a paused menu on the board makes no stereo frames)
  t "$REST_L" "$REST_R"; T_S=0.05 t "btn R stick 1"; sleep 0.12; T_S=0.05 t "btn R stick 0"; sleep 3.5; note "map: $(ui)"
  t "$REST_L" "hand R aim board 0.66 0.40 30"; sleep 0.8; t "btn R trigger 1"; sleep 0.25; t "btn R trigger 0"; sleep 3
  t "$REST_L" "hand R aim board 0.50 0.50 30"; sleep 0.8; t "btn R trigger 1"; sleep 0.25; t "btn R trigger 0"; sleep 2.5
  t "$REST_L" "$REST_R"; sleep 0.5
  for k in 1 2 3 4; do inworld && break; tap R b; sleep 1.8; done; sleep 2; d map-world
  note "map: $(grep -E '^MAP END' "$L" | tail -1 | cut -c1-150)"; note "map: $(nsince "$n0" '^MAP t=.*a press') presses ($(since "$n0" '^MAP t=.*a press' | sed -E 's/.*from the cross: //' | cut -c1-60 | paste -sd'|')); waypoint lines $(nsince "$n0" "UserWaypoint"); back in the world: $(inworld && echo yes || echo NO)"; }
seg_death() {   # the practice's Captain with the stakes on, no guard, until Talion is down; the screens after it
  # answered (B, then A) until the world is back (soak27's)
  local n0 k down=0 t1; n0=$(lines); go "$HOME_AT"
  w "captain stakes on" 0.3; w "practice captain" 2.5; waitfor '^CAPTAIN t=.* the presentation is over' 40
  w "execute chase on" 0.4
  for k in $(seq 1 150); do userin && bail "the user is in VR"
    [ "$(nsince "$n0" '^CAPTAIN t=.* Talion is down')" -gt 0 ] && { down=1; break; }; sleep 1; done
  w "execute chase off" 0.3; sleep 2; d death-down; t1=$(date +%s)
  for k in $(seq 1 40); do sleep 3; inworld && [ $(( $(date +%s) - t1 )) -gt 20 ] && break
    [ $k = 2 ] && d death-screen; [ $((k % 3)) = 0 ] && tap R a || tap R b; done
  note "death: down=$down, the screens $(( $(date +%s) - t1 )) s, back in the world: $(inworld && echo yes || echo NO); $(nsince "$n0" '^BODYMOMENT t=.*death|BM_DEATH|^DEATH') death-view lines"
  w "captain stakes off" 0.3; w "practice remove" 1.5; }

# ---- the plan --------------------------------------------------------------------------------------------------------
case $PLAN in a) ITEMS=("${ITEMS_A[@]}") ;; b) ITEMS=("${ITEMS_B[@]}") ;; d) ITEMS=("${ITEMS_D[@]}") ;; e) ITEMS=("${ITEMS_E[@]}") ;; f) ITEMS=("${ITEMS_F[@]}") ;; *) ITEMS=("${ITEMS_C[@]}") ;; esac
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
note "arrived; plan $PLAN: $(for it in "${ITEMS[@]}"; do printf '%s ' "${it%%|*}"; done)"
for it in "${ITEMS[@]}"; do
  IFS='|' read -r name drv mins what <<< "$it"; [ -n "${RH_CAP:-}" ] && [ "$mins" -gt "$RH_CAP" ] && mins=$RH_CAP
  [ -n "${RH_ONLY:-}" ] && case " $RH_ONLY " in *" $name "*) ;; *) continue ;; esac
  pgrep -x ShadowOfMordor > /dev/null || { note "the game is gone before $name"; break; }
  # an item that cannot end before the boot's own hard limit (RH_HARD, the boot's HEADLESS_HARD less its 130 s) waits
  # for the next boot (RH_ONLY): a clean end, not a driver cut off
  left=$(( RH_HARD - 190 - ($(date +%s) - $(stat -c %W "$R" 2>/dev/null || echo "$T0")) ))
  [ $(( mins * 60 )) -gt "$left" ] && { note "== $name: not in this boot ($mins min > $(( left / 60 )) min left)"; continue; }
  grep -q 'pass-through from now on' "$X" && { note "VR LOST: $(grep 'pass-through from now on' "$X" | head -1 | cut -c1-160)"; break; }
  note "== $name ($drv, at most $mins min): $what${EV[$PLAN.$name]:+ [${EV[$PLAN.$name]}]}"; ts=$(date +%s); n0=$(lines); w0=$(cnt '^WARN'); x0=$(cnt 'WARN' "$X")
  if [ "$drv" = - ]; then "seg_$name"; rc=$?
  elif [ "$name" = start ] && [ -n "${HLA:-}" ]; then
    echo "$HLA" > "$R/.reh-cur"; CUR=$HLA
    while kill -0 "$HLA" 2>/dev/null; do sleep 2; [ $(( $(date +%s) - ts )) -gt $((mins * 60)) ] && { note "TIMEOUT: $drv"; kill -- -"$HLA"; }; done
    wait "$HLA"; rc=$?; CUR=""; cp "$HOUT" "$R/reh-start.out"; results "$R/reh-start.out"
  else
    case $name in ruin|tower|nemesis|beastfar) ;; *) go "$HOME_AT" > /dev/null ;; esac
    w "recenter" 1.5; sheathe
    mkcopy "$drv"
    RUNS="SD_RUN=$R C21_RUN=$R CROWD27_RUN=$R SK_RUN=$R ABIL27_RUN=$R ABIL27C_RUN=$R ABIL27D_RUN=$R KP_RUN=$R NM_RUN=$R PIN_RUN=$R R26_RUN=$R"
    ( cd "$HERE" && eval "exec env $RUNS ${EV[$PLAN.$name]:-} \\
        setsid bash \"$HERE/.reh-$drv-drive.sh\"" ) > "$R/reh-$name.out" 2>&1 &
    CUR=$!; echo "$CUR" > "$R/.reh-cur"
    k9=0; while kill -0 "$CUR" 2>/dev/null; do sleep 2; [ $k9 = 0 ] && [ $(( $(date +%s) - ts )) -gt $((mins * 60)) ] && { note "TIMEOUT: $drv after $mins min"; kill -- -"$CUR"; k9=1; }; done
    wait "$CUR"; rc=$?; CUR=""; results "$R/reh-$name.out"
  fi
  note "$name: exit $rc, $(( $(date +%s) - ts )) s, WARN +$(( $(cnt '^WARN') - w0 ))/+$(( $(cnt 'WARN' "$X") - x0 )), $(nsince "$n0" '.') log lines"
  [ "$(cnt '^WARN')" -gt "$w0" ] && since "$n0" '^WARN' | head -3 | cut -c1-200 | sed 's/^/    /' >> "$ST"
  # what it left behind, before the test tools go back: the world, a screen, the climb, the bow
  note "$name left: $(ui); $(grep -E '^CLIMB state' "$L" | tail -1 | cut -c1-90)"
  tools_back
  inworld || { note "after $name NOT in the world: $(ui) - B"; tap R b; sleep 2; inworld || { tap R b; sleep 2; }; note "  then: $(ui)"; }
  state "$name"
done
note "done after $(( ($(date +%s) - T0) / 60 )) min; $(perfline)"
touch "$R/.reh-done"; w "endlogs" 2; sleep 2; echo quit >> "$R/cmd"; cleanup; echo "$(date +%T) quit"
