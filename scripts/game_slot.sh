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
# ---- b37 (som-b37-slotsvram, docs/re-xr-pipeline.md 158.5): the render mode per boot and the VRAM gate ------------------
# WIN_RENDER=vr|light (per boot; headless_win.sh applies it): vr = the user's VR preset (som-win-session.sh's detection +
# SOMVR_RENDER_SET) - every boot that judges pictures, grass, foliage, fog, weather, GPU or frame time; light = the same
# with GS_RENDER_LIGHT over it (the VRAM-heavy settings lowered: a game at ~2.5 GB in the world instead of ~6).
# Default: slot 1 vr, slots 2.. light. take's VRAM gate: the card's free VRAM, less what the other held slots' games
# will still load (their mode's need less what their game already holds) and less GTT far above its idle level (a
# spill: memory the games wanted in VRAM), must hold this boot's need + a margin; a game alone always runs.
GS_RENDER_LIGHT=${WIN_RENDER_LIGHT:-TextureQuality:1}   # (158.5: the measured light set)
GS_NEED_VR=${GAME_SLOT_NEED_VR:-6656}; GS_NEED_LIGHT=${GAME_SLOT_NEED_LIGHT:-2816}  # MiB a slot adds in the world (158.5: game 6.2 / 2.3 + 0.34)
GS_SLOT_EXTRA=384                      # MiB of that outside the game's process (its Monado, gamescope's Xwayland)
GS_MARGIN=${GAME_SLOT_VRAM_MARGIN:-1024}
GS_GTT_IDLE=${GAME_SLOT_GTT_IDLE:-1792}; GS_GTT_GAME=512; GS_GTT_SPILL=4096   # MiB: the desktop's GTT, a game's, "far above"
GS_FD=""
gs_card() {   # the GPU's sysfs folder (mem_info_*); the self-test only its GAME_SLOT_CARD (none: no VRAM gate)
  local c
  [ -n "${GAME_SLOT_CARD:-}" ] && { echo "$GAME_SLOT_CARD"; return 0; }
  [ -n "${GAME_SLOT_TMP:-}" ] && return 1
  for c in /sys/class/drm/card*/device; do [ -r "$c/mem_info_vram_total" ] && { echo "$c"; return 0; }; done; return 1
}
gs_mib() { echo $(( $(cat "$1" 2>/dev/null || echo 0) / 1048576 )); }
gs_render_default() { [ "$1" = 1 ] && echo vr || echo light; }
gs_render_of() {   # N: the mode slot N runs or was taken for (its lock's render file; else the slot's default)
  local m; m=$(cat "$(gs_lock "$1")/render" 2>/dev/null)
  case $m in vr|light) echo "$m" ;; *) gs_render_default "$1" ;; esac
}
gs_need() { [ "$1" = vr ] && echo "$GS_NEED_VR" || echo "$GS_NEED_LIGHT"; }
gs_game_pid() {   # N: slot N's game (the ShadowOfMordor.exe with its run's SOMVR_LOG; slot 1 with no run: a Linux game)
  local r p; r=$(cat "$(gs_lock "$1")/run" 2>/dev/null)
  if [ -n "$r" ]; then
    for p in $(pgrep -f '^[A-Za-z]:\\.*\\ShadowOfMordor\.exe'); do   # (the exe itself: bwrap/timeout above it share its env)
      gs_env_has "$p" "SOMVR_LOG=$r/somvr.log" && { echo "$p"; return 0; }
    done
  elif [ "$1" = 1 ]; then p=$(pgrep -x -o ShadowOfMordor) && { echo "$p"; return 0; }; fi
  return 1
}
gs_pid_mem() {   # PID: "resident_vram total_vram resident_gtt total_gtt" (MiB) of its DRM clients, each client once
  awk 'function kib(v, u) { return u == "MiB" ? v * 1024 : (u == "KiB" ? v : v / 1024) }
       function put() { if (id != "" && !(id in s)) { s[id] = 1; RV += rv; TV += tv; RG += rg; TG += tg }; id = ""; rv = tv = rg = tg = 0 }
       FNR == 1 { put() }
       /^drm-client-id:/ { id = $2 }        /^drm-total-vram:/ { tv = kib($2, $3) } /^drm-resident-vram:/ { rv = kib($2, $3) }
       /^drm-total-gtt:/ { tg = kib($2, $3) } /^drm-resident-gtt:/ { rg = kib($2, $3) }
       END { put(); printf "%d %d %d %d\n", RV / 1024, TV / 1024, RG / 1024, TG / 1024 }' /proc/"$1"/fdinfo/* 2>/dev/null
}
gs_slot_mem() {   # N: slot N's game's "resident_vram total_vram resident_gtt total_gtt" MiB; nothing = no game (yet)
  local p
  [ -n "${GAME_SLOT_TMP:-}" ] && { cat "$(gs_lock "$1")/test_mem" 2>/dev/null; return 0; }   # (the self-test)
  p=$(gs_game_pid "$1") && gs_pid_mem "$p"
}
gs_vram_why() {   # N MODE: exit 0 = slot N's boot at MODE does not fit now (the reason on stdout); 1 = it fits
  local n=$1 mode=$2 c tot used gtt k m need g cur r res=0 rs="" others=0 gn=0 spill free want
  c=$(gs_card) || return 1                  # (no card information: no gate)
  tot=$(gs_mib "$c/mem_info_vram_total"); used=$(gs_mib "$c/mem_info_vram_used"); gtt=$(gs_mib "$c/mem_info_gtt_used")
  for ((k = 1; k <= GS_SLOTS; k++)); do
    [ "$k" = "$n" ] && continue; [ -d "$(gs_lock $k)" ] || continue
    others=$((others + 1)); gn=$((gn + GS_GTT_GAME))
    m=$(gs_render_of $k); need=$(gs_need "$m"); g=$(gs_slot_mem $k)
    if [ -n "$g" ]; then cur=${g%% *}; r=$((need - cur - GS_SLOT_EXTRA)); else r=$need; fi   # (no game yet: all of it)
    [ $r -lt 0 ] && r=0; res=$((res + r)); rs+="${rs:+, }$k $m$([ -n "$g" ] && echo " ${cur} in" || echo ' starting') +$r"
  done
  [ $others = 0 ] && { echo "VRAM: alone (no other slot held: it always runs) - $used of $tot MiB used, GTT $gtt"; return 1; }
  spill=$((gtt - GS_GTT_IDLE - gn)); [ $spill -lt 0 ] && spill=0
  free=$((tot - used - res - spill)); want=$(( $(gs_need "$mode") + GS_MARGIN ))
  if [ $spill -gt $GS_GTT_SPILL ]; then
    echo "VRAM: a spill in progress - GTT $gtt MiB, $spill above its idle $GS_GTT_IDLE + the held slots' $gn (slots: $rs)"; return 0
  fi
  r="free $free = $tot - $used used - $res for the held slots ($rs) - $spill spilled (GTT $gtt)"
  [ $free -ge $want ] && { echo "VRAM: slot $n at $mode fits ($(gs_need "$mode") + $GS_MARGIN MiB): $r"; return 1; }
  echo "VRAM: slot $n at $mode needs $(gs_need "$mode") + $GS_MARGIN MiB; $r"; return 0
}
gs_take_lock() {   # serialise take's VRAM look and mkdir between takers (this tool's own; old copies only mkdir)
  exec {GS_FD}>> "$GS_TMP/somvr-slots.take"; flock -w 30 "$GS_FD" || true
}
gs_take_unlock() { [ -n "$GS_FD" ] && { flock -u "$GS_FD"; exec {GS_FD}>&-; GS_FD=""; }; return 0; }
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
gs_take1() {   # N OWNER [MODE]: 0 = taken (with MODE: its VRAM gate passed, MODE in the lock's render file); 1 = busy;
               # 3 = a solo request of another owner; 4 = MODE does not fit in VRAM now (GS_WHY: the VRAM line either way)
  local d rc=0; d=$(gs_lock "$1"); GS_WHY=""
  gs_user_stale || true
  [ -d "$GS_SOLO" ] && ! gs_names "$GS_SOLO" "$2" && return 3
  if gs_stale "$1"; then
    echo "game_slot: slot $1's lock is stale ($(head -1 "$d/owner" 2>/dev/null), $(gs_age "$d") s, no game): removed" >&2
    rm -rf "$d"
  fi
  [ -d "$d" ] && return 1
  [ -n "${3:-}" ] && gs_take_lock
  if [ -n "${3:-}" ] && GS_WHY=$(gs_vram_why "$1" "$3"); then rc=4
  elif mkdir "$d" 2>/dev/null; then
    [ -n "${3:-}" ] && echo "$3" > "$d/render"
    echo "$2 $(date +%T)" > "$d/owner"
  else rc=1; fi
  gs_take_unlock; return $rc
}
gs_take() {   # OWNER [N|auto|solo] [WAIT_S]
  local own=${1:?owner} want=${2:-auto} wait=${3:-0} end n w got=() ok
  [[ $wait =~ ^[0-9]+$ ]] || { echo "WAIT_S must be whole seconds" >&2; return 1; }
  case $want in auto|solo) ;; [1-9]) [ "$want" -le "$GS_SLOTS" ] || { echo "slot $want: there are $GS_SLOTS" >&2; return 1; } ;;
    *) echo "slot: N, auto or solo" >&2; return 1 ;; esac
  case ${WIN_RENDER:-} in ''|vr|light) ;; *) echo "WIN_RENDER=$WIN_RENDER: vr or light" >&2; return 1 ;; esac
  # b37: the order - a vr boot tries slot 1 first (then the others, where the VRAM allows); light / unset the extra
  # slots first; each slot's mode = WIN_RENDER, else its default (1 vr, 2.. light)
  local order lastw="" lastt=0 vw
  if [ "${WIN_RENDER:-}" = vr ]; then order="1 $(seq -s ' ' 2 "$GS_SLOTS")"; else order="$(seq -s ' ' 2 "$GS_SLOTS") 1"; fi
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
          vw=""
          for n in $order; do
            [ "$n" -le "$GS_SLOTS" ] || continue
            gs_take1 "$n" "$own" "${WIN_RENDER:-$(gs_render_default "$n")}"; case $? in
              0) [ -n "$GS_WHY" ] && echo "game_slot: slot $n for $own - $GS_WHY" >&2; echo "$n"; return 0 ;;
              4) [ -n "$vw" ] || vw=$GS_WHY ;; esac
          done
          w="every slot is busy"; [ -n "$vw" ] && w=$vw
          [ -d "$GS_SOLO" ] && w="a solo request: $(head -1 "$GS_SOLO/owner" 2>/dev/null)" ;;
        *)
          gs_take1 "$want" "$own" "${WIN_RENDER:-$(gs_render_default "$want")}"; case $? in
            0) [ -n "$GS_WHY" ] && echo "game_slot: slot $want for $own - $GS_WHY" >&2; echo "$want"; return 0 ;;
            3) w="a solo request: $(head -1 "$GS_SOLO/owner" 2>/dev/null)" ;;
            4) w=$GS_WHY ;;
            *) w="slot $want is busy ($(head -1 "$(gs_lock "$want")/owner" 2>/dev/null))" ;; esac ;;
      esac
    fi
    [ "$(date +%s)" -ge "$end" ] && break
    if [ "${w%%:*}" != "${lastw%%:*}" ] || [ $(( $(date +%s) - lastt )) -ge 60 ]; then   # b37: the wait, said
      echo "game_slot $(date +%T): $own waits - $w" >&2; lastw=$w; lastt=$(date +%s)
    fi
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
  gs_vram_status   # b37
}
gs_vram_status() {   # the card's VRAM and GTT, each held slot's mode and its game's memory, what a new light/vr boot would get
  local c n g m w v
  c=$(gs_card) || { echo "vram: no card information"; return 0; }
  echo "vram: $(gs_mib "$c/mem_info_vram_used") of $(gs_mib "$c/mem_info_vram_total") MiB used, GTT $(gs_mib "$c/mem_info_gtt_used") MiB"
  for ((n = 1; n <= GS_SLOTS; n++)); do
    [ -d "$(gs_lock $n)" ] || continue; m=$(gs_render_of $n); g=$(gs_slot_mem $n)
    if [ -n "$g" ]; then set -- $g; g="VRAM $1 MiB resident ($2 asked), GTT $3 MiB resident ($4 asked)"; else g="not running (yet)"; fi
    echo "  slot $n: $m (needs $(gs_need "$m")); its game: $g"
  done
  for m in light vr; do
    for ((n = 1; n <= GS_SLOTS; n++)); do [ -d "$(gs_lock $n)" ] || break; done
    if [ $n -le $GS_SLOTS ]; then w=$(gs_vram_why $n $m) && v=NO || v=yes; echo "  a $m boot in slot $n now: $v - ${w#VRAM: }"
    else echo "  a $m boot: no free slot"; fi
  done
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
    vram) gs_vram_status ;;
    mem) p=$(gs_game_pid "${1:?N}") && echo "$p $(gs_pid_mem "$p")" ;;   # b37: slot N's game: pid rv tv rg tg (MiB)
    prefix) gs_prefix "$@" ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
  esac
fi
