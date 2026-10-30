#!/bin/bash
# q.sh CMD... : append commands to the driver's ctl queue (next index kept in DIR/next)
D=/tmp/b26-ride/bg/ctl; mkdir -p $D; n=$(cat $D/next 2>/dev/null || ls $D | grep -c '^cmd[0-9]*\.txt$')
for c in "$@"; do n=$((n+1)); printf '%s\n' "$c" > $D/.t && mv $D/.t $D/cmd$n.txt; done; echo $n > $D/next
