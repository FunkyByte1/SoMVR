#!/bin/bash
# game_slot.sh - the game slots (som-b35-slots, docs/re-xr-pipeline.md 158, handoff/HANDOFF-winport.md "game slots").
# Slot 1 is /tmp/somvr-boot.lock exactly as before (any boot, Linux or Windows; every older brief's mkdir); slots 2..N
# are /tmp/somvr-slot<N>.lock, for WINDOWS boots only (scripts/headless_vr.sh --win, HEADLESS_SLOT): each slot its own
# Proton prefix (logs/winport/prefix-slot<N>, a reflink copy of logs/winport/prefix), its own runtime folder
# ($XDG_RUNTIME_DIR/somvr-slot<N>: Monado's socket, gamescope's), its own wineserver (one per prefix), run folder, save
# copy and null audio. Solo = every slot (perf tests, the coordinator's suites). Run it OUTSIDE the Bash sandbox.
#   game_slot.sh take OWNER [N|auto|solo] [WAIT_S]   take slot N (default auto: the first free - the extra slots first,
#                         slot 1 last), or every slot (solo); waits up to WAIT_S seconds (default 0: one try) for a
#                         free slot, the fair gap and the VR gate; prints the slot (or "solo"); exit 1 = none
#   game_slot.sh release OWNER [N|all]               release slot N / every slot and the solo mark OWNER holds (only
#                         where the owner file names OWNER); starts OWNER's fair gap (3 minutes before its next take)
#   game_slot.sh status                              every slot: free, or its owner, age, run and game pid
#   game_slot.sh lock N | count | gate | prefix N    slot N's lock folder | the number of slots | exit 0 when a game
#                         may run (else the reason: the VR gate, an amdgpu reset, Hand of Fate 2) | make slot N's prefix
# Rules (logs/coord rules block, docs 158): the owner file's first line starts with the owner's name; release only if
# it names you; after a release wait 3 minutes before the next take (this tool enforces it for its own takes); a
# solo request (/tmp/somvr-slots-solo.lock) stops new takes of every slot but by its owner; a slot >= 2 whose game
# (the pid file headless_win.sh writes) is gone and whose lock is over 90 minutes old is stale (removed by `take`);
# slot 1's stale rule stays the old one (by hand: over 90 min and no boot_check.sh / headless_win.sh running).
# Sourced (`. game_slot.sh`), it only defines the gs_* functions (headless_win.sh uses gs_lock, gs_vr_why, gs_gpu_why).
GS_SLOTS=${GAME_SLOTS:-3}              # b35's measured count (docs 158.4); GAME_SLOTS=N overrides (1 = the old rule)
GS_SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
GS_RT=/run/user/$(id -u)
GS_TMP=${GAME_SLOT_TMP:-/tmp}         # (GAME_SLOT_TMP: the self-test's own folder, scripts/game_slot_test.sh)
GS_SOLO=$GS_TMP/somvr-slots-solo.lock
GS_GAP=$GS_TMP/somvr-slot-gap          # OWNER's last release time (the fair gap)
GS_GAP_S=180; GS_STALE_S=5400         # the fair gap, the stale age (the self-test may shorten both)
[ -n "${GAME_SLOT_TMP:-}" ] && { GS_GAP_S=${GAME_SLOT_GAP_S:-180}; GS_STALE_S=${GAME_SLOT_STALE_S:-5400}; }
gs_lock() { [ "$1" = 1 ] && echo "$GS_TMP/somvr-boot.lock" || echo "$GS_TMP/somvr-slot$1.lock"; }
gs_rtdir() { [ "$1" = 1 ] && echo "$GS_RT" || echo "$GS_RT/somvr-slot$1"; }
gs_key() { printf '%s' "$1" | tr -c 'A-Za-z0-9_.-' '_'; }
gs_names() {   # LOCKDIR OWNER: 0 = its owner file's first line is OWNER or starts with "OWNER "
  local l; l=$(head -1 "$1/owner" 2>/dev/null) || return 1
  case $l in "$2"|"$2 "*) return 0 ;; esac; return 1
}
# The user's launchers (scripts/som-vr-headset.sh, som-vr-headset-win.sh; build 35, the 21:05 race) take the solo request
# as `user-session` before they stop the test runs, with their pid in it: while that launcher lives no test game starts
# (gs_vr_why: every gate, headless_win.sh's guards and its 2-second monitor, boot_check's user_in_vr) and no slot is
# taken; a user-session request whose launcher is gone is stale (removed by the next take).
GS_USER_OWNER=user-session
gs_user_why() {   # exit 0 = a live user-session solo request (the reason on stdout)
  local p
  [ -d "$GS_SOLO" ] && gs_names "$GS_SOLO" "$GS_USER_OWNER" || return 1
  p=$(cat "$GS_SOLO/pid" 2>/dev/null)
  [ -n "$p" ] && kill -0 "$p" 2>/dev/null || return 1
  echo "the user's VR session is starting or running (the slots' solo request $GS_USER_OWNER, launcher pid $p)"; return 0
}
gs_user_stale() {   # a user-session request whose launcher is gone (or that never wrote its pid, after 60 s): removed
  local p
  [ -d "$GS_SOLO" ] && gs_names "$GS_SOLO" "$GS_USER_OWNER" || return 1
  p=$(cat "$GS_SOLO/pid" 2>/dev/null)
  if [ -n "$p" ]; then kill -0 "$p" 2>/dev/null && return 1; else [ "$(gs_age "$GS_SOLO")" -gt 60 ] || return 1; fi
  echo "game_slot: the solo request $(head -1 "$GS_SOLO/owner" 2>/dev/null) is stale (its launcher is gone): removed" >&2
  rm -rf "$GS_SOLO"; return 0
}
gs_vr_why() {   # the VR gate (boot_check's user_in_vr + the headset unit): the reason, exit 0 = the user is in VR
  local j       # (GAME_SLOT_GATE_TEST=FILE: FILE present = closed too - the stop's test; it can only add a stop)
  [ -n "${GAME_SLOT_GATE_TEST:-}" ] && [ -e "$GAME_SLOT_GATE_TEST" ] && { echo "the gate test file $GAME_SLOT_GATE_TEST"; return 0; }
  gs_user_why && return 0
  [ "$(cat /proc/1/comm 2>/dev/null)" = systemd ] || { echo "cannot see the host's processes (sandboxed shell?)"; return 0; }
  systemctl --user is-active -q somvr-headset 2>/dev/null && { echo "the headset session somvr-headset runs"; return 0; }
  if [ -f "$GS_RT/vr-session/status.json" ]; then
    j=$(tr -d ' \n' < "$GS_RT/vr-session/status.json" 2>/dev/null)
    [[ $j == *'"user":true'* ]] && { echo "status.json: \"user\": true"; return 0; }
  else echo "no $GS_RT/vr-session/status.json (the watcher is down: cannot tell)"; return 0; fi
  ss -xH state established 2>/dev/null | grep -q '/wivrn/comp_ipc' && { echo "a VR app is connected to WiVRn"; return 0; }
  pgrep -x vr-session > /dev/null 2>&1 && { echo "vr-session runs (the user is in VR)"; return 0; }
  return 1
}
gs_gpu_why() {   # [SINCE]: an amdgpu reset / VRAM lost / ring timeout in the kernel log (this boot, or since @SINCE)
  local s=(-b); [ -n "${1:-}" ] && s=(--since "@$1")
  journalctl -k "${s[@]}" --no-pager 2>/dev/null | grep -i -m1 -E 'amdgpu.*(GPU reset|VRAM is lost|ring .* timeout|GPU recovery|GPU hang)|VRAM is lost' && return 0
  return 1
}
gs_gate() {   # exit 0 = a test game may start now; else the reason on stdout
  local w
  [ -n "${GAME_SLOT_TMP:-}" ] && [ "${GAME_SLOT_TEST_GATE:-}" = open ] && return 0   # (the self-test only)
  w=$(gs_vr_why) && { echo "VR: $w"; return 1; }
  w=$(gs_gpu_why) && { echo "GPU: $w"; return 1; }
  pgrep -f '[H]and of Fate 2\.' > /dev/null && { echo "Hand of Fate 2 runs"; return 1; }
  return 0
}
gs_env_has() {   # PID LINE: the process's environment has LINE (no pipe: under `set -o pipefail` a `tr | grep -q`
  # can fail when grep stops first and tr gets SIGPIPE: a live game counted as gone)
  grep -qxF -- "$2" < <(tr '\0' '\n' < "/proc/$1/environ" 2>/dev/null)
}
gs_alive() { local p; p=$(cat "$1/pid" 2>/dev/null) && [ -n "$p" ] && kill -0 "$p" 2>/dev/null; }
gs_age() { echo $(( $(date +%s) - $(stat -c %Y "$1/owner" 2>/dev/null || stat -c %Y "$1" 2>/dev/null || date +%s) )); }
gs_stale() {   # N: slot N >= 2 whose game is gone and whose lock is over 90 minutes old
  local d; d=$(gs_lock "$1")
  [ "$1" -ge 2 ] && [ -d "$d" ] && ! gs_alive "$d" && [ "$(gs_age "$d")" -gt $GS_STALE_S ]
}
gs_gap_left() {   # OWNER: seconds left of its fair gap
  local f="$GS_GAP/$(gs_key "$1")" t
  [ -f "$f" ] || { echo 0; return; }
  t=$(( GS_GAP_S - ($(date +%s) - $(stat -c %Y "$f")) )); [ $t -gt 0 ] && echo $t || echo 0
}
gs_take1() {   # N OWNER: 0 = taken; 1 = busy; 3 = a solo request of another owner
  local d; d=$(gs_lock "$1")
  gs_user_stale || true
  [ -d "$GS_SOLO" ] && ! gs_names "$GS_SOLO" "$2" && return 3
  if gs_stale "$1"; then
    echo "game_slot: slot $1's lock is stale ($(head -1 "$d/owner" 2>/dev/null), $(gs_age "$d") s, no game): removed" >&2
    rm -rf "$d"
  fi
  mkdir "$d" 2>/dev/null || return 1
  echo "$2 $(date +%T)" > "$d/owner"
  return 0
}
gs_take() {   # OWNER [N|auto|solo] [WAIT_S]
  local own=${1:?owner} want=${2:-auto} wait=${3:-0} end n w got=() ok
  [[ $wait =~ ^[0-9]+$ ]] || { echo "WAIT_S must be whole seconds" >&2; return 1; }
  case $want in auto|solo) ;; [1-9]) [ "$want" -le "$GS_SLOTS" ] || { echo "slot $want: there are $GS_SLOTS" >&2; return 1; } ;;
    *) echo "slot: N, auto or solo" >&2; return 1 ;; esac
  end=$(( $(date +%s) + wait ))
  while :; do
    w=""
    [ "$(gs_gap_left "$own")" -gt 0 ] && w="the fair gap: $(gs_gap_left "$own") s left for $own"
    [ -z "$w" ] && { w=$(gs_gate) && w=""; }
    if [ -z "$w" ]; then
      case $want in
        solo)
          gs_user_stale || true
          mkdir "$GS_SOLO" 2>/dev/null && echo "$own $(date +%T)" > "$GS_SOLO/owner"   # (atomic: the first asker)
          if gs_names "$GS_SOLO" "$own"; then   # the request stops others' new takes; the busy slots drain
            ok=1
            for ((n = 1; n <= GS_SLOTS; n++)); do
              gs_names "$(gs_lock $n)" "$own" && continue
              gs_take1 $n "$own" || { ok=0; w="solo: waiting for slot $n ($(head -1 "$(gs_lock $n)/owner" 2>/dev/null))"; }
            done
            [ $ok = 1 ] && { echo solo; return 0; }
          else w="another solo request: $(head -1 "$GS_SOLO/owner" 2>/dev/null)"; fi ;;
        auto)
          for n in $(seq 2 "$GS_SLOTS") 1; do
            [ "$n" -le "$GS_SLOTS" ] || continue
            gs_take1 "$n" "$own" && { echo "$n"; return 0; }
          done
          w="every slot is busy" ; [ -d "$GS_SOLO" ] && w="a solo request: $(head -1 "$GS_SOLO/owner" 2>/dev/null)" ;;
        *)
          gs_take1 "$want" "$own"; case $? in 0) echo "$want"; return 0 ;;
            3) w="a solo request: $(head -1 "$GS_SOLO/owner" 2>/dev/null)" ;;
            *) w="slot $want is busy ($(head -1 "$(gs_lock "$want")/owner" 2>/dev/null))" ;; esac ;;
      esac
    fi
    [ "$(date +%s)" -ge "$end" ] && break
    sleep 2
  done
  [ "$want" = solo ] && gs_names "$GS_SOLO" "$own" && gs_release "$own" all nogap > /dev/null   # (nothing half-held)
  echo "no slot: $w" >&2; return 1
}
gs_release() {   # OWNER [N|all] [nogap]
  local own=${1:?owner} which=${2:-all} n d done=0
  for ((n = 1; n <= 9; n++)); do
    [ "$which" = all ] || [ "$which" = "$n" ] || continue
    d=$(gs_lock $n); [ -d "$d" ] && gs_names "$d" "$own" && { rm -rf "$d"; done=1; echo "released slot $n"; }
  done
  [ "$which" = all ] && [ -d "$GS_SOLO" ] && gs_names "$GS_SOLO" "$own" && { rm -rf "$GS_SOLO"; echo "released the solo request"; }
  [ $done = 1 ] && [ "${3:-}" != nogap ] && { mkdir -p "$GS_GAP" && touch "$GS_GAP/$(gs_key "$own")"; }
  return 0
}
gs_status() {
  local n d
  for ((n = 1; n <= GS_SLOTS; n++)); do
    d=$(gs_lock $n)
    if [ -d "$d" ]; then
      printf 'slot %d %s: %s, %d s; run %s; game pid %s%s\n' $n "$d" "$(head -1 "$d/owner" 2>/dev/null)" "$(gs_age "$d")" \
        "$(cat "$d/run" 2>/dev/null || echo -)" "$(cat "$d/pid" 2>/dev/null || echo -)" \
        "$(gs_alive "$d" && echo ' (running)')"
    else printf 'slot %d %s: free\n' $n "$d"; fi
  done
  [ -d "$GS_SOLO" ] && echo "solo request: $(head -1 "$GS_SOLO/owner" 2>/dev/null) ($(gs_age "$GS_SOLO") s)"
  w=$(gs_gate) && echo "gate: open" || echo "gate: closed - $w"
}
gs_prefix() {   # N: logs/winport/prefix-slot<N>, a reflink copy of logs/winport/prefix (once; only while the base
                # prefix has no wineserver: its registry files are whole)
  local n=${1:?slot} base=$GS_SOM/logs/winport/prefix dst
  [ "$n" -ge 2 ] || { echo "slot 1 uses $base itself"; return 0; }
  dst=$GS_SOM/logs/winport/prefix-slot$n
  [ -d "$dst/pfx/drive_c/windows/system32" ] && { echo "$dst exists"; return 0; }
  [ -d "$base/pfx/drive_c/windows/system32" ] || { echo "no base prefix $base (one slot-1 Windows boot makes it)"; return 1; }
  local p
  for p in $(pgrep -x wineserver); do
    gs_env_has "$p" "WINEPREFIX=$base/pfx" && { echo "a wineserver of $base runs (pid $p): try again between its boots"; return 1; }
  done
  rmdir "$dst" 2>/dev/null; [ -e "$dst" ] && { echo "$dst exists but is no prefix: look at it"; return 1; }
  rm -rf "${dst:?}.tmp"; cp -a --reflink=always "$base" "$dst.tmp" && mv "$dst.tmp" "$dst" || { rm -rf "${dst:?}.tmp"; echo "the copy failed"; return 1; }
  echo "made $dst ($(du -sh "$dst" | cut -f1), a reflink copy of $base)"
}
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  cmd=${1:-status}; [ $# -gt 0 ] && shift
  case $cmd in
    take) gs_take "$@" ;;
    release) gs_release "$@" ;;
    status) gs_status ;;
    lock) gs_lock "${1:?N}" ;;
    rtdir) gs_rtdir "${1:?N}" ;;
    count) echo "$GS_SLOTS" ;;
    gate) gs_gate ;;
    prefix) gs_prefix "$@" ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
  esac
fi
