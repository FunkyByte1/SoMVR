#!/bin/bash
# som-vr-headset-win.sh - the user's own headset session on the WINDOWS (GOG) build, one click: WiVRn's in-headset
# app list runs it through ~/.local/share/applications/"Shadow of Mordor VR (Windows).desktop" (Categories X-WiVRn-VR;
# a copy: scripts/som-vr-headset-win.desktop). The Windows twin of som-vr-headset.sh (docs/re-winport.md 7.10): the
# GOG game under Proton's own wine, never through Steam (no Steam client needed, no Steam hours), our DLL in our own
# prefix, the Linux Vulkan layer talking OpenXR to WiVRn.
# The session (scripts/som-win-session.sh, in the systemd user unit somvr-headset - the same unit as the Linux sessions:
# one game at a time, and every helper's "the user plays" guard sees it): the headless gamescope at the headset's 90 Hz,
# the start sequence (scripts/cmds/somvr-vr6.cmd: the main menu's Continue and the story), the bug marks with voice, the
# VR menu's Quit game, the game's end watched, the 4-hour limit.
# The frozen build: WIN_BUILD (below) = logs/$WIN_BUILD/ - ShadowOfMordor/mod/somvr/build/win/xinput9_1_0.dll (the game
# side), ShadowOfMordor/mod/somvr/vklayer (the layer), headset.env (the switches, the Linux launcher's rules: one
# KEY=VALUE per line, SOMVR_* keys only, plain values, read and checked, never sourced), proton/ (a frozen copy of
# Proton's files; without it Steam's Proton - Experimental). Made by scripts/som-win-freeze.sh NAME; changed only
# between sessions.
# The save: the session plays on our prefix's own save (logs/win-play/prefix: Documents\WB Games\Shadow of Mordor\Saves\
# GameData.sav), so the progress made in the Windows build stays. The first session seeds it with a COPY of the user's
# Linux/Steam save (the file is only read); a backup before each session (backups/win-save/<ts>-before/), its sha after.
# The Steam save and the Heroic prefix are never touched (the Steam save's sha is checked before and after).
# Sound: the game's sound goes to the PC's default output, which the vr-session watcher makes WiVRn's sink while the
# user is in VR (as for the Linux sessions); the session checks it 30 s in and moves the game's stream to WiVRn's sink
# if it is elsewhere.
# The PC window (scripts/somvr_window.py): the session's state and time, a small live picture, Stop (= systemctl --user
# stop somvr-headset), the bug marks; it closes 10 s after the session ended.
# Before: WiVRn's server must run (else a notification); a headset not connected yet gets 5 minutes. The player comes
# first: a developer's SoM test (boot_check.sh without --headset, or headless_win.sh with the game) is stopped; another
# game's fresh slot marker (a HoF2 test) gets 2 minutes to go. A start while a session (Linux or Windows) is ending waits
# for its end; while one plays or starts it refuses. Two starts at once: the second one refuses.
# Ends: the game quits, the VR menu's Quit game, the PC window's Stop, the headset disconnected for 5 minutes, 4 hours.
# Everything goes to logs/som-vr-headset-win.log; the session's own folder logs/win-session-<ts>/ (named there).
# SOMVR_HEADSET_DRYRUN=1: the same checks (WiVRn's only noted), the switches file, the PC window's modules, then the
#   session's --dry-run in the foreground (the paths, the prefix, the save seeding, the game's environment; starts
#   nothing). SOMVR_HEADSET_ENVFILE=FILE (dry runs and tests only): read FILE instead of the build's headset.env.
# SOMVR_HEADSET_TEST=1 (a developer's end-to-end test, with the game slot /tmp/somvr-boot.lock held): the launcher's
#   own command line with Monado's null compositor standing in for WiVRn and a null sink of the session's own for the
#   sound, on a test prefix (SOMVR_HEADSET_TEST_PREFIX, default logs/win-launch/test-prefix; never the play prefix),
#   SOMVR_HEADSET_TEST_HARD seconds (default 600); no notifications; the PC window on a headless screen of its own
#   (its pictures in the session folder's window-shots/).
# Tests only (som-b37-practrip, 11-04; never read in the user's session): SOMVR_HEADSET_TEST_SAVE=pin|far|steam|/FILE -
#   the save every test starts from (each test's prefix save replaced by a copy of it at the start: tests repeatable):
#   pin (default) = logs/test-save/saves/GameData.sav (the pinned copy, as headless_vr.sh's; Talion ~38 m from the
#   practice spot), far = logs/test-save/far-461m/GameData.sav (461 m from it: the trip's ground wait, the standing check
#   scripts/launch-far-check.sh), steam = the old way (a fresh prefix seeded from a copy of the live Steam save, which
#   moves after the user's Deck play), /FILE = that file (only read). SOMVR_HEADSET_TEST_BUILD=NAME (dry runs and tests):
#   logs/NAME instead of WIN_BUILD (an older or a candidate frozen build through the user's own launcher path).
set -u
unset LD_PRELOAD LD_LIBRARY_PATH
export PATH=/usr/local/bin:/usr/bin:/bin

SOM=$HOME/Projects/flat2vr/ShadowOfMordor
SCR=$(cd "$(dirname "$0")" && pwd)       # the scripts it copies: the ones next to it (the main checkout's for the user)
WIN_BUILD=build-win5                      # the Windows build the user plays (logs/WIN_BUILD); changed only between sessions
BLD=$SOM/logs/$WIN_BUILD
ENVF=$BLD/headset.env
CMDS=$SCR/cmds/somvr-vr6.cmd              # the start sequence: the main menu's Continue, the story (the Windows build's
                                          # SESSION ui lines are the Linux build's)
UNIT=somvr-headset
HARD=14400                                # the session's limit, seconds (4 h, as the Linux sessions)
WAIT_HEADSET=300; GONE=300; WAIT_SLOT=120
RT=/run/user/$(id -u)
VRS=$RT/vr-session
PRIV=$RT/somvr-headset                    # (shared with som-vr-headset.sh: its ending/next-start files, one unit)
LOGF=$SOM/logs/som-vr-headset-win.log
DRY=${SOMVR_HEADSET_DRYRUN:-0}
TEST=${SOMVR_HEADSET_TEST:-0}
TEST_PREFIX=${SOMVR_HEADSET_TEST_PREFIX:-$SOM/logs/win-launch/test-prefix}
[ "$TEST" = 1 ] && HARD=${SOMVR_HEADSET_TEST_HARD:-600}
if { [ "$DRY" = 1 ] || [ "$TEST" = 1 ]; } && [ -n "${SOMVR_HEADSET_TEST_BUILD:-}" ]; then   # (b37: tests only)
  [[ $SOMVR_HEADSET_TEST_BUILD =~ ^[A-Za-z0-9._-]+$ ]] || { echo "STOP: SOMVR_HEADSET_TEST_BUILD=$SOMVR_HEADSET_TEST_BUILD" >&2; exit 1; }
  WIN_BUILD=$SOMVR_HEADSET_TEST_BUILD; BLD=$SOM/logs/$WIN_BUILD; ENVF=$BLD/headset.env
fi
TSAVE=""
if [ "$TEST" = 1 ]; then   # (b37: the save each test starts from; the play prefix never gets one - tests use TEST_PREFIX)
  case ${SOMVR_HEADSET_TEST_SAVE:-pin} in
    pin) TSAVE=$SOM/logs/test-save/saves/GameData.sav ;;
    far) TSAVE=$SOM/logs/test-save/far-461m/GameData.sav ;;
    steam) TSAVE="" ;;
    /*) TSAVE=$SOMVR_HEADSET_TEST_SAVE ;;
    *) echo "STOP: SOMVR_HEADSET_TEST_SAVE=$SOMVR_HEADSET_TEST_SAVE (pin|far|steam|/FILE)" >&2; exit 1 ;;
  esac
  [ -z "$TSAVE" ] || [ -f "$TSAVE" ] || { echo "STOP: no test save $TSAVE" >&2; exit 1; }
fi
{ [ "$DRY" = 1 ] || [ "$TEST" = 1 ]; } && [ -n "${SOMVR_HEADSET_ENVFILE:-}" ] && ENVF=$SOMVR_HEADSET_ENVFILE
APP="Shadow of Mordor VR (Windows)"
# the launcher's own keys (the file may not set them) and the session's
FIXED=" VK_LAYER_PATH VK_INSTANCE_LAYERS XR_RUNTIME_JSON SOMVR_XR_LOG SOMVR_SETTINGS SOMVR_TUNE SOMVR_SAVEDIR SOMVR_LOG SOMVR_TRIGGER SOMVR_BRIDGE SOMVR_XR_TEST SOMVR_WIN_TEST SOMVR_WIN_SITES "
load_switches() {   # $1 = headset.env -> SW (KEY=VALUE words); 1 + ENV_BAD = refused. Read and checked, never sourced.
  local n=0 line k v re_kv='^[[:space:]]*([A-Z][A-Z0-9_]*)=([^[:space:]#]*)[[:space:]]*(#.*)?$' re_skip='^[[:space:]]*(#.*)?$'
  local re_val='^[A-Za-z0-9_.,:/@+|;-]*$'
  SW=(); ENV_BAD=""
  [ -r "$1" ] || { ENV_BAD="cannot read it"; return 1; }
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1)); line=${line%$'\r'}
    [ $n -le 2000 ] || { ENV_BAD="more than 2000 lines"; return 1; }
    [[ $line =~ $re_skip ]] && continue
    [[ $line =~ $re_kv ]] || { ENV_BAD="line $n is not KEY=VALUE: $(printf '%s' "$line" | cut -c1-60)"; return 1; }
    k=${BASH_REMATCH[1]}; v=${BASH_REMATCH[2]}
    [[ $k =~ ^SOMVR_[A-Z0-9_]+$ || $k == XR_LOADER_DEBUG || $k == VK_LOADER_DEBUG ]] ||
      { ENV_BAD="line $n: $k is not a SOMVR_ key"; return 1; }
    [[ $FIXED == *" $k "* ]] && { ENV_BAD="line $n: $k is the launcher's own"; return 1; }
    [ ${#k} -le 64 ] && [[ $v =~ $re_val ]] && [ ${#v} -le 256 ] ||
      { ENV_BAD="line $n: $k's value is not a plain switch value"; return 1; }
    SW+=("$k=$v")
  done < "$1"
  [ ${#SW[@]} -gt 0 ] || { ENV_BAD="no switches in it"; return 1; }
  return 0
}

note() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$LOGF" 2>/dev/null; printf 'som-vr-headset-win: %s\n' "$*" >&2; }
tell() {   # TITLE BODY: a desktop notification and the log
  note "$1: $2"
  [ "$DRY" = 1 ] || [ "$TEST" = 1 ] || notify-send -a "$APP" -i steam_icon_241930 "$1" "$2" 2>/dev/null || true
}
fail() { tell "$APP did not start" "$1"; exit 1; }
unit_state() { systemctl --user is-active "$UNIT" 2>/dev/null; }
unit_running() { case $(unit_state) in active|activating|deactivating|reloading) return 0 ;; esac; return 1; }
headset() {   # 0 = the Quest is connected to WiVRn, 1 = not, 2 = unknown (WiVRn's D-Bus property, then its TCP link)
  local v
  v=$(timeout 5 busctl --user get-property io.github.wivrn.Server /io/github/wivrn/Server io.github.wivrn.Server \
    HeadsetConnected 2>/dev/null)
  case $v in "b true") return 0 ;; "b false") return 1 ;; esac
  ss -tnH state established '( sport = :9757 )' 2>/dev/null | grep -q . && return 0
  return 2
}
bc_main() {   # the main boot_check.sh process(es) (only those whose parent is not one): "PID headset|test"
  ps -eo pid=,ppid=,args= | awk '$3 ~ /bash$/ && $4 ~ /boot_check\.sh$/ { p[$1] = $2; a[$1] = $0 }
    END { for (i in p) if (!(p[i] in p)) print i " " (a[i] ~ / --headset( |$)/ ? "headset" : "test") }'
}
hw_main() {   # a developer's Windows game test: the main headless_win.sh process(es) with --game
  ps -eo pid=,ppid=,args= | awk '$3 ~ /bash$/ && $4 ~ /headless_win\.sh$/ && / --game / { p[$1] = $2 }
    END { for (i in p) if (!(p[i] in p)) print i }'
}
win_game() {   # the Windows game's processes: a command line that is the exe's Windows path (Wine's; the game names its
               # main thread PrimaryEngine, so not by name)
  pgrep -f '^[A-Za-z]:\\.*\\ShadowOfMordor\.exe' | tr '\n' ' '
}
slot_busy() {   # another environment's fresh reserved-env* / game-env*
  local f age
  for f in "$VRS"/reserved-env* "$VRS"/game-env*; do
    [ -e "$f" ] || continue
    [ "${f##*/}" = reserved-envsom ] && continue
    age=$(( $(date +%s) - $(stat -c %Y "$f" 2>/dev/null || echo 0) ))
    [ "$age" -le 120 ] && { echo "${f##*/}"; return 0; }
  done
  return 1
}

mkdir -p "$SOM/logs"
note "start (build $WIN_BUILD$([ "$DRY" = 1 ] && echo ', DRY RUN')$([ "$TEST" = 1 ] && echo ", TEST: Monado for WiVRn, a null sink, prefix $TEST_PREFIX, $HARD s"); parent: $(ps -o args= -p $PPID 2>/dev/null | cut -c1-120))"
[ -f "$BLD/ShadowOfMordor/mod/somvr/build/win/xinput9_1_0.dll" ] && [ -f "$BLD/ShadowOfMordor/mod/somvr/vklayer/build/libVkLayer_somvr_openxr.so" ] ||
  fail "The Windows VR build $WIN_BUILD is missing. Tell Claude."
load_switches "$ENVF" || fail "The build's switches file is not valid ($ENVF: $ENV_BAD). Tell Claude."
note "switches: ${#SW[@]} from $ENVF; the start sequence $CMDS"
render_by_hz() {   # som-b37-looks (docs/re-winport.md 7.15): SW + the settings file $1 (only read) -> RSV, RSV_NOTE
  # SOMVR_RENDER_SET=NAME:V,... is the session's VR graphics preset. SOMVR_RENDER_BY_HZ=on: the session's refresh rate =
  # the VR menu's saved XR_REFRESH (the file), else the switch SOMVR_XR_REFRESH (60..144); at 90 Hz or less
  # SOMVR_RENDER_SET_90 over the preset, over 90 Hz SOMVR_RENDER_SET_120 (NAME:V,...: a name in both takes the
  # overlay's value; empty or off: none). Off (the code default), or the rate unknown: SOMVR_RENDER_SET alone. The
  # preset is written at the start: the menu's rate changed in a session counts from the next one.
  local kv p on=off base="" lo="" hi="" sw="" hz="" src="" ov="" key names="," out=""
  for kv in "${SW[@]}"; do
    case $kv in
      SOMVR_RENDER_BY_HZ=*) on=${kv#*=} ;; SOMVR_RENDER_SET=*) base=${kv#*=} ;; SOMVR_XR_REFRESH=*) sw=${kv#*=} ;;
      SOMVR_RENDER_SET_90=*) lo=${kv#*=} ;; SOMVR_RENDER_SET_120=*) hi=${kv#*=} ;;
    esac
  done
  RSV=$base; RSV_NOTE=""
  [ "$on" = on ] || return 0
  hz=$(sed -n 's/^XR_REFRESH=\([0-9][0-9]*\)[[:space:]]*$/\1/p' "$1" 2>/dev/null | tail -1); src="the VR menu's saved rate"
  [[ $hz =~ ^[0-9]{2,3}$ ]] && [ "$hz" -ge 60 ] && [ "$hz" -le 144 ] || { hz=$sw; src="SOMVR_XR_REFRESH"; }
  [[ $hz =~ ^[0-9]{2,3}$ ]] && [ "$hz" -ge 60 ] && [ "$hz" -le 144 ] ||
    { RSV_NOTE="SOMVR_RENDER_BY_HZ: the refresh rate is not known (the runtime's own): SOMVR_RENDER_SET alone"; return 0; }
  if [ "$hz" -le 90 ]; then ov=$lo key=SOMVR_RENDER_SET_90; else ov=$hi key=SOMVR_RENDER_SET_120; fi
  if [ -z "$ov" ] || [ "$ov" = off ]; then RSV_NOTE="SOMVR_RENDER_BY_HZ: $hz Hz ($src): $key not set, SOMVR_RENDER_SET alone"; return 0; fi
  for p in ${ov//,/ }; do names+="${p%%:*},"; done
  for p in ${base//,/ }; do [[ $names == *",${p%%:*},"* ]] || out+="${out:+,}$p"; done
  for p in ${ov//,/ }; do out+="${out:+,}$p"; done
  RSV=$out; RSV_NOTE="SOMVR_RENDER_BY_HZ: $hz Hz ($src): $key ($ov) over SOMVR_RENDER_SET"
}
RSET=()   # the session's VR graphics preset (render_by_hz) in the prefix's render.cfg; a bad entry left out
render_by_hz "$HOME/.local/share/somvr/settings.conf"
[ -n "$RSV_NOTE" ] && note "$RSV_NOTE"
if [ -n "$RSV" ]; then
  for p in ${RSV//,/ }; do
    if [[ $p =~ ^(VegetationQuality|ShadowQuality|MeshQuality|LightingQuality|AmbientOcclusionQuality|TessellationToggle|TextureQuality|TextureFiltering|OITToggle|FrameRateLimit):[0-5]$ ]]; then
      RSET+=(--render-set "${p/:/=}")
    else note "SOMVR_RENDER_SET: '$p' is not NAME:0..5 (left out)"; fi
  done
  note "the VR graphics preset: $RSV"
fi
[ -f "$CMDS" ] && [ -f "$SCR/som-win-session.sh" ] || fail "A start file is missing ($CMDS or som-win-session.sh). Tell Claude."

session_ending() { [ "$(unit_state)" = deactivating ] || [ -s "$PRIV/ending" ]; }
start_gate() {   # a session running: refuse (playing or starting) or wait for its end (ending)
  local i
  unit_running || return 0
  if ! session_ending; then tell "Shadow of Mordor VR is already running" "It is starting or running in the headset."; exit 0; fi
  if [ "$DRY" = 1 ]; then note "dry run: the last session is closing: a real start would wait for it, then start"; exit 0; fi
  tell "$APP: starting in a moment" "The last session is closing - the new one starts by itself in a moment."
  mkdir -p "$PRIV" && date +%s > "$PRIV/next-start" 2>/dev/null
  for i in $(seq 1 "${WAIT_END:-300}"); do unit_running || break; sleep 1; done
  unit_running && fail "The last session did not close within 5 minutes. Tell Claude."
  note "the last session closed after $i s: starting"
}
start_gate
# build 35 (the 21:05 race: helpers took game slots in the seconds between this launcher stopping their test runs and
# the session's own gate closing): first the slots' solo request as user-session with this launcher's pid
# (scripts/game_slot.sh: no slot is taken, no test game starts and a running one stops - gs_vr_why, boot_check's
# user_in_vr), then every slot's test run stopped and every game waited out, then the session; the request is released
# when this launcher ends (the session's end, a failed start, a signal). A suite's request is taken over (noted).
SOLO=/tmp/somvr-slots-solo.lock; SOLO_HELD=0
solo_now() { [ -d "$SOLO" ] && echo "$(head -1 "$SOLO/owner" 2>/dev/null), pid $(cat "$SOLO/pid" 2>/dev/null || echo -)" || echo free; }
solo_take() {
  local prev="" p
  if ! mkdir "$SOLO" 2>/dev/null; then
    prev=$(head -1 "$SOLO/owner" 2>/dev/null); p=$(cat "$SOLO/pid" 2>/dev/null)
    case $prev in user-session|"user-session "*)
      [ -n "$p" ] && kill -0 "$p" 2>/dev/null && { note "the slots' solo request is another user launcher's (pid $p): left to it"; return 0; } ;;
    esac
  fi
  if echo "user-session $(date +%T) (the user's Windows VR session)" > "$SOLO/owner" && echo $$ > "$SOLO/pid"; then
    SOLO_HELD=1; note "the slots' solo request taken as user-session$([ -n "$prev" ] && echo " (it was: $prev)"): no test game starts now"
  else note "!!! the slots' solo request could not be taken ($SOLO): going on without it"; fi
}
solo_release() {
  [ "$SOLO_HELD" = 1 ] || return 0
  SOLO_HELD=0
  case $(head -1 "$SOLO/owner" 2>/dev/null) in "user-session "*)
    [ "$(cat "$SOLO/pid" 2>/dev/null)" = "$$" ] && rm -rf "$SOLO" && note "the slots' solo request released" ;;
  esac
}
games() { { pgrep -x ShadowOfMordor; win_game; } 2>/dev/null | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'; }
# (SOMVR_HEADSET_TEST_STOPALL=1: a test of this path - the solo request and the stop of every slot's test run in a test)
STOPALL=0; [ "$TEST" != 1 ] || [ "${SOMVR_HEADSET_TEST_STOPALL:-0}" = 1 ] && STOPALL=1
[ "$TEST" = 1 ] && [ $STOPALL = 1 ] && note "test: the start's solo request and stop of every slot's test run too (SOMVR_HEADSET_TEST_STOPALL)"
if [ $STOPALL = 1 ]; then
  if [ "$DRY" = 1 ]; then
    note "dry run: 1. the slots' solo request would be taken as user-session (now: $(solo_now))"
  else
    trap solo_release EXIT
    trap 'exit 143' TERM INT HUP   # (the EXIT trap releases; the session's own handler replaces this at its start)
    solo_take
  fi
fi
if [ $STOPALL = 1 ]; then   # the player first: a developer's test of the game stops (a test run holds the slot itself)
  [ "$DRY" = 1 ] && note "dry run: 2. every slot's test run would be stopped: Linux $(bc_main | awk '{print $1}' | tr '\n' ' ')- Windows $(hw_main | tr '\n' ' ')- (left alone)"
  while read -r pid kind; do
    [ -n "${pid:-}" ] || continue
    [ "$kind" = headset ] && fail "Another VR session of the game is already running (pid $pid). Tell Claude."
    if [ "$DRY" = 1 ]; then note "dry run: a Linux test run (boot_check $pid) would be stopped first (left alone)"; continue; fi
    tell "Making room" "A test run of the game is stopping first (about a minute)."
    kill -TERM "$pid" 2>/dev/null
    for i in $(seq 1 90); do kill -0 "$pid" 2>/dev/null || break; sleep 2; done
    kill -0 "$pid" 2>/dev/null && fail "A test run of the game would not stop (pid $pid). Tell Claude."
    note "the Linux test run $pid ended"
  done < <(bc_main)
  for pid in $(hw_main); do
    if [ "$DRY" = 1 ]; then note "dry run: a Windows test run (headless_win $pid) would be stopped first (left alone)"; continue; fi
    tell "Making room" "A test run of the game is stopping first (about a minute)."
    kill -TERM "$pid" 2>/dev/null
    for i in $(seq 1 45); do kill -0 "$pid" 2>/dev/null || [ -n "$(win_game)" ] || break; sleep 2; done
    note "the Windows test run $pid: $(kill -0 "$pid" 2>/dev/null && echo 'still there' || echo ended)"
  done
  if [ "$DRY" = 1 ]; then   # 3. every game waited out (any slot's, Linux or Windows; up to 90 s)
    note "dry run: 3. then every game waited out, up to 90 s (now running: $(games || true))"
    note "dry run: 4. the session; the solo request released when this launcher ends"
  elif [ -n "$(games)" ]; then
    for i in $(seq 1 45); do [ -z "$(games)" ] && break; sleep 2; done
    note "the games after the test runs' stop: $([ -z "$(games)" ] && echo "all gone after $((i * 2)) s" || echo "still running: $(games)")"
  fi
fi
if pgrep -x ShadowOfMordor > /dev/null || [ -n "$(win_game)" ]; then
  [ "$DRY" = 1 ] && note "dry run: the game is running (a test's, or outside VR): a real start would refuse" ||
    fail "The game is already running (outside VR). Quit it, then press Play again."
fi

if [ "$TEST" = 1 ]; then
  note "test: Monado's null compositor stands in for WiVRn (no headset checks); the save: $([ -n "$TSAVE" ] && echo "$TSAVE" || echo 'its own, or a copy of the Steam save (SOMVR_HEADSET_TEST_SAVE=steam)')"
elif [ "$DRY" = 1 ]; then
  note "dry run: wivrn-server $(pgrep -x wivrn-server > /dev/null && echo running || echo 'not running'); headset check: $(headset; echo $?) (0 connected, 1 not, 2 unknown)"
else
  pgrep -x wivrn-server > /dev/null ||
    fail "WiVRn is not running. Start WiVRn on the PC, connect the Quest, then press Play again."
  headset; h=$?
  if [ $h = 1 ]; then
    tell "Connect the Quest" "Put the headset on and connect in WiVRn: the game starts when it connects (5 minutes)."
    for i in $(seq 1 $((WAIT_HEADSET / 5))); do sleep 5; headset; h=$?; [ $h = 1 ] || break; done
    [ $h = 1 ] && fail "The Quest did not connect to WiVRn within 5 minutes."
  fi
  note "headset: $([ $h = 0 ] && echo connected || echo 'unknown (WiVRn did not say; going on)')"
fi

if s=$(slot_busy); then
  [ "$DRY" = 1 ] && note "dry run: another game's test ($s) holds the slot: a real start would wait up to 2 minutes" || {
  tell "Waiting for another test" "A test of another game ($s) is ending first (up to 2 minutes)."
  for i in $(seq 1 $((WAIT_SLOT / 5))); do sleep 5; s=$(slot_busy) || break; done
  s=$(slot_busy) && fail "Another game's test ($s) still holds the game slot. Tell Claude."
  note "the slot is free"; }
fi

if [ "$DRY" != 1 ]; then   # one start at a time from here
  exec 9> "$RT/somvr-headset.lock"
  flock -w 30 9 || fail "Another start of the session did not finish within 30 s. Tell Claude."
  if unit_running; then tell "Shadow of Mordor VR is already running" "It is starting or running in the headset."; exit 0; fi
fi
# the session's own copies of the scripts it reads while it runs
mkdir -p "$PRIV" && cp -f "$SCR/som-win-session.sh" "$PRIV/som-win-session.sh" && cp -f "$CMDS" "$PRIV/start.cmd" ||
  fail "Could not copy the session scripts to $PRIV."
WIN=0; cp -f "$SCR/somvr_window.py" "$PRIV/somvr_window.py" 2>/dev/null && WIN=1
rm -f "$PRIV/voice_rec.py"; cp -f "$SCR/voice_rec.py" "$PRIV/voice_rec.py" 2>/dev/null ||
  note "no voice_rec.py beside the launcher: fixed-length voice notes"
rm -f "$PRIV/mark_compose.sh" "$PRIV/xr_compose.py"
cp -f "$SCR/mark_compose.sh" "$SCR/xr_compose.py" "$PRIV/" 2>/dev/null ||
  note "no mark_compose.sh / xr_compose.py beside the launcher: the marks' pictures without the layer (compose by hand)"
# b37 (docs/re-xr-pipeline.md 162): the live loop's scripts. som-win-session.sh starts live_session.sh from its own folder
# (this one), and live_daemon.py / live_lane.sh / lane_run.sh call their siblings by their own folder; copies like the
# rest, so a merge into main during the session changes nothing. Without them SOMVR_LIVE stays off (no live notes).
rm -rf "$PRIV/lane"; rm -f "$PRIV"/live.py "$PRIV"/live_*.py "$PRIV"/live_*.sh "$PRIV/lane_run.sh" "$PRIV/lane_watch.py"
cp -f "$SCR"/live.py "$SCR"/live_*.py "$SCR"/live_*.sh "$SCR/lane_run.sh" "$SCR/lane_watch.py" "$PRIV/" 2>/dev/null &&
  mkdir -p "$PRIV/lane" && cp -f "$SCR/lane/lane.conf" "$PRIV/lane/" 2>/dev/null ||
  note "the live loop's scripts are not beside the launcher: no live notes this session"

TS=$(date +%Y%m%d-%H%M%S)
if [ "$DRY" = 1 ]; then RUN=$SOM/logs/win-session-dryrun-$TS; else RUN=$SOM/logs/win-session-$TS; fi
mkdir -p "$RUN" || fail "Could not make the session folder $RUN."
GE=(--game-env "SOMVR_SETTINGS=$HOME/.local/share/somvr/settings.conf" --game-env SOMVR_TUNE=@RUN@/tune.txt)
for kv in "${SW[@]}"; do GE+=(--game-env "$kv"); done
ARGS=(--run "$RUN" --build "$BLD" --cmds "$PRIV/start.cmd" --hold $((HARD - 200)) --hard "$HARD" --refresh 90
  --ending-file "$PRIV/ending" "${RSET[@]}")
[ "$TEST" = 1 ] && ARGS+=(--test --runtime monado --sink null --prefix "$TEST_PREFIX")
[ -n "$TSAVE" ] && ARGS+=(--test-save "$TSAVE")
ARGS+=("${GE[@]}")
WPID=""; WIN_CLOSE=10; WGS=""; WDISP=""; WGWD=""
start_window() {   # the PC window on the desktop (a test: on a headless gamescope of its own)
  [ $WIN = 1 ] || { note "the PC window: no copy of somvr_window.py (not started)"; return 0; }
  local d=${DISPLAY:-} x=${XAUTHORITY:-} e
  if [ "$TEST" = 1 ]; then
    setsid env -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY XDG_RUNTIME_DIR="$RT" gamescope --backend headless -W 900 -H 700 -w 900 -h 700 -r 10 -- \
      sh -c 'env > "$1.tmp" && mv -f "$1.tmp" "$1" && exec tail -f /dev/null' w "$RUN/window-display.env" > "$RUN/window-gamescope.log" 2>&1 < /dev/null &
    WGS=$!
    for i in $(seq 1 50); do [ -s "$RUN/window-display.env" ] && break; sleep 0.2; done
    d=$(sed -n 's/^DISPLAY=//p' "$RUN/window-display.env" 2>/dev/null | head -1); x=""
    WGWD=$(sed -n 's/^GAMESCOPE_WAYLAND_DISPLAY=//p' "$RUN/window-display.env" 2>/dev/null | head -1)
  elif [ -z "$d" ]; then
    e=$(systemctl --user show-environment 2>/dev/null)
    d=$(printf '%s\n' "$e" | sed -n 's/^DISPLAY=//p' | head -1)
    [ -n "$x" ] || x=$(printf '%s\n' "$e" | sed -n 's/^XAUTHORITY=//p' | head -1)
  fi
  [ -n "$d" ] || { note "the PC window: no DISPLAY (not started)"; return 0; }
  WDISP=$d
  env DISPLAY="$d" ${x:+XAUTHORITY="$x"} setsid python3 -I "$PRIV/somvr_window.py" --unit "$UNIT" --since "$T" --run "$RUN" \
    --build "$WIN_BUILD (Windows)" --close-after "$WIN_CLOSE" --next-start "$PRIV/next-start" > "$PRIV/window.out" 2>&1 < /dev/null &
  WPID=$!
  note "the PC window started (pid $WPID, DISPLAY $d; its output $PRIV/window.out)"
}
window_shot() {   # a test: the window's headless screen -> RUN/window-shots/NAME.png
  [ -n "$WGWD" ] || return 0
  mkdir -p "$RUN/window-shots"
  GAMESCOPE_WAYLAND_DISPLAY="$WGWD" XDG_RUNTIME_DIR="$RT" timeout 10 gamescopectl screenshot "$RUN/window-shots/$1.png" > /dev/null 2>&1
}
window_end() {
  [ -n "$WPID" ] || return 0
  local i
  for i in $(seq 1 $((WIN_CLOSE + 15))); do
    kill -0 "$WPID" 2>/dev/null || { note "the PC window closed"; break; }
    [ "$i" = 3 ] && window_shot ended
    sleep 1
  done
  kill -0 "$WPID" 2>/dev/null && tr '\0' ' ' < "/proc/$WPID/cmdline" 2>/dev/null | grep -q somvr_window.py && kill "$WPID" 2>/dev/null &&
    note "the PC window still open $((WIN_CLOSE + 15)) s after the end: closed"
  [ -n "$WGS" ] && kill -TERM "$WGS" 2>/dev/null
  return 0
}

if [ "$DRY" = 1 ]; then
  [ $WIN = 1 ] && note "dry run: the PC window's modules: $(python3 -I "$PRIV/somvr_window.py" --check 2>&1 | tail -1)"
  note "dry run: the session's --dry-run in the foreground (folder $RUN)"
  (cd "$SOM" && bash "$PRIV/som-win-session.sh" "${ARGS[@]}" --dry-run); st=$?
  note "dry run: the session's dry run exit $st"
  exit $st
fi

T=$(date +%s)
stop_unit() { note "stop asked (signal): stopping the session"; systemctl --user stop --no-block "$UNIT" 2>/dev/null; STOPPING=1; }
STOPPING=0
trap stop_unit TERM INT HUP
systemctl --user reset-failed "$UNIT" 2>/dev/null
rm -f "$PRIV/ending" "$PRIV/next-start"
systemd-run --user --quiet --collect --unit="$UNIT" \
  --description="Shadow of Mordor VR headset session, Windows build ($WIN_BUILD)$([ "$TEST" = 1 ] && echo ' - TEST, Monado')" \
  -p KillMode=mixed -p TimeoutStopSec=240 --working-directory="$SOM" \
  --setenv=PATH=/usr/local/bin:/usr/bin:/bin bash "$PRIV/som-win-session.sh" "${ARGS[@]}" >> "$LOGF" 2>&1 ||
  fail "systemd-run could not start the session. Tell Claude."
note "session unit $UNIT started (folder $RUN)"
exec 9>&-
start_window
tell "$APP is starting" "About a minute: the game starts by itself, then fades in at the practice meadow. The PC window's Stop button ends it."

away=0; n=0; shot60=0
while unit_running; do   # the unit every second, the headset every 5 s
  sleep 1 & wait $!
  [ $STOPPING = 1 ] && continue
  n=$((n + 1))
  [ "$TEST" = 1 ] && [ $shot60 = 0 ] && [ $n -ge 90 ] && { window_shot playing; shot60=1; }
  [ $((n % 5)) = 0 ] || continue
  [ "$TEST" = 1 ] && continue
  headset; h=$?
  if [ $h = 1 ]; then away=$((away + 5)); else away=0; fi
  if [ $away -ge $GONE ]; then
    tell "Stopping $APP" "The Quest has been disconnected for 5 minutes."
    systemctl --user stop --no-block "$UNIT" 2>/dev/null; STOPPING=1
  fi
done
trap - TERM INT HUP

note "session over after $(( $(date +%s) - T )) s; run $RUN"
if r=$(grep -a -m1 -o 'REFUSED: .*' "$RUN/run.log" 2>/dev/null); then
  tell "$APP did not start" "${r#REFUSED: }. Tell Claude."
elif ! grep -a -q 'game started:' "$RUN/run.log" 2>/dev/null; then
  tell "$APP ended" "It ended before the game started. Tell Claude."
elif grep -a -q 'SAVE OK' "$RUN/run.log" 2>/dev/null && ! grep -a -q '!!! save (Windows)' "$RUN/run.log"; then
  tell "$APP ended" "Your progress is in the Windows build's own save (a backup was made before); the Steam save is unchanged."
else
  tell "$APP ended" "The save check found something: $(grep -a -m1 -o -E '!!! (SAVE|save).*' "$RUN/run.log" | cut -c1-120). Tell Claude."
fi
window_end
exit 0
