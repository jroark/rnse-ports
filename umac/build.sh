#!/bin/sh
# Build umac, a Mac Plus emulator, for the Audi RNS-E 192:   umac/build.sh
#
# The result is umac/out/: MACEMU.BIN, umac (Matt Evans's emulator
# around Musashi's 68000) on the SDK's drivers, to be loaded at
# 0x8c010080 by umac/macboot (MAC.BIN on the card), and
# macemu.elf, the same for `rnse boot --gdb`.  Both go on an SD card
# with MAC.BIN, a Mac Plus ROM (MACPLUS.ROM) and a disk image (MAC.DSK):
# docs/umac.md.
#
# The emulator's source is not kept in the repository: umac and its
# Musashi are downloaded at pinned commits, checked against their
# SHA-256, into umac/work/, and patched (umac/patches/).  The Mac's ROM
# and disks are Apple's and are never downloaded: bring your own.  The
# SH-4 GCC is sh4-linux-gnu-gcc when RNSE_UMAC_NATIVE=1, else the
# container of sdk/tools/sh4-docker; Musashi's generator is built with the
# host's cc (HOSTCC).
set -e
here=$(cd "$(dirname "$0")" && pwd)
# The kit: RNSE_SDK, or rnse's sdk/ beside ports/ (passed on to the
# container, which sees the git working trees of both).
sdk=$(cd "${RNSE_SDK:-$here/../../rnse-sdk}" && pwd)
export RNSE_SDK="$sdk"
mounts() {
    for d in "$here" "$sdk"; do
        git -C "$d" rev-parse --show-toplevel 2>/dev/null || echo "$d"
    done | sort -u | while read -r m; do printf ' -v %s:%s' "$m" "$m"; done
}

umac_commit=606f7f75fee8525d4735a0771bf521007115285e
umac_sha256=2904d80d75f734626d77ab4bc85fd81e9a4ffd02bf9813e643e2f5903d6ab642
umac_url=https://github.com/evansm7/umac/archive/$umac_commit.tar.gz
# umac's submodule external/Musashi at that commit.
musashi_commit=0b85a7a648e7b4d279b41e096c4d67ad354a82f7
musashi_sha256=1ba6587c376a4713b082dc0ce1e4d84cd481d249c0aea188e37d49d43ddb0d52
musashi_url=https://github.com/evansm7/Musashi/archive/$musashi_commit.tar.gz

# The Mac: 4 MiB of RAM, a screen of 384 x 235 (the ROM is patched for
# it; the width must be a multiple of 32), no disassembler.
macflags="-DUMAC_MEMSIZE=4096 -DDISP_WIDTH=384 -DDISP_HEIGHT=235 -DENABLE_DASM=0"

work=$here/work
out=$here/out
dl=$work/dl
src=$work/umac

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -c1-64
}
fetch() { # url file sha256
    if [ ! -f "$2" ]; then
        curl -fL -o "$2.part" "$1"
        mv "$2.part" "$2"
    fi
    [ "$(sha256 "$2")" = "$3" ] || { echo "$2: wrong SHA-256" >&2; exit 1; }
}

if [ -z "$RNSE_UMAC_INSIDE" ]; then
    mkdir -p "$dl" "$out"
    fetch "$umac_url" "$dl/umac-$umac_commit.tar.gz" "$umac_sha256"
    fetch "$musashi_url" "$dl/Musashi-$musashi_commit.tar.gz" "$musashi_sha256"

    # The source, unpacked and patched anew whenever the patches change,
    # and Musashi's opcode handlers generated from it.
    stamp=$(cat "$here"/patches/*.patch | (sha256sum 2>/dev/null || shasum -a 256) | cut -c1-16)
    if [ ! -f "$src/.rnse-$stamp" ]; then
        rm -rf "$src" "$work/umac-$umac_commit" "$work/Musashi-$musashi_commit"
        tar -xzf "$dl/umac-$umac_commit.tar.gz" -C "$work"
        tar -xzf "$dl/Musashi-$musashi_commit.tar.gz" -C "$work"
        mv "$work/umac-$umac_commit" "$src"
        rmdir "$src/external/Musashi"
        mv "$work/Musashi-$musashi_commit" "$src/external/Musashi"
        for p in "$here"/patches/*.patch; do
            patch -d "$src" -p1 -s < "$p"
        done
        # umac's configuration of Musashi in the place of Musashi's own
        # (umac's Makefile names it with -DMUSASHI_CNF, a quoted string
        # that the parallel build below would lose).
        cp "$src/include/m68kconf.h" "$src/external/Musashi/m68kconf.h"
        mkdir -p "$src/gen"
        ${HOSTCC:-cc} -O2 -o "$src/gen/m68kmake" "$src/external/Musashi/m68kmake.c"
        "$src/gen/m68kmake" "$src/gen" "$src/external/Musashi/m68k_in.c" >/dev/null
        touch "$src/.rnse-$stamp"
    fi

    if [ -n "$RNSE_UMAC_NATIVE" ]; then
        RNSE_UMAC_INSIDE=1 sh "$here/build.sh"
    else
        docker image inspect rnse-sh4-cross >/dev/null 2>&1 ||
            docker build -q -t rnse-sh4-cross "$sdk/tools/sh4-docker" >&2
        docker run --rm -e RNSE_UMAC_INSIDE=1 -e JOBS -e EXTRA_CFLAGS -e RNSE_SDK $(mounts) -w "$here" \
            rnse-sh4-cross sh "$here/build.sh"
    fi

    # The trailer macboot checks: "RNSEUMAC", the length before it and
    # its CRC-32 (gzip's own, from the end of its output), big-endian.
    bin=$out/MACEMU.BIN
    raw=$work/macemu.raw
    n=$(wc -c < "$raw" | tr -d ' ')
    crc=$(gzip -c "$raw" | tail -c 8 | od -An -tu1 -N4 |
        awk 'NF { printf "%d", $1 + 256 * ($2 + 256 * ($3 + 256 * $4)) }')
    be32() {
        awk -v v="$1" 'BEGIN { for (i = 3; i >= 0; i--) printf "%c", int(v / 2 ^ (8 * i)) % 256 }'
    }
    { cat "$raw"; printf 'RNSEUMAC'; be32 "$n"; be32 "$crc"; } > "$bin.part"
    mv "$bin.part" "$bin"
    ls -l "$out/MACEMU.BIN"
    exit 0
fi

# ---- Inside: compile and link (sh4-linux-gnu-gcc on the PATH). ------------
: "${CROSS:=sh4-linux-gnu-}"
obj=$work/obj
mkdir -p "$obj"
gccinc=$(${CROSS}gcc -print-file-name=include)
# The C library of sdk/libc and the compiler's own headers, and nothing
# of the cross compiler's Linux C library.
common="-g -mb -m4-nofpu -ffreestanding -fno-pic -fno-stack-protector -fno-strict-aliasing -nostdinc -isystem $gccinc"
inc="-I$sdk/libc -I$src/include -I$src/external/Musashi -I$src/gen $macflags"
engine="$common -O2 -w -Werror=implicit-function-declaration $inc $EXTRA_CFLAGS"
port="$common -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror=implicit-function-declaration $inc -I$here/port -I$sdk/include -I$here/macboot $EXTRA_CFLAGS"
libc="$common -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I$sdk/libc -I$sdk/include $EXTRA_CFLAGS"
sdkflags="$common -Os -fno-builtin -Wall -Wextra -I$sdk/include $EXTRA_CFLAGS"

sdk_files="crt0.S common.c libgcc.S cache.S panel.c display.c cd.c can.c sd.c pcm.c radio.c
 flash.c wayback.c enter.c hw192.c"

jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
list=$obj/commands
: > "$list"
objs=""
add() { # flags source object
    echo "${CROSS}gcc $1 -c -o $3 $2" >> "$list"
    objs="$objs $3"
}
# umac without its SDL front end (unix_main.c), and Musashi's core.
for f in main disc rom via scc; do add "$engine" "$src/src/$f.c" "$obj/u-$f.o"; done
add "$engine" "$src/external/Musashi/m68kcpu.c" "$obj/m-m68kcpu.o"
add "$engine" "$src/gen/m68kops.c" "$obj/m-m68kops.o"
for f in $sdk_files; do add "$sdkflags" "$sdk/src/$f" "$obj/s-${f%.*}.o"; done
for f in "$here"/port/*.c; do add "$port" "$f" "$obj/p-$(basename "$f" .c).o"; done
for f in "$sdk"/libc/*.c; do add "$libc" "$f" "$obj/c-$(basename "$f" .c).o"; done
# One compiler a line, several at once.
xargs -P "$jobs" -I CMD sh -c 'CMD || exit 255' < "$list"

# Linked where macboot puts it: the SDK's linker script, LOAD 0x8c010000,
# SIZE up to 0x8c7b0000 (handoff.h), below sdboot's session.
${CROSS}ld -EB --no-warn-rwx-segments -T "$sdk/link.ld" --defsym LOAD=0x8c010000 --defsym SIZE=0x7a0000 \
    -o "$out/macemu.elf" $objs
${CROSS}objcopy -O binary "$out/macemu.elf" "$work/macemu.raw"
${CROSS}nm "$out/macemu.elf" | grep -E ' (_start|_image_end|_bss_end|_stack_top)$'
