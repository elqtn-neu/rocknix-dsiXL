#!/bin/bash
# Bind-mounted over /usr/bin/start_es.sh at boot (autostart-dsixl) while the DSi Menu is the start screen. ROCKNIX's
# essway.service runs it with sway up: it hands the panels to the menu's own unit (session.sh), which stops sway and
# this service. When the menu asks for EmulationStation (/tmp/dsixl-to-es), this mount steps aside: the launcher under
# it (ROCKNIXDS's, or ROCKNIX's) runs, until the next boot.
D=/storage/.config/dsixl
if [ -e /tmp/dsixl-to-es ] || [ ! -x $D/dsixl ] || [ ! -e $D/enabled ]; then
    rm -f /tmp/dsixl-to-es
    umount -l /usr/bin/start_es.sh 2>/dev/null
    exec /usr/bin/start_es.sh "$@"
fi
if systemd-run --unit=dsixl --collect -p ExecStopPost=$D/restore.sh -p TimeoutStopSec=15 $D/session.sh >/dev/null 2>&1; then
    exec sleep 86400            # the session stops this service
fi
# no unit (one is still running?): EmulationStation, rather than nothing
umount -l /usr/bin/start_es.sh 2>/dev/null
exec /usr/bin/start_es.sh "$@"
