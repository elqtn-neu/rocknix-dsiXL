#!/bin/sh
# Installs the DSi Menu on BananaOS from the folder next to this script (dsixl-files): everything goes on the card
# (/data/dsixl), a "DSi Menu" entry goes first on BananaOS's shelf, and the menu opens.
# Run it from BananaOS's Ports shelf. The log: dsixl-install.log on the card.
HERE="$(cd "$(dirname "$0")" && pwd)"
LOG=/data/dsixl-install.log
if [ ! -f /etc/banana/systems.conf ]; then
    echo "The DSi Menu is made for BananaOS on the RG DS Plus." > $LOG
    exit 1
fi
# (older releases had it in a hidden folder, .dsixl-install, which copies could leave behind)
for F in "$HERE/dsixl-files" "$HERE/.dsixl-install"; do
    if [ -f "$F/device/banana/install.sh" ]; then
        sh "$F/device/banana/install.sh" > $LOG 2>&1
        exit $?
    fi
done
{
    echo "$(date): the DSi Menu's files aren't next to this script."
    echo "Expected the folder $HERE/dsixl-files (from the zip, with this script). What's there:"
    ls -la "$HERE"
} > $LOG 2>&1
exit 1
