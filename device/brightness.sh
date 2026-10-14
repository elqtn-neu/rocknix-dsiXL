#!/bin/bash
# brightness.sh up|down|set <1-5>|get|apply: the DSi's five brightness levels, on both screens at once (the menu's
# sun button; with install.sh --dsi-brightness also the Anbernic button + volume).
# The panels are set straight through sysfs, at once; ROCKNIX's own "brightness" then records the level (in the
# background, with a time limit: it takes ROCKNIX's settings lock, which can be held for good).
LEVELS="10 25 45 70 100"            # percent: level 1 (darkest) .. 5
CFG=/storage/.config/system/configs/system.cfg
pct_of() { echo $LEVELS | cut -d' ' -f$1; }
panels() {      # $1 percent: both backlights on, at that brightness
    for d in /sys/class/backlight/*; do
        [ -f "$d/brightness" ] || continue
        max=$(cat "$d/max_brightness" 2>/dev/null || echo 255)
        echo 0 > "$d/bl_power" 2>/dev/null
        echo $(( $1 * max / 100 )) > "$d/brightness" 2>/dev/null
    done
}
# the level now: the one nearest ROCKNIX's setting (read only: no lock)
cur=$(sed -n 's/^display\.brightness=//p' $CFG 2>/dev/null | tail -n1); [ -n "$cur" ] || cur=70
lv=1; best=1000; i=1
for p in $LEVELS; do d=$((p > cur ? p - cur : cur - p)); [ $d -lt $best ] && { best=$d; lv=$i; }; i=$((i + 1)); done
case "$1" in
    up) [ $lv -lt 5 ] && lv=$((lv + 1)) ;;
    down) [ $lv -gt 1 ] && lv=$((lv - 1)) ;;
    set) [ "$2" -ge 1 ] 2>/dev/null && [ "$2" -le 5 ] && lv=$2 ;;
    apply) panels "$cur"; exit 0 ;;   # back as it was set (after the menu dimmed the panels, or turned them off)
    *) echo $lv; exit 0 ;;
esac
panels $(pct_of $lv)
echo $lv > /tmp/dsixl-brightness      # the menu shows its brightness bar when this changes
( timeout 10 $(command -v brightness || echo /usr/bin/brightness) set all $(pct_of $lv) >/dev/null 2>&1 & )
exit 0
