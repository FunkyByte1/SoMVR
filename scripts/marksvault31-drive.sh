#!/bin/bash
# marksvault31-drive.sh - build 31b (som-b31-marks-vault; docs/re-xr-pipeline.md 139): drives one headless boot
# (scripts/headless_vr.sh with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the layer's test file
# SOMVR_XR_TEST=@RUN@/xrtest.txt; with SOMVR_XR_DUMP_MARKED=N for the pictures below and, for the vault,
# SOMVR_VAULT_HANDS=on). MV_STEPS, in order:
#   marks  the practice infantry fights, the sword raised as a guard; for each of SOMVR_COUNTER_ICON's modes (`cprompt
#          game` = the game's icon, `hide` + `cprompt setup game` = build 30's hide, `hide` + `setup skip` = 139.2's) a
#          bug mark at the orc's grant (the word `mark`, as the B + Y chord: it lands in the wind-up) and the layer's
#          dumps of the frame after the next two counter-marker calls (`cprompt dumps 2`); RUN/mv-steps.txt lists the
#          dumps' frames (their xr-layer.log.hudN.ppm: the glyph or not); MV_MARKS more plain marks 12 s apart (the
#          frame time around a mark in the layer's perf windows); boot_check's cleanup composes each mark's seen.jpg
#   vault  `ovault auto MV_VAULTS` (somvr_hands31_game.c: the scripted left hand on a fighting orc's crown at the first
#          frame of its attack, the squeeze, the hold's push, the throw - the same `hands` words a driver sends, with
#          no latency), Talion put in front of the orc (`verbs front`, `execute face`) between tries; the OVAULT lines
#          (every flight frame), PARRY open, the dumps marked through the first flight
# MV_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${MV_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${MV_STEPS:-marks vault}
T0=$(date +%s)
R=${MV_RUN:-}
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$R/somvr.log}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/mv-steps.txt"; }
last() { grep -E "$1" "$R/somvr.log" | tail -${2:-1} | cut -c1-${3:-330}; }
mk() { local n0; n0=$(cnt 'xr: mark: a stereo frame' "$R/xr-layer.log"); w "mark $1" 0.05
  for i in $(seq 1 40); do [ "$(cnt 'xr: mark: a stereo frame' "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.1; done
  grep 'xr: mark: a stereo frame' "$R/xr-layer.log" | tail -1 | sed 's/^.*xr: mark: /  mark: /' | cut -c1-260 | tee -a "$R/mv-steps.txt"; }
REST="hands test R 22 -35 -30 0 180 0"   # the blade behind the hip, pointing back: out of every strike's path
LREST="hands test L -25 -35 30 0 0 0"    # the left hand at rest
GUARD="hands test R 40 -8 35 0 -90 90"   # the sword out in front, across: a guard
SLOT="hands test R 10 -10 -15 0 0 0"; OUT="hands test R 30 -25 40 0 -90 0"
draw_slot() { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6
draw_slot; w "front keep 0" 0.3
if has marks; then
  echo "== marks: a fight's wind-ups as the headset shows them; the counter glyph by SOMVR_COUNTER_ICON"
  w "$GUARD" 0.5
  w "practice spawn infantry" 2
  for MODE in game hideold hide; do
    case $MODE in
      game) w "cprompt game" 0.3 ;;
      hideold) w "cprompt hide" 0.3; w "cprompt setup game" 0.3 ;;
      hide) w "cprompt hide" 0.3; w "cprompt setup skip" 0.3 ;;
    esac
    echo "-- $MODE"
    n0=$(cnt 'dump mark cprompt'); w "cprompt dumps 2" 0.2
    if waitfor '^FRONT grant' 20; then mk "grant-$MODE"; else echo "$(date +%T) no grant within 20 s"; fi
    for i in $(seq 1 60); do [ "$(cnt 'dump mark cprompt')" -ge $((n0 + 2)) ] && break; sleep 0.5; done
    sleep 1.5
    grep -E 'xr: dump mark: record frame .* a dump of the next stereo frame' "$R/xr-layer.log" | tail -2 |
      sed -E 's/.*next stereo frame ([0-9]+).*/\1/' | sed "s/^/windup-$MODE /" | tee -a "$R/mv-steps.txt"
  done
  w "cprompt state" 0.5; last '^CPROMPT state' 1 400
  for k in $(seq 1 ${MV_MARKS:-0}); do sleep 12; mk "plain-$k"; done
  w "practice remove" 1.5; w "practice health" 1
fi
if has vault; then
  echo "== vault: ovault auto (the scripted left hand at a fighting orc's wind-up, no driver latency)"
  w "$REST" 0.6; w "$LREST" 0.6
  w "practice remove" 1.5; w "practice spawn infantry" 3
  w "ovault auto ${MV_VAULTS:-3}" 0.5
  t0=$(date +%s)
  while [ $(( $(date +%s) - t0 )) -lt ${MV_VAULT_S:-120} ]; do
    a=$(cnt '^OVAULT t=.* auto: try [0-9]+ at'); o=$(cnt '^OVAULT t=.* auto: try [0-9]+ over')
    [ "$o" -ge "${MV_VAULTS:-3}" ] && break
    if [ "$a" -le "$o" ]; then w "verbs front ${H31_VAULT_CM:-90}" 0.3; w "execute face" 1.5; else sleep 1; fi
  done
  grep -E '^OVAULT t=.* (auto|grab|throw|blocked|end|let go)' "$R/somvr.log" | cut -c1-300
  grep -E '^OVAULT t=.* flight' "$R/somvr.log" | head -60 | cut -c1-230
  grep -E '^PARRY open' "$R/somvr.log" | tail -3 | cut -c1-260
  grep -E 'dump mark ovault' "$R/somvr.log" | cut -c1-160
  w "ovault auto off" 0.5
fi
w "verbs status" 1
w "kills status" 1
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
