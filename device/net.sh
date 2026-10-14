#!/bin/bash
# net.sh: the menu's Wi-Fi and clock, through ROCKNIX's own tools (wifictl, its settings), so EmulationStation sees
# the same thing.
#   net.sh status              connected <ssid> <ip> | disconnected | off | none (no Wi-Fi here)
#   net.sh scan                the networks: <signal>\t<secured 0/1>\t<in use 0/1>\t<ssid>, strongest first
#   net.sh connect <ssid> [psk]  join it (remembered); prints ok or fail
#   net.sh on | off            Wi-Fi on (joining the remembered network) or off
#   net.sh tz <Region/City>    the time zone
#   net.sh time                set the clock from the internet now
. /etc/profile.d/001-functions 2>/dev/null
WIFI_DEV=$(ls /sys/class/net 2>/dev/null | grep -m1 '^wlan')
ip_of() { ip -4 -o addr show "$WIFI_DEV" 2>/dev/null | awk '{ sub(/\/.*/, "", $4); print $4; exit }'; }
status() {
    [ -n "$WIFI_DEV" ] || { echo none; return; }
    rfkill list wifi 2>/dev/null | grep -q 'Soft blocked: yes' && { echo off; return; }
    S=$(nmcli -t -f GENERAL.CONNECTION device show "$WIFI_DEV" 2>/dev/null | cut -d: -f2-)
    I=$(ip_of)
    if [ -n "$I" ] && [ -n "$S" ] && [ "$S" != "--" ]; then printf 'connected\t%s\t%s\n' "$S" "$I"; else echo disconnected; fi
}
sync_time() {
    # ROCKNIX keeps the clock with systemd-timesyncd when it runs; a nudge after joining a network
    systemctl restart systemd-timesyncd.service 2>/dev/null
    command -v ntpdate >/dev/null && ntpdate -s pool.ntp.org 2>/dev/null
    hwclock -w 2>/dev/null
    return 0
}
case "$1" in
    status) status ;;
    scan)
        [ -n "$WIFI_DEV" ] || exit 1
        rfkill unblock wifi 2>/dev/null
        nmcli -w 10 dev wifi rescan ifname "$WIFI_DEV" >/dev/null 2>&1; sleep 1
        # IN-USE:SIGNAL:SECURITY:SSID (the SSID last: it may have colons, escaped as \:)
        nmcli -t -f IN-USE,SIGNAL,SECURITY,SSID dev wifi list ifname "$WIFI_DEV" 2>/dev/null |
            awk -F: '{ use = ($1 == "*"); sig = $2; sec = ($3 != "" && $3 != "--"); ssid = $0;
                       sub(/^[^:]*:[^:]*:[^:]*:/, "", ssid); gsub(/\\:/, ":", ssid);
                       if (ssid != "" && !(ssid in seen)) { seen[ssid] = 1; printf "%d\t%d\t%d\t%s\n", sig, sec, use, ssid } }' |
            sort -t "$(printf '\t')" -k1,1nr ;;
    connect)
        set_setting wifi.enabled 1; set_setting wifi.ssid "$2"; set_setting wifi.key "$3"
        wifictl enable; wifictl connect "$2" "$3"
        for i in 1 2 3 4 5 6 7 8 9 10; do [ -n "$(ip_of)" ] && break; sleep 1; done
        if [ -n "$(ip_of)" ]; then wifictl pin; sync_time; echo ok; else echo fail; exit 1; fi ;;
    on) set_setting wifi.enabled 1; wifictl enable; wifictl connect; sync_time; status ;;
    off) set_setting wifi.enabled 0; wifictl disconnect; wifictl disable; status ;;
    tz)
        [ -f "/usr/share/zoneinfo/$2" ] || { echo "no such time zone: $2"; exit 1; }
        echo "TIMEZONE=$2" > /storage/.cache/timezone
        ln -sf "/usr/share/zoneinfo/$2" /var/run/localtime
        set_setting system.timezone "$2"
        echo ok ;;
    time) sync_time; date ;;
    *) echo "usage: $0 status|scan|connect <ssid> [psk]|on|off|tz <zone>|time"; exit 2 ;;
esac
