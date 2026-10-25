#!/bin/sh
# Gives the panels back to sway and EmulationStation (after Options > Exit to EmulationStation, or when the session's
# unit is stopped). ROCKNIXDS's own restore when it's installed (it checks sway's outputs, puts governors back and
# shows notices), else the essentials. Safe to run twice.
GPU=/sys/class/devfreq/fde60000.gpu
if [ -f /tmp/dsixl-gpu-governor ]; then cat /tmp/dsixl-gpu-governor > $GPU/governor 2>/dev/null; rm -f /tmp/dsixl-gpu-governor; fi
R=/storage/.config/drastic/dsflip/restore.sh
[ -x $R ] && exec $R
systemctl is-active -q sway.service || systemctl start sway.service
systemctl is-active -q essway.service || systemctl start essway.service
exit 0
