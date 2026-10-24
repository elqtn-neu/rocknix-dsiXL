#!/bin/sh
# brightness.sh up|down|set <1-5>|get|apply: the DSi's five brightness levels on BananaOS (the menu's sun button).
# Both backlights at once, written as BananaOS's Settings > Brightness writes them, and kept where it keeps them
# (banana/backlight, in per mille: bananad puts it back at boot and after a wake).
LEVELS="100 250 450 700 1000"      # per mille: level 1 (darkest) .. 5
F=/data/banana/backlight
pm_of() { echo $LEVELS | cut -d' ' -f$1; }
panels() {      # $1 per mille: both backlights on, at that
    for d in /sys/class/backlight/*; do
        [ -f "$d/brightness" ] || continue
        max=$(cat "$d/max_brightness" 2>/dev/null || echo 255)
        v=$(( $1 * max / 1000 )); [ $v -lt 1 ] && v=1
        echo 0 > "$d/bl_power" 2>/dev/null
        echo $v > "$d/brightness" 2>/dev/null
    done
}
cur=$(head -n1 $F 2>/dev/null); [ "$cur" -gt 0 ] 2>/dev/null || cur=700
lv=1; best=100000; i=1
for p in $LEVELS; do d=$((p > cur ? p - cur : cur - p)); [ $d -lt $best ] && { best=$d; lv=$i; }; i=$((i + 1)); done
case "$1" in
    up) [ $lv -lt 5 ] && lv=$((lv + 1)) ;;
    down) [ $lv -gt 1 ] && lv=$((lv - 1)) ;;
    set) [ "$2" -ge 1 ] 2>/dev/null && [ "$2" -le 5 ] && lv=$2 ;;
    apply) panels "$cur"; exit 0 ;;
    *) echo $lv; exit 0 ;;
esac
panels $(pm_of $lv)
pm_of $lv > $F.new && mv -f $F.new $F
echo $lv > /tmp/dsixl-brightness      # the menu shows its brightness bar when this changes
exit 0
