#!/bin/sh
# The DSi Menu's session (systemd unit "dsixl", outside ES's process tree). Takes the panels from sway, runs the menu
# on them (KMS, both screens), launches what it asks for, and brings the menu back after every game. Leaving it
# (Options > Exit to EmulationStation, or the menu failing to start twice) gives the panels back to sway and ES.
# Modelled on ROCKNIXDS's sessions (dsflip/device/session.sh, the Store's).
D=/storage/.config/dsixl
LOG=$D/data/session.log
mkdir -p $D/data
[ -s $LOG ] && [ $(wc -c < $LOG) -gt 200000 ] && mv -f $LOG $LOG.1
up() { read u _ < /proc/uptime; echo "[$u]"; }
{
  echo "$(up) $(date) session start"
  kill -9 $(pidof gptokeyb) 2>/dev/null
  systemctl stop essway.service sway.service
  echo "$(up) ES and sway stopped"
  # The CPU draws the menu (a few ms a frame) and the display controller shows it: the GPU can idle.
  GPU=/sys/class/devfreq/fde60000.gpu
  if [ -f $GPU/governor ] && [ ! -f /tmp/dsixl-gpu-governor ]; then
    cat $GPU/governor > /tmp/dsixl-gpu-governor; echo powersave > $GPU/governor 2>/dev/null
  fi
  ARGS="$*"
  fails=0
  trap 'kill -TERM $P 2>/dev/null; sleep 0.5; kill -9 $P 2>/dev/null; echo "$(up) stopped by the unit"; exit 0' TERM INT
  while :; do
    # ROCKNIX's exit hotkey runs killall $(cat /tmp/.process-kill-data): in the menu it closes the menu, which
    # comes straight back (below)
    echo "-TERM dsixl" > /tmp/.process-kill-data
    cd $D
    T0=$(cut -d' ' -f1 /proc/uptime)
    XDG_RUNTIME_DIR=/var/run/0-runtime-dir DSIXL_HOME=$D ./dsixl --backend kms $ARGS >> $D/data/dsixl.out 2>&1 &
    P=$!
    wait $P; rc=$?
    echo "$(up) menu exited: $rc"
    case $rc in
      10)
        fails=0
        SYS=$(sed -n 1p /tmp/dsixl-launch); ROM=$(sed -n 2p /tmp/dsixl-launch); FILE=$(sed -n 3p /tmp/dsixl-launch)
        echo "$(up) launch $SYS: $ROM"
        [ -f /tmp/dsixl-gpu-governor ] && cat /tmp/dsixl-gpu-governor > $GPU/governor 2>/dev/null
        $D/launch.sh "$SYS" "$ROM"
        echo "$(up) game ended: $?"
        [ -f $GPU/governor ] && echo powersave > $GPU/governor 2>/dev/null
        ARGS="--return $SYS $FILE"
        ;;
      20) touch /tmp/dsixl-to-es; break ;;                       # to EmulationStation
      21) echo "$(up) power off"; $D/restore.sh; systemctl poweroff; exit 0 ;;
      22) echo "$(up) reboot"; $D/restore.sh; systemctl reboot; exit 0 ;;
      0|143) fails=0; ARGS="--no-boot" ;;                        # the exit hotkey, or closed: straight back
      *)
        # a crash, or no display (3): once more, then EmulationStation rather than black screens
        NOW=$(cut -d' ' -f1 /proc/uptime)
        if [ "$(echo "$NOW $T0" | awk '{ print ($1 - $2 < 15) }')" = 1 ]; then fails=$((fails + 1)); fi
        if [ $fails -ge 2 ]; then
          echo "$(up) the menu failed twice: EmulationStation instead"
          echo "The DSi Menu stopped unexpectedly (exit status $rc). Its log: $D/data/dsixl.log" > /tmp/dsflip-notice
          touch /tmp/dsixl-to-es; break
        fi
        sleep 0.5; ARGS="--no-boot" ;;
    esac
  done
  echo "-9 drastic" > /tmp/.process-kill-data
  $D/restore.sh
  echo "$(up) restored"
} >> $LOG 2>&1
