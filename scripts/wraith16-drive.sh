#!/bin/bash
# wraith16-drive.sh - build 16's wraith round (docs/re-xr-pipeline.md 49, handoff/HANDOFF-wraith.md "build 16"): Y and
# the wraith vision (SOMVR_VISION=hold through somvr_vision.c), the wraith's copy of the held sword and bow
# (SOMVR_HP_COPIES). One headless boot with scripts/headset-build16.env + SOMVR_HANDS_TEST=1 and the layer's test file
# (SOMVR_XR_TEST=@RUN@/xrtest.txt); the trigger words "vision y MS" (Y held MS ms at the snapshot, before the bug
# mark's chord), "vision taps N MS" (raw taps of 122), "vision" (a state line), "hpcopies off|log|on". Steps (W16_STEPS,
# default all):
#   toggle  the game's own answer: two raw taps 60..1000 ms apart from off (when does it refuse the second?)
#   y       Y held 1.5 s, 0.5 s; the shortest press (1 ms: one tick through the chord); 60 ms; 200 ms; held, let go
#           0.1 s, held again (stays on, then off); a press with the vision on by the game's own (a raw tap): every
#           one must end off
#   ident   the sword in view, the vision held on, hpcopies log: each copy node (HEADPARTS copy lines) hidden alone
#           (hpskip) with a dump
#   copyid  the same in three sword poses (the vision held 60 s, ended by a press)
#   sheath  the sword put back over the shoulder, then the vision: hpcopies log / on (its glowing copies on the back)
#   bowid   the bow out at rest, a hand at the right shoulder, nocked, drawn: hpcopies log / on at each (dumps)
#   third   the sword sheathed, the vision on, from the game's camera (fallback third): where its copies glow
#   arrowid (the bow branch's quiver) an arrow carried: the nodes near the head (hpnear) each hidden alone, dumps
#   sword   the sword drawn and held in view, an orc in front: off / the vision with hpcopies log / on / log / on
#           (dumps: the wraith's see-through sword beside ours, then not; the orc's outline kept)
#   panel   Y pressed as the palm gesture opens the wrist panel (80 ms and 1.5 s): it must end off
#   bow     the bow out (the left grip behind the left shoulder, SOMVR_BOW_DRAW=slot), nocked and drawn (its wraith aim
#           view): hpcopies log / on (dumps)
# Dumps are named in RUN/w16-steps.txt (compose: python3 scripts/xr_compose.py RUN -f N). Stops at once (quit) when
# the user goes into VR. FB_WT: the worktree (default: the one this script is in).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${FB_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${W16_STEPS:-toggle y panel sword ident copyid sheath bowid bow outline}
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
# t LINE...: one step of the layer's test file (replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest: $*"; sleep "${T_S:-0.6}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
d() { local n0 f; n0=$(grep -c 'dump layers frame' "$R/xr-layer.log" 2>/dev/null); t "dump"; sleep 1.4
      f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$')
      [ "$(grep -c 'dump layers frame' "$R/xr-layer.log")" -gt "$n0" ] || f="$f (no new dump)"
      echo "$1 $f" | tee -a "$R/w16-steps.txt"; }
# the vision's flag now (on|off|unread), from the state word's line
flag() { local n0 i; n0=$(grep -cE '^VISION t=.* state: ' "$R/somvr.log"); w "vision" 0.1 >&2
  for i in $(seq 1 20); do [ "$(grep -cE '^VISION t=.* state: ' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.1; done
  grep -E '^VISION t=.* state: ' "$R/somvr.log" | tail -1 | sed -nE 's/.*, the flag ([a-z]+), .*/\1/p'; }
ensure_off() { local i f; for i in 1 2 3 4; do f=$(flag); [ "$f" = on ] || return 0; w "vision taps 1 30" 1.5 >&2; done
  echo "$(date +%T) WARN: the vision stays on"; }
lines_since() { tail -n +"$1" "$R/somvr.log" | grep -E '^VISION ' | cut -c1-260; }
# expect NAME: the vision ends off (the flag), its lines since L0 printed
expect_off() { local f; sleep "${2:-0}"; f=$(flag); lines_since "$L0" | tail -12
  if [ "$f" = off ]; then echo "$(date +%T) PASS $1 (the flag off)"; else echo "$(date +%T) FAIL $1 (the flag $f)"; fi; }
face() { local y n; y=$(grep -E '^THREAT (cue|warn) ' "$R/somvr.log" | tail -1 | grep -oE 'yaw=[+-]?[0-9]+' | cut -d= -f2)
  [ -n "$y" ] || return 0; n=$(( (${y#+} + (${y#+} < 0 ? -15 : 15)) / 30 ))
  while [ "$n" -gt 0 ]; do w "turn+" 0.6; n=$((n - 1)); done; while [ "$n" -lt 0 ]; do w "turn-" 0.6; n=$((n + 1)); done; }
drawn() { local l; l=$(tail -800 "$R/somvr.log" | grep -E '^SLOTS .*sword: .*(now in the hand|now sheathed|now on the back|kept in the hand|kept on the back|our keep undone)' | tail -1)
  case $l in *"kept on the back no more (kept in the hand now)"*) return 0 ;; *"kept on the back"*|*"now sheathed"*|*"now on the back"*) return 1 ;;
    *"our keep undone (kept away now)"*) return 1 ;; *"now in the hand"*|*"kept in the hand"*|*"our keep undone"*) return 0 ;; esac; return 1; }
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
HOLD="hands test R 22 -22 38 0 -40 0"      # the sword held out ahead and up, in view
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
L0=1

# 0. the controls card away, the left hand forward, the sword drawn
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away (the right grip squeezed)' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
w "$REST" 2

if has toggle; then
  echo "== toggle: two raw taps from off, the gap between them"
  for gap in 60 150 250 400 700 1000; do
    ensure_off; L0=$(($(wc -l < "$R/somvr.log") + 1))
    w "vision taps 2 $gap" 3
    echo "$(date +%T) gap $gap ms:"; lines_since "$L0" | grep -E 'flag [01] -> [01]|raw tap' | sed 's/^/    /'
  done
  ensure_off
fi

if has y; then
  echo "== y: Y through the chord into the vision"
  for ms in 1500 500 1 60 200; do
    ensure_off; sleep 1.2; L0=$(($(wc -l < "$R/somvr.log") + 1))
    w "vision y $ms" 0.2; expect_off "Y held $ms ms" $(awk -v m="$ms" 'BEGIN { printf "%.1f", m / 1000 + 2.5 }')
  done
  ensure_off; sleep 1.2; L0=$(($(wc -l < "$R/somvr.log") + 1))
  w "vision y 1500" 1.7; w "vision y 1500" 0.2   # let go ~0.1 s (the word's own lag) and held again
  sleep 1.0; f=$(flag); echo "$(date +%T) held again: the flag $f (on expected)"
  expect_off "held, let go briefly, held again" 2.5
  ensure_off; L0=$(($(wc -l < "$R/somvr.log") + 1))
  w "vision taps 1 30" 1.5   # the game's own vision (no wish of ours)
  f=$(flag); echo "$(date +%T) the game's vision: $f"
  w "vision y 300" 0.2; expect_off "a press with the game's vision on" 2.5
  grep -cE 'Y pressed with the vision on: off wished' "$R/somvr.log" | sed 's/^/    presses with the vision on so far: /'
fi

if has panel; then
  echo "== panel: Y as the palm gesture opens the wrist panel"
  w "hands test off" 1
  for ms in 80 1500; do
    ensure_off; L0=$(($(wc -l < "$R/somvr.log") + 1)); O0=$(grep -c 'vrmenu: opened by' "$R/xr-layer.log")
    w "vision y $ms" 0
    T_S=0.2 t "hand L head 5 -16 32 0 -90 50 s 0.4"; sleep 2.5
    [ "$(grep -c 'vrmenu: opened by' "$R/xr-layer.log")" -gt "$O0" ] && echo "$(date +%T) the panel opened" || echo "$(date +%T) the panel did not open"
    expect_off "Y $ms ms with the panel opening" 1
    t "hand L head 5 -16 32 0 -90 -130 s 0.3"; sleep 1; t "hand L off"; sleep 1.5
  done
fi

if has sword; then
  echo "== sword: the wraith's copy of the held sword (no orc near: a fight ends the vision)"
  drawn || draw_slot
  drawn && echo "$(date +%T) the sword is in the hand" || echo "$(date +%T) WARN: no sword in the hand"
  ensure_off; sleep 1.2; w "hpcopies log" 0.5
  k=0
  for P in "$HOLD" "hands test R 12 -28 40 0 -60 20" "hands test R 18 -30 30 -20 -80 0" "hands test R 8 -20 45 10 -30 40"; do
    k=$((k + 1)); w "$P" 1.2
    d "s$k-off"; w "vision y 6000" 1.6
    d "s$k-vision-log"; w "hpcopies on" 0.8; d "s$k-vision-on"; w "hpcopies log" 0.5
    waitfor 'off wished' 8; sleep 1.3
  done
  m s-poses 0
  grep -E '^HEADPARTS (copy:|t=.* copies )' "$R/somvr.log" | tail -6 | cut -c1-420
  expect_off "the sword's holds end off" 0
fi

if has ident; then
  echo "== ident: each copy node hidden alone, the vision on"
  drawn || draw_slot; w "$HOLD" 1.0
  ensure_off; sleep 1.2; w "hpcopies log" 0.5
  w "vision y 30000" 1.6
  d "i0-copies-log"
  for nd in $(grep -oE '^HEADPARTS copy: node 0x[0-9a-f]+' "$R/somvr.log" | awk '{print $4}' | sort -u | head -8); do
    w "hpskip $nd" 1.0; d "i-skip-$nd"; w "hpskip off" 0.6
  done
  w "hpcopies on" 1.0; d "i9-copies-on"
  grep -E '^HEADPARTS (hpskip|who)' "$R/somvr.log" | tail -12 | cut -c1-300
  waitfor 'off wished' 30; sleep 2
fi

if has copyid; then
  echo "== copyid: the vision on, the sword in two poses, each copy node hidden alone"
  drawn || draw_slot; ensure_off; sleep 1.2; w "hpcopies log" 0.5
  w "vision y 60000" 1.5
  NDS=$(grep -oE '^HEADPARTS copy: node 0x[0-9a-f]+' "$R/somvr.log" | awk '{print $4}' | sort -u | head -8)
  k=0
  for P in "hands test R 12 -28 40 0 -60 20" "hands test R 8 -20 45 10 -30 40" "hands test R 15 -40 35 -40 -40 0"; do
    k=$((k + 1)); w "$P" 1.2; d "c$k-log"
    for nd in $NDS; do w "hpskip $nd" 0.8; d "c$k-skip-$nd"; w "hpskip off" 0.5; done
    w "hpcopies on" 0.8; d "c$k-on"; w "hpcopies log" 0.5
  done
  m copyid 0
  w "vision y 1" 3
fi

if has sheath; then
  echo "== sheath: the sword put back over the shoulder, then the vision (its glowing copies on the back)"
  drawn || draw_slot; sleep 0.5
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.6; w "$REST" 1.0
  drawn && echo "$(date +%T) WARN: the sword still in the hand" || echo "$(date +%T) the sword is sheathed"
  ensure_off; sleep 1.2; w "hpcopies log" 0.5
  w "vision y 20000" 1.6
  d "h1-sheathed-vision-log"; m h1-sheathed-log 0
  S0=$(grep -oE 'copies on skipped deep=[0-9]+ forward=[0-9]+' "$R/somvr.log" | tail -1)
  w "hpcopies on" 0.8; d "h2-sheathed-vision-on"; m h2-sheathed-on 0
  sleep 5.5; S1=$(grep -oE 'copies on skipped deep=[0-9]+ forward=[0-9]+' "$R/somvr.log" | tail -1)
  echo "$(date +%T) the copies' skips: before '$S0' after '$S1'"
  grep -E '^HEADPARTS copy: ' "$R/somvr.log" | tail -4 | sed -E 's/.*node (0x[0-9a-f]+).*entry ([0-9]+) of ([0-9]+), ([^)]*)\).*: (not drawn in the eyes|drawn) .*/  copy \1 entry \2\/\3 \4: \5/'
  w "vision y 1" 2.5
fi

if has bowid; then
  echo "== bowid: the bow out (its wraith aim view): at rest, a hand at the right shoulder (an arrow's reach), nocked,"
  echo "   drawn - hpcopies log then on at each"
  w "hands test off" 0.5
  pose() { awk -v a="$1" -v p="$2" -v d="$3" -v L=60 'BEGIN { r = 3.14159265 / 180
    dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 7; ay = -13; az = 0
    bx = ax + L * dx; by = ay + L * dy; bz = az + L * dz; k = L - d
    printf "hand L head %.1f %.1f %.1f %.1f %.1f 0\n", bx, by, bz, p, -a
    printf "hand R head %.1f %.1f %.1f %.1f %.1f 0\n", ax + k * dx + 1.5, ay + k * dy, az + k * dz, p, -a }'; }
  setpose() { local IFS=$'\n'; t $(pose "$@"); }
  lo() { w "hpcopies log" 0.8; d "$1-log"; w "hpcopies on" 0.8; d "$1-on"; }
  t "hand L head -12 -10 -15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.4; t "btn L squeeze 1"; sleep 0.3
  t "hand L head 7 -15 60 -2 0 0" "hand R head 22 -40 15 0 0 0"; waitfor '^BOW .* enter ' 3; sleep 1.5
  lo w0-rest; m w0-rest 0
  t "hand L head 7 -15 60 -2 0 0" "hand R head 12 -8 -12 0 0 0"; sleep 1.0; lo w1-shoulder
  setpose 0 -2 4; sleep 0.3; t "btn R squeeze 1"; waitfor '^BOW .* nock: ' 2 || echo "$(date +%T) no nock line"; sleep 1.0
  lo w2-nocked
  setpose 0 -2 50; sleep 1.2; lo w3-drawn; m w3-drawn 0
  T_S=0.3 t "btn R squeeze 0"; sleep 1
  t "hand L head -12 -10 -15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.4; T_S=0.3 t "btn L squeeze 0"; waitfor '^BOW .* exit: ' 4
  t "hand L off" "hand R off"; sleep 0.5
  grep -E '^HEADPARTS t=.* copies ' "$R/somvr.log" | tail -2 | cut -c1-300
fi

if has third; then
  echo "== third: the sword sheathed, the vision on, seen from the game's camera (fallback third): where the copies are"
  drawn || draw_slot; sleep 0.5
  w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "hands squeeze R 0" 0.6; w "$REST" 1.0
  drawn && echo "$(date +%T) WARN: the sword still in the hand" || echo "$(date +%T) the sword is sheathed"
  ensure_off; sleep 1.2; w "hpcopies log" 0.5
  d "t0-first-off"
  w "fallback third" 2.0; d "t1-third-off"
  w "vision y 20000" 1.6; d "t2-third-vision"; m t2-third-vision 0
  NDS=$(grep -oE '^HEADPARTS copy: node 0x[0-9a-f]+' "$R/somvr.log" | awk '{print $4}' | sort -u | head -4)
  for nd in $NDS; do w "hpskip $nd" 0.8; d "t3-third-skip-$nd"; w "hpskip off" 0.4; done
  w "fallback auto" 2.0; d "t4-first-vision-log"; w "hpcopies on" 0.8; d "t5-first-vision-on"
  w "vision y 1" 2.5
fi

if has arrowid; then
  echo "== arrowid (the bow branch's quiver: SOMVR_BOW_QUIVER=on): an arrow taken and carried; the nodes near the head"
  echo "   then (the word hpnear), each hidden alone with a dump"
  w "hands test off" 0.5
  t "hand L head -12 -10 -15 90 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.4; t "btn L squeeze 1"; waitfor '^BOW .* enter ' 3
  t "hand L head -8 -15 55 90 0 0"; sleep 1
  t "hand R head 12 -10 -15 90 0 0"; sleep 0.5; t "btn R trigger 1"; waitfor 'quiver: an arrow taken' 3 || echo "$(date +%T) no arrow taken"
  t "hand R head 14 -22 32 60 0 0 s 0.5"; sleep 1.0
  d "a0-carry"; m a0-carry 0
  NL0=$(wc -l < "$R/somvr.log"); w "hpnear 70" 2.5
  tail -n +"$NL0" "$R/somvr.log" | grep -E '^HEADPARTS near: ' | head -12 | cut -c1-330
  for nd in $(tail -n +"$NL0" "$R/somvr.log" | grep -oE '^HEADPARTS near: node 0x[0-9a-f]+' | awk '{print $4}' | head -8); do
    w "hpskip $nd" 0.8; d "a-skip-$nd"; w "hpskip off" 0.4
  done
  w "hpnear 0" 0.3; t "btn R trigger 0"; sleep 0.5
  t "hand L head -12 -10 -15 90 0 0"; sleep 0.4; t "btn L squeeze 0"; t "hand L off" "hand R off"
fi

if has bow; then
  echo "== bow: the aim view"
  w "hands test off" 0.5
  # the bow: wraith-drive.sh's poses (head frame, cm), a right-handed archer at the jaw, the bow hand 60 cm ahead
  pose() { awk -v a="$1" -v p="$2" -v d="$3" -v L=60 'BEGIN { r = 3.14159265 / 180
    dx = sin(a * r) * cos(p * r); dy = sin(p * r); dz = cos(a * r) * cos(p * r); ax = 7; ay = -13; az = 0
    bx = ax + L * dx; by = ay + L * dy; bz = az + L * dz; k = L - d
    printf "hand L head %.1f %.1f %.1f %.1f %.1f 0\n", bx, by, bz, p, -a
    printf "hand R head %.1f %.1f %.1f %.1f %.1f 0\n", ax + k * dx + 1.5, ay + k * dy, az + k * dz, p, -a }'; }
  setpose() { local IFS=$'\n'; t $(pose "$@"); }
  w "hpcopies log" 0.5
  t "hand L head -12 -10 -15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.4; t "btn L squeeze 1"; sleep 0.3
  setpose 0 -2 30; waitfor '^BOW .* enter ' 3 || echo "$(date +%T) no BOW enter line"; sleep 1.5
  setpose 0 -2 4; sleep 0.3; t "btn R squeeze 1"; waitfor '^BOW .* nock: ' 2; setpose 0 -2 50; sleep 1.2
  d "b1-aim-copies-log"; m b1-aim-copies-log 0
  w "hpcopies on" 1.0; d "b2-aim-copies-on"; m b2-aim-copies-on 0
  w "hpcopies log" 1.0; d "b3-aim-copies-log"
  w "hpcopies on" 1.0; d "b4-aim-copies-on"
  w "vision" 0.5; grep -E '^VISION t=.* state: ' "$R/somvr.log" | tail -1 | cut -c1-120
  setpose 0 -2 30 ; T_S=0.3 t "btn R squeeze 0"; sleep 0.5
  t "hand L head -12 -10 -15 0 0 0" "hand R head 22 -40 15 0 0 0"; sleep 0.4; T_S=0.3 t "btn L squeeze 0"; waitfor '^BOW .* exit: ' 4
  t "hand L off" "hand R off"; sleep 1
  grep -E '^HEADPARTS (copy:|t=.* copies )' "$R/somvr.log" | tail -6 | cut -c1-420
fi

if has outline; then
  echo "== outline: an orc spawned, the vision on at once (its outline), hpcopies on"
  w "hpcopies on" 0.3; ensure_off; sleep 1.2
  w "practice spawn infantry" 0.3; waitfor '^PRACTICE t=.* spawn ' 8; face
  w "vision y 3000" 1.2; d "o1-orc-vision-copies-on"; m o1-orc-outline 0
  waitfor 'off wished' 6; sleep 1.3
fi

w "vision" 0.5; w "hpcopies on" 0.5
grep -E '^VISION t=.* state: ' "$R/somvr.log" | tail -1 | cut -c1-300
echo "$(date +%T) quit; dumps in $R (w16-steps.txt)"
echo quit >> "$R/cmd"
