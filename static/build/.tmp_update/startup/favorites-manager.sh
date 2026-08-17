#!/bin/sh

# ==============================================================================
# purpose:
# apply the configured favorites manager rules during Onion startup.
#
# key behavior:
# - uses the processing engine already built into Tweaks.
# - remains silent during normal startup.
# - exits immediately when run-on-startup is disabled.
# ==============================================================================

/mnt/SDCARD/.tmp_update/bin/tweaks \
    --process_favorites \
    >/dev/null 2>&1
