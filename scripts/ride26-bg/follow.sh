#!/bin/bash
# follow.sh ROUNDS NAME_RE [DY] - teleport after the objective marker NAME_RE (status, then go there), ROUNDS times
N=$1; RE=$2; DY=${3:-100}; R=$(cat /tmp/b26-ride/bg/R)
sync() { local c0; c0=$(grep -c '^STORY status:' $R/somvr.log); /tmp/b26-ride/bg/q.sh "$@" "word story"
  for k in $(seq 1 90); do sleep 1; [ "$(grep -c '^STORY status:' $R/somvr.log)" -gt "$c0" ] && return 0; done; return 1; }
for i in $(seq 1 $N); do
  l0=$(wc -l < $R/somvr.log); sync "status" || { echo "no sync"; break; }
  p=$(tail -n +$l0 $R/somvr.log | grep -E "^OBJECTIVE status marker .*'$RE'" | tail -1 | grep -oE 'at \(-?[0-9]+ -?[0-9]+ -?[0-9]+\)' | tr -d '()' | cut -c4-)
  nm=$(tail -n +$l0 $R/somvr.log | grep -E "^OBJECTIVE status marker" | grep -oE "'[A-Za-z_0-9]+'[^:]*at \([-0-9 ]*\)" | tr '\n' ';')
  sub=$(tail -n +$l0 $R/somvr.log | grep -E '^STORY t=.*subtitle ch=1 .[A-Z]' | tail -1 | cut -d"'" -f2)
  echo "$(date +%T) round $i: $nm | $sub"
  [ -z "$p" ] && { echo "no $RE marker: stop"; break; }
  set -- $p; sync "go $1 $(( $2 + DY )) $3" "wait ${WAIT:-3}" || break
done
