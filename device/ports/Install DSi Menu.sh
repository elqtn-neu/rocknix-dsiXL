#!/bin/sh
# Installs the DSi Menu on BananaOS from the folder next to this script (.dsixl-install): everything goes on the card
# (/data/dsixl), a "DSi Menu" entry goes first on BananaOS's shelf, and BananaOS's menu starts again to show it.
# Run it from BananaOS's Ports shelf. The log: dsixl-install.log on the card.
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f /etc/banana/systems.conf ]; then
    echo "The DSi Menu is made for BananaOS on the RG DS Plus."
    exit 1
fi
sh "$HERE/.dsixl-install/device/banana/install.sh" > /data/dsixl-install.log 2>&1
