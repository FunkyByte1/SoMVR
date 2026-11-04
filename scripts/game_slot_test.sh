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
ok "the real locks untouched" '[ "$(G lock 1)" != /tmp/somvr-boot.lock ]'
echo "game_slot_test: $P passed, $F failed"
[ $F = 0 ]
