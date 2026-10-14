#!/bin/sh
# sleep.sh lid|power: the handheld to sleep, the way ROCKNIX does it, once the menu's sleep animation has played.
# (While the menu runs it has the power button and the lid to itself, so ROCKNIX's input_sense never saw them.)
# Hardware suspend when ROCKNIX is set to it, else its "fake" suspend (screens off, processes frozen, the timed
# shutdown): what input_sense would have run. Waking is ROCKNIX's as always: the menu sees the screens come back.
CFG=/storage/.config/system/configs/system.cfg
# (rocknix-fake-suspend does nothing unless system.suspendmode is "off": unset means the hardware's own suspend)
MODE=$(sed -n 's/^system\.suspendmode=//p' $CFG 2>/dev/null | tail -n1)
if [ "$MODE" != off ]; then
    exec systemctl suspend
fi
case "$1" in
    lid) exec /usr/bin/rocknix-fake-suspend lid close ;;
    *) exec /usr/bin/rocknix-fake-suspend power ;;
esac
