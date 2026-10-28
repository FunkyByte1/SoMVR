#!/bin/bash
# som-vr-headset.sh - the user's own headset session with the current VR test build, one click: the Steam shortcut (a
# non-Steam game) and WiVRn's in-headset app list both run it, through
# ~/.local/share/applications/Shadow of Mordor VR.desktop (Categories X-WiVRn-VR, as Dishonored VR and HoF2 VR).
# It runs what the developer's headset sessions run (boot_check.sh --headset: the headless gamescope at the headset's
# 90 Hz, the save copy (the real save is never written; boot_check checks it after), a start sequence that takes the
# game to the main menu's Continue and the story), in a systemd user unit of its own (somvr-headset).
# The feature switches come from the frozen build's own file, $SOM/logs/$BUILD/headset.env: one KEY=VALUE per line,
# blank lines and `#` comments allowed; only SOMVR_* keys (and XR_LOADER_DEBUG / VK_LOADER_DEBUG); values from
# [A-Za-z0-9_.,:/@+-] (@RUN@ = the run's folder); the launcher's own keys (the layer path and name, the OpenXR runtime,
# SOMVR_XR_LOG, SOMVR_SETTINGS, SOMVR_TUNE) and the harness's (SOMVR_SAVEDIR, SOMVR_LOG, SOMVR_TRIGGER) refused. The
# file is read line by line and checked, never sourced or executed; a bad line = no start (a notification names it).
# With the file (build 6 on): the build-6 launch (docs/re-launcher.md: --no-feral-options and the run's own prefs with
# GameOptionsDialogShouldShow(BigPicture) 0, SkipOSWarnings 1, the Lnx* alerts 1, DisableSplashVideos 1: no pre-game
# window, no distro box) and scripts/cmds/somvr-vr6.cmd (the boot movies, the main menu's Continue, the story).
# Without it: the built-in build-5 switches and the build-5 launch (somvr-vr5.cmd clicks through the pre-game window).
# The PC window (scripts/somvr_window.py, the user: "it should open a window on the pc that I can see when the game is
# running"): started with the session on the desktop (DISPLAY, else the systemd user environment's); the session's
# state and time, a small live picture every 2 s, Stop the game (= systemctl --user stop somvr-headset), the bug marks;
# it closes by itself 10 s after the session ended (build 15b: 30 s), the save check's result shown.
# A start while the last session is ending (build 16: boot_check's cleanup - its --ending-file $PRIV/ending - or the
# unit stopping) waits for its end and then starts by itself (a notification, and the old PC window says "the last
# session is closing - the next one starts in a moment"); a start while a session is playing or starting still refuses.
# Two starts at once: the second one refuses (a lock around the unit's start).
# The unit (somvr-headset):
#   - Steam's environment (its overlay LD_PRELOAD, its runtime LD_LIBRARY_PATH) never reaches the session
#   - Steam's Stop (SIGTERM here) becomes `systemctl --user stop`: boot_check gets SIGTERM (KillMode=mixed) and its
#     cleanup (the game, gamescope, the save check, the slot) finishes even when Steam kills this script
#   - this script waits for the unit, so Steam shows the shortcut as running until the session ends
#   - the unit runs copies of boot_check.sh, prefs_set.py, the start commands and the PC window
#     ($XDG_RUNTIME_DIR/somvr-headset/): editing the repo's scripts during a session cannot break it (bash reads a
#     script as it runs)
# Before: WiVRn's server must run (else a notification says so); a headset not connected yet gets 5 minutes; Steam is
# started if it isn't running. The player comes first: a developer's SoM test (boot_check without --headset) is
# stopped (its own cleanup, about a minute); another game's fresh slot marker (a HoF2 test; its harness stops when the
# user is in VR) gets 2 minutes to go.
# Ends: the game quits, Steam's Stop, the headset disconnected for 5 minutes, or the 2-hour limit (boot_check --hard).
# Desktop notifications (notify-send) say what happens; every step goes to logs/som-vr-headset.log, the session's own
# logs to logs/boot-check-<ts>/ (named in that log).
# SOMVR_HEADSET_DRYRUN=1: the same checks (WiVRn's only noted), the switches file, the PC window's modules (its
# --check), then boot_check --dry-run in the foreground (guards, save check, paths; reserves and starts nothing).
# SOMVR_HEADSET_ENVFILE=FILE (dry runs only): read FILE instead of the build's headset.env (a test of the file).
set -u
unset LD_PRELOAD LD_LIBRARY_PATH
export PATH=/usr/local/bin:/usr/bin:/bin

SOM=$HOME/Projects/flat2vr/ShadowOfMordor
SCR=$(cd "$(dirname "$0")" && pwd)       # the scripts it copies: the ones next to it (the main checkout's for the user)
BUILD=build-vr19b                        # the build the user tests (logs/BUILD); changed only between sessions
ENVF=$SOM/logs/$BUILD/headset.env       # its feature switches (the header); missing = the built-in build-5 set
CMDS5=$SCR/cmds/somvr-vr5.cmd   # the start sequences: build 5's (the pre-game window), build 6's
CMDS6=$SCR/cmds/somvr-vr6.cmd
SEED=$SOM/logs/boot-check-20261018-003728/preferences.seeded
UNIT=somvr-headset
HARD=7200                               # the session's limit, seconds
WAIT_HEADSET=300; GONE=300; WAIT_SLOT=120
RT=/run/user/$(id -u)
VRS=$RT/vr-session
PRIV=$RT/somvr-headset
LOGF=$SOM/logs/som-vr-headset.log
DRY=${SOMVR_HEADSET_DRYRUN:-0}
BLD=$SOM/logs/$BUILD/ShadowOfMordor
[ "$DRY" = 1 ] && [ -n "${SOMVR_HEADSET_ENVFILE:-}" ] && ENVF=$SOMVR_HEADSET_ENVFILE
# the built-in switches (build 5d's, without its SOMVR_VIGNETTE=0: the mod's default vignette now): no headset.env
SW5=(SOMVR_STEREO=B SOMVR_VIEWS=xr SOMVR_COMFORT=1 SOMVR_XR=1 SOMVR_XR_DUMP=600 SOMVR_EYE_SIZE=xr SOMVR_INPUT=1
  SOMVR_POINTER=1 SOMVR_HUD=quad SOMVR_AUDIO=head SOMVR_BASE=head SOMVR_HIDE_HEAD=parts SOMVR_HEAD_PARTS=main
  SOMVR_FALLBACK=auto SOMVR_ARMS=1 SOMVR_ARMS_STRETCH=1 SOMVR_COMBAT=1 SOMVR_BLADE=controller SOMVR_VRMENU=1
  SOMVR_XR_ENV=on SOMVR_ANCHOR=root SOMVR_LOCO=1 SOMVR_LOCO_HOLD=attack SOMVR_GAMEMOVES=off SOMVR_COMBAT_REACT=1
  SOMVR_CLIMB=b SOMVR_THREAT=1 SOMVR_BODYMOMENTS=1 SOMVR_SLOTS=1 SOMVR_SLOTS_KEEP=attach SOMVR_DEFENCE=1
  SOMVR_PRACTICE=auto SOMVR_PRACTICE_AT=32500,2100,-24800)
# the launcher's own keys (the file may not set them) and the harness's
FIXED=" VK_LAYER_PATH VK_INSTANCE_LAYERS XR_RUNTIME_JSON SOMVR_XR_LOG SOMVR_SETTINGS SOMVR_TUNE SOMVR_SAVEDIR SOMVR_LOG SOMVR_TRIGGER "
load_switches() {   # $1 = headset.env -> SW (KEY=VALUE words); 1 + ENV_BAD = refused. Read and checked, never sourced.
  local n=0 line k v re_kv='^[[:space:]]*([A-Z][A-Z0-9_]*)=([^[:space:]#]*)[[:space:]]*(#.*)?$' re_skip='^[[:space:]]*(#.*)?$'
  SW=(); ENV_BAD=""
  [ -r "$1" ] || { ENV_BAD="cannot read it"; return 1; }
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1)); line=${line%$'\r'}
    [ $n -le 1000 ] || { ENV_BAD="more than 1000 lines"; return 1; }   # build 16's file: 425 lines (its comments)
    [[ $line =~ $re_skip ]] && continue
    [[ $line =~ $re_kv ]] || { ENV_BAD="line $n is not KEY=VALUE: $(printf '%s' "$line" | cut -c1-60)"; return 1; }
    k=${BASH_REMATCH[1]}; v=${BASH_REMATCH[2]}
    [[ $k =~ ^SOMVR_[A-Z0-9_]+$ || $k == XR_LOADER_DEBUG || $k == VK_LOADER_DEBUG ]] ||
      { ENV_BAD="line $n: $k is not a SOMVR_ key"; return 1; }
    [[ $FIXED == *" $k "* ]] && { ENV_BAD="line $n: $k is the launcher's own"; return 1; }
    [ ${#k} -le 64 ] && [[ $v =~ ^[A-Za-z0-9_.,:/@+-]*$ ]] && [ ${#v} -le 256 ] ||
      { ENV_BAD="line $n: $k's value is not a plain switch value"; return 1; }
    SW+=("$k=$v")
  done < "$1"
  [ ${#SW[@]} -gt 0 ] || { ENV_BAD="no switches in it"; return 1; }
  return 0
}

note() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$LOGF" 2>/dev/null; printf 'som-vr-headset: %s\n' "$*" >&2; }
tell() {   # TITLE BODY: a desktop notification and the log
  note "$1: $2"
  [ "$DRY" = 1 ] || notify-send -a "Shadow of Mordor VR" -i steam_icon_241930 "$1" "$2" 2>/dev/null || true
}
fail() { tell "Shadow of Mordor VR did not start" "$1"; exit 1; }
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
bc_main() {   # the main boot_check.sh process(es) (its subshells share the command line: only those whose parent is not one)
  ps -eo pid=,ppid=,args= | awk '$3 ~ /bash$/ && $4 ~ /boot_check\.sh$/ { p[$1] = $2; a[$1] = $0 }
    END { for (i in p) if (!(p[i] in p)) print i " " (a[i] ~ / --headset( |$)/ ? "headset" : "test") }'
}
slot_busy() {   # another environment's fresh reserved-env* / game-env* (boot_check's rule)
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
note "start (build $BUILD$([ "$DRY" = 1 ] && echo ', DRY RUN'); parent: $(ps -o args= -p $PPID 2>/dev/null | cut -c1-120))"
[ -f "$BLD/mod/somvr/build/somvr.so" ] || fail "The VR build $BUILD is missing. Tell Claude."
if [ -e "$ENVF" ]; then   # build 6 on: its own switches, the build-6 launch and start sequence
  load_switches "$ENVF" || fail "The build's switches file is not valid ($ENVF: $ENV_BAD). Tell Claude."
  B6=1; CMDS=$CMDS6
  LAUNCH=(--game-arg --no-feral-options --prefs-set GameOptionsDialogShouldShow=0
    --prefs-set GameOptionsDialogShouldShowBigPicture=0 --prefs-set SkipOSWarnings=1
    --prefs-set 'SpecificationAlerts\LnxDistributionUnsupported=1' --prefs-set 'SpecificationAlerts\LnxCPUGovernorSubOptimal=1'
    --prefs-set DisableSplashVideos=1)
  note "switches: ${#SW[@]} from $ENVF; the build-6 launch, $CMDS"
else
  SW=("${SW5[@]}"); B6=0; CMDS=$CMDS5; LAUNCH=()
  note "switches: the built-in build-5 set (${#SW[@]}; no $ENVF); the build-5 launch, $CMDS"
fi
[ -f "$CMDS" ] && [ -f "$SEED" ] || fail "A start file is missing ($CMDS or the prefs). Tell Claude."
[ $B6 = 0 ] || [ -f "$SCR/prefs_set.py" ] || fail "A start file is missing (prefs_set.py). Tell Claude."

session_ending() {   # 0 = the running session is in its end: the unit stopping, or boot_check's cleanup (its --ending-file,
                     # removed before each start)
  [ "$(unit_state)" = deactivating ] || [ -s "$PRIV/ending" ]
}
start_gate() {   # a session running: refuse (playing or starting) or wait for its end (ending); returns = go on
  local i
  unit_running || return 0
  if ! session_ending; then tell "Shadow of Mordor VR is already running" "It is starting or running in the headset."; exit 0; fi
  if [ "$DRY" = 1 ]; then note "dry run: the last session is closing: a real start would wait for it, then start"; exit 0; fi
  tell "Shadow of Mordor VR: starting in a moment" "The last session is closing - the new one starts by itself in a moment."
  mkdir -p "$PRIV" && date +%s > "$PRIV/next-start" 2>/dev/null   # (the old PC window shows it)
  for i in $(seq 1 "${WAIT_END:-300}"); do unit_running || break; sleep 1; done   # (the unit's TimeoutStopSec is 240)
  unit_running && fail "The last session did not close within 5 minutes. Tell Claude."
  note "the last session closed after $i s: starting"
}
start_gate
while read -r pid kind; do
  [ -n "${pid:-}" ] || continue
  if [ "$kind" = headset ]; then
    fail "Another VR session of the game is already running (pid $pid). Tell Claude."
  fi
  if [ "$DRY" = 1 ]; then note "dry run: a test run (boot_check $pid) would be stopped first (left alone)"; continue; fi
  tell "Making room" "A test run of the game is stopping first (about a minute)."
  kill -TERM "$pid" 2>/dev/null
  for i in $(seq 1 90); do kill -0 "$pid" 2>/dev/null || break; sleep 2; done
  kill -0 "$pid" 2>/dev/null && fail "A test run of the game would not stop (pid $pid). Tell Claude."
  note "the test run $pid ended"
done < <(bc_main)
if pgrep -x ShadowOfMordor > /dev/null; then
  [ "$DRY" = 1 ] && note "dry run: the game is running (a test's, or outside VR): a real start would wait or refuse" ||
    fail "The game is already running (outside VR). Quit it, then press Play again."
fi

if [ "$DRY" = 1 ]; then
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
  tell "Waiting for another test" "A test of another game ($s) is ending first (up to 2 minutes)."
  for i in $(seq 1 $((WAIT_SLOT / 5))); do sleep 5; s=$(slot_busy) || break; done
  s=$(slot_busy) && fail "Another game's test ($s) still holds the game slot. Tell Claude."
  note "the slot is free"
fi

if ! pgrep -x steam > /dev/null; then
  [ "$DRY" = 1 ] && fail "Steam is not running (a dry run starts nothing)."
  tell "Starting Steam" "The game needs Steam: starting it first."
  setsid -f steam -silent > /dev/null 2>&1 < /dev/null
  for i in $(seq 1 60); do pgrep -x steamwebhelper > /dev/null && break; sleep 2; done
  pgrep -x steamwebhelper > /dev/null || fail "Steam did not start within 2 minutes."
  sleep 20   # its login
  note "Steam started"
fi

# one start at a time from here (two starts waiting for the same end: the second one finds the unit running)
if [ "$DRY" != 1 ]; then
  exec 9> "$RT/somvr-headset.lock"
  flock -w 30 9 || fail "Another start of the session did not finish within 30 s. Tell Claude."
  if unit_running; then tell "Shadow of Mordor VR is already running" "It is starting or running in the headset."; exit 0; fi
fi
# the session's own copies of the scripts it reads while it runs (never while a session runs: checked above)
mkdir -p "$PRIV" && cp -f "$SCR/boot_check.sh" "$PRIV/boot_check.sh" && cp -f "$CMDS" "$PRIV/start.cmd" &&
  { [ $B6 = 0 ] || cp -f "$SCR/prefs_set.py" "$PRIV/prefs_set.py"; } ||
  fail "Could not copy the session scripts to $PRIV."
WIN=0; cp -f "$SCR/somvr_window.py" "$PRIV/somvr_window.py" 2>/dev/null && WIN=1   # (no window: still a session)
GE=(--game-env VK_LAYER_PATH="$BLD/mod/somvr/vklayer" --game-env VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
  --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json --game-env SOMVR_XR_LOG=@RUN@/xr-layer.log
  --game-env "SOMVR_SETTINGS=$HOME/.local/share/somvr/settings.conf" --game-env SOMVR_TUNE=@RUN@/tune.txt)
for kv in "${SW[@]}"; do GE+=(--game-env "$kv"); done
ARGS=(--headset --steam --save-copy --seed-prefs "$SEED" "${LAUNCH[@]}"
  --preload "$BLD/mod/somvr/build/somvr.so" "${GE[@]}"
  --cmds "$PRIV/start.cmd" --hold $((HARD - 200)) --hard "$HARD" --refresh 90 --ending-file "$PRIV/ending")
start_window() {   # the PC window on the desktop (Steam's and WiVRn's starts both reach here; DISPLAY from either)
  [ $WIN = 1 ] || { note "the PC window: no copy of somvr_window.py (not started)"; return 0; }
  local d=${DISPLAY:-} x=${XAUTHORITY:-} e
  if [ -z "$d" ]; then
    e=$(systemctl --user show-environment 2>/dev/null)
    d=$(printf '%s\n' "$e" | sed -n 's/^DISPLAY=//p' | head -1)
    [ -n "$x" ] || x=$(printf '%s\n' "$e" | sed -n 's/^XAUTHORITY=//p' | head -1)
  fi
  [ -n "$d" ] || { note "the PC window: no DISPLAY (not started)"; return 0; }
  # its own session (Steam's Stop of this script leaves it to show the end); env and setsid exec: $! is python's pid
  env DISPLAY="$d" ${x:+XAUTHORITY="$x"} setsid python3 -I "$PRIV/somvr_window.py" --unit "$UNIT" --since "$T" \
    --build "$BUILD" --close-after "$WIN_CLOSE" --next-start "$PRIV/next-start" > "$PRIV/window.out" 2>&1 < /dev/null &
  WPID=$!
  note "the PC window started (pid $WPID, DISPLAY $d; its output $PRIV/window.out)"
}
WPID=""; WIN_CLOSE=10   # (build 15b: 30 s)
window_end() {   # after the session: the window closes itself WIN_CLOSE s after the end; gone at WIN_CLOSE + 15 s in any case
  [ -n "$WPID" ] || return 0
  local i
  for i in $(seq 1 $((WIN_CLOSE + 15))); do kill -0 "$WPID" 2>/dev/null || { note "the PC window closed"; return 0; }; sleep 1; done
  tr '\0' ' ' < "/proc/$WPID/cmdline" 2>/dev/null | grep -q somvr_window.py && kill "$WPID" 2>/dev/null &&
    note "the PC window still open $((WIN_CLOSE + 15)) s after the end: closed"
  return 0
}

if [ "$DRY" = 1 ]; then
  if [ $WIN = 1 ]; then note "dry run: the PC window's modules: $(python3 -I "$PRIV/somvr_window.py" --check 2>&1 | tail -1)"; fi
  note "dry run: boot_check --dry-run in the foreground"
  (cd "$SOM" && bash "$PRIV/boot_check.sh" "${ARGS[@]}" --dry-run); st=$?
  note "dry run: boot_check exit $st"
  exit $st
fi

T=$(date +%s)
stop_unit() { note "stop asked (signal): stopping the session"; systemctl --user stop --no-block "$UNIT" 2>/dev/null; STOPPING=1; }
STOPPING=0
trap stop_unit TERM INT HUP
systemctl --user reset-failed "$UNIT" 2>/dev/null
rm -f "$PRIV/ending" "$PRIV/next-start"
systemd-run --user --quiet --collect --unit="$UNIT" --description="Shadow of Mordor VR headset session ($BUILD)" \
  -p KillMode=mixed -p TimeoutStopSec=240 --working-directory="$SOM" \
  --setenv=PATH=/usr/local/bin:/usr/bin:/bin bash "$PRIV/boot_check.sh" "${ARGS[@]}" >> "$LOGF" 2>&1 ||
  fail "systemd-run could not start the session. Tell Claude."
note "session unit $UNIT started"
exec 9>&-   # (the start lock)
start_window
if [ $B6 = 1 ]; then
  tell "Shadow of Mordor VR is starting" "About 2 minutes: the game starts by itself, then fades in at the practice meadow. The PC window's Stop button ends it."
else
  tell "Shadow of Mordor VR is starting" "About 3 minutes: the game clicks itself through its menus, then fades in at the practice meadow."
fi

away=0; n=0; RUN=""
while unit_running; do   # the unit every second (its end seen at once), the headset every 5 s
  # this session's run folder, while it runs (a next start's folder may be newer by the time this one is reported)
  [ -n "$RUN" ] || RUN=$(find "$SOM/logs" -maxdepth 1 -name 'boot-check-2*' -newermt "@$((T - 5))" 2>/dev/null | sort | head -1)
  sleep 1 & wait $!   # (a signal ends the wait at once)
  [ $STOPPING = 1 ] && continue
  n=$((n + 1)); [ $((n % 5)) = 0 ] || continue
  headset; h=$?
  if [ $h = 1 ]; then away=$((away + 5)); else away=0; fi
  if [ $away -ge $GONE ]; then
    tell "Stopping Shadow of Mordor VR" "The Quest has been disconnected for 5 minutes."
    systemctl --user stop --no-block "$UNIT" 2>/dev/null; STOPPING=1
  fi
done
trap - TERM INT HUP

[ -n "$RUN" ] || RUN=$(find "$SOM/logs" -maxdepth 1 -name 'boot-check-2*' -newermt "@$((T - 5))" 2>/dev/null | sort | head -1)
note "session over after $(( $(date +%s) - T )) s; run ${RUN:-(no run folder found)}"
if [ -z "$RUN" ]; then
  tell "Shadow of Mordor VR ended" "It ended before it started a run. Tell Claude."
elif r=$(grep -a -m1 -o 'REFUSED: .*' "$RUN/run.log" 2>/dev/null); then
  tell "Shadow of Mordor VR did not start" "${r#REFUSED: }. Tell Claude."
else
  s=$(grep -a -m1 -o -E 'GameData.sav (UNCHANGED|CHANGED)|RESTORE[A-Z ]*' "$RUN/run.log" 2>/dev/null)
  case $s in
    *UNCHANGED*) tell "Shadow of Mordor VR ended" "Your real save is unchanged." ;;
    *) tell "Shadow of Mordor VR ended" "Save check: ${s:-not found}. Tell Claude." ;;
  esac
fi
window_end
exit 0
