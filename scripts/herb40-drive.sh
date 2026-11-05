#!/bin/bash
# herb40-drive.sh - build 40 (som-b40-herb, docs/re-xr-pipeline.md 176): the herb's pick by the hand and its outline, in
# a headless Windows boot. Run beside
#   HEADLESS_SLOT=auto HEADLESS_OWNER=som-b40-herb HEADLESS_SLOT_WAIT=3600 HEADLESS_HOLD=1500
#   SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ') SOMVR_HANDS_TEST=1
#     SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 SOMVR_INPUT_REPLAY=@RUN@/replay.inrec SOMVR_INPUT_REC=on"
#   setsid -w scripts/headless_vr.sh --win <wt>/ShadowOfMordor/mod/somvr
# Steps (H40_STEPS, default "replay outline"):
#   replay   the 11-05 session's window round marks 6 and 7 (HERB_SRC, HERB_WIN: after the menu's New herb blink - the
#            menu is the layer's input, never replayed) through scripts/replay-drive.sh (REPLAY_NOQUIT=1): the player's
#            own head, hands and controls; dumps at the source's moments (the cue, mark 6, before / at / after each grip)
#   outline  Practice > New herb, the head turned down to it, the open left hand 60 and 30 cm beside the stem and then
#            over it (65 cm over the plant's top): dumps (the outline faint, brighter, full); the grip there (the column
#            over the stem: taken), the fist at the mouth (eaten: the game's pick)
# H40_DUMPS: the replay's dump moments (source times). H40_RUN=RUN drives a given run. Stops at once (quit) when the user goes into VR.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
HERE=$(cd "$(dirname "$0")" && pwd)
WT=${H40_WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
STEPS=${H40_STEPS:-replay outline}
SRC=${HERB_SRC:-$SOM/logs/win-session-20261105-092046}
WIN=${HERB_WIN:-222104.5-222156}
T0=$(date +%Y%m%d-%H%M%S) # (the run's folder name, not its mtime)
R=${H40_RUN:-}
for i in $(seq 1 ${H40_WAIT_N:-900}); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -4); do
    n=${d##*boot-check-}
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [[ ! "$n" < "$T0" ]] && R=$d && break
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
has() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
x() { userin && bail "the user is in VR"; printf '%s\n' "$1" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $1"; sleep "${2:-0.8}"; }
cnt() { grep -cE "$1" "${2:-$R/somvr.log}"; }
dp() { userin && bail "the user is in VR"; echo dump > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep "${2:-1.2}"
  local f; f=$(grep -o 'dump layers frame [0-9]*' "$R/xr-layer.log" | tail -1 | grep -o '[0-9]*$'); echo "$1 $f" | tee -a "$R/h40-steps.txt"; }
for i in $(seq 1 200); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
if has replay; then
  echo "== replay: $SRC $WIN"
  REPLAY_RUN=$R REPLAY_NOQUIT=1 REPLAY_NO_ORCS=1 "$HERE/replay-drive.sh" "$SRC" "$WIN" > "$R/h40-replay.txt" 2>&1 &
  RP=$!
  for i in $(seq 1 240); do grep -qE '^REPLAY t=.* start' "$R/somvr.log" && break; userin && bail "the user is in VR"; sleep 0.25; done
  grep -E '^REPLAY t=.* start' "$R/somvr.log" | tail -1 | cut -c1-300
  ST=$(date +%s.%N); W0=${WIN%-*}
  # the source's moments: the cue up, mark 6, the first grip (222129.05), the second (222150.54), the B pick (222150.73)
  for at in ${H40_DUMPS:-222106.6 222110.3 222127.5 222128.7 222129.3 222131.0 222140.0 222149.8 222150.4 222150.9 222152.5}; do
    off=$(awk -v a="$at" -v b="$W0" 'BEGIN { printf "%.3f", a - b }')
    while :; do now=$(awk -v s="$ST" -v n="$(date +%s.%N)" 'BEGIN { printf "%.3f", n - s }')
      awk -v n="$now" -v o="$off" 'BEGIN { exit !(n + 0 >= o - 0.15) }' && break; userin && bail "the user is in VR"; sleep 0.05; done
    dp "replay-$at" 0.1
  done
  wait $RP
  tail -4 "$R/h40-replay.txt"
  echo "-- the herb in the replay:"; grep -E '^HERB t=.* (taken|eaten|dropped)|^CUES pick|^REPLAY t=.* (start|end)' "$R/somvr.log" | cut -c1-260
fi
if has outline; then
  echo "== outline: Practice > New herb, the open hand beside it and over it, the grip, the mouth"
  REST="hands test R 22 -35 -30 0 180 0"; LREST="hands test L -25 -35 30 0 0 0"
  if ! has replay; then   # the controls card away (the first right squeeze; replay-drive.sh does it otherwise)
    sleep 6; x "btn R squeeze 0"; x "btn R squeeze 1"; sleep 1; x "btn R squeeze 0"
    grep -q 'card: put away' "$R/xr-layer.log" || { x "btn R a 1"; x "btn R a 0"; }
  fi
  w "hands squeeze L 0" 0.3; w "hands squeeze R 0" 0.3; w "$LREST" 0.5; w "$REST" 0.5; x "head 0 0 0 yaw 0 pitch -3" 0.6
  w "practice herb" 1
  for i in $(seq 1 30); do grep -qE '^PRACTICE t=.* (new herb .*refused|arrived at)' <(tail -n 40 "$R/somvr.log") && break; sleep 1; done
  grep -E '^PRACTICE t=.* new herb' "$R/somvr.log" | tail -1 | cut -c1-220; sleep 3
  hcue() { grep -E '^HERB t=.* cue:' "$R/somvr.log" | tail -1; }
  hpos() { hcue | grep -oE '[-+][0-9.]+ right [-+][0-9.]+ up [-+][0-9.]+ ahead' | sed -E 's/ (right|up|ahead)//g'; }
  for i in $(seq 1 10); do [ -n "$(hpos)" ] && break; w "stick 0 0.3 0 0.4" 1.2; done
  hcue | cut -c1-240
  p=$(hpos)
  if [ -n "$p" ]; then
    set -- $p; yw=$(awk -v r="$1" -v a="$3" 'BEGIN{printf "%.0f", atan2(r, a) * 57.2958}')
    for sg in 1 -1; do x "head 0 0 0 yaw $((sg * yw)) pitch 0" 1.5; sleep 1.6
      set -- $(hpos); awk -v r="${1#+}" -v a="${3#+}" 'BEGIN{exit !(a > 0 && (r < 0 ? -r : r) < 40)}' && break; done
    set -- $(hpos); pt=$(awk -v u="$2" -v a="$3" 'BEGIN{v=atan2(u, a) * 57.2958 + 8; if (v < -65) v = -65; printf "%.0f", v}')
    x "head 0 0 0 yaw $((sg * yw)) pitch $pt" 1.5; sleep 1.6; hcue | cut -c1-240
    set -- $(hpos); HR=$1; HU=$2; HA=$3
    dp outline-none
    for k in side60 side30 over; do
      dx=$([ $k = side60 ] && echo -60 || ([ $k = side30 ] && echo -30 || echo 0))
      w "hands test L $(awk -v r="$HR" -v d="$dx" 'BEGIN{printf "%.1f", r + d}') $(awk -v u="$HU" 'BEGIN{printf "%.1f", u + 65}') $HA 0 0 0" 1.6
      dp "outline-$k"
      hcue | grep -oE "the (left|right) hand [0-9]+ cm from it.*" | cut -c1-80
    done
    w "hands squeeze L 1" 0.8; dp outline-grip
    grep -E '^HERB t=.* taken' "$R/somvr.log" | tail -1 | cut -c1-260
    w "hands test L 0 -9 6 0 0 0" 0.4; sleep 0.6; dp outline-mouth
    for i in $(seq 1 12); do grep -qE '^CUES pick t=' <(tail -n 30 "$R/somvr.log") && break; sleep 0.25; done
    sleep 4; dp outline-after
    w "hands squeeze L 0" 0.5; w "$LREST" 0.5
    grep -E '^HERB t=.* (eaten|after)|^CUES pick' "$R/somvr.log" | tail -4 | cut -c1-220
  else
    echo "outline: no cue"
  fi
fi
grep -E 'xr: hands: frame [0-9]+: the pick glow' "$R/xr-layer.log" | head -6 | cut -c1-160
echo quit >> "$R/cmd"
for i in $(seq 1 60); do [ -f "$R/game.exit" ] || [ -f "$R/cmd.done" ] && break; sleep 3; done
echo "done $(date +%T)"
