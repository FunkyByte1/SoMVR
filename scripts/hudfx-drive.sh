#!/bin/bash
# hudfx-drive.sh - SOMVR_HUD_FX's proof (handoff/HANDOFF-hud.md, the build 12 HUD round): one headless boot with build
# 12's switches (SOMVR_HUD_FX=hide among them), the test hands (SOMVR_HANDS_TEST=1), the layer's test file. Prologue as
# execute-drive.sh's. Round 1 as booted (hide), round 2 after the word `hudfx game` (the game's look, as build 11), round
# 3 after `hudfx hide` again: a practice infantry, light slashes with the test chase until the game's charge (8 hits);
# stereo dumps (the layer's `dump`: <log>.stereoN / hudN) after hits 2-3, 5-6 and at the charge, the watch turned to the
# eyes (the streak's `x N` there), a mark at the charge; the orc killed by more slashes (the XP gain: the band, the XP
# bar's line), dumps then; the bow's aim (the left trigger: its prompts in the band) once in round 1. DUMP lines name the
# dumps in order (scripts/xr_compose.py RUN draws them). HD_BOW=1: each round the bow aimed at the new orc first (its
# prompt in the band); HD_WATCH=1: the watch raised at hit 3 (its `x N`; the pause ends the streak: no charge then);
# HD_EXEC=1 (with HD_ROUNDS=2): at the charge the execution (the right trigger held, heavy chops, as execute-drive.sh),
# five dumps after the kill (the XP gain in the band, the XP bar's line), then a second orc: 3 hits, the watch raised.
SOM=$HOME/Projects/flat2vr/ShadowOfMordor
WT=${HD_WT:-$(basename "$(cd "$(dirname "$0")/../.." && pwd -P)")}
LMS=${HD_LIGHT_MS:-600}
T0=$(date +%s)
R=${HD_RUN:-}
for i in $(seq 1 120); do
  [ -n "$R" ] && break
  for d in $(ls -dt $SOM/logs/boot-check-2026* 2>/dev/null | head -3); do
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
bail() { echo "$(date +%T) STOP: $1"; echo quit >> "$R/cmd"; exit 1; }
for i in $(seq 1 150); do
  userin && bail "the user is in VR"
  grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" 2>/dev/null && break; sleep 3
done
grep -q "^PRACTICE t=.* arrived" "$R/somvr.log" || bail "no arrival"
echo "arrived $(date +%T)"
sleep 8
N=0
w() { userin && bail "the user is in VR"; printf '%s' "$1" > "$R/somvr.trigger"; echo "$(date +%T) word: $1"; sleep "${2:-0.8}"; }
t() { userin && bail "the user is in VR"; N=$((N + 1))
      { echo "# step $N"; for l in "$@"; do echo "$l"; done; } > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"
      echo "$(date +%T) xrtest $N: $*"; sleep 0.6; }
d() { t "dump"; echo "$(date +%T) DUMP $1 (streak $(streak))"; sleep "${2:-1.0}"; }
m() { w "mark $1" "${2:-4}"; }
cnt() { grep -cE "$1" "$R/somvr.log"; }
streak() { local v; v=$(grep -E '^EXECUTE streak t=' "$R/somvr.log" | tail -1 | awk '{print $4}'); echo "${v:-0}"; }
ready() { [ "$(cnt '^EXECUTE ready t=')" -gt "$RD0" ]; }
dead() { [ "$(cnt '^COMBAT death orc')" -gt "$DD0" ]; }
WATCH="hand L head 5 -16 32 0 -90 -130"       # the left forearm across the chest, the back of the wrist to the eyes
LOW="hand L head -25 -35 30 0 0 0"
NEAR="hand L head 0 -10 32 0 -90 -120"         # the watch higher and nearer (hud-drive.sh's watch-near, 2 cm up)
REST="hands test R 28 -40 20 0 0 0"
SLOT="hands test R 10 -10 -15 0 0 0"
OUT="hands test R 30 -25 40 0 -90 0"
LA="hands test R 30 -10 45 0 -35 0"
BEHIND="hands test R 22 -35 -30 0 180 0"
SHOW="hands test R 22 -25 40 75 -15 0"   # the blade nearly upright ahead (execute-drive.sh's)
LUNGE_A="hands test R 40 -8 35 0 -90 0"
drawn() { tail -800 "$R/somvr.log" | grep -qE '^SLOTS .*sword: .*(now in the hand|kept in the hand)'; }
slash() { if [ $((K % 2)) = 1 ]; then w "hands swing R -10 -10 50 0 35 0 $LMS" 0.6; else w "hands swing R 30 -10 45 0 -35 0 $LMS" 0.6; fi; K=$((K + 1)); }
# 0. the controls card away, the left hand tracked, the sword drawn
t "btn R squeeze 0"; t "btn R squeeze 1"; sleep 1; t "btn R squeeze 0"
grep -q 'card: put away' "$R/xr-layer.log" || { t "btn R a 1"; t "btn R a 0"; }
w "hands test L -25 -35 30 0 0 0"
t "$LOW"
w "$REST" 2
drawn || { w "$SLOT" 0.7; w "hands squeeze R 1" 0.6; w "$OUT" 0.6; w "hands squeeze R 0" 0.5; }
d arrival 1
for rd in $(seq 1 "${HD_ROUNDS:-3}"); do
  P=$( [ $rd = 2 ] && echo g || echo h )
  echo "$(date +%T) ROUND $rd ($P)"
  RD0=$(cnt '^EXECUTE ready t='); DD0=$(cnt '^COMBAT death orc'); KK0=$(cnt '^EXECUTE kill t='); K=1
  w "practice spawn infantry" 3
  if [ "${HD_BOW:-0}" = 1 ]; then   # the bow aimed at the new orc ahead: its prompts in the band
    t "btn L trigger 1"; sleep 1.5
    d "r$rd-$P-bow" 0.5
    t "btn L trigger 0"; sleep 0.8
  fi
  w "$LA" 0.5; w "execute chase on" 0.5
  s2=0; s5=0; sw=0
  for k in $(seq 1 60); do
    ready && break
    dead && break
    slash
    s=$(streak)
    [ "$s2" = 0 ] && [ "$s" -ge 2 ] && { s2=1; d "r$rd-$P-streak$s" 0.2; }
    [ "$s5" = 0 ] && [ "$s" -ge 5 ] && { s5=1; d "r$rd-$P-streak$s" 0.2; }
    if [ "${HD_WATCH:-0}" = 1 ] && [ "$sw" = 0 ] && [ "$s" -ge 3 ]; then   # the watch mid-streak: its `x N`
      sw=1; t "$WATCH s 0.25"; sleep 0.5; d "r$rd-$P-watch-streak$s" 0.2; t "$LOW s 0.25"; sleep 0.3
    fi
    [ "$(streak)" -ge 7 ] && for j in 1 2 3 4; do ready && break; sleep 0.2; done
  done
  if ready && [ "${HD_EXEC:-0}" = 1 ]; then   # at once (the charge lapses ~2.5 s after the last hit): the trigger held,
    printf '%s\n' "btn R trigger 1" > "$R/xrtest.txt"; echo "$(date +%T) xrtest: btn R trigger 1"; sleep 0.25
    w "$SHOW" 0.4; w "hands swing R -20 -18 62 0 90 0 300" 1.0   # the blade up, a heavy chop (execute-drive.sh's)
  elif ready; then
    d "r$rd-$P-charge" 0.2
    m "hf${rd}a-$P-charge" 1.5
  fi
  if ready; then
    if [ "${HD_EXEC:-0}" = 1 ]; then   # the execution (as execute-drive.sh): the right trigger held, heavy chops
      for k in $(seq 1 6); do
        [ "$(cnt '^EXECUTE kill t=')" -gt "$KK0" ] && break
        w "$LUNGE_A" 0.5; w "hands swing R -20 -18 62 0 90 0 300" 1.0; w "hands swing R 40 -8 35 0 -90 0 900" 1.0
      done
      t "btn R trigger 0"; w "execute chase off" 0.3
      grep -E '^EXECUTE kill t=' "$R/somvr.log" | tail -1 | cut -c1-160
      w "$BEHIND" 0.2
      for j in 1 2 3 4 5; do d "r$rd-$P-xp$j" 0.6; done
      m "hf${rd}b-$P-xp" 1
      # a second orc: three hits, then the watch raised (the streak's `x N` on it; the 3 s lapse ends the streak)
      w "practice spawn infantry" 3; w "$LA" 0.5; w "execute chase on" 0.5
      for k in $(seq 1 12); do [ "$(streak)" -ge 3 ] && break; slash; done
      t "$NEAR s 0.25"; sleep 0.5; d "r$rd-$P-near-streak$(streak)" 0.2
      t "$WATCH s 0.2"; sleep 0.4; d "r$rd-$P-watch2-streak$(streak)" 0.2; t "$LOW s 0.25"; sleep 0.3
    else
      t "$WATCH s 0.3"; sleep 0.8
      d "r$rd-$P-watch" 0.3
      t "$LOW s 0.3"; sleep 0.4
    fi
  else
    echo "no charge in round $rd (streak $(streak))"
  fi
  # the orc down with more slashes: the kill's XP (the band; the XP bar's line in the game's look)
  for k in $(seq 1 40); do dead && break; [ "$(cnt '^EXECUTE kill t=')" -gt "$KK0" ] && break; slash; done
  w "execute chase off" 0.3
  if [ "${HD_EXEC:-0}" != 1 ]; then
    d "r$rd-$P-kill" 0.6
    d "r$rd-$P-kill2" 0.6
    m "hf${rd}b-$P-kill" 2
  fi
  if [ $rd = 1 ] && [ "${HD_EXEC:-0}" != 1 ]; then   # the bow's aim (its prompts in the band)
    t "btn L trigger 1"; sleep 1.5
    d "r1-$P-bow" 0.5
    t "btn L trigger 0"; sleep 1
  fi
  [ $rd = 1 ] && w "hudfx game" 1
  [ $rd = 2 ] && w "hudfx hide" 1
  sleep 2
done
echo "steps done $(date +%T)"
w "endlogs" 2
sleep 2
echo quit >> "$R/cmd"
echo "quit $(date +%T)"
