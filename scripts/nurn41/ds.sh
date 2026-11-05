# nurn41/ds.sh - the death struggle (CStruggleAim, the last chance) answered the VR way (SOMVR_STRUGGLE=thrust): alternating
# thrusts of both hands (each thrust moves the hand, so the cursor sweeps; a thrust inside the ring = the press), the
# game's pausing struggle tutorial answered by A. dstruggle [S]: while a struggle runs (up to S s, 40). dwatch S: for S s,
# every death struggle that starts answered. Lines: `NU struggle: won|lost after N thrusts`.
dstruggle() { local s0=$(lines) h=R k n=0; for k in $(seq 1 $(( ${1:-40} * 3 ))); do
    since $s0 '^STRUGGLE t=.* over after' | grep -q . && break; state | grep -q 'paused=1' && { tap R a 0.3; continue; }
    if [ $h = R ]; then w "hands swing R 10 -12 70 0 0 0 110" 0.15; w "$REST" 0.2; h=L; else w "hands swing L -10 -12 70 0 0 0 110" 0.15; w "$LREST" 0.2; h=R; fi; n=$((n + 1)); done
  sleep 1; echo "$(date +%T) NU struggle: $(since $s0 "Talion attack '[A-Za-z_]*PreDeath_PCWin'" | grep -q . && echo won || echo lost/unknown) after $n thrusts ($(since $s0 '^STRUGGLE t=.* over after' | grep -oE 'presses of ours [0-9]+' | head -1))"; }
dwatch() { local T=$((SECONDS + ${1:-30})) s0; while [ $SECONDS -lt $T ]; do s0=$(lines)
    for i in $(seq 1 20); do since $s0 '^STRUGGLE t=.* start' | grep -q . && break; state | grep -q 'paused=1' && tap R a 0.3; sleep 0.5; done
    since $s0 '^STRUGGLE t=.* start' | grep -q . && dstruggle 40; done; }
