#!/bin/sh
# The DVD player's tests (dvd/dvd): its decoders built for this
# computer and compared with ffmpeg, then the player itself on the
# emulated 192 with test discs.  Needs ffmpeg, dvdauthor and
# genisoimage (apt install ffmpeg dvdauthor genisoimage), and an SH-4
# GCC for the emulator part (see CLAUDE.md).  Everything it makes is
# synthetic (test patterns and tones) and goes to out/dvd (of ports/).
# The kit is RNSE_SDK, or rnse's sdk/ beside ports/.
#
#   sh dvd/check.sh          all of it
#   sh dvd/check.sh host     the decoders only
set -eu
cd "$(dirname "$0")/.."
sdk=$(cd "${RNSE_SDK:-../sdk}" && pwd)
o=out/dvd
mkdir -p "$o"
# (The DVD muxer complains of its buffer model at every packet: quiet.)
ff() { ffmpeg -hide_banner -loglevel fatal -y "$@"; }

# The material, once.
if [ ! -f "$o/ntsc.iso" ]; then
    # Interlaced, field prediction and field DCT, B pictures, the
    # alternate scan and the second table of coefficients, AC-3.
    ff -f lavfi -i testsrc2=size=720x480:rate=30000/1001 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 12 \
        -target ntsc-dvd -aspect 16:9 -flags +ilme+ildct -top 1 -bf 2 -g 15 -b:v 5000k -intra_vlc 1 -alternate_scan 1 \
        -acodec ac3 -ac 2 -b:a 192k "$o/ntsc.mpg"
    # Progressive, 5.1 AC-3 mixed down.
    ff -f lavfi -i testsrc2=size=720x480:rate=30000/1001 -f lavfi -i sine=frequency=550:sample_rate=48000 -t 6 \
        -target ntsc-dvd -acodec ac3 -ac 6 -b:a 384k "$o/surround.mpg"
    # LPCM.
    ff -f lavfi -i testsrc=size=720x480:rate=30000/1001 -f lavfi -i sine=frequency=660:sample_rate=48000 -t 4 \
        -target ntsc-dvd -acodec pcm_s16be -ac 2 "$o/lpcm.mpg"
    # PAL, the non-linear quantiser scale, MPEG-1 layer II.
    ff -f lavfi -i mandelbrot=size=720x576:rate=25 -f lavfi -i sine=frequency=330:sample_rate=48000 -t 5 \
        -target pal-dvd -non_linear_quant 1 -qmax 28 -acodec mp2 -b:a 224k "$o/pal.mpg"
    cat > "$o/ntsc.xml" <<EOF
<dvdauthor dest="$o/ntscdvd"><vmgm />
  <titleset><titles><video format="ntsc" aspect="16:9" widescreen="nopanscan"/>
    <pgc><vob file="$o/ntsc.mpg" chapters="0,0:04,0:08"/></pgc>
    <pgc><vob file="$o/surround.mpg" chapters="0,0:03"/></pgc>
  </titles></titleset>
  <titleset><titles><pgc><vob file="$o/lpcm.mpg"/></pgc></titles></titleset>
</dvdauthor>
EOF
    cat > "$o/pal.xml" <<EOF
<dvdauthor dest="$o/paldvd"><vmgm />
  <titleset><titles><video format="pal"/><pgc><vob file="$o/pal.mpg"/></pgc></titles></titleset>
</dvdauthor>
EOF
    rm -rf "$o/ntscdvd" "$o/paldvd"
    VIDEO_FORMAT=NTSC dvdauthor -x "$o/ntsc.xml" > /dev/null 2>&1
    VIDEO_FORMAT=PAL dvdauthor -x "$o/pal.xml" > /dev/null 2>&1
    genisoimage -quiet -dvd-video -udf -V NTSCTEST -o "$o/ntsc.iso" "$o/ntscdvd"
    genisoimage -quiet -dvd-video -udf -V PALTEST -o "$o/pal.iso" "$o/paldvd"
    # The same disc with its ISO 9660 volume descriptor spoilt: UDF only.
    python3 -c "
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
d[16 * 2048 + 1:16 * 2048 + 6] = b'XXXXX'
open(sys.argv[2], 'wb').write(d)" "$o/ntsc.iso" "$o/udf.iso"
    # Elementary streams and what ffmpeg decodes them to.
    for f in ntsc pal surround; do
        ff -i "$o/$f.mpg" -map 0:v -c copy -f mpeg2video "$o/$f.m2v"
        ff -i "$o/$f.m2v" -f rawvideo -pix_fmt yuv420p "$o/$f.yuv"
    done
    ff -f lavfi -i testsrc2=size=352x240:rate=30000/1001 -t 4 -c:v mpeg1video -b:v 1150k -bf 2 -f mpeg1video "$o/mpeg1.m1v"
    ff -i "$o/mpeg1.m1v" -f rawvideo -pix_fmt yuv420p "$o/mpeg1.yuv"
    ff -i "$o/ntsc.mpg" -map 0:a -c copy -f ac3 "$o/stereo.ac3"
    ff -i "$o/stereo.ac3" -f s16le "$o/stereo.ac3.s16"
    ff -i "$o/surround.mpg" -map 0:a -c copy -f ac3 "$o/surround.ac3"
    ff -i "$o/surround.ac3" -ac 2 -f s16le "$o/surround.ac3.s16"
    ff -f lavfi -i "anoisesrc=c=pink:r=48000:a=0.3,aformat=channel_layouts=stereo" -t 4 -c:a ac3 -b:a 96k \
        -channel_coupling 1 "$o/coupled.ac3"
    ff -i "$o/coupled.ac3" -f s16le "$o/coupled.ac3.s16"
    for b in 64 192; do
        ff -f lavfi -i "anoisesrc=c=pink:r=48000:a=0.3,aformat=channel_layouts=stereo" -t 2 -c:a mp2 -b:a ${b}k "$o/layer2-$b.mp2"
        ff -i "$o/layer2-$b.mp2" -f s16le "$o/layer2-$b.mp2.s16"
    done
fi

${CC:-cc} -std=gnu99 -O2 -Wall -Wextra -Werror -I"$sdk/include" -o "$o/dvd_host" dvd/tests/dvd_host.c \
    dvd/dvd/video.c dvd/dvd/ac3.c dvd/dvd/mpa.c dvd/dvd/disc.c \
    dvd/dvd/stream.c -lm
t="$o/dvd_host"
for f in ntsc pal surround; do
    $t video "$o/$f.m2v" "$o/$f.yuv" full
    $t video "$o/$f.m2v" "$o/$f.yuv"
done
$t video "$o/mpeg1.m1v" "$o/mpeg1.yuv"
$t audio ac3 "$o/stereo.ac3" "$o/stereo.ac3.s16"
$t audio ac3 "$o/surround.ac3" "$o/surround.ac3.s16" 40
# (ffmpeg dithers what has no bits, at random: the noise is not exact.)
$t audio ac3 "$o/coupled.ac3" "$o/coupled.ac3.s16" 25
$t audio mpa "$o/layer2-64.mp2" "$o/layer2-64.mp2.s16" 60
$t audio mpa "$o/layer2-192.mp2" "$o/layer2-192.mp2.s16" 60
$t disc "$o/ntsc.iso"
$t disc "$o/udf.iso"
$t disc "$o/pal.iso"
[ "${1:-}" = host ] && exit 0
RNSE_DVD="$PWD/$o/ntsc.iso" cargo test --release -p rnse-ports --test dvd
