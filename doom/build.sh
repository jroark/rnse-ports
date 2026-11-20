#!/bin/sh
# Build Doom for the Audi RNS-E 192:   doom/build.sh
#
# The result is doom/out/: DOOMGAME.BIN, the game (doomgeneric, a port
# of Chocolate Doom, on the SDK's drivers), to be loaded at 0x8c010080
# by doom/doomboot (DOOM.BIN on the card), doomgame.elf, the
# same for `rnse boot --gdb`, and DOOM1.WAD, the shareware IWAD.  All
# three go on an SD card beside DOOM.BIN: docs/doom.md.
#
# Neither the game's source (GPL-2.0) nor the WAD (shareware, id
# Software's) is kept in the repository: both are downloaded, pinned and
# checked against their SHA-256, into doom/work/.  DOOM_WAD=FILE takes a
# DOOM1.WAD from elsewhere (checked the same way).  The SH-4 GCC is
# sh4-linux-gnu-gcc when RNSE_DOOM_NATIVE=1, else the container of
# sdk/tools/sh4-docker.
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

dg_commit=e257a09527062bfb986b6e5e0780839b6cbd5b71
dg_sha256=26f8f60daa6c6e4d3c01e5eac9fb700d592dede803d13ac848154ff356b2a2e7
dg_url=https://github.com/ozkl/doomgeneric/archive/$dg_commit.tar.gz
# The shareware DOOM1.WAD of Doom 1.9 (MD5 f0cefca49926d00903cf57551d901abe).
wad_commit=9b384dc68add3eb2f5eb7754654cafeeaea5103b
wad_sha256=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771
wad_url=https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/$wad_commit/doom1.wad

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

if [ -z "$RNSE_DOOM_INSIDE" ]; then
    mkdir -p "$dl" "$out"
    fetch "$dg_url" "$dl/doomgeneric-$dg_commit.tar.gz" "$dg_sha256"
    if [ -n "$DOOM_WAD" ]; then
        [ "$(sha256 "$DOOM_WAD")" = "$wad_sha256" ] || { echo "$DOOM_WAD: not the shareware DOOM1.WAD 1.9" >&2; exit 1; }
        cp "$DOOM_WAD" "$dl/doom1.wad"
    fi
    fetch "$wad_url" "$dl/doom1.wad" "$wad_sha256"

    # The source, unpacked and patched anew whenever the patches change.
    src=$work/doomgeneric
    stamp=$(cat "$here"/patches/*.patch | (sha256sum 2>/dev/null || shasum -a 256) | cut -c1-16)
    if [ ! -f "$src/.rnse-$stamp" ]; then
        rm -rf "$src" "$work/doomgeneric-$dg_commit"
        tar -xzf "$dl/doomgeneric-$dg_commit.tar.gz" -C "$work"
        mv "$work/doomgeneric-$dg_commit/doomgeneric" "$src"
        rm -rf "$work/doomgeneric-$dg_commit"
        for p in "$here"/patches/*.patch; do
            patch -d "$src" -p1 -s < "$p"
        done
        touch "$src/.rnse-$stamp"
    fi

    if [ -n "$RNSE_DOOM_NATIVE" ]; then
        RNSE_DOOM_INSIDE=1 sh "$here/build.sh"
    else
        docker image inspect rnse-sh4-cross >/dev/null 2>&1 ||
            docker build -q -t rnse-sh4-cross "$sdk/tools/sh4-docker" >&2
        docker run --rm -e RNSE_DOOM_INSIDE=1 -e JOBS -e EXTRA_CFLAGS -e RNSE_SDK $(mounts) -w "$here" \
            rnse-sh4-cross sh "$here/build.sh"
    fi

    # The trailer doomboot checks: "RNSEDOOM", the length before it and
    # its CRC-32 (gzip's own, from the end of its output), big-endian.
    bin=$out/DOOMGAME.BIN
    objcopy_out=$work/doomgame.raw
    n=$(wc -c < "$objcopy_out" | tr -d ' ')
    crc=$(gzip -c "$objcopy_out" | tail -c 8 | od -An -tu1 -N4 |
        awk 'NF { printf "%d", $1 + 256 * ($2 + 256 * ($3 + 256 * $4)) }')
    be32() {
        awk -v v="$1" 'BEGIN { for (i = 3; i >= 0; i--) printf "%c", int(v / 2 ^ (8 * i)) % 256 }'
    }
    { cat "$objcopy_out"; printf 'RNSEDOOM'; be32 "$n"; be32 "$crc"; } > "$bin.part"
    mv "$bin.part" "$bin"
    cp "$dl/doom1.wad" "$out/DOOM1.WAD"
    ls -l "$out/DOOMGAME.BIN" "$out/DOOM1.WAD"
    exit 0
fi

# ---- Inside: compile and link (sh4-linux-gnu-gcc on the PATH). ------------
: "${CROSS:=sh4-linux-gnu-}"
src=$work/doomgeneric
obj=$work/obj
mkdir -p "$obj"
gccinc=$(${CROSS}gcc -print-file-name=include)
# The game's C library (sdk/libc) and the compiler's own headers,
# and nothing of the cross compiler's Linux C library.
common="-g -mb -m4-nofpu -ffreestanding -fno-pic -fno-stack-protector -fno-strict-aliasing -nostdinc -isystem $gccinc"
engine="$common -O2 -w -Werror=implicit-function-declaration -DFEATURE_SOUND -DRNSE -I$sdk/libc -I$src $EXTRA_CFLAGS"
port="$common -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror=implicit-function-declaration -DFEATURE_SOUND -DRNSE -I$here/port -I$sdk/libc -I$src -I$sdk/include -I$here/doomboot $EXTRA_CFLAGS"
sdkflags="$common -Os -fno-builtin -Wall -Wextra -I$sdk/include $EXTRA_CFLAGS"

# The engine: doomgeneric's list, without what is ours (doom/port) or for
# other systems.
engine_files="dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net
 f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_sound i_system i_timer
 memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors
 p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight
 p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things
 sha1 sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main
 w_wad z_zone"
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
for f in $engine_files; do add "$engine" "$src/$f.c" "$obj/e-$f.o"; done
for f in $sdk_files; do add "$sdkflags" "$sdk/src/$f" "$obj/s-${f%.*}.o"; done
for f in "$here"/port/*.c "$sdk"/libc/*.c; do
    add "$port" "$f" "$obj/p-$(basename "$f" .c).o"
done
# One compiler a line, several at once.
xargs -P "$jobs" -I CMD sh -c 'CMD || exit 255' < "$list"

# Linked where doomboot puts it: the SDK's linker script, LOAD 0x8c010000,
# SIZE up to 0x8c7b0000 (handoff.h), below sdboot's session.
${CROSS}ld -EB --no-warn-rwx-segments -T "$sdk/link.ld" --defsym LOAD=0x8c010000 --defsym SIZE=0x7a0000 \
    -o "$out/doomgame.elf" $objs
${CROSS}objcopy -O binary "$out/doomgame.elf" "$work/doomgame.raw"
${CROSS}nm "$out/doomgame.elf" | grep -E ' (_start|_image_end|_bss_end|_stack_top)$'
