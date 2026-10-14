#!/bin/sh
# update.sh <dsixl-x.y.z.zip>: the menu's "Update": installs a newer release dropped on the games card (the zip
# itself, in roms/ports or the card's top folder). It unpacks it, runs its installer as the Ports entry would (the
# start screen stays as it is), puts its Install / Uninstall entries in Ports, and moves the zip out of the way.
ZIP=$1
D=/storage/.config/dsixl
[ -f "$ZIP" ] || { echo "no file: $ZIP"; exit 1; }
T=/storage/.dsixl-update
rm -rf $T; mkdir -p $T
echo "unpacking $ZIP"
if command -v unzip >/dev/null; then unzip -q -o "$ZIP" -d $T
else python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$ZIP" $T; fi || { echo "can't unpack it"; rm -rf $T; exit 1; }
I=$T/roms/ports/.dsixl-install
[ -f $I/device/install.sh ] && [ -f $I/bin/dsixl-aarch64 ] || { echo "not a DSi Menu release"; rm -rf $T; exit 1; }
chmod -R a+rX $I; chmod 755 $I/bin/dsixl-aarch64 $I/device/*.sh
FLAG=; [ -e $D/enabled ] && FLAG=--enable
sh $I/device/install.sh $FLAG || { echo "the installer failed"; rm -rf $T; exit 1; }
# the Ports entries and the installer's copy, as the zip has them
P=/storage/roms/ports
mkdir -p $P
cp "$T/roms/ports/Install DSi Menu.sh" "$T/roms/ports/Uninstall DSi Menu.sh" $P/ 2>/dev/null
rm -rf $P/.dsixl-install.old; [ -d $P/.dsixl-install ] && mv $P/.dsixl-install $P/.dsixl-install.old
mv $I $P/.dsixl-install && rm -rf $P/.dsixl-install.old
mv -f "$ZIP" "$ZIP.installed" 2>/dev/null || rm -f "$ZIP"
rm -rf $T
cat $D/VERSION > /tmp/dsixl-updated 2>/dev/null
sync
echo "updated to $(cat $D/VERSION 2>/dev/null)"
