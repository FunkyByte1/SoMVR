#!/bin/bash
# replay35-drive.sh - b35's proof (docs/re-xr-pipeline.md 160.6): a scripted grab and kill recorded by SOMVR_INPUT_REC
# for a replay into a second boot (scripts/replay-drive.sh). One headless boot (headless_vr.sh [--win] with the env
# file's switches + SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt): the setup - the controls card away, the sword
# drawn, the AI blind (`story invisible on`: the orc stands still), the far unaware infantry, Talion 110 cm behind it
# facing it - then `mark seg-start`, the player's part by the test hands only (the left hand out to its collar, the
# squeeze and the fist: the grab; held; the sword's cuts across the held orc until the kill; let go; two cuts in the
# air), `mark seg-end`; quits. The replay: replay-drive.sh RUN T0-T1 with REPLAY_SETUP=<this file> (its `setup`, the
# same words) and T0/T1 from this run's seg-start/seg-end marks (printed at the end). REP_RUN=RUN drives a running boot.
setup() {
  w "hands test L -25 -35 30 0 0 0" 0.6
  w "hands test R 10 -10 -15 0 0 0" 0.7; w "hands squeeze R 1" 0.6; w "hands test R 30 -25 40 0 -90 0" 0.6
  w "hands squeeze R 0" 0.5; w "hands test R 22 -35 -30 0 180 0" 0.6
  w "story invisible on" 0.6; w "front keep 0" 0.3
  w "practice far infantry" 3
  w "verbs behind 110" 0.8; w "execute face" 1.2
  sleep 1
}
[ -n "${REP_SOURCE_ONLY:-}" ] && return 0
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${REP_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
T00=$(date +%s)
R=${REP_RUN:-}
for i in $(seq 1 $(( ${REP_RUN_WAIT:-3600} / 5 ))); do   # (a boot may wait for a game slot first)
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T00" ] && R=$d && break
  done
  [ -n "$R" ] && break; sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
echo "run $R"
L=$R/somvr.log
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.6}"; }
cnt() { grep -cE "$1" "$L"; }
waitfor() { local n0 i; n0=$(cnt "$1"); for i in $(seq 1 $((${2:-10} * 4))); do
  userin && bail "the user is in VR"; [ "$(cnt "$1")" -gt "$n0" ] && return 0; sleep 0.25; done; return 1; }
last() { grep -E "$1" "$L" | tail -"${2:-1}" | cut -c1-"${3:-300}"; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$L" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$L" || bail "no arrival"
echo "arrived $(date +%T)"; sleep 6
x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
setup
last '^EXECUTE where ' 1 260
w "mark seg-start" 1.0
# the grab: the left hand out to the collar (58 cm ahead, 20 below the eyes), the squeeze, the fist
w "hands test L 0 -20 58 0 0 0" 1.0
g0=$(cnt '^VERBS t=.* grab: the left hand')
w "hands squeeze L 1" 0.3; w "hands trigger L 1" 0.8
[ "$(cnt '^VERBS t=.* grab: the left hand')" -gt "$g0" ] && echo "$(date +%T) GRAB" || echo "$(date +%T) no grab"
last '^VERBS t=.* grab: the left hand' 1 260
sleep 1.5
# the cuts across the held orc, right to left (hands31's kill), until the kill
k0=$(cnt '^HOLD31 kill')
for xy in 0,-25 10,-35 -10,-20 0,-45; do x0=${xy%,*}; y0=${xy#*,}
  w "hands test R $((x0 + 45)) $y0 40 0 -90 90" 0.6; w "hands swing R $((x0 - 30)) $y0 55 0 90 90 300" 0.8
  [ "$(cnt '^HOLD31 kill')" -gt "$k0" ] && break; done
last '^(HOLD31 kill|COMBAT hit)' 3 260
sleep 1
w "hands trigger L -1" 0.3; w "hands squeeze L 0" 0.5; w "hands test L -25 -35 30 0 0 0" 0.8
# two cuts in the air
for k in 1 2; do w "hands test R 45 -10 25 0 -70 0" 0.8; w "hands swing R -25 -15 55 0 70 0 450" 1.0; done
w "hands test R 22 -35 -30 0 180 0" 0.8
w "mark seg-end" 1.0
sleep 2
echo quit >> "$R/cmd"
for i in $(seq 1 60); do [ -f "$R/game.exit" ] || [ -f "$R/cmd.done" ] && break; sleep 3; done
sleep 4
python3 -I "$(dirname "$0")/inrec.py" info "$L.inrec" | head -40
echo "replay: REPLAY_SETUP=$(cd "$(dirname "$0")" && pwd)/replay35-drive.sh scripts/replay-drive.sh $R T0-T1 (T0/T1: the seg marks above)"
