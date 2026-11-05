#!/bin/bash
# headless_win.sh - one headless boot of the WINDOWS build under Proton (docs/re-winport.md 4, 7.1): the somvr DLL as
# xinput9_1_0 in our own prefix's system32 (never in the game folder), the Linux Vulkan layer and Monado's null
# compositor as headless_vr.sh does, a headless gamescope for the window, no audio driver at all (winepulse/winealsa
# disabled: nothing can reach a real output), the bridge file between the DLL and the layer. Run it OUTSIDE the Bash
# sandbox. The caller holds the game slot (/tmp/somvr-boot.lock, logs/coord/winport/common.md) for every mode but xinput
# and compat (those two run in their own prefix, logs/winport/prefix-xinput, one at a time: no boot's wineserver shared).
#   headless_win.sh MOD --test xinput           the stand-in program: DLL load, gate, entry, bridge file (no GPU, no XR)
#   headless_win.sh MOD --test compat           build/win/test_compat.exe: win/somvr_win_compat.c's stand-ins (exit 0 = ok)
#   headless_win.sh MOD --test d3d SECONDS      the stand-in program drawing through DXVK in gamescope, with the layer
#                                               and Monado (the bridge, the locate thread, the interop)
#   headless_win.sh MOD --game SECONDS          the GOG game (WIN_GAME_DIR, read only, run in place)
#   MOD = /ABS/PATH/TO/mod/somvr (built: build/win/xinput9_1_0.dll, build/win/d3dtest.exe, vklayer/build/...)
# Environment: WIN_PREFIX (default logs/winport/prefix: ours alone, never the user's Heroic prefix), WIN_PROTON
#   (default Steam's Proton - Experimental), WIN_GAME_DIR (default /home/bara/Games/Heroic/Shadow of Mordor),
#   WIN_NO_XR=1 (no layer, no Monado: the DLL alone), WIN_SIZE (1920x1080), SOMVR_EXTRA ("K=V K=V" for the game),
#   WIN_SHOT_S (the flat pictures' interval, default 30 s; 0 = none: a long soak's disk).
#   WIN_MONADO_ENV="K=V ..." (b37 lane2; headless_vr.sh's HEADLESS_MONADO_ENV): more environment for monado-service,
#   U_PACING_* / XRT_COMPOSITOR_* only - U_PACING_APP_IMMEDIATE_WAIT_FRAME_RETURN=1: xrWaitFrame returns at once, the game
#   paced by its own frame limit, not the compositor's (the null compositor's 20 Hz still holds it: its images come at 20 Hz).
#   WIN_MONADO_RATE=HZ (b37 lane2; e.g. 90): Monado's real compositor instead of the null one - its window in a headless
#   gamescope of its own (-r HZ), XRT_COMPOSITOR_FORCE_XCB=1 and XRT_COMPOSITOR_DEFAULT_FRAMERATE=HZ: the game paced at
#   HZ as by the headset's runtime (a frame every 11.1 ms at 90), the compositor's GPU pass included. Default unset.
#   WIN_CMDS=FILE (game mode): commands run from the launch, one per line (boot_check's language, a subset):
#   shot NAME | key KEY... (xdotool key) | xdo ARGS... | mark LABEL | wait SECS | waitlog SECS TEXT (fixed text in
#   somvr.log after the last match) | trigger [WORDS] (the SOMVR_TRIGGER file's content) | untrigger | xrtest LINE
#   [| LINE...] (a new SOMVR_XR_TEST file: `head X Y Z ...`, `btn R a 1`, `dump`)
#   | driver SCRIPT [K=V...] ('~' in V = a space; a Linux driver, scripts/SCRIPT, on this run: the run is linked as logs/boot-check-TS-win
#   with a launch-cmd.txt naming the worktree; BG_RUN/P18_RUN set) | quit (also a `quit` line in RUN/cmd, the drivers'
#   way). WIN_SAVE_FROM=FILE (game mode; default none): a COPY of that save (read only) as the prefix's
#   Documents/WB Games/Shadow of Mordor/Saves/GameData.sav before the run; its sha256 before/after in the summary.
# Output: logs/winport/run-<ts>/ (somvr.log, bridge, wine.log, monado.log, gamescope.log, summary.txt).
# The drivers' boot (winport-drivers, docs/re-winport.md 7.11; `headless_vr.sh --win MOD` sets these): WIN_RUN=DIR
#   (game mode) the run folder instead (logs/boot-check-<ts>: where scripts/*-drive.sh look; under logs/, new). In
#   every game run: RUN/launch-cmd.txt (the argv, `# game only (--game-env): ...` = what the drivers grep for their
#   build), the Linux names beside ours (game.log -> wine.log, run.log -> summary.txt; the layer's log is xr-layer.log),
#   SOM_BOOTCHECK_RUN=RUN (the mod's quit.request and marks/), @RUN@ in WIN_ENV_FILE / SOMVR_EXTRA values = RUN
#   (SOMVR_LOG / SOMVR_XR_LOG / SOMVR_TRIGGER / SOMVR_BRIDGE stay the harness's), boot_check's command mode: WIN_CMDS
#   goes into RUN/cmd at the launch and lines appended to RUN/cmd later run too (done ones in cmd.done; `quit` ends the
#   run), the player's requests served every 0.5 s (RUN/quit.request = a clean stop; RUN/marks/<id>/want -> served,
#   flat.png and the log tails, never a voice), at every stop the word `endlogs` first (the END lines), and the save
#   guard's lines (`save after: GameData.sav UNCHANGED ...`: the user's file, only read, by its sha256).
# b35 slots (som-b35-slots, docs/re-xr-pipeline.md 158; scripts/game_slot.sh): WIN_SLOT=N (default 1; headless_vr.sh's
#   HEADLESS_SLOT) = the game slot the caller holds: 1 = /tmp/somvr-boot.lock with logs/winport/prefix and Monado/gamescope
#   in $XDG_RUNTIME_DIR, all exactly as before; N >= 2 = /tmp/somvr-slotN.lock with its own prefix
#   (logs/winport/prefix-slotN, a reflink copy made once) and runtime folder ($XDG_RUNTIME_DIR/somvr-slotN: Monado's
#   socket, gamescope's, the game's XDG_RUNTIME_DIR). Every slot's game stops at once when the user goes into VR (the
#   headset unit, status.json, a WiVRn app) or the kernel logs an amdgpu reset; the game's own processes are told apart
#   from other slots' by their SOMVR_LOG (this run's).
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; RT=/run/user/$(id -u)
HERE=$(cd "$(dirname "$0")" && pwd)   # the scripts next to this one (a worktree's own drivers)
MOD=${1:?usage: headless_win.sh /ABS/PATH/TO/mod/somvr --test xinput|d3d S | --game S}; shift
MOD=$(cd "$MOD" 2>/dev/null && pwd -P) || { echo "STOP: no folder $MOD"; exit 1; }
MODE=${1:?mode}; shift
case $MODE in
  --test) KIND=${1:?xinput|d3d|compat}; shift; SECS=${1:-0}; [ "$KIND" = d3d ] && shift
    case $KIND in xinput|d3d|compat) ;; *) echo "STOP: --test $KIND"; exit 1 ;; esac ;;
  --game) KIND=game; SECS=${1:?seconds}; shift ;;
  *) echo "STOP: mode $MODE"; exit 1 ;;
esac
[[ $SECS =~ ^[0-9]+$ ]] || { echo "STOP: seconds must be whole"; exit 1; }
DLL=$MOD/build/win/xinput9_1_0.dll; TEST=$MOD/build/win/d3dtest.exe; LAYER=$MOD/vklayer
[ -f "$DLL" ] && [ -f "$TEST" ] || { echo "STOP: $MOD/build/win is not built (win/build.sh --check)"; exit 1; }
STEAM=/home/bara/.local/share/Steam
PROTON=${WIN_PROTON:-"$STEAM/steamapps/common/Proton - Experimental/proton"}
PFX=${WIN_PREFIX:-$SOM/logs/winport/prefix}
# WIN_W1=1 (below) runs in its own copy, logs/winport/prefix-w1 (a reflink of the prefix, made once): its registry
# entries (the layer's ExplicitLayers, wineopenxr's Software\Wine\VR) never reach the prefix other runs copy
[ "${WIN_W1:-0}" = 1 ] && [ -z "${WIN_PREFIX:-}" ] && PFX=$SOM/logs/winport/prefix-w1
# the slot-free modes (xinput, compat: no game slot held) run in their own copy too, logs/winport/prefix-xinput: in the
# shared prefix their wine would join a running boot's wineserver and their `wineserver -k` end that boot's game
SLOTFREE=0; case $KIND in xinput|compat) SLOTFREE=1 ;; esac
[ $SLOTFREE = 1 ] && [ -z "${WIN_PREFIX:-}" ] && PFX=$SOM/logs/winport/prefix-xinput
# b35 slots: the slot's lock, prefix and runtime folder (SRT: Monado's and gamescope's XDG_RUNTIME_DIR)
. "$HERE/game_slot.sh"
SLOT=${WIN_SLOT:-1}; [[ $SLOT =~ ^[1-9]$ ]] && [ "$SLOT" -le "$GS_SLOTS" ] || { echo "STOP: WIN_SLOT=$SLOT (1..$GS_SLOTS)"; exit 1; }
LOCKD=$(gs_lock "$SLOT"); SRT=$(gs_rtdir "$SLOT")
if [ "$SLOT" -ge 2 ]; then
  [ "${WIN_W1:-0}" = 1 ] && { echo "STOP: WIN_W1 runs in slot 1 only (its own prefix-w1)"; exit 1; }
  [ -z "${WIN_PREFIX:-}" ] && PFX=$SOM/logs/winport/prefix-slot$SLOT
fi
# b37 (som-b37-slotsvram, docs/re-xr-pipeline.md 158.5): this game run's render mode - WIN_RENDER=vr|light, else what
# the slot was taken for (its lock's render file, game_slot.sh take), else the slot's default (1 vr, 2.. light);
# light = the VR preset with game_slot.sh's GS_RENDER_LIGHT over it (WIN_RENDER_LIGHT: another set, for measuring)
WRM=""; WRM_WHY=""
if [ "$KIND" = game ]; then
  if [ -n "${WIN_RENDER:-}" ]; then WRM=$WIN_RENDER; WRM_WHY=WIN_RENDER
  elif WRM=$(cat "$LOCKD/render" 2>/dev/null) && [ -n "$WRM" ]; then WRM_WHY="slot $SLOT's take"
  else WRM=$(gs_render_default "$SLOT"); WRM_WHY="slot $SLOT's default"; fi
  case $WRM in vr|light) ;; *) echo "STOP: WIN_RENDER=$WRM (vr or light)"; exit 1 ;; esac
fi
GAME_DIR=${WIN_GAME_DIR:-"/home/bara/Games/Heroic/Shadow of Mordor"}
case $PFX in *Heroic*|*/compatdata/*|*/Games/*) echo "STOP: $PFX looks like the user's prefix"; exit 1 ;; esac
XR=1; [ "${WIN_NO_XR:-}" = 1 ] || [ $SLOTFREE = 1 ] && XR=0
MONENV=()
for kv in ${WIN_MONADO_ENV:-}; do
  [[ $kv =~ ^(U_PACING_|XRT_COMPOSITOR_)[A-Z0-9_]*=[A-Za-z0-9._-]*$ ]] || { echo "STOP: WIN_MONADO_ENV: $kv is not U_PACING_*=V or XRT_COMPOSITOR_*=V"; exit 1; }
  MONENV+=("$kv")
done
SIZE=${WIN_SIZE:-1920x1080}; W=${SIZE%x*}; H=${SIZE#*x}
TS=$(date +%Y%m%d-%H%M%S); RUN=$SOM/logs/winport/run-$TS
for i in 1 2 3 4 5; do [ -e "$RUN" ] || break; sleep 1; TS=$(date +%Y%m%d-%H%M%S); RUN=$SOM/logs/winport/run-$TS; done  # b35 slots
if [ -n "${WIN_RUN:-}" ]; then   # the drivers' place (headless_vr.sh --win): a new folder under logs/
  [ "$KIND" = game ] || { echo "STOP: WIN_RUN is for --game"; exit 1; }
  case $WIN_RUN in "$SOM"/logs/?*) ;; *) echo "STOP: WIN_RUN must be under $SOM/logs/"; exit 1 ;; esac
  [ -e "$WIN_RUN" ] && { echo "STOP: WIN_RUN $WIN_RUN exists"; exit 1; }
  RUN=$WIN_RUN
fi
mkdir "$RUN" || { echo "STOP: $RUN exists (another slot's boot of the same second?)"; exit 1; }   # b35 slots: atomic
# the Linux drivers (scripts/*-drive.sh) find their run as the newest logs/boot-check-* whose launch-cmd.txt names their
# worktree, and use RUN/somvr.log, somvr.trigger, xrtest.txt, xr-layer.log, cmd: the run looks like one for them
printf '# headless_win.sh %s (the Windows build; MOD %s%s)\n' "$TS" "$MOD" "${WRM:+; slot $SLOT; render $WRM}" > "$RUN/launch-cmd.txt"
# (WIN_RUN: the run IS logs/boot-check-<ts>, no link; headless_vr.sh --win)
if [ -n "${WIN_RUN:-}" ]; then BCL=$RUN; else BCL=$SOM/logs/boot-check-$TS-win; ln -sfn "$RUN" "$BCL"; fi; : > "$RUN/cmd"
echo "RUN=$RUN"
say() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$RUN/summary.txt"; }

# ---- the guards (every mode that uses the GPU or a runtime) ----------------------------------------------------
if [ $SLOTFREE = 1 ]; then   # two slot-free runs at once would share (and end) one wineserver: one at a time
  exec 8> "$SOM/logs/winport/.prefix-xinput.lock"
  flock -w 300 8 || { echo "STOP: another slot-free run held $PFX for 5 minutes"; exit 1; }
else
  [ -d "$LOCKD" ] || { echo "STOP: take the game slot first (mkdir $LOCKD; scripts/game_slot.sh)"; exit 1; }
  systemctl --user is-active -q somvr-headset && { echo "STOP: the user's headset session runs"; exit 1; }
  grep -q '"user": false' "$RT/vr-session/status.json" 2>/dev/null || { echo "STOP: the user is in VR (status.json)"; exit 1; }
  # build 35: the whole VR gate - also a user's launcher that is starting (its solo request user-session, game_slot.sh)
  gw=$(gs_vr_why) && { echo "STOP: $gw"; exit 1; }
  # b35 slots: no Monado on this slot's runtime folder (other slots' Monados have their own; an unreadable one counts)
  for mp in $(pgrep -x monado-service); do
    me=$(tr '\0' '\n' < /proc/$mp/environ 2>/dev/null | sed -n 's/^XDG_RUNTIME_DIR=//p'); [ -n "$me" ] || me=$RT
    [ "${me%/}" = "$SRT" ] && { echo "STOP: a monado-service runs on $SRT (pid $mp)"; exit 1; }
  done
  gs_alive "$LOCKD" && { echo "STOP: slot $SLOT already runs a game (pid $(cat "$LOCKD/pid"))"; exit 1; }
  if [ "$SLOT" -ge 2 ]; then   # b35 slots: never a prefix in use (slot 1's base prefix, another boot's WIN_PREFIX)
    [ "$PFX" = "$SOM/logs/winport/prefix" ] && { echo "STOP: logs/winport/prefix is slot 1's (WIN_PREFIX)"; exit 1; }
    for wp in $(pgrep -x wineserver); do
      gs_env_has "$wp" "WINEPREFIX=$PFX/pfx" && { echo "STOP: the prefix $PFX is in use (wineserver $wp)"; exit 1; }
    done
  fi
  pgrep -f '[H]and of Fate 2\.' > /dev/null && { echo "STOP: Hand of Fate 2 runs"; exit 1; }
  journalctl -k -b --no-pager 2>/dev/null | grep -qi 'amdgpu.*\(GPU reset\|VRAM is lost\)' &&
    { echo "STOP: an amdgpu reset in this boot's kernel log"; exit 1; }
  if [ -n "$WRM" ]; then   # b37: the slot's mode for the other takers' VRAM gate (a vr boot in a slot taken for light:
    tm=$(cat "$LOCKD/render" 2>/dev/null)   # only if vr fits now)
    lo=$(head -1 "$LOCKD/owner" 2>/dev/null); lo=${lo%% *}   # (not under the holder's own solo: it holds them all)
    if [ "$WRM" = vr ] && [ "$tm" = light ] && ! { [ -n "$lo" ] && gs_names "$GS_SOLO" "$lo"; } && vw=$(gs_vram_why "$SLOT" vr); then
      echo "STOP: slot $SLOT was taken for light and this boot asks WIN_RENDER=vr, which does not fit now ($vw): take the slot with WIN_RENDER=vr"; exit 1
    fi
    echo "$WRM" > "$LOCKD/render"
  fi
  echo $$ > "$LOCKD/pid"; echo "$RUN" > "$LOCKD/run"   # b35 slots: whose game the slot runs (status, the stale rule)
  [ "$SLOT" -ge 2 ] && mkdir -p -m 0700 "$SRT"          # b35 slots: the slot's runtime folder (Monado, gamescope)
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
if [ "$SLOT" -ge 2 ] && [ -z "${WIN_PREFIX:-}" ] && [ ! -d "$PFX/pfx/drive_c/windows/system32" ]; then   # b35 slots
  say "slot $SLOT's prefix: $(gs_prefix "$SLOT")"   # (once: a reflink copy of logs/winport/prefix, not a fresh one)
  [ -d "$PFX/pfx/drive_c/windows/system32" ] || { say "STOP: slot $SLOT has no prefix $PFX"; exit 1; }
fi
mkdir -p "$PFX"
CLEAN=(env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u LD_PRELOAD -u VK_LAYER_PATH -u VK_INSTANCE_LAYERS
       -u SteamAppId -u SteamGameId -u STEAM_COMPAT_APP_ID XDG_CONFIG_HOME="$RUN/xdg-config" WINEPREFIX="$PFX/pfx")
mkdir -p "$RUN/xdg-config"
if { [ "$PFX" = "$SOM/logs/winport/prefix-w1" ] || [ "$PFX" = "$SOM/logs/winport/prefix-xinput" ]; } &&
   [ ! -d "$PFX/pfx" ] && [ -d "$SOM/logs/winport/prefix/pfx" ]; then
  say "${PFX##*/}: its own prefix, a reflink copy of logs/winport/prefix (once)"
  rmdir "$PFX" 2>/dev/null; [ -e "$PFX" ] && { say "STOP: $PFX exists without a pfx/ (left alone)"; exit 1; }
  rm -rf "$PFX.part"; cp -a --reflink=always "$SOM/logs/winport/prefix" "$PFX.part" && rm -f "$PFX.part/pfx.lock" &&
    mv "$PFX.part" "$PFX" || { say "STOP: the prefix copy ${PFX##*/} failed"; exit 1; }
fi
if [ ! -d "$PFX/pfx/drive_c/windows/system32" ]; then
  say "making the prefix $PFX (once, Proton's setup: DXVK and vkd3d into its system32)"
  "${CLEAN[@]}" WINEDEBUG=-all timeout 900 "$PROTON" run cmd.exe /c exit > "$RUN/prefix-init.log" 2>&1 < /dev/null
  "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
  [ -n "$MGS" ] && kill -TERM "$MGS" 2>/dev/null   # b37 lane2: Monado's own gamescope (after Monado)
  [ -n "$MGSRT" ] && case $MGSRT in /tmp/somvr-mgs.*) rm -rf "$MGSRT" ;; esac
fi
SYS32=$PFX/pfx/drive_c/windows/system32
[ -d "$SYS32" ] && [ -f "$SYS32/d3d11.dll" ] || { say "STOP: no prefix system32 with DXVK (see prefix-init.log)"; exit 1; }
install -m 0644 "$DLL" "$SYS32/xinput9_1_0.dll"
cmp -s "$DLL" "$SYS32/xinput9_1_0.dll" || { say "STOP: the DLL did not land in $SYS32"; exit 1; }
say "DLL $(sha256sum "$DLL" | cut -c1-16) in $SYS32/xinput9_1_0.dll"
# the split build (docs/re-xr-pipeline.md 156): the host loads somvr_logic.dll from its own folder; with
# SOMVR_DEV_RELOAD=1 (SOMVR_EXTRA) it watches the build's own file (SOMVR_DEV_RELOAD_DLL below) and swaps it in
LDLL=$MOD/build/win/somvr_logic.dll
if [ -f "$LDLL" ]; then
  install -m 0644 "$LDLL" "$SYS32/somvr_logic.dll"
  cmp -s "$LDLL" "$SYS32/somvr_logic.dll" || { say "STOP: the logic DLL did not land in $SYS32"; exit 1; }
  say "LOGIC $(sha256sum "$LDLL" | cut -c1-16) in $SYS32/somvr_logic.dll (reload watches $LDLL)"
fi

# ---- the save (game mode): a copy into our prefix; the user's file is only read ---------------------------------
SAVES="$PFX/pfx/drive_c/users/steamuser/Documents/WB Games/Shadow of Mordor/Saves"
if [ "$KIND" = game ] && [ -n "${WIN_SAVE_FROM:-}" ]; then
  [ -f "$WIN_SAVE_FROM" ] || { say "STOP: no save $WIN_SAVE_FROM"; exit 1; }
  case $(realpath "$WIN_SAVE_FROM") in "$PFX"/*) ;; *) ;; esac
  mkdir -p "$SAVES"
  cp --no-preserve=mode "$WIN_SAVE_FROM" "$SAVES/GameData.sav.tmp" && mv -f "$SAVES/GameData.sav.tmp" "$SAVES/GameData.sav"
  SAVE_SHA0=$(sha256sum "$SAVES/GameData.sav" | cut -c1-16)
  SRC_SHA0=$(sha256sum "$WIN_SAVE_FROM" | cut -d' ' -f1)
  say "save: a copy of $WIN_SAVE_FROM (${SRC_SHA0:0:16}) as $SAVES/GameData.sav"
fi

# ---- the environment of the program ----------------------------------------------------------------------------
# 11-04: winebus.sys=d - no real game controller reaches a test game (the user's Spelunky 2 pad drove suite boots at 18:41)
# audio: only ALSA's null plugin, inside a namespace without /dev/snd and without the PulseAudio / PipeWire sockets
# (bwrap below): the game has a sound device (Wwise wants one) and nothing can reach a real output
printf 'pcm.!default { type null }\nctl.!default { type hw card 0 }\n' > "$RUN/asound.conf"
GENV=(WINEDLLOVERRIDES="xinput9_1_0=n,b;d3d11,d3d10core,dxgi,d3d9=n;winepulse.drv=d;wineoss.drv=d;winealsa.drv=b;winebus.sys=d"
      ALSA_CONFIG_PATH="$RUN/asound.conf"
      WINEDEBUG="${WINEDEBUG:--all,err+module}" SOMVR_LOG="$RUN/somvr.log" SOMVR_BRIDGE="$RUN/bridge"
      DXVK_LOG_PATH="$RUN" DXVK_LOG_LEVEL=info SDL_AUDIODRIVER=dummy SOMVR_TRIGGER="$RUN/somvr.trigger"
      SOMVR_XR_TEST="$RUN/xrtest.txt" SOMVR_DEV_RELOAD_DLL="$MOD/build/win/somvr_logic.dll")
[ "$KIND" = game ] || GENV+=(SOMVR_WIN_TEST=1)
[ "$KIND" = game ] && GENV+=(SOM_BOOTCHECK_RUN="$RUN")   # the mod's quit.request and marks/ (served below)
[ "$SLOT" -ge 2 ] && GENV+=(XDG_RUNTIME_DIR="$SRT")   # b35 slots: the layer's OpenXR client finds this slot's Monado
# WIN_W1=1 (game mode): the native Windows player's stack (W1, docs/re-winport.md 3.2) instead of the Linux layer:
# win/package.sh's files in the game's x64 folder (through the overlay: the GOG folder stays untouched), DXVK's
# d3d11/dxgi, the Khronos vulkan-1.dll with our Windows layer (SOMVR_LAYER=win: the proxy sets VK_ADD_LAYER_PATH and
# VK_INSTANCE_LAYERS), the Khronos openxr_loader.dll -> the prefix's runtime = Proton's wineopenxr -> the host's
# OpenXR (XR_RUNTIME_JSON: Monado)
W1=${WIN_W1:-0}
if [ "$W1" = 1 ]; then
  [ "$KIND" = game ] || { say "STOP: WIN_W1 is for --game"; exit 1; }
  [ -f "$MOD/build/win/package/x64/vulkan-1.dll" ] || { say "STOP: no $MOD/build/win/package (win/package.sh)"; exit 1; }
  GENV[0]="WINEDLLOVERRIDES=xinput9_1_0,d3d11,dxgi,d3d10core,vulkan-1,openxr_loader=n,b;d3d9=n;winepulse.drv=d;wineoss.drv=d;winealsa.drv=b;winebus.sys=d"
  GENV+=(SOMVR_LAYER=win XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json SOMVR_XR_LOG="$RUN/xr-layer.log")
elif [ $XR = 1 ]; then
  GENV+=(VK_LAYER_PATH="$LAYER" VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
         XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json SOMVR_XR_LOG="$RUN/xr-layer.log")
  [ "$KIND" = game ] && GENV+=(SOMVR_XR=1) || GENV+=(SOMVR_XR=force)
fi
# WIN_ENV_FILE (game mode): the headset build's switches (scripts/headset-buildN.env: K=V lines), then SOMVR_EXTRA
if [ "$KIND" = game ] && [ -n "${WIN_ENV_FILE:-}" ]; then
  while IFS= read -r kv; do
    case $kv in ''|'#'*) continue ;; SOMVR_LOG=*|SOMVR_XR_LOG=*|SOMVR_TRIGGER=*|SOMVR_XR_TEST=*|SOMVR_BRIDGE=*) continue ;; esac
    [[ $kv =~ ^[A-Z_][A-Z0-9_]*= ]] && GENV+=("${kv//@RUN@/$RUN}")
  done < "$WIN_ENV_FILE"
fi
for kv in ${SOMVR_EXTRA:-}; do   # @RUN@ = the run folder (final.sh's SOMVR_XR_TEST=@RUN@/xrtest.txt); our log/channel paths stay
  case $kv in SOMVR_LOG=*|SOMVR_XR_LOG=*|SOMVR_TRIGGER=*|SOMVR_BRIDGE=*|SOM_BOOTCHECK_RUN=*) continue ;; esac
  GENV+=("${kv//@RUN@/$RUN}")
done
# som-b35-live (re-xr-pipeline.md 162): SOMVR_LIVE=on among the game's switches - the live folder (live_session.sh:
# LIVE_ROOT, default logs/live-test) for the layer and the DLL; its daemon once the game runs; stopped at the end
LIVE_ON=0
if [ "$KIND" = game ] && printf '%s\n' "${GENV[@]}" | grep -qx -E 'SOMVR_LIVE=(on|1)' && [ -x "$HERE/live_session.sh" ]; then
  LIVE_ON=1; GENV+=(SOMVR_LIVE_DIR="$("$HERE/live_session.sh" dir "$RUN")")
  # b40 live4 (re-xr-pipeline.md 162.13): a patch staged for this build before the launch is the session's module
  [ -f "$SYS32/somvr_logic.dll" ] && say "live: $("$HERE/live_session.sh" preapply "$RUN" "$MOD" "$SYS32/somvr_logic.dll" 2>&1 | tail -1)"
fi
# b37 (162.8): SOMVR_LIVE_TTS=on - the spoken answers of a test go to a null sink of this run's own (never wivrn.sink,
# never a real output), its monitor recorded to RUN/live-tts.wav (the proof of what was said); both gone at the end
LIVE_TTS=off; TTS_SINK=""; TTS_MOD=""; TTS_REC=""
if [ "$LIVE_ON" = 1 ] && printf '%s\n' "${GENV[@]}" | grep -qx -E 'SOMVR_LIVE_TTS=(on|1)'; then
  TTS_SINK=somvr_live_tts_$$
  if TTS_MOD=$(pactl load-module module-null-sink "sink_name=$TTS_SINK" "sink_properties=device.description=$TTS_SINK" 2>/dev/null) &&
     pactl list short sinks | awk -v n="$TTS_SINK" '$2 == n { f = 1 } END { exit !f }'; then
    LIVE_TTS=on
    parec -d "$TTS_SINK.monitor" --file-format=wav --channels=1 --rate=24000 "$RUN/live-tts.wav" > /dev/null 2>&1 &
    TTS_REC=$!
    say "live: the spoken answers on the null sink $TTS_SINK (module $TTS_MOD; its monitor recorded to live-tts.wav)"
  else
    say "live: no null sink for the spoken answers (pactl): SOMVR_LIVE_TTS off for this run"; TTS_SINK=""
  fi
fi
printf '%s\n' "${GENV[@]}" > "$RUN/env.txt"

# ---- render.cfg (game mode; som-b33-grass, re-winport.md 7.11): the VR graphics preset as the user's session has it -
# the PC's detection (som-win-session.sh's table, read from it) with SOMVR_RENDER_SET over it (WIN_RENDER_SET; else the
# switches' own; else scripts/headset-build26.env's), written into this slot's prefix for the run and the old file put
# back on every exit (cleanup). WIN_RENDER_SET=asis: the prefix's render.cfg as it is (the slots' prefixes hold Low).
RCF=""; RCB=""; RSW=${WIN_RENDER_SET-}
[ -n "${WIN_RENDER_SET+x}" ] || for kv in "${GENV[@]}"; do case $kv in SOMVR_RENDER_SET=*) RSW=${kv#*=} ;; esac; done
[ -n "$RSW" ] || [ -n "${WIN_RENDER_SET+x}" ] || RSW=$(sed -n 's/^SOMVR_RENDER_SET=//p' "$HERE/headset-build26.env" | tail -1)
RSV=$RSW   # b37: light = the VR preset, then GS_RENDER_LIGHT over it (asis: the prefix's file, Low - lighter still)
if [ "$KIND" = game ]; then
  if [ "$RSW" = asis ]; then printf 'asis (the prefix'"'"'s render.cfg as it is; asked %s by %s)\n' "$WRM" "$WRM_WHY" > "$RUN/render.mode"
  else
    [ "$WRM" = light ] && RSW="${RSW:+$RSW,}$GS_RENDER_LIGHT"
    printf '%s (%s) %s\n' "$WRM" "$WRM_WHY" "$RSW" > "$RUN/render.mode"
  fi
fi
if [ "$KIND" = game ] && [ -n "$RSW" ] && [ "$RSW" != asis ]; then
  for wp in $(pgrep -x wineserver); do
    gs_env_has "$wp" "WINEPREFIX=$PFX/pfx" && { say "STOP: render.cfg: the prefix $PFX is in use (wineserver $wp)"; exit 1; }
  done
  BASE=$(sed -n '/^declare -A RCFG=(/,/)$/p' "$HERE/som-win-session.sh")
  [[ $BASE == "declare -A RCFG=("*")" ]] || { say "STOP: render.cfg: no detection table in som-win-session.sh"; exit 1; }
  eval "$BASE"
  for p in ${RSW//,/ }; do
    [[ $p =~ ^([A-Za-z]+):([0-5])$ ]] && [ -n "${RCFG[${BASH_REMATCH[1]}]+x}" ] || { say "STOP: render.cfg: '$p' is not NAME:0..5"; exit 1; }
    RCFG[${BASH_REMATCH[1]}]=${BASH_REMATCH[2]}
  done
  RCF="$PFX/pfx/drive_c/users/steamuser/Documents/WB Games/Shadow of Mordor/render.cfg"
  for k in $(printf '%s\n' "${!RCFG[@]}" | sort); do printf '"Render.Setting.%s" "%s.000000"\n' "$k" "${RCFG[$k]}"; done > "$RUN/render.cfg.seeded"
  if [ -f "$RCF" ]; then RCB=$RUN/render.cfg.before; cp -p "$RCF" "$RCB"; else RCB=none; fi
  mkdir -p "$(dirname "$RCF")" && cp "$RUN/render.cfg.seeded" "$RCF" ||
    { [ "$RCB" = none ] && rm -f "$RCF" || cp -p "$RCB" "$RCF"; say "STOP: render.cfg not written"; exit 1; }
  say "render.cfg: WIN_RENDER=$WRM ($WRM_WHY) - the PC's detection with the VR preset $RSV$([ "$WRM" = light ] && echo ", then the light set $GS_RENDER_LIGHT") (put back after the run)"
fi

MSVC=""; GS=""; SHOTS=""; MGS=""; MGSRT=""
cleanup() {   # (every exit, a TERM included - the player's launcher stops a test run so: the picture loop too, or it
              # would go on shooting whatever gamescope later takes the same socket name)
  [ -n "$SHOTS" ] && kill "$SHOTS" 2>/dev/null
  [ -n "$GS" ] && kill -TERM "$GS" 2>/dev/null
  if [ -n "$MSVC" ]; then kill -INT "$MSVC" 2>/dev/null; for i in $(seq 1 25); do kill -0 "$MSVC" 2>/dev/null || break; sleep 0.2; done
    kill -0 "$MSVC" 2>/dev/null && kill -TERM "$MSVC"; fi
  "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
  [ -n "$MGS" ] && kill -TERM "$MGS" 2>/dev/null   # b37 lane2: Monado's own gamescope (after Monado)
  [ -n "$MGSRT" ] && case $MGSRT in /tmp/somvr-mgs.*) rm -rf "$MGSRT" ;; esac
  [ -L "$BCL" ] && rm -f "$BCL"
  [ "${LIVE_ON:-0}" = 1 ] && "$HERE/live_session.sh" stop "$RUN"   # som-b35-live: its daemon (its own pid only)
  [ -n "${TTS_REC:-}" ] && { kill -INT "$TTS_REC" 2>/dev/null; sleep 0.3; }   # b37: the monitor's recorder, then the sink
  [ -n "${TTS_MOD:-}" ] && pactl unload-module "$TTS_MOD" 2>/dev/null
  [ "$(cat "$LOCKD/pid" 2>/dev/null)" = $$ ] && rm -f "$LOCKD/pid" "$LOCKD/run"   # b35 slots
  if [ -n "$RCF" ]; then   # render.cfg: the prefix's own again (after wineserver -k: the game can no longer write it)
    if [ "$RCB" = none ]; then rm -f "$RCF"; else cp -p "$RCB" "$RCF"; fi; RCF=""
  fi
}
trap cleanup EXIT

# ---- Monado's null compositor (headless_vr.sh's way) -------------------------------------------------------------
if [ $XR = 1 ]; then
  M=$RUN/mxdg; mkdir -p "$M/config" "$M/cache" "$M/data"
  [ "$SLOT" -ge 2 ] && { ss -xlH 2>/dev/null | grep -qF "$SRT/monado_comp_ipc" || rm -f "$SRT/monado_comp_ipc" "$SRT/monado.pid"; }
  MCOMP=(XRT_COMPOSITOR_NULL=1)
  if [ -n "${WIN_MONADO_RATE:-}" ]; then   # b37 lane2: the real compositor at HZ in a headless gamescope of its own
    [[ $WIN_MONADO_RATE =~ ^[0-9]{2,3}$ ]] || { say "STOP: WIN_MONADO_RATE=$WIN_MONADO_RATE (whole Hz)"; exit 1; }
    MGSRT=$(mktemp -d /tmp/somvr-mgs.XXXXXX) && chmod 700 "$MGSRT"
    setsid "${CLEAN[@]}" XDG_RUNTIME_DIR="$MGSRT" gamescope --backend headless -W 1920 -H 1080 -w 1920 -h 1080 \
      -r "$WIN_MONADO_RATE" -- sh -c 'env > "$1.tmp" && mv -f "$1.tmp" "$1" && exec tail -f /dev/null' vd "$RUN/monado-gs.env" \
      > "$RUN/monado-gamescope.log" 2>&1 < /dev/null &
    MGS=$!
    for i in $(seq 1 50); do [ -s "$RUN/monado-gs.env" ] && break; sleep 0.2; done
    MDISP=$(sed -n 's/^DISPLAY=//p' "$RUN/monado-gs.env" 2>/dev/null)
    [ -n "$MDISP" ] || { say "STOP: Monado's gamescope did not come up"; exit 1; }
    MCOMP=(XRT_COMPOSITOR_FORCE_XCB=1 XRT_COMPOSITOR_DEFAULT_FRAMERATE="$WIN_MONADO_RATE" DISPLAY="$MDISP")
    say "Monado's own gamescope pid $MGS $MDISP: the real compositor at $WIN_MONADO_RATE Hz"
  fi
  setsid chrt -o 0 env -i HOME="$M" PATH=/usr/bin:/bin XDG_RUNTIME_DIR=$SRT XDG_CONFIG_HOME="$M/config" \
    XDG_CACHE_HOME="$M/cache" XDG_DATA_HOME="$M/data" SIMULATED_ENABLE=1 "${MCOMP[@]}" XRT_NO_STDIN=1 \
    XRT_LOG=info "${MONENV[@]}" VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
    timeout -s INT -k 10 $((SECS + 300)) /usr/bin/monado-service > "$RUN/monado.log" 2>&1 < /dev/null &
  MSVC=$!
  for i in $(seq 1 50); do [ -S "$SRT/monado_comp_ipc" ] && break; sleep 0.2; done
  [ -S "$SRT/monado_comp_ipc" ] || { say "STOP: monado did not come up"; exit 1; }
  MWHAT="null compositor"; [ -n "${WIN_MONADO_RATE:-}" ] && MWHAT="compositor at $WIN_MONADO_RATE Hz (xcb)"
  say "monado $MWHAT pid $MSVC (slot $SLOT: $SRT/monado_comp_ipc)${MONENV[*]:+ with ${MONENV[*]}}"
fi

# ---- the window: a headless gamescope (d3d, game) ------------------------------------------------------------
DISP=()
if [ $SLOTFREE = 0 ]; then
  setsid "${CLEAN[@]}" XDG_RUNTIME_DIR="$SRT" gamescope --backend headless -W "$W" -H "$H" -w "$W" -h "$H" -r 60 -- \
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
  compat) CMD=("$MOD/build/win/test_compat.exe"); TMO=120
    [ -f "${CMD[0]}" ] || { say "STOP: no ${CMD[0]} (win/build.sh --check)"; exit 1; } ;;
  d3d) CMD=("$TEST" d3d "$SECS" "$W" "$H"); TMO=$((SECS + 120)) ;;
  game) CMD=("$EXE"); TMO=$((SECS + 60)) ;;
esac
say "run ${CMD[*]} (timeout $TMO s)"
if [ "$KIND" = game ]; then   # the drivers' names (boot_check's): what they read, beside ours
  { echo "$WINE ${CMD[*]}"; echo "# mod (windows, headless_win.sh): $MOD"
    echo "# render (b37, slot $SLOT): $(cat "$RUN/render.mode" 2>/dev/null)"   # (which look the run's pictures had)
    echo "# game only (--game-env): ${GENV[*]}"; } > "$RUN/launch-cmd.txt"
  ln -s wine.log "$RUN/game.log"; ln -s summary.txt "$RUN/run.log"   # (the layer writes xr-layer.log itself)
  : > "$RUN/cmd"; [ -n "${WIN_CMDS:-}" ] && sed -e '$a\' "$WIN_CMDS" >> "$RUN/cmd"   # command mode (below)
fi
T0=$(date +%s)
# the program's folder in a mount namespace (bwrap over the whole system): the stand-in's read-only; the game's install
# as an overlay whose writes land in $RUN/game-upper (the GOG folder itself is never written, whatever the game tries;
# the summary lists what it wrote)
RO=$(dirname "${CMD[0]}")
MUTE=(--tmpfs /dev/snd --tmpfs "$RT/pulse")   # no sound card, no PulseAudio socket ...
for so in "$RT"/pipewire-0 "$RT"/pipewire-0-manager; do [ -S "$so" ] && MUTE+=(--ro-bind /dev/null "$so"); done  # ... no PipeWire
if [ "$KIND" = game ]; then
  mkdir -p "$RUN/game-upper" "$RUN/game-work"
  if [ "$W1" = 1 ]; then   # the package beside the exe, in the overlay's upper layer only
    mkdir -p "$RUN/game-upper/x64" && cp -r "$MOD/build/win/package/x64/." "$RUN/game-upper/x64/"
    say "W1: the player's package ($(find "$RUN/game-upper/x64" -type f | wc -l) files) beside the exe (overlay)"
  fi
  NS=(bwrap --dev-bind / / --overlay-src "$GAME_DIR" --overlay "$RUN/game-upper" "$RUN/game-work" "$GAME_DIR" "${MUTE[@]}")
  "${NS[@]}" -- sh -c 'touch "$1/.somvr-ns-probe"' p "$GAME_DIR" && [ ! -e "$GAME_DIR/.somvr-ns-probe" ] &&
    [ -e "$RUN/game-upper/.somvr-ns-probe" ] || { say "STOP: the overlay over $GAME_DIR did not hold"; exit 1; }
  rm -f "$RUN/game-upper/.somvr-ns-probe"
  [ "$W1" = 1 ] && [ ! -f "$GAME_DIR/x64/vulkan-1.dll" ] && "${NS[@]}" -- test -f "$GAME_DIR/x64/vulkan-1.dll" ||
    [ "$W1" != 1 ] || { say "STOP: W1's files not visible in the overlay (or the real folder has a vulkan-1.dll)"; exit 1; }
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
# W1, in its own prefix (logs/winport/prefix-w1; kept there):
# - Wine's token has a high integrity level, so the Khronos loader ignores VK_ADD_LAYER_PATH ("running with elevated
#   permissions"): the layer's manifest is registered in HKLM ExplicitLayers (VK_INSTANCE_LAYERS still enables it); a
#   Windows player's normal (medium) token takes VK_ADD_LAYER_PATH
# - wineopenxr's xrCreateInstance waits for HKCU\Software\Wine\VR "state", which Proton's steam.exe sets at a Steam
#   launch (vrclient_init_registry -> wineopenxr_init_registry): the stand-in's `vrinit` does that against Monado
if [ "$W1" = 1 ]; then
  W1REG="Z:$(printf '%s' "$GAME_DIR/x64/somvr/VkLayer_somvr_openxr.json" | tr / '\\')"
  "${CLEAN[@]}" WINEDEBUG=-all "$WINE" reg add 'HKLM\Software\Khronos\Vulkan\ExplicitLayers' /v "$W1REG" /t REG_DWORD \
    /d 0 /f >> "$RUN/w1-reg.log" 2>&1 || { say "STOP: W1: could not register the layer in its prefix"; exit 1; }
  say "W1: the layer registered in its prefix (HKLM ExplicitLayers): $W1REG"
  "${CLEAN[@]}" WINEDEBUG=-all,+openxr XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json timeout 120 \
    "$WINE" "$TEST" vrinit >> "$RUN/w1-reg.log" 2>&1 || { say "STOP: W1: wineopenxr's registry setup failed (w1-reg.log)"; exit 1; }
  say "W1: $(grep -m1 'vrinit:' "$RUN/w1-reg.log" | cut -c1-200)"
  # these wine runs' wineserver is OUTSIDE the namespace (and Wine opens files in the server): it must be gone (its
  # registry saved) before the game starts, or the game would see the real install instead of the overlay
  "${CLEAN[@]}" timeout 60 "$WSRV" -w
  "${CLEAN[@]}" "$WSRV" -k0 2>/dev/null && { say "STOP: W1: a wineserver of the prefix is still running"; exit 1; }
fi
# (the working folder is entered inside the namespace: a cd outside would hand the game the real folder's inode)
( cd / && setsid "${CLEAN[@]}" "${DISP[@]}" "${GENV[@]}" timeout -s TERM -k 15 "$TMO" \
    "${NS[@]}" --chdir "$(dirname "${CMD[0]}")" -- "$WINE" "${CMD[@]}" > "$RUN/wine.log" 2>&1 < /dev/null ) &
PP=$!
[ "$LIVE_ON" = 1 ] && say "live: the daemon $(SOMVR_LIVE_TTS=$LIVE_TTS LIVE_TTS_SINK=$TTS_SINK "$HERE/live_session.sh" start "$RUN" $$ "$MOD") ($("$HERE/live_session.sh" dir "$RUN"))"
shot_to() {   # FILE.png: the headless gamescope's screen
  GAMESCOPE_WAYLAND_DISPLAY="$GWD" XDG_RUNTIME_DIR="$SRT" timeout 10 gamescopectl screenshot "$1" >> "$RUN/shots.log" 2>&1
  for i in $(seq 1 30); do [ -s "$1" ] && break; sleep 0.2; done
  [ -s "$1" ]
}
shot() {   # shot NAME -> shots/NAME.png
  mkdir -p "$RUN/shots"
  shot_to "$RUN/shots/$1.png" && say "shot shots/$1.png" || say "shot $1 failed"
}
SHOT_S=${WIN_SHOT_S:-30}; [[ $SHOT_S =~ ^[0-9]+$ ]] || SHOT_S=30
if [ $SLOTFREE = 0 ] && [ "$SHOT_S" -gt 0 ]; then   # pictures every SHOT_S (30) s while the program runs (and this script: its pid is $$ in the loop)
  ( n=0; while [ $n -lt $(( (SECS + SHOT_S - 1) / SHOT_S )) ] && kill -0 $$ 2>/dev/null; do sleep "$SHOT_S"; n=$((n + 1))
      shot "t$((n * SHOT_S))"; kill -0 $PP 2>/dev/null || say "(the launch process $PP has ended; pictures go on)"; done ) &
  SHOTS=$!
fi
VCARD=$(gs_card) || VCARD=""
[ "$KIND" = game ] && [ -n "$VCARD" ] && say "vram at the launch: $(gs_mib "$VCARD/mem_info_vram_used") of $(gs_mib "$VCARD/mem_info_vram_total") MiB used, GTT $(gs_mib "$VCARD/mem_info_gtt_used") MiB (render $WRM)"
if [ "$KIND" = game ]; then   # b35 slots: the VR gate every 2 s, the kernel log every 10 s, also during a `driver`
  ( n=0; while kill -0 $$ 2>/dev/null; do   # command: the user in VR / an amdgpu reset = this slot's game stops at once
      why=$(gs_vr_why) || why=""
      [ -z "$why" ] && [ $((n % 5)) = 0 ] && { why=$(gs_gpu_why "$T0") && why="GPU: $why" || why=""; }
      if [ $((n % 5)) = 0 ] && [ -n "$VCARD" ] && gp=$(gs_game_pid "$SLOT"); then   # b37: the game's and the card's memory
        echo "$(date +%s) $(gs_pid_mem "$gp") $(gs_mib "$VCARD/mem_info_vram_used") $(gs_mib "$VCARD/mem_info_gtt_used")" >> "$RUN/vram.tsv"
      fi
      if [ -n "$why" ]; then echo "$why" > "$RUN/ABORT"; "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
        say "!!! STOPPED AT ONCE (slot $SLOT): $why"; exit 0; fi
      n=$((n + 1)); sleep 2
    done ) &
  VRMON=$!
fi
TG=$(date +%s); CUR=0; QUIT=0; NCMD=0; MARKS_N=0
gel() { echo $(( $(date +%s) - TG )); }
game_alive() {   # b35 slots: this run's game (its SOMVR_LOG), not another slot's
  local gp; for gp in $(pgrep -f 'ShadowOfMordor\.exe'); do
    gs_env_has "$gp" "SOMVR_LOG=$RUN/somvr.log" && return 0; done; return 1; }
xd() { env -u XAUTHORITY "${DISP[@]}" xdotool "$@" >> "$RUN/input.log" 2>&1; }
mark_serve() {   # RUN/marks/<id> with a new `want` (the mod's bug mark): the flat picture and the log tails; no voice here
  local d=$1
  mv -f "$d/want" "$d/served" 2>/dev/null || return 0
  MARKS_N=$((MARKS_N + 1))
  shot_to "$d/flat.png" || echo "no flat picture (gamescope's screenshot failed)" > "$d/flat.txt"
  tail -n 400 "$RUN/somvr.log" > "$d/somvr_tail.txt" 2>/dev/null
  tail -n 200 "$RUN/xr-layer.log" > "$d/xr_tail.txt" 2>/dev/null
  if [ -n "${WIN_MARK_VOICE:-}" ] && [ -r "$WIN_MARK_VOICE" ]; then   # som-b35-live: a recorded note fed in (no microphone, no
    ( vs=$(python3 -I -c 'import sys, wave; w = wave.open(sys.argv[1]); print("%.1f" % (w.getnframes() / w.getframerate()))' \
          "$WIN_MARK_VOICE" 2>/dev/null) || vs=1   # sound out): as voice_rec.py leaves a note - the WAV, then voice.txt at its end
      sleep "$vs"; cp -f "$WIN_MARK_VOICE" "$d/voice.wav"
      echo "$vs s from $(basename "$WIN_MARK_VOICE") (a recorded note fed in; ended: its end)" > "$d/voice.txt" ) &
  else
    echo "no voice: the Windows harness records nothing" > "$d/voice.txt"
  fi
  say "bug mark $MARKS_N: $d ($(head -1 "$d/state.txt" 2>/dev/null | cut -c1-120))"
  echo "$(date +%T) ${d##*/} $(head -1 "$d/state.txt" 2>/dev/null | cut -c1-120) | voice: none" >> "$RUN/marks.log"
}
svc() {   # every 0.5 s (the loop, wait, waitlog): the player's Quit (the VR menu's / the word quitgame), new bug marks
  local w
  [ $QUIT = 0 ] && [ -e "$RUN/ABORT" ] && { say "the game was stopped at once: $(cat "$RUN/ABORT")"; QUIT=1; }   # b35 slots
  if [ $QUIT = 0 ] && [ -e "$RUN/quit.request" ]; then
    say "QUIT: the player asked to quit ($(head -c 200 "$RUN/quit.request" 2>/dev/null | tr '\n' ' ')): stopping the game"
    QUIT=1
  fi
  for w in "$RUN"/marks/*/want; do [ -e "$w" ] && mark_serve "${w%/want}"; done
}
nap() {   # SECS (whole or decimal): sleep in 0.5 s steps, serving the player
  local end=$(( $(date +%s%N) / 1000000 + ${1%.*} * 1000 ))
  [[ $1 == *.* ]] && end=$(( end + 10#$(printf '%-3.3s' "${1#*.}" | tr ' ' 0) ))
  while [ $(( $(date +%s%N) / 1000000 )) -lt $end ] && [ $QUIT = 0 ]; do svc; sleep 0.5; done
}
waitlog() {   # SECS TEXT: a line holding TEXT after the last match
  local secs=$1 end n; shift
  end=$(( $(date +%s) + secs ))
  while :; do
    n=$(grep -n -F -- "$*" "$RUN/somvr.log" 2>/dev/null | awk -F: -v c=$CUR '$1 > c { print $1; exit }')
    [ -n "$n" ] && { CUR=$n; say "waitlog '$*' at $(gel) s: $(sed -n "${n}p" "$RUN/somvr.log" | cut -c1-120)"; return 0; }
    [ "$(date +%s)" -ge "$end" ] && { say "!!! waitlog: no '$*' within $secs s"; return 1; }
    game_alive || return 1
    svc; [ $QUIT = 0 ] || return 1
    sleep 0.5
  done
}
run_cmd() {
  local c=${1:-}; [ $# -gt 0 ] && shift
  case $c in
    shot) shot "${1:-manual-$(gel)s}" ;;
    key) xd key --delay 120 "$@"; say "key $* at $(gel) s" ;;
    xdo) xd "$@" ;;
    mark) say "MARK $* at $(gel) s" ;;
    wait) nap "${1:-1}" ;;
    waitlog) waitlog "$@" ;;
    trigger) printf '%s' "$*" > "$RUN/somvr.trigger"; say "trigger $* at $(gel) s" ;;
    untrigger) rm -f "$RUN/somvr.trigger" ;;
    xrtest) { echo "# $(date +%s%N)"; echo "$*" | sed 's/ | /\n/g'; } > "$RUN/xrtest.tmp" && mv -f "$RUN/xrtest.tmp" "$RUN/xrtest.txt"
      say "xrtest $* at $(gel) s"; sleep 1 ;;   # a new file each time (the layer runs each new file's lines once,
                                                   # polled: 1 s before the next); ' | ' = a new line
    quit) say "quit (the commands) at $(gel) s"; return 9 ;;
    driver) local left=$(( SECS - $(gel) - 10 )); say "driver $* (at most $left s)"   # a Linux driver on this run
      # (the drivers want a run whose folder is newer than their own start: the link's time is refreshed meanwhile)
      ( for k in $(seq 1 30); do sleep 1; touch -h "$BCL"; done ) & local tl=$!
      local kv=() a; for a in "${@:2}"; do kv+=("${a//\~/ }"); done   # (K=V values: '~' = a space)
      ( cd "$HERE" && timeout "$left" env BG_RUN="$BCL" P18_RUN="$BCL" PIN_RUN="$BCL" "${kv[@]}" bash "./$1" ) >> "$RUN/driver.log" 2>&1
      kill $tl 2>/dev/null
      say "driver $1 done at $(gel) s (driver.log)" ;;
    ''|'#'*) ;;
    *) say "unknown command: $c $*" ;;
  esac
}
if [ "$KIND" = game ]; then
  # command mode (boot_check's): WIN_CMDS went into RUN/cmd at the launch; lines appended later (the drivers' `quit`,
  # `shot`, `key`) run in order; whole lines only (wc -l); done ones go to cmd.done
  while [ $QUIT = 0 ]; do
    [ "$(gel)" -ge "$SECS" ] && { say "the hold ($SECS s) is over: ending the game"; break; }
    game_alive || { [ -e "$RUN/ABORT" ] && say "the game was stopped at once after $(gel) s: $(cat "$RUN/ABORT")" ||
                    say "the game exited by itself after $(gel) s (pgrep: $(pgrep -f 'ShadowOfMordor\.exe' | tr '\n' ' ')- none of them this run's)"; break; }   # (b35 slots: ABORT = the VR/GPU stop)
    if [ "$(wc -l < "$RUN/cmd")" -gt $NCMD ]; then
      NCMD=$((NCMD + 1)); line=$(sed -n "${NCMD}p" "$RUN/cmd"); echo "$line" >> "$RUN/cmd.done"
      read -r -a words <<< "$line"   # (words, no glob)
      run_cmd "${words[@]}"; [ $? = 9 ] && QUIT=1
      continue
    fi
    svc; [ $QUIT = 0 ] && sleep 0.5
  done
  if game_alive; then
    # the stop: the word endlogs first (every feature's END lines and the VR menu's unsaved change; the writer thread
    # reads the trigger file every 0.25 s), then the whole wine session
    if [ ! -e "$RUN/ABORT" ] && [ -e "$RUN/somvr.trigger" ] && ! LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null; then
      printf 'endlogs' > "$RUN/somvr.trigger"
      for i in $(seq 1 12); do LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null && break; sleep 0.25; done
      LC_ALL=C grep -a -q '^END t=' "$RUN/somvr.log" 2>/dev/null && say "somvr's END lines written (the word endlogs)" ||
        say "!!! somvr wrote no END lines within 3 s of the word endlogs"
    fi
    say "stopping the game at $(gel) s: wineserver -k"
  fi
  "${CLEAN[@]}" "$WSRV" -k 2>/dev/null
  [ -n "$MGS" ] && kill -TERM "$MGS" 2>/dev/null   # b37 lane2: Monado's own gamescope (after Monado)
  [ -n "$MGSRT" ] && case $MGSRT in /tmp/somvr-mgs.*) rm -rf "$MGSRT" ;; esac
fi
wait $PP; RC=$?
[ -n "${SHOTS:-}" ] && kill $SHOTS 2>/dev/null
[ -n "${VRMON:-}" ] && kill $VRMON 2>/dev/null   # b35 slots
say "program exit $RC after $(( $(date +%s) - T0 )) s"
grep -m3 "d3dtest:" "$RUN/wine.log" | sed 's/^/  /' | tee -a "$RUN/summary.txt"
[ "$KIND" = compat ] && grep -a "test_compat:" "$RUN/wine.log" | sed 's/^/  /' | tee -a "$RUN/summary.txt"
[ -f "$RUN/somvr.log" ] && say "somvr.log: $(grep -c '' "$RUN/somvr.log") lines, $(grep -c '^WIN ' "$RUN/somvr.log") WIN lines"
[ -f "$RUN/bridge" ] && say "bridge: $(grep -c '^somvr_' "$RUN/bridge") objects, $(grep '^nonce' "$RUN/bridge")"
[ -n "${SAVE_SHA0:-}" ] && say "save: the prefix copy $SAVE_SHA0 -> $(sha256sum "$SAVES/GameData.sav" 2>/dev/null | cut -c1-16) (the user's file was only read)"
# b37 ladder (som-b37-ladder): the run keeps the prefix's save as it is at the end (RUN/saves/GameData.sav, as boot_check's
# --save-copy does), before the slot is released - the next boot in this slot copies its own save over the prefix's
if [ "$KIND" = game ] && [ -f "$SAVES/GameData.sav" ]; then
  mkdir -p "$RUN/saves" && cp --no-preserve=mode "$SAVES/GameData.sav" "$RUN/saves/GameData.sav" &&
    say "save: kept as RUN/saves/GameData.sav ($(sha256sum "$RUN/saves/GameData.sav" | cut -c1-16), $(stat -c %y "$SAVES/GameData.sav" | cut -c1-19))"
fi
if [ -n "${SRC_SHA0:-}" ]; then   # the save guard's line (boot_check's words): the user's file, by its sha256
  SRC_SHA1=$(sha256sum "$WIN_SAVE_FROM" 2>/dev/null | cut -d' ' -f1)
  [ "$SRC_SHA1" = "$SRC_SHA0" ] && say "  save after: GameData.sav UNCHANGED (the user's $WIN_SAVE_FROM, only read: sha256 ${SRC_SHA0:0:16})" ||
    say "  save after: GameData.sav CHANGED !!! the user's $WIN_SAVE_FROM: ${SRC_SHA0:0:16} -> ${SRC_SHA1:0:16}"
fi
C=$(grep -a -m1 -E 'Unhandled exception|unhandled page fault|wine: Unhandled' "$RUN/wine.log" 2>/dev/null | cut -c1-140)
[ -n "$C" ] && say "!!! crash (wine.log): $C"
[ -d "$RUN/game-upper" ] && say "the game wrote into its folder (kept in game-upper): $(cd "$RUN/game-upper" && find . -type f | head -20 | tr '\n' ' ')"
[ -f "$RUN/xr-layer.log" ] && say "xr-layer.log: $(grep -c '' "$RUN/xr-layer.log") lines"
[ -s "$RUN/vram.tsv" ] && say "vram (render $WRM): $(awk '{ for (i = 2; i <= 7; i++) if ($i > m[i]) m[i] = $i; n++ }
  END { printf "the game'"'"'s peak %d MiB resident (%d asked), GTT %d (%d asked); the card'"'"'s peak %d MiB used, GTT %d (%d samples, RUN/vram.tsv)", m[2], m[3], m[4], m[5], m[6], m[7], n }' "$RUN/vram.tsv")"
exit $RC
