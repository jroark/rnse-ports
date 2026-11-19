# A DVD player for the 192

`sdk/examples/dvd` plays DVD-Video discs on a 192, bare metal: no
stock software underneath, everything from the disc's file system to
the sound written for the unit (no code from elsewhere).  It is started
from an SD card by the SD loader (`sdk/examples/sdboot`,
[sd-loader.md](sd-loader.md)), like any of the loader's programs.

```sh
sdk/build.sh 192 dvd
cp sdk/out/dvd-192.bin /Volumes/YOUR_CARD/DVD.BIN
```

With the loader's disc in the drive and the card in a slot, the loader
starts `DVD.BIN` (or offers it in its menu among the card's other
programs).  The player then asks for a DVD: CD EJECT takes the loader's
disc out (the display key opens the screen in front of the drive
first), and a DVD goes in.  The loader's disc is not needed again: the
loader waits in RAM for the player to return.

| key | in the list of titles | while a title plays |
|---|---|---|
| select knob, NEXT, PREV | choose a title | the chapter after or before (past the last: the title ends) |
| SELECT | play it | pause, go on |
| volume knob | | louder, quieter (the amplifier's volume) |
| SETUP | | the next sound track |
| INFO | | title, chapter, time, sound, frames a second, the drive's rate, the read-ahead |
| CAR | | the picture at 8 bits a pixel instead of 16, or back |
| RETURN | | back to the list |
| RETURN held 2 s | back to the SD loader | the same |
| CD EJECT | the disc out | the same |

## What it plays

* **Unencrypted DVD-Video**: discs made with dvdauthor, DVDStyler,
  ffmpeg's `-target ntsc-dvd`/`pal-dvd` and the like, and discs one
  has made from one's own recordings.
* **Not discs scrambled with CSS**, which almost every commercial disc
  is.  The player recognises them (the PES headers say so) and says so
  rather than showing garbage.  It has a place where a descrambler can
  be added, and none in it: see "Scrambled discs" below.
* Video: MPEG-2 main profile at main level (all of DVD: frame and field
  pictures, field and frame prediction, field DCT, both coefficient
  tables, both scans, the non-linear quantiser scale) and MPEG-1.  Dual
  prime prediction is read but predicted from the same-parity field
  only (an approximation; few encoders use it).
* Sound: AC-3 (stereo to 5.1, mixed down to two channels, dynamic range
  compression as the stream asks), MPEG-1 layer II (PAL discs) and
  16-bit LPCM.  DTS and 20- and 24-bit LPCM are recognised and played
  as silence.
* The disc: ISO 9660 or UDF (the DVD-Video file system; the ISO 9660
  bridge is optional), the titles of `VIDEO_TS.IFO`, the chapters and
  cells of each title set's program chains, the first angle of angle
  blocks.
* **Not**: the disc's menus (they are programs for a virtual machine
  of the DVD standard, with buttons drawn as subpictures), subtitles,
  angles other than the first.  The player shows the titles as a list
  instead.

## Scrambled discs

The repository has no CSS descrambler and will not have one: in the
United States (17 U.S.C. 1201), the EU (article 6 of directive
2001/29/EC) and elsewhere, distributing a tool that gets around CSS is
itself prohibited.  Where that is not the law, one can add a
descrambler of one's own without touching the player:
`sdk/examples/dvd/scramble.c` defines two weak functions, and a file in
that directory that defines them again takes their place (`build.sh`
compiles every `.c` file there).

* `int scramble_open(const struct disc_playlist *pl, u32 ifo)` is
  called before a title is played: its cells, as sectors of the disc,
  and the sector of its title set's IFO.  The drive can be talked to
  with `cd_command`.  Return 0 if the title's scrambled sectors can be
  handled.
* `int scramble_sector(u8 *sector)` gets each pack (2048 bytes,
  writable) whose PES packets are marked as scrambled, and has to leave
  it as it would have been unscrambled, with the marks cleared (bits 5
  and 4 of the byte after each packet's length).  Return 0 when done.

The player checks the marks again after `scramble_sector` and refuses
the title if any is left, as it does when `scramble_open` says no.
What such a file would have to do beyond the sectors themselves: a
drive of a DVD player refuses to read scrambled sectors until it has
been through the authentication of the standard (REPORT KEY and SEND
KEY), and whether this unit's drive, a navigation drive, does that, or
has a region set, has not been tried.  The player's reads retry a
refused sector three times and then pass over it.

`tests/dvd.rs` (`dvd_player_hands_scrambled_sectors_to_a_descrambler`)
checks the extension point with a stand-in built with `SCRAMBLE_TEST`,
which only clears the marks of a test disc whose data was never
scrambled: it shows that the hooks are called where they should be,
and descrambles nothing.

## How it works

Everything lives in RAM the stock software no longer needs (the 21 MB
below the stock loader's place that `sdk/examples/h264bench` uses);
the program itself is 75 KB, of the 112 KB a loader's program may have.

* **The drive** (`drive.c`): READ (10) of up to 32 sectors, the sectors
  taken by programmed I/O as the drive offers them, between the
  decoders' work, into a ring of 4 MB.  The kit's `cd_read` waits for
  each command to finish, which would leave the decoders standing.
* **The stream** (`stream.c`): the packs of the program stream, the PES
  packets in them, the video and one sound stream out of them with
  their time stamps.
* **Video** (`video.c`): pictures wider than 400 pixels are decoded at
  half their width, 720 to 360: each block's inverse transform from its
  four lower horizontal frequencies only (a 4x8 transform giving the
  values halfway between pixel pairs), motion compensation on the
  narrower frames with a quarter of a stored pixel for each half pixel
  of the coded picture.  The height is kept, so that fields stay exact.
  The screen has 400 pixels a line and 235 lines anyway, which is what
  makes this the right trade: about half the work, at the price of a
  little drift within each group of pictures (35 to 46 dB against
  ffmpeg's full-size decoding taken to half width, below).  At full
  width the decoder is ffmpeg's to within the inverse transform's
  rounding.
* **The picture** (`screen.c`): scaled to keep its shape (16:9 at
  400x225, 4:3 at 313x235, the panel's pixels taken as square), from
  the top field's lines (240 of a frame's 480, as many as the screen
  has, and without combing), through a table of 32 K colours to 16 bits
  a pixel (5-5-5), on one of two surfaces while the other is shown.
* **Sound** (`sound.c`): what the decoders give is made 44.1 kHz by
  linear interpolation and played by the kit's PCM program on card
  processor 0 (`sdk/src/pcm.c`), through the amplifier
  (`RADIO_DECODER`).  The processor holds two samples, so its DMA
  transfer must not stop: one transfer covers a ring of 47.5 s,
  written through P2 ahead of where it plays, and started again at the
  ring's end (the player checks before everything long it does, and
  waits for the end when it is less than 150 ms away).  The transfer's
  count says what has been heard: that is the player's clock.
* **Keeping time** (`main.c`): a picture is shown when the sound has
  reached its time stamp.  Late pictures make the decoder leave out B
  pictures (nothing refers to them) until it has caught up, and more
  than half a second late everything up to the next I picture.  Time
  stamps that jump (they start again in many a title's cells) are
  followed by the timer until the sound has passed the jump.
* The sound decoders use the FPU (`flags`: `-m4`, which with this GCC
  means FPSCR.PR set between functions; `main` sets FPSCR to
  0x000c0001 before anything else).  The rest of the program, and the
  kit, is built without it as always; nothing in an interrupt touches
  the FPU.

## How it was tested

`sh sdk/tests/check-dvd.sh` makes test discs with ffmpeg, dvdauthor and
genisoimage (test patterns and tones, NTSC and PAL, interlaced and
progressive, AC-3 stereo and 5.1, LPCM, layer II, a UDF-only copy) and
compares what the decoders, built for the computer, make of them with
what ffmpeg makes of them:

| stream | full width | half width (what the player does) |
|---|---|---|
| NTSC, interlaced, field prediction and DCT, B pictures | 67.4 dB (worst picture 42.2) | 46.0 dB (40.7) |
| PAL, non-linear quantiser | 62.8 dB (60.1) | 37.0 dB (34.3) |
| NTSC, progressive | 66.8 dB (60.9) | 44.8 dB (43.1) |
| MPEG-1, 352x240 | 67.3 dB (50.8) | |

The worst full-width pictures are B pictures in which ffmpeg predicts a
macroblock left out after one with field prediction from the top
field's vector alone; the standard's reference decoder, and this one,
use each field's own vector.  B pictures are no references, so the
difference stays in that picture.

| sound | signal to noise against ffmpeg |
|---|---|
| AC-3 stereo | 74.6 dB, gain 1.000 |
| AC-3 5.1 mixed down | 66.3 dB (gain 1.03: the two mix-downs are scaled differently) |
| AC-3 at 96 kbit/s with coupling, noise | 50 dB (both decoders dither what has no bits, at random) |
| layer II, 64 and 192 kbit/s, 32 to 48 kHz, joint stereo | 67 to 68 dB |

Then `tests/dvd.rs` runs the player on an emulated 192, started in the
loader's place by the boot ROM's stand-in (no firmware needed), with
the test disc in the drive and the caches modelled: the list of
titles, the first title played (the picture checked for colour, the
amplifier's output for the tone), INFO, CAR, NEXT, RETURN, and a copy
of the disc with its video marked as scrambled, which is refused, and
the same disc with the descrambler's stand-in built in, which plays.
`tests/disc.rs`, `dvd_player_from_the_sd_loader`, does it the way a
unit does it: stock software, SD loader, the player from a card, the
loader's disc ejected, the DVD put in, RETURN held back to the loader.
It needs the firmware (`RNSE_DISC`) and the test disc (`RNSE_DVD`) and
has not been run where this was written.

## How fast

In the emulator, which counts one instruction a cycle (on a unit,
with the caches set up as here, `h264bench` ran at 92 to 98 % of
that rate, `sdbridge/docs/android-auto.md`), with the drive answering at once:

| stream | pictures decoded a second | shown |
|---|---|---|
| 720x480, 29.97 a second, interlaced, 5 Mbit/s, test pattern | about 18 of 30 | 16 to 19, B pictures left out |
| 720x480, 23.976 a second, progressive, 4.5 Mbit/s, I-B-B-P | about 19 of 24 | 16 to 18, B pictures left out |

The sound plays without a break either way (the tone was checked for
gaps: none but the one a chapter jump makes).  Where the time goes
(`RNSE_DVD_PROFILE`, `tools/profile.py`): the coefficients a quarter,
motion compensation and the picture's conversion a sixth each, the
inverse transform a tenth, the sound a tenth.  What would help next:
decoding the top field alone (the screen shows nothing else), which
would halve motion compensation and the transforms; hand-written SH-4
code for the coefficient loop; the store queues for the writes to video
memory, which on a unit cost more than the emulator counts (95 ns an
uncached store, about 4 ms a picture).

**The drive is the open question.**  The 192's drive was measured
reading a CD-R at about 5.5 ms a sector, 370 KB/s (`docs/boot-rom.md`),
and that is what `--drive-times` gives the emulated drive.  At that
rate a DVD's 4 to 8 Mbit/s does not come off the disc in time (the
emulated player then shows 5 frames a second and waits for data).
How fast it reads a DVD has not been measured: a DVD drive reads a DVD
at 1.4 MB/s at single speed, which would be enough for any DVD.  INFO
shows the rate the drive gives (DISC ... KB/S) and how full the
read-ahead is: the first thing to look at on a unit.

## What is assumed about the hardware

* The 16-bit picture: the kit's layer (the stock loader's "BL" layer)
  switched to 16 bits a pixel by bit 31 of its mode register, with
  832 bytes a line.  The stock operating system shows 16-bit layers on
  this chip, but through the chip's extended mode; this one is the
  documented chip's behaviour and the emulator's, not something seen on
  a unit.  CAR switches to 8 bits a pixel through a 3-3-2 palette,
  which `h264bench` has shown on a unit.
* The panel's pixels are taken to be square.
* Sound through card processor 0 with the kit's PCM program, as
  `sdk/examples/pcmtest` does it, with one transfer of four million
  words and its source in P2.  The emulator's DMA controller takes the
  24-bit count and the address region bits as the SH7750R does.
* The drive's PIO transfers of 2048 bytes a block while a READ (10) of
  32 sectors runs, as the kit's `cd_command` does them one command at a
  time.

None of this has run on a unit yet.
