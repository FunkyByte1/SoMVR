#!/bin/bash
# ladder_rung.sh - som-b37-ladder (docs/re-xr-pipeline.md 172): keep a save as a story ladder rung, read only.
#   ladder_rung.sh NN-mission-name SAVE README
# copies SAVE (a run's saves/GameData.sav or saves/NAME.sav - never the user's Steam save or play prefix, never the pinned
# test save's own file: a copy of it is fine) to logs/test-save/ladder/NN-mission-name/GameData.sav (mode 0400), its
# sha256 into GameData.sav.sha256, README (a text file: the mission, `marker: NAME X Y Z`, Talion's place, the time of
# day, the weather, what is unlocked, how it was reached) as README.txt. An existing rung is never replaced (remove it by
# hand first, after looking at it). The source's sha256 is checked before and after (only read).
set -uo pipefail
SOM=/home/bara/Projects/flat2vr/ShadowOfMordor; L=$SOM/logs/test-save/ladder
N=${1:?usage: ladder_rung.sh NN-mission-name SAVE README}; S=${2:?save}; T=${3:?README}
[[ $N =~ ^[0-9][0-9][a-z]?-[a-z0-9-]+$ ]] || { echo "STOP: $N is not NN-mission-name"; exit 1; }
[ -f "$S" ] && [ "$(stat -c %s "$S")" -gt 0 ] || { echo "STOP: no save $S"; exit 1; }
case $(realpath "$S") in */Steam/*|*/Heroic/*|*/Games/*|*/compatdata/*) echo "STOP: $S looks like the user's own save"; exit 1 ;; esac
[ -f "$T" ] && grep -q '^marker: ' "$T" || { echo "STOP: $T has no 'marker:' line"; exit 1; }
D=$L/$N; [ -e "$D" ] && { echo "STOP: $D exists"; exit 1; }
H0=$(sha256sum "$S" | cut -d' ' -f1)
mkdir -p "$D.tmp" && cp --no-preserve=mode "$S" "$D.tmp/GameData.sav" && cp "$T" "$D.tmp/README.txt" || { rm -rf "${D:?}.tmp"; exit 1; }
H1=$(sha256sum "$D.tmp/GameData.sav" | cut -d' ' -f1)
[ "$H1" = "$H0" ] && [ "$(sha256sum "$S" | cut -d' ' -f1)" = "$H0" ] || { echo "STOP: the copy differs"; rm -rf "${D:?}.tmp"; exit 1; }
echo "$H1  GameData.sav" > "$D.tmp/GameData.sav.sha256"
chmod 0400 "$D.tmp/GameData.sav"; chmod 0444 "$D.tmp/GameData.sav.sha256"   # (the README stays editable: a boot fills it in)
mv "$D.tmp" "$D" && echo "rung $N: ${H1:0:16} from $S"
