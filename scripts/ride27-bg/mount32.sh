#!/bin/bash
# mount32.sh - (attached to a running boot, seated) Big Game past the mount: every ~2.5 s the shown objective marker
# (`objective status`), `ride steer X Z` toward it, a bite (RT tap) every other round; off the beast = the hand closes
# at the game's offer again; SECS at most; then quit
R=$(cat /tmp/b27-ride2/bg/R); L=$R/somvr.log; SECS=${1:-600}
w() { printf '%s' "$1" > "$R/somvr.trigger"; sleep "${2:-0.8}"; }
t() { { for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; sleep 0.3; }
B0=$(stat -c %s "$L"); since() { tail -c +"$B0" "$L"; }
pick() { local l0; l0=$(wc -l < "$L"); w "objective status" 1.0
  tail -n +"$l0" "$L" | grep -aE "^OBJECTIVE status marker" | grep -v "shown never" | awk '{m=$0; sub(/.*shown /,"",m); split(m,a," "); print a[1], $0}' \
    | sort -n | head -1 | grep -oE "'[A-Za-z_0-9]+'.* at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)" ; }
T0=$(date +%s); k=0; last=""
while [ $(( $(date +%s) - T0 )) -lt "$SECS" ]; do
  k=$((k+1)); p=$(pick); xyz=$(echo "$p" | grep -oE '\(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()')
  if [ -n "$xyz" ]; then set -- $xyz; w "ride steer $1 $3" 0.1; fi
  n=$(echo "$p" | cut -d"'" -f2); [ "$n" != "$last" ] && { echo "$(date +%T) round $k: $(echo "$p" | cut -c1-120)"; last="$n"; }
  if since | grep -a -E "^RIDE t=" | tail -1 | grep -qE "off: standing|dismounted"; then
    echo "$(date +%T) off the beast"; w "ride steer off" 0.1
    for q in $(seq 1 12); do since | tail -c 60000 | grep -aqE "^RIDE t=.* the game offers the mount" && break; w "stick 0 1 0 0.4" 0.5; done
    w "hands squeeze L 1" 2.5; w "hands squeeze L 0" 0.3; B0=$(stat -c %s "$L")
  fi
  [ $((k % 2)) = 0 ] && { t "btn R trigger 1"; sleep 0.2; t "btn R trigger 0"; }
  sleep 0.4
done
w "ride steer off" 0.2; w "ride status" 0.5
echo quit >> "$R/cmd"; echo "$(date +%T) quit asked"
