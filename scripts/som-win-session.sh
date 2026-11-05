#!/bin/bash
# som-win-session.sh - one VR session of the WINDOWS (GOG) build under Proton's wine: the Windows side of
# boot_check.sh --headset, run by scripts/som-vr-headset-win.sh in the systemd user unit somvr-headset (docs/re-winport.md
# 7.10). Never started by hand for the user; tests run it through the launcher (SOMVR_HEADSET_TEST=1).
#
#   som-win-session.sh --run DIR --build DIR [--prefix DIR] [--runtime wivrn|monado] [--sink default|null]
#                      [--cmds FILE] [--hold S] [--hard S] [--refresh HZ] [--size WxH] [--ending-file F]
#                      [--render-set NAME=0..5]... [--game-env K=V]... [--test [--test-save FILE]] [--dry-run]
#   --test-save FILE (tests only, never the play prefix; som-b37-practrip): the prefix's save replaced by a copy of
#     FILE (only read) at every start - each test starts from the same place (the launcher's SOMVR_HEADSET_TEST_SAVE)
#
# What it does (each step in DIR/run.log, the same lines somvr_window.py reads):
#   - the build: DIR/ShadowOfMordor/mod/somvr/build/win/xinput9_1_0.dll (the game side) and .../vklayer (the Linux
#     Vulkan layer, VK_LAYER_PATH); its Proton: BUILD/proton (a frozen copy) else Steam's Proton - Experimental
#   - the game: the GOG install (/home/bara/Games/Heroic/Shadow of Mordor, the exe sha256 f5d72578... = the tables' exe),
#     run IN PLACE as an overlay whose writes land in DIR/game-upper (the GOG folder is never written), on Proton's own
#     wine directly (never `proton run`: no steam.exe, no Steam at all - the GOG build adds no Steam hours), in a
#     headless gamescope at REFRESH Hz (the headset's: 90), DXVK from the prefix's system32, the layer -> OpenXR
#   - the prefix (default logs/win-play/prefix: the user's Windows play prefix, ours alone, never the Heroic prefix):
#     made once as a copy of logs/winport/prefix (Proton-made, DXVK in system32) WITHOUT its Saves; the DLL into its
#     system32 before every session (WINEDLLOVERRIDES xinput9_1_0=n,b)
#   - the save: the prefix's own Documents\WB Games\Shadow of Mordor\Saves\GameData.sav, the session plays on it (the
#     progress stays). None there: seeded from the newest backup (backups/win-save/*/GameData.sav) if one exists, else
#     from a COPY of the user's Linux/Steam save (userdata/*/241930/remote/GameData.sav, only read). A backup before each
#     session (backups/win-save/<ts>-before/, SHA256SUMS) and after it (<ts>-after/, build 34: the newest seeds a lost
#     prefix), its sha after; the Steam save's sha before and after ("SAVE OK" = unchanged). Not the play prefix (a test's): the backup goes to DIR/save-backup and no backup seeds it.
#   - render.cfg in the prefix's Documents\WB Games\Shadow of Mordor: the PC's detected settings (boot_check's set,
#     rating 10) with --render-set over them (the VR graphics preset), written before each session; the old file kept
#     as DIR/render.cfg.before
#   - sound (--sink default, the user's session): winepulse only (winealsa/wineoss off) to the PC's default output -
#     the vr-session watcher (shared/tools/vr-session) makes WiVRn's sink the default while the user is in VR, as for
#     the Linux sessions; 30 s after the start the game's stream is checked once and moved to WiVRn's sink if it is not
#     on it (the watcher off); then logged every 5 minutes. (--sink null, tests only: a null sink of its own, PULSE_SINK
#     to it, /dev/snd hidden; every 0.25 s the game's streams are checked: one on any other sink is muted at once and
#     the session stopped - "!!! AUDIO GUARD")
#   - the slot: $VRS/reserved-envsom held (touched every 20 s; boot_check's rule), refused while another environment's
#     marker is fresh; >= 4 GB free VRAM
#   - the start sequence --cmds (boot_check's language, a subset: shot key xdo mark wait waitlog trigger untrigger quit;
#     somvr-vr6.cmd: the main menu's Continue and the story), the player's requests every 0.5 s (SOM_BOOTCHECK_RUN=DIR:
#     the VR menu's Quit game -> DIR/quit.request; bug marks DIR/marks/<id>/want -> the flat picture, the log tails,
#     voice.wav from WiVRn's microphone through voice_rec.py beside this script, the JPEGs), the game's end watched (it
#     exits; still alive 10 s after the layer's exit handler; no frame for 60 s once it showed some; a Wine crash)
#   - the end (the game quits, Quit game, SIGTERM from `systemctl --user stop`, --hold, --hard): the word `endlogs`
#     (the END lines), the save left quiet (no write for 2 s, at most 10 s), wineserver -k, the processes' tree
#     killed, gamescope stopped, the save lines, summary.txt, the marks' pictures with the layer (mark_compose.sh)
#   --runtime wivrn (default): XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json, wivrn-server must run.
#   --runtime monado (only with --test): Monado's null compositor as headless_win.sh starts it; the caller holds the
#     game slot (/tmp/somvr-boot.lock); refused while the user is in VR (status.json) or a monado-service runs.
#   --dry-run: every check and the paths, the prefix and the save as they would be made (nothing copied or written but
#     DIR's files), the game's command line and environment in DIR/launch-cmd.txt; starts nothing.
# Run it OUTSIDE the Bash sandbox.
set -uo pipefail
chrt -o -p 0 $$ 2>/dev/null || true   # never inherit keyd's SCHED_FIFO (memory: keyd real-time inheritance)
unset LD_PRELOAD LD_LIBRARY_PATH
export PATH=/usr/local/bin:/usr/bin:/bin

SOM=$HOME/Projects/flat2vr/ShadowOfMordor
GAME_DIR="/home/bara/Games/Heroic/Shadow of Mordor"
EXE_SHA=f5d72578f7e5fcf05e6d15d8bd231f763078620c8545f8464bb186e842279455   # the analysed Steam-Win exe = the GOG exe
TEMPLATE=$SOM/logs/winport/prefix
PLAY_PFX=$SOM/logs/win-play/prefix
BACKUPS=$SOM/backups/win-save
STEAM=$HOME/.local/share/Steam
RT=/run/user/$(id -u); VRS=$RT/vr-session; RES=$VRS/reserved-envsom
VRAM_DEV=/sys/bus/pci/devices/0000:0d:00.0
MAIN=$$; T0=$(date +%s)
HERE=$(cd "$(dirname "$0")" 2>/dev/null && pwd)

RUN=""; BUILD=""; PFX=$PLAY_PFX; RUNTIME=wivrn; SINK=default; CMDS=""; HOLD=14200; HARD=14400; REFRESH=90
SIZE=1920x1080; ENDING=""; TEST=0; DRY=0; RSETS=(); GEXTRA=(); TSAVE=""
bad() { echo "som-win-session: $*" >&2; exit 2; }
while [ $# -gt 0 ]; do
  case $1 in
    --run) RUN=${2:?}; shift 2 ;;
    --build) BUILD=${2:?}; shift 2 ;;
    --prefix) PFX=${2:?}; shift 2 ;;
    --runtime) RUNTIME=${2:?}; shift 2 ;;
    --sink) SINK=${2:?}; shift 2 ;;
    --cmds) CMDS=${2:?}; shift 2 ;;
    --hold) HOLD=${2:?}; shift 2 ;;
    --hard) HARD=${2:?}; shift 2 ;;
    --refresh) REFRESH=${2:?}; shift 2 ;;
    --size) SIZE=${2:?}; shift 2 ;;
    --ending-file) ENDING=${2:?}; shift 2 ;;
    --render-set) RSETS+=("${2:?}"); shift 2 ;;
    --game-env) GEXTRA+=("${2:?}"); shift 2 ;;
    --test) TEST=1; shift ;;
    --test-save) TSAVE=${2:?}; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) awk 'NR > 1 && /^set -uo/ { exit } NR > 1' "$0"; exit 0 ;;
    *) bad "unknown argument: $1" ;;
  esac
done
case $RUN in /*) ;; *) bad "--run needs an absolute folder" ;; esac
case $BUILD in /*) ;; *) bad "--build needs an absolute folder" ;; esac
case $PFX in /*) ;; *) bad "--prefix needs an absolute folder" ;; esac
case $PFX in *Heroic*|*/compatdata/*|*/Games/*) bad "--prefix $PFX looks like the user's own prefix" ;; esac
case $RUNTIME in wivrn) ;; monado) [ $TEST = 1 ] || bad "--runtime monado is for tests (--test)" ;; *) bad "--runtime $RUNTIME" ;; esac
case $SINK in default) ;; null) [ $TEST = 1 ] || bad "--sink null is for tests (--test)" ;; *) bad "--sink $SINK" ;; esac
[ $TEST = 1 ] && [ "$RUNTIME" = monado ] && [ "$SINK" != null ] && bad "a test on Monado needs --sink null (no sound to a real output)"
[ $TEST = 1 ] && [ "$PFX" = "$PLAY_PFX" ] && bad "a test never runs on the play prefix (--prefix)"
if [ -n "$TSAVE" ]; then
  [ $TEST = 1 ] || bad "--test-save is for tests (--test)"
  [ "$PFX" = "$PLAY_PFX" ] && bad "--test-save never on the play prefix"
  case $TSAVE in /*) ;; *) bad "--test-save needs an absolute file" ;; esac
  [ -f "$TSAVE" ] || bad "--test-save $TSAVE: no such file"
fi
[[ $HARD =~ ^[0-9]+$ ]] && [ "$HARD" -ge 300 ] && [ "$HARD" -le 14400 ] || bad "--hard $HARD (300..14400)"
[[ $HOLD =~ ^[0-9]+$ ]] || bad "--hold $HOLD"
[[ $REFRESH =~ ^[0-9]+$ ]] && [ "$REFRESH" -ge 20 ] && [ "$REFRESH" -le 144 ] || bad "--refresh $REFRESH"
case $SIZE in [0-9]*x[0-9]*) W=${SIZE%x*}; H=${SIZE#*x} ;; *) bad "--size $SIZE" ;; esac
# (b37 looks: FrameRateLimit too - the game's limiter: 0 = 30 fps, 1 = 60, 2 = 100, 3 = none; re-winport.md 7.15)
RSET_NAMES=" VegetationQuality ShadowQuality MeshQuality LightingQuality AmbientOcclusionQuality TessellationToggle TextureQuality TextureFiltering OITToggle FrameRateLimit "
for kv in "${RSETS[@]}"; do
  [[ $kv =~ ^([A-Za-z]+)=([0-5])$ && $RSET_NAMES == *" ${BASH_REMATCH[1]} "* ]] || bad "--render-set: $kv"
done
for kv in "${GEXTRA[@]}"; do
  k=${kv%%=*}
  [[ $kv == *=* && $k =~ ^[A-Z_][A-Z0-9_]*$ ]] || bad "--game-env: bad K=V: $kv"
  case $k in WINE*|PULSE_*|DISPLAY|WAYLAND_DISPLAY|XDG_*|LD_*|SOM_BOOTCHECK_RUN|SOMVR_LOG|SOMVR_TRIGGER|SOMVR_BRIDGE|\
    DXVK_LOG_PATH|DXVK_STATE_CACHE_PATH|PATH|HOME) bad "--game-env: $k is the session's own" ;; esac
done
genv() { local kv v="" st=1; for kv in "${GEXTRA[@]}"; do [ "${kv%%=*}" = "$1" ] && { v=${kv#*=}; st=0; }; done; printf '%s' "$v"; return $st; }
if [ -n "$CMDS" ]; then
  [ -r "$CMDS" ] || bad "--cmds: no readable $CMDS"
  b=$(LC_ALL=C awk '$1 == "" || $1 ~ /^#/ { next }
    $1 !~ /^(shot|key|xdo|mark|wait|waitlog|trigger|untrigger|quit)$/ { print "  line " NR ": " $0; next }
    $1 == "waitlog" && ($2 !~ /^[0-9]+$/ || NF < 3) { print "  line " NR " (waitlog SECS TEXT...): " $0 }' "$CMDS")
  [ -z "$b" ] || bad "--cmds: unknown commands in $CMDS:
$b"
fi
mkdir -p "$RUN/shots" || exit 1
GEXTRA=("${GEXTRA[@]//@RUN@/"$RUN"}")
LOG=$RUN/run.log
log() { printf '%s +%ss %s\n' "$(date +%T)" "$(( $(date +%s) - T0 ))" "$*" | tee -a "$LOG" >&2; }
die() { log "REFUSED: $*"; exit "${2:-3}"; }
alive() { [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null; }
tree() { local p=$1 c; echo "$p"; for c in $(pgrep -P "$p" 2>/dev/null); do tree "$c"; done; }
win_game() {   # the Windows game's processes: a command line that is the exe's Windows path (Wine's; the game names its
               # main thread PrimaryEngine, so not by name)
  pgrep -f '^[A-Za-z]:\\.*\\ShadowOfMordor\.exe' | tr '\n' ' '
}
sha() { sha256sum < "$1" 2>/dev/null | cut -c1-64; }

log "som-win-session $([ $DRY = 1 ] && echo 'DRY-RUN ')$([ $TEST = 1 ] && echo 'TEST ')run dir $RUN (runtime $RUNTIME, sound $SINK, prefix $PFX)"

# ---- the build, Proton, the game ------------------------------------------------------------------------------
DLL=$BUILD/ShadowOfMordor/mod/somvr/build/win/xinput9_1_0.dll
LAYER=$BUILD/ShadowOfMordor/mod/somvr/vklayer
[ -f "$DLL" ] || die "no DLL $DLL"
[ -f "$LAYER/VkLayer_somvr_openxr.json" ] && [ -f "$LAYER/build/libVkLayer_somvr_openxr.so" ] || die "no layer in $LAYER"
if [ -x "$BUILD/proton/bin/wine" ]; then PFILES=$BUILD/proton
else PFILES="$STEAM/steamapps/common/Proton - Experimental/files"; fi
WINE=$PFILES/bin/wine; WSRV=$PFILES/bin/wineserver
[ -x "$WINE" ] && [ -x "$WSRV" ] || die "no Proton wine at $PFILES"
PVER=$(cat "$BUILD/proton.version" 2>/dev/null || cat "$(dirname "$PFILES")/version" 2>/dev/null)
log "build $BUILD: DLL $(sha "$DLL" | cut -c1-16), layer $(sha "$LAYER/build/libVkLayer_somvr_openxr.so" | cut -c1-16); Proton $PFILES (${PVER:-version ?})"
EXE=$GAME_DIR/x64/ShadowOfMordor.exe
[ -f "$EXE" ] || die "no game at $EXE (the GOG install)"
es=$(sha "$EXE"); [ "$es" = "$EXE_SHA" ] || die "the GOG exe changed (sha256 ${es:0:16}, the build knows ${EXE_SHA:0:16}): the VR build does not know this version"
log "game $EXE (sha256 ${es:0:16}: the tables' exe)"
for t in gamescope gamescopectl xdotool bwrap pactl magick; do command -v $t > /dev/null || die "missing tool $t"; done

# ---- guards ---------------------------------------------------------------------------------------------------
other_slot_busy() {
  local f age
  for f in "$VRS"/reserved-env* "$VRS"/game-env*; do
    [ -e "$f" ] || continue; [ "$f" = "$RES" ] && continue
    age=$(( $(date +%s) - $(stat -c %Y "$f" 2>/dev/null || echo 0) ))
    [ "$age" -le 120 ] && { echo "${f##*/} is fresh (${age} s old)"; return 0; }
  done
  return 1
}
if why=$(other_slot_busy); then die "game slot busy: $why"; fi
pgrep -x ShadowOfMordor > /dev/null && die "the Linux game runs (pid $(pgrep -x ShadowOfMordor | tr '\n' ' '))"
[ -n "$(win_game)" ] && die "a Windows game process runs already (pid $(win_game))"
if [ -r "$VRAM_DEV/mem_info_vram_used" ]; then
  fr=$(( $(cat "$VRAM_DEV/mem_info_vram_total") - $(cat "$VRAM_DEV/mem_info_vram_used") ))
  log "VRAM free $((fr / 1048576)) MB (need 4096 MB)"
  [ "$fr" -ge 4294967296 ] || die "only $((fr / 1048576)) MB VRAM free"
fi
if [ "$RUNTIME" = wivrn ]; then
  XRJ=/usr/share/openxr/1/openxr_wivrn.json
  [ -f "$XRJ" ] || die "no $XRJ"
  [ $DRY = 1 ] || pgrep -x wivrn-server > /dev/null || die "WiVRn is not running"
else
  XRJ=/usr/share/openxr/1/openxr_monado.json
  [ -d /tmp/somvr-boot.lock ] || die "a Monado test needs the game slot (/tmp/somvr-boot.lock)"
  grep -q '"user": false' "$VRS/status.json" 2>/dev/null || die "the user is in VR (status.json)"
  pgrep -x monado-service > /dev/null && die "a monado-service runs"
  pgrep -f '[H]and of Fate 2\.' > /dev/null && die "Hand of Fate 2 runs"
fi
journalctl -k -b --no-pager 2>/dev/null | grep -qi 'amdgpu.*\(GPU reset\|VRAM is lost\)' &&
  { [ $TEST = 1 ] && die "an amdgpu reset in this boot's kernel log (tests stop)"; log "!!! an amdgpu reset in this boot's kernel log (a reboot before VR is wise)"; }

# ---- the Steam save (only read), the prefix, the prefix's save -------------------------------------------------
SSAVES=("$STEAM"/userdata/*/241930/remote/GameData.sav)
[ ${#SSAVES[@]} = 1 ] && [ -f "${SSAVES[0]}" ] || die "need exactly one Steam save userdata/*/241930/remote/GameData.sav (found ${#SSAVES[@]})"
SSAVE=${SSAVES[0]}; SSAVE_SHA0=$(sha "$SSAVE"); SSAVE_STAT0=$(stat -c '%s %Y' "$SSAVE")
log "the Steam save $SSAVE: sha256 ${SSAVE_SHA0:0:16} (only read)"
SYS32=$PFX/pfx/drive_c/windows/system32
DOCS="$PFX/pfx/drive_c/users/steamuser/Documents/WB Games/Shadow of Mordor"
SAVEF=$DOCS/Saves/GameData.sav
if [ "$PFX" = "$PLAY_PFX" ]; then BK_ROOT=$BACKUPS; else BK_ROOT=$RUN/save-backup; fi
NEW_PFX=0
if [ ! -f "$SYS32/d3d11.dll" ]; then
  [ -e "$PFX" ] && die "$PFX exists but holds no prefix with DXVK (left alone: tell Claude)"
  [ -f "$TEMPLATE/pfx/drive_c/windows/system32/d3d11.dll" ] || die "no template prefix $TEMPLATE"
  NEW_PFX=1
  if [ $DRY = 1 ]; then log "dry run: the prefix would be made: a copy of $TEMPLATE without its Saves -> $PFX"
  else
    log "the first session: the prefix $PFX = a copy of $TEMPLATE (Proton-made, DXVK) without its Saves"
    mkdir -p "$(dirname "$PFX")" && rm -rf "$PFX.part" && cp -a --reflink=auto "$TEMPLATE" "$PFX.part" ||
      die "the prefix copy failed"
    rm -rf "$PFX.part/pfx/drive_c/users/steamuser/Documents/WB Games/Shadow of Mordor/Saves" "$PFX.part/pfx.lock"
    mv "$PFX.part" "$PFX" || die "the prefix copy could not be moved into place"
    log "the prefix: $(du -sh "$PFX" 2>/dev/null | cut -f1) (shared blocks with the template where the disk allows)"
  fi
fi
if [ -n "$TSAVE" ]; then   # (b37: a test's fixed start - the prefix's save, a test's, replaced by a copy of it)
  SEED=$TSAVE
  log "the save: a copy of the test save $TSAVE ($(sha "$TSAVE" | cut -c1-16); only read)$([ -f "$SAVEF" ] && echo " over the prefix's last one ($(sha "$SAVEF" | cut -c1-16))")"
  if [ $DRY = 0 ]; then
    mkdir -p "$DOCS/Saves" && cp --no-preserve=mode "$SEED" "$SAVEF.part" && mv -f "$SAVEF.part" "$SAVEF" || die "the test save copy failed"
    [ "$(sha "$SAVEF")" = "$(sha "$SEED")" ] || die "the test save copy does not match its source"
  fi
elif [ -f "$SAVEF" ]; then
  log "the save: the prefix's own GameData.sav, sha256 $(sha "$SAVEF" | cut -c1-16) ($(stat -c '%s B, %y' "$SAVEF" | cut -c1-40))"
  SEED=""
else
  SEED=""
  [ "$BK_ROOT" = "$BACKUPS" ] && SEED=$(ls -d "$BACKUPS"/*/GameData.sav 2>/dev/null | sort | tail -1)
  if [ -n "$SEED" ]; then log "the save: none in the prefix - seeded from the newest Windows backup $SEED ($(sha "$SEED" | cut -c1-16))"
  else SEED=$SSAVE; log "the save: none in the prefix - seeded from a COPY of the Steam save (${SSAVE_SHA0:0:16}; the file itself only read)"; fi
  if [ $DRY = 0 ]; then
    mkdir -p "$DOCS/Saves" && cp --no-preserve=mode "$SEED" "$SAVEF.part" && mv -f "$SAVEF.part" "$SAVEF" || die "the save seed failed"
    [ "$(sha "$SAVEF")" = "$(sha "$SEED")" ] || die "the seeded save does not match its source"
  fi
fi
if [ $DRY = 0 ]; then   # the backup before the session
  BK=$BK_ROOT/$(date +%Y%m%d-%H%M%S)-before
  mkdir -p "$BK" && cp -p "$SAVEF" "$BK/GameData.sav" && ( cd "$BK" && sha256sum GameData.sav > SHA256SUMS ) &&
    echo "$RUN" > "$BK/session.txt" || die "the save backup failed"
  SAVE_SHA0=$(sha "$SAVEF")
  log "save backup $BK (sha256 ${SAVE_SHA0:0:16})"
else
  log "dry run: a backup of the save would go to $BK_ROOT/<ts>-before, and one after the session to $BK_ROOT/<ts>-after"
fi

# ---- render.cfg: the PC's detection with the VR preset over it --------------------------------------------------
declare -A RCFG=([AmbientOcclusionQuality]=3 [AspectRatioLock]=1 [AutoDetectedGPURating]=10 [AutoDetectionVersion]=1
  [DepthofFieldToggle]=1 [FrameRateLimit]=2 [LightingQuality]=2 [MeshQuality]=3 [MotionBlurQuality]=3 [OITToggle]=0
  [ShadowQuality]=3 [TessellationToggle]=1 [TextureFiltering]=3 [TextureQuality]=3 [VegetationQuality]=4)
for kv in "${RSETS[@]}"; do RCFG[${kv%%=*}]=${kv#*=}; log "render.cfg: Render.Setting.$kv (the VR preset)"; done
for k in $(printf '%s\n' "${!RCFG[@]}" | sort); do printf '"Render.Setting.%s" "%s.000000"\n' "$k" "${RCFG[$k]}"; done > "$RUN/render.cfg.seeded"
if [ $DRY = 0 ]; then
  [ -f "$DOCS/render.cfg" ] && cp "$DOCS/render.cfg" "$RUN/render.cfg.before"
  mkdir -p "$DOCS" && cp "$RUN/render.cfg.seeded" "$DOCS/render.cfg" || die "render.cfg not written"
fi

# ---- the game's environment ---------------------------------------------------------------------------------
CACHE=$(dirname "$PFX")/dxvk-cache; mkdir -p "$CACHE" 2>/dev/null
WOV="xinput9_1_0=n,b;d3d11,d3d10core,dxgi,d3d9=n;winealsa.drv=d;wineoss.drv=d"   # sound: winepulse only
GENV=(WINEPREFIX="$PFX/pfx" WINEDLLOVERRIDES="$WOV" WINEFSYNC=1 WINEDEBUG="${WINEDEBUG:--all,err+module}"
  XDG_CONFIG_HOME="$RUN/xdg-config" SOMVR_LOG="$RUN/somvr.log" SOMVR_BRIDGE="$RUN/bridge" SOMVR_TRIGGER="$RUN/somvr.trigger"
  SOM_BOOTCHECK_RUN="$RUN" DXVK_LOG_PATH="$RUN" DXVK_LOG_LEVEL=info DXVK_STATE_CACHE_PATH="$CACHE"
  VK_LAYER_PATH="$LAYER" VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr XR_RUNTIME_JSON="$XRJ")
genv SOMVR_XR_LOG > /dev/null || GENV+=(SOMVR_XR_LOG="$RUN/xr-layer.log")
XRLOG=$(genv SOMVR_XR_LOG) || XRLOG=$RUN/xr-layer.log
NULLSINK=""; NULLMOD=""
[ "$SINK" = null ] && { NULLSINK=somvr_win_null_$$; GENV+=(PULSE_SINK="$NULLSINK" PULSE_SOURCE="$NULLSINK.monitor"); }
GENV+=("${GEXTRA[@]}")
# som-b35-live (docs/re-xr-pipeline.md 162): SOMVR_LIVE=on - the live folder (logs/live/<this run>) for the layer and the
# DLL; its daemon (the marks' voices into the coordinator's inbox) once the game runs, stopped at the end
LIVE_ON=0; LIVE_W=$(genv SOMVR_LIVE) && { [ "$LIVE_W" = on ] || [ "$LIVE_W" = 1 ]; } && [ -x "$HERE/live_session.sh" ] && LIVE_ON=1
[ $LIVE_ON = 1 ] && [ $DRY = 0 ] && GENV+=(SOMVR_LIVE_DIR="$("$HERE/live_session.sh" dir "$RUN")")
# b37 (docs/re-xr-pipeline.md 156.10): a session the launcher started for the user (its test too) - the host refuses the
# raw `reload` word and the file watch here whatever SOMVR_DEV_RELOAD says; only the player's own Apply / Undo swaps
# (SOMVR_LIVE=on). Last, so no --game-env takes it off; a single-DLL build ignores it
GENV+=(SOMVR_USER_SESSION=1)
mkdir -p "$RUN/xdg-config"
# the game in a mount namespace: the install as an overlay (writes -> RUN/game-upper); a test: no /dev/snd either
mkdir -p "$RUN/game-upper" "$RUN/game-work"
NS=(bwrap --dev-bind / / --overlay-src "$GAME_DIR" --overlay "$RUN/game-upper" "$RUN/game-work" "$GAME_DIR")
[ "$SINK" = null ] && NS+=(--tmpfs /dev/snd)
launch_record() {
  { echo "# the game: env -u DISPLAY -u WAYLAND_DISPLAY DISPLAY=<gamescope's> <env below> ${NS[*]} --chdir '$GAME_DIR/x64' -- $WINE $EXE"
    printf '%s\n' "${GENV[@]}"; } > "$RUN/launch-cmd.txt"
  log "launch: ${#GENV[@]} environment entries (launch-cmd.txt): $(printf '%s\n' "${GENV[@]}" | grep -c '^SOMVR_') SOMVR_ switches, XR_RUNTIME_JSON=$XRJ, sound $([ -n "$NULLSINK" ] && echo "PULSE_SINK=$NULLSINK (a null sink)" || echo 'the default output (winepulse)')"
}
launch_record

if [ $DRY = 1 ]; then
  log "dry run: would reserve $RES, start$([ "$RUNTIME" = monado ] && echo " Monado's null compositor,") gamescope --backend headless -W $W -H $H -r $REFRESH, the DLL into $SYS32, then the game"
  [ -n "$CMDS" ] && log "dry run: the start sequence $CMDS ($(grep -c . "$CMDS") lines) checked"
  "${NS[@]}" -- sh -c 'touch "$1/.somvr-ns-probe"' p "$GAME_DIR" 2>/dev/null && [ ! -e "$GAME_DIR/.somvr-ns-probe" ] &&
    [ -e "$RUN/game-upper/.somvr-ns-probe" ] && log "dry run: the overlay over the install holds (a write landed in game-upper)" ||
    log "!!! dry run: the overlay over $GAME_DIR did not hold"
  rm -f "$RUN/game-upper/.somvr-ns-probe"
  log "dry run: nothing started; the prefix $([ $NEW_PFX = 1 ] && echo 'would be made' || echo exists), the save $([ -n "$SEED" ] && echo "would be seeded from $SEED" || echo 'is the prefix'"'"'s own')"
  exit 0
fi

# ---- from here on everything we start is stopped by cleanup --------------------------------------------------
GS_PID=""; GS_CHILD=""; BW_PID=""; GAME_PID=""; KEEPER=""; WDOG=""; MSVC=""; AGUARD=""; CLEANED=0; GAME_T0=""; GAME_END=""
FAST_STOP=0; QUIT=0; PLAYER_QUIT=0
wsrv_kill() { env WINEPREFIX="$PFX/pfx" "$WSRV" -k 2>/dev/null; }
crash_sign() { LC_ALL=C grep -a -m1 -E 'Unhandled (page fault|exception)|wine: Assertion|err:seh:' "$RUN/wine.log" 2>/dev/null | cut -c1-140; }
save_quiet() {   # the game's save not written for 2 s (at most 10 s): a stop never cuts a save in half
  local i m now
  for i in $(seq 1 20); do
    m=$(stat -c %Y "$SAVEF" 2>/dev/null || echo 0); now=$(date +%s)
    [ $((now - m)) -ge 2 ] && return 0
    [ "$i" = 1 ] && log "the save was written $((now - m)) s ago: waiting for it to be quiet"
    sleep 0.5
  done
  log "!!! the save kept changing for 10 s: stopping anyway"
}
stop_game() {
  local i pids
  alive "$BW_PID" || alive "$GAME_PID" || return 0
  if [ $FAST_STOP = 0 ] && alive "$GAME_PID" && [ -e "$RUN/somvr.trigger" ] && ! LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null; then
    printf 'endlogs' > "$RUN/somvr.trigger"
    for i in $(seq 1 12); do LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null && break; sleep 0.25; done
    LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null && log "the DLL's END lines written (the word endlogs)" ||
      log "!!! the DLL wrote no END lines within 3 s of the word endlogs"
  fi
  alive "$GAME_PID" && save_quiet
  pids=$(tree "$BW_PID" 2>/dev/null | tr '\n' ' ')
  log "stopping the game: wineserver -k (prefix $PFX)"
  wsrv_kill
  for i in $(seq 1 30); do alive "$GAME_PID" || break; sleep 0.5; done
  alive "$GAME_PID" && log "the game still alive 15 s after wineserver -k: SIGKILL"
  for i in $pids $(tree "$BW_PID" 2>/dev/null); do alive "$i" && [ "$i" != "$MAIN" ] && kill -KILL "$i" 2>/dev/null; done
  for i in $(seq 1 20); do alive "$BW_PID" || break; sleep 0.25; done
  [ -n "$GAME_END" ] || GAME_END=$(date +%s)
  [ -f "$RUN/game.exit" ] || { wait "$BW_PID" 2>/dev/null; echo "exit $? (stopped by the session)" > "$RUN/game.exit"; }
}
stop_gamescope() {
  local i
  alive "$GS_PID" || return 0
  [ -n "$GS_CHILD" ] && kill -TERM "$GS_CHILD" 2>/dev/null
  for i in $(seq 1 25); do alive "$GS_PID" || break; sleep 0.2; done
  alive "$GS_PID" && { kill -TERM "$GS_PID" 2>/dev/null; sleep 1; }
  alive "$GS_PID" && kill -KILL "$GS_PID" 2>/dev/null
  log "gamescope stopped (pid $GS_PID)"
}
save_lines() {
  local s1 st1
  if [ -f "$SAVEF" ]; then
    s1=$(sha "$SAVEF")
    log "save (Windows, the prefix's): ${SAVE_SHA0:0:16} -> ${s1:0:16} ($([ "$s1" = "$SAVE_SHA0" ] && echo unchanged || echo 'the game saved its progress')); backup before: $BK"
    sha256sum "$SAVEF" > "$RUN/save-after.sha256"
    # build 34 (the Windows review): a copy after the session too (the save only read), kept like the ones before - the
    # newest backup seeds a lost prefix, so a lost play prefix never loses the last session's progress
    if [ $DRY = 0 ] && [ -n "$BK" ] && [ -n "$GAME_PID" ]; then
      BKA=$BK_ROOT/$(date +%Y%m%d-%H%M%S)-after
      if mkdir -p "$BKA" && cp -p "$SAVEF" "$BKA/GameData.sav" && ( cd "$BKA" && sha256sum GameData.sav > SHA256SUMS ) &&
           echo "$RUN" > "$BKA/session.txt" && [ "$(sha "$BKA/GameData.sav")" = "$s1" ]; then
        log "save backup after the session: $BKA (sha256 ${s1:0:16})"
      else log "!!! the save backup after the session failed ($BKA)"; fi
    fi
  else log "!!! save (Windows): no GameData.sav in the prefix after the session (the backup: $BK)"; fi
  st1=$(stat -c '%s %Y' "$SSAVE" 2>/dev/null)
  if [ "$(sha "$SSAVE")" = "$SSAVE_SHA0" ] && [ "$st1" = "$SSAVE_STAT0" ]; then log "SAVE OK: the Steam save is unchanged (${SSAVE_SHA0:0:16})"
  else log "!!! SAVE CHANGED: the Steam save differs from the session's start (${SSAVE_SHA0:0:16}; size/mtime $SSAVE_STAT0 -> $st1)"; fi
}
summary() {
  {
    echo "Shadow of Mordor VR - Windows build session  (run dir $RUN)"
    echo "start $(date -d @"$T0" +%T)  end $(date +%T)  total $(( $(date +%s) - T0 )) s; runtime $RUNTIME; sound $SINK"
    [ -f "$RUN/TIMEOUT" ] && echo "HARD LIMIT: $(cat "$RUN/TIMEOUT")"
    [ -n "$GAME_T0" ] && echo "game pid $GAME_PID ran $(( ${GAME_END:-$(date +%s)} - GAME_T0 )) s; exit: $(cat "$RUN/game.exit" 2>/dev/null || echo '?')"
    echo "--- marks (game time)"; grep -a ' MARK ' "$LOG" | sed 's/^/  /'
    [ -e "$RUN/quit.request" ] && echo "--- the player quit from the VR menu: $(head -c 200 "$RUN/quit.request" | tr '\n' ' ')"
    [ -s "$RUN/marks.log" ] && { echo "--- bug marks (RUN/marks/)"; sed 's/^/  /' "$RUN/marks.log"; }
    echo "--- save"; grep -a -E 'save|SAVE' "$LOG" | sed 's/^[0-9:]* +[0-9]*s *//' | sed 's/^/  /'
    echo "--- sound (WirePlumber's remembered state for the game: $(grep -a -F 'application.name:ShadowOfMordor.exe=' "$HOME/.local/state/wireplumber/stream-properties" 2>/dev/null | grep -a -o '"mute":[a-z]*' | tr '\n' ' '))"; grep -a -E 'sound|AUDIO' "$LOG" | sed 's/^[0-9:]* +[0-9]*s *//' | sed 's/^/  /' | tail -12
    echo "--- the install (overlay writes kept in game-upper)"; (cd "$RUN/game-upper" 2>/dev/null && find . -type f | head -20 | sed 's/^/  /')
    echo "--- somvr.log: $([ -e "$RUN/somvr.log" ] && echo "$(wc -l < "$RUN/somvr.log") lines, tail:" || echo 'not written')"
    tail -12 "$RUN/somvr.log" 2>/dev/null | cut -c1-200 | sed 's/^/    /'
    echo "--- wine errors"; grep -a -E '^err:|Unhandled' "$RUN/wine.log" 2>/dev/null | head -12 | cut -c1-200 | sed 's/^/  /'
    echo "--- left over"
    for p in $GAME_PID $BW_PID $GS_PID $KEEPER $WDOG $MSVC $AGUARD; do alive "$p" && echo "  STILL ALIVE pid $p ($(cat /proc/$p/comm 2>/dev/null))"; done
    echo "  Windows game processes: $(win_game)"
    echo "  $RES: $([ -e "$RES" ] && echo PRESENT || echo gone)"
    echo "--- screenshots"; ls "$RUN/shots" 2>/dev/null | sed 's/^/  /' | tail -30
  } > "$RUN/summary.txt.part" 2>&1
  mv -f "$RUN/summary.txt.part" "$RUN/summary.txt"
  log "summary: $RUN/summary.txt"
}
cleanup() {
  [ $CLEANED = 1 ] && return
  CLEANED=1
  trap '' INT TERM
  local te ts mb
  te=$(date +%s)
  [ -n "$ENDING" ] && printf 'ending %s %s\n' "$te" "$RUN" > "$ENDING" 2>/dev/null
  [ -n "$WDOG" ] && kill "$WDOG" 2>/dev/null
  stop_game
  [ "${LIVE_ON:-0}" = 1 ] && "$HERE/live_session.sh" stop "$RUN"   # som-b35-live: its daemon (its own pid only)
  [ -n "$AGUARD" ] && kill "$AGUARD" 2>/dev/null
  stop_gamescope
  if alive "$MSVC"; then kill -INT "$MSVC" 2>/dev/null; for i in $(seq 1 25); do alive "$MSVC" || break; sleep 0.2; done; alive "$MSVC" && kill -TERM "$MSVC"; fi
  [ -n "$NULLMOD" ] && { pactl unload-module "$NULLMOD" 2>/dev/null; log "sound: the null sink $NULLSINK unloaded"; }
  save_lines
  ts=$(date +%s)
  [ -n "$KEEPER" ] && kill "$KEEPER" 2>/dev/null
  rm -f "$RES"
  summary
  mb=$(( 200 - ($(date +%s) - te) ))
  if [ -d "$RUN/marks" ] && compgen -G "$RUN/marks/*/xr.log" > /dev/null && [ -x "$HERE/mark_compose.sh" ]; then
    if [ "$mb" -ge 15 ]; then log "$("$HERE/mark_compose.sh" "$RUN" --budget "$mb" 2>&1 | tail -3 | tr '\n' ' ')"
    else log "mark_compose: no time left in the stop ($mb s): mark_compose.sh $RUN later"; fi
  fi
  local tg=${GAME_END:-$te}; [ "$tg" -ge "$te" ] || tg=$te
  log "the end: $(( $(date +%s) - te )) s (the game gone at +$((tg - te)) s, the save checked at +$((ts - te)) s)"
}
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap 'cleanup' EXIT

# ---- the slot, the hard limit -----------------------------------------------------------------------------------
mkdir -p "$VRS"; touch "$RES"
( n=0; while kill -0 "$MAIN" 2>/dev/null; do [ $((n % 10)) -eq 0 ] && touch "$RES"; n=$((n + 1)); sleep 2; done; rm -f "$RES" ) > /dev/null 2>&1 &
KEEPER=$!
sleep 1
if why=$(other_slot_busy); then die "game slot taken meanwhile: $why"; fi
log "slot reserved: $RES (keeper $KEEPER)"
( end=$((T0 + HARD - 45)); while kill -0 "$MAIN" 2>/dev/null && [ "$(date +%s)" -lt "$end" ]; do sleep 5; done
  kill -0 "$MAIN" 2>/dev/null && { echo "$HARD s limit" > "$RUN/TIMEOUT"; kill -TERM "$MAIN"; } ) > /dev/null 2>&1 &
WDOG=$!

# ---- the DLL into the prefix ------------------------------------------------------------------------------------
install -m 0644 "$DLL" "$SYS32/xinput9_1_0.dll" && cmp -s "$DLL" "$SYS32/xinput9_1_0.dll" || die "the DLL did not land in $SYS32"
log "DLL $(sha "$DLL" | cut -c1-16) in $SYS32/xinput9_1_0.dll"
LDLL=$(dirname "$DLL")/somvr_logic.dll # a split build (docs/re-xr-pipeline.md 156): the host loads it from its folder
if [ -f "$LDLL" ]; then
  install -m 0644 "$LDLL" "$SYS32/somvr_logic.dll" && cmp -s "$LDLL" "$SYS32/somvr_logic.dll" || die "the logic DLL did not land in $SYS32"
  log "LOGIC $(sha "$LDLL" | cut -c1-16) in $SYS32/somvr_logic.dll"
  # b40 live4 (re-xr-pipeline.md 162.13): a patch staged for this build before the launch is the session's module
  [ "${LIVE_ON:-0}" = 1 ] && [ $DRY = 0 ] && log "live: $("$HERE/live_session.sh" preapply "$RUN" "$(dirname "$(dirname "$(dirname "$DLL")")")" "$SYS32/somvr_logic.dll" 2>&1 | tail -1)"
else # a single-DLL build (build-win3 and older): no logic module - an earlier split session's copy goes (never loaded)
  [ -f "$SYS32/somvr_logic.dll" ] && rm -f -- "$SYS32/somvr_logic.dll" && log "a single-DLL build: $SYS32/somvr_logic.dll of an earlier split build removed"
  log "a single-DLL build (no $LDLL): the game side in xinput9_1_0.dll"
fi

# ---- sound ------------------------------------------------------------------------------------------------------
game_inputs() {   # the game's streams: "INDEX SINKNAME MUTED" per line (SINKNAME "-": on no sink yet - Wine first opens
                  # a probe stream with sink 4294967295, then the real one): application.name ShadowOfMordor.exe, or a process whose
                  # WINEPREFIX is ours
  local sinks
  sinks=$(pactl list short sinks 2>/dev/null)
  pactl list sink-inputs 2>/dev/null | awk '
    function out() { if (i != "") print i, s, (m == "" ? "?" : m), (p == "" ? 0 : p), (a == "" ? "?" : a) }
    /^Sink Input #/ { out(); i = substr($3, 2); s = m = p = a = "" }
    /^[ \t]*Sink: / { s = $2 } /^[ \t]*Mute: / { m = $2 }
    /application\.process\.id = / { p = $3; gsub(/"/, "", p) }
    /application\.name = / { a = $3; gsub(/"/, "", a) }
    END { out() }' | while read -r i s m p a; do
      if [ "$a" != ShadowOfMordor.exe ]; then   # not by name: a process of our prefix (its environment)?
        tr '\0' '\n' < "/proc/${p:-0}/environ" 2>/dev/null | grep -q -x -F "WINEPREFIX=$PFX/pfx" || continue
      fi
      n=$(printf '%s\n' "$sinks" | awk -v x="$s" '$1 == x { print $2 }')
      printf '%s %s %s\n' "$i" "${n:--}" "$m"; done
}
wivrn_sink() { pactl list short sinks 2>/dev/null | awk 'tolower($2) ~ /^wivrn/ { print $2; exit }'; }
if [ "$SINK" = null ]; then
  NULLMOD=$(pactl load-module module-null-sink "sink_name=$NULLSINK" "sink_properties=device.description=$NULLSINK") ||
    die "no null sink (pactl load-module)"
  pactl list short sinks | awk -v n="$NULLSINK" '$2 == n { f = 1 } END { exit !f }' || die "the null sink $NULLSINK is not listed"
  log "sound: the null sink $NULLSINK (module $NULLMOD); the game's PULSE_SINK (PULSE_SOURCE its monitor); /dev/snd hidden; the guard checks its streams every 0.25 s"
  # the guard: a game stream on any other sink is moved onto the null sink at once (not muted: WirePlumber would keep
  # the mute for the application's next start - the user's session) and the game is ended
  ( while kill -0 "$MAIN" 2>/dev/null; do
      game_inputs | while read -r i s m; do
        case $s in "$NULLSINK"|-) continue ;; esac
        pactl move-sink-input "$i" "$NULLSINK" 2>/dev/null
        echo "$(date +%T) stream $i on '$s' (not $NULLSINK): moved to the null sink, the game ended" >> "$RUN/AUDIO_GUARD"
      done
      [ -s "$RUN/AUDIO_GUARD" ] && { env WINEPREFIX="$PFX/pfx" "$WSRV" -k 2>/dev/null; kill -TERM "$MAIN"; exit 0; }
      sleep 0.25
    done ) > /dev/null 2>&1 &
  AGUARD=$!
else
  log "sound: winepulse to the PC's default output ($(pactl get-default-sink 2>/dev/null)); WiVRn's sink: $(wivrn_sink || true)"
fi
AUD_NEXT=$(( $(date +%s) + 30 )); AUD_FIXED=0
audio_check() {   # (the user's session) at +30 s once: the game's stream onto WiVRn's sink if it is elsewhere; a log line every 5 min
  local now l ws i s m
  now=$(date +%s); [ "$now" -ge "$AUD_NEXT" ] || return 0
  AUD_NEXT=$((now + 300))
  if [ "$SINK" = null ]; then l=$(game_inputs | tr '\n' ';'); log "sound check: the game's streams ${l:-none yet} (all on $NULLSINK or the guard stops the session)"; return 0; fi
  ws=$(wivrn_sink); l=$(game_inputs)
  log "sound check: the game's streams: $(printf '%s' "${l:-none}" | tr '\n' ';'); WiVRn's sink: ${ws:-none}; the default: $(pactl get-default-sink 2>/dev/null)"
  [ $AUD_FIXED = 0 ] && [ -n "$ws" ] && [ -n "$l" ] || return 0
  AUD_FIXED=1
  printf '%s\n' "$l" | while read -r i s m; do
    case $s in "$ws"|-) continue ;; esac
    pactl move-sink-input "$i" "$ws" 2>/dev/null && log "sound: the game's stream $i moved from $s to $ws (the headset)"
  done
}

# ---- Monado's null compositor (tests) ----------------------------------------------------------------------------
if [ "$RUNTIME" = monado ]; then
  M=$RUN/mxdg; mkdir -p "$M/config" "$M/cache" "$M/data"
  setsid chrt -o 0 env -i HOME="$M" PATH=/usr/bin:/bin XDG_RUNTIME_DIR=$RT XDG_CONFIG_HOME="$M/config" \
    XDG_CACHE_HOME="$M/cache" XDG_DATA_HOME="$M/data" SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
    XRT_LOG=info VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
    timeout -s INT -k 10 $((HARD + 120)) /usr/bin/monado-service > "$RUN/monado.log" 2>&1 < /dev/null &
  MSVC=$!
  for i in $(seq 1 50); do [ -S "$RT/monado_comp_ipc" ] && break; sleep 0.2; done
  [ -S "$RT/monado_comp_ipc" ] || die "monado did not come up"
  log "Monado's null compositor (the test's stand-in for WiVRn): pid $MSVC"
fi

# ---- the virtual screen ------------------------------------------------------------------------------------------
setsid env -u LD_LIBRARY_PATH -u LD_PRELOAD -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY XDG_RUNTIME_DIR="$RT" \
  gamescope --backend headless -W "$W" -H "$H" -w "$W" -h "$H" -r "$REFRESH" -- \
  sh -c 'echo "CHILD_PID=$$" > "$1.tmp" && env >> "$1.tmp" && mv -f "$1.tmp" "$1" && exec tail -f /dev/null' \
  vdisplay "$RUN/vdisplay.env" > "$RUN/gamescope.log" 2>&1 < /dev/null &
GS_PID=$!
for i in $(seq 1 50); do [ -s "$RUN/vdisplay.env" ] && break; alive "$GS_PID" || break; sleep 0.2; done
[ -s "$RUN/vdisplay.env" ] || { tail -5 "$RUN/gamescope.log" >&2; die "gamescope did not come up within 10 s"; }
envval() { sed -n "s/^$1=//p" "$RUN/vdisplay.env" | head -1; }
GS_CHILD=$(envval CHILD_PID); GDISP=$(envval DISPLAY); GWD=$(envval GAMESCOPE_WAYLAND_DISPLAY)
[ -n "$GDISP" ] && [ -S "/tmp/.X11-unix/X${GDISP#:}" ] || die "no X display from gamescope (DISPLAY=$GDISP)"
log "virtual screen up: gamescope pid $GS_PID, DISPLAY $GDISP, $GWD, ${W}x${H} at $REFRESH Hz"
shot_to() {
  local f=$1 i
  GAMESCOPE_WAYLAND_DISPLAY="$GWD" XDG_RUNTIME_DIR="$RT" timeout 10 gamescopectl screenshot "$f" >> "$RUN/shots.log" 2>&1
  for i in $(seq 1 30); do [ -s "$f" ] && break; sleep 0.2; done
  [ -s "$f" ] && echo "shot $f" >> "$RUN/shots.log" || { echo "shot FAILED $f" >> "$RUN/shots.log"; return 1; }
}
shot() { shot_to "$RUN/shots/$1.png"; }
xkey() { env -u XAUTHORITY DISPLAY="$GDISP" xdotool key --delay 120 "$@" >> "$RUN/input.log" 2>&1; }

# ---- the game ----------------------------------------------------------------------------------------------------
"${NS[@]}" -- sh -c 'touch "$1/.somvr-ns-probe"' p "$GAME_DIR" && [ ! -e "$GAME_DIR/.somvr-ns-probe" ] &&
  [ -e "$RUN/game-upper/.somvr-ns-probe" ] || die "the overlay over $GAME_DIR did not hold"
rm -f "$RUN/game-upper/.somvr-ns-probe"
log "the install is an overlay for the session (writes -> $RUN/game-upper): $GAME_DIR"
( cd / && exec setsid env -u WAYLAND_DISPLAY -u XAUTHORITY DISPLAY="$GDISP" "${GENV[@]}" \
    "${NS[@]}" --chdir "$GAME_DIR/x64" -- "$WINE" "$EXE" > "$RUN/wine.log" 2>&1 < /dev/null ) &
BW_PID=$!
for i in $(seq 1 120); do
  for p in $(win_game); do   # the exe's Windows command line, in OUR prefix (its environment; not by the tree: Wine's
                             # processes need not stay below bwrap)
    tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -q -x -F "WINEPREFIX=$PFX/pfx" && { GAME_PID=$p; break; }
  done
  [ -n "$GAME_PID" ] && break
  alive "$BW_PID" || break
  sleep 0.25
done
[ -n "$GAME_PID" ] || { tail -20 "$RUN/wine.log" >&2; die "the game process did not appear (bwrap $BW_PID)"; }
GAME_T0=$(date +%s)
log "game started: bwrap $BW_PID, ShadowOfMordor.exe $GAME_PID (Proton's wine, prefix $PFX)"
# b37 (162.8): the spoken answers on wivrn.sink by that exact name (live_tts.py); a test (--sink null): its null sink
[ $LIVE_ON = 1 ] && log "live: the daemon $(SOMVR_LIVE_TTS=$(genv SOMVR_LIVE_TTS || echo off) LIVE_TTS_SINK=$([ "$SINK" = null ] && echo "$NULLSINK") "$HERE/live_session.sh" start "$RUN" $$ "$BUILD/ShadowOfMordor/mod/somvr") ($("$HERE/live_session.sh" dir "$RUN")); spoken answers: $(genv SOMVR_LIVE_TTS || echo off) on $([ "$SINK" = null ] && echo "$NULLSINK" || echo wivrn.sink)"

# ---- the player's requests, the game's end -------------------------------------------------------------------------
gel() { echo $(( $(date +%s) - GAME_T0 )); }
WL_POS=0; MARKS_N=0
VOICE_S=$(genv SOMVR_MARK_VOICE_S) || VOICE_S=9
{ [[ $VOICE_S =~ ^[0-9]+$ ]] && [ "$VOICE_S" -ge 1 ] && [ "$VOICE_S" -le 30 ]; } || VOICE_S=9
VOICE_MODE=$(genv SOMVR_MARK_VOICE) || VOICE_MODE=talk; [ "$VOICE_MODE" = fixed ] || VOICE_MODE=talk
VOICE_MAX=$(genv SOMVR_MARK_VOICE_MAX_S) || VOICE_MAX=30
{ [[ $VOICE_MAX =~ ^[0-9]+$ ]] && [ "$VOICE_MAX" -ge 5 ] && [ "$VOICE_MAX" -le 30 ]; } || VOICE_MAX=30
VOICE_QUIET=$(genv SOMVR_MARK_VOICE_QUIET_S) || VOICE_QUIET=2; [[ $VOICE_QUIET =~ ^[0-9]+(\.[0-9]+)?$ ]] || VOICE_QUIET=2
VOICE_REC=$HERE/voice_rec.py; [ "$VOICE_MODE" = talk ] && [ ! -r "$VOICE_REC" ] && VOICE_MODE=fixed
VOICE_PID=""
voice_stop() { [ -n "$VOICE_PID" ] && kill -0 "$VOICE_PID" 2>/dev/null && kill -TERM "$VOICE_PID" 2>/dev/null; VOICE_PID=""; }
mic_source() { timeout 5 pactl list short sources 2>/dev/null | awk '$2 ~ /[Ww][Ii][Vv][Rr][Nn]/ && $2 !~ /\.monitor$/ { print $2; exit }'; }
mark_serve() {   # as boot_check.sh's: the flat picture and the log tails now, the voice and the JPEGs in their own process
  local d=$1 src head how
  mv -f "$d/want" "$d/served" 2>/dev/null || return 0
  MARKS_N=$((MARKS_N + 1))
  shot_to "$d/flat.png" || echo "no flat picture (gamescope's screenshot failed)" > "$d/flat.txt"
  tail -n 400 "$RUN/somvr.log" > "$d/somvr_tail.txt" 2>/dev/null
  tail -n 200 "$XRLOG" > "$d/xr_tail.txt" 2>/dev/null
  src=$(mic_source); head=$(head -1 "$d/state.txt" 2>/dev/null | cut -c1-120)
  how="$VOICE_S s from "; [ "$VOICE_MODE" = talk ] && how="until the talk ends (${VOICE_QUIET} s quiet, at most $VOICE_MAX s) from "
  log "bug mark $MARKS_N: $d (${head:-no state.txt}); voice: ${src:+$how}${src:-none (no WiVRn microphone source: is its microphone on?)}"
  echo "$(date +%T) ${d##*/} ${head:-?} | voice: ${src:+recording }${src:-none}" >> "$RUN/marks.log"
  voice_stop
  if [ -n "$src" ] && [ "$VOICE_MODE" = talk ]; then
    python3 -I "$VOICE_REC" --src "$src" --out "$d/voice.wav" --txt "$d/voice.txt" --err "$d/voice.err" \
      --max "$VOICE_MAX" --quiet "$VOICE_QUIET" > /dev/null 2>> "$d/voice.err" < /dev/null &
    VOICE_PID=$!
  fi
  ( if [ -n "$src" ] && [ "$VOICE_MODE" = talk ]; then :
    elif [ -n "$src" ]; then
      timeout -s INT "$VOICE_S" parecord -d "$src" --channels=1 --rate=48000 --format=s16le --file-format=wav "$d/voice.wav" 2> "$d/voice.err"
      sz=$(stat -c %s "$d/voice.wav" 2>/dev/null || echo 0)
      if [ "$sz" -gt 1000 ]; then echo "$(awk -v b="$sz" 'BEGIN { printf "%.1f", (b - 44) / 96000 }') s from $src" > "$d/voice.txt"
      else echo "no voice: $src gave $sz bytes" > "$d/voice.txt"; fi
      [ -s "$d/voice.err" ] || rm -f "$d/voice.err"
    else
      echo "no voice: no WiVRn microphone source (turn the microphone on in the WiVRn app)" > "$d/voice.txt"
    fi
    for i in $(seq 1 20); do [ -s "$d/eyes.ppm" ] && break; sleep 0.5; done
    if [ -s "$d/eyes.ppm" ]; then sleep 0.5; nice -n 19 taskset -c 15 magick "$d/eyes.ppm" -quality 85 "$d/eyes.jpg" 2>> "$d/convert.err" && rm -f "$d/eyes.ppm"
    else echo "no eyes picture from the layer within 10 s" > "$d/eyes.txt"; fi
    [ -s "$d/flat.png" ] && nice -n 19 taskset -c 15 magick "$d/flat.png" -quality 85 "$d/flat.jpg" 2>> "$d/convert.err" && rm -f "$d/flat.png"
    [ -s "$d/convert.err" ] || rm -f "$d/convert.err"
  ) > /dev/null 2>&1 < /dev/null &
}
QW_EXIT_S=10; QW_CRASH_S=2; QW_FRAMES_S=60
QW_NEXT=0; QW_EXIT=""; QW_CRASH=""; QW_SEEN=0; QW_NOFR=""; QW_WHY=""
quit_watch() {
  local now l h9 age
  now=$(date +%s); [ "$now" -ge "$QW_NEXT" ] || return 0
  QW_NEXT=$((now + 1))
  [ $QUIT = 0 ] && alive "$GAME_PID" || return 0
  if [ -z "$QW_EXIT" ] && l=$(LC_ALL=C grep -a -m1 -F 'xr: exit: the game is exiting' "$XRLOG" 2>/dev/null) && [ -n "$l" ]; then
    QW_EXIT=$now; log "the game is exiting: $(printf '%s' "${l#*xr: exit: the game is exiting }" | cut -c1-150)"
  fi
  if [ -e "$RUN/somvr.log" ]; then
    h9=$(tail -c 65536 "$RUN/somvr.log" 2>/dev/null | LC_ALL=C grep -a '^RATE ' | tail -1 | sed -n 's/.* H9=\([0-9]*\) .*/\1/p')
    age=$(( now - $(stat -c %Y "$RUN/somvr.log" 2>/dev/null || echo "$now") ))
    if [ -n "$h9" ] && [ "$h9" -gt 0 ] && [ "$age" -lt 5 ]; then QW_SEEN=1; QW_NOFR=""
    elif [ $QW_SEEN = 1 ] && [ -z "$QW_NOFR" ]; then QW_NOFR=$now; log "the game shows no frames (somvr.log: H9=${h9:-?}, written $age s ago)"; fi
  fi
  if [ -z "$QW_CRASH" ] && l=$(crash_sign) && [ -n "$l" ]; then QW_CRASH=$now; log "!!! the game crashed ($l)"; fi
  if [ -n "$QW_CRASH" ] && [ $((now - QW_CRASH)) -ge $QW_CRASH_S ]; then QW_WHY="crashed"
  elif [ -n "$QW_EXIT" ] && [ $((now - QW_EXIT)) -ge $QW_EXIT_S ]; then QW_WHY="is still alive $QW_EXIT_S s after its exit began"
  elif [ -n "$QW_NOFR" ] && [ $((now - QW_NOFR)) -ge $QW_FRAMES_S ]; then QW_WHY="has shown no frame for $QW_FRAMES_S s"
  else return 0; fi
  log "the game $QW_WHY: the session ends it (a fast stop)"
  FAST_STOP=1; QUIT=1
}
svc() {
  local w
  quit_watch; audio_check
  if [ $PLAYER_QUIT = 0 ] && [ -e "$RUN/quit.request" ]; then
    log "QUIT: the player asked to quit ($(head -c 200 "$RUN/quit.request" 2>/dev/null | tr '\n' ' ')): stopping the game"
    QUIT=1; PLAYER_QUIT=1
  fi
  for w in "$RUN"/marks/*/want; do [ -e "$w" ] && mark_serve "${w%/want}"; done
}
sleep_svc() { local i; for i in $(seq 1 $(( $1 * 2 ))); do sleep 0.5; svc; [ $QUIT = 1 ] && return 0; alive "$GAME_PID" || return 0; done; }
wait_log() {
  local secs=$1 text hit line end; shift
  text=$*; [ "$secs" -le 300 ] || secs=300
  end=$(( $(date +%s) + secs ))
  while :; do
    hit=$(tail -c +$((WL_POS + 1)) "$RUN/somvr.log" 2>/dev/null | LC_ALL=C grep -a -b -F -m1 -- "$text")
    if [ -n "$hit" ]; then
      line=${hit#*:}; WL_POS=$(LC_ALL=C; echo $((WL_POS + ${hit%%:*} + ${#line} + 1)))
      log "waitlog: '$text' at game time $(gel) s: $(printf '%s' "$line" | cut -c1-160)"; return 0
    fi
    [ "$(date +%s)" -ge "$end" ] && { log "!!! waitlog: no '$text' in somvr.log within $secs s (game time $(gel) s); going on"; return 1; }
    alive "$GAME_PID" || return 1
    sleep 0.5; svc; [ $QUIT = 1 ] && return 1
  done
}
run_cmd() {
  local c=$1; shift
  case $c in
    shot) shot "${1:-manual-$(gel)s}" ;;
    key) xkey "$@" ;;
    xdo) env -u XAUTHORITY DISPLAY="$GDISP" xdotool "$@" >> "$RUN/input.log" 2>&1 ;;
    mark) log "MARK $* at game time $(gel) s" ;;
    wait) sleep_svc "$(( ${1:-1} > 30 ? 30 : ${1:-1} ))" ;;
    waitlog) wait_log "$@" ;;
    trigger) printf '%s' "${1:-}" > "$RUN/somvr.trigger"; log "trigger: $RUN/somvr.trigger${1:+ ($1)} at game time $(gel) s" ;;
    untrigger) rm -f -- "$RUN/somvr.trigger"; log "untrigger at game time $(gel) s" ;;
    quit) QUIT=1 ;;
    ''|'#'*) ;;
    *) log "unknown command: $c $*" ;;
  esac
}
if [ -n "$CMDS" ]; then
  log "the start sequence $CMDS ($(grep -c . "$CMDS") lines)"
  while read -r line; do
    alive "$GAME_PID" || { log "the game is gone: the rest of the start sequence is dropped"; break; }
    case $line in ''|'#'*) continue ;; esac
    log "cmd: $line"
    read -r -a w <<< "$line"; [ ${#w[@]} -gt 0 ] && run_cmd "${w[@]}"
    svc; [ $QUIT = 1 ] && break
  done < "$CMDS"
fi
next=0
while :; do
  el=$(gel)
  if ! alive "$GAME_PID"; then
    GAME_END=$(date +%s); for i in $(seq 1 20); do alive "$BW_PID" || break; sleep 0.25; done
    wait "$BW_PID" 2>/dev/null; echo "exit $? (by itself)" > "$RUN/game.exit"
    log "the game exited by itself after $el s ($(cat "$RUN/game.exit"))"; break
  fi
  [ $(( $(date +%s) - T0 )) -ge $((HARD - 130)) ] && { log "near the hard limit: quitting"; break; }
  [ "$el" -ge "$HOLD" ] && { shot "hold-final-${el}s"; log "hold limit $HOLD s reached"; break; }
  if [ "$el" -ge "$next" ]; then shot "$(printf 'auto-%04ds' "$el")"; if [ "$el" -lt 240 ]; then next=$((el + 10)); else next=$((el + 60)); fi; fi
  svc
  [ $QUIT = 1 ] && { log "quit requested$([ $PLAYER_QUIT = 1 ] && echo ' (by the player)')${QW_WHY:+ (the game $QW_WHY)}"; break; }
  [ -s "$RUN/AUDIO_GUARD" ] && { log "!!! AUDIO GUARD: $(head -1 "$RUN/AUDIO_GUARD")"; break; }
  sleep 0.5
done
exit 0   # the EXIT trap: the game, gamescope, the save lines, the slot, the summary
