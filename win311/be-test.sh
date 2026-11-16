#!/bin/sh
# Run the PC emulator built for a big-endian CPU (MIPS under qemu-user,
# in docker) and for this machine on the same disk image, both in
# instruction-counted time, and compare their screens: the check that
# the port does not depend on the host's byte order (docs/win311.md).
#   win311/be-test.sh IMAGE [MS...]
set -e
here=$(cd "$(dirname "$0")" && pwd)
img=$1; shift
[ -n "$img" ] || { echo "usage: $0 IMAGE [MS...]" >&2; exit 2; }
times=${*:-3000 30000}
run=$here/work/betest
mkdir -p "$run"
for bios in bios.bin vgabios.bin; do cp "$here/work/run/$bios" "$run/"; done
for e in le be; do
    cp "$img" "$run/$e.IMG"
    printf '[pc]\nbios = %s/bios.bin\nvga_bios = %s/vgabios.bin\nmem_size = 16M\nvga_mem_size = 256K\nhda = %s/%s.IMG\n[display]\nwidth = 720\nheight = 480\n[cpu]\ngen = 4\nfpu = 0\n' \
        "$run" "$run" "$run" $e > "$run/$e.ini"
done
shots() { for t in $times; do printf -- '--shot %s:%s/%s-%s.png ' "$t" "$run" "$1" "$t"; done; }
sh "$here/host.sh" >/dev/null
"$here/work/host/pcemu-host" --virtual $(shots le) "$run/le.ini" >/dev/null 2>&1
docker image inspect rnse-be >/dev/null 2>&1 || docker build -q -t rnse-be "$here/docker/be" >&2
docker run --rm -v "$here":"$here" -w "$here" rnse-be sh -c \
    "CC='mips-linux-gnu-gcc -static' WIN311_OUT=work/host-be sh host.sh >/dev/null &&
     qemu-mips-static work/host-be/pcemu-host --virtual $(shots be) $run/be.ini >/dev/null 2>&1"
status=0
for t in $times; do
    if cmp -s "$run/le-$t.png" "$run/be-$t.png"; then echo "$t ms: same"; else echo "$t ms: DIFFERENT"; status=1; fi
done
exit $status
