#!/usr/bin/env bash
# test_som_vr.sh - offline test of scripts/som-vr.sh (the user's Steam launch-option wrapper). Launches nothing real:
# a copy of the wrapper runs in a temp tree (fake builds, the real layer manifest) with a stub as %command% that writes
# its arguments and exported environment and exits 7. Each run starts from `env -i` with a fake HOME/XDG, so the real
# ~/.config/openxr, Steam, Feral's prefs and the game are never looked at.
# Checks: --make-release from mod/somvr and from a frozen build folder (absolute, relative to the project; HEAD.txt as
# the source commit; release.prev); VR mode env = VR build 3's game env (scripts/cmds/somvr-vr3.cmd's header) with
# LD_PRELOAD appended to Steam's, the layer env, logs, the VR menu's settings file (an inherited one wins, no folder
# made), no save guard; --no-feral-options appended in VR mode only (not
# twice); som-vr.conf: good lines (comments, blanks, quotes, CRLF, no final newline, the last line wins, off = unset),
# bad values, malformed lines, unknown and forbidden keys (the save guard, the trigger, LD_PRELOAD), no command
# substitution, an inherited value wins, unreadable -> ignored, every key it takes is one somvr/the layer reads; flat
# mode (SOMVR_PLAY=flat, disable_vr.txt, no headset) = the environment and arguments exactly as given, som-vr.conf not
# read; a missing / mismatched / unlisted release and a missing manifest -> pass-through; arguments with spaces, quotes,
# globs, empty and newline preserved; exit status passed on; the wrapper's own tools ignore a hostile LD_LIBRARY_PATH.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../som-vr.sh
MANIFEST=$HERE/../../mod/somvr/vklayer/VkLayer_somvr_openxr.json
VR3CMD=$HERE/../cmds/somvr-vr3.cmd
T=$(mktemp -d "${TMPDIR:-/tmp}/test_som_vr.XXXXXX")
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
PASS=0 FAIL=0
ok() { PASS=$((PASS + 1)); echo "ok   $*"; }
no() { FAIL=$((FAIL + 1)); echo "FAIL $*"; }
check() { local d=$1; shift; if "$@"; then ok "$d"; else no "$d"; fi; }

W=$T/som   # stands for ShadowOfMordor/
R=$W/release
C=$W/som-vr.conf
mkdir -p "$W/scripts" "$W/mod/somvr/build" "$W/mod/somvr/vklayer/build" "$T/out" "$T/cfg/openxr/1" "$T/home" \
	"$T/rtlib" "$T/xdgdirs/openxr/1"
cp "$SRC" "$W/scripts/som-vr.sh" && chmod 755 "$W/scripts/som-vr.sh"
cp "$MANIFEST" "$W/mod/somvr/vklayer/VkLayer_somvr_openxr.json"
# empty ELF objects where a process will really preload them (the stub's shell); bytes elsewhere
mkso() { gcc -shared -fPIC -nostdlib -o "$1" -x c /dev/null ${2:+-Wl,-soname,$2} 2>/dev/null || printf 'not an elf %s\n' "$1" >"$1"; }
mkso "$W/mod/somvr/build/somvr.so"
mkso "$T/overlay32.so"
mkso "$T/overlay64.so"
printf 'fake layer library\n' >"$W/mod/somvr/vklayer/build/libVkLayer_somvr_openxr.so"
mkso "$T/rtlib/libz.so.1" libz.so.1   # a zlib without symbols, as an old runtime copy could break host tools
touch "$W/mod/somvr/build/somvr.so"   # newer than nothing: no "sources newer" warning expected

cat >"$T/stub" <<EOF
#!/bin/sh
for a in "\$@"; do printf '[%s]\n' "\$a"; done >"$T/out/args"
export -p >"$T/out/env"
echo "stub stdout"
echo "stub stderr" >&2
exit 7
EOF
chmod 755 "$T/stub"

ARGS=("plain" "with space" "" "two  spaces" "*" "quote'\"" $'new\nline' "--fresh-prefs" '$HOME' "%command%")
printf '[%s]\n' "${ARGS[@]}" >"$T/args.expected"
printf '[%s]\n' "${ARGS[@]}" --no-feral-options >"$T/args.vr"
BASE=(HOME="$T/home" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/cfg" XDG_CONFIG_DIRS="$T/nocfg" SOMVR_SKIPOS=0) # SKIPOS: own section
WRAP=$W/scripts/som-vr.sh
RC=0
# run [K=V...]: the wrapper with a fresh env, the stub as the command; rc in RC, stderr in out/stderr
run() { rm -f "$T"/out/*; env -i "${BASE[@]}" "$@" "$WRAP" "$T/stub" "${ARGS[@]}" >"$T/out/stdout" 2>"$T/out/stderr"; RC=$?; }
# direct [K=V...]: the stub alone with the same env (the flat-mode reference)
direct() { rm -f "$T"/out/*; env -i "${BASE[@]}" "$@" "$T/stub" "${ARGS[@]}" >/dev/null 2>&1; cp "$T/out/env" "$T/env.direct"; }
envf() { grep -v -E '^(export|declare -x) (SHLVL|PWD|OLDPWD|_)(=|$)' "$1"; }
getv() { # value of an exported var in out/env (bash posix `export -p` format), or <unset>
	bash -c 'set -a; unset LD_PRELOAD; . "$1" 2>/dev/null; if [ -n "${!2+x}" ]; then printf %s "${!2}"; else printf "<unset>"; fi' \
		_ "$T/out/env" "$1"
}
is() { [ "$(getv "$1")" = "$2" ] || { echo "     $1=[$(getv "$1")] expected [$2]" >&2; return 1; }; }
are() { # K=V... all as given (V <unset> = unset)
	local kv bad=0
	for kv; do is "${kv%%=*}" "${kv#*=}" || bad=1; done
	return $bad
}
plays() { ls -1d "$W"/logs/play-* 2>/dev/null | wc -l; }
lastplay() { ls -1d "$W"/logs/play-* 2>/dev/null | tail -n 1; }
lastlog() { tail -n 1 "$W/logs/som-vr.log" 2>/dev/null; }
loglines() { wc -l <"$W/logs/som-vr.log" 2>/dev/null || echo 0; }
args_ok() { cmp -s "$T/out/args" "$T/args.expected"; }
args_vr() { cmp -s "$T/out/args" "$T/args.vr"; }
vr_ran() { # the last run went VR: exit 7, args + --no-feral-options, SOMVR_LOG in a logs/play-* folder that exists
	local l; l=$(getv SOMVR_LOG)
	[ $RC = 7 ] && args_vr && [[ $l == "$W"/logs/play-*/somvr.log ]] && [ -d "${l%/*}" ]
}
flat_same() { # run the wrapper and the stub alone with the same env: same args, same env, rc 7, no new play folder
	local before; before=$(plays)
	run "$@"
	local rcw=$RC
	cp "$T/out/env" "$T/env.wrapped" 2>/dev/null || : >"$T/env.wrapped"
	direct "$@"
	[ $rcw = 7 ] && args_ok && diff <(envf "$T/env.direct") <(envf "$T/env.wrapped") >/dev/null && [ "$(plays)" = "$before" ]
}
# conf LINES...: write som-vr.conf (each argument one line, a final newline); noconf: remove it
conf() { printf '%s\n' "$@" >"$C"; }
noconf() { rm -f "$C"; }
# new_notes N: the som-vr.log lines since line N
new_notes() { tail -n +"$(($1 + 1))" "$W/logs/som-vr.log" 2>/dev/null; }

# VR build 3's game env: the SOMVR_* of the headset session's launch line (not its harness-only log/dump switches;
# SOMVR_SETTINGS is the launcher's own, checked below)
mapfile -t VR3 < <(grep -oE -- '--game-env SOMVR_[A-Z0-9_]+=[^ ]+' "$VR3CMD" | sed 's/^--game-env //' |
	grep -vE '^SOMVR_(XR|XR_LOG|XR_DUMP|SETTINGS)=' | sort -u)
check "vr3.cmd: its header has the 23 VR build 3 switches" [ ${#VR3[@]} = 23 ]
check "vr3.cmd: VR build 1b's 15 with the vignette off, plus the menu, the sky, the anchor, LOCO, REACT, bow, climb, threat" \
	bash -c '[ "$(printf "%s\n" "${@:1}" | tr "\n" " ")" = "SOMVR_ANCHOR=root SOMVR_ARMS=1 SOMVR_AUDIO=head SOMVR_BASE=head SOMVR_BLADE=controller SOMVR_BOW=1 SOMVR_CLIMB=b SOMVR_COMBAT=1 SOMVR_COMBAT_REACT=1 SOMVR_COMFORT=1 SOMVR_EYE_SIZE=xr SOMVR_FALLBACK=auto SOMVR_HIDE_HEAD=near SOMVR_HUD=quad SOMVR_INPUT=1 SOMVR_LOCO=1 SOMVR_POINTER=1 SOMVR_STEREO=B SOMVR_THREAT=1 SOMVR_VIEWS=xr SOMVR_VIGNETTE=0 SOMVR_VRMENU=1 SOMVR_XR_ENV=on " ]' _ "${VR3[@]}"
check "vr3.cmd: SOMVR_SETTINGS is the launcher's file" grep -q -- '--game-env SOMVR_SETTINGS=$HOME/.local/share/somvr/settings.conf ' "$VR3CMD"

# ---- --make-release -------------------------------------------------------------------------------------------------
env -i "${BASE[@]}" "$WRAP" --make-release >"$T/mr.out" 2>&1
check "make-release: exit 0" [ $? = 0 ]
check "make-release: the three files + SHA256SUMS + release.txt" \
	test -f "$R/somvr.so" -a -f "$R/libVkLayer_somvr_openxr.so" -a -f "$R/VkLayer_somvr_openxr.json" -a -f "$R/SHA256SUMS" -a -f "$R/release.txt"
check "make-release: SHA256SUMS verifies (sha256sum -c), 3 entries" \
	bash -c 'cd "$1" && sha256sum --quiet --strict -c SHA256SUMS && [ $(wc -l <SHA256SUMS) = 3 ]' _ "$R"
check "make-release: manifest library_path -> ./libVkLayer_somvr_openxr.so, otherwise the source manifest" \
	bash -c 'grep -q "\"library_path\": \"./libVkLayer_somvr_openxr.so\"" "$1/VkLayer_somvr_openxr.json" &&
		diff <(sed "/library_path/d" "$1/VkLayer_somvr_openxr.json") <(sed "/library_path/d" "$2") >/dev/null' _ "$R" "$MANIFEST"
check "make-release: no 'sources newer' warning for a fresh build" bash -c '! grep -q WARNING "$1"' _ "$T/mr.out"
check "make-release: release.txt names mod/somvr and a git source line" \
	bash -c 'grep -qx "build $2/mod/somvr" "$1/release.txt" && grep -q "^source commit .*changed paths under mod/somvr" "$1/release.txt"' \
	_ "$R" "$(cd "$W" && pwd -P)"
cp -a "$R" "$T/release.good"
mv "$W/mod/somvr/build/somvr.so" "$T/somvr.so.away"
env -i "${BASE[@]}" "$WRAP" --make-release >"$T/mr2.out" 2>&1
check "make-release: refuses without a build (exit != 0), release/ unchanged" \
	bash -c '[ $1 != 0 ] && diff -r "$2" "$3" >/dev/null && [ ! -e "$4" ]' _ $? "$R" "$T/release.good" "$W/release.new"
mv "$T/somvr.so.away" "$W/mod/somvr/build/somvr.so"
env -i "${BASE[@]}" "$WRAP" --make-release a b >/dev/null 2>&1
check "make-release: two folders -> usage, exit 2, release/ unchanged" \
	bash -c '[ $1 = 2 ] && diff -r "$2" "$3" >/dev/null' _ $? "$R" "$T/release.good"

# ---- VR mode --------------------------------------------------------------------------------------------------------
ln -s /nonexistent/openxr_wivrn.json "$T/cfg/openxr/1/active_runtime.json"   # WiVRn's link (dangling is fine)
OVL="$T/overlay32.so:$T/overlay64.so"
run LD_PRELOAD="$OVL" LD_LIBRARY_PATH="$T/rtlib" SOMVR_SAVEDIR="$T/saves" SOMVR_TRIGGER="$T/trig" SOMVR_XR_DUMP=60 \
	SOMVR_XR_LOADER=/x SOMVR_PLAY=vr
S=$(lastplay)
check "VR: exit status of the command (7)" [ $RC = 7 ]
check "VR: arguments unchanged, --no-feral-options appended" args_vr
check "VR: one play folder logs/play-<ts>" bash -c '[ "$(ls -1d "$1"/logs/play-* | wc -l)" = 1 ] && [[ ${2##*/} =~ ^play-[0-9]{8}-[0-9]{6}$ ]]' _ "$W" "$S"
check "VR: LD_PRELOAD = Steam's entries + release/somvr.so" is LD_PRELOAD "$OVL:$R/somvr.so"
check "VR: VK_LAYER_PATH = release/" is VK_LAYER_PATH "$R"
check "VR: VK_INSTANCE_LAYERS" is VK_INSTANCE_LAYERS VK_LAYER_SOMVR_openxr
check "VR: SOMVR_XR=1" is SOMVR_XR 1
check "VR: SOMVR_LOG in the play folder" is SOMVR_LOG "$S/somvr.log"
check "VR: SOMVR_XR_LOG in the play folder" is SOMVR_XR_LOG "$S/xr-layer.log"
check "VR: every VR build 3 switch as in somvr-vr3.cmd (${VR3[*]})" are "${VR3[@]}"
check "VR: no other SOMVR_* (no SOMVR_FOLLOW, SOMVR_OVERSCAN...: the mod's defaults, as in the session)" \
	bash -c '[ "$(grep -oE "SOMVR_[A-Z0-9_]+=" "$1" | sort | tr -d "\n")" = "$2" ]' _ "$T/out/env" \
	"$( (printf '%s\n' "${VR3[@]}" | sed 's/=.*/=/'; printf 'SOMVR_LOG=\nSOMVR_XR=\nSOMVR_XR_LOG=\nSOMVR_SETTINGS=\n') | sort |
		tr -d '\n')"
check "VR: SOMVR_SETTINGS = \$HOME/.local/share/somvr/settings.conf (the VR menu's file)" \
	is SOMVR_SETTINGS "$T/home/.local/share/somvr/settings.conf"
check "VR: the wrapper makes no settings folder (somvr.so does, at its first save)" test ! -e "$T/home/.local"
check "VR: no save guard (SOMVR_SAVEDIR unset although inherited)" is SOMVR_SAVEDIR "<unset>"
check "VR: SOMVR_TRIGGER, SOMVR_XR_DUMP, SOMVR_XR_LOADER, SOMVR_PLAY unset" \
	are SOMVR_TRIGGER="<unset>" SOMVR_XR_DUMP="<unset>" SOMVR_XR_LOADER="<unset>" SOMVR_PLAY="<unset>"
check "VR: XR_RUNTIME_JSON not set (the user's active runtime is used)" is XR_RUNTIME_JSON "<unset>"
check "VR: LD_LIBRARY_PATH, HOME, PATH, XDG_CONFIG_HOME unchanged" \
	are LD_LIBRARY_PATH="$T/rtlib" HOME="$T/home" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/cfg"
check "VR: the command's stdout+stderr in play/game.log" bash -c 'grep -qx "stub stdout" "$1" && grep -qx "stub stderr" "$1"' _ "$S/game.log"
check "VR: launch.txt has the env, the sources and every argument (--no-feral-options last)" \
	bash -c 'grep -qx "SOMVR_STEREO=B" "$1" && grep -qx "SOMVR_HIDE_HEAD=near" "$1" && grep -qx "LD_PRELOAD=$2" "$1" &&
		grep -qx "features: VR build 3.s values; from som-vr.conf: none; inherited (wins): none" "$1" &&
		grep -qx "SOMVR_SETTINGS=$3/home/.local/share/somvr/settings.conf" "$1" &&
		grep -qx "arg2=\[with space\]" "$1" && grep -qx "arg3=\[\]" "$1" && grep -qx "arg10=\[%command%\]" "$1" &&
		grep -qx "arg11=\[--no-feral-options\]" "$1" && ! grep -q "^arg12=" "$1"' _ "$S/launch.txt" "$OVL:$R/somvr.so" "$T"
check "VR: som-vr.log line" bash -c '[[ $1 == *"VR: logs in $2" ]]' _ "$(lastlog)" "$S"
sleep 1.1   # a new second: a new play folder name

run   # no LD_PRELOAD at all
check "VR: no LD_PRELOAD before -> only release/somvr.so" is LD_PRELOAD "$R/somvr.so"
run LD_PRELOAD="$T/overlay64.so $R/somvr.so" VK_LAYER_PATH=/other/layers VK_INSTANCE_LAYERS=VK_LAYER_other
check "VR: somvr.so already preloaded -> not added twice" is LD_PRELOAD "$T/overlay64.so $R/somvr.so"
check "VR: existing VK_LAYER_PATH kept after release/" is VK_LAYER_PATH "$R:/other/layers"
check "VR: existing VK_INSTANCE_LAYERS kept" is VK_INSTANCE_LAYERS "VK_LAYER_SOMVR_openxr:VK_LAYER_other"
run SOMVR_SETTINGS="$T/my settings.conf"
check "VR: an inherited SOMVR_SETTINGS wins (kept as given)" is SOMVR_SETTINGS "$T/my settings.conf"
run HOME=
check "VR: no HOME and no SOMVR_SETTINGS -> SOMVR_SETTINGS empty (the mod reads and saves nothing)" is SOMVR_SETTINGS ""
run SOMVR_ARMS=0 SOMVR_BASE=camera SOMVR_FOLLOW=1
check "VR: inherited SOMVR_ARMS / SOMVR_BASE win over VR build 3's, an extra SOMVR_FOLLOW passes" \
	are SOMVR_ARMS=0 SOMVR_BASE=camera SOMVR_FOLLOW=1 SOMVR_COMBAT=1
check "VR: launch.txt names the inherited ones" \
	grep -qx "features: VR build 3.s values; from som-vr.conf: none; inherited (wins): SOMVR_BASE=camera SOMVR_ARMS=0" "$(lastplay)/launch.txt"
rm "$T/cfg/openxr/1/active_runtime.json"
run XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_wivrn.json
check "VR: XR_RUNTIME_JSON counts as a headset (kept as given)" vr_ran
check "VR: XR_RUNTIME_JSON passed on as given" is XR_RUNTIME_JSON /usr/share/openxr/1/openxr_wivrn.json
touch "$T/xdgdirs/openxr/1/active_runtime.json"
BASE_SAVE=("${BASE[@]}")
BASE=(HOME="$T/home" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/cfg" XDG_CONFIG_DIRS="$T/nocfg:$T/xdgdirs" SOMVR_SKIPOS=0)
run
check "VR: an active runtime in XDG_CONFIG_DIRS counts" vr_ran
BASE=("${BASE_SAVE[@]}")
ln -s /nonexistent/openxr_wivrn.json "$T/cfg/openxr/1/active_runtime.json"

# ---- --no-feral-options ---------------------------------------------------------------------------------------------
ARGS_SAVE=("${ARGS[@]}")
for nfo in --no-feral-options -no-feral-options no-feral-options ---no-feral-options; do
	ARGS=("${ARGS_SAVE[@]:0:3}" "$nfo" "${ARGS_SAVE[@]:3}")
	printf '[%s]\n' "${ARGS[@]}" >"$T/args.expected"
	run
	check "feral: '$nfo' already in the command -> not added again (VR, args as given)" bash -c '[ $1 = 7 ] && "$2" -s "$3" "$4"' _ $RC cmp "$T/out/args" "$T/args.expected"
done
ARGS=("${ARGS_SAVE[@]:0:3}" "--no-feral-optionsX" "no-feral" "${ARGS_SAVE[@]:3}")
printf '[%s]\n' "${ARGS[@]}" --no-feral-options >"$T/args.vr2"
run
check "feral: look-alikes (--no-feral-optionsX, no-feral) don't count -> appended" cmp -s "$T/out/args" "$T/args.vr2"
ARGS=("${ARGS_SAVE[@]}")
printf '[%s]\n' "${ARGS[@]}" >"$T/args.expected"

# ---- som-vr.conf ----------------------------------------------------------------------------------------------------
N0=$(loglines)
conf '# my settings for Shadow of Mordor VR' '' 'SOMVR_ARMS=0' '  SOMVR_COMBAT = 0   # no sword hits' \
	'SOMVR_BASE="camera"' "SOMVR_SNAP_DEG='45'" 'SOMVR_HIDE_HEAD=off' 'SOMVR_EYE_SIZE=xr,1832x2015' \
	$'SOMVR_TURN=smooth\r' $'\tSOMVR_OVERSCAN=0.15' 'SOMVR_EYE_CM=-4.5' 'SOMVR_HAPTICS=50' 'SOMVR_HAPTICS=60' \
	'#SOMVR_INPUT=0'
printf 'SOMVR_XR_POINTER=dot' >>"$C"   # no final newline
run
check "conf good: VR still runs" vr_ran
check "conf good: every line applied (quotes, blanks, comments, CRLF, no final newline, the last one wins)" \
	are SOMVR_ARMS=0 SOMVR_COMBAT=0 SOMVR_BASE=camera SOMVR_SNAP_DEG=45 SOMVR_EYE_SIZE=xr,1832x2015 SOMVR_TURN=smooth \
	SOMVR_OVERSCAN=0.15 SOMVR_EYE_CM=-4.5 SOMVR_HAPTICS=60 SOMVR_XR_POINTER=dot
check "conf good: SOMVR_HIDE_HEAD=off -> unset" is SOMVR_HIDE_HEAD "<unset>"
check "conf good: a commented-out line does nothing; the rest stays VR build 3's" \
	are SOMVR_INPUT=1 SOMVR_POINTER=1 SOMVR_HUD=quad SOMVR_AUDIO=head SOMVR_STEREO=B SOMVR_BLADE=controller
check "conf good: no notes in som-vr.log, the VR line lists the settings" \
	bash -c '[ "$(printf "%s\n" "$1" | wc -l)" = 1 ] && [[ $1 == *"VR: logs in "*"; som-vr.conf: SOMVR_ARMS=0 SOMVR_COMBAT=0 "*" SOMVR_XR_POINTER=dot" ]]' \
	_ "$(new_notes "$N0")"
check "conf good: launch.txt has each line's number and value" \
	bash -c 'grep -qx "som-vr.conf: line 3: SOMVR_ARMS=0" "$1" && grep -qx "som-vr.conf: line 15: SOMVR_XR_POINTER=dot" "$1" &&
		grep -qx "SOMVR_HIDE_HEAD=(unset)" "$1" && grep -qx "SOMVR_ARMS=0" "$1"' _ "$(lastplay)/launch.txt"
conf 'SOMVR_HIDE_HEAD=parts' # docs/re-xr-pipeline.md 26
run
check "conf good: SOMVR_HIDE_HEAD=parts" is SOMVR_HIDE_HEAD parts
conf 'SOMVR_HIDE_HEAD=parts+near'
run
check "conf good: SOMVR_HIDE_HEAD=parts+near" is SOMVR_HIDE_HEAD parts+near

N0=$(loglines)
conf 'SOMVR_ARMS=2' 'SOMVR_SNAP_DEG=500' 'SOMVR_SNAP_DEG=abc' 'SOMVR_HAPTICS=1e2' 'SOMVR_TURN_DPS=--5' 'SOMVR_EYE_SIZE=10x10' \
	'SOMVR_HUD=world' "SOMVR_BASE=\$(touch $T/PWNED)" "SOMVR_MOVE=\`touch $T/PWNED\`" 'just some words' '=1' 'SOMVR_COMBAT' \
	'SOMVR_INPUT=1 # fine'
run
check "conf bad values: VR still runs" vr_ran
check "conf bad values: each ignored (VR build 3's values stay, nothing set)" \
	are SOMVR_ARMS=1 SOMVR_SNAP_DEG="<unset>" SOMVR_HAPTICS="<unset>" SOMVR_TURN_DPS="<unset>" SOMVR_EYE_SIZE=xr \
	SOMVR_HUD=quad SOMVR_BASE=head SOMVR_MOVE="<unset>" SOMVR_COMBAT=1 SOMVR_INPUT=1
check "conf bad values: no command substitution ran" test ! -e "$T/PWNED"
check "conf bad values: one som-vr.log note per bad line (12), with its line number and why" \
	bash -c 'n=$(printf "%s\n" "$1" | grep -c "som-vr.conf: line [0-9]* ignored") && [ "$n" = 12 ] &&
		grep -q "line 1 ignored: SOMVR_ARMS=2 (SOMVR_ARMS takes one of: 0 1)" <<<"$1" &&
		grep -q "line 2 ignored: SOMVR_SNAP_DEG=500 (SOMVR_SNAP_DEG takes a number from 1 to 180)" <<<"$1" &&
		grep -q "line 6 ignored: SOMVR_EYE_SIZE=10x10 (SOMVR_EYE_SIZE takes xr, off" <<<"$1" &&
		grep -q "line 10 ignored: no .=. in: just some words" <<<"$1" && grep -q "line 11 ignored: .. is not a SOMVR_ setting" <<<"$1" &&
		grep -q "line 12 ignored: no .=. in: SOMVR_COMBAT" <<<"$1" && ! grep -q "line 13" <<<"$1"' _ "$(new_notes "$N0")"
check "conf bad values: the notes are in launch.txt too" bash -c '[ "$(grep -c "^som-vr.conf: line [0-9]* ignored" "$1")" = 12 ]' _ "$(lastplay)/launch.txt"

N0=$(loglines)
conf 'SOMVR_FOO=1' "SOMVR_SAVEDIR=$T/saves" "SOMVR_TRIGGER=$T/trig" 'SOMVR_XR_DUMP=60' 'SOMVR_STEREO=A' 'SOMVR_VIEWS=fake' \
	'SOMVR_BLADE=fake' 'SOMVR_LOG=/tmp/x.log' 'SOMVR_PLAY=flat' "LD_PRELOAD=$T/evil.so" 'VK_INSTANCE_LAYERS=VK_LAYER_evil' \
	'XR_RUNTIME_JSON=/x.json' 'somvr_arms=0' "\$(touch $T/PWNED2)=1" 'SOMVR_ARMS;true=0' 'export SOMVR_ARMS=0'
run
check "conf unknown keys: VR still runs" vr_ran
check "conf unknown keys: no save guard, trigger, dumps or other log; STEREO/VIEWS/BLADE stay" \
	are SOMVR_FOO="<unset>" SOMVR_SAVEDIR="<unset>" SOMVR_TRIGGER="<unset>" SOMVR_XR_DUMP="<unset>" SOMVR_STEREO=B \
	SOMVR_VIEWS=xr SOMVR_BLADE=controller SOMVR_LOG="$(lastplay)/somvr.log" SOMVR_ARMS=1
check "conf unknown keys: LD_PRELOAD, the layers and the runtime untouched" \
	are LD_PRELOAD="$R/somvr.so" VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr XR_RUNTIME_JSON="<unset>" somvr_arms="<unset>"
check "conf unknown keys: no command substitution ran" test ! -e "$T/PWNED2"
check "conf unknown keys: 16 notes (9 SOMVR_ keys it doesn't take, 7 that aren't SOMVR_ settings)" \
	bash -c '[ "$(grep -c "is not a setting som-vr.conf takes" <<<"$1")" = 9 ] && [ "$(grep -c "is not a SOMVR_ setting" <<<"$1")" = 7 ]' \
	_ "$(new_notes "$N0")"

N0=$(loglines)
conf 'SOMVR_COMBAT=0' 'SOMVR_ARMS=0'
run SOMVR_COMBAT=1
check "conf vs inherited: the inherited SOMVR_COMBAT wins, the conf's SOMVR_ARMS applies" are SOMVR_COMBAT=1 SOMVR_ARMS=0
check "conf vs inherited: noted" bash -c 'grep -q "line 1 not used: SOMVR_COMBAT=1 was set before the launcher" <<<"$1"' _ "$(new_notes "$N0")"

# one good value for every key som-vr.conf takes (the PLAY.md table)
CK=(SOMVR_BASE=player SOMVR_HIDE_HEAD=near SOMVR_ARMS=0 SOMVR_COMBAT=0 SOMVR_INPUT=0 SOMVR_POINTER=hud SOMVR_HUD=off
	SOMVR_AUDIO=off SOMVR_EYE_SIZE=2015x2217 SOMVR_OVERSCAN=0 SOMVR_TURN=snap SOMVR_SNAP_DEG=30 SOMVR_TURN_DPS=90
	SOMVR_MOVE=hand SOMVR_HAPTICS=0 SOMVR_RUMBLE=0 SOMVR_HITSTOP_MS=0 SOMVR_FOLLOW=0 SOMVR_EYE_CM=4 SOMVR_XR_HUD=head
	SOMVR_XR_POINTER=off SOMVR_FALLBACK=off SOMVR_VIGNETTE=0.4 SOMVR_SNAP_BLINK_MS=140 SOMVR_SEATED=1 SOMVR_HEIGHT_CM=120
	SOMVR_COMBAT_REACT=0 SOMVR_VRMENU=0 SOMVR_XR_ENV=world SOMVR_ANCHOR=head SOMVR_LOCO=0 SOMVR_BOW=0 SOMVR_CLIMB=off
	SOMVR_THREAT=0)
N0=$(loglines)
conf "${CK[@]}"
run
check "conf: all ${#CK[@]} keys it takes are applied" are "${CK[@]}"
check "conf: ... with no notes" bash -c '[ "$(printf "%s\n" "$1" | wc -l)" = 1 ]' _ "$(new_notes "$N0")"
check "conf: the wrapper's key list = these keys (launch.txt shows each)" \
	bash -c 'for kv in "${@:2}"; do grep -qx "$kv" "$1" || { echo "     $kv not in launch.txt" >&2; exit 1; }; done' _ "$(lastplay)/launch.txt" "${CK[@]}"
check "conf: the template som-vr.conf names every key, sets none" \
	bash -c 'f=$1/../../som-vr.conf; [ -f "$f" ] && ! grep -qE "^[[:space:]]*[A-Z]" "$f" &&
		for kv in "${@:2}"; do grep -qE "^# *${kv%%=*}=" "$f" || { echo "     ${kv%%=*} not in $f" >&2; exit 1; }; done' _ "$HERE" "${CK[@]}"
cp "$HERE/../../som-vr.conf" "$C"
run
check "conf: the template as it is -> exactly VR build 3's values" are "${VR3[@]}"

N0=$(loglines)
conf 'SOMVR_ARMS=0'
chmod 000 "$C"
if [ -r "$C" ]; then
	echo "info conf unreadable: running as root, skipped"
else
	run
	check "conf unreadable: ignored, noted, VR build 3's values" \
		bash -c '[ "$1" = 1 ] && grep -q "som-vr.conf: not a readable file" <<<"$2"' _ "$(getv SOMVR_ARMS)" "$(new_notes "$N0")"
fi
chmod 644 "$C"
noconf
mkdir "$C"
run
check "conf a folder: ignored, VR runs" bash -c '[ "$1" = 1 ] && [ "$2" = yes ]' _ "$(getv SOMVR_ARMS)" "$(vr_ran && echo yes)"
rmdir "$C"

# ---- flat: exactly as given -------------------------------------------------------------------------------------------
conf 'SOMVR_ARMS=0' 'SOMVR_FOO=1'
N0=$(loglines)
check "flat: SOMVR_PLAY=flat -> env and args unchanged (no --no-feral-options), no play folder" flat_same SOMVR_PLAY=flat LD_PRELOAD="$OVL" SOMVR_SAVEDIR=/keep
check "flat: SOMVR_PLAY=flat logged, som-vr.conf not read" bash -c '[[ $1 == *"flat: SOMVR_PLAY=flat" ]] && ! grep -q som-vr.conf <<<"$2"' _ "$(lastlog)" "$(new_notes "$N0")"
touch "$W/disable_vr.txt"
check "flat: disable_vr.txt -> unchanged" flat_same LD_PRELOAD="$OVL" VK_INSTANCE_LAYERS=VK_LAYER_other
check "flat: disable_vr.txt logged" bash -c '[[ $1 == *"flat: disable_vr.txt" ]]' _ "$(lastlog)"
rm "$W/disable_vr.txt"
rm "$T/cfg/openxr/1/active_runtime.json"
check "flat: no headset (no active runtime) -> unchanged" flat_same LD_PRELOAD="$OVL"
check "flat: no headset logged" bash -c '[[ $1 == *"flat: no headset connected"* ]]' _ "$(lastlog)"
ln -s /nonexistent/openxr_wivrn.json "$T/cfg/openxr/1/active_runtime.json"
noconf

# ---- the release check ---------------------------------------------------------------------------------------------
restore() { rm -rf "$R"; cp -a "$T/release.good" "$R"; }
rm "$R/somvr.so"
check "release: somvr.so missing -> unchanged" flat_same LD_PRELOAD="$OVL"
check "release: missing logged" bash -c '[[ $1 == *"release check failed: missing $2/somvr.so" ]]' _ "$(lastlog)" "$R"
restore
printf 'x' >>"$R/libVkLayer_somvr_openxr.so"
check "release: layer library changed -> unchanged" flat_same
check "release: mismatch logged" bash -c '[[ $1 == *"libVkLayer_somvr_openxr.so does not match SHA256SUMS"* ]]' _ "$(lastlog)"
restore
sed -i 's/"1"/"2"/' "$R/VkLayer_somvr_openxr.json"
check "release: layer manifest edited -> unchanged" flat_same
restore
rm "$R/SHA256SUMS"
check "release: no SHA256SUMS -> unchanged" flat_same
restore
sed -i '/somvr.so$/d' "$R/SHA256SUMS"
check "release: somvr.so not listed -> unchanged" flat_same
check "release: unlisted logged" bash -c '[[ $1 == *"somvr.so is not in SHA256SUMS" ]]' _ "$(lastlog)"
rm -rf "$R"
conf 'SOMVR_ARMS=0'
check "release: no release/ at all (a conf present) -> unchanged" flat_same LD_PRELOAD="$OVL"
check "release: no release/ logged" bash -c '[[ $1 == *"release check failed: no $2/SHA256SUMS" ]]' _ "$(lastlog)" "$R"
noconf
restore
run LD_LIBRARY_PATH="$T/rtlib"
check "release: back to good -> VR again (with a hostile LD_LIBRARY_PATH)" bash -c '[ "$1" = "$2" ] && [ "$3" = yes ]' _ \
	"$(getv LD_PRELOAD)" "$R/somvr.so" "$(vr_ran && echo yes)"
if ! env LD_LIBRARY_PATH="$T/rtlib" sha256sum </dev/null >/dev/null 2>&1; then
	ok "control: that LD_LIBRARY_PATH really breaks a host sha256sum (so the wrapper's tools ran without it)"
else
	echo "info control: the fake libz does not break sha256sum here (the hostile-runtime check proves less)"
fi
conf 'SOMVR_SNAP_DEG=45' 'SOMVR_ARMS=7'
run LD_LIBRARY_PATH="$T/rtlib"
check "conf with a hostile LD_LIBRARY_PATH: the number check (awk) still works" are SOMVR_SNAP_DEG=45 SOMVR_ARMS=1
noconf
cp -a "$W" "$T/with:colon"
rm -rf "$T/with:colon/logs"
WRAP="$T/with:colon/scripts/som-vr.sh"
check "release: ':' in the path -> unchanged" flat_same
WRAP=$W/scripts/som-vr.sh

# ---- --make-release from a frozen build folder (as logs/build-vr1) ----------------------------------------------------
B=$W/logs/build-x/ShadowOfMordor/mod/somvr
mkdir -p "$B/build" "$B/vklayer/build"
printf 'frozen somvr %s\n' "$RANDOM" >"$B/build/somvr.so"
printf 'frozen layer %s\n' "$RANDOM" >"$B/vklayer/build/libVkLayer_somvr_openxr.so"
cp "$MANIFEST" "$B/vklayer/VkLayer_somvr_openxr.json"
echo 8cd52b8bbfd5ac4b2ae11bc76db38793190b1e57 >"$W/logs/build-x/HEAD.txt"
cp -a "$R" "$T/release.before"
env -i "${BASE[@]}" "$WRAP" --make-release "$B" >"$T/mr3.out" 2>&1
check "make-release DIR: exit 0, the build folder's files, SHA256SUMS verifies" \
	bash -c '[ $1 = 0 ] && cmp -s "$2/build/somvr.so" "$3/somvr.so" && cmp -s "$2/vklayer/build/libVkLayer_somvr_openxr.so" "$3/libVkLayer_somvr_openxr.so" &&
		cd "$3" && sha256sum --quiet --strict -c SHA256SUMS' _ $? "$B" "$R"
check "make-release DIR: release.txt = the build folder and HEAD.txt's commit" \
	bash -c 'grep -qx "build $2" "$1" && grep -qx "source commit 8cd52b8bbfd5ac4b2ae11bc76db38793190b1e57 (from $3/HEAD.txt)" "$1"' \
	_ "$R/release.txt" "$(cd "$B" && pwd -P)" "$(cd "$W/logs/build-x" && pwd -P)"
check "make-release DIR: the previous release in release.prev/" diff -r "$T/release.before" "$W/release.prev"
run
check "make-release DIR: VR starts with it" vr_ran
rm "$W/logs/build-x/HEAD.txt"
(cd / && env -i "${BASE[@]}" "$WRAP" --make-release logs/build-x/ShadowOfMordor/mod/somvr >"$T/mr4.out" 2>&1)
check "make-release DIR relative to the project (run from /): ok, no HEAD.txt -> commit unknown" \
	bash -c '[ $1 = 0 ] && grep -q "^source commit unknown (no HEAD.txt" "$2/release.txt" && cmp -s "$3/build/somvr.so" "$2/somvr.so"' _ $? "$R" "$B"
rm -rf "$T/release.before" && cp -a "$R" "$T/release.before"
env -i "${BASE[@]}" "$WRAP" --make-release "$T/nonexistent" >"$T/mr5.out" 2>&1
check "make-release DIR missing: exit != 0, release/ unchanged" \
	bash -c '[ $1 != 0 ] && diff -r "$2" "$3" >/dev/null && grep -q "no build folder" "$4"' _ $? "$R" "$T/release.before" "$T/mr5.out"

# ---- SkipOSWarnings (Feral's -setreg_ word; the user's OK 2026-10-20) ---------------------------------------------------
SK='-setreg_name="SkipOSWarnings"value="1"type="integer"'
env -i HOME="$T/home" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/cfg" XDG_CONFIG_DIRS="$T/nocfg" SOMVR_PLAY=flat \
	"$WRAP" "$T/stub" a "b c" >"$T/out/stdout" 2>"$T/out/stderr"
check "skipos: flat mode appends only the SkipOSWarnings word" bash -c '[ "$(cat "$1")" = "$(printf "[a]\n[b c]\n[%s]" "$2")" ]' _ "$T/out/args" "$SK"
env -i HOME="$T/home" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/cfg" XDG_CONFIG_DIRS="$T/nocfg" SOMVR_PLAY=flat \
	"$WRAP" "$T/stub" a "$SK" >"$T/out/stdout" 2>"$T/out/stderr"
check "skipos: not added twice" bash -c '[ "$(grep -c SkipOSWarnings "$1")" = 1 ]' _ "$T/out/args"
env -i HOME="$T/home" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$T/cfg" XDG_CONFIG_DIRS="$T/nocfg" SOMVR_PLAY=flat SOMVR_SKIPOS=0 \
	"$WRAP" "$T/stub" a >"$T/out/stdout" 2>"$T/out/stderr"
check "skipos: SOMVR_SKIPOS=0 leaves it out" bash -c '! grep -q SkipOSWarnings "$1"' _ "$T/out/args"

# ---- misc -------------------------------------------------------------------------------------------------------------
env -i "${BASE[@]}" "$WRAP" >/dev/null 2>&1
check "no command -> usage, exit 2" [ $? = 2 ]
check "the copy under test is the script in the repo" cmp -s "$SRC" "$WRAP"
check "every SOMVR_* the wrapper sets or som-vr.conf takes is a switch somvr/the layer reads" bash -c '
	for kv in SOMVR_XR= SOMVR_LOG= SOMVR_XR_LOG= "${@:2}"; do v=${kv%%=*}
		grep -qE "\"$v\"" "$1/mod/somvr/somvr.c" "$1/mod/somvr/vklayer/somvr_vklayer.c" || { echo "     $v unknown" >&2; exit 1; }
	done' _ "$HERE/../.." "${VR3[@]}" "${CK[@]}"

echo "test_som_vr: $PASS passed, $FAIL failed"
[ $FAIL = 0 ]
