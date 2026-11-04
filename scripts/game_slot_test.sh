#!/bin/bash
# game_slot_test.sh - the self-test of scripts/game_slot.sh (som-b35-slots, docs/re-xr-pipeline.md 158): the lock rules
# in a folder of its own (GAME_SLOT_TMP; the real /tmp/somvr-*.lock are never touched), the VR gate held open.
#   bash scripts/game_slot_test.sh      -> "game_slot_test: N passed, 0 failed"
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/gstest.XXXXXX"); trap 'rm -rf "${T:?}"' EXIT
export GAME_SLOT_TMP=$T GAME_SLOT_TEST_GATE=open GAME_SLOT_GAP_S=2 GAME_SLOTS=3
G() { bash "$HERE/game_slot.sh" "$@"; }
P=0; F=0
ok() { if eval "$2"; then P=$((P + 1)); else F=$((F + 1)); echo "FAIL: $1 ($2)"; fi; }
ok "the lock names" '[ "$(G lock 1)" = "$T/somvr-boot.lock" ] && [ "$(G lock 3)" = "$T/somvr-slot3.lock" ]'
ok "auto: the extra slots first" '[ "$(G take alice auto 2>/dev/null)" = 2 ]'
ok "auto: then slot 3" '[ "$(G take bob auto 2>/dev/null)" = 3 ]'
ok "auto: slot 1 last" '[ "$(G take carol auto 2>/dev/null)" = 1 ]'
ok "auto: all busy = exit 1" '! G take dave auto 2>/dev/null'
ok "the owner file" '[ "$(head -1 "$T/somvr-slot2.lock/owner" | cut -d" " -f1)" = alice ]'
ok "release only names its own" 'G release dave 2 > /dev/null; [ -d "$T/somvr-slot2.lock" ]'
ok "a prefix of a name is not the name" 'G release ali 2 > /dev/null; [ -d "$T/somvr-slot2.lock" ]'
ok "release by name" 'G release alice 2 > /dev/null; [ ! -d "$T/somvr-slot2.lock" ]'
ok "the fair gap holds the releaser off" '! G take alice auto 2>/dev/null'
ok "another owner may take it" '[ "$(G take dave auto 2>/dev/null)" = 2 ]'
ok "the gap waited out (WAIT_S)" 'G release dave 2 > /dev/null; [ "$(G take dave 2 10 2>/dev/null)" = 2 ]'
ok "an old-protocol owner line (name + time) is released by its name only" \
  'mkdir "$T/x.lock" && echo "som-b33-story6 18:48:34" > "$T/x.lock/owner" && . "$HERE/game_slot.sh" && gs_names "$T/x.lock" som-b33-story6 && ! gs_names "$T/x.lock" som-b33'
ok "solo, one try while others hold slots: refused, nothing half-held" \
  '! G take erin solo 2>/dev/null && [ ! -d "$T/somvr-slots-solo.lock" ] && head -1 "$T/somvr-boot.lock/owner" | grep -q "^carol "'
G take erin solo 40 > "$T/solo.out" 2>/dev/null & SP=$!
sleep 1
ok "a solo request stops others' new takes" 'G release bob 3 > /dev/null; sleep 0.5; ! G take frank 3 2>/dev/null'
G release dave 2 > /dev/null; G release carol 1 > /dev/null
wait $SP
ok "solo got every slot" '[ "$(cat "$T/solo.out")" = solo ] && for n in 1 2 3; do head -1 "$(G lock $n)/owner" | grep -q "^erin " || exit 1; done'
ok "solo's release frees everything" 'G release erin all > /dev/null; [ -z "$(ls -d "$T"/somvr-*.lock 2>/dev/null)" ]'
ok "slot 4 of 3 refused" '! G take gina 4 2>/dev/null'
mkdir "$T/somvr-slot3.lock"; echo "ghost 00:00" > "$T/somvr-slot3.lock/owner"; echo 999999 > "$T/somvr-slot3.lock/pid"
ok "a fresh lock without a game is not stale" '! G take hank 3 2>/dev/null'
touch -d '2 hours ago' "$T/somvr-slot3.lock/owner"
ok "an old lock without a game is stale (slot >= 2)" '[ "$(G take hank 3 2>/dev/null)" = 3 ]'
mkdir "$T/somvr-boot.lock"; echo "old 00:00" > "$T/somvr-boot.lock/owner"; touch -d '3 hours ago' "$T/somvr-boot.lock/owner"
ok "slot 1 is never removed as stale (its old rule, by hand)" '! G take ivan 1 2>/dev/null && [ -d "$T/somvr-boot.lock" ]'
sleep 30 & LIVE=$!
G release hank 3 > /dev/null; mkdir "$T/somvr-slot3.lock"; echo "jo 00:00" > "$T/somvr-slot3.lock/owner"
echo $LIVE > "$T/somvr-slot3.lock/pid"; touch -d '3 hours ago' "$T/somvr-slot3.lock/owner"
ok "an old lock whose game runs is not stale" '! G take kim 3 2>/dev/null && [ -d "$T/somvr-slot3.lock" ]'
kill $LIVE 2>/dev/null; wait $LIVE 2>/dev/null
rm -rf "${T:?}"/somvr-*.lock
ok "GAME_SLOTS=1: auto takes slot 1 only" '[ "$(GAME_SLOTS=1 G take lee auto 2>/dev/null)" = 1 ] && [ ! -d "$T/somvr-slot2.lock" ]'
ok "GAME_SLOTS=1: then none" '! GAME_SLOTS=1 G take max auto 2>/dev/null'
ok "the gate test file closes the VR gate (it can only add a stop)" \
  'touch "$T/gt" && [ "$(GAME_SLOT_GATE_TEST=$T/gt bash -c ". \"$HERE/game_slot.sh\"; gs_vr_why")" = "the gate test file $T/gt" ]'
# a game's big environment, its SOMVR_LOG near the start, `set -o pipefail` (b35's first measurement: a game ended at 145 s)
BIG=$(head -c 30000 /dev/zero | tr '\0' x)
env -i SOMVR_LOG=/r/somvr.log PAD1="$BIG" PAD2="$BIG" sleep 60 & EP=$!
sleep 0.3
old_bad=0; new_bad=0
for i in $(seq 1 60); do
  bash -c 'set -o pipefail; tr "\0" "\n" < /proc/$1/environ 2>/dev/null | grep -qxF "SOMVR_LOG=/r/somvr.log"' x $EP || old_bad=$((old_bad + 1))
  bash -c 'set -o pipefail; . "$2"; gs_env_has "$1" "SOMVR_LOG=/r/somvr.log"' x $EP "$HERE/game_slot.sh" || new_bad=$((new_bad + 1))
done
kill $EP 2>/dev/null; wait $EP 2>/dev/null
echo "(the old piped check under pipefail: $old_bad of 60 missed - the race is rare; gs_env_has has none by construction)"
ok "gs_env_has never misses it ($new_bad of 60)" '[ $new_bad = 0 ]'
ok "gs_env_has: a line not there" '! bash -c ". \"$HERE/game_slot.sh\"; gs_env_has $$ SOMVR_LOG=/nowhere"'
# build 35 (the 21:05 race): a user's launcher's solo request user-session with its pid - no take, the VR gate closed;
# stale when that launcher is gone (or after 60 s with no pid)
rm -rf "${T:?}"/somvr-*.lock
sleep 30 & UL=$!
mkdir "$T/somvr-slots-solo.lock"; echo "user-session 21:05:00 (the user's Windows VR session)" > "$T/somvr-slots-solo.lock/owner"
echo $UL > "$T/somvr-slots-solo.lock/pid"
ok "a live user-session request: no slot taken" '! G take nina auto 2>/dev/null && [ -z "$(ls -d "$T"/somvr-slot[0-9].lock "$T"/somvr-boot.lock 2>/dev/null)" ]'
ok "a live user-session request: the VR gate is closed (its reason)" \
  'bash -c ". \"$HERE/game_slot.sh\"; gs_vr_why" | grep -q "the user.s VR session is starting or running (the slots. solo request user-session, launcher pid $UL)"'
ok "a live user-session request: another solo request waits" '! G take otto solo 2>/dev/null && head -1 "$T/somvr-slots-solo.lock/owner" | grep -q "^user-session "'
kill $UL 2>/dev/null; wait $UL 2>/dev/null
ok "its launcher gone: the request is stale, the next take removes it" '[ "$(G take pia auto 2>/dev/null)" = 2 ] && [ ! -d "$T/somvr-slots-solo.lock" ]'
ok "... and the VR gate opens again (no user-session line)" '! bash -c ". \"$HERE/game_slot.sh\"; gs_user_why"'
G release pia all > /dev/null
mkdir "$T/somvr-slots-solo.lock"; echo "user-session 21:06:00" > "$T/somvr-slots-solo.lock/owner"
ok "a fresh user-session request with no pid yet holds" '! G take quinn auto 2>/dev/null'
touch -d '2 minutes ago' "$T/somvr-slots-solo.lock/owner"
ok "with no pid after 60 s it is stale" '[ "$(G take quinn auto 2>/dev/null)" = 2 ] && [ ! -d "$T/somvr-slots-solo.lock" ]'
G release quinn all > /dev/null
mkdir "$T/somvr-slots-solo.lock"; echo "coordinator final 21:07:00" > "$T/somvr-slots-solo.lock/owner"; touch -d '3 hours ago' "$T/somvr-slots-solo.lock/owner"
ok "another owner's solo request is never removed as stale" '! G take ruth auto 2>/dev/null && [ -d "$T/somvr-slots-solo.lock" ]'
rm -rf "${T:?}"/somvr-*.lock
# b37 (som-b37-slotsvram, docs 158.5): the render mode and take's VRAM gate on a fake card (GAME_SLOT_CARD: its
# mem_info_* files; a lock's test_mem = its game's "resident_vram total_vram resident_gtt total_gtt" MiB; none = starting)
mkdir -p "$T/card"; echo $((16368 * 1048576)) > "$T/card/mem_info_vram_total"
card() { echo $(($1 * 1048576)) > "$T/card/mem_info_vram_used"; echo $(($2 * 1048576)) > "$T/card/mem_info_gtt_used"; }
export GAME_SLOT_CARD=$T/card
card 15000 1500
ok "vram: a game alone always runs, even on a full card; slot 2's mode light" \
  '[ "$(G take va auto 2>/dev/null)" = 2 ] && [ "$(cat "$T/somvr-slot2.lock/render")" = light ]'
card 3000 1500
ok "vram: a light boot beside a starting light one (its 2816 reserved) fits" '[ "$(G take vb auto 2>/dev/null)" = 3 ]'
card 4000 1500
ok "vram: vr in slot 1 beside two starting light boots: refused, said why" \
  '! G take vc auto 2> "$T/vc.err" && grep -q "VRAM: slot 1 at vr needs 6656 + 1024 MiB; free 6736 " "$T/vc.err"'
echo "2700 2800 200 200" > "$T/somvr-slot2.lock/test_mem"; echo "2700 2800 200 200" > "$T/somvr-slot3.lock/test_mem"; card 8400 2000
ok "vram: ... fits once both are in the world (their memory counted once); slot 1's mode vr" \
  '[ "$(G take vc auto 2> "$T/vc.err")" = 1 ] && [ "$(cat "$T/somvr-boot.lock/render")" = vr ] && grep -q "slot 1 at vr fits" "$T/vc.err"'
G release vc 1 > /dev/null; card 8400 8000
ok "vram: a spill in progress (GTT far above its idle level) refuses" \
  '! G take vd 1 2> "$T/vd.err" && grep -q "a spill in progress - GTT 8000 MiB" "$T/vd.err"'
card 8400 4000
ok "vram: the spilled GTT counts as VRAM wanted: vr refused, light fits" \
  '! G take vd 1 2>/dev/null && [ "$(WIN_RENDER=light G take vd 1 2>/dev/null)" = 1 ] && [ "$(cat "$T/somvr-boot.lock/render")" = light ]'
G release vd 1 > /dev/null; card 8400 1500; rm -f "$T/somvr-slot3.lock/test_mem"
ok "vram: a vr boot waits for room with a line saying why" \
  '! WIN_RENDER=vr G take ve 1 3 2> "$T/ve.err" && grep -q "ve waits - VRAM: slot 1 at vr needs" "$T/ve.err"'
ok "vram: WIN_RENDER=bad refused" '! WIN_RENDER=ultra G take vf auto 2>/dev/null'
G release va all > /dev/null; G release vb all > /dev/null; card 1500 1500
ok "vram: WIN_RENDER=vr takes slot 1 first" '[ "$(WIN_RENDER=vr G take vg auto 2>/dev/null)" = 1 ] && [ "$(cat "$T/somvr-boot.lock/render")" = vr ]'
ok "vram: ... and then another slot when vr fits (beside the starting vr: 16368-1500-6656 >= 7680)" \
  '[ "$(WIN_RENDER=vr G take vh auto 2>/dev/null)" = 2 ] && [ "$(cat "$T/somvr-slot2.lock/render")" = vr ]'
ok "vram: a third vr does not fit beside two starting vr boots" '! WIN_RENDER=vr G take vi auto 2>/dev/null'
ok "vram: status shows each slot's mode and what a new boot gets" \
  'G status | grep -q "slot 2: vr (needs 6656); its game: not running" && G status | grep -q "a light boot in slot 3 now: NO - slot 3 at light needs"'
G release vg all > /dev/null; G release vh all > /dev/null; card 16000 1500
ok "vram: solo is not gated (it holds every slot)" '[ "$(G take vj solo 2>/dev/null)" = solo ]'
G release vj all > /dev/null; unset GAME_SLOT_CARD
# b37: render_need.sh - the looks/perf drivers' check of their run's mode
RN() { bash "$HERE/render_need.sh" "$@" > "$T/rn.out" 2>&1; }
mkdir -p "$T/rl" "$T/rw" "$T/rv" "$T/ro"; echo "boot_check.sh --mod X" > "$T/rl/launch-cmd.txt"
for r in rw rv ro; do echo "# headless_win.sh 1 (the Windows build; MOD /m; slot 2; render light)" > "$T/$r/launch-cmd.txt"; : > "$T/$r/cmd"; done
echo "light (slot 2's default) X,TextureQuality:1" > "$T/rw/render.mode"; echo "vr (WIN_RENDER) X" > "$T/rv/render.mode"
echo "[1] render.cfg: the PC's detection with the VR preset X (put back after the run)" > "$T/ro/summary.txt"
ok "render_need: a Linux run passes" 'RN "$T/rl" x'
ok "render_need: a vr run passes" 'RN "$T/rv" x'
ok "render_need: an older harness's VR-preset run passes" 'RN "$T/ro" x'
ok "render_need: a light run fails, says why, asks the run to quit" \
  '! RN "$T/rw" looks36 && grep -q "FAIL render: looks36 judges" "$T/rn.out" && [ "$(cat "$T/rw/cmd")" = quit ]'
ok "render_need: RENDER_ANY=1 lets a light run through" 'RENDER_ANY=1 RN "$T/rw" x'
rm -rf "${T:?}"/somvr-*.lock
ok "the real locks untouched" '[ "$(G lock 1)" != /tmp/somvr-boot.lock ]'
echo "game_slot_test: $P passed, $F failed"
[ $F = 0 ]
