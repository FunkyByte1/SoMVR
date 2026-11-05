#!/bin/bash
# struggle41-ioreth-drive.sh - build 41 (som-b41-struggle, docs/re-xr-pipeline.md 180): the Tower's last beat at Ioreth
# (Lord of Mordor, the user's critical path) - a CStruggleHold of B (120 'CombatPrimary03') for 2 s inside its window,
# found by som-b41-nurn (docs 181, boot 20261105-165235: lost there x4 with the drain hand, B held as an aid and the left
# grip). Run by scripts/ladder_boot.sh from rung 19 (that run's own end save loads in the open world, not the Tower:
# boot 20261105-175120): scripts/ladder_boot.sh 19 struggle41-ioreth-drive.sh
# IO_FULL=1 (default): the mission played to Ioreth first, nurn41's way (its run's ctl files) - the start (ladder-drive.sh:
# the prep, the card, A, the video), the battle with test aids (mortal off, CanBeAttacked 0, the nemesis' health 5 and
# a cut; the warchiefs' infight) until the 'BlackCaptains' marker, there, the video, the three stealth drains of the
# Tower's incarnations (behind, crouched, the left hand on the head + the trigger), then Ioreth; the last chances on the
# way answered by the rule's test hands (`struggle auto on 0.4`: the outline's stab).
# Each try: Quest.FailActiveQuest -> Retry (the checkpoint after the drains; nurn41's way), invisible, to 4600 1890
# -8600, the stick ahead until the struggle starts; then the answer:
#   IO_TRIES (default "b27 b41 b41"): the same act each try - the left hand open at the start, then a fresh fist kept
#   closed (the left controller's grip pressed 0.4 s after the start and held: the layer's test button); b27 = build
#   27's rule (no answer: the baseline, lost), b41 = the rule's hold; with
#   IO_DRAIN=1 (default) the drain's trigger is held from before the start (the drain hand on him: B 120 held by the
#   verbs since before - the rule keeps 120 off until the fist, so the class still gets a fresh press).
# Pictures at the cue (the ring at its node) and after; lines STRUGGLE (b41 ...); io-steps.txt (PASS/FAIL).
R=${LADDER_RUN:-${ST_RUN:-}}; [ -d "$R" ] || { echo "no run (LADDER_RUN)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/nurn41/lib.sh"
ST=$R/io-steps.txt
note() { echo "$(date +%T) $*" | tee -a "$ST"; }
for i in $(seq 1 150); do userin && bail "the user is in VR"; gone && bail "the game is gone"
  [ "$(cnt '^SESSION ui ifs=3 menus=0 paused=0 video=0 ctx=world')" -ge 2 ] && break; sleep 3; done
note "in the world: $(state)"; sleep 4
hands0
w "struggle auto on 0.4" 1; w "struggle status" 1
grep -aE '^CONFIG struggle build 41' "$L" | cut -c1-300 | tee -a "$ST"
# a death on the way: the screens answered (A, B), back in the world
deaths() { local k; for k in $(seq 1 6); do inworld && ! win && return 0; state | grep -q 'paused=1' && tap R a 0.3; win && { tap R a 0.4; sleep 2; }
  state | grep -q 'video=1' && { tap R a 0.3; sleep 0.7; tap R b 0.3; }; sleep 2; done; }
if [ "${IO_FULL:-1}" = 1 ]; then
  LADDER_STEPS=start LADDER_NOQUIT=1 bash "$HERE/ladder-drive.sh" 2>&1 | grep -vE ' (word|tap) ' | tail -12 | tee -a "$ST"
  inworld || { sleep 5; inworld || note "after the start: $(state)"; }
  note "== the battle (aids: mortal off, CanBeAttacked 0, the nemesis' health 5 + a cut)"
  w "practice mortal off" 0.6; w "practice console Player.CanBeAttacked 0" 2; w "fight guard learn" 0.5; draw
  B0=$(cnt "'BlackCaptains': shown by the game"); T=$((SECONDS + ${IO_BATTLE_S:-600})); nk=0
  while [ $SECONDS -lt $T ]; do userin && bail "the user is in VR"; gone && bail "the game is gone"
    [ "$(cnt "'BlackCaptains': shown by the game")" -gt "$B0" ] && break
    deaths; ptut
    nk=$((nk + 1)); C=$(capat)
    if [ -n "$C" ] && [ $((nk % 2)) = 1 ]; then set -- $C; behind 110 "$1" "$3"; w "execute face" 0.4; w "captain hp 5" 0.6; slash; slash; thrust; fi
    z 20; done
  [ "$(cnt "'BlackCaptains': shown by the game")" -gt "$B0" ] || { note "FAIL the battle: no 'BlackCaptains' marker in ${IO_BATTLE_S:-600} s ($(state))"; obj 0 6 | tee -a "$ST"; saveas io-battle; echo quit >> "$R/cmd"; exit 1; }
  note "the battle won: the Tower's marker"; pics io-battle-won
  go 4592 2045 -7256; z 6; for nn in $(seq 1 20); do state | grep -qE 'video=1|paused=1' && break; win && break; z 1; done; endphase 3; z 3
  note "== the Tower: three stealth drains ($(state))"; invis on; D0=$(cnt "Talion attack 'PC_BH2_StealthDrain")
  for nt in $(seq 1 12); do [ $(( $(cnt "Talion attack 'PC_BH2_StealthDrain") - D0 )) -ge 3 ] && break; deaths; l1=$(lines)
    w "verbs orcs 5000 -6500 5000" 1.3 >&2
    I=$(since $l1 "^VERBS orcs t=.* [0-9]+/" | grep "attack '[A-Za-z_]*BH" | grep -v DrainPC | head -1 | sed -E 's/.* orc (0x[0-9a-f]+) at (-?[0-9]+),(-?[0-9]+),(-?[0-9]+) .*/\1 \2 \4/')
    [ -n "$I" ] || { z 2; continue; }
    set -- $I; crouch; behind 90 "$2" "$3"; sleep 0.6; hold L y; w "verbs aim head" 0.2; hold L trigger; z 4; release L trigger; w "verbs aim off" 0.2; release L y; z 2
    note "drain try $nt: drains $(( $(cnt "Talion attack 'PC_BH2_StealthDrain") - D0 ))"; done
  [ $(( $(cnt "Talion attack 'PC_BH2_StealthDrain") - D0 )) -ge 3 ] || note "(fewer than 3 drains)"
  pics io-drained; obj 0 4 | tee -a "$ST"
fi
WON=0; k=0
for mode in ${IO_TRIES:-b41 b41 b41}; do
  k=$((k + 1)); note "== try $k: rule $mode, the drain's trigger held from before: ${IO_DRAIN:-1}"
  w "struggle rule $mode" 1
  if [ $k -gt 1 ] || [ "${IO_FULL:-1}" != 1 ]; then # (a retry: the checkpoint after the drains - nurn41's cmd21)
    w "practice console Quest.FailActiveQuest" 3; for nw in $(seq 1 20); do win && break; sleep 0.5; done
    tap R a 0.4; for nw in $(seq 1 60); do inworld && ! win && break; sleep 1; done; z 4; fi
  invis on; go 4600 1890 -8600; z 1; l1=$(lines)
  [ "${IO_DRAIN:-1}" = 1 ] && { w "verbs aim head" 0.2; hold L trigger; }
  x "stick L 0 0.6"
  for nw in $(seq 1 75); do since $l1 '^STRUGGLE t=.* start' | grep -q . && break; sleep 0.2; done; x "stick L 0 0"
  since $l1 '^STRUGGLE t=.* start' | grep -q . || { note "FAIL try $k: no struggle ($(state))"; release L trigger; w "verbs aim off" 0.2; obj $l1 4 | tee -a "$ST"; continue; }
  since $l1 '^STRUGGLE t=.*(start|b41: )' | head -2 | cut -c1-300 | tee -a "$ST"
  # (the act: the test hands' fresh fist - `struggle auto`: the left hand open at the start, closed 0.3 s later, kept)
  for nw in $(seq 1 40); do since $l1 '^STRUGGLE t=.*b41 the hold: .* closed|^STRUGGLE t=.* over after' | grep -q . && break; sleep 0.25; done
  sleep 1.0; pics io$k-hold
  for nw in $(seq 1 60); do since $l1 '^STRUGGLE t=.* over after' | grep -q . && break; sleep 0.25; done
  pics io$k-after; release L trigger; w "verbs aim off" 0.2; w "hands squeeze L 0" 0.2; z 3; pics io$k-later
  since $l1 '^STRUGGLE t=.*(b41 (the hold|over|the cue)|over after|its command now|result changed)|^RIDE t=.*struggle' | head -12 | cut -c1-320 | tee -a "$ST"
  since $l1 "Talion attack '[A-Za-z_0-9]*'|attack 'PC_BH2[A-Za-z_]*'|AI_BH2_[A-Za-z_]*(PLLose|PLWin)[A-Za-z_]*|subtitle ch=[01] .[A-Z]" | head -6 | cut -c1-200 | tee -a "$ST"
  if since $l1 '^STRUGGLE t=.*b41 over: .* WON' | grep -q .; then note "PASS try $k ($mode): the Ioreth hold won"; WON=$((WON + 1))
    break # (won: the story moves on - no retry from here)
  else note "FAIL try $k ($mode): the Ioreth hold lost"; fi
done
z 8; pics io-end; endphase 2; obj 0 6 | tee -a "$ST"
note "won $WON"; grep -aE '^STRUGGLE END' "$L" | cut -c1-700 | tee -a "$ST"
saveas io-end
w "endlogs" 2; sleep 2; echo quit >> "$R/cmd"; echo "$(date +%T) quit"
