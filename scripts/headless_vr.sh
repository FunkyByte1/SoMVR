#!/bin/bash
# headless_vr.sh - one headless VR test boot (Monado's null compositor as the OpenXR runtime, boot_check's headless
# gamescope, the user's save copy) with every switch of the user's headset build (scripts/som-vr-headset.sh), for helpers
# and the coordinator. Run it OUTSIDE the Bash sandbox, in the background. It refuses when the slot is busy (boot_check).
#   headless_vr.sh /ABS/PATH/TO/mod/somvr      (a built mod folder: build/somvr.so and vklayer/build/...)
# Environment: HEADLESS_CMDS (default scripts/cmds/somvr-vr6.cmd: straight to the main menu's Continue, the practice
#   meadow; with the launch below), HEADLESS_HOLD (seconds, default 900), SOMVR_EXTRA ("K=V K=V": more or overriding
#   --game-env), HEADLESS_LAUNCH=old (the build-5 launch: the pre-game window, the distro box and the logo movies, for
#   somvr-vr5.cmd), HEADLESS_UNIT=NAME (boot_check runs in a systemd user unit NAME with KillMode=mixed, as the user's
#   headset session does: `systemctl --user stop NAME` = the PC window's Stop; this script waits for the unit),
#   HEADLESS_SAVE_FROM=/ABS/RUN/saves/GameData.sav (build 23: the run's save copy is that earlier run's copy - its story
#   progress - not the real save's; boot_check --save-from), HEADLESS_HARD (build 27: seconds, 1801..4500 = boot_check
#   --hard with --soak, the soak's long boot; unset = boot_check's 1800), HEADLESS_MONADO_ENV (build 27: "K=V ..." for
#   monado-service, U_PACING_* / XRT_COMPOSITOR_* only, e.g. U_PACING_APP_IMMEDIATE_WAIT_FRAME_RETURN=1: the game unpaced).
#   Build 27b (som-b27-perf2, re-xr-pipeline.md 119): SOMVR_RENDER_SET=NAME:V,NAME:V in SOMVR_EXTRA (the env file's VR
#   graphics preset, as scripts/som-vr-headset.sh takes it) = boot_check --render-set NAME=V each (the run's own
#   render.cfg); HEADLESS_RENDER="NAME:V,..." instead of it (set but empty or asis: none - the game's own detection).
#   --win (som-b33-grass, docs/re-winport.md 7.11): the same preset over the PC's detection into the slot's prefix's
#   render.cfg for the run (headless_win.sh, put back after); HEADLESS_RENDER=asis = the prefix's file as it is (Low).
#   The launch (build 6): --no-feral-options and the run's own prefs with GameOptionsDialogShouldShow 0,
#   SkipOSWarnings 1, SpecificationAlerts\LnxDistributionUnsupported 1 and DisableSplashVideos 1 (docs/re-launcher.md);
#   SOMVR_MARK=1 and SOMVR_QUIT=1 (bug marks, the menu's Quit game). boot_check.sh: the one next to this script.
#   Live commands: append lines to
#   RUN/cmd (boot_check's command mode: shot, key, wait, mark, quit...); trigger words (e.g. `practice spawn shield`)
#   go into RUN/somvr.trigger directly. Monado's log: logs/headless-<ts>/monado.log; the run: the newest
#   logs/boot-check-<ts>/. Note: the null compositor paces at 20 Hz, so frame rates here say nothing about 90 fps.
#   headless_vr.sh --win /ABS/PATH/TO/mod/somvr (winport-drivers, docs/re-winport.md 7.11): the WINDOWS build (the GOG
#   game under Proton, the DLL from build/win/) through scripts/headless_win.sh with the same switches (the list below
#   + SOMVR_EXTRA; the layer/runtime/log ones are headless_win.sh's), the same run folder (logs/boot-check-<ts>/: the
#   drivers find it, launch-cmd.txt names the mod) and the same channels (RUN/cmd, somvr.trigger, xrtest.txt,
#   quit.request, marks). HEADLESS_CMDS default cmds/win-vr6.cmd (the main menu's Continue, the meadow);
#   HEADLESS_SAVE_FROM = the save file to copy (default the user's Steam save, only read); HEADLESS_HOLD as above.
#   Not on Windows (yet): HEADLESS_UNIT, HEADLESS_HARD, HEADLESS_LAUNCH=old, the render preset. HEADLESS_MONADO_ENV: as
#   WIN_MONADO_ENV (b37 lane2).
#   b35 slots (som-b35-slots, docs/re-xr-pipeline.md 158, scripts/game_slot.sh): HEADLESS_SLOT = the game slot: unset or
#   1 = the caller holds /tmp/somvr-boot.lock (as before); 2..3 (--win only) = the caller holds /tmp/somvr-slotN.lock;
#   auto = this script takes the first free slot for HEADLESS_OWNER (--win: slots 2.. first, then 1; Linux: slot 1),
#   waiting up to HEADLESS_SLOT_WAIT s (default 0), and releases it at the end; solo = every slot (perf tests).
#   b37 (som-b37-slotsvram, docs/re-xr-pipeline.md 158.5): WIN_RENDER=vr|light (--win; the environment reaches
#   game_slot.sh's take and headless_win.sh): vr = the VR preset (must be set by every boot that judges pictures, grass,
#   foliage, fog, weather, GPU or frame time - perf, soaks, the suites, looks/render drivers), light = the preset with
#   the VRAM-heavy settings lowered (behaviour drivers). Unset: slot 1 vr, slots 2.. light. auto with WIN_RENDER=vr tries
#   slot 1 first; take's VRAM gate waits until the boot's need fits (it says why, in this script's output).
# (From the coordinator's run13/run14 scripts, 2026-10-22.)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor; RT=/run/user/$(id -u); MSVC=""
HERE=$(cd "$(dirname "$0")" && pwd); BC=$HERE/boot_check.sh   # (a worktree's copy runs its own boot_check)
# 11-03: tests start from a pinned copy of the user's Steam save (logs/test-save/saves/GameData.sav, its sha next to it),
# so Steam Cloud progress made on another device never changes what a test starts from (Windows: always; Linux: only
# once the real save differs from it - boot_check refuses a --save-from equal to the real save). HEADLESS_SAVE_FROM wins.
PIN=$SOM/logs/test-save/saves/GameData.sav
WIN=0; [ "${1:-}" = --win ] && { WIN=1; shift; }
MOD=${1:?usage: headless_vr.sh [--win] /ABS/PATH/TO/mod/somvr}; MOD=$(cd "$MOD" 2>/dev/null && pwd -P) || { echo "STOP: no folder $1"; exit 1; }
[ -f "$MOD/build/somvr.so" ] && [ -f "$MOD/vklayer/build/libVkLayer_somvr_openxr.so" ] || { echo "STOP: $MOD is not built"; exit 1; }
[ $WIN = 1 ] && { [ -f "$MOD/build/win/xinput9_1_0.dll" ] || { echo "STOP: $MOD has no build/win/xinput9_1_0.dll (win/build.sh)"; exit 1; }; }
# b35 slots: HEADLESS_SLOT (above); slot_release at the end for a slot taken here
SLOT=${HEADLESS_SLOT:-1}; GSR=""
slot_release() { [ -n "$GSR" ] && bash "$HERE/game_slot.sh" release "$HEADLESS_OWNER" "$GSR"; GSR=""; }
case $SLOT in
  auto|solo)
    [ -n "${HEADLESS_OWNER:-}" ] || { echo "STOP: HEADLESS_SLOT=$SLOT needs HEADLESS_OWNER=<your name>"; exit 1; }
    GSW=$SLOT; [ $WIN = 0 ] && [ "$SLOT" = auto ] && GSW=1   # (Linux boots: slot 1 only)
    SLOT=$(bash "$HERE/game_slot.sh" take "$HEADLESS_OWNER" "$GSW" "${HEADLESS_SLOT_WAIT:-0}") || { echo "STOP: no game slot"; exit 1; }
    GSR=$SLOT; [ "$SLOT" = solo ] && { GSR=all; SLOT=1; }
    trap slot_release EXIT
    echo "game slot $SLOT taken for $HEADLESS_OWNER$([ "$GSR" = all ] && echo ' (solo: every slot)')" ;;
  1) ;;
  [2-9]) [ $WIN = 1 ] || { echo "STOP: Linux boots run in slot 1 only (HEADLESS_SLOT unset or 1)"; exit 1; } ;;
  *) echo "STOP: HEADLESS_SLOT=$SLOT (1..3, auto or solo)"; exit 1 ;;
esac
# build 27: the runtime's own limit follows HEADLESS_HARD (the first soak's monado was killed at 1800 s, 21 minutes before
# its boot's end: XR_ERROR_INSTANCE_LOST, the rest of the boot flat - re-xr-pipeline.md 107.4)
[[ ${HEADLESS_HARD:-} =~ ^[0-9]*$ ]] || { echo "STOP: HEADLESS_HARD must be whole seconds"; exit 1; }
MON_S=1800; [ -n "${HEADLESS_HARD:-}" ] && MON_S=$((HEADLESS_HARD + 60))
# build 27 (som-b27-perf, re-xr-pipeline.md 115): HEADLESS_MONADO_ENV="K=V ..." more environment for monado-service, only
# its pacing and compositor options (U_PACING_*, XRT_COMPOSITOR_*): U_PACING_APP_IMMEDIATE_WAIT_FRAME_RETURN=1 = xrWaitFrame
# returns at once, the game runs as fast as it can (the null compositor still shows 20 Hz): frame rates then say what the
# GPU and the CPU can hold
MONENV=()
for kv in ${HEADLESS_MONADO_ENV:-}; do
  [[ $kv =~ ^(U_PACING_|XRT_COMPOSITOR_)[A-Z0-9_]*=[A-Za-z0-9._-]*$ ]] || { echo "STOP: HEADLESS_MONADO_ENV: $kv is not U_PACING_*=V or XRT_COMPOSITOR_*=V"; exit 1; }
  MONENV+=("$kv")
done
GE=(--game-env SOMVR_STEREO=B --game-env SOMVR_VIEWS=xr --game-env SOMVR_COMFORT=1
    --game-env VK_LAYER_PATH=$MOD/vklayer --game-env VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr
    --game-env XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json --game-env SOMVR_XR=1
    --game-env SOMVR_XR_LOG=@RUN@/xr-layer.log --game-env SOMVR_XR_DUMP=300,1800,3600,5400,7200,9000 --game-env SOMVR_BASE=head --game-env SOMVR_HIDE_HEAD=parts --game-env SOMVR_HEAD_PARTS=main --game-env SOMVR_FALLBACK=auto --game-env SOMVR_VRMENU=1 --game-env SOMVR_XR_ENV=on --game-env SOMVR_ANCHOR=root --game-env SOMVR_LOCO=1 --game-env SOMVR_COMBAT_REACT=1 --game-env SOMVR_CLIMB=b --game-env SOMVR_ARMS=1 --game-env SOMVR_ARMS_STRETCH=1 --game-env SOMVR_COMBAT=1 --game-env SOMVR_BLADE=controller --game-env SOMVR_HUD=quad --game-env SOMVR_POINTER=1 --game-env SOMVR_AUDIO=head --game-env SOMVR_EYE_SIZE=2015x2217 --game-env SOMVR_INPUT=1
    --game-env XR_LOADER_DEBUG=info --game-env VK_LOADER_DEBUG=layer)
GE+=(--game-env SOMVR_TUNE=@RUN@/tune.txt)
GE+=(--game-env SOMVR_THREAT=1 --game-env SOMVR_BODYMOMENTS=1 --game-env SOMVR_SLOTS=1 --game-env SOMVR_SLOTS_KEEP=attach
    --game-env SOMVR_DEFENCE=1 --game-env SOMVR_GAMEMOVES=off --game-env SOMVR_PRACTICE=auto --game-env SOMVR_LOCO_HOLD=attack --game-env SOMVR_HP_NEARLOG=1)
GE+=(--game-env SOMVR_MARK=1 --game-env SOMVR_QUIT=1)
for kv in ${SOMVR_EXTRA:-}; do GE+=(--game-env "$kv"); done
[ $WIN = 0 ] && [ -n "${WIN_RENDER:-}" ] && echo "note: WIN_RENDER is for --win (a Linux boot runs SOMVR_RENDER_SET / HEADLESS_RENDER)"
RS=""; for kv in ${SOMVR_EXTRA:-}; do case $kv in SOMVR_RENDER_SET=*) RS=${kv#*=} ;; esac; done # build 27b: the preset
[ -n "${HEADLESS_RENDER+x}" ] && RS=$HEADLESS_RENDER
RSWIN=$RS; [ "$RS" = asis ] && RS=""   # som-b33-grass: asis = the game's own (Linux) / the prefix's render.cfg as it is (Windows)
RSA=(); for p in ${RS//,/ }; do RSA+=(--render-set "${p/:/=}"); done
if [ $WIN = 1 ]; then   # the Windows build: the same switches and channels through headless_win.sh (its own Monado)
  WE=()
  for ((i = 1; i < ${#GE[@]}; i += 2)); do
    case ${GE[i]} in VK_LAYER_PATH=*|VK_INSTANCE_LAYERS=*|XR_RUNTIME_JSON=*|SOMVR_XR=*|SOMVR_XR_LOG=*) ;; *) WE+=("${GE[i]}") ;; esac
  done
  for v in HEADLESS_UNIT HEADLESS_HARD HEADLESS_LAUNCH; do [ -n "${!v:-}" ] && echo "note: $v is not used on Windows"; done
  WRN=${WIN_RENDER:-"unset: the take of slot $SLOT or its default (1 vr, 2.. light)"}
  echo "render: WIN_RENDER=$WRN (headless_win.sh writes RUN/render.mode)"
  WRS=(); [ -n "${HEADLESS_RENDER+x}" ] && WRS=(WIN_RENDER_SET="$RSWIN")   # else headless_win.sh: the switches' preset
  SAVE=${HEADLESS_SAVE_FROM:-}
  if [ "$SAVE" = none ]; then   # b37 ladder: no save copied - the prefix's own (a new game), only in a prefix of its own
    [ -n "${WIN_PREFIX:-}" ] || { echo "STOP: HEADLESS_SAVE_FROM=none needs WIN_PREFIX (a prefix of its own, not a slot's)"; exit 1; }
    SAVE=""; echo "save: none copied (HEADLESS_SAVE_FROM=none): $WIN_PREFIX's own"
  else
  [ -z "$SAVE" ] && [ -f "$PIN" ] && SAVE=$PIN   # 11-03: the pinned test save
  if [ -z "$SAVE" ]; then   # the user's Steam save (boot_check's way: exactly one userdata/*/241930), only read (copied)
    SV=(); for f in "$HOME"/.local/share/Steam/userdata/*/241930/remote/GameData.sav; do [ -f "$f" ] && SV+=("$f"); done
    [ ${#SV[@]} = 1 ] || { echo "STOP: ${#SV[@]} Steam saves (userdata/*/241930/remote/GameData.sav); set HEADLESS_SAVE_FROM"; exit 1; }
    SAVE=${SV[0]}
  fi
  fi
  # b35 slots (11-03 soak: two boots in one second got one name; a driver then drove the other's game): the name is
  # claimed under a lock (/tmp/somvr-runnames/<name>), so two boots never share it
  mkdir -p /tmp/somvr-runnames
  RUNW=$( exec 9> /tmp/somvr-runnames/.lock; flock -w 30 9 || exit 1
          for i in $(seq 1 30); do n=boot-check-$(date +%Y%m%d-%H%M%S)
            [ -e "$SOM/logs/$n" ] || [ -e "/tmp/somvr-runnames/$n" ] || { : > "/tmp/somvr-runnames/$n"; echo "$SOM/logs/$n"; exit 0; }
            sleep 1; done; exit 1 ) || { echo "STOP: no free run name (/tmp/somvr-runnames)"; exit 1; }
  echo "windows: $RUNW (headless_win.sh, slot $SLOT)"
  env "${WRS[@]}" WIN_MONADO_ENV="${MONENV[*]:-${WIN_MONADO_ENV:-}}" WIN_SLOT=$SLOT WIN_RUN=$RUNW WIN_CMDS=${HEADLESS_CMDS:-$HERE/cmds/win-vr6.cmd} WIN_SAVE_FROM=$SAVE SOMVR_EXTRA="${WE[*]}" \
    bash "$HERE/headless_win.sh" "$MOD" --game "${HEADLESS_HOLD:-900}"
  rc=$?; echo "headless_win exit $rc"; exit $rc
fi
TS=$(date +%Y%m%d-%H%M%S); R5=$SOM/logs/headless-$TS
mkdir -p "$R5/mxdg/config" "$R5/mxdg/cache" "$R5/mxdg/data"
echo "R5=$R5"
stop_monado() {
  [ -n "$MSVC" ] || return 0
  kill -INT "$MSVC" 2>/dev/null; for i in $(seq 1 25); do kill -0 "$MSVC" 2>/dev/null || break; sleep 0.2; done
  kill -0 "$MSVC" 2>/dev/null && kill -TERM "$MSVC"; sleep 1
  [ -e $RT/monado_comp_ipc ] && ! ss -xlH | grep -q monado_comp_ipc && rm -f $RT/monado_comp_ipc
  echo "monado stopped"
}
trap 'stop_monado; slot_release' EXIT   # (b35 slots: a slot taken here goes back too)
setsid chrt -o 0 env -i HOME="$R5/mxdg" PATH=/usr/bin:/bin XDG_RUNTIME_DIR=$RT \
  XDG_CONFIG_HOME="$R5/mxdg/config" XDG_CACHE_HOME="$R5/mxdg/cache" XDG_DATA_HOME="$R5/mxdg/data" \
  SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
  XRT_LOG=info XRT_COMPOSITOR_LOG=info XRT_PRINT_OPTIONS=on "${MONENV[@]}" \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
  timeout -s INT -k 10 "$MON_S" /usr/bin/monado-service > "$R5/monado.log" 2>&1 < /dev/null &
TPID=$!
for i in $(seq 1 50); do [ -S $RT/monado_comp_ipc ] && break; sleep 0.2; done
MSVC=$(pgrep -P "$TPID" -x monado-service)
[ -n "$MSVC" ] && tr '\0' '\n' < /proc/$MSVC/environ | grep -qx "XDG_CONFIG_HOME=$R5/mxdg/config" ||
  { echo "STOP: no Monado of ours (pid ${MSVC:-none})"; tail -30 "$R5/monado.log"; kill -INT "$TPID" 2>/dev/null; exit 1; }
grep -q 'Null compositor starting' "$R5/monado.log" || { sleep 2; grep -q 'Null compositor starting' "$R5/monado.log"; } || { echo "STOP: no null compositor"; tail -30 "$R5/monado.log"; exit 1; }
echo "monado up pid $MSVC"
LA=(--game-arg --no-feral-options --prefs-set GameOptionsDialogShouldShow=0 --prefs-set GameOptionsDialogShouldShowBigPicture=0
    --prefs-set SkipOSWarnings=1 --prefs-set 'SpecificationAlerts\LnxDistributionUnsupported=1'
    --prefs-set 'SpecificationAlerts\LnxCPUGovernorSubOptimal=1' --prefs-set DisableSplashVideos=1)
DCMDS=$HERE/cmds/somvr-vr6.cmd
[ "${HEADLESS_LAUNCH:-}" = old ] && { LA=(); DCMDS=$HERE/cmds/somvr-vr5.cmd; }
SF=(); [ -n "${HEADLESS_SAVE_FROM:-}" ] && SF=(--save-from "$HEADLESS_SAVE_FROM") # build 23: an earlier run's save copy
if [ -z "${HEADLESS_SAVE_FROM:-}" ] && [ -f "$PIN" ]; then   # 11-03: the pinned test save, once the real one moved on
  for f in "$HOME"/.local/share/Steam/userdata/*/241930/remote/GameData.sav; do [ -f "$f" ] && ! cmp -s "$PIN" "$f" && SF=(--save-from "$PIN"); done
fi
[ -n "${HEADLESS_HARD:-}" ] && SF+=(--hard "$HEADLESS_HARD" --soak) # build 27: the soak's long boot (at most 4500 s)
B=(--steam --save-copy "${SF[@]}" --seed-prefs $SOM/logs/boot-check-20261018-003728/preferences.seeded "${LA[@]}"
   "${RSA[@]}" --preload $MOD/build/somvr.so "${GE[@]}" --cmds "${HEADLESS_CMDS:-$DCMDS}" --hold "${HEADLESS_HOLD:-900}" --refresh 90)
"$BC" --dry-run "${B[@]}" > "$R5/dry.txt" 2>&1 || { echo "STOP: dry run failed"; tail -20 "$R5/dry.txt"; exit 1; }
DRY=$(ls -d $SOM/logs/boot-check-dryrun-* | sort | tail -1)
grep -q '^# game only (--game-env):.* XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado\.json' "$DRY/launch-cmd.txt" ||
  { echo "STOP: XR_RUNTIME_JSON missing in $DRY/launch-cmd.txt"; exit 1; }
grep -q "^# game only (--game-env):.* SOMVR_XR_LOG=$DRY/xr-layer\.log" "$DRY/launch-cmd.txt" ||
  { echo "STOP: @RUN@ not replaced in $DRY/launch-cmd.txt"; exit 1; }
rm -rf "$DRY"
echo "dry run ok; launching"
if [ -n "${HEADLESS_UNIT:-}" ]; then   # boot_check in a systemd user unit, as scripts/som-vr-headset.sh runs it
  case $HEADLESS_UNIT in somvr-headset|*[!a-z0-9-]*) echo "STOP: HEADLESS_UNIT must be [a-z0-9-] and not somvr-headset"; exit 1 ;; esac
  systemctl --user reset-failed "$HEADLESS_UNIT" 2>/dev/null
  systemd-run --user --quiet --collect --unit="$HEADLESS_UNIT" --description="SoM VR headless test (unit stop path)" \
    -p KillMode=mixed -p TimeoutStopSec=240 --working-directory="$SOM" --setenv=PATH=/usr/local/bin:/usr/bin:/bin \
    bash "$BC" "${B[@]}" || { echo "STOP: systemd-run failed"; exit 1; }
  echo "unit $HEADLESS_UNIT started (its output: journalctl --user -u $HEADLESS_UNIT; stop: systemctl --user stop $HEADLESS_UNIT)"
  while case $(systemctl --user is-active "$HEADLESS_UNIT" 2>/dev/null) in active|activating|deactivating|reloading) true ;; *) false ;; esac; do
    sleep 2
  done
  echo "unit $HEADLESS_UNIT ended"
else
  "$BC" "${B[@]}"
  echo "boot_check exit $?"
fi
