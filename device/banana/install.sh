#!/bin/sh
# install.sh [--uninstall] [--no-restart]: the DSi Menu on BananaOS, from the release folder this is in
# (roms/ports/dsixl-files, which the zip puts on the card). Run by Ports > Install DSi Menu (or by the menu's
# Update). BananaOS's system is read-only, so everything goes on the card:
#   /data/dsixl              the menu, its scripts and its data (art in media/, your sounds in sounds/)
#   /data/banana/shaders     dsi-lcd.frag and dsi-lcd-color.frag (Settings > Nintendo DS > Shader lists them too)
#   /data/banana/systems.conf  BananaOS's systems list with a "DSi Menu" entry first on its shelf ([ndsiware]: the
#                            DS's icon). A card that had no copy of the list gets BananaOS's own, with the entry.
# Then banana-ui starts again, so its shelf has the entry. Uninstalling takes it all away again (art and sounds stay).
SRC=$(cd "$(dirname "$0")/../.." && pwd)
D=/data/dsixl
SYS=/data/banana/systems.conf
ID=ndsiware
restart_ui() {
    # banana-ui reads the systems list when it starts; init starts it again when it stops. It's asked to stop once the
    # port that ran this has ended (from a session of its own, so the Ports runner doesn't stop it with the rest of
    # the port). With "first": it opens on the shelf's first entry, the DSi Menu (banana-ui saves the shelf it was on
    # as it stops, so that's written after it's gone and before the new one reads it).
    case " $* " in *" --no-restart "*) return ;; esac
    FIRST=0; case " $* " in *" first "*) FIRST=1 ;; esac
    setsid sh -c '
        sleep 1
        P=$(pidof banana-ui) || exit 0
        kill -TERM $P
        i=0; while [ $i -lt 100 ] && kill -0 $P 2>/dev/null; do sleep 0.05; i=$((i + 1)); done
        if [ '$FIRST' = 1 ]; then i=0; while [ $i -lt 20 ]; do echo 0 > /data/banana/last-system; sleep 0.05; i=$((i + 1)); done; fi
    ' >/dev/null 2>&1 < /dev/null &
}
# the bottom panel's backlight back on (the Ports runner turns it off: ports draw on the top panel only)
bottom_on() {
    BL=$(cat /run/banana/panel-off 2>/dev/null)
    [ -n "$BL" ] && [ -w "$BL/bl_power" ] && echo 0 > "$BL/bl_power"
    rm -f /run/banana/panel-off
}
# the systems list without our entry (its [ndsiware] section, to the next section)
without_entry() { awk -v id="[$ID]" '/^\[/ { skip = ($1 == id) } !skip' "$1"; }

if [ "$1" = --uninstall ]; then
    if [ -f $SYS ]; then
        without_entry $SYS > $SYS.new && mv -f $SYS.new $SYS
        # nothing of yours in it: BananaOS's own list again (so its updates' new systems show)
        cmp -s $SYS /etc/banana/systems.conf && rm -f $SYS
    fi
    rm -f /data/banana/shaders/dsi-lcd.frag /data/banana/shaders/dsi-lcd-color.frag
    for f in $D/* $D/.[!.]*; do case ${f##*/} in media|sounds) ;; *) rm -rf "$f" ;; esac; done
    echo "Removed. Your art and sounds stay in $D (delete the folder to remove them too)."
    restart_ui "$@"
    exit 0
fi

[ -f /etc/banana/systems.conf ] || { echo "This isn't BananaOS"; exit 1; }
BIN=$SRC/bin/dsixl-aarch64; [ -f $BIN ] || { echo "No dsixl-aarch64 in $SRC/bin"; exit 1; }
mkdir -p $D/data $D/cache $D/media/nds $D/media/gba $D/sounds $D/shelf/media/box /data/banana/shaders
cp $BIN $D/dsixl.new && chmod 755 $D/dsixl.new && mv -f $D/dsixl.new $D/dsixl
rm -rf $D/assets.new && cp -r $SRC/assets $D/assets.new && rm -rf $D/assets && mv $D/assets.new $D/assets
# (each into a new file, then renamed over the old: the session script that's running keeps its own copy)
for f in session.sh brightness.sh update.sh; do
    cp $SRC/device/banana/$f $D/$f.new && chmod 755 $D/$f.new && mv -f $D/$f.new $D/$f
done
cp $SRC/tools/dsixl-scrape.py $D/dsixl-scrape.py.new && mv -f $D/dsixl-scrape.py.new $D/dsixl-scrape.py
cp $SRC/VERSION $D/VERSION 2>/dev/null
cp $SRC/docs/sounds.md $D/sounds/README.md 2>/dev/null
cp $SRC/device/shaders/*.frag /data/banana/shaders/
# the shelf's entry: one "game" (the menu), with its picture
: > "$D/shelf/DSi Menu.app"
cp $SRC/assets/banana/box.png "$D/shelf/media/box/DSi Menu.png" 2>/dev/null
# the systems list: BananaOS's (or the card's own), the entry first
[ -f $SYS ] || cp /etc/banana/systems.conf $SYS
without_entry $SYS > $SYS.rest
{
    awk '/^\[/ { exit } { print }' $SYS.rest            # (its opening comments)
    cat <<SECTION
[$ID]
name = DSi Menu
maker = DSi XL style
folder = $D/shelf
ext = app
run = $D/session.sh %ROM%
perf = powersave
always = yes
flat = yes

SECTION
    awk 'f || /^\[/ { f = 1; print }' $SYS.rest
} > $SYS.new && mv -f $SYS.new $SYS
rm -f $SYS.rest
sync
echo "Installed $(cat $D/VERSION 2>/dev/null) in $D. BananaOS's shelf: DSi Menu, first."
case " $* " in
*" --no-restart "*) ;;
*)
    # straight into it (the port's own run: Exit to BananaOS ends it), then BananaOS's menu again, on the DSi Menu
    bottom_on
    echo "opening it"
    sh $D/session.sh
    restart_ui first
    ;;
esac
exit 0
