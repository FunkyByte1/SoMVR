#!/bin/bash
# ladder-drive.sh - som-b37-ladder (the story save ladder, GAME-STUDY item 1.6; docs/re-xr-pipeline.md 172): the driver of
# scripts/ladder_boot.sh's boot from a rung's save (logs/test-save/ladder/<NN-name>/). The steps of LADDER_STEPS in order
# (default "look card"), then quit (LADDER_NOQUIT=1: no quit):
#   look          `objective status`, `practice status nav`, `practice status` (Talion's place, the light now),
#                 `beasts skills` -> RUN/ladder-look<N>.txt (what a rung's README says)
#   card          Talion to the marker (+50 cm; LADDER_MARKER="NAME X Y Z", else the rung README's `marker:` line), B
#                 (QuestConfirm 282: the mission's card, the window opens), the card's picture (shots/st-ladder-card<N>),
#                 B again (QuestConfirmCancel 284: the card away, nothing started)
#   start         the same up to the card, then A (QuestConfirmStart 283), the story video skipped (A, then B: MenuEnter,
#                 MenuEscape), back in the world (up to 3 min), the mission's first objective line and picture
#   complete      `practice console Quest.CompleteActiveQuest` (the test aid: the active quest completed by the game's own
#                 command), then the end: a video's A + B, a paused page's A, a window's A then B, until the world is
#                 quiet for 6 rounds (LADDER_END_MIN minutes at most, 8)
#   fail          `practice console Quest.FailActiveQuest`, then the end as above (the failed screen: B = Abandon, A on
#                 Leave Mission)
#   console CMD~ARGS   one console line by the test aid ('~' = a space)
#   save NAME     the slot prefix's save as it is now -> RUN/saves/NAME.sav (the prefix: RUN/summary.txt's `save:` line)
#   go X Y Z      `practice go X Y Z`, the arrival waited for;  marker NAME X Y Z: the next `card`/`start`'s marker;
#                 marker new: the first golden marker of the last look that the first look did not have (a mission the
#                 completed one opened); mark NAME|any: the nearest golden marker NAME (any golden) of the last look
#   end           the end handling alone;  wait S;  shot NAME (a flat picture);  word W~W (a trigger word)
#   qstate NAME S the quest NAME (its record at +0xd0, the game's list 2) set to state S by its state machine (2 =
#                 available: its marker; the test word `practice quest state N S`); qcomplete NAME: the active quest
#                 NAME completed (`practice quest complete N`, as Quest.CompleteActiveQuest)
#   chain N [S]   up to N times: wait up to S s (60) for an active quest, complete it, the end, saves/chainK.sav, a look,
#                 the region's GP_ quests (the story's last missions run one into the next)
#   qrun NAME     the story quest NAME started by the test aid (state 8, no card) and completed; saves/NAME.sav
#   board [NAME]  Sauron's Army's warchief board (`verbs press 281 1500`): its picture (shots/st-ladder-NAME), then B
#   wcbrand K     up to K of the save's warchiefs (`captain status`) brought alone to the practice area (`captain pick N
#                 warchief`, `practice captain`; Talion must be there: `word practice go` first) and branded by the
#                 console's `AI.AddBrand SomVR_Practice` (the practice spawn's object name)
#   press CMD~MS~N~GAP  the stock command CMD (`verbs press`, a test aid) held MS ms, N times GAP s apart, then the
#                 lessons and lines that came (the prologue's lessons wait for the stock command)
# A rung README's `prep: STEP ...` line runs first (LADDER_NOPREP=1: not). Words go to RUN/somvr.trigger, buttons to RUN/xrtest.txt (the test hands: SOMVR_HANDS_TEST=1), pictures through RUN/cmd.
# Stops at once (quit) when the user goes into VR. Every console command is a test aid on the run's save copy only.
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
R=${LADDER_RUN:-${ST_RUN:-}}; [ -d "$R" ] || { echo "no run (LADDER_RUN)"; exit 1; }
RUNG=${LADDER_RUNG:-}
STEPS=${LADDER_STEPS:-look card}
# a rung's README may name steps that must run first ("prep: STEP STEP ...", '~' = a space): rung 19's availability
PREP=""; [ -n "$RUNG" ] && [ -f "$RUNG/README.txt" ] && PREP=$(grep -m1 -E '^prep: ' "$RUNG/README.txt" | cut -c7- | cut -d'(' -f1)
[ -n "$PREP" ] && [ -z "${LADDER_NOPREP:-}" ] && STEPS="$PREP $STEPS"
echo "$(date +%T) run $R, rung ${RUNG:-none}, steps: $STEPS"
userin() { [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
           grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0; return 1; }
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
gone() { grep -qE '^(.* )?(program exit|the hold .* is over|the game was stopped)' "$R/summary.txt" 2>/dev/null; }
N=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# ladder $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.6; }
tap() { userin && bail "the user is in VR"; printf 'btn %s %s 1\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        sleep "${3:-0.3}"; printf 'btn %s %s 0\n' "$1" "$2" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
        echo "$(date +%T) tap $1 $2 ${3:-0.3}"; sleep 0.5; }
state() { grep -E '^SESSION ui ' "$R/somvr.log" | tail -1 | cut -c12-80; }
shot() { echo "shot st-$1" >> "$R/cmd"; echo "$(date +%T) shot st-$1 ($(state))"; sleep "${2:-1.2}"; }
pics() { t "dump"; shot "$@"; }
cnt() { local c; c=$(grep -cE "$1" "$R/somvr.log" 2>/dev/null); echo "${c:-0}"; }
lines() { wc -l < "$R/somvr.log"; }
win() { local a b; a=$(grep -n 'window 0 -> 1' "$R/somvr.log" | tail -1 | cut -d: -f1); b=$(grep -n 'window 1 -> 0' "$R/somvr.log" | tail -1 | cut -d: -f1)
        [ "${a:-0}" -gt "${b:-0}" ]; }
inworld() { state | grep -q 'paused=0 video=0 ctx=world'; }
moments() { tail -n +"$1" "$R/somvr.log" | grep -E '^(BODYMOMENT t=.*window|SESSION ui|WARN|OBJECTIVE t=.*(shown by the game|marker [0-9]+ m=)|PRACTICE t=.*(arrived|console)|HINTS tutorial|USE t=|STORY t=.*subtitle ch=[01] )' |
  cut -c1-200 | awk '!s[$0]++' | head -"${2:-40}"; }
# the marker: LADDER_MARKER, else the README's "marker: NAME X Y Z"
MK=${LADDER_MARKER:-}
[ -z "$MK" ] && [ -n "$RUNG" ] && [ -f "$RUNG/README.txt" ] && MK=$(grep -m1 -E '^marker: ' "$RUNG/README.txt" | cut -c9- | awk '{print $1, $2, $3, $4}')
# the arrival: the world twice (story22-drive.sh's test)
for i in $(seq 1 150); do
  userin && bail "the user is in VR"; gone && { echo "the game ended before the world"; exit 1; }
  [ "$(cnt '^SESSION ui .*paused=0 video=0 ctx=world')" -ge 2 ] && break; sleep 3
done
[ "$(cnt '^SESSION ui .*paused=0 video=0 ctx=world')" -ge 2 ] || bail "no arrival"
echo "$(date +%T) arrived ($(state))"; sleep 6
# the controls card away, the hands at rest (story22-drive.sh's start), the AI blind to Talion (a test aid)
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" 2>/dev/null || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"; t "hand L head -25 -35 30 0 0 0"; w "hands test R 28 -40 20 0 0 0" 2
w "story invisible on" 1
pics ladder-arrival 1
LK=0
look() { local n0; n0=$(lines); LK=$((LK + 1))
  w "objective status" 1.5; w "practice status nav" 2.5; w "practice status" 1.2; w "beasts skills" 2
  tail -n +"$n0" "$R/somvr.log" | grep -E "^OBJECTIVE status|^PRACTICE nav [0-9]+ '|^PRACTICE t=.*status|^BEASTS (t=.*)?skills" \
    > "$R/ladder-look$LK.txt"   # (whole lines: the skills' lines are long)
  echo "$(date +%T) look $LK: $(grep -c "^PRACTICE nav" "$R/ladder-look$LK.txt") markers; golden: $(grep -oE "nav [0-9]+ 'golden[A-Za-z]*' at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)" "$R/ladder-look$LK.txt" | cut -d' ' -f3- | tr '\n' ';')"
  grep -E '^PRACTICE t=.*status' "$R/ladder-look$LK.txt" | cut -c1-200; }
goto() { local a0 i; a0=$(cnt 'arrived at'); w "practice go $1 $2 $3" 1
  for i in $(seq 1 60); do [ "$(cnt 'arrived at')" -gt "$a0" ] && break; sleep 1; done
  grep 'arrived at' "$R/somvr.log" | tail -1 | cut -c1-150; sleep 3; }
CD=0
card() { # the card opened at the marker: 0 = it opened
  [ -n "$MK" ] || { echo "no marker (LADDER_MARKER or the README's marker: line)"; return 1; }
  set -- $MK; echo "$(date +%T) the marker $1 at $2 $3 $4"; goto "$2" $(( $3 + 50 )) "$4"
  CD=$((CD + 1)); pics ladder-marker$CD 0.5
  local l0 i; l0=$(lines); tap R b 0.4
  for i in $(seq 1 20); do win && break; sleep 0.5; done
  sleep 2; pics ladder-card$CD 0.5
  win && { echo "$(date +%T) CARD $CD open: $(state)"; return 0; }
  echo "$(date +%T) CARD $CD did not open: $(state)"; moments "$l0" 12; return 1; }
endphase() { # videos (A, B), paused pages (A), windows (A then B) until quiet for 6 rounds
  local q=0 n=0 T=$((SECONDS + ${LADDER_END_MIN:-8} * 60)) s
  while [ $SECONDS -lt $T ]; do n=$((n + 1)); s=$(state); userin && bail "the user is in VR"; gone && return 1
    if echo "$s" | grep -q 'video=1'; then q=0; echo "$(date +%T) end $n video"; sleep 6; shot ladder-end-v$n 0.5; tap R a 0.3; sleep 0.7; tap R b 0.3; sleep 2
    elif echo "$s" | grep -q 'paused=1'; then q=0; echo "$(date +%T) end $n paused"; sleep 2; shot ladder-end-p$n 0.5; tap R a 0.4; sleep 2
    elif win; then q=0; echo "$(date +%T) end $n window"; shot ladder-end-w$n 0.5; sleep 5; win && { tap R a 0.4; sleep 3; }; win && { tap R b 0.4; sleep 3; }
    else q=$((q + 1)); [ $q -ge 6 ] && { echo "$(date +%T) quiet after $n rounds"; return 0; }; sleep 4; fi
  done; echo "$(date +%T) end: not quiet in ${LADDER_END_MIN:-8} min ($(state))"; }
PFXSAVE=${LADDER_PFXSAVE:-$(grep -m1 -oE 'save: a copy of .* as .*/GameData\.sav' "$R/summary.txt" 2>/dev/null | sed -E 's/.* as //')}
for p in $STEPS; do
  p=${p//\~/ }; set -- $p; c=$1; shift
  echo "$(date +%T) ==== $c $*"
  userin && bail "the user is in VR"; gone && { echo "the game is gone"; exit 1; }
  case $c in
    look) look ;;
    card) card && { tap R b 0.4; for i in $(seq 1 20); do win || break; sleep 0.5; done
                    echo "$(date +%T) the card away: $(state)"; } ;;
    start) l0=$(lines); card || continue; tap R a 0.4
      for i in $(seq 1 40); do state | grep -q 'video=1' && break; sleep 1; done
      echo "$(date +%T) after A: $(state)"; state | grep -q 'video=1' && { sleep 8; tap R a 0.3; sleep 0.7; tap R b 0.3; }
      for i in $(seq 1 90); do sleep 2; inworld && ! win && break
        state | grep -q 'video=1' && { tap R a 0.3; sleep 0.7; tap R b 0.3; }; done
      echo "$(date +%T) back in the world: $(state)"; sleep 6; pics ladder-started 0.5; moments "$l0" 30 ;;
    complete|fail) l0=$(lines); [ $c = complete ] && w "practice console Quest.CompleteActiveQuest" 3 || w "practice console Quest.FailActiveQuest" 3
      grep -E "^PRACTICE t=.*console \(the test word\)" "$R/somvr.log" | tail -1 | cut -c1-200
      sleep 4; pics ladder-$c 0.5; endphase; moments "$l0" 40 ;;
    console) w "practice console $*" 3; grep -E "^PRACTICE t=.*console \(the test word\)" "$R/somvr.log" | tail -1 | cut -c1-200 ;;
    save) [ -n "$PFXSAVE" ] && [ -f "$PFXSAVE" ] || { echo "no prefix save ($PFXSAVE)"; continue; }
      mkdir -p "$R/saves"; cp --no-preserve=mode "$PFXSAVE" "$R/saves/$1.sav"
      echo "$(date +%T) save $1: $(sha256sum "$R/saves/$1.sav" | cut -c1-16), written by the game $(stat -c %y "$PFXSAVE" | cut -c1-19)" ;;
    go) goto "$1" "$2" "$3" ;;
    mark) # mark NAME: the next card's marker = the golden marker NAME of the last look (the nearest one)
      mre=$1; [ "$1" = any ] && mre='golden[A-Za-z]*'   # (any: the nearest golden marker)
      MKN=$(grep -E "^PRACTICE nav [0-9]+ '$mre' at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\), [0-9.]+ m" "$R/ladder-look$LK.txt" 2>/dev/null |
            sed -E "s/^PRACTICE nav [0-9]+ '([^']*)' at \((-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\), ([0-9.]+) m.*/\5 \1 \2 \3 \4/" | sort -n | head -1 | cut -d' ' -f2-)
      [ -n "$MKN" ] && MK=$MKN || echo "no marker $1 in look $LK"; echo "marker now: $MK" ;;
    marker) if [ "$1" = new ]; then   # a golden marker of the last look that the first look did not have
        g() { grep -oE "^PRACTICE nav [0-9]+ 'golden[A-Za-z]*' at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)" "$R/ladder-look$1.txt" 2>/dev/null |
              sed -E "s/^PRACTICE nav [0-9]+ '([^']*)' at \((-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\)/\1 \2 \3 \4/" | sort -u; }
        NEW=$(comm -13 <(g 1) <(g $LK) | head -1); [ -n "$NEW" ] && MK=$NEW || echo "no new golden marker (look 1 vs $LK)"
      else MK="$*"; fi; echo "marker now: $MK" ;;
    end) endphase ;;
    word) w "$*" 1 ;;
    chain) # chain N [S]: up to N times - wait up to S s (60) for an active quest (`practice quest list`), complete it
      # (Quest.CompleteActiveQuest), the end, the save (saves/chainK.sav), a look, the region's GP_ quests (list 2)
      for k in $(seq 1 "${1:-3}"); do
        aq=""; for i in $(seq 1 $(( ${2:-60} / 4 ))); do l0=$(lines); w "practice quest list" 3
          aq=$(tail -n +"$l0" "$R/somvr.log" | grep -E "^PRACTICE quest [0-9]+ q=.* state 8: \+0xd0 '[^']*'" | head -1 | grep -oE "\+0xd0 '[^']*'" | cut -d"'" -f2)
          [ -n "$aq" ] && break; state | grep -q 'video=1' && { tap R a 0.3; sleep 0.7; tap R b 0.3; }; done
        echo "$(date +%T) chain $k: the active quest '${aq:-none}'"; [ -n "$aq" ] || break
        w "practice console Quest.CompleteActiveQuest" 3; endphase; [ -n "$PFXSAVE" ] && [ -f "$PFXSAVE" ] && {
          mkdir -p "$R/saves"; cp --no-preserve=mode "$PFXSAVE" "$R/saves/chain$k.sav"
          echo "$(date +%T) save chain$k ($aq done): $(sha256sum "$R/saves/chain$k.sav" | cut -c1-16), written $(stat -c %y "$PFXSAVE" | cut -c1-19)"; }
        look; l0=$(lines); w "practice quest list 2" 3
        tail -n +"$l0" "$R/somvr.log" | grep -E "^PRACTICE quest \(list 2\) [0-9]+ .*'GP_" | sed -E 's/q=0x[0-9a-f]+ //' | cut -c1-120
      done ;;
    qrun) # qrun NAME: the story quest NAME (its record) started by the test aid (state 8: no card - for a mission whose
      # marker the game does not show) and completed at once; the end; the save as saves/NAME.sav
      STEPS_SUB="qstate~$1~8 wait~8 end qcomplete~$1 wait~4 end save~$1"
      for p2 in $STEPS_SUB; do p2=${p2//\~/ }; echo "$(date +%T) ---- $p2"; set -- $p2; c2=$1; shift
        case $c2 in
          qstate|qcomplete) l0=$(lines); [ $c2 = qstate ] && w "practice quest list 2" 3 || w "practice quest list" 3
            qn=$(tail -n +"$l0" "$R/somvr.log" | grep -E "^PRACTICE quest (\(list 2\) )?[0-9]+ q=0x[0-9a-f]+ state -?[0-9]+: \+0x[0-9a-f]+ '$1'" | head -1 |
                 sed -E 's/^PRACTICE quest (\(list 2\) )?([0-9]+) .*/\2/')
            [ -n "$qn" ] || { echo "$(date +%T) no quest '$1' in the list"; continue; }
            l0=$(lines); [ $c2 = qstate ] && w "practice quest state $qn $2" 3 || w "practice quest complete $qn" 3
            tail -n +"$l0" "$R/somvr.log" | grep -E "^PRACTICE t=.*quest (state|complete) " | cut -c1-200 ;;
          wait) sleep "$1" ;;
          end) endphase ;;
          save) [ -n "$PFXSAVE" ] && [ -f "$PFXSAVE" ] && { mkdir -p "$R/saves"; cp --no-preserve=mode "$PFXSAVE" "$R/saves/$1.sav"
            echo "$(date +%T) save $1: $(sha256sum "$R/saves/$1.sav" | cut -c1-16), written by the game $(stat -c %y "$PFXSAVE" | cut -c1-19)"; } ;;
        esac
      done ;;
    qstate|qcomplete) # qstate NAME S: the quest NAME (its record, list 2) set to state S (2 = available) | qcomplete NAME:
      # the active quest NAME (list 1) completed - the test words `practice quest list [2]` + `state N S` / `complete N`
      l0=$(lines); [ $c = qstate ] && w "practice quest list 2" 3 || w "practice quest list" 3
      qn=$(tail -n +"$l0" "$R/somvr.log" | grep -E "^PRACTICE quest (\(list 2\) )?[0-9]+ q=0x[0-9a-f]+ state -?[0-9]+: \+0x[0-9a-f]+ '$1'" | head -1 |
           sed -E 's/^PRACTICE quest (\(list 2\) )?([0-9]+) .*/\2/')
      [ -n "$qn" ] || { echo "$(date +%T) no quest '$1' in the list"; continue; }
      l0=$(lines); [ $c = qstate ] && w "practice quest state $qn $2" 3 || w "practice quest complete $qn" 3
      tail -n +"$l0" "$R/somvr.log" | grep -E "^PRACTICE t=.*quest (state|complete) " | cut -c1-220 ;;
    board) # Sauron's Army's warchief board (`verbs press 281`, EnterHierarchyMenu): its picture (the "N / 5 Branded" line), then away
      w "verbs press 281 1500" 5; shot "ladder-${1:-board}" 1; state; tap R b 0.3; sleep 2
      state | grep -q 'ctx=world' || { tap R b 0.3; sleep 2; }; echo "$(date +%T) the board away: $(state)" ;;
    wcbrand) # wcbrand K: the save's warchiefs (`captain status`), up to K not yet seen, each brought alone to the practice
      # area (`captain pick N warchief`, `practice captain`) and branded by `practice console AI.AddBrand SomVR_Practice`
      l0=$(lines); w "captain status" 2.5
      WC=$(tail -n +"$l0" "$R/somvr.log" | grep -E '^CAPTAIN member [0-9]+: .*: a warchief \(his bodyguards\)' |
           grep -vE 'flags 0x[0-9a-f]*[89a-f][0-9a-f]:' | sed -E 's/^CAPTAIN member ([0-9]+):.*/\1/')   # (flags bit 0x80: branded already)
      echo "$(date +%T) warchiefs (members): $(echo $WC)"
      tail -n +"$l0" "$R/somvr.log" | grep -E '^CAPTAIN member [0-9]+: .*warchief' | cut -c1-200
      k=0; for m in $WC; do [ $k -ge "${1:-4}" ] && break; k=$((k + 1))
        l1=$(lines); w "captain pick $m warchief" 0.8; w "practice captain" 8
        tail -n +"$l1" "$R/somvr.log" | grep -E '^CAPTAIN t=.* spawn ' | cut -c1-230
        # (boot 224213: two of four took the brand - the name found the previous, removed Captain's object: each one is
        # removed after its brand now, and the brand is asked twice)
        w "practice console AI.AddBrand SomVR_Practice" 4; w "practice console AI.AddBrand SomVR_Practice" 3
        pics "ladder-wc$m" 0.5; w "practice remove" 4
        tail -n +"$l1" "$R/somvr.log" | grep -E "console \(the test word\)|^HINTS tutorial #.*show|^STORY t=.*subtitle ch=[01] '|^CAPTAIN t=.*(brand|died|gone|left)|^PROMPTS show" |
          cut -c1-200 | head -8
      done; w "captain status" 2.5 ;;
    press) # press CMD MS N GAP: the stock command CMD (or CMD+CMD) held MS ms, N times, GAP s apart (the test word
      # `verbs press`); the tutorial lessons and the story lines that came, after each
      l0=$(lines); for i in $(seq 1 "${3:-1}"); do w "verbs press $1 ${2:-150}" "${4:-1.5}"; done
      tail -n +"$l0" "$R/somvr.log" | grep -E "^HINTS tutorial #[0-9]+ .*(show|hide)|^STORY t=.*subtitle ch=[01] '|^SESSION ui|^VERBS t=.*press" |
        cut -c1-160 | awk '!s[$0]++' | head -12 ;;
    wait) sleep "${1:-5}" ;;
    shot) pics "${1:-ladder-shot}" 1 ;;
    *) echo "unknown step $c" ;;
  esac
done
[ "${LADDER_NOQUIT:-0}" = 1 ] || { echo "$(date +%T) quit"; echo quit >> "$R/cmd"; }
