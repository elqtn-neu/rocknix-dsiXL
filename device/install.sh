#!/bin/sh
# install.sh: installs the DSi Menu on the handheld (ROCKNIX with ROCKNIXDS, RG DS Plus). Run it over ssh from the
# unpacked release (or the repository, after sh build.sh aarch64):
#
#   sh device/install.sh             installs it, and a "DSi Menu" entry in Ports to try it from EmulationStation
#   sh device/install.sh --enable    ...and makes it the start screen (from the next boot)
#   sh device/install.sh --disable   back to EmulationStation at boot (the menu stays installed, in Ports)
#   sh device/install.sh --uninstall removes it (your art and sounds in /storage/roms/dsixl stay)
#   add --with-rocknixds to install ROCKNIXDS too when it's missing (DraStic on both panels; needs Wi-Fi)
#   add --dsi-brightness to make the Anbernic button + volume step through the DSi's five levels (else ROCKNIX's own)
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
D=/storage/.config/dsixl
# the Anbernic button + volume: ROCKNIX's input service runs these (the DSi's five brightness levels) instead of its
# own 5% steps; put back on uninstall. The service reads them when it starts.
hotkeys() {
    # (never waits long: ROCKNIX's settings lock or its input service must not hold the install up)
    timeout 15 bash -c '. /etc/profile.d/001-functions 2>/dev/null || exit 0
        if [ "$1" = on ]; then set_setting key.function.a.up "$2/brightness.sh up"; set_setting key.function.a.down "$2/brightness.sh down"
        else case "$(get_setting key.function.a.up)" in *dsixl/brightness.sh*) del_setting key.function.a.up; del_setting key.function.a.down ;; esac; fi' _ "$1" "$D" || true
    systemctl --no-block restart input.service 2>/dev/null || true     # (stopping it can take a while: don't wait)
}
case "$1" in
--uninstall)
    rm -f $D/enabled /storage/.config/autostart/zz-dsixl "/storage/roms/ports/DSi Menu.sh"
    if cmp -s $D/start_es.sh /usr/bin/start_es.sh 2>/dev/null; then umount -l /usr/bin/start_es.sh; fi
    hotkeys off
    rm -f /storage/.config/drastic/shaders/dsi-lcd.frag
    rm -rf $D
    echo "Removed. /storage/roms/dsixl (art, sounds) is still there."
    exit 0 ;;
--disable)
    rm -f $D/enabled
    echo "EmulationStation is the start screen again from the next boot. The DSi Menu is still in Ports."
    exit 0 ;;
esac

# ---- the emulators: the menu starts the ones on the handheld; check they're there ----
# DS: ROCKNIXDS's DraStic (libdsflip: both panels, the in-game menu, resume). GBA: ROCKNIX's RetroArch with mGBA.
D_DR=/storage/.config/drastic
have_rocknixds() { [ -f $D_DR/dsflip/libdsflip.so ] && [ -x $D_DR/dsflip/drastic ]; }
have_mgba() { ls /usr/lib/libretro/mgba_libretro.so /tmp/cores/mgba_libretro.so >/dev/null 2>&1; }
[ -x /usr/bin/runemu.sh ] || { echo "This isn't ROCKNIX (no /usr/bin/runemu.sh). Flash ROCKNIXDS's image first (see the README)."; exit 1; }
if ! have_rocknixds; then
    if [ "$WITH_ROCKNIXDS" = 1 ] || [ "$1" = --with-rocknixds ] || [ "$2" = --with-rocknixds ]; then
        echo "Installing ROCKNIXDS (DraStic on both screens), its own installer, over Wi-Fi..."
        curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | sh || { echo "ROCKNIXDS didn't install: DS games would use ROCKNIX's own DraStic launcher."; }
    else
        echo "Note: ROCKNIXDS isn't installed: DS games will use ROCKNIX's own DraStic launcher (under sway)."
        echo "      For DraStic straight to both panels, run this again with --with-rocknixds (needs Wi-Fi),"
        echo "      or flash ROCKNIXDS's RG DS Plus image (the README's way)."
    fi
fi
have_mgba || echo "Note: RetroArch's mGBA core wasn't found where ROCKNIX keeps it; GBA games use whatever ROCKNIX has."

BIN=$SRC/build/dsixl-aarch64; [ -f $BIN ] || BIN=$SRC/bin/dsixl-aarch64
[ -f $BIN ] || { echo "No dsixl-aarch64 binary (build it: sh build.sh aarch64)"; exit 1; }
mkdir -p $D/data $D/cache /storage/roms/dsixl/media/nds /storage/roms/dsixl/media/gba /storage/roms/dsixl/sounds
cp $BIN $D/dsixl.new && chmod 755 $D/dsixl.new && mv -f $D/dsixl.new $D/dsixl
rm -rf $D/assets.new && cp -r $SRC/assets $D/assets.new && rm -rf $D/assets && mv $D/assets.new $D/assets
# (each into a new file, then renamed over the old: a script that's running, as session.sh is during an update
# from the menu, keeps reading its own copy)
for f in session.sh restore.sh start_es.sh launch.sh brightness.sh backup.sh net.sh update.sh; do
    cp $SRC/device/$f $D/$f.new && chmod 755 $D/$f.new && mv -f $D/$f.new $D/$f
done
# the in-game DSi LCD look (Emulator settings > Screen > DSi LCD): libdsflip reads DraStic's shader folder
mkdir -p /storage/.config/drastic/shaders && cp $SRC/device/shaders/*.frag /storage/.config/drastic/shaders/ 2>/dev/null || true
cp $SRC/tools/dsixl-scrape.py $D/dsixl-scrape.py.new && mv -f $D/dsixl-scrape.py.new $D/dsixl-scrape.py
cp $SRC/VERSION $D/VERSION 2>/dev/null || true
# the recommended emulator settings, only where ROCKNIX has none of its own yet (yours are never touched)
DSIXL_HOME=$D $D/dsixl --setup-emulators || true
case " $* " in *" --dsi-brightness "*) hotkeys on ;; esac      # (ROCKNIX's own brightness steps unless asked)
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
if [ "$1" = --enable ] || [ "$2" = --enable ]; then
    touch $D/enabled
    echo "The DSi Menu is the start screen from the next boot (sh $0 --disable to undo)."
fi
echo "Installed $(cat $D/VERSION 2>/dev/null) in $D."
echo "  Art: python3 $D/dsixl-scrape.py  (box art, labels, logos into /storage/roms/dsixl/media)"
echo "  Sounds: /storage/roms/dsixl/sounds (see the README there)"
echo "  Try it: Ports > DSi Menu in EmulationStation."
