#!/bin/sh
# launch.sh NDS|GBA <rom>: runs a game for the DSi Menu's session (session.sh), with nothing else on the panels,
# and returns when it ends.
#
#   NDS  ROCKNIXDS's DraStic with libdsflip, straight to both panels (no compositor): the same path ROCKNIXDS's own
#        DS session takes (dsflip/device/session.sh), with its settings: the power profile, the 3D renderer,
#        resume on quit (the exit hotkey saves and quits; the next start carries on). Without ROCKNIXDS: ROCKNIX's
#        own DraStic launcher under sway.
#   GBA  RetroArch (mGBA by default: gba_core in config.ini) through ROCKNIX's runemu.sh, under sway, on the top
#        screen at a whole-number scale.
SYS=$1 ROM=$2
D=/storage/.config/dsixl
DR=/storage/.config/drastic
RT=/var/run/0-runtime-dir
CFG=/storage/.config/system/configs/system.cfg
conf() { sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" $D/config.ini 2>/dev/null | tail -n1; }
sysconf() { grep "^$1=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2-; }
up() { read u _ < /proc/uptime; echo "[$u]"; }
CPU=/sys/devices/system/cpu/cpufreq/policy0
T0=$(cut -d' ' -f1 /proc/uptime)
rm -f /tmp/dsixl-error /tmp/dsixl-rescued

# what went wrong, for the menu to say when it's back (/tmp/dsixl-error: a title line, then the reason)
err() { printf '%s\n%s\n' "$1" "$2" > /tmp/dsixl-error; echo "$(up) error: $1: $2"; }
# a game that ends within a few seconds with an error didn't really start
check_end() {   # $1 exit status, $2 the emulator's name, $3 its log
    E=$(awk -v a="$T0" -v b="$(cut -d' ' -f1 /proc/uptime)" 'BEGIN { print (b - a < 4) }')
    if [ "$E" = 1 ] && [ "$1" != 0 ] && [ ! -e /tmp/dsixl-rescued ]; then
        err "This software couldn't be started." "$2 closed right away (error $1). What it said is in $3"
    fi
}
# the battery nearly empty in a game: save and leave, rather than lose the game (watched every 20 s)
battery_now() {
    for d in /sys/class/power_supply/*; do
        [ "$(cat $d/type 2>/dev/null)" = Battery ] || continue
        echo "$(cat $d/capacity 2>/dev/null) $(cat $d/status 2>/dev/null)"; return
    done
}
rescue_watch() {    # $@: how to make the game save and quit
    while sleep 20; do
        read -r cap st <<EOF
$(battery_now)
EOF
        if [ "$st" = Discharging ] && [ "${cap:-100}" -le 3 ] 2>/dev/null; then
            echo "$(up) battery at $cap%: saving and leaving the game"
            touch /tmp/dsixl-rescued; "$@"; break
        fi
    done
}
[ -f "$ROM" ] || { err "This software couldn't be started." "The game's file isn't there any more: $ROM"; exit 1; }

# sway for the games that need a compositor; stopped again afterwards (the menu needs the panels to itself)
sway_up() {
    systemctl start sway.service
    i=0
    while [ $i -lt 100 ]; do
        S=$(ls $RT/sway-ipc.*.sock 2>/dev/null | head -n1)
        [ -n "$S" ] && XDG_RUNTIME_DIR=$RT swaymsg -s "$S" -t get_outputs 2>/dev/null | grep -q '"active": true' && break
        sleep 0.05; i=$((i + 1))
    done
    export XDG_RUNTIME_DIR=$RT SWAYSOCK=$S
    W=$(ls $RT 2>/dev/null | grep -m1 '^wayland-[0-9]*$'); [ -n "$W" ] && export WAYLAND_DISPLAY=$W
    # the game on the top screen alone
    swaymsg "output DSI-2 pos 0 0" >/dev/null 2>&1
    [ "$(conf gba_bottom_off)" != 0 ] && swaymsg "output DSI-1 power off" >/dev/null 2>&1
}
sway_down() { systemctl stop sway.service; }

run_emu() {   # ROCKNIX's launcher, as ES calls it
    [ -f /etc/profile ] && . /etc/profile >/dev/null 2>&1
    /usr/bin/runemu.sh "$ROM" -P"$1" --core="$2" --emulator="$3" --controllers=""
}

case "$SYS" in
NDS)
    if [ -f $DR/dsflip/libdsflip.so ] && [ -x $DR/dsflip/drastic ] && [ ! -e $DR/nodsflip ]; then
        GAME=$(basename "$ROM")
        # the clock: on performance for the game (libdsflip's own governor sets the limit); put back afterwards
        GOV=$(cat $CPU/scaling_governor 2>/dev/null); MAXF=$(cat $CPU/scaling_max_freq 2>/dev/null)
        echo performance > $CPU/scaling_governor 2>/dev/null
        # ES's DS settings, per game or for the system, as ROCKNIXDS's session reads them
        pick() { v=$(grep -F "nds[\"$GAME\"].$1=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2); [ -n "$v" ] || v=$(sysconf nds.$1); echo "$v"; }
        PROF=$(pick power_profile); RND=$(pick renderer); RES=$(pick resolution3d); TF=$(pick texture_filter); RESUME=$(pick resume_on_quit)
        SHADER=$(pick shader)       # the menu's game settings (and ES's) write these per game
        case "$PROF" in
            performance) Q=1 QW=0 CMAX= ;;
            battery) Q=3 QW=20 CMAX=1416000 ;;
            *) Q=1 QW=20 CMAX=1416000 ;;
        esac
        export DSFLIP_QUEUE=${DSFLIP_QUEUE:-$Q} DSFLIP_QUEUE_WAIT=${DSFLIP_QUEUE_WAIT:-$QW}
        [ -n "$CMAX" ] && export DSFLIP_CPU_MAX=$CMAX DSFLIP_CPU_MAX_SOFT=1
        PANEL=; for m in /sys/class/drm/card*-DSI-*/modes; do read -r PANEL < "$m" 2>/dev/null && [ -n "$PANEL" ] && break; done
        [ "${PANEL%%x*}" -gt 640 ] 2>/dev/null && export DSFLIP_LATCH_MARGIN=${DSFLIP_LATCH_MARGIN:-3000}
        case "$RND" in drastic) ;; *) export DSFLIP_RAST=1 ;; esac
        case "$RES" in 3x|3) [ -n "$DSFLIP_RAST" ] && export DSFLIP_RAST_SCALE=3 ;; esac
        case "$TF" in bilinear) export DSFLIP_RAST_TEXFILTER=1 ;; sharp) export DSFLIP_RAST_TEXFILTER=2 ;; esac
        # resume on quit (on unless ES says off): the exit hotkey saves a resume state, then quits
        if [ "$RESUME" != 0 ]; then
            RSTATE="/storage/roms/savestates/nds/${GAME%.*}.resume.dss" DSV="$(dirname "$ROM")/${GAME%.*}.dsv"; RLOAD=0
            if [ -f "$RSTATE" ]; then if [ -f "$DSV" ] && [ "$DSV" -nt "$RSTATE" ]; then rm -f "$RSTATE"; else RLOAD=1; fi; fi
            export DSFLIP_RESUME_FILE="$RSTATE" DSFLIP_RESUME_LOAD=$RLOAD
            echo "-USR1 drastic" > /tmp/.process-kill-data
        else
            echo "-9 drastic" > /tmp/.process-kill-data
        fi
        echo "$(up) DraStic (libdsflip): profile ${PROF:-balanced}, renderer ${RND:-auto}, 3D ${RES:-2x}, screen ${SHADER:-none}, resume ${RESUME:-on}"
        # a shader draws every frame on the GPU: let it clock up (the menu's session left it at powersave)
        GPU=/sys/class/devfreq/fde60000.gpu
        GPU_MIN=$(cat $GPU/min_freq 2>/dev/null)
        case "${SHADER:-none}" in none|bilinear) ;; ds-fsr) echo performance > $GPU/governor 2>/dev/null ;;
            *) echo simple_ondemand > $GPU/governor 2>/dev/null; echo 400000000 > $GPU/min_freq 2>/dev/null ;; esac
        cd $DR
        rm -f /tmp/dsflip-state
        SDL_VIDEODRIVER=dummy XDG_RUNTIME_DIR=$RT DSFLIP_LOG=$DR/dsflip/dsflip.log DSHOOK_SHADER=${SHADER:-none} \
            LD_PRELOAD=$DR/dsflip/libdsflip.so $DR/dsflip/drastic "$ROM" > $DR/dsflip/drastic.out 2>&1 &
        P=$!
        # the battery at 3%: the exit hotkey's way out (with resume on, that saves a resume state first)
        if [ "$RESUME" != 0 ]; then rescue_watch kill -USR1 $P & else rescue_watch kill -TERM $P & fi
        W=$!
        trap 'kill -9 $P 2>/dev/null; kill $W 2>/dev/null' TERM INT
        # libdsflip says within a few seconds whether it has the panels ("ready"); without that, no black screens:
        # stop the game and go back to the menu
        v=; i=0
        while [ $i -lt 200 ] && kill -0 $P 2>/dev/null; do v=$(cat /tmp/dsflip-state 2>/dev/null); [ -n "$v" ] && break; sleep 0.05; i=$((i + 1)); done
        echo "$(up) libdsflip: ${v:-no verdict}"
        if [ "$v" != ready ]; then
            kill -9 $P 2>/dev/null
            err "This software couldn't be started." "DraStic couldn't take the screens (${v:-no answer}). What it said is in .config/drastic/dsflip/dsflip.log"
        fi
        wait $P; rc=$?
        kill $W 2>/dev/null
        [ -e /tmp/dsixl-error ] || check_end $rc DraStic .config/drastic/dsflip/drastic.out
        [ -n "$MAXF" ] && echo "$MAXF" > $CPU/scaling_max_freq 2>/dev/null
        [ -n "$GPU_MIN" ] && echo "$GPU_MIN" > $GPU/min_freq 2>/dev/null
        [ -n "$GOV" ] && echo "$GOV" > $CPU/scaling_governor 2>/dev/null
        echo "-9 drastic" > /tmp/.process-kill-data
        exit $rc
    fi
    echo "$(up) no libdsflip: ROCKNIX's DraStic under sway"
    sway_up
    echo "-9 drastic" > /tmp/.process-kill-data
    rescue_watch killall -TERM drastic & W=$!
    run_emu nds drastic drastic
    rc=$?
    kill $W 2>/dev/null
    check_end $rc DraStic /var/log/exec.log
    sway_down
    exit $rc
    ;;
GBA)
    GAME=$(basename "$ROM")
    # the game's own emulator (the menu's game settings), else config.ini's, else mGBA; RetroArch's other settings
    # (screen size, smoothing, resume, rewind, run-ahead) are read by ROCKNIX's launcher from system.cfg itself
    CORE=$(grep -F "gba[\"$GAME\"].core=" $CFG 2>/dev/null | tail -n1 | cut -d= -f2)
    [ -n "$CORE" ] || CORE=$(sysconf gba.core)       # the menu's Emulator settings (and ES's system setting)
    [ -n "$CORE" ] || CORE=$(conf gba_core); [ -n "$CORE" ] || CORE=mgba
    EMU=$(conf gba_emulator); [ -n "$EMU" ] || EMU=retroarch
    echo "$(up) RetroArch ($CORE)"
    sway_up
    echo "-9 retroarch" > /tmp/.process-kill-data
    # the battery at 3%: RetroArch saves (and its resume state, when that's on) on SIGTERM
    rescue_watch killall -TERM retroarch & W=$!
    run_emu gba "$CORE" "$EMU"
    rc=$?
    kill $W 2>/dev/null
    check_end $rc "RetroArch ($CORE)" /var/log/exec.log
    sway_down
    exit $rc
    ;;
esac
exit 1
