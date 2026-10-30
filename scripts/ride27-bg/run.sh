#!/bin/bash
# run.sh - som-b27-ride2: one Big Game boot (the slot, the gate, story22-drive.sh's ctl mode) + after.sh (the follow
# loop, the caragor steps, mount27.sh)
cd /tmp/b27-ride2/bg; rm -rf ctl; mkdir -p ctl; rm -f R
bash ./boot2.sh x "SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 SOMVR_RIDE_TRACE=4000 SOMVR_RIDE_SEAT=on" > /tmp/b27-ride2/boot2-$(date +%H%M).log 2>&1 &
BP=$!
bash ./after.sh > /tmp/b27-ride2/after-$(date +%H%M).log 2>&1 &
AP=$!
wait $BP; echo "boot2 rc=$?"; sleep 5; kill $AP 2>/dev/null; wait $AP 2>/dev/null; echo "end $(date +%T)"
