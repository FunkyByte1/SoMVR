# lh-mirror.sh - build 30 (som-b30-leftie, docs/re-xr-pipeline.md 132): a right-hander's test choreography played by a
# left-hander, for boots with SOMVR_HANDED=left. Sourced after a driver's own w / x (pin16-drive.sh with MIRROR_LH=1,
# leftie30-drive.sh): every test hand and test button through mirw (the preload's `hands ...` words) / mirx (the layer's
# xrtest lines): L <-> R, the hand's x and its turns about Y and Z negated (the body mirrored about its middle); the
# menu button stays the left controller's (it has no twin); a touch's point on a quad, the sticks' directions, the
# `stick` / `turn` words and the head lines stay. The game then answers as it answered the right-handed driver (its
# logs name the hands' roles: R = the sword hand) while the other controllers do it.
mirw() { awk 'function n(v) { return v == 0 ? 0 : -v }
  $1 == "hands" && ($2 == "test" || $2 == "swing") && ($3 == "L" || $3 == "R") && NF >= 9 { $3 = $3 == "L" ? "R" : "L"; $4 = n($4); $8 = n($8); $9 = n($9); print; next }
  $1 == "hands" && ($3 == "L" || $3 == "R") { $3 = $3 == "L" ? "R" : "L" } { print }' <<< "$1"; }
mirx() { awk 'function n(v) { return v == 0 ? 0 : -v }
  $1 == "hand" && ($2 == "L" || $2 == "R") { $2 = $2 == "L" ? "R" : "L"
    if ($3 == "head") { $4 = n($4); if (NF >= 9 && $7 != "s") { $8 = n($8); $9 = n($9) } } print; next }
  ($1 == "btn" && $3 != "menu" || $1 == "stick") && ($2 == "L" || $2 == "R") { $2 = $2 == "L" ? "R" : "L" } { print }' <<< "$1"; }
w() { userin && bail "the user is in VR"; local m; m=$(mirw "$1"); printf '%s' "$m" > "$R/somvr.trigger"
      echo "$(date +%T) word: $m$([ "$m" != "$1" ] && echo " (mirrored from: $1)")" >&2; sleep "${2:-0.8}"; }
t() { local l m o=""; userin && bail "the user is in VR"; for l in "$@"; do m=$(mirx "$l"); o="$o$m"$'\n'; done
      printf '%s' "$o" > "$R/xrtest.tmp" && mv "$R/xrtest.tmp" "$R/xrtest.txt"; echo "$(date +%T) xrtest: $(echo $o)" >&2; sleep "${T_S:-0.6}"; }
x() { T_S=0.8 t "$1"; }
LH_A=L # the controller whose lower button is the game's A (the left X)
