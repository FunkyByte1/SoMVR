#!/bin/bash
# headless_win.sh - one headless boot of the WINDOWS build under Proton (docs/re-winport.md 4, 7.1): the somvr DLL as
# xinput9_1_0 in our own prefix's system32 (never in the game folder), the Linux Vulkan layer and Monado's null
# compositor as headless_vr.sh does, a headless gamescope for the window, no audio driver at all (winepulse/winealsa
# disabled: nothing can reach a real output), the bridge file between the DLL and the layer. Run it OUTSIDE the Bash
# sandbox. The caller holds the game slot (/tmp/somvr-boot.lock, logs/coord/winport/common.md) for every mode but xinput.
#   headless_win.sh MOD --test xinput           the stand-in program: DLL load, gate, entry, bridge file (no GPU, no XR)
#   headless_win.sh MOD --test d3d SECONDS      the stand-in program drawing through DXVK in gamescope, with the layer
#                                               and Monado (the bridge, the locate thread, the interop)
#   headless_win.sh MOD --game SECONDS          the GOG game (WIN_GAME_DIR, read only, run in place)
#   MOD = /ABS/PATH/TO/mod/somvr (built: build/win/xinput9_1_0.dll, build/win/d3dtest.exe, vklayer/build/...)
# Environment: WIN_PREFIX (default logs/winport/prefix: ours alone, never the user's Heroic prefix), WIN_PROTON
#   (default Steam's Proton - Experimental), WIN_GAME_DIR (default /home/bara/Games/Heroic/Shadow of Mordor),
#   WIN_NO_XR=1 (no layer, no Monado: the DLL alone), WIN_SIZE (1920x1080), SOMVR_EXTRA ("K=V K=V" for the game).
#   WIN_CMDS=FILE (game mode): commands run from the launch, one per line (boot_check's language, a subset):
#   shot NAME | key KEY... (xdotool key) | xdo ARGS... | mark LABEL | wait SECS | waitlog SECS TEXT (fixed text in
#   somvr.log after the last match) | trigger [WORDS] (the SOMVR_TRIGGER file's content) | untrigger | xrtest LINE
#   [| LINE...] (a new SOMVR_XR_TEST file: `head X Y Z ...`, `btn R a 1`, `dump`)
#   | quit. WIN_SAVE_FROM=FILE (game mode; default none): a COPY of that save (read only) as the prefix's
#   Documents/WB Games/Shadow of Mordor/Saves/GameData.sav before the run; its sha256 before/after in the summary.
# Output: logs/winport/run-<ts>/ (somvr.log, bridge, wine.log, monado.log, gamescope.log, summary.txt).
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; RT=/run/user/$(id -u)
MOD=${1:?usage: headless_win.sh /ABS/PATH/TO/mod/somvr --test xinput|d3d S | --game S}; shift
MOD=$(cd "$MOD" 2>/dev/null && pwd -P) || { echo "STOP: no folder $MOD"; exit 1; }
MODE=${1:?mode}; shift
case $MODE in
  --test) KIND=${1:?xinput|d3d}; shift; SECS=${1:-0}; [ "$KIND" = d3d ] && shift ;;
  --game) KIND=game; SECS=${1:?seconds}; shift ;;
  *) echo "STOP: mode $MODE"; exit 1 ;;
esac
[[ $SECS =~ ^[0-9]+$ ]] || { echo "STOP: seconds must be whole"; exit 1; }
DLL=$MOD/build/win/xinput9_1_0.dll; TEST=$MOD/build/win/d3dtest.exe; LAYER=$MOD/vklayer
[ -f "$DLL" ] && [ -f "$TEST" ] || { echo "STOP: $MOD/build/win is not built (win/build.sh --check)"; exit 1; }
STEAM=/home/bara/.local/share/Steam
PROTON=${WIN_PROTON:-"$STEAM/steamapps/common/Proton - Experimental/proton"}
PFX=${WIN_PREFIX:-$SOM/logs/winport/prefix}
GAME_DIR=${WIN_GAME_DIR:-"/home/bara/Games/Heroic/Shadow of Mordor"}
case $PFX in *Heroic*|*/compatdata/*|*/Games/*) echo "STOP: $PFX looks like the user's prefix"; exit 1 ;; esac
XR=1; [ "${WIN_NO_XR:-}" = 1 ] || [ "$KIND" = xinput ] && XR=0
SIZE=${WIN_SIZE:-1920x1080}; W=${SIZE%x*}; H=${SIZE#*x}
TS=$(date +%Y%m%d-%H%M%S); RUN=$SOM/logs/winport/run-$TS; mkdir -p "$RUN"
echo "RUN=$RUN"
say() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$RUN/summary.txt"; }

# ---- the guards (every mode that uses the GPU or a runtime) ----------------------------------------------------
if [ "$KIND" != xinput ]; then
  [ -d /tmp/somvr-boot.lock ] || { echo "STOP: take the game slot first (mkdir /tmp/somvr-boot.lock)"; exit 1; }
  systemctl --user is-active -q somvr-headset && { echo "STOP: the user's headset session runs"; exit 1; }
  grep -q '"user": false' "$RT/vr-session/status.json" 2>/dev/null || { echo "STOP: the user is in VR (status.json)"; exit 1; }
  pgrep -x monado-service > /dev/null && { echo "STOP: a monado-service runs"; exit 1; }
  pgrep -f '[H]and of Fate 2\.' > /dev/null && { echo "STOP: Hand of Fate 2 runs"; exit 1; }
  journalctl -k -b --no-pager 2>/dev/null | grep -qi 'amdgpu.*\(GPU reset\|VRAM is lost\)' &&
    { echo "STOP: an amdgpu reset in this boot's kernel log"; exit 1; }
fi
if [ "$KIND" = game ]; then
  EXE="$GAME_DIR/x64/ShadowOfMordor.exe"; [ -f "$EXE" ] || EXE="$GAME_DIR/ShadowOfMordor.exe"
  [ -f "$EXE" ] || { echo "STOP: no ShadowOfMordor.exe in $GAME_DIR (the GOG download)"; exit 1; }
fi

# ---- the prefix: ours, made once by Proton; the DLL into its system32 before every run ----------------------------
# The programs run on Proton's own wine directly, never through Proton's steam.exe (that one talks to the running
# Steam client and initialises OpenVR - the user's OpenComposite - and hangs a console program here): no Steam at all.
PDIR=$(dirname "$PROTON")/files; WINE=$PDIR/bin/wine; WSRV=$PDIR/bin/wineserver
export STEAM_COMPAT_DATA_PATH=$PFX STEAM_COMPAT_CLIENT_INSTALL_PATH=$STEAM
mkdir -p "$PFX"
CLEAN=(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u LD_PRELOAD -u VK_LAYER_PATH -u VK_INSTANCE_LAYERS
       -u SteamAppId -u SteamGameId -u STEAM_COMPAT_APP_ID XDG_CONFIG_HOME="$RUN/xdg-config" WINEPREFIX="$PFX/pfx")
mkdir -p "$RUN/xdg-config"
if [ ! -d "$PFX/pfx/drive_c/windows/system32" ]; then
  say "making the prefix $PFX (once, Proton's setup: DXVK and vkd3d into its system32)"
  "${CLEAN[@]}" WINEDEBUG=-all timeout 900 "$PROTON" run cmd.exe /c exit > "$RUN/prefix-init.log" 2>&1 < /dev/null
  "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
fi
SYS32=$PFX/pfx/drive_c/windows/system32
[ -d "$SYS32" ] && [ -f "$SYS32/d3d11.dll" ] || { say "STOP: no prefix system32 with DXVK (see prefix-init.log)"; exit 1; }
install -m 0644 "$DLL" "$SYS32/xinput9_1_0.dll"
cmp -s "$DLL" "$SYS32/xinput9_1_0.dll" || { say "STOP: the DLL did not land in $SYS32"; exit 1; }
say "DLL $(sha256sum "$DLL" | cut -c1-16) in $SYS32/xinput9_1_0.dll"

# ---- the save (game mode): a copy into our prefix; the user's file is only read ---------------------------------
SAVES="$PFX/pfx/drive_c/users/steamuser/Documents/WB Games/Shadow of Mordor/Saves"
if [ "$KIND" = game ] && [ -n "${WIN_SAVE_FROM:-}" ]; then
  [ -f "$WIN_SAVE_FROM" ] || { say "STOP: no save $WIN_SAVE_FROM"; exit 1; }
  case $(realpath "$WIN_SAVE_FROM") in "$PFX"/*) ;; *) ;; esac
  mkdir -p "$SAVES"
  cp --no-preserve=mode "$WIN_SAVE_FROM" "$SAVES/GameData.sav.tmp" && mv -f "$SAVES/GameData.sav.tmp" "$SAVES/GameData.sav"
  SAVE_SHA0=$(sha256sum "$SAVES/GameData.sav" | cut -c1-16)
  say "save: a copy of $WIN_SAVE_FROM ($(sha256sum "$WIN_SAVE_FROM" | cut -c1-16)) as $SAVES/GameData.sav"
fi

# ---- the environment of the program ----------------------------------------------------------------------------
# audio: only ALSA's null plugin, inside a namespace without /dev/snd and without the PulseAudio / PipeWire sockets
# (bwrap below): the game has a sound device (Wwise wants one) and nothing can reach a real output
printf 'pcm.!default { type null }\nctl.!default { type hw card 0 }\n' > "$RUN/asound.conf"
GENV=(WINEDLLOVERRIDES="xinput9_1_0=n,b;d3d11,d3d10core,dxgi,d3d9=n;winepulse.drv=d;wineoss.drv=d;winealsa.drv=b"
      ALSA_CONFIG_PATH="$RUN/asound.conf"
      WINEDEBUG="${WINEDEBUG:--all,err+module}" SOMVR_LOG="$RUN/somvr.log" SOMVR_BRIDGE="$RUN/bridge"
      DXVK_LOG_PATH="$RUN" DXVK_LOG_LEVEL=info SDL_AUDIODRIVER=dummy SOMVR_TRIGGER="$RUN/somvr.trigger"
      SOMVR_XR_TEST="$RUN/xrtest.txt")
[ "$KIND" = game ] || GENV+=(SOMVR_WIN_TEST=1)
if [ $XR = 1 ]; then
  GENV+=(VK_LAYER_PATH="$LAYER" VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
         XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json SOMVR_XR_LOG="$RUN/vklayer.log")
  [ "$KIND" = game ] && GENV+=(SOMVR_XR=1) || GENV+=(SOMVR_XR=force)
fi
# WIN_ENV_FILE (game mode): the headset build's switches (scripts/headset-buildN.env: K=V lines), then SOMVR_EXTRA
if [ "$KIND" = game ] && [ -n "${WIN_ENV_FILE:-}" ]; then
  while IFS= read -r kv; do
    case $kv in ''|'#'*) continue ;; SOMVR_LOG=*|SOMVR_XR_LOG=*|SOMVR_TRIGGER=*|SOMVR_XR_TEST=*|SOMVR_BRIDGE=*) continue ;; esac
    [[ $kv =~ ^[A-Z_][A-Z0-9_]*= ]] && GENV+=("$kv")
  done < "$WIN_ENV_FILE"
fi
for kv in ${SOMVR_EXTRA:-}; do GENV+=("$kv"); done
printf '%s\n' "${GENV[@]}" > "$RUN/env.txt"

MSVC=""; GS=""
cleanup() {
  [ -n "$GS" ] && kill -TERM "$GS" 2>/dev/null
  if [ -n "$MSVC" ]; then kill -INT "$MSVC" 2>/dev/null; for i in $(seq 1 25); do kill -0 "$MSVC" 2>/dev/null || break; sleep 0.2; done
    kill -0 "$MSVC" 2>/dev/null && kill -TERM "$MSVC"; fi
  "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
}
trap cleanup EXIT

# ---- Monado's null compositor (headless_vr.sh's way) -------------------------------------------------------------
if [ $XR = 1 ]; then
  M=$RUN/mxdg; mkdir -p "$M/config" "$M/cache" "$M/data"
  setsid chrt -o 0 env -i HOME="$M" PATH=/usr/bin:/bin XDG_RUNTIME_DIR=$RT XDG_CONFIG_HOME="$M/config" \
    XDG_CACHE_HOME="$M/cache" XDG_DATA_HOME="$M/data" SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
    XRT_LOG=info VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
    timeout -s INT -k 10 $((SECS + 300)) /usr/bin/monado-service > "$RUN/monado.log" 2>&1 < /dev/null &
  MSVC=$!
  for i in $(seq 1 50); do [ -S $RT/monado_comp_ipc ] && break; sleep 0.2; done
  [ -S $RT/monado_comp_ipc ] || { say "STOP: monado did not come up"; exit 1; }
  say "monado null compositor pid $MSVC"
fi

# ---- the window: a headless gamescope (d3d, game) ------------------------------------------------------------
DISP=()
if [ "$KIND" != xinput ]; then
  setsid "${CLEAN[@]}" XDG_RUNTIME_DIR="$RT" gamescope --backend headless -W "$W" -H "$H" -w "$W" -h "$H" -r 60 -- \
    sh -c 'env > "$1.tmp" && mv -f "$1.tmp" "$1" && exec tail -f /dev/null' vd "$RUN/vdisplay.env" \
    > "$RUN/gamescope.log" 2>&1 < /dev/null &
  GS=$!
  for i in $(seq 1 50); do [ -s "$RUN/vdisplay.env" ] && break; sleep 0.2; done
  [ -s "$RUN/vdisplay.env" ] || { say "STOP: gamescope did not come up"; exit 1; }
  DISP=(DISPLAY="$(sed -n 's/^DISPLAY=//p' "$RUN/vdisplay.env")")
  GWD=$(sed -n 's/^GAMESCOPE_WAYLAND_DISPLAY=//p' "$RUN/vdisplay.env" | head -1)
  say "gamescope pid $GS ${DISP[*]} $GWD"
fi

# ---- the program ----------------------------------------------------------------------------------------------
case $KIND in
  xinput) CMD=("$TEST" xinput); TMO=120 ;;
  d3d) CMD=("$TEST" d3d "$SECS" "$W" "$H"); TMO=$((SECS + 120)) ;;
  game) CMD=("$EXE"); TMO=$((SECS + 60)) ;;
esac
say "run ${CMD[*]} (timeout $TMO s)"
T0=$(date +%s)
# the program's folder in a mount namespace (bwrap over the whole system): the stand-in's read-only; the game's install
# as an overlay whose writes land in $RUN/game-upper (the GOG folder itself is never written, whatever the game tries;
# the summary lists what it wrote)
RO=$(dirname "${CMD[0]}")
MUTE=(--tmpfs /dev/snd --tmpfs "$RT/pulse")   # no sound card, no PulseAudio socket ...
for so in "$RT"/pipewire-0 "$RT"/pipewire-0-manager; do [ -S "$so" ] && MUTE+=(--ro-bind /dev/null "$so"); done  # ... no PipeWire
if [ "$KIND" = game ]; then
  mkdir -p "$RUN/game-upper" "$RUN/game-work"
  NS=(bwrap --dev-bind / / --overlay-src "$GAME_DIR" --overlay "$RUN/game-upper" "$RUN/game-work" "$GAME_DIR" "${MUTE[@]}")
  "${NS[@]}" -- sh -c 'touch "$1/.somvr-ns-probe"' p "$GAME_DIR" && [ ! -e "$GAME_DIR/.somvr-ns-probe" ] &&
    [ -e "$RUN/game-upper/.somvr-ns-probe" ] || { say "STOP: the overlay over $GAME_DIR did not hold"; exit 1; }
  rm -f "$RUN/game-upper/.somvr-ns-probe"
  say "the install is an overlay for the run (writes -> $RUN/game-upper): $GAME_DIR"
  "${NS[@]}" -- sh -c '[ -z "$(ls /dev/snd)" ] && [ ! -S "$1/pipewire-0" ] && [ -z "$(ls "$1/pulse")" ]' p "$RT" ||
    { say "STOP: the namespace still sees a sound device or socket"; exit 1; }
  say "audio: ALSA null only (no /dev/snd, no pulse/pipewire socket in the namespace)"
else
  NS=(bwrap --dev-bind / / --ro-bind "$RO" "$RO" "${MUTE[@]}")
  "${NS[@]}" -- sh -c 'touch "$1/.somvr-rw-probe" 2>/dev/null' ro "$RO" &&
    { rm -f "$RO/.somvr-rw-probe"; say "STOP: bwrap did not make $RO read-only"; exit 1; }
  say "the program's folder is read-only for the run: $RO"
fi
# (the working folder is entered inside the namespace: a cd outside would hand the game the real folder's inode)
( cd / && setsid "${CLEAN[@]}" "${DISP[@]}" "${GENV[@]}" timeout -s TERM -k 15 "$TMO" \
    "${NS[@]}" --chdir "$(dirname "${CMD[0]}")" -- "$WINE" "${CMD[@]}" > "$RUN/wine.log" 2>&1 < /dev/null ) &
PP=$!
shot() {   # shot NAME -> shots/NAME.png (the headless gamescope's screen)
  mkdir -p "$RUN/shots"
  GAMESCOPE_WAYLAND_DISPLAY="$GWD" XDG_RUNTIME_DIR="$RT" timeout 10 gamescopectl screenshot "$RUN/shots/$1.png" \
    >> "$RUN/shots.log" 2>&1
  for i in $(seq 1 30); do [ -s "$RUN/shots/$1.png" ] && break; sleep 0.2; done
  [ -s "$RUN/shots/$1.png" ] && say "shot shots/$1.png" || say "shot $1 failed"
}
if [ "$KIND" != xinput ]; then   # pictures every 30 s while the program runs
  ( n=0; while [ $n -lt $(( (SECS + 29) / 30 )) ]; do sleep 30; n=$((n + 1))
      shot "t$((n * 30))"; kill -0 $PP 2>/dev/null || say "(the launch process $PP has ended; pictures go on)"; done ) &
  SHOTS=$!
fi
TG=$(date +%s); CUR=0
gel() { echo $(( $(date +%s) - TG )); }
game_alive() { pgrep -f 'ShadowOfMordor\.exe' > /dev/null; }
xd() { env -u XAUTHORITY "${DISP[@]}" xdotool "$@" >> "$RUN/input.log" 2>&1; }
waitlog() {   # SECS TEXT: a line holding TEXT after the last match
  local secs=$1 end n; shift
  end=$(( $(date +%s) + secs ))
  while :; do
    n=$(grep -n -F -- "$*" "$RUN/somvr.log" 2>/dev/null | awk -F: -v c=$CUR '$1 > c { print $1; exit }')
    [ -n "$n" ] && { CUR=$n; say "waitlog '$*' at $(gel) s: $(sed -n "${n}p" "$RUN/somvr.log" | cut -c1-120)"; return 0; }
    [ "$(date +%s)" -ge "$end" ] && { say "!!! waitlog: no '$*' within $secs s"; return 1; }
    game_alive || return 1
    sleep 0.5
  done
}
run_cmd() {
  local c=$1; shift
  case $c in
    shot) shot "${1:-manual-$(gel)s}" ;;
    key) xd key --delay 120 "$@"; say "key $* at $(gel) s" ;;
    xdo) xd "$@" ;;
    mark) say "MARK $* at $(gel) s" ;;
    wait) sleep "${1:-1}" ;;
    waitlog) waitlog "$@" ;;
    trigger) printf '%s' "$*" > "$RUN/somvr.trigger"; say "trigger $* at $(gel) s" ;;
    untrigger) rm -f "$RUN/somvr.trigger" ;;
    xrtest) { echo "# $(date +%s%N)"; echo "$*" | sed 's/ | /\n/g'; } > "$RUN/xrtest.tmp" && mv -f "$RUN/xrtest.tmp" "$RUN/xrtest.txt"
      say "xrtest $* at $(gel) s" ;;   # a new file each time (the layer runs each new file's lines once); ' | ' = a new line
    quit) return 9 ;;
    ''|'#'*) ;;
    *) say "unknown command: $c $*" ;;
  esac
}
if [ "$KIND" = game ]; then
  if [ -n "${WIN_CMDS:-}" ]; then
    while IFS= read -r line; do
      [ "$(gel)" -ge "$SECS" ] && { say "the hold is over during the commands"; break; }
      game_alive || { say "the game is gone at $(gel) s"; break; }
      run_cmd $line; [ $? = 9 ] && break
    done < "$WIN_CMDS"
  fi
  while [ "$(gel)" -lt "$SECS" ] && game_alive; do sleep 2; done
  say "the hold ($SECS s) is over: ending the game"
  "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
fi
wait $PP; RC=$?
[ -n "${SHOTS:-}" ] && kill $SHOTS 2>/dev/null
say "program exit $RC after $(( $(date +%s) - T0 )) s"
grep -m3 "d3dtest:" "$RUN/wine.log" | sed 's/^/  /' | tee -a "$RUN/summary.txt"
[ -f "$RUN/somvr.log" ] && say "somvr.log: $(grep -c '' "$RUN/somvr.log") lines, $(grep -c '^WIN ' "$RUN/somvr.log") WIN lines"
[ -f "$RUN/bridge" ] && say "bridge: $(grep -c '^somvr_' "$RUN/bridge") objects, $(grep '^nonce' "$RUN/bridge")"
[ -n "${SAVE_SHA0:-}" ] && say "save: the prefix copy $SAVE_SHA0 -> $(sha256sum "$SAVES/GameData.sav" 2>/dev/null | cut -c1-16) (the user's file was only read)"
[ -d "$RUN/game-upper" ] && say "the game wrote into its folder (kept in game-upper): $(cd "$RUN/game-upper" && find . -type f | head -20 | tr '\n' ' ')"
[ -f "$RUN/vklayer.log" ] && say "vklayer.log: $(grep -c '' "$RUN/vklayer.log") lines"
exit $RC
