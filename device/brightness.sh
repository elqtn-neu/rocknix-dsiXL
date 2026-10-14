#!/bin/bash
# brightness.sh up|down|set <1-5>|get|apply: the DSi's five brightness levels, on both screens at once.
# ROCKNIX's input service runs it for the Anbernic button + volume up/down (key.function.a.up/down in system.cfg,
# set by install.sh), in the menu and in games; the menu runs it for its brightness button. ROCKNIX's own
# "brightness" does the writing, so its settings (and the level at the next boot) stay its own.
. /etc/profile.d/001-functions 2>/dev/null
LEVELS="10 25 45 70 100"            # percent: level 1 (darkest) .. 5
pct_of() { echo $LEVELS | cut -d' ' -f$1; }
# the level now: the one nearest ROCKNIX's setting for the top screen
cur=$(get_setting display.brightness 2>/dev/null); [ -n "$cur" ] || cur=70
lv=1; best=1000; i=1
for p in $LEVELS; do d=$((p > cur ? p - cur : cur - p)); [ $d -lt $best ] && { best=$d; lv=$i; }; i=$((i + 1)); done
case "$1" in
    up) [ $lv -lt 5 ] && lv=$((lv + 1)) ;;
    down) [ $lv -gt 1 ] && lv=$((lv - 1)) ;;
    set) [ "$2" -ge 1 ] 2>/dev/null && [ "$2" -le 5 ] && lv=$2 ;;
    apply)  # the level as it is, written to both panels again (after the menu dimmed them, or turned them off)
        for f in /sys/class/backlight/*/bl_power; do echo 0 > "$f"; done 2>/dev/null
        $(command -v brightness || echo /usr/bin/brightness) set all $(pct_of $lv) >/dev/null 2>&1; exit 0 ;;
    *) echo $lv; exit 0 ;;
esac
$(command -v brightness || echo /usr/bin/brightness) set all $(pct_of $lv) >/dev/null 2>&1
# the menu shows its brightness bar when this changes
echo $lv > /tmp/dsixl-brightness
