#!/bin/sh
# backup.sh NDS|GBA <rom>: after every game, a copy of its save (when it changed) in
# /storage/roms/dsixl/backups/<nds|gba>/<game>/, the last 5 kept. A save that goes bad can be put back from there:
# copy the newest good one next to the game under its own name (Game.dsv, Game.srm).
SYS=$1 ROM=$2
B=/storage/roms/dsixl/backups
NAME=$(basename "$ROM"); NAME=${NAME%.*}
DIR=$(dirname "$ROM")
case "$SYS" in NDS) SUB=nds ;; GBA) SUB=gba ;; *) exit 0 ;; esac
OUT="$B/$SUB/$NAME"
pick() {    # the newest of the save files that exist
    newest=
    for c in "$@"; do [ -f "$c" ] || continue; [ -z "$newest" ] || [ "$c" -nt "$newest" ] && newest=$c; done
    echo "$newest"
}
case "$SYS" in
    NDS) S=$(pick "$DIR/$NAME.dsv" "/storage/roms/nds/$NAME.dsv" "/storage/.config/drastic/backup/$NAME.dsv") ;;
    GBA) S=$(pick "$DIR/$NAME.srm" "/storage/roms/gba/$NAME.srm" "/storage/roms/savestates/gba/$NAME.srm" "/storage/roms/saves/gba/$NAME.srm") ;;
esac
[ -n "$S" ] || exit 0
mkdir -p "$OUT" || exit 0
LAST=$(ls -1 "$OUT" 2>/dev/null | sort | tail -n1)
[ -n "$LAST" ] && cmp -s "$S" "$OUT/$LAST" && exit 0           # unchanged since the last copy
EXT=${S##*.}
cp "$S" "$OUT/$(date +%Y-%m-%d_%H%M%S).$EXT" && sync
# the last 5
n=$(ls -1 "$OUT" | wc -l)
[ "$n" -gt 5 ] && ls -1 "$OUT" | sort | head -n $((n - 5)) | while read -r old; do rm -f "$OUT/$old"; done
exit 0
