#!/bin/sh
# som-vr.sh - Shadow of Mordor VR launcher (DRAFT, not given to the user yet; PLAY.md). Steam launch option, set once:
#     /home/bara/Projects/flat2vr/ShadowOfMordor/scripts/som-vr.sh %command%
# Steam's command line is passed on in every mode (exec, the same arguments; VR mode appends one, below). Environment,
# game only:
#   VR    if a headset is connected (an OpenXR runtime is active: XR_RUNTIME_JSON, or an active_runtime.json in
#         $XDG_CONFIG_HOME, $XDG_CONFIG_DIRS or /etc - WiVRn writes ~/.config/openxr/1/active_runtime.json only while
#         the Quest is connected) and release/ matches its SHA256SUMS:
#           LD_PRELOAD += release/somvr.so (appended, the existing entries kept: Steam's overlay)
#           VK_LAYER_PATH = release/ (+ any existing), VK_INSTANCE_LAYERS = VK_LAYER_SOMVR_openxr (+ any existing)
#           SOMVR_XR=1, SOMVR_LOG / SOMVR_XR_LOG = logs/play-<ts>/somvr.log / xr-layer.log
#           SOMVR_SETTINGS = $HOME/.local/share/somvr/settings.conf (an inherited value wins): the in-VR menu's settings
#           file (docs/re-xr-pipeline.md 15), read and written by somvr.so itself, only with SOMVR_VRMENU=1 (which makes
#           the folder at its first save); this script never reads, writes or creates it
#           the features of VR build 3 (the game env of scripts/cmds/somvr-vr3.cmd, the headset session): SOMVR_STEREO=B
#           SOMVR_VIEWS=xr SOMVR_COMFORT=1 SOMVR_EYE_SIZE=xr SOMVR_INPUT=1 SOMVR_POINTER=1 SOMVR_HUD=quad
#           SOMVR_AUDIO=head SOMVR_BASE=head SOMVR_HIDE_HEAD=near SOMVR_FALLBACK=auto SOMVR_VIGNETTE=0 SOMVR_ARMS=1
#           SOMVR_COMBAT=1 SOMVR_BLADE=controller (VR build 1b's, the vignette off: comfort aids off by default) and
#           SOMVR_VRMENU=1 SOMVR_XR_ENV=on SOMVR_ANCHOR=root SOMVR_LOCO=1 SOMVR_COMBAT_REACT=1 SOMVR_BOW=1 SOMVR_CLIMB=b
#           SOMVR_THREAT=1 (docs/re-xr-pipeline.md 19)
#           (the overscan 0.1 is the mod's default with these). Over them ShadowOfMordor/som-vr.conf (below); over both
#           a value inherited from the environment, e.g. the launch option `SOMVR_ARMS=0 /abs/som-vr.sh %command%`
#           unset: SOMVR_SAVEDIR SOMVR_TRIGGER SOMVR_XR_DUMP SOMVR_XR_LOADER SOMVR_PLAY: no save guard (the user's own
#           play uses the real save), stereo whenever the world renders, no test dumps. XR_RUNTIME_JSON is left as it
#           is: the active runtime (WiVRn's link) is used like by any OpenXR app (boot_check --headset sets it only
#           as its WiVRn guard)
#           argument --no-feral-options appended (docs/re-launcher.md 1: Feral's pre-game window is skipped; no prefs
#           are changed), unless the command already has it (Feral strips any number of leading dashes)
#         The game's stdout/stderr go to logs/play-<ts>/game.log; launch.txt = the release, the environment, where the
#         feature values came from, som-vr.conf's notes and the arguments.
#   flat  SOMVR_PLAY=flat, a file ShadowOfMordor/disable_vr.txt, no headset, or release/ missing or not matching its
#         SHA256SUMS: exec the command exactly as given (environment and arguments unchanged, som-vr.conf not read).
#         logs/som-vr.log gets one line per start.
# som-vr.conf (optional, the user's; read in VR mode only, never run): one KEY=VALUE per line; '#' starts a comment;
#   blanks round the key and the value and one pair of quotes round the value are dropped. Only the keys and values of
#   somvr_conf_ok below (the user's switches; not the layer env, the logs, the save guard or the trigger). A line with
#   another key, a value its key doesn't take or no '=' is ignored with a note in logs/som-vr.log and launch.txt (VR
#   still starts); the last good line of a key wins. SOMVR_HIDE_HEAD=off / SOMVR_EYE_SIZE=off leave that switch unset.
# Not done here (needs the user's OK, PLAY.md): the SkipOSWarnings pref, so Feral's unsupported-distro box still comes
#   up once per start on the desktop.
# Saves: no save guard: the game uses the player's real save and Steam Cloud, as flat play does.
# Never touches the game install, Steam's config, Feral's prefs, ~/.config/openxr or anything system-wide; no network.
# Its own tools run without LD_LIBRARY_PATH/LD_PRELOAD (Steam's runtime libraries can break host tools, as Feral's
# script notes).
#
#   som-vr.sh --make-release [MOD_DIR]   (developer, offline) copy a build into release/: MOD_DIR (default
#       ShadowOfMordor/mod/somvr; e.g. logs/build-vr1/ShadowOfMordor/mod/somvr; relative = to the current folder, else
#       to ShadowOfMordor/) build/somvr.so, vklayer/build/libVkLayer_somvr_openxr.so and the layer manifest
#       (library_path rewritten to ./); SHA256SUMS (the manifest every start checks) and release.txt (time, the build
#       folder, the source commit: git for mod/somvr itself, else the first HEAD.txt in MOD_DIR or the 3 folders above
#       it, as a frozen build has); the previous release -> release.prev/
# Test: scripts/tests/test_som_vr.sh (a stub command; launches nothing).
set -u

somvr_clean() ( unset LD_LIBRARY_PATH LD_PRELOAD; "$@" )
somvr_note() {
	somvr_clean mkdir -p "$somvr_root/logs" 2>/dev/null
	printf '%s %s\n' "$(somvr_clean date '+%Y-%m-%d %H:%M:%S')" "$*" 2>/dev/null >>"$somvr_root/logs/som-vr.log"
	printf 'som-vr: %s\n' "$*" >&2
}
# Feral's -setreg_ switch (docs/re-launcher.md 3): SkipOSWarnings=1 in the player's prefs silences the "unsupported
# Linux distribution" box in both modes (the user's OK, 2026-10-20). SOMVR_SKIPOS=0 leaves it out. Never added twice.
somvr_skipos='-setreg_name="SkipOSWarnings"value="1"type="integer"'
somvr_skip=${SOMVR_SKIPOS:-1}
somvr_has_skipos() { for somvr_b in "$@"; do [ "$somvr_b" != "$somvr_skipos" ] || return 0; done; return 1; }
somvr_pass() { # reason, then the command as Steam gave it (plus only the SkipOSWarnings word)
	somvr_note "$1"
	shift
	if [ "$somvr_skip" != 0 ] && ! somvr_has_skipos "$@"; then exec "$@" "$somvr_skipos"; fi
	exec "$@"
}

if [ $# -eq 0 ] || { [ "$1" = --make-release ] && [ $# -gt 2 ]; }; then
	echo "usage: $0 %command%   (Steam launch option)   |   $0 --make-release [MOD_DIR]" >&2
	exit 2
fi
case $0 in */*) somvr_root=${0%/*}/.. ;; *) somvr_root=.. ;; esac
somvr_root=$(CDPATH= cd -- "$somvr_root" 2>/dev/null && pwd -P) || exec "$@"
somvr_rel=$somvr_root/release
somvr_files="somvr.so libVkLayer_somvr_openxr.so VkLayer_somvr_openxr.json"

if [ "$1" = --make-release ]; then
	somvr_m=${2:-$somvr_root/mod/somvr}
	case $somvr_m in /*) ;; *) [ -d "$somvr_m" ] || somvr_m=$somvr_root/$somvr_m ;; esac
	somvr_clean sh -euc '
		S=$1 F=$2 N=$1/release.new
		M=$(CDPATH= cd -- "$3" 2>/dev/null && pwd -P) || { echo "som-vr: no build folder $3" >&2; exit 1; }
		for f in "$M/build/somvr.so" "$M/vklayer/build/libVkLayer_somvr_openxr.so" "$M/vklayer/VkLayer_somvr_openxr.json"; do
			[ -f "$f" ] || { echo "som-vr: missing $f (build first)" >&2; exit 1; }
		done
		grep -q "\"library_path\": *\"\./build/libVkLayer_somvr_openxr\.so\"" "$M/vklayer/VkLayer_somvr_openxr.json" ||
			{ echo "som-vr: unexpected library_path in the layer manifest" >&2; exit 1; }
		new=$(find "$M" -maxdepth 2 \( -name "*.c" -o -name "*.h" \) -newer "$M/build/somvr.so" | head -3)
		[ -z "$new" ] || echo "som-vr: WARNING sources newer than build/somvr.so: $new" >&2
		if [ "$M" = "$(cd "$S/mod/somvr" 2>/dev/null && pwd -P)" ]; then
			c=$(git -C "$S" rev-parse --short HEAD 2>/dev/null || echo unknown)
			d=$(git -C "$S" status --porcelain -- mod/somvr 2>/dev/null | wc -l)
			src="source commit $c, $d changed paths under mod/somvr"
		else
			src="source commit unknown (no HEAD.txt in the build folder or the 3 above it)"
			for h in "$M" "$M/.." "$M/../.." "$M/../../.."; do
				[ -f "$h/HEAD.txt" ] || continue
				src="source commit $(head -c 64 "$h/HEAD.txt" | tr -dc 0-9a-f) (from $(cd "$h" && pwd -P)/HEAD.txt)"
				break
			done
		fi
		rm -rf "$N"; mkdir -p "$N"
		cp "$M/build/somvr.so" "$M/vklayer/build/libVkLayer_somvr_openxr.so" "$N/"
		sed "s#\"\./build/libVkLayer_somvr_openxr\.so\"#\"./libVkLayer_somvr_openxr.so\"#" \
			"$M/vklayer/VkLayer_somvr_openxr.json" >"$N/VkLayer_somvr_openxr.json"
		(cd "$N" && sha256sum $F >SHA256SUMS)
		printf "made %s\nbuild %s\n%s\n" "$(date "+%Y-%m-%d %H:%M:%S")" "$M" "$src" >"$N/release.txt"
		rm -rf "$S/release.prev"
		[ ! -d "$S/release" ] || mv "$S/release" "$S/release.prev"
		mv "$N" "$S/release"
		echo "release: $S/release"; cat "$S/release/SHA256SUMS" "$S/release/release.txt"
	' som-vr "$somvr_root" "$somvr_files" "$somvr_m"
	exit
fi

# ---- flat: the command exactly as Steam gave it --------------------------------------------------------------------
[ "${SOMVR_PLAY:-}" != flat ] || somvr_pass "flat: SOMVR_PLAY=flat" "$@"
[ ! -e "$somvr_root/disable_vr.txt" ] || somvr_pass "flat: disable_vr.txt" "$@"

somvr_headset() {
	[ -z "${XR_RUNTIME_JSON:-}" ] || return 0
	somvr_dirs=${XDG_CONFIG_HOME:-${HOME:-/nonexistent}/.config}:${XDG_CONFIG_DIRS:-/etc/xdg}:/etc
	while [ -n "$somvr_dirs" ]; do
		somvr_d=${somvr_dirs%%:*}
		case $somvr_dirs in *:*) somvr_dirs=${somvr_dirs#*:} ;; *) somvr_dirs= ;; esac
		# -L too: a symlink into /usr counts even where it dangles (HoF2's launcher, inside Steam's container)
		if [ -n "$somvr_d" ] && { [ -e "$somvr_d/openxr/1/active_runtime.json" ] || [ -L "$somvr_d/openxr/1/active_runtime.json" ]; }; then
			return 0
		fi
	done
	return 1
}
somvr_headset || somvr_pass "flat: no headset connected (no active OpenXR runtime)" "$@"

case $somvr_rel in *:* | *' '* | *"$(printf '\t')"*) somvr_pass "flat: ':' or a blank in $somvr_rel" "$@" ;; esac
somvr_check() {
	[ -f "$somvr_rel/SHA256SUMS" ] || { somvr_why="no $somvr_rel/SHA256SUMS"; return 1; }
	for somvr_f in $somvr_files; do
		[ -f "$somvr_rel/$somvr_f" ] || { somvr_why="missing $somvr_rel/$somvr_f"; return 1; }
		somvr_want=
		while read -r somvr_h somvr_n; do
			case $somvr_n in "$somvr_f" | "*$somvr_f") somvr_want=$somvr_h ;; esac
		done <"$somvr_rel/SHA256SUMS"
		[ -n "$somvr_want" ] || { somvr_why="$somvr_f is not in SHA256SUMS"; return 1; }
		somvr_have=$(somvr_clean sha256sum <"$somvr_rel/$somvr_f" 2>/dev/null)
		somvr_have=${somvr_have%% *}
		[ "$somvr_have" = "$somvr_want" ] || {
			somvr_why="$somvr_f does not match SHA256SUMS (sha256 ${somvr_have:-unreadable})"
			return 1
		}
	done
}
somvr_check || somvr_pass "flat: release check failed: $somvr_why" "$@"

somvr_s=$somvr_root/logs/play-$(somvr_clean date +%Y%m%d-%H%M%S)
[ ! -e "$somvr_s" ] || somvr_s=$somvr_s-$$
# (printf, not ':': a failed redirection of a special builtin would end this shell before the exec)
somvr_clean mkdir -p "$somvr_s" 2>/dev/null && printf '' 2>/dev/null >>"$somvr_s/game.log" ||
	somvr_pass "flat: cannot write $somvr_s" "$@"

# ---- VR: the game-only environment ---------------------------------------------------------------------------------
case ":${LD_PRELOAD:-}:" in # glibc splits LD_PRELOAD at ':' and blanks
*[[:space:]:]"$somvr_rel/somvr.so"[[:space:]:]*) ;;
*) LD_PRELOAD=${LD_PRELOAD:+$LD_PRELOAD:}$somvr_rel/somvr.so ;;
esac
case ":${VK_INSTANCE_LAYERS:-}:" in
*:VK_LAYER_SOMVR_openxr:*) ;;
*) VK_INSTANCE_LAYERS=VK_LAYER_SOMVR_openxr${VK_INSTANCE_LAYERS:+:$VK_INSTANCE_LAYERS} ;;
esac
VK_LAYER_PATH=$somvr_rel${VK_LAYER_PATH:+:$VK_LAYER_PATH}
unset SOMVR_SAVEDIR SOMVR_TRIGGER SOMVR_XR_DUMP SOMVR_XR_LOADER SOMVR_PLAY SOMVR_SKIPOS
export LD_PRELOAD VK_INSTANCE_LAYERS VK_LAYER_PATH SOMVR_XR=1 \
	SOMVR_LOG="$somvr_s/somvr.log" SOMVR_XR_LOG="$somvr_s/xr-layer.log" \
	SOMVR_SETTINGS="${SOMVR_SETTINGS:-${HOME:+$HOME/.local/share/somvr/settings.conf}}"

# the features: VR build 3's values < som-vr.conf < a value inherited from the environment
somvr_vr3="SOMVR_STEREO=B SOMVR_VIEWS=xr SOMVR_COMFORT=1 SOMVR_EYE_SIZE=xr SOMVR_INPUT=1 SOMVR_POINTER=1 SOMVR_HUD=quad
	SOMVR_AUDIO=head SOMVR_BASE=head SOMVR_HIDE_HEAD=near SOMVR_FALLBACK=auto SOMVR_VIGNETTE=0 SOMVR_ARMS=1
	SOMVR_COMBAT=1 SOMVR_BLADE=controller SOMVR_VRMENU=1 SOMVR_XR_ENV=on SOMVR_ANCHOR=root SOMVR_LOCO=1
	SOMVR_COMBAT_REACT=1 SOMVR_BOW=1 SOMVR_CLIMB=b SOMVR_THREAT=1"
# the keys som-vr.conf takes (each one's values: somvr_conf_ok); the user's switches, PLAY.md "Turning things off"
somvr_ckeys="SOMVR_BASE SOMVR_HIDE_HEAD SOMVR_ARMS SOMVR_COMBAT SOMVR_INPUT SOMVR_POINTER SOMVR_HUD SOMVR_AUDIO
	SOMVR_EYE_SIZE SOMVR_OVERSCAN SOMVR_TURN SOMVR_SNAP_DEG SOMVR_TURN_DPS SOMVR_MOVE SOMVR_HAPTICS SOMVR_RUMBLE
	SOMVR_HITSTOP_MS SOMVR_FOLLOW SOMVR_EYE_CM SOMVR_XR_HUD SOMVR_XR_POINTER SOMVR_FALLBACK SOMVR_VIGNETTE
	SOMVR_SNAP_BLINK_MS SOMVR_SEATED SOMVR_HEIGHT_CM SOMVR_COMBAT_REACT SOMVR_VRMENU SOMVR_XR_ENV SOMVR_ANCHOR
	SOMVR_LOCO SOMVR_BOW SOMVR_CLIMB SOMVR_THREAT SOMVR_EYE_FWD_CM"
somvr_in() { # VALUE WORD...: VALUE is one of the words
	somvr_x=$1
	shift
	somvr_why="takes one of: $*"
	for somvr_y in "$@"; do [ "$somvr_x" != "$somvr_y" ] || return 0; done
	return 1
}
somvr_num() { # VALUE LO HI: a plain decimal number from LO to HI
	somvr_why="takes a number from $2 to $3"
	case ${1#-} in '' | *[!0-9.]* | *.*.* | .* | *.) return 1 ;; esac
	somvr_clean env LC_ALL=C awk -v v="$1" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v + 0 >= lo + 0 && v + 0 <= hi + 0) }' \
		2>/dev/null
}
somvr_size() { # WxH, W and H from 64 to 16384
	case $1 in [1-9]*x[1-9]*) ;; *) return 1 ;; esac
	case $1 in *[!0-9x]* | *x*x*) return 1 ;; esac
	somvr_num "${1%x*}" 64 16384 && somvr_num "${1#*x}" 64 16384
}
somvr_conf_ok() { # KEY VALUE: 0 = a key som-vr.conf takes, with a value it takes; else somvr_why (unknown = the key)
	case $1 in
	SOMVR_BASE) somvr_in "$2" head camera player ;;
	SOMVR_HIDE_HEAD) somvr_in "$2" bone+near bone near parts parts+near off ;;
	SOMVR_ARMS | SOMVR_COMBAT | SOMVR_INPUT | SOMVR_RUMBLE | SOMVR_FOLLOW) somvr_in "$2" 0 1 ;;
	SOMVR_VRMENU | SOMVR_LOCO | SOMVR_BOW | SOMVR_THREAT) somvr_in "$2" 0 1 ;;
	SOMVR_XR_ENV) somvr_in "$2" on world off ;;
	SOMVR_ANCHOR) somvr_in "$2" root head ;;
	SOMVR_CLIMB) somvr_in "$2" b off ;;
	SOMVR_POINTER) somvr_in "$2" 0 1 hud ;;
	SOMVR_HUD) somvr_in "$2" quad off ;;
	SOMVR_AUDIO) somvr_in "$2" head off ;;
	SOMVR_TURN) somvr_in "$2" snap smooth ;;
	SOMVR_MOVE) somvr_in "$2" head hand ;;
	SOMVR_XR_HUD) somvr_in "$2" world head off ;;
	SOMVR_XR_POINTER) somvr_in "$2" on dot off ;;
	SOMVR_FALLBACK) somvr_in "$2" auto off ;;
	SOMVR_VIGNETTE) somvr_num "$2" 0 1 ;;
	SOMVR_SNAP_BLINK_MS) somvr_num "$2" 0 500 ;;
	SOMVR_SEATED) somvr_in "$2" 0 1 ;;
	SOMVR_HEIGHT_CM) somvr_num "$2" 100 220 ;;
	SOMVR_COMBAT_REACT) somvr_in "$2" 0 1 force ;;
	SOMVR_SNAP_DEG) somvr_num "$2" 1 180 ;;
	SOMVR_TURN_DPS) somvr_num "$2" 10 360 ;;
	SOMVR_HAPTICS) somvr_num "$2" 0 150 ;;
	SOMVR_HITSTOP_MS) somvr_num "$2" 0 500 ;;
	SOMVR_OVERSCAN) somvr_num "$2" 0 0.5 ;;
	SOMVR_EYE_CM) somvr_num "$2" -30 30 ;;
	SOMVR_EYE_FWD_CM) somvr_num "$2" -30 40 ;;
	SOMVR_EYE_SIZE)
		somvr_in "$2" xr off || somvr_size "${2#xr,}" ||
			{ somvr_why="takes xr, off, WxH or xr,WxH (W and H from 64 to 16384)"; return 1; } ;;
	*) somvr_why=unknown; return 1 ;;
	esac
}
somvr_cset=" " somvr_capp= somvr_cinfo= somvr_inh=
somvr_cnote() { # a som-vr.conf note: launch.txt, logs/som-vr.log, stderr
	somvr_cinfo="$somvr_cinfo
som-vr.conf: $*"
	somvr_note "som-vr.conf: $*"
}
somvr_trim() { # somvr_t without leading and trailing blanks (a CR too)
	while :; do case $somvr_t in [[:space:]]*) somvr_t=${somvr_t#?} ;; *) break ;; esac; done
	while :; do case $somvr_t in *[[:space:]]) somvr_t=${somvr_t%?} ;; *) break ;; esac; done
}
somvr_conf_line() { # line $somvr_n = $1; values are never run or expanded
	somvr_t=${1%%#*}
	somvr_trim
	[ -n "$somvr_t" ] || return 0
	case $somvr_t in *=*) ;; *) { somvr_cnote "line $somvr_n ignored: no '=' in: $somvr_t"; return 0; } ;; esac
	somvr_l=$somvr_t
	somvr_t=${somvr_l%%=*}
	somvr_trim
	somvr_k=$somvr_t
	somvr_t=${somvr_l#*=}
	somvr_trim
	somvr_v=$somvr_t
	case $somvr_v in \"*\" | \'*\')
		somvr_v=${somvr_v#?}
		somvr_v=${somvr_v%?}
		;;
	esac
	case $somvr_k in SOMVR_?*) somvr_x= ;; *) somvr_x=bad ;; esac
	case $somvr_k in *[!A-Z0-9_]*) somvr_x=bad ;; esac
	if [ -n "$somvr_x" ]; then
		somvr_cnote "line $somvr_n ignored: '$somvr_k' is not a SOMVR_ setting"
		return 0
	fi
	if ! somvr_conf_ok "$somvr_k" "$somvr_v"; then
		if [ "$somvr_why" = unknown ]; then
			somvr_cnote "line $somvr_n ignored: $somvr_k is not a setting som-vr.conf takes (PLAY.md lists them)"
		else
			somvr_cnote "line $somvr_n ignored: $somvr_k=$somvr_v ($somvr_k $somvr_why)"
		fi
		return 0
	fi
	case $somvr_cset in
	*" $somvr_k "*) ;;
	*)
		eval "somvr_x=\${$somvr_k:-}" # (a key of somvr_conf_ok's: a plain name)
		if [ -n "$somvr_x" ]; then
			somvr_cnote "line $somvr_n not used: $somvr_k=$somvr_x was set before the launcher (that wins)"
			return 0
		fi
		somvr_cset="$somvr_cset$somvr_k "
		;;
	esac
	somvr_cinfo="$somvr_cinfo
som-vr.conf: line $somvr_n: $somvr_k=$somvr_v"
	somvr_capp="$somvr_capp $somvr_k=$somvr_v"
	case $somvr_k=$somvr_v in
	SOMVR_HIDE_HEAD=off | SOMVR_EYE_SIZE=off) unset "$somvr_k" ;;
	*) export "$somvr_k=$somvr_v" ;;
	esac
}
somvr_conf=$somvr_root/som-vr.conf
if [ -e "$somvr_conf" ] || [ -L "$somvr_conf" ]; then
	if [ -f "$somvr_conf" ] && [ -r "$somvr_conf" ]; then
		somvr_n=0
		while IFS= read -r somvr_r || [ -n "$somvr_r" ]; do
			somvr_n=$((somvr_n + 1))
			[ $somvr_n -le 1000 ] || { somvr_cnote "more than 1000 lines: the rest ignored"; break; }
			somvr_conf_line "$somvr_r"
		done <"$somvr_conf"
	else
		somvr_cnote "not a readable file: ignored (VR build 3's settings)"
	fi
fi
for somvr_kv in $somvr_vr3; do
	somvr_k=${somvr_kv%%=*}
	case $somvr_cset in *" $somvr_k "*) continue ;; esac
	eval "somvr_x=\${$somvr_k:-}"
	if [ -n "$somvr_x" ]; then somvr_inh="$somvr_inh $somvr_k=$somvr_x"; else export "$somvr_kv"; fi
done

# Feral's switch: no pre-game window (VR mode only; docs/re-launcher.md 1)
somvr_nfo=1
for somvr_a in "$@"; do
	while :; do case $somvr_a in -*) somvr_a=${somvr_a#-} ;; *) break ;; esac; done
	[ "$somvr_a" != no-feral-options ] || somvr_nfo=0
done
[ $somvr_nfo = 0 ] || set -- "$@" --no-feral-options
{ [ "$somvr_skip" = 0 ] || somvr_has_skipos "$@"; } || set -- "$@" "$somvr_skipos"

{
	echo "som-vr VR start $(somvr_clean date '+%Y-%m-%d %H:%M:%S'), release $somvr_rel:"
	somvr_clean cat "$somvr_rel/SHA256SUMS" "$somvr_rel/release.txt" 2>/dev/null
	for somvr_v in LD_PRELOAD VK_LAYER_PATH VK_INSTANCE_LAYERS XR_RUNTIME_JSON SOMVR_XR SOMVR_LOG SOMVR_XR_LOG \
		SOMVR_SETTINGS SOMVR_STEREO SOMVR_VIEWS SOMVR_COMFORT SOMVR_BLADE $somvr_ckeys; do
		eval "printf '%s=%s\n' $somvr_v \"\${$somvr_v-(unset)}\""
	done
	echo "features: VR build 3's values; from som-vr.conf:${somvr_capp:- none}; inherited (wins):${somvr_inh:- none}"
	[ -z "$somvr_cinfo" ] || printf '%s\n' "${somvr_cinfo#?}"
	somvr_i=-1 # arg0 = the command
	for somvr_a in "$@"; do
		somvr_i=$((somvr_i + 1))
		printf 'arg%d=[%s]\n' "$somvr_i" "$somvr_a"
	done
} >"$somvr_s/launch.txt" 2>/dev/null
somvr_note "VR: logs in $somvr_s${somvr_capp:+; som-vr.conf:$somvr_capp}"
exec "$@" >>"$somvr_s/game.log" 2>&1
