#!/bin/sh
echo $0 $*
progdir=`dirname "$0"`
homedir=`dirname "$1"`

biosdir=/mnt/SDCARD/BIOS
swanconfig=
pspbios=

# Detect the filename using its stored spelling.
for entry in "$biosdir"/*; do
    [ -f "$entry" ] || continue

    case "$(basename "$entry")" in
        PSXONPSP660.bin)
            pspbios="$entry"
            break
            ;;
        psxonpsp660.bin)
            pspbios="$entry"
            ;;
    esac
done

# SwanStation expects the PSP BIOS as lowercase while Onion documents
# PSXONPSP660.bin. Provide both spellings in a temporary BIOS view.
if [ -n "$pspbios" ]; then
    swanbios=/tmp/swanstation-bios
    swanconfig=/tmp/swanstation-bios.cfg

    rm -rf "$swanbios"
    mkdir -p "$swanbios"

    for entry in "$biosdir"/* "$biosdir"/.[!.]*; do
        [ -e "$entry" ] || continue
        ln -s "$entry" "$swanbios/$(basename "$entry")"
    done

    ln -sf "$pspbios" "$swanbios/PSXONPSP660.bin"
    ln -sf "$pspbios" "$swanbios/psxonpsp660.bin"

    printf 'system_directory = "%s"\n' "$swanbios" > "$swanconfig"
fi

cd /mnt/SDCARD/RetroArch/

if [ -n "$swanconfig" ]; then
    HOME=/mnt/SDCARD/RetroArch/ \
        "$progdir/../../RetroArch/retroarch" \
        --appendconfig="$swanconfig" \
        -v \
        -L "$progdir/../../RetroArch/.retroarch/cores/km_duckswanstation_xtreme_amped_libretro.so" \
        "$1"
else
    HOME=/mnt/SDCARD/RetroArch/ \
        "$progdir/../../RetroArch/retroarch" \
        -v \
        -L "$progdir/../../RetroArch/.retroarch/cores/km_duckswanstation_xtreme_amped_libretro.so" \
        "$1"
fi
