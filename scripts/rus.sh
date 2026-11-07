#!/bin/bash
# rus.sh CMD [ARGS...] - the one word for a boot on the test laptop rus (som-laptop, 11-06; handoff/HANDOFF-laptop.md):
# put `scripts/rus.sh` in front of the command you would run here, e.g.
#   scripts/rus.sh bash -c 'env P31_WT=som-x bash scripts/polish31-drive.sh > /tmp/d.txt & HEADLESS_SLOT=auto \
#       HEADLESS_OWNER=som-x HEADLESS_SLOT_WAIT=3600 scripts/headless_vr.sh --win $PWD/mod/somvr; wait'
# It (1) copies to rus, at the same paths, the main checkout's harness, the pinned test save and every tree the command
# names (this folder's checkout and each /home/bara/... word: a worktree, a frozen logs/build-*, a logs/ file or folder;
# built files included - rus never builds), (2) runs the command there in this folder with this shell's HEADLESS_*,
# WIN_*, SOMVR_*, FINAL_*, GAME_SLOT* and LIVE_* variables (and those named in RUS_ENV), in a mount namespace where
# /home/bara is a real folder (on rus it is a link to /home/mith: `pwd -P` and every log path stay /home/bara/...),
# (3) copies back every file the command wrote under logs/ (the run folders logs/boot-check-*, a suite's
# logs/coord/...) into this desktop's logs/ - new files only, never over one of the desktop's; the raw .ppm/.pgm
# dumps stay on rus (RUS_DUMPS=1: those too; or later: rus.sh pull RUN). Exit = the command's.
# rus has its own slots (game_slot.sh there: 2, its own /tmp locks; `scripts/rus.sh scripts/game_slot.sh status`),
# no VR gate of its own (the user's VR is this desktop's: rus boots run while the user plays) and its own prefixes.
# Stopping rus.sh (TERM/INT: at once; KILL or its machine gone: no heartbeat, within ~100 s) stops the command on
# rus (TERM to its process group: the harness's
# traps release the slot and stop the game). Perf and frame-time tests stay on the desktop (another GPU and CPU).
#   rus.sh pull RUN...   copy RUN folders (or files under logs/) from rus as they are now, dumps included
# Run it OUTSIDE the Bash sandbox (ssh). RUS_HOST (default mith@rus). Big parts (the game, Proton, the base prefix) are
# not copied here: scripts/rus_sync.sh game|proton|prefix after they change (rus.sh says when Proton differs).
set -uo pipefail
RUS=${RUS_HOST:-mith@rus}; ROOT=/home/bara/Projects/flat2vr; SOM=$ROOT/ShadowOfMordor; HERE=$(cd "$(dirname "$0")" && pwd)
SSHO=(ssh -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=30 -o ControlMaster=auto -o ControlPath=/tmp/rus-ssh-%C
      -o ControlPersist=120)   # (one connection for the syncs and the command: rus_sync.sh uses the same path)
ST=/run/user/$(id -u)/vr-session/status.json
[ "$(hostname -s)" = rus ] && { echo "STOP: rus.sh runs on the desktop (this is rus)"; exit 1; }
[ $# -gt 0 ] || { sed -n '2,25p' "$0"; exit 2; }
user_in_vr() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  tr -d ' \n' < "$ST" 2>/dev/null | grep -q '"user":true'
}
bw() { user_in_vr && echo "--bwlimit=${RUS_VR_BWLIMIT:-4096}"; }   # (the LAN carries WiVRn while the user is in VR)
# (and a /tmp/.X11-unix of its own: in the user namespace the real one's owner, root, shows as nobody, and gamescope's
# Xwayland refuses it - "not owned by root or us"; the display numbers stay apart through /tmp/.X<n>-lock)
NS="bwrap --dev-bind / / --tmpfs /home --bind /home/mith /home/mith --bind /home/mith /home/bara --setenv HOME /home/bara"
NS+=" --perms 1777 --tmpfs /tmp/.X11-unix"
# (and the desktop's Proton at the desktop's path - our prefixes' system32 files are links into it; on rus the copy is
# logs/rus/proton-experimental, mounted there only inside the namespace: rus's own Steam folder gets an empty mount point)
NS+=" --bind $SOM/logs/rus/proton-experimental '/home/bara/.local/share/Steam/steamapps/common/Proton - Experimental' --"
pull() {   # LIST-FILE (paths relative to logs/, NUL-separated) [extra rsync args]: rus -> here, new files only
  local l=$1; shift
  rsync -a --from0 --files-from="$l" --ignore-existing -e "${SSHO[*]}" $(bw) "$@" "$RUS:$SOM/logs/" "$SOM/logs/"
}
if [ "$1" = pull ]; then
  shift; L=$(mktemp); rc=0
  for r in "$@"; do r=${r%/}; r=${r#"$SOM/logs/"}
    "${SSHO[@]}" "$RUS" "cd '$SOM/logs' && find '$r' \( -type f -o -type l \) -print0" > "$L" && pull "$L" || rc=1
  done; rm -f "$L"; exit $rc
fi
# ---- 1. the trees ----------------------------------------------------------------------------------------------
CWD=$(pwd -P)
declare -A TREES=() PATHS=()
root_of() {   # PATH: the tree it lies in (a folder with ShadowOfMordor/mod/somvr), else nothing
  local d=$1
  while [ -n "$d" ] && [ "$d" != / ]; do
    [ -d "$d/ShadowOfMordor/mod/somvr" ] && { echo "$d"; return 0; }
    d=$(dirname "$d")
  done; return 1
}
consider() {   # WORD: a /home/bara/... path -> its tree (or, under logs/ and in no tree, the path itself)
  local w=$1 p r
  case $w in   # (a frozen build's bare name, final.sh's BUILD: logs/build-X)
    build-*) [[ $w =~ ^[A-Za-z0-9._-]+$ ]] && [ -d "$SOM/logs/$w/ShadowOfMordor/mod/somvr" ] && TREES[$SOM/logs/$w]=1; return 0 ;;
    /home/bara/*) ;; *) return 0 ;; esac
  p=$w; while [ -n "$p" ] && [ "$p" != / ] && [ ! -e "$p" ]; do p=$(dirname "$p"); done   # (a file the run will write)
  [ -e "$p" ] || return 0
  if r=$(root_of "$p"); then TREES[$r]=1
  elif [ "$p" = "$w" ]; then   # (an existing file or folder under logs/, below its top level: a driver, a save, a cmd file)
    case $p in "$SOM"/logs/?*/?*) PATHS[$p]=1 ;; esac
  fi
}
consider "$CWD"
FWD=(); for v in $(compgen -v); do
  case $v in HEADLESS_*|WIN_*|SOMVR_*|FINAL_*|GAME_SLOT*|LIVE_*) FWD+=("$v=${!v}") ;; esac
done
for v in ${RUS_ENV:-}; do [ -n "${!v+x}" ] && FWD+=("$v=${!v}"); done
for a in "$@" "${FWD[@]}"; do
  for w in $(printf '%s' "$a" | tr "=:;,'\"()" '        '); do consider "$w"; done
done
unset 'TREES[/home/bara/Projects/flat2vr]'   # (the main checkout: always, as the harness)
SYNC=(save harness); for t in "${!TREES[@]}"; do SYNC+=(tree "$t"); done
[ ${#PATHS[@]} -gt 0 ] && SYNC+=(path "${!PATHS[@]}")
echo "rus: syncing ${SYNC[*]}"
( flock -w 900 9 || { echo "STOP: another rus.sh sync held /tmp/rus-sync.lock for 15 minutes"; exit 1; }
  bash "$HERE/rus_sync.sh" "${SYNC[@]}" ) 9> /tmp/rus-sync.lock > "${TMPDIR:-/tmp}/rus-sync.$$.log" 2>&1 ||
  { echo "STOP: the sync to rus failed:"; tail -5 "${TMPDIR:-/tmp}/rus-sync.$$.log"; rm -f "${TMPDIR:-/tmp}/rus-sync.$$.log"; exit 1; }
rm -f "${TMPDIR:-/tmp}/rus-sync.$$.log"
PV=$(cat "/home/bara/.local/share/Steam/steamapps/common/Proton - Experimental/version" 2>/dev/null)
RV=$("${SSHO[@]}" "$RUS" "cat '$SOM/logs/rus/proton-experimental/version' 2>/dev/null")
[ "$PV" = "$RV" ] || echo "rus: NOTE - Proton differs (desktop '$PV', rus '$RV'): scripts/rus_sync.sh proton"
# ---- 2. the command on rus --------------------------------------------------------------------------------------
Q=$(printf '%q ' "$@"); E=""; [ ${#FWD[@]} -gt 0 ] && E=$(printf '%q ' "${FWD[@]}")
HB=/tmp/rus-alive.$(hostname -s).$$.$(date +%s)   # (the heartbeat: touched from here every 20 s while this script lives)
REMOTE=$(cat <<EOF
st=\$(mktemp /tmp/rus-run.XXXXXX) || exit 97; echo "RUS_STAMP \$st"; touch $HB
cd $(printf '%q' "$CWD") || exit 96
setsid env $E $NS $Q < /dev/null &
p=\$!; echo \$p > $HB
( while kill -0 \$p 2>/dev/null; do sleep 10
    [ -n "\$(find $HB -mmin -1.5 2>/dev/null)" ] && continue   # (the desktop side gone 90 s: the command stops)
    echo "rus: no heartbeat from the desktop for 90 s: stopping the command"; kill -TERM -- -\$p 2>/dev/null
    sleep 20; kill -KILL -- -\$p 2>/dev/null; break; done ) > /dev/null 2>&1 &
w=\$!
wait \$p; rc=\$?; kill \$w 2>/dev/null; rm -f $HB; exit \$rc
EOF
)
OUT=$(mktemp); trap 'rm -f "$OUT"' EXIT
# (the heartbeat: while this script lives; its end, even by KILL, stops the command on rus within ~100 s)
( while kill -0 $$ 2>/dev/null; do sleep 20; "${SSHO[@]}" "$RUS" "touch $HB" < /dev/null > /dev/null 2>&1; done ) &
HBP=$!
# (a TERM/INT here: the command on rus is told at once - its group gets TERM; this script then copies back as usual)
stop_remote() { echo "rus: stopping the command on rus"; "${SSHO[@]}" "$RUS" "p=\$(cat $HB 2>/dev/null) && kill -TERM -- -\$p" < /dev/null; }
trap stop_remote TERM INT HUP
"${SSHO[@]}" "$RUS" "bash -c $(printf '%q' "$REMOTE")" < /dev/null > >(tee "$OUT") 2>&1 &
SP=$!
while :; do wait $SP; RC=$?; kill -0 $SP 2>/dev/null || break; done   # (wait returns early on a trapped signal)
trap - TERM INT HUP; kill $HBP 2>/dev/null; sleep 0.5
STAMP=$(sed -n 's/^RUS_STAMP //p' "$OUT" | head -1)
[ -n "$STAMP" ] || { echo "rus: the command did not start (exit $RC)"; exit "$RC"; }
# ---- 3. what it wrote under logs/ back here -----------------------------------------------------------------------
L=$(mktemp)
DX="! -name '*.ppm' ! -name '*.pgm'"; [ "${RUS_DUMPS:-0}" = 1 ] && DX=""
"${SSHO[@]}" "$RUS" "cd '$SOM/logs' && find . \( -path ./winport/prefix -o -path './winport/prefix-*' -o -path ./rus \) -prune -o \
  \( -type f -o -type l \) -newer '$STAMP' $DX -print0 2>/dev/null; rm -f '$STAMP'" > "$L"   # (2>/dev/null: the overlay's work folder is unreadable)
# a run folder this desktop already has and that is not rus's (rus names end in -1, game_slot.sh GS_RUN_SUFFIX, so
# this is only a guard): none of its files copied - said
CL=$(tr '\0' '\n' < "$L" | sed -n -E 's#^\./((boot-check-|headless-|winport/run-)[^/]+)/.*#\1#p' | sort -u | while read -r r; do
       [ -e "$SOM/logs/$r" ] && [ "$(cat "$SOM/logs/$r/host.txt" 2>/dev/null)" != rus ] && echo "$r"; done)
if [ -n "$CL" ]; then
  echo "rus: !!! run name(s) also on the desktop, NOT copied back (rus.sh pull from logs/rus-clash/ by hand): $CL"
  for r in $CL; do tr '\0' '\n' < "$L" | grep -v "^\./$r/" | tr '\n' '\0' > "$L.2"; mv "$L.2" "$L"; done
fi
N=$(tr -cd '\0' < "$L" | wc -c)
if pull "$L"; then echo "rus: $N files copied back into $SOM/logs/ (new ones only; dumps ${DX:+left on rus})"
else echo "rus: !!! the copy back failed (rus.sh pull RUN)"; fi
rm -f "$L"
exit "$RC"
