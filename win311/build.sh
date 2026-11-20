#!/bin/sh
# Build the PC emulator for the Audi RNS-E 192:   win311/build.sh
#
# The result is win311/out/: PCEMU.BIN, the emulator (Tiny386 on the
# SDK's drivers, win311/port), to be loaded at 0x8c010080 by
# win311/pcboot (WIN311.BIN on the card); pcemu.elf, the same for
# `rnse boot --gdb`; BIOS.ROM and VGABIOS.ROM, SeaBIOS and its VGA BIOS
# as Tiny386 configures them; and WIN311.BIN, pcboot.  All go on an SD
# card beside a disk image WIN311.IMG: docs/win311.md.
#
# Neither Tiny386 (BSD-3-Clause) nor SeaBIOS (LGPL-3) is kept in the
# repository: both are downloaded, pinned and checked against their
# SHA-256, into win311/work/.  The SH-4 GCC is sh4-linux-gnu-gcc when
# RNSE_WIN311_NATIVE=1, else the container of sdk/tools/sh4-docker; SeaBIOS
# is built in the container of win311/docker (an x86 GCC).
set -e
here=$(cd "$(dirname "$0")" && pwd)
# The kit: RNSE_SDK, or rnse-sdk beside this repository (passed on to the
# container, which sees the git working trees of both).
sdk=$(cd "${RNSE_SDK:-$here/../../rnse-sdk}" && pwd)
export RNSE_SDK="$sdk"
mounts() {
    for d in "$here" "$sdk"; do
        git -C "$d" rev-parse --show-toplevel 2>/dev/null || echo "$d"
    done | sort -u | while read -r m; do printf ' -v %s:%s' "$m" "$m"; done
}

t386_commit=c74cb710942461fa105370bbd2e79f201dcdb3ee
t386_sha256=373df2c42b6649093046e1ad9679964435dd2418f7c37c1b4167615a9d01439b
t386_url=https://github.com/hchunhui/tiny386/archive/$t386_commit.tar.gz
sb_commit=28aa46349b97ef0ebf742ae4fb9f9fad03578670
sb_sha256=1d9b266e416efa97ea29a991dab16f9d1825c616a6fa65c37329ad8665e5ff43
sb_url=https://github.com/coreboot/seabios/archive/$sb_commit.tar.gz

work=$here/work
out=$here/out
dl=$work/dl

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

if [ -z "$RNSE_WIN311_INSIDE" ]; then
    mkdir -p "$dl" "$out"
    fetch "$t386_url" "$dl/tiny386-$t386_commit.tar.gz" "$t386_sha256"
    fetch "$sb_url" "$dl/seabios-$sb_commit.tar.gz" "$sb_sha256"

    # Tiny386, unpacked and patched anew whenever the patches change
    # (win311/work/src; host.sh and be-test.sh build from it too).
    src=$work/src
    stamp=$(cat "$here"/patches/*.patch | (sha256sum 2>/dev/null || shasum -a 256) | cut -c1-16)
    if [ ! -f "$src/.rnse-$stamp" ]; then
        rm -rf "$src" "$work/tiny386-$t386_commit"
        tar -xzf "$dl/tiny386-$t386_commit.tar.gz" -C "$work"
        mv "$work/tiny386-$t386_commit" "$src"
        for p in "$here"/patches/*.patch; do
            patch -d "$src" -p1 -s < "$p"
        done
        touch "$src/.rnse-$stamp"
    fi

    # SeaBIOS with Tiny386's patch and configuration.
    sb=$work/seabios
    if [ ! -f "$sb/out/bios.bin" ]; then
        rm -rf "$sb" "$work/seabios-$sb_commit"
        tar -xzf "$dl/seabios-$sb_commit.tar.gz" -C "$work"
        mv "$work/seabios-$sb_commit" "$sb"
        patch -d "$sb" -p1 -s < "$src/seabios/patch"
        cp "$src/seabios/config" "$sb/.config"
        echo "$sb_commit" > "$sb/.version"
        docker image inspect rnse-seabios >/dev/null 2>&1 ||
            docker build -q -t rnse-seabios "$here/docker" >&2
        docker run --rm -v "$sb":/s -w /s rnse-seabios sh -c \
            'make CROSS_PREFIX=i686-linux-gnu- oldconfig >/dev/null && make CROSS_PREFIX=i686-linux-gnu- -j8 >/dev/null 2>&1'
    fi
    cp "$sb/out/bios.bin" "$out/BIOS.ROM"
    cp "$sb/out/vgabios.bin" "$out/VGABIOS.ROM"

    if [ -n "$RNSE_WIN311_NATIVE" ]; then
        RNSE_WIN311_INSIDE=1 sh "$here/build.sh"
        sh "$sdk/build.sh" 192 "$here/pcboot"
    else
        docker image inspect rnse-sh4-cross >/dev/null 2>&1 ||
            docker build -q -t rnse-sh4-cross "$sdk/tools/sh4-docker" >&2
        docker run --rm -e RNSE_WIN311_INSIDE=1 -e JOBS -e EXTRA_CFLAGS -e RNSE_SDK $(mounts) -w "$here" \
            rnse-sh4-cross sh "$here/build.sh"
        docker run --rm $(mounts) -w "$sdk" rnse-sh4-cross ./build.sh 192 "$here/pcboot" >/dev/null
    fi
    cp "$sdk/out/pcboot-192.bin" "$out/WIN311.BIN"

    # The trailer pcboot checks: "RNSEPCEM", the length before it and
    # its CRC-32 (gzip's own, from the end of its output), big-endian.
    bin=$out/PCEMU.BIN
    raw=$work/pcemu.raw
    n=$(wc -c < "$raw" | tr -d ' ')
    crc=$(gzip -c "$raw" | tail -c 8 | od -An -tu1 -N4 |
        awk 'NF { printf "%d", $1 + 256 * ($2 + 256 * ($3 + 256 * $4)) }')
    be32() {
        awk -v v="$1" 'BEGIN { for (i = 3; i >= 0; i--) printf "%c", int(v / 2 ^ (8 * i)) % 256 }'
    }
    { cat "$raw"; printf 'RNSEPCEM'; be32 "$n"; be32 "$crc"; } > "$bin.part"
    mv "$bin.part" "$bin"
    ls -l "$out"
    exit 0
fi

# ---- Inside: compile and link (sh4-linux-gnu-gcc on the PATH). ------------
: "${CROSS:=sh4-linux-gnu-}"
src=$work/src
obj=$work/obj
mkdir -p "$obj"
gccinc=$(${CROSS}gcc -print-file-name=include)
# The SDK's C library (sdk/libc) and the compiler's own headers, and
# nothing of the cross compiler's Linux C library.
common="-g -mb -m4-nofpu -ffreestanding -fno-pic -fno-stack-protector -fno-strict-aliasing -nostdinc -isystem $gccinc"
emu="$common ${EMU_OPT:--O2} -w -Werror=implicit-function-declaration -DRNSE -DUSE_FMOPL_n -I$sdk/libc -I$src $EXTRA_CFLAGS"
port="$common -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror=implicit-function-declaration -DRNSE -I$here/port -I$sdk/libc -I$src -I$sdk/include -I$here/pcboot $EXTRA_CFLAGS"
sdkflags="$common -Os -fno-builtin -Wall -Wextra -I$sdk/include $EXTRA_CFLAGS"

# Tiny386's machine without what is for other systems (term.c, win32.c,
# the SDL and ESP32 front ends) and without the x87 (fpu.c): Windows 3.11
# runs on a 486SX.
emu_files="ini i8259 i8254 ide vga vgafont i8042 misc adlib ne2000 i8257 sb16 pcspk pc pci i386"
sdk_files="crt0.S common.c libgcc.S cache.S panel.c display.c cd.c can.c sd.c pcm.c radio.c
 flash.c wayback.c enter.c hw192.c"
port_files="unit screen disk"

jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
list=$obj/commands
: > "$list"
objs=""
add() { # flags source object
    echo "${CROSS}gcc $1 -c -o $3 $2" >> "$list"
    objs="$objs $3"
}
for f in $emu_files; do add "$emu" "$src/$f.c" "$obj/e-$f.o"; done
for f in $sdk_files; do add "$sdkflags" "$sdk/src/$f" "$obj/s-${f%.*}.o"; done
for f in $port_files; do add "$port" "$here/port/$f.c" "$obj/p-$f.o"; done
for f in "$sdk"/libc/*.c; do add "$port" "$f" "$obj/l-$(basename "$f" .c).o"; done
xargs -P "$jobs" -I CMD sh -c 'CMD || exit 255' < "$list"

# Linked where pcboot puts it: the SDK's linker script, LOAD 0x8c010000,
# SIZE up to 0x8c400000 (handoff.h), below sdboot's session.
${CROSS}ld -EB --no-warn-rwx-segments -T "$sdk/link.ld" --defsym LOAD=0x8c010000 --defsym SIZE=0x3f0000 \
    -o "$out/pcemu.elf" $objs $(${CROSS}gcc -mb -m4-nofpu -print-libgcc-file-name)
${CROSS}objcopy -O binary "$out/pcemu.elf" "$work/pcemu.raw"
${CROSS}nm "$out/pcemu.elf" | grep -E ' (_start|_image_end|_bss_end|_stack_top)$'
