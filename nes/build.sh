#!/bin/sh
# SH-4 SDK runtime plus a small payload for the existing SD loader.
# Nofrendo is compiled freestanding; only src/libgcc.S supplies arithmetic.
set -eu
cd "$(dirname "$0")"
CROSS=${CROSS:-sh4-linux-gnu-}
mkdir -p out/nes
objs=""
flags="-DSDK_SESSION_LOAD=0x8c100000u -mb -m4-nofpu -O2 -g -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -ffunction-sections -fdata-sections -Iinclude -Iexamples/nes/include -Ithird_party/nofrendo -Ithird_party/nofrendo/nes -Ithird_party/nofrendo/cpu -Ithird_party/nofrendo/sndhrdw -Ithird_party/nofrendo/libsnss ${EXTRA_CFLAGS:-}"
for src in src/crt0.S src/common.c src/heap.c src/libgcc.S src/cache.S src/panel.c src/display.c src/cd.c src/can.c src/sd.c src/pcm.c src/radio.c src/flash.c src/wayback.c src/hw192.c examples/nes/*.c third_party/nofrendo/bitmap.c third_party/nofrendo/nes/nes.c third_party/nofrendo/nes/nes_ppu.c third_party/nofrendo/nes/nes_mmc.c third_party/nofrendo/nes/nesinput.c third_party/nofrendo/cpu/nes6502.c third_party/nofrendo/sndhrdw/nes_apu.c third_party/nofrendo/mappers/map00[012347].c; do
 obj=out/nes/$(echo "$src" | tr / _).o
 ${CROSS}gcc $flags -c "$src" -o "$obj"
 objs="$objs $obj"
done
${CROSS}ld -EB --gc-sections -T link.ld --defsym LOAD=0x8c100000 --defsym SIZE=0x200000 -o out/nes/nes.elf $objs
${CROSS}nm out/nes/nes.elf > out/nes/nes.sym
${CROSS}objcopy -O binary out/nes/nes.elf out/nes/NESCORE.BIN
if [ "$(wc -c < out/nes/NESCORE.BIN)" -le 114688 ]; then dd if=/dev/zero of=out/nes/NESCORE.BIN bs=1 count=1 seek=114688 conv=notrunc 2>/dev/null; fi
./build.sh 192 nesboot
cp out/nesboot-192.bin out/nes/RNSE.BIN
