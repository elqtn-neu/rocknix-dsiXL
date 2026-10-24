#!/bin/sh
# package.sh: dist/dsixl-<version>.zip, for BananaOS: unpacked onto the card's root it puts roms/ports/Install DSi
# Menu.sh (and Uninstall) in BananaOS's Ports, and everything they install in roms/ports/.dsixl-install.
#   sh build.sh aarch64 && sh tools/package.sh
set -e
cd "$(dirname "$0")/.."
V=$(cat VERSION)
BIN=build/dsixl-aarch64; [ -f $BIN ] || BIN=bin/dsixl-aarch64
T=$(mktemp -d)
P=$T/roms/ports; I=$P/.dsixl-install
mkdir -p $I/bin $I/tools $I/docs
cp "device/ports/Install DSi Menu.sh" "device/ports/Uninstall DSi Menu.sh" $P/
cp $BIN $I/bin/dsixl-aarch64
mkdir -p $I/device && cp -r assets $I/ && cp -r device/banana device/shaders $I/device/
cp tools/dsixl-scrape.py $I/tools/ && cp docs/sounds.md $I/docs/ && cp VERSION README.md THIRD_PARTY.md $I/
mkdir -p dist && rm -f dist/dsixl-$V.zip
(cd $T && python3 -c "
import os, zipfile, sys
z = zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED)
for root, dirs, files in os.walk('roms'):
    for f in files:
        p = os.path.join(root, f); i = zipfile.ZipInfo.from_file(p, p)
        i.external_attr = (0o755 if f.endswith('.sh') or 'dsixl-aarch64' in f else 0o644) << 16
        z.writestr(i, open(p, 'rb').read(), zipfile.ZIP_DEFLATED)
" "$OLDPWD/dist/dsixl-$V.zip")
rm -rf $T
ls -l dist/dsixl-$V.zip
