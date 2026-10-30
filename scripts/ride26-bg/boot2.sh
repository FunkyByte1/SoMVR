#!/bin/bash
# boot.sh STEPS [EXTRA_ENV] - som-b26-ride: wait for the VR gate and the boot lock, then one headless boot + ride26-drive.sh
W=/home/bara/.t3/worktrees/flat2vr/t3-5f7724e9/ShadowOfMordor
ME=som-b26-ride; LK=/tmp/somvr-boot.lock
STEPS=$1; EXTRA=${2:-}
gate() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && { echo "user in VR (headset unit)"; return 1; }
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && { echo "user in VR (status)"; return 1; }
  pgrep -x monado-service >/dev/null && { echo "monado running"; return 1; }
  pgrep -f '[H]and of Fate 2\.' >/dev/null && { echo "HoF2 running"; return 1; }
  journalctl -k --since "-3h" 2>/dev/null | grep -qi 'VRAM is lost' && { echo "AMDGPU RESET"; return 2; }
  return 0
}
userok() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 1
  grep -q '"user": true' /run/user/$(id -u)/vr-session/status.json 2>/dev/null && return 1
  pgrep -f '[H]and of Fate 2\.' >/dev/null && return 1
  return 0
}
for i in $(seq 1 5400); do
  why=""
  if userok && mkdir $LK 2>/dev/null; then # the lock first; then the last boot's monado must be gone (60 s at most)
    echo $ME > $LK/owner; echo "$(date +%T) lock taken"
    for k in $(seq 1 60); do why=$(gate); rc=$?; [ $rc != 1 ] && break; sleep 1; done
    [ $rc = 2 ] && { grep -q "$ME" $LK/owner && rm -rf /tmp/somvr-boot.lock; echo "$(date +%T) STOP: $why"; exit 3; }
    [ $rc = 0 ] && { echo "$(date +%T) gate ok"; break; }
    grep -q "$ME" $LK/owner && rm -rf /tmp/somvr-boot.lock; echo "$(date +%T) gate not ok after 60 s ($why): lock released"; sleep 60
  fi
  [ $((i % 60)) = 1 ] && echo "$(date +%T) waiting: ${why:-lock held by $(cat $LK/owner 2>/dev/null)}"
  sleep 2
done
grep -q "$ME" $LK/owner 2>/dev/null || { echo "no slot"; exit 4; }
cd $W
ST_PLAN=ctl ST_SCRIPT=/tmp/b26-ride/bg/start.txt ST_CTL_DIR=/tmp/b26-ride/bg/ctl bash scripts/story22-drive.sh > /tmp/b26-ride/story-$(date +%H%M).log 2>&1 &
DP=$!
HEADLESS_HOLD=2400 SOMVR_EXTRA="$(grep -E '^SOMVR_[A-Z0-9_]+=' scripts/headset-build26.env | tr '\n' ' ') SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217 $EXTRA" setsid -w scripts/headless_vr.sh $W/mod/somvr > /tmp/b26-ride/boot-$(date +%H%M).log 2>&1
echo "$(date +%T) boot done rc=$?"
wait $DP
grep -q "$ME" $LK/owner 2>/dev/null && rm -rf /tmp/somvr-boot.lock && echo "$(date +%T) lock released"
