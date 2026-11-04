#!/bin/bash
# prompts36-drive.sh - build 36 (som-b36-prompts; docs/re-xr-pipeline.md 165): drives one headless boot (scripts/
# headless_vr.sh --win with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, the layer's test file
# SOMVR_XR_TEST=@RUN@/xrtest.txt). Each scene twice: `gprompts on` (the game's prompts, build 35) and `gprompts off`
# (SOMVR_GAME_PROMPTS=off), the layer's dumps (both eyes and the HUD image: RUN/pm-steps.txt lists their frames) and bug
# marks. PM_STEPS, in order:
#   fight   the practice infantry fights: a mark at its grant (the wind-up: the counter glyph's moment, 139.2), dumps
#           0.4 s and 1.9 s later and three more 3 s apart (the orc's overhead markers while it fights)
#   vision  the wraith vision held (Y) over the meadow with the orc out, kept still (`kills still on`): dumps
#   icons   the user's mark 15 place (11-03 session, t=91853: Talion at 31271 3721 -31962; the game's 'Firepit' marker
#           6 m away at 30781 3857 -32314): there, a step toward the fire pit, the head at it, the wraith vision held, dumps
#   gate    the user's mark 16 place (t=91910: 23023 2698 -28040, the wooden gear frame): the wraith vision held, the
#           head turned round in 90-degree steps, dumps; the OBJECTIVE / GPROMPTS lines name the markers the game shows
# PM_RUN=RUN drives a running boot.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd -P)
WT=${PM_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
STEPS=${PM_STEPS:-fight vision icons gate}
T0=$(date +%s)
R=${PM_RUN:-}
for i in $(seq 1 900); do   # the slot may take an hour (HEADLESS_SLOT_WAIT)
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
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
for i in $(seq 1 200); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$R/somvr.log}"; }
waitfor() { local n0 i; n0=$(grep -cE "$1" "$R/somvr.log"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(grep -cE "$1" "$R/somvr.log")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 1.2
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/pm-steps.txt"; }
mk() { local n0; n0=$(cnt 'xr: mark: a stereo frame' "$R/xr-layer.log"); w "mark $1" 0.05
  for i in $(seq 1 40); do [ "$(cnt 'xr: mark: a stereo frame' "$R/xr-layer.log")" -gt "$n0" ] && break; sleep 0.1; done
  grep 'xr: mark: a stereo frame' "$R/xr-layer.log" | tail -1 | sed 's/^.*xr: mark: /  mark: /' | cut -c1-200 | tee -a "$R/pm-steps.txt"; }
trip() { local a0=$(cnt 'arrived at') f0=$(cnt 'did not arrive')
  w "practice go $1 $2 $3" 1
  for i in $(seq 1 40); do [ "$(cnt 'arrived at')" -gt "$a0" ] || [ "$(cnt 'did not arrive')" -gt "$f0" ] && break; sleep 1; done
  grep -E 'arrived at|did not arrive' "$R/somvr.log" | tail -1 | cut -c1-170; sleep 3; }
gp() { w "gprompts $1" 0.4; }
REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "$LREST" 0.6; w "$REST" 0.6
x "head 0 0 0 yaw 0 pitch -5" 0.6
if has fight; then
  echo "== fight: the practice infantry's overhead markers and glyphs, the game's prompts on, then off"
  w "practice spawn infantry" 3
  for MODE in on off; do
    gp $MODE; echo "-- $MODE"
    w "cprompt dumps 2" 0.2
    if waitfor '^FRONT grant' 25; then mk "fight-$MODE"; sleep 0.3; dp "fight-$MODE-grant"; else echo "$(date +%T) no grant within 25 s"; fi
    sleep 1.5; dp "fight-$MODE-b"
    for k in 1 2 3; do sleep 3; dp "fight-$MODE-$k"; done
  done
  w "gprompts state" 0.5
  w "practice remove" 2
fi
if has vision; then
  echo "== vision: the wraith vision over the meadow with an orc out, kept still"
  w "practice spawn infantry" 3; w "kills still on" 1
  x "btn L b 1"; sleep 2.5
  for MODE in on off; do gp $MODE; sleep 1.5; dp "vision-$MODE"; mk "vision-$MODE"; sleep 1; done
  x "btn L b 0"; sleep 1
  w "kills still off" 0.5; w "practice remove" 2
fi
if has icons; then
  echo "== icons: the mark-15 place, the fire pit in the wraith vision"
  trip 31271 3721 -31962
  python3 "$HERE/collect15_walk.py" "$R" 30781 -32314 --stop 5.2 --secs 12 --look 3857 | tail -2
  x "btn L b 1"; sleep 3
  for MODE in on off; do gp $MODE; sleep 1.5; dp "icons-$MODE"; mk "icons-$MODE"; sleep 1; done
  x "btn L b 0"; sleep 1
fi
if has gate; then
  echo "== gate: the mark-16 place (the wooden gear frame), the wraith vision, the head round"
  trip 23023 2698 -28040
  x "btn L b 1"; sleep 3
  for MODE in on off; do gp $MODE
    for YAW in 0 90 180 270; do x "head 0 0 0 yaw $YAW pitch 10" 1.2; dp "gate-$MODE-yaw$YAW"; done
    mk "gate-$MODE"
  done
  x "head 0 0 0 yaw 0 pitch 0" 0.5
  x "btn L b 0"; sleep 1
fi
w "gprompts state" 0.5
echo "-- the markers the game showed (OBJECTIVE first sight) and those hidden (GPROMPTS):"
grep -E '^OBJECTIVE t=.* marker [0-9]+ |^GPROMPTS t=.*world icon|^GPROMPTS (state|parts)' "$R/somvr.log" | cut -c1-220 | tail -40
echo "-- CPROMPT:"; grep -E '^CPROMPT' "$R/somvr.log" | cut -c1-200 | tail -6
echo "-- PROMPTS shows (last):"; grep -E '^PROMPTS show' "$R/somvr.log" | cut -c1-220 | tail -12
w "endlogs" 2; sleep 2
echo "done $(date +%T)"
echo quit >> "$R/cmd"
