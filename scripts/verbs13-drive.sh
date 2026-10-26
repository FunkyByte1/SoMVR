#!/bin/bash
# verbs13-drive.sh - SOMVR_VERBS's proof (handoff/HANDOFF-verbs.md, docs/re-xr-pipeline.md 39): drives one headless boot
# (scripts/headless_vr.sh with scripts/headset-build13.env + SOMVR_VERBS, SOMVR_HANDS_TEST=1, the layer's test file).
# Prologue as execute-drive.sh's (the run, the user check, the words). VD_STEPS (default "discover"), in order:
#   e1        the VR grab (SOMVR_VERBS=on): the layer's left test hand (`hand L head`, `btn L squeeze`) reached out at the orc's head and squeezed: held = B held
#             = the game's hold (the drain) to its end, a quick squeeze = the stun, let go mid-hold = its out; a squeeze at
#             the hip = nothing; marks before, during, after
#   e2        the stealth kill (SOMVR_VERBS=on): a patrol orc 12 m off (verbs ahead/template), Talion teleported 1.3 m
#             behind it (verbs behind), the sword drawn and thrust at its back: the game's offer or not, marks
#   d3        the game's own selection (verbs tree) at an aware, a stunned, a held and an unaware (patrol) orc
#   d2        the hold to its end, a stunned orc's B and X, the stealth approach staged by `verbs behind CM`
#   g1        the wraith glow (SOMVR_VERBS_GLOW, re-xr-pipeline.md 39.7): the grab, its drain by the trigger, the pull to
#             the face (the interrogation), its Eliminate, then the open hand's hold at a new orc's head; marks and the
#             layer's dumps at each moment (names $GT-..., g1 first)
#   gwait     waits (up to 10 min) for RUN/glow-words, then `vbglow fx WORDS` + `vbglow hide`; the next g1's names gh-...
#             (VD_STEPS="g1 gwait g1": the game's look and the fix at the same moments in one boot)
#   discover  the flat game's buttons pressed by the test word `verbs press CMD MS` at a practice infantry: the stun (B
#             120), a hold of B, X (118) and Y (119) on a stunned orc; the game's prompts, Talion's and the orc's records
#             by name (the VERBS lines) - what the game offers and on which command.
# VD_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${VD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${VD_STEPS:-discover}
GT=${VD_GT:-g1}   # g1's mark names
T0=$(date +%s)
R=${VD_RUN:-}
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
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
k() { echo "key $1" >> "$R/cmd"; echo "$(date +%T) key $1"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hand L head -22 -40 15 0 0 0"     # the layer's left hand at the hip (cm right/up/ahead of the head, deg)
LREACH="hand L head -3 -3 50 0 90 0"     # ... reached out ahead at an orc's head, the palm turned forward
LFACE="hand L head -3 -8 24 0 90 0"      # ... pulled in to the face (24 cm ahead of the eyes)
spawn() { w "practice spawn ${1:-infantry}" 3
  if grep -E '^PRACTICE t=.*spawn ' "$R/somvr.log" | tail -1 | grep -q 'FAILED'; then
    echo "$(date +%T) the spawn failed: back to the practice area"; w "practice go" 6; w "practice spawn ${1:-infantry}" 3
  fi; }
near() { local i; for i in $(seq 1 ${1:-20}); do w "execute where" 0.4
    d=$(grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1 | grep -oE 'dist [0-9]+' | cut -d' ' -f2)
    [ -n "$d" ] && [ "$d" -lt "${2:-200}" ] && return 0; w "execute face" 0.3; done; return 1; }
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
DRAWN=0   # (the slot's squeeze toggles: draw once)
draw_slot() { [ "$DRAWN" = 1 ] && return 0; DRAWN=1; w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
# the layer's left hand reached out 50 cm at the nearest orc's bearing (`execute where`: from the head, + = right)
reach() { local l b hx hz ry; w "execute where" 0.3; l=$(grep -E '^EXECUTE where ' "$R/somvr.log" | tail -1)
  b=$(echo "$l" | grep -oE 'bearing [+-]?[0-9]+' | awk '{print $2}'); b=${b:-0}
  read -r hx hz ry <<< "$(awk -v b="$b" 'BEGIN { r = b * 3.14159265 / 180; printf "%.1f %.1f %.0f", 50 * sin(r), 50 * cos(r), 90 - b }')"
  x "hand L head $hx -3 $hz 0 $ry 0"; }
# the grab with the test palm at the nearest orc's collar: up to 4 tries (the orc circles in and out of reach)
grab_try() { local i n0; w "verbs aim head" 0.5; for i in 1 2 3 4 5 6; do near 25 130
    n0=$(cnt '^VERBS t=[0-9.]* grab: '); x "btn L squeeze 1"; sleep 0.4
    [ "$(cnt '^VERBS t=[0-9.]* grab: ')" -gt "$n0" ] && return 0
    x "btn L squeeze 0"; w "execute face" 0.3; done; echo "no grab"; return 1; }
# the stun by the open hand's quick trigger at the orc's head: up to 4 tries (the game's stun record PL_St_*)
stun_try() { local i n0; for i in 1 2 3 4; do near 25 120; w "execute face" 0.3; w "verbs aim head" 0.5
    n0=$(cnt "Talion's 'PL_St_"); w "hands trigger L 1" 0.3; w "hands trigger L -1" 1.2; w "verbs aim off" 0.5
    [ "$(cnt "Talion's 'PL_St_")" -gt "$n0" ] && return 0; sleep 0.6; done; echo "no stun"; return 1; }
stun() { w "execute face" 0.3; w "verbs press 120 150" 1.2; w "verbs status" 0.8; }

w "$REST" 0.5
x "$LREST"   # the layer's left hand at the hip, its palm in (headless Monado's own left hand opened the wrist panel)
for s in $STEPS; do
  case $s in
  discover)
    echo "== discover: the flat game's buttons at an infantry"
    w "verbs words test" 0.5   # every new prompt's text = "VR " + its StringId: does the HUD print a missing id as is?
    spawn infantry; near 25 220 || echo "the orc did not come"
    m "d1-before" 0.2
    stun; m "d1-stunned" 0.1
    w "verbs press 120 2500" 3.0; w "verbs status" 1.0; m "d1-hold-b" 0.1
    w "practice remove" 2; spawn infantry; near 25 220
    stun; w "verbs press 119 150" 2.0; w "verbs status" 1.5; m "d2-y" 0.1
    w "verbs press 270 150" 1.5; w "verbs press 264 150" 1.5; w "verbs status" 1
    w "practice remove" 2; spawn infantry; near 25 220
    stun; w "verbs press 118 150" 2.5; w "verbs status" 1; m "d3-x" 0.1
    w "practice remove" 2; spawn infantry; near 25 220
    stun; w "verbs press 160+120 2500" 3.0; w "verbs status" 1; m "d4-rt-b" 0.1
    w "practice remove" 2
    ;;
  d2)
    echo "== d2: the hold to its end, a stunned orc's B and X, the stealth approach (X, B, then Y / B held / X)"
    spawn infantry; near 25 220
    w "execute face" 0.3; w "verbs press 120 7000" 7.5; w "verbs status" 1; m "d2a-hog-end" 0.1
    w "practice remove" 2; spawn infantry; near 25 200
    w "execute face" 0.3; w "verbs press 120 150" 0.5; w "verbs press 120 6000" 6.5; w "verbs status" 1
    w "practice remove" 2; spawn infantry; near 25 200
    w "execute face" 0.3; w "verbs press 120 150" 0.4; w "verbs press 118 150" 0.4; w "verbs press 118 150" 0.4
    w "verbs press 118 150" 2; w "verbs status" 1
    behind() { w "practice remove" 2; spawn infantry; w "verbs status" 1.5; w "verbs behind 110" 1.5; w "execute face" 0.6
      w "verbs status" 0.5; }
    w "verbs ahead 1200" 0.3; w "verbs press 15 150" 1.0
    behind; m "d2d-behind" 0.1; w "verbs press 118 150" 3; w "verbs status" 1; m "d2d-stealth-x" 0.1
    behind; w "verbs press 120 150" 1.5; w "verbs status" 0.5; m "d2e-grab" 0.1
    w "verbs press 119 150" 2.5; w "verbs status" 1; m "d2e-grab-y" 0.1; w "verbs press 270 150" 1.5; w "verbs status" 1
    behind; w "verbs press 120 150" 1.5; w "verbs press 120 6000" 6.5; w "verbs status" 1; m "d2f-grab-hold" 0.1
    behind; w "verbs press 120 150" 1.5; w "verbs press 118 150" 3; w "verbs status" 1; m "d2g-grab-x" 0.1
    behind; w "verbs press 120 150" 1.5; sleep 6; w "verbs status" 1; m "d2h-grab-wait" 0.1
    w "verbs ahead 350" 0.3; w "practice remove" 2
    ;;
  d3)
    echo "== d3: the game's own selection (verbs tree) at an aware, a stunned, a held and an unaware orc"
    spawn infantry; near 25 220; w "execute face" 0.3; w "verbs tree" 1.5
    w "verbs press 120 150" 0.9; w "verbs tree" 1.5
    near 25 200; w "execute face" 0.3; w "verbs press 120 2500" 1.6; w "verbs tree" 1.5
    w "practice remove" 2; w "verbs ahead 1200" 0.3; w "verbs template Orc_Infantry_Patrol" 0.3; spawn infantry
    w "verbs status" 2.5; w "verbs behind 300" 1.2; w "verbs tree" 1.5; m "d3-behind300" 0.1
    w "verbs behind 150" 1.0; w "verbs tree" 1.5; w "verbs press 118 150" 2.5; w "verbs status" 1; m "d3-x-behind" 0.1
    w "practice remove" 2; spawn infantry; w "verbs status" 2.5; w "verbs behind 150" 1.0; w "verbs press 120 150" 2.5
    w "verbs status" 1; m "d3-b-behind" 0.1; w "verbs tree" 1.5
    w "verbs template off" 0.3; w "verbs ahead 350" 0.3; w "practice remove" 2
    ;;
  g1)
    echo "== g1 ($GT): the wraith glow at the grab, its drain, the pull, the interrogation, the open hand's hold"
    draw_slot; w "$REST" 0.6
    spawn infantry; x "$LREACH"
    grab_try; w "verbs status" 0.4; m "$GT-grab" 0.1; x "dump"
    w "hands trigger L 1" 0.6; x "dump"; m "$GT-drain" 0.1; x "dump"; w "hands trigger L -1" 0.4; m "$GT-drained" 0.1
    w "verbs aim face" 0.5; x "$LFACE"; sleep 0.6; x "dump"; m "$GT-ask" 0.3; x "dump"; w "verbs status" 0.4
    m "$GT-choice" 0.1; x "dump"
    x "btn R b 1"; x "btn R b 0"; sleep 1.0; x "dump"; m "$GT-kill" 0.1; w "verbs status" 0.4
    x "btn L squeeze 0"; w "verbs aim off" 0.5; sleep 2; m "$GT-after" 0.1
    w "practice remove" 2; spawn infantry
    near 25 120; w "execute face" 0.3; w "verbs aim head" 0.5; w "hands trigger L 1" 0.6; x "dump"; m "$GT-hold" 0.1
    x "dump"; sleep 0.5; m "$GT-held" 0.1; w "hands trigger L -1" 0.8; w "verbs aim off" 0.5
    w "practice remove" 2
    ;;
  gwait)
    echo "== gwait: waiting for $R/glow-words (the FX name words to hide)"
    for i in $(seq 1 240); do userin && bail "the user is in VR"; [ -s "$R/glow-words" ] && break; sleep 2.5; done
    [ -s "$R/glow-words" ] || bail "no glow-words"
    w "vbglow fx $(head -1 "$R/glow-words")" 0.5; w "vbglow hide" 0.5; GT=gh
    ;;
  e1)
    echo "== e1: the VR grab, the wraith hand, the interrogation (the test palm at the orc's collar: verbs aim)"
    draw_slot; w "$REST" 0.6   # the sword drawn, held back behind the hip (no contact by accident)
    # a: the squeeze = the grab (123 held: the view eases back from the orc's head); the trigger = B (the grab's drain);
    #    pulled to the face = A (the interrogation); its choice: the trigger = Interrogate; the intel menu left by B
    spawn infantry; x "$LREACH"; m "e1a-reach" 0.1
    grab_try; w "verbs status" 0.4; m "e1a-grab" 0.1; x "dump"   # (the grab's prompts: the band)
    w "hands trigger L 1" 0.6; m "e1a-drain" 0.1; w "hands trigger L -1" 0.4; m "e1a-drained" 0.1
    w "verbs aim face" 0.5; x "$LFACE"; sleep 0.6; m "e1a-ask" 0.3; w "verbs status" 0.4; m "e1a-choice" 0.1; x "dump"
    x "$LREACH"; w "hands trigger L 1" 0.4; w "hands trigger L -1" 1.5; m "e1a-interrogate" 0.5; x "dump"; w "verbs status" 1
    x "btn R b 1"; x "btn R b 0"; sleep 1; x "btn R b 1"; x "btn R b 0"; sleep 1   # the intel menu: B = the pad's Back
    x "btn L menu 1"; x "btn L menu 0"; sleep 1.5; w "verbs status" 1               # ... the menu button = its Exit
    x "btn L squeeze 0"; w "verbs aim off" 0.5; sleep 2; m "e1a-after" 0.1
    # b: a new orc: grab, hold 2.5 s (the game puts it in front), pull, the choice by the sword = Eliminate
    w "practice remove" 2; spawn infantry; grab_try; sleep 2.5; w "verbs aim face" 0.5; sleep 4.5; w "verbs status" 0.4
    w "verbs aim head" 0.5; w "hands swing R 5 -30 55 -40 0 0 250" 0.6; m "e1b-eliminate" 0.3; w "$REST" 0.5
    sleep 2; m "e1b-after" 0.1; x "btn L squeeze 0"; w "verbs aim off" 0.5; sleep 1
    # c: the open hand's trigger tap at the head = B tapped (the stun); d: held = B held (the combat drain) to its end
    w "practice remove" 2; spawn infantry; stun_try; m "e1c-stun" 0.1
    near 25 120; w "execute face" 0.3; w "verbs aim head" 0.5; w "hands trigger L 1" 0.6; m "e1d-hold" 0.1; sleep 0.5
    m "e1d-done" 0.1; w "hands trigger L -1" 0.8
    # e: the grab, then the sword's thrust at the held orc = X (the game's move from the grab)
    grab_try; sleep 1.5; w "hands swing R 5 -12 75 0 0 0 250" 0.6; m "e1e-kill" 0.2; w "$REST" 0.5; sleep 1.5
    m "e1e-after" 0.1; x "btn L squeeze 0"; w "verbs aim off" 0.5; sleep 1.0
    # f: the grab, then the Touch's X (the prompt's letter) = the grab's move
    grab_try; sleep 1.5; x "btn L a 1"; x "btn L a 0"; m "e1f-x" 0.2; sleep 1.5; x "btn L squeeze 0"; w "verbs aim off" 0.5
    x "$LREST"; x "btn L squeeze 1"; x "btn L squeeze 0"   # a squeeze at the hip: nothing
    w "practice remove" 2
    ;;
  e3)
    echo "== e3: the stun by a trigger tap (SOMVR_VERBS_TAP_S as booted), the Eliminate choice, the grab's X (sword, button)"
    draw_slot; w "$REST" 0.6; spawn infantry; stun_try; m "e3a-stun" 0.1
    grab_try; w "verbs aim off" 0.5; x "$LREACH"; sleep 2; w "verbs aim face" 0.5; sleep 4.5; w "verbs status" 0.4
    w "verbs aim off" 0.5; w "hands swing R 0 -45 60 0 0 0 250" 0.6; m "e3b-eliminate" 0.3; w "$REST" 0.5
    w "hands swing R 5 -30 70 0 0 0 250" 0.6; w "$REST" 0.5; sleep 2; m "e3b-after" 0.1   # (a second thrust)
    [ "$(cnt 'choice: the blade')" = 0 ] && { x "btn R b 1"; x "btn R b 0"; sleep 2; }  # (else the Touch's B: Eliminate)
    x "btn L squeeze 0"; sleep 1
    w "practice remove" 2; spawn infantry
    grab_try; w "verbs aim off" 0.5; x "$LREACH"; sleep 1.5; w "hands swing R 5 -25 60 0 0 0 250" 0.6; m "e3c-kill" 0.2
    w "$REST" 0.5; sleep 1.5; m "e3c-after" 0.1; x "btn L squeeze 0"; sleep 1.0
    grab_try; w "verbs aim off" 0.5; sleep 1.5; x "btn L a 1"; x "btn L a 0"; m "e3d-x" 0.2; sleep 1.5; x "btn L squeeze 0"
    w "practice remove" 2
    ;;
  e4)
    echo "== e4: the pull to the face (the interrogation) after a plain grab; then the sword = Eliminate"
    draw_slot; w "$REST" 0.6; x "$LREACH"; spawn infantry
    grab_try; w "verbs aim off" 0.5; x "$LREACH"; sleep 2.5; w "verbs aim face" 0.5; sleep 4.5; w "verbs status" 0.4
    w "verbs aim head" 0.5; w "hands swing R 5 -30 55 -40 0 0 250" 0.6; m "e4-eliminate" 0.3; w "$REST" 0.5; sleep 1.5
    x "btn R b 1"; x "btn R b 0"; sleep 2; m "e4-after" 0.1; x "btn L squeeze 0"; w "verbs aim off" 0.5; sleep 1
    w "practice remove" 2
    ;;
  e2)
    echo "== e2: the stealth kill: crouched, a patrol orc 12 m off, Talion put 3 m behind it, the sneak in, the thrust"
    draw_slot; w "$REST" 0.6
    w "verbs press 15 150" 1.0   # crouch (the sneak's predicates: run 233249 offered the stealth grab, not the kill)
    for r in 1 2 3; do
      w "verbs ahead 1200" 0.3; w "verbs template Orc_Infantry_Patrol" 0.3; spawn infantry; w "verbs status" 3
      w "verbs behind 300" 0.8; w "execute face" 0.6; w "stick 0 0.35 0 2.5" 2.8; w "execute face" 0.4
      w "verbs tree" 1.2; m "e2-$r-behind" 0.1
      w "hands swing R 5 -12 75 0 0 0 250" 0.6; m "e2-$r-stab" 0.3; w "$REST" 0.5; sleep 1.5; m "e2-$r-after" 0.1
      w "practice remove" 2
    done
    w "verbs template off" 0.3; w "verbs ahead 350" 0.3
    ;;
  esac
done
w "verbs status" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
