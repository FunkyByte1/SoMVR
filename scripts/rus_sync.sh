#!/bin/bash
# rus_sync.sh - copy what a headless Windows boot needs from this desktop to the test laptop rus (som-laptop, 11-06;
# handoff/HANDOFF-laptop.md). Every part lands at the SAME absolute path on rus (/home/bara is a link to /home/mith
# there), so the harness, the drivers and the run folders' paths are the desktop's. This side is only read: rsync reads
# here and writes on rus; rus never commits, pushes or copies back anything but run results (scripts/rus.sh).
# Run it OUTSIDE the Bash sandbox (ssh needs ~/.ssh).
#   rus_sync.sh PART...
#     game      the GOG install (/home/bara/Games/Heroic/Shadow of Mordor) - 54 GB the first time; on rus only a copy,
#               never written (headless_win.sh runs it through its overlay, as here)
#     proton    Steam's Proton - Experimental (the version the desktop runs) -> logs/rus/proton-experimental/ on rus
#               (not under rus's Steam: its Steam client would own and update it); the harness on rus uses it
#     prefix    our base prefix logs/winport/prefix (refused while a wineserver of it runs here: half-written
#               registry files); PREFIX_FROM=logs/winport/prefix-slotN copies that idle slot prefix as rus's base
#               instead. rus's slot prefixes (prefix-slot2) are reflink copies of its base, made once by game_slot.sh
#     save      logs/test-save (the pinned test save; the user's own saves are never copied)
#     harness   the main checkout's ShadowOfMordor (scripts, mod, tools, docs; no logs, backups, reference,
#               third_party) + logs/coord/final.sh, logs/coord/quitpath.cmd
#     tree DIR  a checkout, worktree or frozen build (DIR has ShadowOfMordor/, or DIR is logs/build-*): the same parts,
#               built files included (rus does not build: boots run what the desktop built)
#     path P..  each absolute path as it is (files or folders under /home/bara)
#     all       game proton prefix save harness
# The LAN is shared with WiVRn: while the user is in VR (somvr-headset active or status.json "user": true) the big
# parts (game, proton, prefix) are paused (SIGSTOP, resumed when the user leaves VR); the small ones run at
# RUS_VR_BWLIMIT (KB/s, default 4096). RUS_HOST (default mith@rus).
set -uo pipefail
RUS=${RUS_HOST:-mith@rus}; ROOT=/home/bara/Projects/flat2vr; SOM=$ROOT/ShadowOfMordor
ST=/run/user/$(id -u)/vr-session/status.json
SSHO="ssh -o BatchMode=yes -o ConnectTimeout=15 -o ServerAliveInterval=30 -o ControlMaster=auto -o ControlPath=/tmp/rus-ssh-%C -o ControlPersist=120"
VRBW=${RUS_VR_BWLIMIT:-4096}
user_in_vr() {
  [ "$(systemctl --user is-active somvr-headset 2>/dev/null)" = active ] && return 0
  tr -d ' \n' < "$ST" 2>/dev/null | grep -q '"user":true'
}
# the tree excludes: what a boot does not need (and never the user's or the desktop's state)
TEX=(--exclude=/logs/ --exclude=/backups/ --exclude=/reference --exclude=/third_party --exclude=.git --exclude='*.ppm'
     --exclude='*.pgm' --exclude=__pycache__)   # (relative to a tree's ShadowOfMordor/; FEX: to a frozen build's folder)
FEX=(--exclude=/proton/ --exclude=/ShadowOfMordor/logs/ --exclude=/ShadowOfMordor/reference --exclude=/ShadowOfMordor/third_party
     --exclude=.git --exclude='*.ppm' --exclude='*.pgm' --exclude=__pycache__)
push() {   # big|small SRC DST [rsync args]: SRC (a folder with a trailing / or a file) onto DST on rus
  local kind=$1 src=$2 dst=$3 p paused=0 bw=(); shift 3
  [ "$kind" = small ] && user_in_vr && bw=(--bwlimit="$VRBW")
  while [ "$kind" = big ] && user_in_vr; do echo "$(date +%T) waiting: the user is in VR (a big copy: $src)"; sleep 30; done
  if [ "$kind" = small ]; then   # (no pause: a small copy runs at RUS_VR_BWLIMIT in VR)
    rsync -a --mkpath --partial --info=stats1 -e "$SSHO" "${bw[@]}" "$@" "$src" "$RUS:$dst"; local rc=$?
    echo "$(date +%T) $src -> rus:$dst exit $rc"; return $rc
  fi
  setsid rsync -a --mkpath --partial --info=stats1 -e "$SSHO" "${bw[@]}" "$@" "$src" "$RUS:$dst" &
  p=$!
  while kill -0 "$p" 2>/dev/null; do
    if [ "$kind" = big ]; then
      if user_in_vr; then [ $paused = 0 ] && { kill -STOP -- "-$p" 2>/dev/null; paused=1; echo "$(date +%T) paused: the user is in VR"; }
      elif [ $paused = 1 ]; then kill -CONT -- "-$p" 2>/dev/null; paused=0; echo "$(date +%T) resumed"; fi
    fi
    sleep 3
  done
  wait "$p"; local rc=$?
  echo "$(date +%T) $src -> rus:$dst exit $rc"; return $rc
}
wineserver_of() {   # PREFIX: a wineserver of PREFIX/pfx runs here
  local w; for w in $(pgrep -x wineserver); do
    grep -qxF "WINEPREFIX=$1/pfx" < <(tr '\0' '\n' < "/proc/$w/environ" 2>/dev/null) && return 0; done; return 1
}
tree() {   # DIR: a tree's ShadowOfMordor (a frozen build logs/build-*: the folder, its Proton copy left out)
  local d; d=$(cd "${1:?tree DIR}" && pwd -P) || return 1
  case $d in /home/bara/*) ;; *) echo "STOP: $d is not under /home/bara"; return 1 ;; esac
  if [ -d "$d/ShadowOfMordor/mod/somvr" ]; then
    case $d in "$SOM"/logs/build-*) push small "$d/" "$d/" "${FEX[@]}" ;;
               *) push small "$d/ShadowOfMordor/" "$d/ShadowOfMordor/" --delete "${TEX[@]}" ;; esac
  else echo "STOP: $d has no ShadowOfMordor/mod/somvr"; return 1; fi
}
part() {
  case $1 in
    game) push big "/home/bara/Games/Heroic/Shadow of Mordor/" "/home/bara/Games/Heroic/Shadow of Mordor/" --delete ;;
    proton) push big "/home/bara/.local/share/Steam/steamapps/common/Proton - Experimental/" "$SOM/logs/rus/proton-experimental/" --delete --exclude=__pycache__ ;;
    prefix)
      local src=$SOM/logs/winport/prefix; [ -n "${PREFIX_FROM:-}" ] && src=$SOM/${PREFIX_FROM#"$SOM"/}
      wineserver_of "$src" && { echo "STOP: a wineserver of $src runs (a boot): try between boots or PREFIX_FROM=logs/winport/prefix-slotN"; return 1; }
      $SSHO "$RUS" "for w in \$(pgrep -x wineserver); do tr '\\0' '\\n' < /proc/\$w/environ | grep -qx 'WINEPREFIX=$SOM/logs/winport/prefix/pfx' && exit 1; done; exit 0" ||
        { echo "STOP: rus runs a boot in its base prefix"; return 1; }
      push big "$src/" "$SOM/logs/winport/prefix/" --delete --exclude=/pfx.lock ;;
    save) push small "$SOM/logs/test-save/" "$SOM/logs/test-save/" --delete ;;
    harness) tree "$ROOT" && push small "$SOM/logs/coord/final.sh" "$SOM/logs/coord/final.sh" &&
             push small "$SOM/logs/coord/quitpath.cmd" "$SOM/logs/coord/quitpath.cmd" ;;
    all) part game && part proton && part prefix && part save && part harness ;;
    *) echo "STOP: no part $1 (game proton prefix save harness tree path all)"; return 1 ;;
  esac
}
[ $# -gt 0 ] || { sed -n '2,25p' "$0"; exit 2; }
rc=0
while [ $# -gt 0 ]; do
  case $1 in
    tree) shift; [ $# -gt 0 ] || { echo "STOP: tree DIR"; exit 1; }
          while [ $# -gt 0 ] && [ -d "$1" ]; do tree "$1" || rc=1; shift; done ;;
    path) shift; while [ $# -gt 0 ]; do
            case $1 in /home/bara/*) p=${1%/}; [ -d "$p" ] && p=$p/; push small "$p" "$p" || rc=1 ;;
              *) echo "STOP: $1 is not under /home/bara"; rc=1 ;; esac; shift; done ;;
    *) part "$1" || rc=1; shift ;;
  esac
done
exit $rc
