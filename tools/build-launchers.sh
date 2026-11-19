#!/bin/sh
# Build the programs of this repository that are built with the kit alone
# (no downloads): the launchers, the DVD player and the NES emulator, for
# the units they are for.  The kit is RNSE_SDK, or the rnse-sdk checkout
# beside this repository; its build.sh puts them in its out/.
#
#     sh tools/build-launchers.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=$(CDPATH= cd -- "${RNSE_SDK:-$here/../rnse-sdk}" && pwd)
for program in doom/doomboot quake/quakeboot umac/macboot win311/pcboot nes/nesboot dvd/dvd; do
    for unit in 192 193; do
        sh "$sdk/build.sh" "$unit" "$here/$program" >/dev/null
        echo "built $program for the $unit"
    done
done
RNSE_SDK="$sdk" sh "$here/nes/build.sh" >/dev/null
echo "built nes (NESCORE.BIN, RNSE.BIN)"
