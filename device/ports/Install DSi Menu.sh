#!/bin/bash
# Installs the DSi Menu from the folder next to this script (.dsixl-install), makes it the start screen, and opens it.
# Run it from EmulationStation: Ports > Install DSi Menu. The log: /storage/dsixl-install.log
# (On BananaOS: its Ports shelf, Install DSi Menu.)
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -f /etc/banana/systems.conf ]; then
    # BananaOS (its Ports shelf runs this): everything on the card, a "DSi Menu" entry first on the shelf, and
    # BananaOS's menu started again to show it. The log: dsixl-install.log on the card
    sh "$HERE/.dsixl-install/device/banana/install.sh" > /data/dsixl-install.log 2>&1
    exit $?
fi
LOG=/storage/dsixl-install.log
{
  echo "$(date) installing from $HERE/.dsixl-install"
  sh "$HERE/.dsixl-install/device/install.sh" --enable
} > $LOG 2>&1
rc=$?
if [ $rc = 0 ] && [ -x /storage/.config/dsixl/dsixl ]; then
    # open it now (from the next boot it's the start screen by itself)
    D=/storage/.config/dsixl
    systemd-run --unit=dsixl --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=15 $D/session.sh >/dev/null 2>&1 && exec sleep 86400
fi
echo "The install didn't finish: see $LOG" >> $LOG
exit 1
