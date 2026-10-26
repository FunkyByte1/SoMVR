#!/bin/bash
# headless_vr.sh - one headless VR test boot (Monado's null compositor as the OpenXR runtime, boot_check's headless
# gamescope, the user's save copy) with every switch of the user's headset build (scripts/som-vr-headset.sh), for helpers
# and the coordinator. Run it OUTSIDE the Bash sandbox, in the background. It refuses when the slot is busy (boot_check).
#   headless_vr.sh /ABS/PATH/TO/mod/somvr      (a built mod folder: build/somvr.so and vklayer/build/...)
# Environment: HEADLESS_CMDS (default scripts/cmds/somvr-vr6.cmd: straight to the main menu's Continue, the practice
#   meadow; with the launch below), HEADLESS_HOLD (seconds, default 900), SOMVR_EXTRA ("K=V K=V": more or overriding
#   --game-env), HEADLESS_LAUNCH=old (the build-5 launch: the pre-game window, the distro box and the logo movies, for
#   somvr-vr5.cmd), HEADLESS_UNIT=NAME (boot_check runs in a systemd user unit NAME with KillMode=mixed, as the user's
#   headset session does: `systemctl --user stop NAME` = the PC window's Stop; this script waits for the unit).
#   The launch (build 6): --no-feral-options and the run's own prefs with GameOptionsDialogShouldShow 0,
#   SkipOSWarnings 1, SpecificationAlerts\LnxDistributionUnsupported 1 and DisableSplashVideos 1 (docs/re-launcher.md);
#   SOMVR_MARK=1 and SOMVR_QUIT=1 (bug marks, the menu's Quit game). boot_check.sh: the one next to this script.
#   Live commands: append lines to
#   RUN/cmd (boot_check's command mode: shot, key, wait, mark, quit...); trigger words (e.g. `practice spawn shield`)
#   go into RUN/somvr.trigger directly. Monado's log: logs/headless-<ts>/monado.log; the run: the newest
#   logs/boot-check-<ts>/. Note: the null compositor paces at 20 Hz, so frame rates here say nothing about 90 fps.
# (From the coordinator's run13/run14 scripts, 2026-10-22.)
SOM=$HOME/Projects/flat2vr/ShadowOfMordor; RT=/run/user/$(id -u); MSVC=""
HERE=$(cd "$(dirname "$0")" && pwd); BC=$HERE/boot_check.sh   # (a worktree's copy runs its own boot_check)
MOD=${1:?usage: headless_vr.sh /ABS/PATH/TO/mod/somvr}; MOD=$(cd "$MOD" 2>/dev/null && pwd -P) || { echo "STOP: no folder $1"; exit 1; }
[ -f "$MOD/build/somvr.so" ] && [ -f "$MOD/vklayer/build/libVkLayer_somvr_openxr.so" ] || { echo "STOP: $MOD is not built"; exit 1; }
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
trap stop_monado EXIT
setsid chrt -o 0 env -i HOME="$R5/mxdg" PATH=/usr/bin:/bin XDG_RUNTIME_DIR=$RT \
  XDG_CONFIG_HOME="$R5/mxdg/config" XDG_CACHE_HOME="$R5/mxdg/cache" XDG_DATA_HOME="$R5/mxdg/data" \
  SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
  XRT_LOG=info XRT_COMPOSITOR_LOG=info XRT_PRINT_OPTIONS=on \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.x86_64.json \
  timeout -s INT -k 10 1800 /usr/bin/monado-service > "$R5/monado.log" 2>&1 < /dev/null &
TPID=$!
for i in $(seq 1 50); do [ -S $RT/monado_comp_ipc ] && break; sleep 0.2; done
MSVC=$(pgrep -P "$TPID" -x monado-service)
[ -n "$MSVC" ] && tr '\0' '\n' < /proc/$MSVC/environ | grep -qx "XDG_CONFIG_HOME=$R5/mxdg/config" ||
  { echo "STOP: no Monado of ours (pid ${MSVC:-none})"; tail -30 "$R5/monado.log"; kill -INT "$TPID" 2>/dev/null; exit 1; }
grep -q 'Null compositor starting' "$R5/monado.log" || { sleep 2; grep -q 'Null compositor starting' "$R5/monado.log"; } || { echo "STOP: no null compositor"; tail -30 "$R5/monado.log"; exit 1; }
echo "monado up pid $MSVC"
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
LA=(--game-arg --no-feral-options --prefs-set GameOptionsDialogShouldShow=0 --prefs-set GameOptionsDialogShouldShowBigPicture=0
    --prefs-set SkipOSWarnings=1 --prefs-set 'SpecificationAlerts\LnxDistributionUnsupported=1'
    --prefs-set 'SpecificationAlerts\LnxCPUGovernorSubOptimal=1' --prefs-set DisableSplashVideos=1)
DCMDS=$HERE/cmds/somvr-vr6.cmd
[ "${HEADLESS_LAUNCH:-}" = old ] && { LA=(); DCMDS=$HERE/cmds/somvr-vr5.cmd; }
B=(--steam --save-copy --seed-prefs $SOM/logs/boot-check-20261018-003728/preferences.seeded "${LA[@]}"
   --preload $MOD/build/somvr.so "${GE[@]}" --cmds "${HEADLESS_CMDS:-$DCMDS}" --hold "${HEADLESS_HOLD:-900}" --refresh 90)
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
