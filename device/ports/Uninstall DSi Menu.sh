#!/bin/bash
# Removes the DSi Menu (EmulationStation is the start screen again). Your art and sounds in roms/dsixl stay.
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -f /etc/banana/systems.conf ]; then
    sh "$HERE/.dsixl-install/device/banana/install.sh" --uninstall > /data/dsixl-install.log 2>&1
    exit $?
fi
sh "$HERE/.dsixl-install/device/install.sh" --uninstall > /storage/dsixl-install.log 2>&1
