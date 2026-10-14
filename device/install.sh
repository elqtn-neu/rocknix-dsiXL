#!/bin/sh
# install.sh: installs the DSi Menu on the handheld (ROCKNIX with ROCKNIXDS, RG DS Plus). Run it over ssh from the
# unpacked release (or the repository, after sh build.sh aarch64):
#
#   sh device/install.sh             installs it, and a "DSi Menu" entry in Ports to try it from EmulationStation
#   sh device/install.sh --enable    ...and makes it the start screen (from the next boot)
#   sh device/install.sh --disable   back to EmulationStation at boot (the menu stays installed, in Ports)
#   sh device/install.sh --uninstall removes it (your art and sounds in /storage/roms/dsixl stay)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
D=/storage/.config/dsixl
case "$1" in
--uninstall)
    rm -f $D/enabled /storage/.config/autostart/zz-dsixl "/storage/roms/ports/DSi Menu.sh"
    if cmp -s $D/start_es.sh /usr/bin/start_es.sh 2>/dev/null; then umount -l /usr/bin/start_es.sh; fi
    rm -rf $D
    echo "Removed. /storage/roms/dsixl (art, sounds) is still there."
    exit 0 ;;
--disable)
    rm -f $D/enabled
    echo "EmulationStation is the start screen again from the next boot. The DSi Menu is still in Ports."
    exit 0 ;;
esac

BIN=$SRC/build/dsixl-aarch64; [ -f $BIN ] || BIN=$SRC/bin/dsixl-aarch64
[ -f $BIN ] || { echo "No dsixl-aarch64 binary (build it: sh build.sh aarch64)"; exit 1; }
mkdir -p $D/data $D/cache /storage/roms/dsixl/media/nds /storage/roms/dsixl/media/gba /storage/roms/dsixl/sounds
cp $BIN $D/dsixl.new && chmod 755 $D/dsixl.new && mv -f $D/dsixl.new $D/dsixl
rm -rf $D/assets && cp -r $SRC/assets $D/assets
for f in session.sh restore.sh start_es.sh launch.sh; do cp $SRC/device/$f $D/$f; chmod 755 $D/$f; done
cp $SRC/tools/dsixl-scrape.py $D/dsixl-scrape.py
cp $SRC/VERSION $D/VERSION 2>/dev/null || true
cp $SRC/docs/sounds.md /storage/roms/dsixl/sounds/README.md 2>/dev/null || true
mkdir -p /storage/.config/autostart
cp $SRC/device/autostart-dsixl /storage/.config/autostart/zz-dsixl && chmod 755 /storage/.config/autostart/zz-dsixl
mkdir -p /storage/roms/ports
cat > "/storage/roms/ports/DSi Menu.sh" <<'EOF'
#!/bin/bash
# DSi Menu: takes over both screens; Options > Exit to EmulationStation comes back here.
D=/storage/.config/dsixl
if systemd-run --unit=dsixl --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=15 $D/session.sh >/dev/null 2>&1; then
    exec sleep 86400
fi
EOF
chmod 755 "/storage/roms/ports/DSi Menu.sh"
if [ "$1" = --enable ]; then
    touch $D/enabled
    echo "The DSi Menu is the start screen from the next boot (sh $0 --disable to undo)."
fi
echo "Installed $(cat $D/VERSION 2>/dev/null) in $D."
echo "  Art: python3 $D/dsixl-scrape.py  (box art, labels, logos into /storage/roms/dsixl/media)"
echo "  Sounds: /storage/roms/dsixl/sounds (see the README there)"
echo "  Try it: Ports > DSi Menu in EmulationStation."
