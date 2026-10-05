#!/vendor/bin/sh
# ADSP crash watchdog.
#
# When the ADSP crashes, remoteproc recovery removes the ALSA card and
# snd_card_disconnect_sync() waits until every open handle is closed. The audio
# HAL keeps the card open forever, so recovery (and audio) hangs until reboot.
# Restart the audio HAL and audioserver to drop their handles, and restart them
# again once the card is back so they reopen it.

rproc=
for r in /sys/class/remoteproc/remoteproc*; do
    [ "$(cat $r/name 2>/dev/null)" = adsp ] && rproc=$r
done
[ -n "$rproc" ] || exit 0

restarted=0
while true; do
    sleep 5
    state=$(cat $rproc/state 2>/dev/null)
    if [ "$state" = crashed ] && [ $restarted = 0 ]; then
        sleep 5
        [ "$(cat $rproc/state 2>/dev/null)" = crashed ] || continue
        log -t adspwatch "ADSP stuck in recovery, restarting audio HAL"
        setprop ctl.stop audioserver
        setprop ctl.restart vendor.audio-hal
        restarted=1
    elif [ $restarted = 1 ] && [ "$state" = running ] && \
         ! grep -q "no soundcards" /proc/asound/cards; then
        sleep 2
        log -t adspwatch "ADSP and sound card back, restarting audio"
        setprop ctl.restart vendor.audio-hal
        setprop ctl.start audioserver
        restarted=0
    fi
done
