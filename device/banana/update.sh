#!/bin/sh
# update.sh <dsixl-x.y.z.zip>: the menu's "Update" on BananaOS: a newer release's zip dropped on the card (its top
# folder, roms/ports or dsixl/updates). Unpacks it, runs its installer (banana-ui isn't restarted: the menu is
# running from it), puts its Install / Uninstall entries in Ports, and moves the zip out of the way.
ZIP=$1
D=/data/dsixl
[ -f "$ZIP" ] || { echo "no file: $ZIP"; exit 1; }
T=/data/.dsixl-update
rm -rf $T; mkdir -p $T
echo "unpacking $ZIP"
if command -v unzip >/dev/null; then unzip -q -o "$ZIP" -d $T
else python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$ZIP" $T; fi || { echo "can't unpack it"; rm -rf $T; exit 1; }
I=$T/roms/ports/dsixl-files
[ -f $I/device/banana/install.sh ] && [ -f $I/bin/dsixl-aarch64 ] || { echo "not a DSi Menu release for BananaOS"; rm -rf $T; exit 1; }
sh $I/device/banana/install.sh --no-restart || { echo "the installer failed"; rm -rf $T; exit 1; }
P=/data/roms/ports
mkdir -p $P
cp "$T/roms/ports/Install DSi Menu.sh" "$T/roms/ports/Uninstall DSi Menu.sh" $P/ 2>/dev/null
rm -rf $P/dsixl-files.old $P/.dsixl-install; [ -d $P/dsixl-files ] && mv $P/dsixl-files $P/dsixl-files.old
mv $I $P/dsixl-files && rm -rf $P/dsixl-files.old
mv -f "$ZIP" "$ZIP.installed" 2>/dev/null || rm -f "$ZIP"
rm -rf $T
cat $D/VERSION > /tmp/dsixl-updated 2>/dev/null
sync
echo "updated to $(cat $D/VERSION 2>/dev/null)"
