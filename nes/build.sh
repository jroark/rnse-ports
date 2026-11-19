#!/bin/sh
# SH-4 SDK runtime plus a small payload for the existing SD loader.
# Nofrendo is compiled freestanding; only src/libgcc.S supplies arithmetic.
# The result is in the kit's out/nes/: NESCORE.BIN and RNSE.BIN (nesboot).
#
#   sh nes/build.sh
#
# The kit is RNSE_SDK, or rnse's sdk/ beside ports/; the game (nes/) and
# nofrendo (nofrendo/) are beside this file.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
cd "${RNSE_SDK:-$here/../../sdk}"
CROSS=${CROSS:-sh4-linux-gnu-}
mkdir -p out/nes
objs=""
flags="-DSDK_SESSION_LOAD=0x8c100000u -mb -m4-nofpu -O2 -g -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -ffunction-sections -fdata-sections -Iinclude -I$here/nes/include -I$here/nofrendo -I$here/nofrendo/nes -I$here/nofrendo/cpu -I$here/nofrendo/sndhrdw -I$here/nofrendo/libsnss ${EXTRA_CFLAGS:-}"
for src in src/crt0.S src/common.c src/heap.c src/libgcc.S src/cache.S src/panel.c src/display.c src/cd.c src/can.c src/sd.c src/pcm.c src/radio.c src/flash.c src/wayback.c src/hw192.c $here/nes/*.c $here/nofrendo/bitmap.c $here/nofrendo/nes/nes.c $here/nofrendo/nes/nes_ppu.c $here/nofrendo/nes/nes_mmc.c $here/nofrendo/nes/nesinput.c $here/nofrendo/cpu/nes6502.c $here/nofrendo/sndhrdw/nes_apu.c $here/nofrendo/mappers/map00[012347].c; do
 obj=out/nes/$(echo "$src" | tr / _).o
 ${CROSS}gcc $flags -c "$src" -o "$obj"
 objs="$objs $obj"
done
${CROSS}ld -EB --gc-sections -T link.ld --defsym LOAD=0x8c100000 --defsym SIZE=0x200000 -o out/nes/nes.elf $objs
${CROSS}nm out/nes/nes.elf > out/nes/nes.sym
${CROSS}objcopy -O binary out/nes/nes.elf out/nes/NESCORE.BIN
if [ "$(wc -c < out/nes/NESCORE.BIN)" -le 114688 ]; then dd if=/dev/zero of=out/nes/NESCORE.BIN bs=1 count=1 seek=114688 conv=notrunc 2>/dev/null; fi
./build.sh 192 "$here/nesboot"
cp out/nesboot-192.bin out/nes/RNSE.BIN
