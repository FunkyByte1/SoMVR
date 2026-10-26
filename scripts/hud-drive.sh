#!/bin/bash
# hud-drive.sh - the HUD helper's (build 8) test hands, fights and dumps for one headless boot: the head-level HUD and
# the wrist watch in play (the arrival, a fight the practice orc wins hits in, a streak, focus), each with a stereo dump
# (the layer's `dump`: <log>.stereoN.ppm + .hudN.ppm + the `dump layers` lines for scripts/xr_compose.py). Start it with
# the boot (both outside the sandbox):
#   HEADLESS_HOLD=900 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' <build-vr7>/headset.env | tr '\n' ' ')
#     SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt <the switches under test>" scripts/headless_vr.sh <wt>/.../somvr
# It finds the newest run of this worktree (WT), waits for the practice arrival, writes RUN/xrtest.txt (the layer runs
# a changed file's lines once), RUN/somvr.trigger (the game's words) and RUN/cmd (the harness's), ends with `quit`.
# HUD_PLAN=a (default): discovery (every HUD element in play); b: the build-8 look (the watch and the prompts composed).
# Stops at once (quit) when the user goes into VR (somvr-headset active or status.json "user": true).
HERE=$(cd "$(dirname "$0")" && pwd)
LOGS=/home/bara/Projects/flat2vr/ShadowOfMordor/logs
WT=${WT:-$(basename "$(cd "$HERE/../.." && pwd -P)")}
PLAN=${HUD_PLAN:-a}
T0=$(date +%s)
R=""
for i in $(seq 1 120); do
  for d in $(ls -dt "$LOGS"/boot-check-2026* 2>/dev/null | head -3); do
    [ -f "$d/launch-cmd.txt" ] && grep -q "$WT" "$d/launch-cmd.txt" && [ "$(stat -c %Y "$d")" -ge "$T0" ] && R=$d && break
  done
  [ -n "$R" ] && break
  sleep 5
done
[ -n "$R" ] || { echo "no run of $WT"; exit 1; }
echo "run $R"
userin() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  grep -q '"user": true' /run/user/1000/vr-session/status.json 2>/dev/null && return 0
  return 1
}
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 6
N=0
# t LINE...: one layer test step (the file replaced whole: its lines run once, in order)
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
# w WORD [SECS]: a game word (somvr.trigger)
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
k() { echo "$*" >> "$R/cmd"; echo "$(date +%T) cmd: $*"; }
d() { t "dump"; echo "$(date +%T) DUMP $1"; sleep "${2:-1.2}"; }
WATCH="hand L head 5 -16 32 0 -90 -130"       # the left forearm across the chest, the back of the wrist to the eyes
LOW="hand L head -22 -40 25 0 0 0"            # the left hand down at the side
down() { grep -q "^PRACTICE t=.* orc is down" "$R/somvr.log"; }
LUNGE_A="hands test R 40 -8 35 0 -90 0";  LUNGE="hands swing R -20 -18 62 0 90 0 300"
LUNGE_B="hands swing R 40 -8 35 0 -90 0 300"
lunge() { w "$LUNGE_A" 0.5; w "$LUNGE" 1.0; w "$LUNGE_B" 1.0; }
d arrival-card 2
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
sleep 1.5
d arrival-hud
t "$WATCH s 0.4"; sleep 1.5
d watch-arrival
t "$LOW s 0.3"
w "hands test R 28 -40 20 0 0 0"
w "hands draw" 2
if [ "$PLAN" = a ]; then
  # the orc's hits: no guard, the right hand low; dumps through its attacks (the health ring, its prompts)
  w "practice spawn infantry" 4
  for i in 1 2 3 4 5 6 7 8; do d fight-$i 2.3; done
  t "$WATCH s 0.4"; sleep 1.2
  d watch-hurt
  t "$LOW s 0.3"
  # a streak: lunges until it is down (the streak counter, the hit prompts)
  for kk in $(seq 1 10); do
    down && break
    lunge
    [ $((kk % 2)) = 0 ] && d streak-$kk 0.3
  done
  d after-fight 1.5
  # focus: the game's aim (the right mouse button held: the bow's slow time drains focus)
  k "xdo mousedown 3"; sleep 1.5
  d focus-aim 0.5
  sleep 1.5
  d focus-aim2 0.5
  k "xdo mouseup 3"; sleep 1.5
  t "$WATCH s 0.4"; sleep 1.2
  d watch-focus
  t "$LOW s 0.3"
  # a second orc (the berserker) for more prompts
  w "practice spawn berserker" 4
  for i in 1 2 3 4 5; do d fight2-$i 2.3; done
else
  # b: the build-8 look: the watch (arrival, hurt, mid-streak), the band (the banner, the hint, the XP after a kill, the
  # aim's prompts: the right mouse button held = the bow's focus, its "Distract" / "Deactivate Focus" prompts)
  k "xdo mousedown 3"; sleep 1.5
  d aim-arrival 0.5
  k "xdo mouseup 3"; sleep 1
  w "practice spawn infantry" 4
  for i in 1 2 3 4; do d fight-$i 2.3; done
  k "xdo mousedown 3"; sleep 1.5
  d aim-fight 0.5
  k "xdo mouseup 3"; sleep 1
  t "$WATCH s 0.4"; sleep 1.2
  d watch-hurt
  t "hand L head 0 -12 34 0 -90 -120 s 0.4"; sleep 1.2
  d watch-near
  t "$LOW s 0.3"
  for kk in $(seq 1 10); do
    down && break
    lunge
    [ $kk = 4 ] && { t "$WATCH s 0.3"; sleep 0.8; d watch-streak 0.3; t "$LOW s 0.3"; }
    [ $((kk % 2)) = 0 ] && d streak-$kk 0.3
  done
  d after-fight 0.8
  d after-fight2 1.5
  k "xdo mousedown 3"; sleep 1.5
  d focus-aim 0.5
  sleep 1.5
  d focus-aim2 0.5
  k "xdo mouseup 3"; sleep 1.5
  t "$WATCH s 0.4"; sleep 1.2
  d watch-focus
  t "$LOW s 0.3"
fi
sleep 1
k quit
echo "$(date +%T) quit; dumps in $R (xr-layer.log.stereoN / hudN); DUMP lines above name them in order"
