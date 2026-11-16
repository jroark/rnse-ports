#!/bin/sh
# The PC emulator for the Mac or Linux (win311/port/host.c), from the same
# patched Tiny386 as the unit's: win311/work/host/pcemu-host.  For quick
# runs of a disk image and for checking the port (docs/win311.md).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=${WIN311_SRC:-$here/work/src}
out=${WIN311_OUT:+$here/$WIN311_OUT}; out=${out:-$here/work/host}
mkdir -p "$out"
files="ini.c i8259.c i8254.c ide.c vga.c vgafont.c i8042.c misc.c adlib.c ne2000.c i8257.c sb16.c pcspk.c pc.c pci.c i386.c fpu.c"
set --
for f in $files; do set -- "$@" "$src/$f"; done
${CC:-cc} -O2 -g -w -I"$src" -I"$here/port" -DI386_ENABLE_FPU $EXTRA_CFLAGS \
    -DRNSE "$here/port/host.c" "$here/port/screen.c" "$here/port/disk.c" "$@" -lm -o "$out/pcemu-host"
echo "$out/pcemu-host"
