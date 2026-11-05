#!/bin/bash
# ladder_boot.sh - som-b37-ladder (the story save ladder, GAME-STUDY item 1.6; docs/re-xr-pipeline.md 172): one headless
# Windows boot from a story ladder rung's save, so a driver can start that mission at its card.
#   ladder_boot.sh RUNG [DRIVER [K=V ...]]   |   ladder_boot.sh new [DRIVER [K=V ...]]
#   RUNG    a rung under logs/test-save/ladder/ (its folder name, e.g. 18-the-rescue, or its number: 18)
#   FILE    a save file (e.g. a ladder run's saves/after-x.sav: a rung to be, before ladder_rung.sh keeps it), read only
#   new     a NEW GAME: the ladder's own prefix logs/winport/prefix-ladder (a reflink copy of logs/winport/prefix, made
#           once by hand) with its save moved aside (into prefix-ladder/old-saves), HEADLESS_SAVE_FROM=none, the commands
#           cmds/win-ladder-new.cmd (the main menu, then pictures; the menu's way is appended to RUN/cmd); no driver
#           unless one is named. The prologue's first save is then the run's saves/GameData.sav.
#   DRIVER  a driver in this scripts/ folder, run beside the boot (default ladder-drive.sh: the rung's place, light and
#           markers, then its mission card); it gets LADDER_RUN=RUN (also ST_RUN, BG_RUN, PIN_RUN, P18_RUN), LADDER_RUNG=
#           the rung's folder and the K=V given here (e.g. LADDER_STEPS='look start complete save~after').
# The boot: scripts/headless_vr.sh --win (the GOG build under Proton) in a game slot (HEADLESS_SLOT, default auto: the first
# free one, waiting up to HEADLESS_SLOT_WAIT s, default 3600), the slot's own prefix with a COPY of the rung's save (the
# rung's file is only read: its sha256 is checked before and after against GameData.sav.sha256), the main menu's
# Continue, scripts/headset-build26.env's switches + the test hands, the practice tools and the test aids
# (SOMVR_HANDS_TEST=1: never a build's env) + LADDER_EXTRA ("K=V ..."). HEADLESS_HOLD (default 1500 s), HEADLESS_OWNER
# (default som-b37-ladder). At the end the run keeps the prefix's save (RUN/saves/GameData.sav, headless_win.sh) - the
# start of a new rung once a mission ended in it (scripts/ladder_rung.sh). Run it outside the Bash sandbox.
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; HERE=$(cd "$(dirname "$0")" && pwd)
L=$SOM/logs/test-save/ladder
RG=${1:?usage: ladder_boot.sh RUNG|new [DRIVER [K=V ...]]}; shift
NEW=0; [ "$RG" = new ] && NEW=1
if [ $NEW = 1 ]; then   # a new game: the ladder's own prefix with no save (never a slot's prefix, never the user's)
  NP=$SOM/logs/winport/prefix-ladder; NS="$NP/pfx/drive_c/users/steamuser/Documents/WB Games/Shadow of Mordor/Saves"
  [ -d "$NP/pfx/drive_c/windows/system32" ] || { echo "STOP: no $NP (a reflink copy of logs/winport/prefix: cp -a --reflink=always, while no wineserver runs it)"; exit 1; }
  for p in $(pgrep -x wineserver); do tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$NP/pfx" && { echo "STOP: $NP is in use (wineserver $p)"; exit 1; }; done
  [ -f "$NS/GameData.sav" ] && { mkdir -p "$NP/old-saves"; mv "$NS/GameData.sav" "$NP/old-saves/GameData.sav.$(date +%Y%m%d-%H%M%S)"; }
  export WIN_PREFIX=$NP; D=$NP; S=""; SHA0=none
  DRV=${1:-}; [ $# -gt 0 ] && shift
  [ -z "$DRV" ] || [ -f "$HERE/$DRV" ] || { echo "STOP: no driver $HERE/$DRV"; exit 1; }
elif [ -f "$RG" ]; then   # a save file (a run's saves/*.sav: a rung to be): its sha256 checked before and after
  S=$(realpath "$RG"); D=$(dirname "$S"); SHA0=$(sha256sum "$S" | cut -d' ' -f1); NEW=2
  case $S in */Steam/*|*/Heroic/*|*/win-play/*|*/compatdata/*) echo "STOP: $S looks like the user's own save"; exit 1 ;; esac
  DRV=${1:-ladder-drive.sh}; [ $# -gt 0 ] && shift
  [ -f "$HERE/$DRV" ] || { echo "STOP: no driver $HERE/$DRV"; exit 1; }
elif [ ! -d "$L/$RG" ]; then
  M=$(compgen -G "$L/${RG}-*" | head -2); [ "$(printf '%s\n' "$M" | grep -c .)" = 1 ] || { echo "STOP: no rung $RG (or more than one) in $L"; ls "$L"; exit 1; }
  RG=$(basename "$M")
fi
if [ $NEW = 0 ]; then
D=$L/$RG; S=$D/GameData.sav
[ -f "$S" ] && [ -f "$D/GameData.sav.sha256" ] || { echo "STOP: $D has no GameData.sav + GameData.sav.sha256"; exit 1; }
SHA0=$(sha256sum "$S" | cut -d' ' -f1)
[ "$SHA0" = "$(cut -d' ' -f1 "$D/GameData.sav.sha256")" ] || { echo "STOP: $S does not match its GameData.sav.sha256"; exit 1; }
DRV=${1:-ladder-drive.sh}; [ $# -gt 0 ] && shift
[ -f "$HERE/$DRV" ] || { echo "STOP: no driver $HERE/$DRV"; exit 1; }
fi
MOD=${LADDER_MOD:-$(cd "$HERE/../mod/somvr" && pwd -P)}
ENVF=$HERE/headset-build26.env
X="$(grep -E '^SOMVR_[A-Z0-9_]+=' "$ENVF" | tr '\n' ' ') SOMVR_HANDS_TEST=1 SOMVR_XR_TEST=@RUN@/xrtest.txt SOMVR_EYE_SIZE=2015x2217"
X="$X SOMVR_PRACTICE=1 SOMVR_PRACTICE_QUIET=0 SOMVR_PRACTICE_CLEAR=0 SOMVR_PRACTICE_DAY=0 ${LADDER_EXTRA:-}"
O=$(mktemp /tmp/ladder-boot-XXXXXX.out)
echo "$(date +%T) rung $RG (save ${SHA0:0:16}), driver $DRV $*; boot output $O"
HEADLESS_SLOT=${HEADLESS_SLOT:-auto} HEADLESS_OWNER=${HEADLESS_OWNER:-som-b37-ladder} HEADLESS_SLOT_WAIT=${HEADLESS_SLOT_WAIT:-3600} \
  HEADLESS_SAVE_FROM=${S:-none} HEADLESS_HOLD=${HEADLESS_HOLD:-1500} SOMVR_EXTRA="$X" \
  HEADLESS_CMDS=${HEADLESS_CMDS:-$([ $NEW = 1 ] && echo "$HERE/cmds/win-ladder-new.cmd" || echo "$HERE/cmds/win-vr6.cmd")} \
  setsid -w bash "$HERE/headless_vr.sh" --win "$MOD" > "$O" 2>&1 &
BP=$!
RUN=""
while kill -0 $BP 2>/dev/null; do
  RUN=$(grep -m1 -oE '^windows: [^ ]+' "$O" | cut -c10-); [ -n "$RUN" ] && [ -d "$RUN" ] && break; RUN=""; sleep 2
done
if [ -n "$RUN" ] && [ -n "$DRV" ]; then
  echo "$(date +%T) run $RUN"; echo "$RG $DRV $*" > "$RUN/ladder-rung.txt"
  ( cd "$HERE" && env LADDER_RUN="$RUN" ST_RUN="$RUN" BG_RUN="$RUN" PIN_RUN="$RUN" P18_RUN="$RUN" LADDER_RUNG="$D" "$@" \
      bash "./$DRV" ) > "$RUN/ladder-driver.log" 2>&1 &
  DP=$!
fi
wait $BP; RC=$?
[ -n "${DP:-}" ] && { sleep 5; kill $DP 2>/dev/null; wait $DP 2>/dev/null; }
grep -E '^(game slot|STOP|windows:)|save|WARN|crash|headless_win exit' "$O" | cut -c1-220 | tail -12
if [ -n "$S" ]; then
  SHA1=$(sha256sum "$S" | cut -d' ' -f1)
  [ "$SHA1" = "$SHA0" ] && echo "rung save UNCHANGED (${SHA0:0:16})" || echo "rung save CHANGED !!! ${SHA0:0:16} -> ${SHA1:0:16}"
fi
[ -n "$RUN" ] && { [ -f "$RUN/saves/GameData.sav" ] && echo "the run's save: $RUN/saves/GameData.sav ($(sha256sum "$RUN/saves/GameData.sav" | cut -c1-16))"
                   echo "driver log: $RUN/ladder-driver.log"; }
exit $RC
