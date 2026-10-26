#!/usr/bin/env bash
# boot_check.sh - one flat boot of the native Linux (Feral, Vulkan) Shadow of Mordor on a headless gamescope
# ("virtual screen", the HoF2 idea: nothing on the user's desktop), with the user's real save provably untouched.
#
#   boot_check.sh [--steam] [--dry-run] [--auto SECS] [--hold SECS] [--hard SECS] [--size WxH] [--backup DIR]
#                 [--vram-min-gb N] [--seed-prefs F] [--prefs-set NAME=INT]... [--preload /ABS/LIB.so]...
#                 [--game-env K=V]... [--game-arg ARG]... [--cmds FILE]
#
#   --steam         the user's decision 2026-10-17 ~23:40: the game reaches the running Steam client normally (real
#                   HOME, network, writable save folder, Steam Cloud on); still started directly in the headless
#                   gamescope, never through the Steam client (`steam`/`xdg-open` are PATH stubs: its relaunch-
#                   through-Steam fallback `system("steam steam://run/%u")` is blocked). Prefs still go to RUN/home
#                   (XDG_DATA_HOME). Right before the launch: a new backup backups/save-<ts>-prelaunch, checked against
#                   --backup. After the quit: 30 s for Steam's sync, compare; if GameData.sav changed it is restored
#                   (only it) from the pre-launch backup and the changed copy kept in RUN/changed/.
#                   Launch: scout LD_LIBRARY_PATH runtime + scripts/ptrace_parent.py as the game's parent.
#                   D-Bus: the real session bus (inherited DBUS_SESSION_BUS_ADDRESS, else $XDG_RUNTIME_DIR/bus) - the
#                   bundled SDL 2.0.8 crashes in SDL_DBus_Init without a reachable bus (attempt 2, 2026-10-18 00:03).
#                   So the game's GameMode client reaches gamemoded (the user's normal gaming behaviour: LACT's
#                   "Gaming" profile has a gamemode rule); RUN/power.log records governor / gamemoded / LACT's active
#                   profile read-only. After the run, new feral* entries in the real ~/.local/share, ~/.config and
#                   ~/.cache (the crash reporter's feral_tmp_crash_message) are moved into RUN/realhome-moved/.
#   --seed-prefs F  start from this Feral preferences file (e.g. the one an earlier boot wrote)
#   --prefs-set NAME=INT  (repeatable) an integer value in the run's own seeded prefs (RUN/home, never the user's), in
#                   the game's real setup key Software\Monolith Productions\Middle Earth Shadow of Mordor\Setup
#                   (docs/re-launcher.md 1); a backslash names a subkey: 'SpecificationAlerts\LnxDistributionUnsupported=1'.
#                   scripts/prefs_set.py (next to this script, else in SOM/scripts) edits the copy after --seed-prefs;
#                   RUN/preferences.seeded shows the result. E.g. GameOptionsDialogShouldShow=0, SkipOSWarnings=1 (the
#                   unsupported-distro box), DisableSplashVideos=1 (the game's own "+SkipSplashScreens 1": no logo and
#                   legal movies, docs/re-launcher.md 8)
#   --game-arg ARG  (repeatable) an argument for the game binary, after it in the order given (e.g. --no-feral-options:
#                   no pre-game window, docs/re-launcher.md 1); in launch-cmd.txt
#   --dry-run       guards, save check, paths and tools only; prints the launch; reserves and starts nothing
#   --headset       the user plays in the headset on purpose (a supervised session the user asked for, 2026-10-19):
#                   needs --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json; the "user in VR" guard
#                   before the launch and the VR monitor (which would end the run when the game connects to WiVRn)
#                   are off; the game's sound is on (no SDL_AUDIODRIVER=dummy: SDL's own pick, PulseAudio, as a Steam
#                   start; WiVRn's sink plays it in the headset). The slot, the save check and everything else stay as
#                   they are
#   --auto SECS     no commands: screenshots for SECS seconds after the start, then quit (default: command mode)
#   --hold SECS     command mode: at most SECS seconds of game time (default 1200; the hard limit wins)
#   --hard SECS     the hard limit of the whole run (default 1800, the 30 minutes of a test); above 1800 only with
#                   --headset (the user's own sessions, scripts/som-vr-headset.sh), at most 14400
#   --size WxH      gamescope output and nested X screen (default 1920x1080)
#   --refresh HZ    gamescope's refresh, the game's vsync cap (default 60; the headset sessions: the headset's, 90)
#   --backup DIR    save backup with SHA256SUMS (default: the newest ShadowOfMordor/backups/save-*)
#   --preload SO    LD_PRELOAD=SO (absolute path, ELF64 x86-64) plus SOMVR_LOG=RUN/somvr.log and
#                   SOMVR_TRIGGER=RUN/somvr.trigger for the game process only: set by the `env -i` that execs the game
#                   binary, so gamescope, ptrace_parent.py / bwrap, env and the harness's stub test never get them. The
#                   game's own children (FeralCefSubprocess, FeralLinuxMessage, the crash reporter, the `steam` stub via
#                   system()) inherit the game's environment unless the library unsets LD_PRELOAD. In launch-cmd.txt.
#                   Repeatable: LD_PRELOAD = the paths joined with ':' in the order given (no ':' or blanks in a path),
#                   e.g. --preload .../mod/spike1/build/somvr_spike1.so --preload .../mod/probe/build/somvr_probe.so.
#                   The probe library (a preload named somvr_probe*.so) also gets SOMVR_PROBE_CMD=RUN/probe.cmd,
#                   SOMVR_PROBE_LOG=RUN/probe.log and SOMVR_PROBE_OUT=RUN/probe-out, each unless --game-env gives it
#                   (then an absolute path). Its active words need spike1's save guard (somvr_spike1*.so + --save-copy):
#                   without it the plan says so and the probe refuses them.
#   --save-copy     (with --steam and --preload) the library's save guard: the pre-launch backup's GameData.sav is
#                   copied to RUN/saves/ and SOMVR_SAVEDIR=RUN/saves goes to the game only. The library then serves all
#                   of the game's remote-storage file calls (WBOnlineService slots 219-222) from that folder and exits
#                   with status 3 before the game runs if the guard is incomplete. The real-save check still runs.
#   --game-env K=V  extra environment for the game only (repeatable; K =~ ^[A-Z_][A-Z0-9_]*$ and not one the harness or
#                   --preload sets), e.g. SOMVR_MODE=off30 SOMVR_AT=vp VK_LAYER_PATH=... VK_INSTANCE_LAYERS=...
#                   @RUN@ in V becomes the run folder RUN (in a dry run the dry-run folder), which exists before the
#                   launch: e.g. SOMVR_XR_LOG=@RUN@/xr-layer.log (the layer's log and its SOMVR_XR_DUMP pictures land in
#                   RUN). A SOMVR_PROBE_* path may start with it. launch-cmd.txt shows the replaced values.
#   --cmds FILE     command mode: FILE (e.g. scripts/cmds/menu-to-benchmark.cmd) is appended to RUN/cmd right before
#                   the launch, so it runs from game time 0 as one batch; unknown commands refuse the run
#
# Command mode: append lines to RUN/cmd (RUN = logs/boot-check-<ts>/, printed at the start); done ones go to cmd.done:
#   shot NAME | key KEY... (xdotool key) | xdo ARGS... (raw xdotool) | mark LABEL | wait SECS (<= 30) |
#   trigger [view|vp|sbs|sbs0|sbsB|sbsB0|blur|recenter|turn+|turn-] | untrigger | probe WORDS... | quit |
#   waitlog SECS TEXT... | skipvideos SECS KEY TEXT...
#   waitlog = wait (at most SECS, <= 300) until somvr.log has a line with TEXT (fixed text, blank runs = one space) after
#   the previous waitlog's match (each waitlog starts where the last one matched: a line that came early still counts);
#   not found in time = logged ("!!! waitlog") and the batch goes on. skipvideos = the same wait for TEXT, and meanwhile
#   KEY (xdotool) at every new `SESSION ui ... video=1 ...` line (somvr.so's SOMVR_MARK/SOMVR_QUIT UI state lines: a
#   video starts; the boot movies), from the same cursor. `wait`, `waitlog` and `skipvideos` keep serving the player's
#   requests below every 0.5 s.
#   trigger = write RUN/somvr.trigger (the --preload library offsets while it exists; the word picks its hook, or
#   sbs/sbs0 = the two-eyes-per-frame test in halves (layout A), sbsB/sbsB0 = two full-size eyes (layout B, the screen shows
#   eye R), blur = the plain trigger with spike1's velocity blur back on); recenter / turn+ / turn- = mod/somvr's one-shot
#   words (somvr.c SOMVR_TRIGGER: it polls every 250 ms, a content or mtime change with one of them first acts once - a
#   rig recentre, a snap turn right / left - and the file stays, so stereo stays on). Only while the file already stands
#   (after a plain `trigger`): a new file's first content is not acted on, it only turns stereo on (logged). One change
#   per poll: `wait 1` or more after the plain `trigger` and between two words;
#   untrigger = remove it. '#' starts a comment on a WHOLE line only.
#   probe = WORDS as one line (blank runs become one space; ';' separates probe words, e.g. `probe orcs;mark x`) into
#   the probe's command file (SOMVR_PROBE_CMD, default RUN/probe.cmd): a tmp file in the same folder, then rename;
#   the probe reads each new version once (it polls every 100 ms), so the harness then waits up to 2 s for the probe's
#   "CMD queued" line in its log before the next command. At most 511 bytes.
#   A batch stops early when the game is gone.
# The player's requests (somvr.so's SOMVR_QUIT / SOMVR_MARK, mod/somvr/somvr_session.h; served every 0.5 s, also inside
# `wait` and `waitlog`): RUN/quit.request (the VR menu's Quit game, confirmed) = a clean stop as `quit` (the cleanup stops
# the game and checks the save); RUN/marks/<time>_<n>/want (a bug mark: the chord B + Y or the menu's Report; the
# library wrote state.txt there and the layer writes eyes.ppm) = the flat picture (flat.jpg), the log tails
# (somvr_tail.txt, xr_tail.txt) and voice.wav: VOICE seconds (default 9, SOMVR_MARK_VOICE_S in --game-env) from WiVRn's
# microphone (its PipeWire source, a name with "wivrn", never another microphone) in a separate process (parecord),
# voice.txt says what it got; eyes.ppm -> eyes.jpg once written (nice 19, one CPU). want -> served; RUN/marks.log lists them.
# Auto screenshots: shots/auto-NNNNs.png every 10 s for the first 240 s of game time, then every 60 s (paused while a
# batch runs).
# The stop (every end: the commands' `quit`, the player's Quit, a signal, the limits): with --preload and the trigger
# file standing, the word `endlogs` first (somvr.so writes every feature's END lines and the VR menu's unsaved change;
# up to 3 s), then SIGTERM (which skips the library's destructor).
# Game install (2026-10-18: the pre-game window's CEF wrote bin/debug.log and bin/blob_storage/ into it): before the
# launch RUN/install.stamp + install-before.txt (type, size, mtime, path); after the run (aborts and traps too)
# install-newer.txt (find -newer the stamp), new bin/debug.log and bin/blob_storage (only if absent before) moved into
# RUN/install-leftovers/, install-after.txt; anything else new/changed/gone = "!!!" lines in summary.txt, left in place.
# libcef.so (CEF 3440, Chromium 68.0.3440.7) builds debug.log from PathService::Get(2 = DIR_EXE) = bin/ unless a
# --log-file switch is set: a different cwd would not move it. blob_storage = BrowserContext::GetPath()/blob_storage/<GUID>.
#
# Isolation (2026-10-17): the port keeps its profile (GameData.sav) in Steam remote storage through the Steam
# client (FeralNet -> STEAMREMOTESTORAGE_INTERFACE_VERSION013), which a mount namespace cannot stop. So this boot
# never reaches Steam: HOME/XDG_* point at RUN/home (prefs land there, pre-seeded to skip the pre-game window),
# ~/.steam is a tmpfs and linux64/steamclient.so is /dev/null inside the game's namespace, the real
# userdata/STEAM_ACCOUNT_ID/241930 is read-only there, no network (--unshare-net), no D-Bus, and `steam`/`xdg-open` are
# stubs that log to RUN/blocked.log. Audio: SDL dummy driver (silent). Never through the Steam client.
# RESULT 2026-10-17 23:22 (logs/boot-check-20261017-232204): this build does NOT boot without Steam - SteamAPI_Init fails,
# it runs `steam steam://...` (blocked by the stub) and exits 255 after ~7 s, no window. A boot that reaches the menu
# needs the Steam client, i.e. the user's real profile save in Steam remote storage: the user's decision (NOTES.md).
# Screenshots: `gamescopectl screenshot ABS.png` works once a client draws (tested with vkcube --wsi xcb).
#
# Slot (HandOfFate2/docs/two-environments.md): refuses while any other reserved-env*/game-env* marker is fresh
# (< 120 s); holds $VRS/reserved-envsom (touched every 20 s) while it runs; HoF2's run.sh globs reserved-env*, so it
# waits for us. Refuses / stops at once when the user is in VR. Needs >= 4 GB free VRAM. Hard limit 30 minutes.
# Run it OUTSIDE the Bash sandbox (host PIDs, the Steam folders).
set -uo pipefail
chrt -o -p 0 $$ 2>/dev/null || true   # never inherit keyd's SCHED_FIFO (memory: keyd real-time inheritance)

SOM=/home/bara/Projects/flat2vr/ShadowOfMordor
GAME=/home/bara/.local/share/Steam/steamapps/common/ShadowOfMordor
BIN=$GAME/bin/ShadowOfMordor
BUILDID=87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9
STEAMDIR=/home/bara/.local/share/Steam
SAVEAPP=$STEAMDIR/userdata/STEAM_ACCOUNT_ID/241930
REALPREFS=/home/bara/.local/share/feral-interactive
RT=/run/user/$(id -u)
VRS=$RT/vr-session
RES=$VRS/reserved-envsom
VRAM_DEV=/sys/bus/pci/devices/0000:0d:00.0
HARD=1800
MAIN=$$
T0=$(date +%s)

SEED=""; STEAM=0; DRY=0; HEADSET=0; AUTO=0; HOLD=1200; SIZE=1920x1080; REFRESH=60; BACKUP=""; VRAM_MIN_GB=4; PRELOADS=(); GEXTRA=(); CMDS=""; SAVECOPY=0
GARGS=(); PSETS=()
BCDIR=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
while [ $# -gt 0 ]; do
  case $1 in
    --dry-run) DRY=1; shift ;;
    --headset) HEADSET=1; shift ;;
    --steam) STEAM=1; shift ;;
    --seed-prefs) SEED=${2:?}; shift 2 ;;
    --auto) AUTO=${2:?}; shift 2 ;;
    --hold) HOLD=${2:?}; shift 2 ;;
    --hard) HARD=${2:?}; shift 2 ;;
    --size) SIZE=${2:?}; shift 2 ;;
    --refresh) REFRESH=${2:?}; shift 2 ;;
    --backup) BACKUP=${2:?}; shift 2 ;;
    --vram-min-gb) VRAM_MIN_GB=${2:?}; shift 2 ;;
    --preload) PRELOADS+=("${2:?}"); shift 2 ;;
    --save-copy) SAVECOPY=1; shift ;;
    --game-env) GEXTRA+=("${2:?}"); shift 2 ;;
    --game-arg) GARGS+=("${2:?}"); shift 2 ;;
    --prefs-set) PSETS+=("${2:?}"); shift 2 ;;
    --cmds) CMDS=${2:?}; shift 2 ;;
    -h|--help) awk 'NR > 1 && /^set -uo/ { exit } NR > 1' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ $HARD =~ ^[0-9]+$ ]] && [ "$HARD" -ge 300 ] && [ "$HARD" -le 14400 ] || { echo "bad --hard $HARD (300..14400)" >&2; exit 2; }
[ "$HARD" -le 1800 ] || [ $HEADSET = 1 ] || { echo "--hard above 1800 needs --headset" >&2; exit 2; }
case $SIZE in [0-9]*x[0-9]*) W=${SIZE%x*}; H=${SIZE#*x} ;; *) echo "bad --size $SIZE" >&2; exit 2 ;; esac
bad() { echo "$*" >&2; exit 2; }
PROBE=""; SPIKE1=""   # the probe / spike1 library among the preloads (by file name)
for p in "${PRELOADS[@]}"; do
  case $p in /*) ;; *) bad "--preload needs an absolute path: $p" ;; esac
  case $p in *:*|*[[:space:]]*) bad "--preload: ':' or a blank in $p (LD_PRELOAD separators)" ;; esac
  [ -f "$p" ] && [ -r "$p" ] || bad "--preload: no readable file $p"
  hdr=$(readelf -h "$p" 2>/dev/null)
  [[ $hdr == *'Class:'*ELF64* && $hdr == *'Machine:'*X86-64* ]] || bad "--preload: $p is not an ELF64 x86-64 object"
  case ${p##*/} in somvr_probe*.so) PROBE=$p ;; somvr_spike1*.so) SPIKE1=$p ;; esac
done
PRELOAD=$(IFS=:; printf '%s' "${PRELOADS[*]}")   # the LD_PRELOAD value (one --preload: its path, as before)
for kv in "${GEXTRA[@]}"; do
  k=${kv%%=*}
  [[ $kv == *=* && $k =~ ^[A-Z_][A-Z0-9_]*$ ]] || bad "--game-env: bad K=V: $kv"
  case $k in HOME|USER|LOGNAME|SHELL|PATH|LANG|DISPLAY|XDG_*|GAMESCOPE_*|ENABLE_GAMESCOPE_WSI|SDL_VIDEODRIVER|SDL_AUDIODRIVER|\
    DBUS_*|SSL_CERT_FILE|LD_PRELOAD|LD_LIBRARY_PATH|LD_AUDIT|SOM_BOOTCHECK_RUN|STEAM_RUNTIME|SOMVR_LOG|SOMVR_TRIGGER)
    bad "--game-env: $k is set by the harness (or by --preload)" ;; esac
  case $k in SOMVR_PROBE_CMD|SOMVR_PROBE_LOG|SOMVR_PROBE_OUT)   # the game's cwd is the install's bin/
    case ${kv#*=} in /?*|@RUN@*) ;; *) bad "--game-env: $k needs an absolute path: $kv" ;; esac ;; esac
done
for a in "${GARGS[@]}"; do
  [[ -n $a && $a != *$'\n'* ]] || bad "--game-arg: an empty argument or one with a newline"
done
PSET_PY=$BCDIR/prefs_set.py; [ -f "$PSET_PY" ] || PSET_PY=$SOM/scripts/prefs_set.py
PSET_RE='^[A-Za-z0-9_ ]+(\\[A-Za-z0-9_ ]+)*=-?[0-9]+$'
for kv in "${PSETS[@]}"; do
  [[ $kv =~ $PSET_RE ]] || bad "--prefs-set: not NAME=INT (letters, digits, _ and blanks; a backslash for a subkey): $kv"
done
[ ${#PSETS[@]} -eq 0 ] || [ -f "$PSET_PY" ] || bad "--prefs-set needs $PSET_PY"
genv() {   # $1 = KEY: print its --game-env value (the last one wins, as with env); status 1 = not given
  local kv v="" st=1
  for kv in "${GEXTRA[@]}"; do [ "${kv%%=*}" = "$1" ] && { v=${kv#*=}; st=0; }; done
  printf '%s' "$v"; return $st
}
cmds_bad() {   # $1 = a --cmds file: print its lines that are not commands (or a probe line without words / too long)
  LC_ALL=C awk '$1 == "" || $1 ~ /^#/ { next }
    $1 !~ /^(shot|key|xdo|mark|wait|waitlog|skipvideos|trigger|untrigger|probe|quit)$/ { print "  line " NR ": " $0; next }
    $1 == "waitlog" && ($2 !~ /^[0-9]+$/ || NF < 3) { print "  line " NR " (waitlog SECS TEXT...): " $0; next }
    $1 == "skipvideos" && ($2 !~ /^[0-9]+$/ || NF < 4) { print "  line " NR " (skipvideos SECS KEY TEXT...): " $0; next }
    $1 == "probe" { w = $2; for (i = 3; i <= NF; i++) w = w " " $i
      if (NF < 2 || length(w) > 511) print "  line " NR " (probe: no words or over 511 bytes): " $0 }' "$1"
}
if [ -n "$CMDS" ]; then
  [ "$AUTO" -eq 0 ] || bad "--cmds needs command mode (not --auto)"
  [ -f "$CMDS" ] && [ -r "$CMDS" ] || bad "--cmds: no readable file $CMDS"
  CMDS=$(realpath -e "$CMDS")
  b=$(cmds_bad "$CMDS")
  [ -z "$b" ] || bad "--cmds: unknown commands in $CMDS:
$b"
fi
[ -n "$BACKUP" ] || BACKUP=$(ls -d "$SOM"/backups/save-* 2>/dev/null | sort | tail -1)

TS=$(date +%Y%m%d-%H%M%S)
if [ $DRY = 1 ]; then RUN=$SOM/logs/boot-check-dryrun-$TS; else RUN=$SOM/logs/boot-check-$TS; fi
GEXTRA=("${GEXTRA[@]//@RUN@/"$RUN"}")   # --game-env values: @RUN@ = this run's folder (quoted: an '&' stays an '&')
THOME=$RUN/home
CMD=$RUN/cmd
mkdir -p "$RUN/shots" || exit 1
LOG=$RUN/run.log
log() { printf '%s +%ss %s\n' "$(date +%T)" "$(( $(date +%s) - T0 ))" "$*" | tee -a "$LOG" >&2; }
die() { log "REFUSED: $*"; exit "${2:-3}"; }
GPRE=()   # --preload: the game's env only (fixed names, the library reads them)
[ -n "$PRELOAD" ] && GPRE=(LD_PRELOAD="$PRELOAD" SOMVR_LOG="$RUN/somvr.log" SOMVR_TRIGGER="$RUN/somvr.trigger")
if [ $SAVECOPY = 1 ]; then
  { [ -n "$PRELOAD" ] && [ $STEAM = 1 ]; } || die "--save-copy needs --steam and --preload"
  GPRE+=(SOMVR_SAVEDIR="$RUN/saves")
fi
PROBE_ENV=()   # the probe library: its three paths under RUN, unless --game-env gives them
if [ -n "$PROBE" ]; then
  for kv in SOMVR_PROBE_CMD="$RUN/probe.cmd" SOMVR_PROBE_LOG="$RUN/probe.log" SOMVR_PROBE_OUT="$RUN/probe-out"; do
    genv "${kv%%=*}" > /dev/null || PROBE_ENV+=("$kv")
  done
  GPRE+=("${PROBE_ENV[@]}")
fi
PROBE_CMDF=$(genv SOMVR_PROBE_CMD) || PROBE_CMDF=$RUN/probe.cmd   # where `probe WORDS` writes
PROBE_LOGF=$(genv SOMVR_PROBE_LOG) || PROBE_LOGF=$RUN/probe.log   # where it looks for the probe's "CMD queued"

# ---- guards ---------------------------------------------------------------------------------------------------
user_in_vr() {
  [ "$(cat /proc/1/comm 2>/dev/null)" = systemd ] || { echo "cannot see the host's processes (sandboxed shell?)"; return 0; }
  ss -xH state established 2>/dev/null | grep -q '/wivrn/comp_ipc' && { echo "a VR app is connected to WiVRn"; return 0; }
  pgrep -x vr-session > /dev/null 2>&1 && { echo "vr-session runs (the user is in VR)"; return 0; }
  local j
  if [ -f "$VRS/status.json" ]; then
    j=$(tr -d ' \n' < "$VRS/status.json" 2>/dev/null)
    [[ $j == *'"user":true'* ]] && { echo "status.json: \"user\": true"; return 0; }
  fi
  return 1
}
other_slot_busy() {   # any other environment's fresh reserved-env* / game-env* (HoF2 env1, env2, ...)
  local f age
  for f in "$VRS"/reserved-env* "$VRS"/game-env*; do
    [ -e "$f" ] || continue
    [ "$f" = "$RES" ] && continue
    age=$(( $(date +%s) - $(stat -c %Y "$f" 2>/dev/null || echo 0) ))
    [ "$age" -le 120 ] && { echo "${f##*/} is fresh (${age} s old)"; return 0; }
  done
  return 1
}
vram_free() { echo $(( $(cat "$VRAM_DEV/mem_info_vram_total") - $(cat "$VRAM_DEV/mem_info_vram_used") )); }
save_check() {   # $1 = label; 0 = byte-identical to the backup
  local out st
  out=$(cd / && sha256sum -c "$BACKUP/SHA256SUMS" 2>&1); st=$?
  printf '%s\n' "$out" | sed "s/^/  save $1: /" | tee -a "$LOG" >&2
  stat -c '%n %s %y' "$SAVEAPP/remote/GameData.sav" "$SAVEAPP/remotecache.vdf" > "$RUN/save-stat-$1.txt" 2>&1
  # size/mtime: GameData.sav only (Steam rewrites remotecache.vdf with the same content whenever it evaluates the app)
  if [ -f "$BACKUP/STAT" ] && [ "$(grep -F '/GameData.sav ' "$BACKUP/STAT")" != "$(grep -F '/GameData.sav ' "$RUN/save-stat-$1.txt")" ]; then
    log "  save $1: GameData.sav size/mtime differ from $BACKUP/STAT"; st=1; fi
  return $st
}

log "boot_check $([ $DRY = 1 ] && echo DRY-RUN) run dir $RUN"
[ -x "$BIN" ] || die "no game binary $BIN"
bid=$(readelf -n "$BIN" 2>/dev/null | sed -n 's/.*Build ID: *//p')
[ "$bid" = "$BUILDID" ] || die "BuildID $bid, expected $BUILDID"
[ -f "$GAME/bin/steam_appid.txt" ] || die "no bin/steam_appid.txt"
for t in gamescope gamescopectl xdotool bwrap magick; do command -v $t > /dev/null || die "missing tool $t"; done
[ -n "$BACKUP" ] && [ -f "$BACKUP/SHA256SUMS" ] || die "no save backup with SHA256SUMS (--backup)"
if [ $HEADSET = 1 ]; then
  printf '%s\n' "${GEXTRA[@]}" | grep -qx 'XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json' ||
    die "--headset needs --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json"
  log "HEADSET session: the user-in-VR guard and the VR monitor are off (now: $(user_in_vr || echo 'not in VR'))"
elif why=$(user_in_vr); then die "user first: $why" 4; fi
if why=$(other_slot_busy); then die "game slot busy: $why"; fi
pgrep -x ShadowOfMordor > /dev/null && die "a ShadowOfMordor process already runs: $(pgrep -x ShadowOfMordor | tr '\n' ' ')"
if [ -r "$VRAM_DEV/mem_info_vram_used" ]; then
  fr=$(vram_free); need=$(awk -v g="$VRAM_MIN_GB" 'BEGIN{printf "%d", g*1073741824}')
  log "VRAM free $((fr / 1048576)) MB (need $((need / 1048576)) MB)"
  [ "$fr" -ge "$need" ] || die "only $((fr / 1048576)) MB VRAM free"
else
  die "VRAM not readable at $VRAM_DEV"
fi
save_check before || die "the real save does not match the backup $BACKUP - not starting"
PREFS_BEFORE=$([ -e "$REALPREFS" ] && echo present || echo absent)
log "real $REALPREFS: $PREFS_BEFORE; steam client: $(pgrep -x steam > /dev/null && echo running || echo 'not running'); mode: $([ $STEAM = 1 ] && echo 'STEAM (the game reaches the Steam client)' || echo isolated)"
[ $STEAM = 1 ] && ! pgrep -x steam > /dev/null && die "--steam: the Steam client is not running"
SDBUS=""
if [ $STEAM = 1 ]; then   # the real session bus for the game (SDL 2.0.8's SDL_DBus_Init crashes when the bus is unreachable)
  SDBUS=${DBUS_SESSION_BUS_ADDRESS:-unix:path=$RT/bus}
  case $SDBUS in unix:path=*) p=${SDBUS#unix:path=}; p=${p%%,*}; [ -S "$p" ] || die "--steam: session D-Bus socket $p missing" ;; esac
  DBUS_SESSION_BUS_ADDRESS=$SDBUS timeout 5 dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
    org.freedesktop.DBus.Peer.Ping > /dev/null 2>&1 || die "--steam: session D-Bus $SDBUS does not answer"
  log "session D-Bus for the game: $SDBUS (answers); system bus socket: $([ -S /run/dbus/system_bus_socket ] && echo present || echo MISSING)"
fi
LACTCTL=/home/bara/Projects/flat2vr/shared/tools/vr-session/lactctl.py
power_state() {   # $1 = label; read-only: CPU governors, gamemoded running?, LACT's active profile (list_profiles only)
  local gov gm lact
  gov=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c | awk '{printf "%s%s x%s", (NR > 1 ? ", " : ""), $2, $1}')
  if pgrep -x gamemoded > /dev/null; then   # property read only while it runs (no D-Bus activation by us)
    gm="running, clients $(timeout 5 busctl --user get-property com.feralinteractive.GameMode /com/feralinteractive/GameMode \
      com.feralinteractive.GameMode ClientCount 2>/dev/null | awk '{print $2}')"
  else gm='not running'; fi
  lact=$(timeout 5 python3 -I "$LACTCTL" '{"command":"list_profiles","args":{"include_state":false}}' 2>/dev/null |
    python3 -I -c 'import json,sys; d=json.load(sys.stdin).get("data") or {}; print("profile %s, auto_switch %s" % (d.get("current_profile") or "Default (null)", d.get("auto_switch")))' 2>/dev/null)
  echo "$(date +%T) $1: governor ${gov:-?}; gamemoded $gm; LACT ${lact:-unreadable}" >> "$RUN/power.log"
  log "power $1: governor ${gov:-?}; gamemoded $gm; LACT ${lact:-unreadable}"
}
feral_real() {   # feral* entries in the REAL home's data/config/cache folders
  find /home/bara/.local/share /home/bara/.config /home/bara/.cache -maxdepth 1 -iname 'feral*' 2>/dev/null | sort
}

# ---- the isolated home: pre-seeded Feral prefs (registry XML; candidate setup keys, the real one shows after the run)
mkdir -p "$THOME/.local/share/feral-interactive/ShadowOfMordor" "$THOME/.config" "$THOME/.cache"
setup_vals() {
  local i=$1 n
  for n in "GameOptionsDialogShown 1" "GameOptionsDialogShouldShow 0" "GameOptionsDialogShouldShowBigPicture 0" \
           "GameOptionsDialogForceDialog 0" "AllowSendUsageData 0" "SoftwareUpdatedAskedUser 1" "SoftwareUpdatedCanCheck 0" \
           "DontShowFeralNetWarningAgain 1" "EULAPresented 1"; do
    printf '%*s<value name="%s" type="integer">%s</value>\n' "$i" '' "${n% *}" "${n#* }"
  done
}
{
  echo '<?xml version="1.0" encoding="UTF-8"?>'
  echo '<registry>'
  echo '  <key name="HKEY_CURRENT_USER">'
  echo '    <key name="DozeGameSetup">'; setup_vals 6; echo '    </key>'
  echo '    <key name="Software">'
  echo '      <key name="Feral Interactive">'
  for prod in "Middle Earth Shadow of Mordor" "Shadow of Mordor" "ShadowOfMordor"; do
    printf '        <key name="%s">\n          <key name="Setup">\n' "$prod"; setup_vals 12; printf '          </key>\n        </key>\n'
  done
  echo '      </key>'
  echo '    </key>'
  echo '  </key>'
  echo '</registry>'
} > "$THOME/.local/share/feral-interactive/ShadowOfMordor/preferences"
if [ -n "$SEED" ]; then cp "$SEED" "$THOME/.local/share/feral-interactive/ShadowOfMordor/preferences" || die "no --seed-prefs $SEED"; fi
if [ ${#PSETS[@]} -gt 0 ]; then   # --prefs-set: the run's own copy only (RUN/home)
  pso=$(python3 -I "$PSET_PY" "$THOME/.local/share/feral-interactive/ShadowOfMordor/preferences" "${PSETS[@]}" 2>&1) ||
    die "--prefs-set failed: $pso"
  printf '%s\n' "$pso" | while IFS= read -r l; do log "prefs (RUN/home only): $l"; done
fi
cp "$THOME/.local/share/feral-interactive/ShadowOfMordor/preferences" "$RUN/preferences.seeded"
printf '#!/bin/sh\necho "$(date +%%T) BLOCKED: $0 $*" >> "%s/blocked.log"\nexit 1\n' "$RUN" > "$RUN/steam-stub"
chmod 755 "$RUN/steam-stub"
mkdir -p "$RUN/stubbin" && ln -sf "$RUN/steam-stub" "$RUN/stubbin/steam" && ln -sf "$RUN/steam-stub" "$RUN/stubbin/xdg-open"
SSL=/etc/pki/tls/certs/ca-bundle.crt; [ -e "$SSL" ] || SSL=/etc/ssl/certs/ca-certificates.crt

build_game_argv() {   # needs GDISP, GWD (and GWSI) set; fills GARGV with the isolated launch
  local envs=(HOME="$THOME" USER="$(id -un)" LOGNAME="$(id -un)" SHELL=/bin/bash PATH=/usr/local/bin:/usr/bin:/bin
    LANG="${LANG:-en_US.UTF-8}" XDG_RUNTIME_DIR="$RT" XDG_DATA_HOME="$THOME/.local/share" XDG_CONFIG_HOME="$THOME/.config"
    XDG_CACHE_HOME="$THOME/.cache" DISPLAY="$GDISP" GAMESCOPE_WAYLAND_DISPLAY="$GWD" SDL_VIDEODRIVER=x11
    SDL_AUDIODRIVER=dummy DBUS_SESSION_BUS_ADDRESS="unix:path=$RUN/no-dbus" SteamAppId=241930 GameAppId=241930
    SSL_CERT_FILE="$SSL" LD_LIBRARY_PATH="$GAME/lib/x86_64" SOM_BOOTCHECK_RUN="$RUN")
  [ -n "${GWSI:-}" ] && envs+=(ENABLE_GAMESCOPE_WSI="$GWSI")
  if [ $STEAM = 1 ]; then
    local se=() e
    for e in "${envs[@]}"; do
      case $e in HOME=*|XDG_CONFIG_HOME=*|XDG_CACHE_HOME=*|PATH=*|DBUS_SESSION_BUS_ADDRESS=*) ;; *) se+=("$e") ;; esac
    done
    if [ $HEADSET = 1 ]; then   # the user plays: the game's sound on (SDL picks PulseAudio, as in a Steam start), not dummy
      for e in "${!se[@]}"; do case ${se[$e]} in SDL_AUDIODRIVER=*) unset 'se[e]' ;; esac; done
      se=("${se[@]}")
    fi
    se+=(DBUS_SESSION_BUS_ADDRESS="$SDBUS")   # the real session bus (checked in the guards)
    # as Steam starts a native title without a container: the scout LD_LIBRARY_PATH runtime (FeralLinuxMessage, the
    # game's alert box, needs its SDL2 / SDL2_ttf / SDL2_image), computed from a clean environment; lib/x86_64 first
    # as in ShadowOfMordor.sh. The parent is ptrace_parent.py (the game makes its parent its tracer: PTRACE_TRACEME).
    local sr=$STEAMDIR/ubuntu12_32/steam-runtime rtp
    rtp=$(env -u LD_LIBRARY_PATH -u LD_PRELOAD "$sr/run.sh" --print-steam-runtime-library-paths 2>/dev/null)
    for e in "${!se[@]}"; do case ${se[$e]} in LD_LIBRARY_PATH=*) se[$e]="LD_LIBRARY_PATH=$GAME/lib/x86_64${rtp:+:$rtp}" ;; esac; done
    se+=(HOME=/home/bara PATH="$RUN/stubbin:/usr/local/bin:/usr/bin:/bin" STEAM_RUNTIME="$sr")
    GARGV=(python3 -I "$SOM/scripts/ptrace_parent.py" "$RUN/ptrace.log" -- /usr/bin/env -i -C "$GAME/bin" "${se[@]}"
      "${GPRE[@]}" "${GEXTRA[@]}" "$BIN" "${GARGS[@]}")
    return 0
  fi
  GARGV=(bwrap --dev-bind / / --unshare-net --die-with-parent
    --ro-bind "$SAVEAPP" "$SAVEAPP"
    --tmpfs /home/bara/.steam
    --ro-bind /dev/null "$STEAMDIR/linux64/steamclient.so"
    --ro-bind "$RUN/steam-stub" "$(readlink -f /usr/bin/steam)"
    --ro-bind "$RUN/steam-stub" "$(readlink -f /usr/bin/xdg-open)"
    --chdir "$GAME/bin"
    -- /usr/bin/env -i "${envs[@]}" "${GPRE[@]}" "${GEXTRA[@]}" "$BIN" "${GARGS[@]}")
}
preload_info() {   # sha256, size, mtime of each preload (several: "NAME: " before each, '; ' between)
  local p s=""
  for p in "${PRELOADS[@]}"; do
    s="$s${s:+; }$([ ${#PRELOADS[@]} -gt 1 ] && printf '%s: ' "${p##*/}")sha256 $(sha256sum < "$p" | cut -c1-64), $(stat -c '%s B, mtime %y' "$p")"
  done
  printf '%s' "$s"
}
launch_record() {   # RUN/launch-cmd.txt: the game's argv, then what goes to the game only
  { printf '%q ' "${GARGV[@]}"; echo
    [ -n "$PRELOAD" ] && echo "# game only: LD_PRELOAD=$PRELOAD ($(preload_info)) SOMVR_LOG=$RUN/somvr.log SOMVR_TRIGGER=$RUN/somvr.trigger$([ $SAVECOPY = 1 ] && echo " SOMVR_SAVEDIR=$RUN/saves (save guard)")$([ ${#PROBE_ENV[@]} -gt 0 ] && printf ' %s' "${PROBE_ENV[@]}")"
    [ ${#GEXTRA[@]} -gt 0 ] && echo "# game only (--game-env):$(printf ' %q' "${GEXTRA[@]}")"
    [ ${#GARGS[@]} -gt 0 ] && echo "# game arguments (--game-arg):$(printf ' %q' "${GARGS[@]}")"
  } > "$RUN/launch-cmd.txt"
  grep -E '^# game (only|arguments)' "$RUN/launch-cmd.txt" | while IFS= read -r l; do log "launch: ${l#\# }"; done
  if [ -n "$PROBE" ]; then
    log "probe: ${PROBE##*/} preloaded; \`probe WORDS\` -> $PROBE_CMDF (tmp + rename), its log $PROBE_LOGF"
    { [ -n "$SPIKE1" ] && [ $SAVECOPY = 1 ]; } || [ "$(genv SOMVR_PROBE_NOGUARD_OK)" = 1 ] ||
      log "!!! probe: NO save guard in this run (needs somvr_spike1*.so in --preload and --save-copy): the probe refuses its active words"
  fi
}

# ---- the game install: listed before the launch, checked after the run (cleanup, also aborts/traps) ---------------
install_list() {   # $1 = before|after -> RUN/install-$1.txt: type, size (not for dirs), mtime, path; sorted by path
  find "$GAME" \( -type d -printf 'd\t-\t%T@\t%p\n' \) -o -printf '%y\t%s\t%T@\t%p\n' 2>/dev/null |
    LC_ALL=C sort -t "$(printf '\t')" -k4,4 > "$RUN/install-$1.txt"
}
IL_PRE=""
install_snapshot() {
  local n
  touch "$RUN/install.stamp"; install_list before
  for n in debug.log blob_storage; do
    if [ -e "$GAME/bin/$n" ] || [ -L "$GAME/bin/$n" ]; then
      IL_PRE="$IL_PRE $n"; log "!!! install: bin/$n exists BEFORE the run: left alone (shows as changed if it changes)"; fi
  done
  log "install: $(wc -l < "$RUN/install-before.txt") entries in install-before.txt, stamp install.stamp; CEF leftovers in bin/ before:${IL_PRE:- none}"
}
install_check() {   # $1 = dry: report only, move nothing
  local n f moved=0 IL=$RUN/install.log
  [ -e "$RUN/install.stamp" ] && [ -s "$RUN/install-before.txt" ] || return 0
  find "$GAME" -newer "$RUN/install.stamp" 2>/dev/null | LC_ALL=C sort > "$RUN/install-newer.txt"
  echo "find -newer install.stamp: $(wc -l < "$RUN/install-newer.txt") entries (install-newer.txt)" >> "$IL"
  for n in debug.log blob_storage; do
    f=$GAME/bin/$n
    { [ -e "$f" ] || [ -L "$f" ]; } || continue
    case " $IL_PRE " in *" $n "*) continue ;; esac
    if [ "${1:-}" = dry ]; then echo "!!! dry run: new $f (not moved)" >> "$IL"; continue; fi
    if mkdir -p "$RUN/install-leftovers" && mv "$f" "$RUN/install-leftovers/$n"; then
      moved=1; echo "moved new $f -> $RUN/install-leftovers/$n" >> "$IL"
    else echo "!!! FAILED to move new $f" >> "$IL"; fi
  done
  install_list after
  LC_ALL=C awk -F '\t' -v bin="$GAME/bin" -v moved=$moved '
    NR == FNR { b[$4] = $1 " " $2 " " $3; next }
    { a[$4] = 1; now = $1 " " $2 " " $3
      if (!($4 in b)) print "!!! NEW " $4 " (" now ")"
      else if (b[$4] != now) {
        if ($4 == bin && $1 == "d" && moved) print "bin/ mtime changed (the moved leftovers): expected"
        else print "!!! CHANGED " $4 " (before " b[$4] ", now " now ")" } }
    END { for (p in b) if (!(p in a)) print "!!! GONE " p }' "$RUN/install-before.txt" "$RUN/install-after.txt" >> "$IL"
  if grep -q '^!!!' "$IL"; then log "!!! THE GAME INSTALL CHANGED (left in place): $(grep -c '^!!!' "$IL") entries, see $IL"
  else log "install: unchanged$([ $moved = 1 ] && echo ', apart from the moved CEF leftovers')"; fi
  sed 's/^/  install: /' "$IL" | head -30 | tee -a "$LOG" >&2
}
install_snapshot

if [ $DRY = 1 ]; then
  GDISP=":N"; GWD="gamescope-N"; GWSI=""
  log "dry run: would reserve $RES, start: gamescope --backend headless -W $W -H $H -w $W -h $H -r $REFRESH, then:"
  build_game_argv; { printf '%q ' "${GARGV[@]}"; echo; } | tee -a "$LOG" >&2
  launch_record
  [ -n "$CMDS" ] && log "dry run: --cmds $CMDS checked ($(grep -c . "$CMDS") lines); would be appended to RUN/cmd at the launch"
  if [ $STEAM = 1 ]; then
    PATH="$RUN/stubbin:/usr/local/bin:/usr/bin:/bin" sh -c 'steam steam://run/241930; xdg-open x' 2>&1
    power_state dry-run
    log "dry run (--steam): real-home feral* entries now: $(feral_real | tr '\n' ' ')"
    log "dry run (--steam): stub test, blocked.log: $(cat "$RUN/blocked.log" 2>/dev/null | tr '\n' ';'); nothing started"
    install_check dry
    exit 0
  fi
  # the namespace itself (no game): the binds work, the save is read-only inside, steamclient.so and ~/.steam are hidden
  GPRE=(); GEXTRA=(); GARGS=(); build_game_argv   # the test shell gets no --preload / --game-env / --game-arg
  GARGV=("${GARGV[@]:0:${#GARGV[@]}-1}")   # drop "$BIN"
  "${GARGV[@]}" /bin/sh -c 'touch "$1/remote/.w" 2>/dev/null && { rm -f "$1/remote/.w"; echo "save dir WRITABLE"; } || echo "save dir read-only";
    [ -s /home/bara/.local/share/Steam/linux64/steamclient.so ] && echo "steamclient.so visible" || echo "steamclient.so hidden";
    ls -A /home/bara/.steam | wc -l | sed "s/^/~\/.steam entries: /"; echo "HOME=$HOME pwd=$(pwd)";
    timeout 5 bash -c "exec 3<>/dev/tcp/1.1.1.1/443" 2>/dev/null && echo "network UP (tcp 1.1.1.1:443)" || echo "network none (tcp 1.1.1.1:443 fails)"; steam x; xdg-open y; true' sh "$SAVEAPP" 2>&1 | sed 's/^/  ns: /' | tee -a "$LOG" >&2
  log "dry run: blocked.log: $(cat "$RUN/blocked.log" 2>/dev/null | tr '\n' ';')"
  log "dry run: seeded prefs $RUN/preferences.seeded ($(grep -c '<value' "$RUN/preferences.seeded") values); nothing started"
  install_check dry
  exit 0
fi

# ---- from here on everything we start is stopped by cleanup -------------------------------------------------
GS_PID=""; GS_CHILD=""; BW_PID=""; GAME_PID=""; KEEPER=""; VRMON=""; WDOG=""; CLEANED=0; GAME_T0=""; GAME_END=""
tree() { local p=$1 c; echo "$p"; for c in $(pgrep -P "$p" 2>/dev/null); do tree "$c"; done; }
alive() { [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null; }
stop_game() {   # $1 = grace seconds for SIGTERM
  local g=${1:-30} pids i
  alive "$BW_PID" || alive "$GAME_PID" || return 0
  pids=$( { tree "$BW_PID"; [ -n "$GAME_PID" ] && tree "$GAME_PID"; } 2>/dev/null | sort -u | tr '\n' ' ')
  if alive "$GAME_PID" && [ -n "$PRELOAD" ] && [ -e "$RUN/somvr.trigger" ] &&
     ! LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null; then
    # the SIGTERM skips somvr.so's destructor: its word `endlogs` writes every feature's END lines (and the VR menu's
    # unsaved change) first; the writer thread reads the trigger file every 0.25 s
    printf 'endlogs' > "$RUN/somvr.trigger"
    for i in $(seq 1 12); do LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null && break; sleep 0.25; done
    if LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null; then log "somvr.so's END lines written (the word endlogs)"
    else log "!!! somvr.so wrote no END lines within 3 s of the word endlogs"; fi
  fi
  if alive "$GAME_PID"; then
    log "stopping the game: SIGTERM $GAME_PID (grace $g s)"
    kill -TERM "$GAME_PID" 2>/dev/null
    for i in $(seq 1 $((g * 2))); do alive "$GAME_PID" || break; sleep 0.5; done
  fi
  pids="$pids $( { tree "$BW_PID"; } 2>/dev/null | tr '\n' ' ')"
  if alive "$GAME_PID"; then log "game still alive after $g s: SIGKILL"; fi
  for i in $pids; do alive "$i" && [ "$i" != "$MAIN" ] && kill -KILL "$i" 2>/dev/null; done
  for i in $(seq 1 20); do alive "$BW_PID" || alive "$GAME_PID" || break; sleep 0.25; done
  [ -n "$GAME_END" ] || GAME_END=$(date +%s)
  if [ ! -f "$RUN/game.exit" ] && [ -n "$BW_PID" ]; then wait "$BW_PID" 2>/dev/null; echo "exit $? (stopped by us)" > "$RUN/game.exit"; fi
}
stop_gamescope() {
  local i
  alive "$GS_PID" || return 0
  [ -n "$GS_CHILD" ] && kill -TERM "$GS_CHILD" 2>/dev/null   # its child ends: gamescope ends with it
  for i in $(seq 1 25); do alive "$GS_PID" || break; sleep 0.2; done
  alive "$GS_PID" && { kill -TERM "$GS_PID" 2>/dev/null; sleep 1; }
  alive "$GS_PID" && kill -KILL "$GS_PID" 2>/dev/null
  log "gamescope stopped (pid $GS_PID)"
}
summary() {
  local S=$RUN/summary.txt st
  {
    echo "Shadow of Mordor flat boot check  $TS  (run dir $RUN)"
    echo "start $(date -d @"$T0" +%T)  end $(date +%T)  total $(( $(date +%s) - T0 )) s"
    [ -f "$RUN/ABORT" ] && echo "ABORTED (user in VR): $(cat "$RUN/ABORT")"
    [ -f "$RUN/TIMEOUT" ] && echo "HARD TIMEOUT: $(cat "$RUN/TIMEOUT")"
    [ -n "$GAME_T0" ] && echo "game pid $GAME_PID ran $(( ${GAME_END:-$(date +%s)} - GAME_T0 )) s; exit: $(cat "$RUN/game.exit" 2>/dev/null || echo '?')"
    grep -q '^!!!' "$RUN/install.log" 2>/dev/null && echo "!!! WARNING: THE GAME INSTALL CHANGED - see '--- game install' below (left in place, nothing deleted)"
    echo "--- marks (game time)"; grep -a ' MARK ' "$LOG" 2>/dev/null | sed 's/^/  /'
    echo "--- game install ($GAME)"; sed 's/^/  /' "$RUN/install.log" 2>/dev/null | head -40
    head -20 "$RUN/install-newer.txt" 2>/dev/null | sed 's/^/  newer than install.stamp (before the leftover move): /'
    if [ -n "$PRELOAD" ]; then
      echo "--- preload (game only)"; grep '^# game only' "$RUN/launch-cmd.txt" 2>/dev/null | sed 's/^# /  /'
      grep -a ' trigger: ' "$LOG" 2>/dev/null | sed 's/^/  /' | tail -10
      echo "  somvr.log: $([ -e "$RUN/somvr.log" ] && echo "$(wc -l < "$RUN/somvr.log") lines, tail:" || echo 'not written')"
      tail -15 "$RUN/somvr.log" 2>/dev/null | cut -c1-200 | sed 's/^/    /'
    fi
    if [ -n "$PROBE" ]; then
      echo "--- probe ($PROBE_LOGF)"; grep -a ' probe: ' "$LOG" 2>/dev/null | sed 's/^/  /' | tail -20
      echo "  probe.log: $([ -e "$PROBE_LOGF" ] && echo "$(wc -l < "$PROBE_LOGF") lines; $(grep -a -c -E '^(FAIL|WARN)' "$PROBE_LOGF") FAIL/WARN; tail:" || echo 'not written')"
      tail -15 "$PROBE_LOGF" 2>/dev/null | cut -c1-200 | sed 's/^/    /'
    fi
    [ -e "$RUN/quit.request" ] && echo "--- the player quit from the VR menu: $(head -c 200 "$RUN/quit.request" | tr '\n' ' ')"
    [ -s "$RUN/marks.log" ] && { echo "--- bug marks (RUN/marks/)"; sed 's/^/  /' "$RUN/marks.log"; }
    echo "--- save"; grep -a '  save ' "$LOG" | sed 's/^[0-9:]* +[0-9]*s *//; s/^ *//'
    echo "real $REALPREFS before: $PREFS_BEFORE, after: $([ -e "$REALPREFS" ] && echo present || echo absent)"
    echo "--- left over"
    for p in $GAME_PID $BW_PID $GS_PID $GS_CHILD $KEEPER $VRMON $WDOG; do alive "$p" && echo "  STILL ALIVE pid $p ($(cat /proc/$p/comm 2>/dev/null))"; done
    echo "  pgrep -x ShadowOfMordor: $(pgrep -x ShadowOfMordor | tr '\n' ' ')  FeralCefSubproc: $(pgrep -x FeralCefSubproc | tr '\n' ' ')"
    echo "  $RES: $([ -e "$RES" ] && echo PRESENT || echo gone)"
    echo "--- Vulkan / GPU (game log, maps)"
    grep -a -i -E 'vulkan|radv|amd|radeon|gpu|driver|device' "$RUN/game.log" 2>/dev/null | head -25 | cut -c1-200 | sed 's/^/  /'
    cat "$RUN/maps-libs.txt" 2>/dev/null | sed 's/^/  maps: /'
    echo "--- errors in the game log"
    grep -a -i -E 'error|fail|fatal|abort|assert|segfault|exception|cannot|unable' "$RUN/game.log" 2>/dev/null | head -30 | cut -c1-200 | sed 's/^/  /'
    echo "--- GameMode (game log) / power (governor, gamemoded, LACT profile)"
    grep -a -i gamemode "$RUN/game.log" 2>/dev/null | head -5 | cut -c1-200 | sed 's/^/  /'
    cat "$RUN/power.log" 2>/dev/null | sed 's/^/  /'
    echo "--- real home: feral* entries new or changed since the launch"
    cat "$RUN/realhome.log" 2>/dev/null | sed 's/^/  /'
    [ -e "$RUN/.t-launch" ] && find /tmp/dumps -type f -newer "$RUN/.t-launch" 2>/dev/null | head -10 | sed 's#^#  new in Steam /tmp/dumps: #'
    echo "--- blocked calls"; cat "$RUN/blocked.log" 2>/dev/null | sed 's/^/  /'
    echo "--- prefs the game wrote ($THOME/.local/share/feral-interactive/ShadowOfMordor)"
    find "$THOME/.local/share/feral-interactive" -maxdepth 4 2>/dev/null | head -30 | sed "s#^$THOME#  ~#"
    echo "--- benchmark files"; find "$THOME" -iname 'Benchmark_*' 2>/dev/null | sed 's/^/  /'
    echo "--- Steam logs about 241930 since the start"
    for f in content_log.txt cloud_log.txt; do
      awk -v t="[$(date -d @"$T0" '+%Y-%m-%d %H:%M:%S')]" '$0 >= t && /241930/' "$STEAMDIR/logs/$f" 2>/dev/null | head -40 | cut -c1-220 | sed "s/^/  $f: /"
    done
    echo "--- screenshots"; ls "$RUN/shots" 2>/dev/null | sed 's/^/  /' | tail -40
    echo "--- gamescope log tail"; tail -5 "$RUN/gamescope.log" 2>/dev/null | cut -c1-200 | sed 's/^/  /'
  } > "$S" 2>&1
  log "summary: $S"
}
fileinfo() { [ -e "$1" ] && echo "$(sha256sum < "$1" | cut -c1-64) $(stat -c '%s B, mtime %y' "$1")" || echo missing; }
steam_after() {   # --steam: wait for Steam's sync, compare with the pre-launch backup, restore GameData.sav only if it changed
  local i f
  log "steam mode: waiting 30 s for Steam to finish any sync"
  for i in $(seq 1 30); do sleep 1; done
  for f in remote/GameData.sav remotecache.vdf; do
    log "  save after ($f): now    $(fileinfo "$SAVEAPP/$f")"
    log "  save after ($f): before $(fileinfo "$PRE/$f")"
  done
  if cmp -s "$PRE/remote/GameData.sav" "$SAVEAPP/remote/GameData.sav"; then
    log "  save after: GameData.sav UNCHANGED (byte-identical to $PRE)"
  else
    log "  save after: GameData.sav CHANGED - keeping a copy in $RUN/changed/, restoring it (only it) from $PRE"
    mkdir -p "$RUN/changed" && cp -p "$SAVEAPP/remote/GameData.sav" "$RUN/changed/GameData.sav" 2>/dev/null
    cp -p "$PRE/remote/GameData.sav" "$SAVEAPP/remote/GameData.sav"
    if cmp -s "$PRE/remote/GameData.sav" "$SAVEAPP/remote/GameData.sav"; then
      log "  save after: RESTORED, verified: $(fileinfo "$SAVEAPP/remote/GameData.sav")"
    else
      log "  save after: RESTORE FAILED: $(fileinfo "$SAVEAPP/remote/GameData.sav")"
    fi
  fi
  cmp -s "$PRE/remotecache.vdf" "$SAVEAPP/remotecache.vdf" && log "  save after: remotecache.vdf unchanged" || {
    log "  save after: remotecache.vdf changed (left alone); diff:"; diff "$PRE/remotecache.vdf" "$SAVEAPP/remotecache.vdf" | sed 's/^/    /' | tee -a "$LOG" >&2; }
  if [ $SAVECOPY = 1 ] && [ -f "$RUN/saves.before.sha256" ]; then   # what the save guard saw
    local g
    g=$(grep -a -m1 -E '^GUARD (on|FAIL)' "$RUN/somvr.log" 2>/dev/null)
    log "  save guard: ${g:-NO GUARD LINE in somvr.log}"
    grep -aq '^exit 3 ' "$RUN/game.exit" 2>/dev/null && log "  save guard: the game exited with status 3 (the guard refused to start it)"
    log "  save guard: calls write $(grep -ac '^SAVE write' "$RUN/somvr.log") read $(grep -ac '^SAVE read' "$RUN/somvr.log") size $(grep -ac '^SAVE size' "$RUN/somvr.log") delete $(grep -ac '^SAVE delete' "$RUN/somvr.log")"
    if sha256sum -c --status "$RUN/saves.before.sha256" 2>/dev/null; then log "  save guard: RUN/saves/GameData.sav unchanged"
    else log "  save guard: RUN/saves/GameData.sav CHANGED (the guard caught the game's writes): $(fileinfo "$RUN/saves/GameData.sav")"; fi
  fi
}
realhome_sweep() {   # --steam (real HOME): feral* entries created during the run are moved into RUN/realhome-moved/
  local f
  [ -e "$RUN/.t-launch" ] || return 0
  feral_real > "$RUN/realhome-feral.after"
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    if grep -qxF "$f" "$RUN/realhome-feral.before" 2>/dev/null; then
      [ -n "$(find "$f" -maxdepth 0 -newer "$RUN/.t-launch" 2>/dev/null)" ] &&
        echo "$f existed before the launch and changed during the run: left alone" >> "$RUN/realhome.log"
      continue
    fi
    if mkdir -p "$RUN/realhome-moved${f%/*}" && mv "$f" "$RUN/realhome-moved$f"; then
      echo "moved new $f -> $RUN/realhome-moved$f" >> "$RUN/realhome.log"
    else
      echo "FAILED to move new $f" >> "$RUN/realhome.log"
    fi
  done < "$RUN/realhome-feral.after"
  [ -s "$RUN/realhome.log" ] && sed 's/^/real home: /' "$RUN/realhome.log" | while IFS= read -r f; do log "$f"; done
  [ -s "$RUN/realhome.log" ] || log "real home: no new feral* entries"
}
cleanup() {
  [ $CLEANED = 1 ] && return
  CLEANED=1
  trap '' INT TERM
  local g=30
  [ -f "$RUN/ABORT" ] && g=2
  [ -n "$VRMON" ] && kill "$VRMON" 2>/dev/null
  [ -n "$WDOG" ] && kill "$WDOG" 2>/dev/null
  [ $STEAM = 1 ] && alive "$GAME_PID" && power_state before-stop
  stop_game "$g"
  stop_gamescope
  install_check
  if [ $STEAM = 1 ]; then realhome_sweep; power_state after-stop; fi
  if [ $STEAM = 1 ] && [ -n "${PRE:-}" ]; then
    steam_after
  elif save_check after; then log "SAVE OK: byte-identical to $BACKUP"; else log "SAVE CHANGED OR UNREADABLE - see $RUN/save-stat-after.txt"; fi
  [ -n "$KEEPER" ] && kill "$KEEPER" 2>/dev/null
  rm -f "$RES"
  summary
}
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap 'cleanup' EXIT

# ---- reserve the slot, then re-check (a HoF2 run.sh may have started in between) ------------------------------
mkdir -p "$VRS"
touch "$RES"
( n=0; while kill -0 "$MAIN" 2>/dev/null; do [ $((n % 10)) -eq 0 ] && touch "$RES"; n=$((n + 1)); sleep 2; done; rm -f "$RES" ) > /dev/null 2>&1 &
KEEPER=$!
sleep 1
if why=$(other_slot_busy); then die "game slot taken meanwhile: $why"; fi
log "slot reserved: $RES (keeper $KEEPER)"
( while kill -0 "$MAIN" 2>/dev/null; do
    if [ $HEADSET = 0 ] && why=$(user_in_vr); then echo "$why" > "$RUN/ABORT"; kill -TERM "$MAIN"; exit 0; fi
    sleep 2
  done ) > /dev/null 2>&1 &
VRMON=$!
( end=$((T0 + HARD - 45)); while kill -0 "$MAIN" 2>/dev/null && [ "$(date +%s)" -lt "$end" ]; do sleep 5; done
  kill -0 "$MAIN" 2>/dev/null && { echo "$HARD s limit" > "$RUN/TIMEOUT"; kill -TERM "$MAIN"; } ) > /dev/null 2>&1 &
WDOG=$!

# ---- the virtual screen ----------------------------------------------------------------------------------------
setsid env -u LD_LIBRARY_PATH -u LD_PRELOAD -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY XDG_RUNTIME_DIR="$RT" SOM_VDISPLAY_OF="$RUN" \
  gamescope --backend headless -W "$W" -H "$H" -w "$W" -h "$H" -r "$REFRESH" -- \
  sh -c 'echo "CHILD_PID=$$" > "$1.tmp" && env >> "$1.tmp" && mv -f "$1.tmp" "$1" && exec tail -f /dev/null' \
  vdisplay "$RUN/vdisplay.env" > "$RUN/gamescope.log" 2>&1 < /dev/null &
GS_PID=$!   # setsid and env exec: this is gamescope
for i in $(seq 1 50); do [ -s "$RUN/vdisplay.env" ] && break; alive "$GS_PID" || break; sleep 0.2; done
[ -s "$RUN/vdisplay.env" ] || { tail -5 "$RUN/gamescope.log" >&2; die "gamescope did not come up within 10 s"; }
envval() { sed -n "s/^$1=//p" "$RUN/vdisplay.env" | head -1; }
GS_CHILD=$(envval CHILD_PID); GDISP=$(envval DISPLAY); GWD=$(envval GAMESCOPE_WAYLAND_DISPLAY); GWSI=$(envval ENABLE_GAMESCOPE_WSI)
[ -n "$GDISP" ] && [ -S "/tmp/.X11-unix/X${GDISP#:}" ] || die "no X display from gamescope (DISPLAY=$GDISP)"
[ -n "$GWD" ] && [ -S "$RT/$GWD" ] || die "no gamescope wayland socket ($GWD)"
log "virtual screen up: gamescope pid $GS_PID ($(cat /proc/$GS_PID/comm 2>/dev/null)), DISPLAY $GDISP, $GWD, ENABLE_GAMESCOPE_WSI=${GWSI:-unset}"

shot() { shot_to "$RUN/shots/$1.png"; }   # shot NAME -> shots/NAME.png
shot_to() {   # shot_to /ABS/FILE.png
  local f=$1 i n
  GAMESCOPE_WAYLAND_DISPLAY="$GWD" XDG_RUNTIME_DIR="$RT" timeout 10 gamescopectl screenshot "$f" >> "$RUN/shots.log" 2>&1
  for i in $(seq 1 30); do [ -s "$f" ] && break; sleep 0.2; done
  if [ ! -s "$f" ]; then   # fallback: gamescope's own default names
    n=$(ls -t /tmp/gamescope*.png /tmp/gamescope*.avif 2>/dev/null | head -1)
    if [ -n "$n" ] && [ "$(stat -c %Y "$n")" -ge $(( $(date +%s) - 15 )) ]; then magick "$n" "$f" 2>> "$RUN/shots.log" && rm -f "$n"; fi
  fi
  [ -s "$f" ] && echo "shot $f" >> "$RUN/shots.log" || { echo "shot FAILED $f" >> "$RUN/shots.log"; return 1; }
}
xkey() { env -u XAUTHORITY DISPLAY="$GDISP" xdotool key --delay 120 "$@" >> "$RUN/input.log" 2>&1; }
libs_snapshot() {
  local p
  [ -n "$GAME_PID" ] || return 0
  { grep -a -o -E '[^ ]*(libvulkan[^ ]*|VkLayer[^ ]*|radeonsi[^ ]*|libdrm_amdgpu[^ ]*)$' "/proc/$GAME_PID/maps" 2>/dev/null | sort -u
    for p in "${PRELOADS[@]}"; do
      grep -q -a -F "$(readlink -f "$p")" "/proc/$GAME_PID/maps" 2>/dev/null && echo "preload mapped: $p" || echo "preload NOT mapped: $p"
    done; } > "$RUN/maps-libs.txt"
}
shot 00-gamescope-only || log "note: no screenshot of the empty screen (gamescope may draw nothing without a client)"

# ---- the game ----------------------------------------------------------------------------------------------------
build_game_argv
launch_record
if [ $STEAM = 1 ]; then   # the pre-launch backup (new folder), checked against --backup
  PRE=$SOM/backups/save-$(date +%Y%m%d-%H%M%S)-prelaunch
  mkdir -p "$PRE/remote" && cp -p "$SAVEAPP/remote/GameData.sav" "$PRE/remote/GameData.sav" && cp -p "$SAVEAPP/remotecache.vdf" "$PRE/remotecache.vdf" \
    || { PRE=""; die "pre-launch backup failed"; }
  ( cd "$PRE" && sha256sum remote/GameData.sav remotecache.vdf > SHA256SUMS.rel ) && sha256sum "$SAVEAPP/remote/GameData.sav" "$SAVEAPP/remotecache.vdf" > "$PRE/SHA256SUMS"
  stat -c '%n %s %y' "$SAVEAPP/remote/GameData.sav" "$SAVEAPP/remotecache.vdf" > "$PRE/STAT"
  if ! cmp -s "$PRE/remote/GameData.sav" "$BACKUP/remote/GameData.sav" || ! cmp -s "$PRE/remotecache.vdf" "$BACKUP/remotecache.vdf"; then
    P2=$PRE; PRE=""; die "the save changed since $BACKUP (pre-launch copy $P2) - not starting"
  fi
  log "pre-launch backup $PRE: identical to $BACKUP ($(cut -c1-16 "$PRE/SHA256SUMS" | tr '\n' ' '))"
  if [ $SAVECOPY = 1 ]; then   # the save guard's folder: a copy of the pre-launch backup
    mkdir -p "$RUN/saves" && cp -p "$PRE/remote/GameData.sav" "$RUN/saves/GameData.sav" || die "save copy failed"
    sha256sum "$RUN/saves/GameData.sav" > "$RUN/saves.before.sha256"
    log "save guard: $RUN/saves/GameData.sav = copy of $PRE ($(cut -c1-16 "$RUN/saves.before.sha256")); SOMVR_SAVEDIR set for the game"
  fi
  feral_real > "$RUN/realhome-feral.before"
  log "real-home feral* entries before the launch: $(tr '\n' ' ' < "$RUN/realhome-feral.before")"
  power_state pre-launch
  touch "$RUN/.t-launch"
fi
if [ -n "$CMDS" ]; then   # (a missing final newline is added: `read` would drop that line)
  { cat "$CMDS"; [ -n "$(tail -c 1 "$CMDS")" ] && echo; } >> "$CMD"
  log "commands from $CMDS ($(grep -c . "$CMDS") lines) appended to $CMD"
fi
setsid "${GARGV[@]}" > "$RUN/game.log" 2>&1 < /dev/null &
BW_PID=$!   # setsid execs (not a group leader here): this is bwrap, a direct child (--die-with-parent = this script)
for i in $(seq 1 40); do
  for p in $(tree "$BW_PID" 2>/dev/null); do [ "$(cat /proc/$p/comm 2>/dev/null)" = ShadowOfMordor ] && { GAME_PID=$p; break; }; done
  [ -n "$GAME_PID" ] && break
  alive "$BW_PID" || break
  sleep 0.25
done
[ -n "$GAME_PID" ] || { tail -20 "$RUN/game.log" >&2; die "the game process did not appear (bwrap $BW_PID)"; }
GAME_T0=$(date +%s)
log "game started: bwrap $BW_PID, ShadowOfMordor $GAME_PID; commands -> $CMD"

# ---- watch: auto screenshots, commands, until quit / exit / limits ----------------------------------------------
QUIT=0; next=0; snap=0; pnext=20
gel() { echo $(( $(date +%s) - GAME_T0 )); }
# ---- the player's requests (mod/somvr/somvr_session.h): the VR menu's Quit game, the bug marks ---------------------
WL_POS=0; MARKS_N=0; PLAYER_QUIT=0
VOICE_S=$(genv SOMVR_MARK_VOICE_S) || VOICE_S=9
{ [[ $VOICE_S =~ ^[0-9]+$ ]] && [ "$VOICE_S" -ge 1 ] && [ "$VOICE_S" -le 30 ]; } || VOICE_S=9
XRLOG=$(genv SOMVR_XR_LOG) || XRLOG=$RUN/xr-layer.log
mic_source() {   # WiVRn's microphone: the first PipeWire source with "wivrn" in its name (not a monitor); never another
  timeout 5 pactl list short sources 2>/dev/null | awk '$2 ~ /[Ww][Ii][Vv][Rr][Nn]/ && $2 !~ /\.monitor$/ { print $2; exit }'
}
mark_serve() {   # $1 = RUN/marks/<dir> with a new `want`: the flat picture and the log tails now; the voice and the
                 # pictures' JPEGs in a separate process (nice 19, one CPU) that ends by itself (at most VOICE_S + 25 s)
  local d=$1 src head
  mv -f "$d/want" "$d/served" 2>/dev/null || return 0
  MARKS_N=$((MARKS_N + 1))
  shot_to "$d/flat.png" || echo "no flat picture (gamescope's screenshot failed)" > "$d/flat.txt"
  tail -n 400 "$RUN/somvr.log" > "$d/somvr_tail.txt" 2>/dev/null
  tail -n 200 "$XRLOG" > "$d/xr_tail.txt" 2>/dev/null
  src=$(mic_source)
  head=$(head -1 "$d/state.txt" 2>/dev/null | cut -c1-120)
  log "bug mark $MARKS_N: $d (${head:-no state.txt}); voice: ${src:+$VOICE_S s from }${src:-none (no WiVRn microphone source: is its microphone on?)}"
  echo "$(date +%T) ${d##*/} ${head:-?} | voice: ${src:+recording }${src:-none}" >> "$RUN/marks.log"
  ( if [ -n "$src" ]; then
      timeout -s INT "$VOICE_S" parecord -d "$src" --channels=1 --rate=48000 --format=s16le --file-format=wav \
        "$d/voice.wav" 2> "$d/voice.err"
      sz=$(stat -c %s "$d/voice.wav" 2>/dev/null || echo 0)
      if [ "$sz" -gt 1000 ]; then echo "$(awk -v b="$sz" 'BEGIN { printf "%.1f", (b - 44) / 96000 }') s from $src" > "$d/voice.txt"
      else echo "no voice: $src gave $sz bytes ($(head -c 200 "$d/voice.err" 2>/dev/null | tr '\n' ' '))" > "$d/voice.txt"; fi
      [ -s "$d/voice.err" ] || rm -f "$d/voice.err"
    else
      echo "no voice: no WiVRn microphone source (turn the microphone on in the WiVRn app)" > "$d/voice.txt"
    fi
    for i in $(seq 1 20); do [ -s "$d/eyes.ppm" ] && break; sleep 0.5; done
    if [ -s "$d/eyes.ppm" ]; then
      sleep 0.5   # (the layer writes eyes.ppm.part and renames it when done)
      nice -n 19 taskset -c 15 magick "$d/eyes.ppm" -quality 85 "$d/eyes.jpg" 2>> "$d/convert.err" && rm -f "$d/eyes.ppm"
    else
      echo "no eyes picture from the layer within 10 s (no XR frame, or SOMVR_MARK off in the layer)" > "$d/eyes.txt"
    fi
    [ -s "$d/flat.png" ] && nice -n 19 taskset -c 15 magick "$d/flat.png" -quality 85 "$d/flat.jpg" 2>> "$d/convert.err" &&
      rm -f "$d/flat.png"
    [ -s "$d/convert.err" ] || rm -f "$d/convert.err"
  ) > /dev/null 2>&1 < /dev/null &
}
svc() {   # every 0.5 s (the main loop, `wait`, `waitlog`): the player's Quit, new bug marks
  local w
  if [ $PLAYER_QUIT = 0 ] && [ -e "$RUN/quit.request" ]; then
    log "QUIT: the player asked to quit ($(head -c 200 "$RUN/quit.request" 2>/dev/null | tr '\n' ' ')): stopping the game"
    QUIT=1; PLAYER_QUIT=1
  fi
  for w in "$RUN"/marks/*/want; do [ -e "$w" ] && mark_serve "${w%/want}"; done
}
skip_videos() {   # SECS KEY TEXT...: somvr.log from waitlog's cursor: each new "SESSION ui" line with video=1 -> KEY;
                  # ends at a line with TEXT (the cursor past it) or after SECS (see the header)
  local secs=$1 key=$2 text line n=0 end; shift 2
  text=$*; [ "$secs" -le 300 ] || secs=300
  end=$(( $(date +%s) + secs ))
  while :; do
    while IFS= read -r line; do   # complete lines only (a last line without its newline waits for the next round)
      WL_POS=$(LC_ALL=C; echo $((WL_POS + ${#line} + 1)))
      if [[ $line == *"$text"* ]]; then
        log "skipvideos: '$text' at game time $(gel) s after $n key presses: $(printf '%s' "$line" | cut -c1-120)"; return 0
      fi
      if [[ $line == "SESSION ui "* && $line == *" video=1 "* ]]; then
        xkey "$key"; n=$((n + 1)); log "skipvideos: a video at game time $(gel) s: key $key ($n)"
      fi
    done < <(tail -c +$((WL_POS + 1)) "$RUN/somvr.log" 2>/dev/null)
    if [ "$(date +%s)" -ge "$end" ]; then log "!!! skipvideos: no '$text' within $secs s ($n key presses); going on"; return 1; fi
    alive "$GAME_PID" || return 1
    sleep 0.25; svc; [ $QUIT = 1 ] && return 1
  done
}
sleep_svc() {   # SECS: sleep in 0.5 s steps, serving the player's requests; a Quit ends it
  local i
  for i in $(seq 1 $(( $1 * 2 ))); do sleep 0.5; svc; [ $QUIT = 1 ] && return 0; alive "$GAME_PID" || return 0; done
}
wait_log() {   # SECS TEXT...: somvr.log, from the last match on (see the header)
  local secs=$1 text hit line end; shift
  text=$*; [ "$secs" -le 300 ] || secs=300
  end=$(( $(date +%s) + secs ))
  while :; do
    hit=$(tail -c +$((WL_POS + 1)) "$RUN/somvr.log" 2>/dev/null | LC_ALL=C grep -a -b -F -m1 -- "$text")
    if [ -n "$hit" ]; then
      line=${hit#*:}
      WL_POS=$(LC_ALL=C; echo $((WL_POS + ${hit%%:*} + ${#line} + 1)))
      log "waitlog: '$text' at game time $(gel) s: $(printf '%s' "$line" | cut -c1-160)"
      return 0
    fi
    if [ "$(date +%s)" -ge "$end" ]; then log "!!! waitlog: no '$text' in somvr.log within $secs s (game time $(gel) s); going on"; return 1; fi
    alive "$GAME_PID" || return 1
    sleep 0.5; svc; [ $QUIT = 1 ] && return 1
  done
}
probe_send() {   # `probe WORDS...`: WORDS as one line into the probe's command file (a tmp file in the same folder, then
                 # rename), then up to 2 s for the probe's "CMD queued" line: it reads only the versions it sees (100 ms polls)
  local line=$* f=$PROBE_CMDF n off=0 i qs=""
  n=$(printf '%s' "$line" | wc -c)
  [ -n "$line" ] || { log "probe: no words"; return 0; }
  [ "$n" -le 511 ] || { log "probe: NOT sent, $n bytes (the probe keeps 511): $line"; return 0; }
  [ -n "$PROBE" ] && off=$(stat -c %s "$PROBE_LOGF" 2>/dev/null || echo 0)
  if ! { printf '%s\n' "$line" > "$f.tmp" && mv -f "$f.tmp" "$f"; }; then
    rm -f -- "$f.tmp"; log "probe: FAILED to write $f: $line"; return 0
  fi
  if [ -z "$PROBE" ]; then log "probe: $line -> $f at game time $(gel) s (no probe library in --preload: nobody reads it)"; return 0; fi
  for i in $(seq 1 20); do
    qs=$(tail -c +$((off + 1)) "$PROBE_LOGF" 2>/dev/null | L="'$line' t=" awk 'index($0, "CMD queued seq=") == 1 && index($0, ENVIRON["L"]) {
      sub(/^CMD queued seq=/, ""); print $1; exit }')
    [ -n "$qs" ] && break
    sleep 0.1
  done
  if [ -n "$qs" ]; then log "probe: $line -> $f at game time $(gel) s: queued by the probe (seq $qs)"
  else log "probe: $line -> $f at game time $(gel) s: !!! no \"CMD queued\" in $PROBE_LOGF within 2 s (the next probe command may replace it unread)"; fi
}
run_cmd() {
  local c=$1; shift
  case $c in
    shot) shot "${1:-manual-$(gel)s}" ;;
    key) xkey "$@" ;;
    xdo) env -u XAUTHORITY DISPLAY="$GDISP" xdotool "$@" >> "$RUN/input.log" 2>&1 ;;
    mark) log "MARK $* at game time $(gel) s"; libs_snapshot; [ $STEAM = 1 ] && power_state "mark $*" ;;
    wait) sleep_svc "$(( ${1:-1} > 30 ? 30 : ${1:-1} ))" ;;
    waitlog) wait_log "$@" ;;
    skipvideos) skip_videos "$@" ;;
    trigger) case ${1:-} in ''|view|vp|sbs|sbs0|sbsB|sbsB0|blur|recenter|turn+|turn-) ;;
        *) log "trigger: bad word '$1' (view|vp|sbs|sbs0|sbsB|sbsB0|blur|recenter|turn+|turn-)"; return ;; esac
      local once=""   # somvr's one-shot words act only on a file that already stands
      case ${1:-} in recenter|turn+|turn-) [ -e "$RUN/somvr.trigger" ] || once=" (a new file: stereo on, '$1' does not act)" ;; esac
      printf '%s' "${1:-}" > "$RUN/somvr.trigger"
      log "trigger: $RUN/somvr.trigger${1:+ ($1)} at game time $(gel) s$once$([ -z "$PRELOAD" ] && echo ' (no --preload: nobody reads it)')" ;;
    untrigger) rm -f -- "$RUN/somvr.trigger"; log "untrigger: $RUN/somvr.trigger removed at game time $(gel) s" ;;
    probe) probe_send "$@" ;;
    quit) QUIT=1 ;;
    ''|'#'*) ;;
    *) log "unknown command: $c $*" ;;
  esac
}
while :; do
  el=$(gel)
  if ! alive "$GAME_PID"; then
    GAME_END=$(date +%s); for i in $(seq 1 20); do alive "$BW_PID" || break; sleep 0.25; done
    wait "$BW_PID" 2>/dev/null; echo "exit $? (by itself)" > "$RUN/game.exit"
    log "the game exited by itself after $el s ($(cat "$RUN/game.exit"))"; break
  fi
  [ -f "$RUN/ABORT" ] && { log "ABORT: $(cat "$RUN/ABORT")"; break; }
  [ $(( $(date +%s) - T0 )) -ge $((HARD - 130)) ] && { log "near the hard limit: quitting"; break; }
  [ "$AUTO" -gt 0 ] && [ "$el" -ge "$AUTO" ] && { shot "auto-final-${el}s"; log "auto mode: $AUTO s reached"; break; }
  [ "$AUTO" -eq 0 ] && [ "$el" -ge "$HOLD" ] && { shot "hold-final-${el}s"; log "hold limit $HOLD s reached"; break; }
  if [ "$el" -ge "$next" ]; then
    shot "$(printf 'auto-%04ds' "$el")"
    if [ "$el" -lt 240 ]; then next=$((el + 10)); else next=$((el + 60)); fi
  fi
  [ $snap = 0 ] && [ "$el" -ge 60 ] && { libs_snapshot; snap=1; }
  [ $STEAM = 1 ] && [ "$el" -ge "$pnext" ] && { power_state "game+${el}s"; pnext=$((el + 60)); }
  if [ "$AUTO" -eq 0 ] && [ -s "$CMD" ]; then
    mv -f "$CMD" "$CMD.work"
    while read -r line; do
      [ -f "$RUN/ABORT" ] && break
      alive "$GAME_PID" || { log "the game is gone: the rest of this batch is dropped"; break; }
      log "cmd: $line"; echo "$line" >> "$CMD.done"
      read -r -a w <<< "$line"; [ ${#w[@]} -gt 0 ] && run_cmd "${w[@]}"
      svc
      [ $QUIT = 1 ] && break
    done < "$CMD.work"
    rm -f "$CMD.work"
  fi
  svc
  [ $QUIT = 1 ] && { log "quit requested$([ $PLAYER_QUIT = 1 ] && echo ' (by the player)')"; break; }
  sleep 0.5
done
exit 0   # the EXIT trap stops the game and gamescope, checks the save, removes the reservation, writes the summary
