#!/bin/sh

# ==============================================================================
# purpose:
# launch tweaks and hand off safely to full-screen helper applications.
#
# behavior:
# - runs emusort only when tweaks explicitly requests it.
# - lets tweaks release sdl and audio resources before emusort starts.
# - returns to tweaks after emusort saves or cancels.
# ==============================================================================

cd /mnt/SDCARD/.tmp_update || exit 1

while true; do
    rm -f /tmp/launch_emusort

    ./bin/tweaks

    if [ ! -f /tmp/launch_emusort ]; then
        break
    fi

    rm -f /tmp/launch_emusort
    ./bin/emusort
done
