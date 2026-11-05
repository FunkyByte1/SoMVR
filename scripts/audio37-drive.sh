#!/bin/bash
# audio37-drive.sh - som-b37-audio (docs/re-xr-pipeline.md 173): the rain at the practice meadow, heard and seen.
# Boot the Windows build with scripts/headset-build26.env, SOMVR_HANDS_TEST=1, SOMVR_XR_TEST=@RUN@/xrtest.txt and
# SOMVR_AUDIO_LOG=1 (test only: the SNDPOS / SNDAXIS / SNDSUM lines); headless audio is a null device - nothing is
# ever played to a real output (the game's mix is only written to a WAV by Wwise's own capture, `sndcap`).
# 1. The arrival's weather forced to AU_SKY_WANT (default SON_Day03_Rain) by `practice weather AU_SKY` (DayNoRain:
#    Day03 or Day03_Rain; again until the state is the wanted one, at most AU_SKY_TRIES 8).
# 2. Pictures (bug marks: both eyes): rain-ahead, rain-up, rain-back (AU_PICS=0 skips them).
# 3. For each mode of AU_MODES (default "game vr": `sndnear MODE`): the head held level and then turned in 15-degree
#    steps from -90 to +90 and back (a step each 0.75 s), then looked down 60 and up 40 - the same rain, the same turns;
#    the SNDAXIS lines give each object near the head's vertical line, raw and as heard, twice a second. (`sndcap`,
#    Wwise's output capture, is accepted by this build's sound engine but writes no file - boot 20261105-011856.)
# AU_CHARGE=1: before the modes, the execution charged (`verbs brand charge`: the wraith's fire on the sheathed sword on
# Talion's back - the 11-04 session's case, 20 s before the report) and its sounds given 4 s to start.
# BG_RUN=RUN drives a running boot; AU_NOQUIT=1 leaves the game running.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
T0=$(date +%s)
R=${BG_RUN:-}
for i in $(seq 1 240); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -6); do
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
bail() { echo "$(date +%T) STOP: $1"; [ -n "${AU_NOQUIT:-}" ] || echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 240); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
mk() {   # mk NAME: a bug mark (both eyes in RUN/marks/<id>)
  local before i d
  userin && bail "the user is in VR"
  before=$(ls "$R/marks" 2>/dev/null | wc -l)
  printf '%s' "mark $1" > "$R/somvr.trigger"
  for i in $(seq 1 40); do
    if [ "$(ls "$R/marks" 2>/dev/null | wc -l)" -gt "$before" ]; then
      d=$R/marks/$(ls -t "$R/marks" | head -1)
      [ -s "$d/state.txt" ] && break
    fi
    sleep 0.25
  done
  for i in $(seq 1 30); do [ -s "$d/eyes.ppm" ] || [ -s "$d/eyes.jpg" ] && break; sleep 0.25; done
  LAST=$d
  echo "$1 ${d##*/}" | tee -a "$R/audio-steps.txt"
  sleep 1.5
}
SKYW=${AU_SKY_WANT:-SON_Day03_Rain}
sky_now() { grep -E "^PRACTICE t=.* light( now| later \([0-9]+ s\))?: weather '" "$R/somvr.log" | sed -nE "s/.* weather '([^']*)'.*/\1/p" | tail -1; }

x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "front keep 0" 0.3
w "hands test R 25 -75 0 0 180 0" 0.6; w "hands test L -25 -75 0 0 0 0" 0.6
x "head 0 0 0 yaw 0 pitch 0" 1
echo "$(date +%T) arrival weather '$(sky_now)'" | tee -a "$R/audio-steps.txt"
for i in $(seq 1 "${AU_SKY_TRIES:-8}"); do
  [ "$(sky_now)" = "$SKYW" ] && break
  n0=$(grep -cE '^PRACTICE t=.* light later' "$R/somvr.log")
  w "practice weather ${AU_SKY:-DayNoRain}" 1
  for j in $(seq 1 50); do [ "$(grep -cE '^PRACTICE t=.* light later' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.5; done
  echo "$(date +%T) try $i: '$(sky_now)'" | tee -a "$R/audio-steps.txt"
done
[ "$(sky_now)" = "$SKYW" ] || echo "$(date +%T) OFF - not $SKYW" | tee -a "$R/audio-steps.txt"
sleep 12   # the rain in (its particles and sounds start with the state)
# the pictures aimed in world yaw (each boot's room is turned its own way): a reference mark at the test yaw 0 gives the
# head's world yaw W0; the test yaw's sign is checked on the first view (looks36-drive.sh's way); AU_AHEAD (default
# -60: Talion's yaw at the meadow's arrival) and AU_AHEAD + 180
headyaw() { grep -m1 '^head ' "$1/state.txt" | grep -oE 'yaw [-0-9.]+' | head -1 | cut -d' ' -f2; }
wrapd() { python3 -c "import sys; a=float(sys.argv[1]); print(round((a+180)%360-180,1))" "$1"; }
SIGN=1; CHECKED=0
view() {   # view NAME WORLD_YAW PITCH
  local y a off k
  y=$(wrapd "$(python3 -c "print(($2) - ($W0))")")
  [ "$SIGN" = -1 ] && y=$(wrapd "$(python3 -c "print(-($y))")")
  for k in 0 1 2; do
    x "head 0 0 0 yaw $y pitch $3 roll 0" 2.5
    mk "$1"
    a=$(headyaw "$LAST"); off=$(wrapd "$(python3 -c "print(($a) - ($2))")")
    if [ $CHECKED = 0 ]; then
      CHECKED=1
      if python3 -c "import sys; sys.exit(0 if abs(float('$off')) > 25 else 1)"; then
        SIGN=-1; echo "$(date +%T) the test yaw's sign is the other way (off $off deg): flipped"; view "$@"; return
      fi
    fi
    python3 -c "import sys; sys.exit(0 if abs(float('$off')) > 2 else 1)" || break
    y=$(wrapd "$(python3 -c "print(($y) - ($SIGN) * ($off))")")
  done
  echo "$(date +%T) $1: head yaw $a (asked $2, off $off deg)" | tee -a "$R/audio-steps.txt"
}
if [ "${AU_PICS:-1}" = 1 ]; then
  x "head 0 0 0 yaw 0 pitch 0" 2.5; mk rain-ref; W0=$(headyaw "$LAST")
  echo "$(date +%T) the head's world yaw at the test yaw 0: $W0" | tee -a "$R/audio-steps.txt"
  AH=${AU_AHEAD:--60}
  view rain-ahead "$AH" -5
  view rain-up "$AH" 35
  view rain-back "$(python3 -c "print(($AH) + 180)")" -5
  x "head 0 0 0 yaw 0 pitch 0" 1.5
fi
if [ "${AU_CHARGE:-0}" = 1 ]; then
  x "head 0 0 0 yaw 0 pitch 0" 1
  w "verbs brand charge" 4
  grep -E "^(FXCENSUS|VERBS|EXECUTE) t=.*(firesword|charge|brand)" "$R/somvr.log" | tail -3 | cut -c1-200
fi
for m in ${AU_MODES:-game vr}; do
  w "sndnear $m" 0.5
  w "sndnear state" 0.5
  x "head 0 0 0 yaw 0 pitch 0" 2
  sleep 4
  for y in -15 -30 -45 -60 -75 -90 -75 -60 -45 -30 -15 0 15 30 45 60 75 90 75 60 45 30 15 0; do
    x "head 0 0 0 yaw $y pitch 0" 0.75
  done
  x "head 0 0 0 yaw 0 pitch -60" 3
  x "head 0 0 0 yaw 0 pitch 40" 3
  x "head 0 0 0 yaw 0 pitch 0" 3
  w "sndnear state" 0.5
done
echo "$(date +%T) done"
grep -E '^SNDCAP|^SNDNEAR|^CONFIG audio near' "$R/somvr.log" | cut -c1-220
[ -n "${AU_NOQUIT:-}" ] || echo quit >> "$R/cmd"
