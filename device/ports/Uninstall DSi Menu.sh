#!/bin/sh
# Removes the DSi Menu from BananaOS (its shelf entry, its files). Your box art and sounds in /data/dsixl stay.
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -f /etc/banana/systems.conf ] || exit 1
for F in "$HERE/dsixl-files" "$HERE/.dsixl-install"; do
    [ -f "$F/device/banana/install.sh" ] && { sh "$F/device/banana/install.sh" --uninstall > /data/dsixl-install.log 2>&1; exit $?; }
done
exit 1
