#!/bin/bash
# surface13-drive.sh - SOMVR_HANDS_SURFACE=on (docs/re-xr-pipeline.md 38, handoff/HANDOFF-surfaces.md): the hands and
# the sword against the stone banks west of the practice arrival point, the ground and a practice orc; one headless boot
# with scripts/headset-build13.env + SOMVR_HANDS_SURFACE=on, the test hands (SOMVR_HANDS_TEST=1) and the layer's test
# file (SOMVR_XR_TEST=@RUN@/xrtest.txt); marks with both eyes. SF_STEPS = any of (in this order; default all):
#   wall    the nearest stone bank found (`climb probe`, climb-drive.sh's way), faced and walked to (~60 cm); the left
#           hand pushed 20 and 40 cm into it (marks), a quick push (the tick), out again (the release)
#   ground  the left hand down to 20 cm into the ground (mark), up again
#   sword   the sword drawn (at the shoulder); the blade raised and turned onto the bank (the hand held back, mark), a
#           thrust into it (the clank), pointing down into the ground (the hand lifted, mark), past the void (in)
#   orc     a practice infantry: lunges at it until a hit lands (COMBAT hit lines: the hits as before), the left hand
#           into it (its flesh: passed)
#   bow     the test hands off; the bow summoned (the left trigger), nocked, drawn, let down (BOW lines: the bow draws;
#           SURF except: the bow)
# SF_AT="X Y Z": first taken there (practice back, practice go; default the stone pillar of build 9's wall-top bug,
# 33350 2000 -27900: bare stone, no ivy; "" = stay at the arrival point). Each picture step is a stereo dump with our
# hands' layer (RUN/surf-steps.txt: name and frame; compose with scripts/xr_compose.py RUN -f N) and a mark.
# Stops at once (quit) when the user goes into VR. SF_WT: the worktree (default: the one this script is in).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${SF_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${SF_STEPS:-wall ground sword orc bow}
AT=${SF_AT-33350 2000 -27900}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
m() { sleep "${2:-0.3}"; w "mark $1" 4; }
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
check() { if grep -qE "$2" "$R/somvr.log"; then echo "$(date +%T) PASS $1"; else echo "$(date +%T) FAIL $1"; fi; }
# d NAME: a stereo dump now (both eyes + our hands' layer), noted in RUN/surf-steps.txt; md NAME: the dump and a mark
d() { t "dump"; sleep 1.2; local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      echo "$1 $f" | tee -a "$R/surf-steps.txt"; }
md() { d "$1"; m "$1" 0; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
surf() { grep -E '^SURF t=.* (clamp|hold|release|blade|ghost|except)' "$R/somvr.log" | tail -${1:-4} | cut -c1-260; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# climb-drive.sh's probe, near, ahead, turnto
probe() { local n0 i; n0=$(grep -c '^CLIMB probe surfaces' "$R/somvr.log"); w "climb probe${1:+ $1}" 0.3
  for i in $(seq 1 24); do [ "$(grep -c '^CLIMB probe surfaces' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.25; done
  grep '^CLIMB probe' "$R/somvr.log" | tail -$([ -n "$1" ] && echo 2 || echo 5) | cut -c1-300; }
near() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | sed 's/.*the distance (cm)://' | tr ' ' '\n' |
  grep -E '[Ch]$' | sed 's/[Ch]$//; s/=/ /' | sort -k2 -n | head -1; }
ahead() { grep "^CLIMB probe t=.*at head $1 cm" "$R/somvr.log" | tail -1 | grep -oE ' \+0=[0-9]+[Chn]' | sed -E 's/ \+0=([0-9]+).*/\1/'; }
turnto() { local b=$1 n; n=$(( (${b#+} + (${b#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
# the sword (combat11-drive.sh's)
drawn() { local l; l=$(grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' "$R/somvr.log" | tail -1) # (the whole log: 800 lines no longer cover a step)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
REST="hands test R 28 -40 20 0 0 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
lunge() { w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; }
# 0. the controls card away, both test squeezes open, both hands low
x0() { printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep 0.8; }
x0 "btn R squeeze 0"; x0 "btn R squeeze 1"; sleep 1; x0 "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { x0 "btn R a 1"; x0 "btn R a 0"; }
w "hands squeeze L 0"; w "hands squeeze R 0"
w "hands test L -25 -45 15 0 0 0"; w "$REST" 1
check "the switch's config" '^CONFIG surface=on'
grep -E '^CONFIG surface' "$R/somvr.log" | cut -c1-300
m s00-start 0.3
if [ -n "$AT" ] && { has wall || has sword; }; then
  w "practice back" 1
  waitfor '^PRACTICE t=.* arrived at' 30 || echo "$(date +%T) no arrival back"
  sleep 2
  w "practice go $AT" 1
  waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 30
  grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-220
  sleep 3
fi
if has wall || has sword; then
  probe
  NB=$(near -50); [ -n "$NB" ] || NB=$(near +0)
  echo "$(date +%T) nearest wall: ${NB:-none}"
  [ -n "$NB" ] && turnto "${NB% *}"
  probe >/dev/null
  PREV=""
  for k in $(seq 1 25); do
    d=$(ahead -50); d0=$(ahead +0); [ -n "$d0" ] && { [ -z "$d" ] || [ "$d0" -lt "$d" ]; } && d=$d0
    [ -n "$d" ] || { NB=$(near -50); echo "$(date +%T) nothing ahead; nearest ${NB:-none}"; [ -n "$NB" ] && turnto "${NB% *}"; probe >/dev/null; continue; }
    echo "$(date +%T) ahead $d cm"
    [ "$d" -le 62 ] && break
    if [ "$d" -gt 500 ]; then w "stick 0 1 0 2" 2.4; elif [ "$d" -gt 160 ]; then w "stick 0 0.7 0 0.8" 1.2; else w "stick 0 0.6 0 0.6" 1.0; fi
    probe >/dev/null
    [ -n "$PREV" ] && [ "$d" -ge "$((PREV - 3))" ] && [ "$d" -le 90 ] && { echo "$(date +%T) stopped at $d cm (the wall)"; break; }
    PREV=$d
  done
  D=${d:-60}
  echo "$(date +%T) WALL at $D cm ahead (the view's head)"
  md s01-at-wall
fi
if has wall; then
  # the left hand open, at shoulder height: up to the wall, 20 cm in, 40 cm in, out
  for z in $((D - 25)) $((D + 20)) $((D + 40)); do
    w "hands test L -20 -15 $z 0 0 0" 1.2
    surf 2
    md "s02-left-z$z"
  done
  w "hands test L -20 -15 $((D - 30)) 0 0 0" 1.2
  surf 2
  # a quick push (300 ms) into the bank: the contact's tick
  w "hands swing L -20 5 $((D + 25)) 0 0 0 300" 1.5
  surf 3
  w "hands test L -20 5 $((D - 30)) 0 0 0" 1.2
  check "a hand stopped at the wall" '^SURF t=.* clamp hand=L depth='
  check "the ghost" '^SURF t=.* ghost hand=L on'
  check "a release" '^SURF t=.* release hand=L'
fi
if has ground; then
  w "hands test L -25 -120 25 0 0 0" 1.0
  w "hands test L -25 -185 25 0 0 0" 1.5
  surf 2
  md s03-left-ground
  w "hands test L -25 -45 15 0 0 0" 1.0
  check "a hand stopped at the ground" '^SURF t=.* clamp hand=L .* n=-?[0-9.]+,(0\.[5-9][0-9]|1\.00),'
fi
if has sword; then
  drawn || draw_slot
  sleep 1
  drawn && echo "$(date +%T) the sword is drawn" || echo "$(date +%T) the sword is NOT drawn"
  # the blade pitched 45 deg (up, then down) at the bank, its tip ~11 cm in: the hand held back
  for p in "25 -25 $((D - 60)) -45 0 0" "25 -25 $((D - 60)) 45 0 0"; do
    w "hands test R $p" 1.5
    surf 2
  done
  md s04-sword-on-bank
  # a thrust (200 ms) from the tip 30 cm short to 10 cm in: the clank
  w "hands test R 25 -25 $((D - 130)) 0 0 0" 1.2
  w "hands swing R 25 -25 $((D - 90)) 0 0 0 200" 1.5
  surf 3
  md s05-sword-thrust
  # straight at it, the tip 40 cm in: deeper than the void (25): the blade goes in
  w "hands test R 25 -25 $((D - 60)) 0 0 0" 1.2
  surf 2
  md s06-sword-void
  # pointing down into the ground: the hand lifted
  w "hands test R 25 -40 20 0 0 0" 1.0
  for rx in 90 -90; do w "hands test R 25 -80 20 $rx 0 0" 1.5; surf 2; done
  md s07-sword-ground
  w "$REST" 1
  check "the blade stopped" '^SURF t=.* blade (clank|touch) depth='
  check "a clank" '^SURF t=.* blade clank'
fi
if has orc; then
  w "stick 0 -1 0 1.5" 2.2   # back from the bank
  drawn || draw_slot
  H0=$(cnt '^COMBAT hit ')
  w "practice spawn infantry" 2
  for k in $(seq 1 14); do
    [ "$(cnt '^COMBAT hit ')" -gt "$H0" ] && break
    face; w "stick 0 1 0 0.35" 0.5; lunge
  done
  [ "$(cnt '^COMBAT hit ')" -gt "$H0" ] && echo "$(date +%T) PASS the sword's hits land ($(( $(cnt '^COMBAT hit ') - H0 )))" || echo "$(date +%T) FAIL no hit landed"
  grep -E '^COMBAT hit ' "$R/somvr.log" | tail -2 | cut -c1-260
  m s08-orc-hit 0.2
  # the left hand into the orc (ahead, chest height): its flesh is passed, no clamp
  C0=$(cnt '^SURF t=.* clamp hand=L')
  w "hands test L -5 -30 70 0 0 0" 1.5
  w "hands test L -5 -30 95 0 0 0" 1.5
  md s09-left-in-orc
  grep -E "^SURF t=.* hit obj=.*surf='(Flesh|Armor)" "$R/somvr.log" | head -3 | cut -c1-200
  C1=$(( $(cnt '^SURF t=.* clamp hand=L') - C0 ))
  [ "$C1" = 0 ] && echo "$(date +%T) PASS the orc stops no hand" || echo "$(date +%T) FAIL the left hand clamped $C1 times at the orc"
  w "hands test L -25 -45 15 0 0 0" 1.0
  w "$REST" 1
fi
if has bow; then
  w "hands test off" 1
  t "hand L head -22 -40 15 0 0 0" "hand R head 22 -40 15 0 0 0"
  pose() { awk -v a="$1" -v p="$2" -v d="$3" -v sm="${4:-}" -v L=60 'BEGIN { r = 3.14159265 / 180
    dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 7; ay = -13; az = 0
    bx = ax + L * dx; by = ay + L * dy; bz = az + L * dz; k = L - d
    printf "hand L head %.1f %.1f %.1f %.1f %.1f 0\n", bx, by, bz, p, -a
    printf "hand R head %.1f %.1f %.1f %.1f %.1f 0%s\n", ax + k * dx + 1.5, ay + k * dy, az + k * dz, p, -a, sm != "" ? " s " sm : "" }'; }
  setpose() { local IFS=$'\n'; t $(pose "$@"); }
  setpose 0 0 30; sleep 0.5
  t "btn L trigger 1"; sleep 1.5
  check "the bow summoned" '^BOW .* enter \(by the button\)'
  setpose 0 0 4; sleep 0.3; t "btn R squeeze 1"; sleep 1.5; check "nock" '^BOW .* nock: '
  setpose 0 0 57 0.8; sleep 1.2
  waitfor '^BOW .* (armed|full draw)' 2; check "the bow drawn" '^BOW .* (armed|full draw)'
  md s10-bow-drawn
  setpose 0 0 8 0.6; sleep 0.8; t "btn R squeeze 0"; sleep 0.6
  check "let-down" '^BOW .* let down'
  T_S=0.3 t "btn L trigger 0"; sleep 1.5
  check "the bow is an exception" '^SURF t=.* except hand=.: the bow is out'
fi
grep -E '^SURF t=.* last 10 s' "$R/somvr.log" | tail -1 | cut -c1-400
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
grep -E '^SURF END' "$R/somvr.log" | cut -c1-500
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
