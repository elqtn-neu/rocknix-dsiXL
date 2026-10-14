#!/bin/bash
# Removes the DSi Menu (EmulationStation is the start screen again). Your art and sounds in roms/dsixl stay.
HERE="$(cd "$(dirname "$0")" && pwd)"
sh "$HERE/.dsixl-install/device/install.sh" --uninstall > /storage/dsixl-install.log 2>&1
