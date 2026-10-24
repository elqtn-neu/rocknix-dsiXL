#!/bin/sh
# The DSi Menu on BananaOS: what BananaOS's shelf runs for the "DSi Menu" entry (banana/systems.conf's [ndsiware],
# a "game" as far as banana-ui is concerned). banana-ui has let go of the panels, the buttons and the sound; this
# runs the menu on both panels, starts what it asks for with BananaOS's own launchers (run-nds, run-retroarch, as
# its shelf would), and brings the menu back after each game. Options > Exit to BananaOS ends it: banana-ui's shelf.
# Sleep is bananad's: it pauses this whole session (the menu, or the game it started) and goes on with it after.
D=/data/dsixl
LOG=$D/data/session.log
mkdir -p $D/data
[ -s $LOG ] && [ $(wc -c < $LOG) -gt 200000 ] && mv -f $LOG $LOG.1
up() { read u _ < /proc/uptime; echo "[$u]"; }
export DSIXL_HOME=$D DSIXL_PLATFORM=banana DSIXL_NO_GRAB=1
export DSIXL_TOP=${BANANA_TOP:-DSI-2}
[ -n "$BANANA_TOUCH" ] && export DSIXL_TOUCH_BOTTOM=$BANANA_TOUCH
{
  echo "$(up) $(date) session start"
  banana-perf ui                        # (the menu draws on the CPU in a few ms: BananaOS's menu clocks)
  RET_SYS= RET_FILE= ARGS=
  fails=0
  while :; do
    cd $D
    T0=$(cut -d' ' -f1 /proc/uptime)
    if [ -n "$RET_FILE" ]; then set -- --return "$RET_SYS" "$RET_FILE"; else set --; fi
    ./dsixl --backend kms $ARGS "$@" >> $D/data/dsixl.out 2>&1
    rc=$?
    echo "$(up) menu exited: $rc"
    case $rc in
      10)
        fails=0
        SYS=$(sed -n 1p /tmp/dsixl-launch); ROM=$(sed -n 2p /tmp/dsixl-launch); FILE=$(sed -n 3p /tmp/dsixl-launch)
        echo "$(up) launch $SYS: $ROM"
        G0=$(cut -d' ' -f1 /proc/uptime)
        export BANANA_ROM="$ROM"
        case $SYS in
          NDS) export BANANA_SYSTEM=nds; banana-perf superdrastic; /usr/libexec/banana/run-nds "$ROM" ;;
          *)   export BANANA_SYSTEM=gba; banana-perf balanced
               CORE=$(sed -n 's/^ *core\.gba *= *//p' /data/banana/settings.conf 2>/dev/null | tail -n1)
               /usr/libexec/banana/run-retroarch "${CORE:-gpsp}" "$ROM" ;;
        esac
        echo "$(up) game ended: $?"
        banana-perf ui
        printf '%s\t%s\t%s\t%s\n' "$SYS" "$FILE" "$(awk -v a="$G0" -v b="$(cut -d' ' -f1 /proc/uptime)" 'BEGIN { printf "%d", b - a }')" "$(date +%s)" >> $D/data/plays.log
        ARGS= RET_SYS=$SYS RET_FILE=$FILE
        ;;
      20) echo "$(up) back to BananaOS"; break ;;
      21) echo "$(up) power off"; $D/brightness.sh apply; poweroff; exit 0 ;;
      22) echo "$(up) reboot"; $D/brightness.sh apply; reboot; exit 0 ;;
      0|143) fails=0; ARGS="--no-boot" RET_FILE= ;;
      *)
        # a crash, or no display (3): once more, then back to BananaOS rather than black screens
        $D/brightness.sh apply
        RET_FILE=
        NOW=$(cut -d' ' -f1 /proc/uptime)
        if [ "$(echo "$NOW $T0" | awk '{ print ($1 - $2 < 15) }')" = 1 ]; then fails=$((fails + 1)); fi
        if [ $fails -ge 2 ]; then echo "$(up) the menu failed twice: back to BananaOS"; exit 1; fi
        sleep 0.5; ARGS="--no-boot" ;;
    esac
  done
  $D/brightness.sh apply                # (the menu may have left the panels dimmed)
} >> $LOG 2>&1
exit 0
