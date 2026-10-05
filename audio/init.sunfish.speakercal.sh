#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Apply the factory speaker calibration to the CS35L41 protection firmware.
# /mnt/vendor/persist/audio/audio.cal holds one record of six little-endian
# u32 per amp (left, then right): cal_r, status, checksum, ambient, ...
# The DSP controls take big-endian words; writes made before the firmware
# runs are cached and applied when it starts.

CAL=/mnt/vendor/persist/audio/audio.cal
[ -r "$CAL" ] || exit 0

# The firmware controls only appear once the amps' DSPs have loaded
for i in $(seq 60); do
    /system/bin/tinymix2 -D 0 get "R DSP1 Protection cd CAL_R" >/dev/null 2>&1 && break
    sleep 1
done

set -- $(/system/bin/od -An -tu4 -v "$CAL")
[ $# -ge 12 ] || exit 0

setw() {
    /system/bin/tinymix2 -D 0 set "$1 DSP1 Protection cd $2" \
        $(printf "0x%02x 0x%02x 0x%02x 0x%02x" \
            $(($3 >> 24 & 255)) $(($3 >> 16 & 255)) $(($3 >> 8 & 255)) $(($3 & 255)))
}

apply() {
    # $1 amp prefix, $2 cal_r, $3 status, $4 checksum, $5 ambient
    [ "$3" = 1 ] || return
    setw "$1" CAL_R "$2"
    setw "$1" CAL_STATUS "$3"
    setw "$1" CAL_CHECKSUM "$4"
    setw "$1" CAL_AMBIENT "$5"
}

getw() {
    /system/bin/tinymix2 -D 0 get "$1 DSP1 Protection cd $2" 2>/dev/null
}

expect() {
    printf "%02x, %02x, %02x, %02x" $(($1 >> 24 & 255)) $(($1 >> 16 & 255)) $(($1 >> 8 & 255)) $(($1 & 255))
}

# Loading the firmware and its tuning resets the controls, which can happen
# after we first see them: write until both amps keep the values.
for i in $(seq 60); do
    apply L $1 $2 $3 $4
    apply R $7 $8 $9 ${10}
    sleep 1
    [ "$(getw L CAL_R)" = "$(expect $1)" ] && [ "$(getw R CAL_R)" = "$(expect $7)" ] && exit 0
done
exit 1
