#!/bin/sh
# Build Quake for the Audi RNS-E 192:   quake/build.sh
#
# The result is quake/out/: QUAKEGAM.BIN, the game (id Software's
# WinQuake, its software renderer, on the SDK's drivers), to be loaded at
# 0x8c010080 by sdk/examples/quakeboot (QUAKE.BIN on the card),
# quakegame.elf, the same for `rnse boot --gdb`, and PAK0.PAK, the
# shareware data, which the game reads from the card as it needs it.
# All three go on an SD card beside QUAKE.BIN: docs/quake.md.
#
# Neither the game's source (GPL-2.0) nor the data (shareware, id
# Software's) is kept in the repository: both are downloaded, pinned and
# checked against their SHA-256, into quake/work/.  The data comes out
# of the shareware archive quake106.zip (its resource.1 is an LHA
# archive, which bsdtar reads); QUAKE_PAK=FILE takes a PAK0.PAK from
# elsewhere (checked the same way).  The SH-4 GCC is sh4-linux-gnu-gcc
# when RNSE_QUAKE_NATIVE=1, else the container of sdk/tools/sh4-docker.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")

q_commit=bf4ac424ce754894ac8f1dae6a3981954bc9852d
q_sha256=fb98e8c99740123f3fac830c1387584ec1f684db8908cbf42b7187d06d399013
q_url=https://github.com/id-Software/Quake/archive/$q_commit.tar.gz
# Shareware Quake 1.06; its PAK0.PAK has MD5 5906e5998fc3d896ddaf5e6a62e03abb.
sw_sha256=ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239
sw_url=https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake/quake106.zip
pak_sha256=35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af

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

if [ -z "$RNSE_QUAKE_INSIDE" ]; then
    mkdir -p "$dl" "$out"
    fetch "$q_url" "$dl/quake-$q_commit.tar.gz" "$q_sha256"
    if [ -n "$QUAKE_PAK" ]; then
        [ "$(sha256 "$QUAKE_PAK")" = "$pak_sha256" ] || { echo "$QUAKE_PAK: not the shareware PAK0.PAK 1.06" >&2; exit 1; }
        cp "$QUAKE_PAK" "$dl/pak0.pak"
    fi
    if [ ! -f "$dl/pak0.pak" ]; then
        fetch "$sw_url" "$dl/quake106.zip" "$sw_sha256"
        rm -rf "$work/sw"
        mkdir -p "$work/sw"
        (cd "$work/sw" && unzip -q "$dl/quake106.zip" resource.1 &&
            bsdtar -xf resource.1 ID1/PAK0.PAK) ||
            { echo "PAK0.PAK not unpacked (needs unzip and bsdtar); or give QUAKE_PAK=FILE" >&2; exit 1; }
        mv "$work/sw/ID1/PAK0.PAK" "$dl/pak0.pak"
        rm -rf "$work/sw"
    fi
    [ "$(sha256 "$dl/pak0.pak")" = "$pak_sha256" ] || { echo "$dl/pak0.pak: wrong SHA-256" >&2; exit 1; }

    # The source, unpacked and patched anew whenever the patches change.
    src=$work/WinQuake
    stamp=$(cat "$here"/patches/*.patch 2>/dev/null | (sha256sum 2>/dev/null || shasum -a 256) | cut -c1-16)
    if [ ! -f "$src/.rnse-$stamp" ]; then
        rm -rf "$src" "$work/Quake-$q_commit"
        tar -xzf "$dl/quake-$q_commit.tar.gz" -C "$work" "Quake-$q_commit/WinQuake"
        mv "$work/Quake-$q_commit/WinQuake" "$src"
        rm -rf "$work/Quake-$q_commit"
        # (The source has DOS line ends in places; the patches have none.)
        for f in "$src"/*.c "$src"/*.h; do
            tr -d '\r' < "$f" > "$f.lf" && mv "$f.lf" "$f"
        done
        for p in "$here"/patches/*.patch; do
            [ -f "$p" ] || continue
            patch -d "$src" -p1 -s < "$p"
        done
        touch "$src/.rnse-$stamp"
    fi

    if [ -n "$RNSE_QUAKE_NATIVE" ]; then
        RNSE_QUAKE_INSIDE=1 sh "$here/build.sh"
    else
        docker image inspect rnse-sh4-cross >/dev/null 2>&1 ||
            docker build -q -t rnse-sh4-cross "$root/sdk/tools/sh4-docker" >&2
        docker run --rm -e RNSE_QUAKE_INSIDE=1 -e JOBS -e EXTRA_CFLAGS -v "$root":"$root" -w "$root" \
            rnse-sh4-cross sh "$here/build.sh"
    fi

    # The trailer quakeboot checks: "RNSEQUAK", the length before it and
    # its CRC-32 (gzip's own, from the end of its output), big-endian.
    bin=$out/QUAKEGAM.BIN
    raw=$work/quakegame.raw
    n=$(wc -c < "$raw" | tr -d ' ')
    crc=$(gzip -c "$raw" | tail -c 8 | od -An -tu1 -N4 |
        awk 'NF { printf "%d", $1 + 256 * ($2 + 256 * ($3 + 256 * $4)) }')
    be32() {
        awk -v v="$1" 'BEGIN { for (i = 3; i >= 0; i--) printf "%c", int(v / 2 ^ (8 * i)) % 256 }'
    }
    { cat "$raw"; printf 'RNSEQUAK'; be32 "$n"; be32 "$crc"; } > "$bin.part"
    mv "$bin.part" "$bin"
    cp "$dl/pak0.pak" "$out/PAK0.PAK"
    ls -l "$out/QUAKEGAM.BIN" "$out/PAK0.PAK"
    exit 0
fi

# ---- Inside: compile and link (sh4-linux-gnu-gcc on the PATH). ------------
: "${CROSS:=sh4-linux-gnu-}"
src=$work/WinQuake
sdk=$root/sdk
obj=$work/obj
mkdir -p "$obj"
gccinc=$(${CROSS}gcc -print-file-name=include)
# The game's C library (quake/port/libc first, then the kit's, sdk/libc)
# and the compiler's own headers, and nothing of the cross compiler's
# Linux C library.  The game has the FPU (-m4: double precision between
# functions, single switched to where floats are used; this compiler
# knows no -m4-single-only), its unsuffixed constants single
# (-fsingle-precision-constant: `x * 0.5` stays a float sum, as on the
# PC), sqrt an instruction (-fno-math-errno); integer division keeps to
# the SDK's routines (-mdiv=call-table), which do not touch the FPU.
# The SDK's own files are built without the FPU, so that no interrupt
# handler uses its registers and none has to save them.
common="-g -mb -ffreestanding -fno-pic -fno-stack-protector -fno-strict-aliasing -nostdinc -isystem $gccinc"
fpu="-m4 -mdiv=call-table -fsingle-precision-constant -fno-math-errno -fcommon"
libc="-I$here/port/libc -I$root/sdk/libc"
engine="$common $fpu -O2 -w -Werror=implicit-function-declaration -DRNSE $libc -I$src $EXTRA_CFLAGS"
port="$common $fpu -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror=implicit-function-declaration -DRNSE -I$here/port $libc -I$src -I$sdk/include -I$sdk/examples/quakeboot $EXTRA_CFLAGS"
sdkflags="$common -m4-nofpu -Os -fno-builtin -Wall -Wextra -I$sdk/include $EXTRA_CFLAGS"

# The engine: WinQuake's software renderer as Makefile.linuxi386 lists it,
# in C only (nonintel.c for the assembly), without what is ours
# (quake/port) or for other systems; one player through the loopback.
engine_files="cl_demo cl_input cl_main cl_parse cl_tent chase cmd common console crc cvar draw
 d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky d_sprite d_surf d_vars d_zpoint
 host host_cmd keys menu mathlib model net_loop net_main net_none net_vcr nonintel pr_cmds pr_edict pr_exec
 r_aclip r_alias r_bsp r_light r_draw r_efrag r_edge r_misc r_main r_sky r_sprite r_surf r_part
 r_vars screen sbar sv_main sv_phys sv_move sv_user zone view wad world cd_null
 snd_dma snd_mem snd_mix"
sdk_files="crt0.S common.c libgcc.S cache.S panel.c display.c cd.c can.c sd.c pcm.c radio.c
 flash.c wayback.c enter.c hw192.c"
# The kit's C library (sdk/libc), whole; quake/port/libc adds to it.
libc_files="libc printf libgcc64"

jobs=${JOBS:-$(nproc 2>/dev/null || echo 4)}
list=$obj/commands
: > "$list"
objs=""
add() { # flags source object
    echo "${CROSS}gcc $1 -c -o $3 $2" >> "$list"
    objs="$objs $3"
}
for f in $engine_files; do add "$engine" "$src/$f.c" "$obj/e-$f.o"; done
for f in $sdk_files; do add "$sdkflags" "$sdk/src/$f" "$obj/s-${f%.*}.o"; done
for f in $libc_files; do add "$port" "$root/sdk/libc/$f.c" "$obj/d-$f.o"; done
for f in "$here"/port/*.c "$here"/port/libc/*.c "$here"/port/libc/*.S; do
    [ -f "$f" ] || continue
    b=$(basename "$f")
    add "$port" "$f" "$obj/p-${b%.*}.o"
done
# One compiler a line, several at once.
xargs -P "$jobs" -I CMD sh -c 'CMD || exit 255' < "$list"

# Linked where quakeboot puts it: the SDK's linker script, LOAD 0x8c010000,
# SIZE up to 0x8c7b0000 (handoff.h), below sdboot's session.
${CROSS}ld -EB --no-warn-rwx-segments -T "$sdk/link.ld" --defsym LOAD=0x8c010000 --defsym SIZE=0x7a0000 \
    -o "$out/quakegame.elf" $objs
${CROSS}objcopy -O binary "$out/quakegame.elf" "$work/quakegame.raw"
${CROSS}nm "$out/quakegame.elf" | grep -E ' (_start|_image_end|_bss_end|_stack_top)$'
