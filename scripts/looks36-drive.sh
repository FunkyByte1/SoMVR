#!/bin/bash
# looks36-drive.sh - som-b36-winlooks (docs/re-winport.md 7.14): the same places in both eyes on the Linux and the
# Windows build, for a side-by-side of the look (the grass, the walls' and the ground's relief, the fog). One bug mark
# per view (RUN/marks/<id>/state.txt + the eyes; the name and the mark in RUN/looks-steps.txt), the arrival's weather
# (LK_WEATHER=NAME: `perf console GameServerShell.ChangeWeather NAME`, SOMVR_PERF's - NAME must be a Weather/Definition
# (DayNoRain, TEST_Day_02 ...), not a state (SON_Day01: nothing happens), and the perf console runs it from the render
# thread; for a set weather boot with SOMVR_PRACTICE_SKY=always SOMVR_PRACTICE_WEATHER=DEF instead), the views aimed in
# world yaw (each place's first mark gives the head's world yaw at the test yaw 0; the sign of the test yaw is checked
# on the first aimed view and flipped if it is the other way). Boot either build with scripts/headset-build26.env
# (its SOMVR_RENDER_SET: the VR preset; Windows: the slot's render.cfg, headless_win.sh), SOMVR_HANDS_TEST=1 and
# SOMVR_XR_TEST=@RUN@/xrtest.txt. The places (the user's Windows session 11-03, logs/coord/b36/marks.md):
#   meadow   the practice arrival (32500 -24800): the grass at the feet and ahead
#   wall     31770 -30462 (mark 4): the fort's stone wall, looked up at
#   ground   33389 -28793 (mark 6): the fort's paved floor
#   dry      34874 -26745 (mark 14): the tall dry grass on the fort's top
#   fog      29444 -27898 (mark 18): the bank under the ruins, the haze
#   sky      (at the meadow) each Weather/Definition of LK_SKIES in turn by the test word `practice weather DEF` (the
#            game's ChangeWeather on the game thread; its reset pause in view), 12 s, then the meadow ahead and back
# LK_PLACES picks (default all, in that order); LK_PERF_S (20) s of PERF window at the meadow (frame times; meaningful
# only in a solo slot); BG_RUN=RUN drives a running boot; LK_NOQUIT=1 leaves the game running.
# b37 looks (som-b37-looks): LK_SKY=DEF forces a weather for every place: the test word `practice weather DEF` at the
# arrival, again until its `light later` line names LK_SKY_WANT (default SON_Day03, DayNoRain's overcast; at most
# LK_SKY_TRIES 5); after each later trip the arrival's `light` line is read and the weather forced again if it drifted
# (the place's views are taken after it). Boot with SOMVR_PRACTICE_SKY=dry (a `clear` trip would undo an overcast).
# The weather per place in RUN/looks-weather.txt; a place not in LK_SKY_WANT after the tries is marked there (OFF).
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${BG_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
T0=$(date +%s)
R=${BG_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run"; exit 1; }
bash "$(dirname "$0")/render_need.sh" "$R" looks36-drive.sh || exit 1   # b37: a Windows run at WIN_RENDER=vr (re-xr-pipeline.md 158.5)
echo "run $R"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; [ -n "${LK_NOQUIT:-}" ] || echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 200); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
  echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
waitfor() { local i; for i in $(seq 1 $(( ${2:-30} * 2 ))); do grep -qE "$1" "$R/somvr.log" 2>/dev/null && return 0; sleep 0.5; done; return 1; }
LAST=""
mk() {   # mk NAME: a bug mark; LAST = its folder (state.txt there)
  local before i d
  userin && bail "the user is in VR"
  before=$(ls "$R/marks" 2>/dev/null | wc -l)
  printf '%s' "mark $1" > "$R/somvr.trigger"
  LAST=""
  for i in $(seq 1 40); do
    if [ "$(ls "$R/marks" 2>/dev/null | wc -l)" -gt "$before" ]; then
      d=$R/marks/$(ls -t "$R/marks" | head -1)
      [ -s "$d/state.txt" ] && { LAST=$d; break; }
    fi
    sleep 0.25
  done
  [ -n "$LAST" ] || { echo "$(date +%T) mark $1: none"; return 1; }
  for i in $(seq 1 30); do [ -s "$LAST/eyes.ppm" ] || [ -s "$LAST/eyes.jpg" ] && break; sleep 0.25; done
  echo "$1 ${LAST##*/} $(grep -m1 '^talion root' "$LAST/state.txt" | grep -oE 'yaw [-0-9.]+') head $(headyaw "$LAST")" |
    tee -a "$R/looks-steps.txt"
  sleep 1.5
}
headyaw() { grep -m1 '^head ' "$1/state.txt" | grep -oE 'yaw [-0-9.]+' | head -1 | cut -d' ' -f2; }
wrap() { python3 -c "import sys; a=float(sys.argv[1]); print(round((a+180)%360-180,1))" "$1"; }
SIGN=1; CHECKED=0
# b37 looks: LK_AIM_TOL=DEG - a view whose head yaw is more than DEG off the asked one is aimed again (the test yaw
# corrected by the miss; Talion's body turns in steps behind the head) and marked again, up to LK_AIM_TRIES (2) times;
# the sheets take a name's last mark
view() {   # view NAME TARGET_YAW PITCH (W0 = the place's head yaw at the test yaw 0)
  local y a off k
  y=$(wrap "$(python3 -c "print(($2) - ($W0))")")
  [ "$SIGN" = -1 ] && y=$(wrap "$(python3 -c "print(-($y))")")
  for k in $(seq 0 "${LK_AIM_TRIES:-2}"); do
    x "head 0 0 0 yaw $y pitch $3 roll 0" 2.5
    mk "$1" || return
    a=$(headyaw "$LAST")
    off=$(wrap "$(python3 -c "print(($a) - ($2))")")
    if [ $CHECKED = 0 ]; then
      CHECKED=1
      if python3 -c "import sys; sys.exit(0 if abs(float('$off')) > 25 else 1)"; then
        SIGN=-1; echo "$(date +%T) the test yaw's sign is the other way (off $off deg): flipped, again"
        view "$@"; return
      fi
    fi
    [ -n "${LK_AIM_TOL:-}" ] && [ "$k" -lt "${LK_AIM_TRIES:-2}" ] &&
      python3 -c "import sys; sys.exit(0 if abs(float('$off')) > float('$LK_AIM_TOL') else 1)" || break
    echo "$(date +%T) $1: head yaw $a (asked $2, off $off deg): aimed again"
    y=$(wrap "$(python3 -c "print(($y) - ($SIGN) * ($off))")")
  done
  echo "$(date +%T) $1: head yaw $a (asked $2, off $off deg)"
}
SKYW=${LK_SKY_WANT:-SON_Day03}
skyline() { grep -E "^PRACTICE t=.* light( now| later \([0-9]+ s\))?: weather '" "$R/somvr.log" 2>/dev/null; }
sky_of() { sed -nE "s/.* weather '([^']*)'.*/\1/p" | tail -1; }
sky_force() {   # sky_force WHERE: LK_SKY until the state is SKYW; 0 = there
  local i j n0 s
  for i in $(seq 1 "${LK_SKY_TRIES:-5}"); do
    n0=$(grep -cE '^PRACTICE t=.* light later' "$R/somvr.log")
    w "practice weather $LK_SKY" 1
    for j in $(seq 1 50); do [ "$(grep -cE '^PRACTICE t=.* light later' "$R/somvr.log")" -gt "$n0" ] && break; sleep 0.5; done
    s=$(grep -E '^PRACTICE t=.* light later' "$R/somvr.log" | sky_of)
    echo "$(date +%T) $1: practice weather $LK_SKY, try $i: '$s'" | tee -a "$R/looks-weather.txt"
    [ "$s" = "$SKYW" ] && { sleep 3; return 0; }
  done
  echo "$(date +%T) $1: OFF - not $SKYW after ${LK_SKY_TRIES:-5} tries" | tee -a "$R/looks-weather.txt"; return 1
}
place() {   # place NAME [X Y Z]: there (practice go), the streaming's wait, the reference mark
  local n=$1 l0 s; shift
  if [ $# = 3 ]; then
    l0=$(skyline | wc -l)
    w "practice go $1 $2 $3" 1
    waitfor "^PRACTICE t=.*(arrived at \\($1|did not arrive)" 40 || waitfor '^PRACTICE t=.*(arrived at|did not arrive)' 5
    grep -E '^(WARN )?PRACTICE t=.*(arrived at|did not arrive)' "$R/somvr.log" | tail -1 | cut -c1-200
    sleep "${LK_SETTLE_S:-14}"
    if [ -n "${LK_SKY:-}" ]; then
      s=$(skyline | tail -n +$((l0 + 1)) | sky_of)
      echo "$(date +%T) $n: the trip's weather '${s:-(no line)}'" | tee -a "$R/looks-weather.txt"
      [ "$s" = "$SKYW" ] || [ -z "$s" ] || sky_force "$n"
    fi
  fi
  x "head 0 0 0 yaw 0 pitch 0" 2
  mk "$n-ref" || bail "no reference mark"
  W0=$(headyaw "$LAST")
  echo "$(date +%T) $n: the head's world yaw at the test yaw 0: $W0"
}

x "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
w "front keep 0" 0.3
# the hands down at the sides, out of the pictures
w "hands test R 25 -75 0 0 180 0" 0.6; w "hands test L -25 -75 0 0 0 0" 0.6
[ -n "${LK_WEATHER:-}" ] && { w "perf console GameServerShell.ChangeWeather $LK_WEATHER" 14; }
grep -E "^PRACTICE t=.* light:|FLATFX t=.*console" "$R/somvr.log" | tail -2 | cut -c1-200
if [ -n "${LK_SKY:-}" ]; then
  echo "$(date +%T) arrival: weather '$(skyline | sky_of)'" | tee -a "$R/looks-weather.txt"
  [ "$(skyline | sky_of)" = "$SKYW" ] || sky_force arrival
fi
for p in ${LK_PLACES:-meadow wall ground dry fog}; do
  case $p in
  meadow) place meadow
          view meadow-feet -88 -35; view meadow-ahead -88 -8; view meadow-back 92 -15
          if [ "${LK_PERF_S:-20}" -gt 0 ]; then
            x "head 0 0 0 yaw 0 pitch -15" 1; w "perf meadow" 0.3; x "perf meadow" "${LK_PERF_S:-20}"
            w "perf after" 0.3; x "perf after" 1
          fi ;;
  wall)   place wall 31770 3800 -30462;  view wall-up 160 25; view wall-level 160 5; view wall-side 70 10 ;;
  ground) place ground 33389 3700 -28793; view ground-down -158 -40; view ground-ahead -38 -25 ;;
  dry)    place dry 34874 3700 -26745;    view dry-down -68 -50; view dry-ahead -68 -12 ;;
  fog)    place fog 29444 1850 -27898;    view fog-ahead 71 2; view fog-back -109 2 ;;
  sky)    place sky 32500 2100 -24800
          for d in ${LK_SKIES:-DayNoRain TEST_Day_01 TEST_Day_03 AnyTimeNoRain TEST_Day_NoWeather GP_BGIntro_Day02 DayNoRain}; do
            w "practice weather $d" 12
            grep -E "^PRACTICE t=.*(console 'GameServerShell.ChangeWeather $d|light later)" "$R/somvr.log" | tail -2 | cut -c1-200
            view "sky-$d-ahead" -88 -8; view "sky-$d-back" 92 -15
          done ;;
  *) echo "unknown place $p" ;;
  esac
done
x "head 0 0 0 yaw 0 pitch 0" 1
echo "$(date +%T) done"
cat "$R/looks-steps.txt"
grep -E '^PERF t=.*meadow' "$R/somvr.log" | tail -1 | grep -oE 'its frame work[^;]*' | cut -c1-200
grep -E 'xr: perf.*meadow' "$R/xr-layer.log" | head -2 | cut -c1-260
[ -n "${LK_NOQUIT:-}" ] || echo quit >> "$R/cmd"
