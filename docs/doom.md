# Doom on the 192

The shareware Doom (1.9), run on the 192 as a program of the SDK's,
started from an SD card by the SD loader (`sdboot`, [sd-loader.md](sd-loader.md)):
no Linux, no other operating system, the game on the kit's own drivers
for the screen, the faceplate, the display, the drive and the sound.
The game is [doomgeneric](https://github.com/ozkl/doomgeneric), a port
of Chocolate Doom with a small layer per platform; `doom/port/` is that
layer for the 192.

It runs on a unit (the North American 192 on the bench, from its
32 MB card); what was and was not seen there is at the end of this
page.

## Building

    PATH=$PWD/sdk/tools/sh4-docker:$PATH sdk/build.sh 192 doomboot   # sdk/out/doomboot-192.bin
    doom/build.sh                                               # doom/out/DOOMGAME.BIN, DOOM1.WAD

`doom/build.sh` downloads doomgeneric at a pinned commit and the
shareware `DOOM1.WAD` of Doom 1.9 (MD5 `f0cefca49926d00903cf57551d901abe`)
into `doom/work/dl`, checks both against their SHA-256, applies
`doom/patches/`, and builds in the container of `sdk/tools/sh4-docker`
(`RNSE_DOOM_NATIVE=1`: with an `sh4-linux-gnu-gcc` on the PATH
instead).  `DOOM_WAD=FILE` takes the WAD from a file instead of the
download; it has to be the same file.  Out come `doom/out/DOOMGAME.BIN`,
`doom/out/doomgame.elf` (for `rnse boot --gdb`) and `doom/out/DOOM1.WAD`.

Neither the game's source nor the WAD is in the repository, and neither
may be: doomgeneric is GPL-2.0 (so is `DOOMGAME.BIN`, which is built
from it; whoever hands the binary on owes its source, which is the
pinned commit plus `doom/patches/` and `doom/port/`), and the WAD is id
Software's, freely given away as shareware but not ours to put in a
repository.  `.gitignore` keeps `doom/work/`, `doom/out/` and `*.wad`
out.

What is ours (MIT, as the rest):

| file | what |
|------|------|
| `doom/doomboot/` | the first stage, `DOOM.BIN`, and `handoff.h`, what it leaves the game |
| `doom/port/main.c` | the start, the waits (`port_idle`), the clock, the end |
| `doom/port/i_video.c` | the picture and the palette |
| `doom/port/i_input.c` | the faceplate as keyboard and mouse |
| `doom/port/sound.c` | the sound effects (no music) |
| `doom/port/w_file.c` | the WAD, from RAM |
| `sdk/libc/` | the C library the game wants (shared with umac, [umac.md](umac.md)): strings, `printf` and `scanf`, `malloc` with `free`, files in RAM, the 64-bit division the compiler calls |
| `doom/patches/0001-rnse.patch` | doomgeneric without floating point (a frame rate in tenths, no float settings, no mouse-speed box) and without SDL |
| `tests/disc.rs` `doom_runs_from_an_sd_card` | the end-to-end test |

## The card

    DOOM.BIN        sdk/out/doomboot-192.bin
    DOOMGAME.BIN    doom/out/DOOMGAME.BIN
    DOOM1.WAD       doom/out/DOOM1.WAD
    DOOM.ARG        (optional) more arguments for the game, one line

on a FAT card beside `sdboot` (as `RNSE.BIN`, or on the loader disc),
and whatever else is there.  `sdboot` lists `DOOM.BIN` in its menu, or
runs it at once if it is the only program; `DOOMGAME.BIN` is too big
to be a program of its own and is left out of the menu, as `VMLINUX.BIN`
is.  `DOOM.ARG` is read as Doom's command line: `-timedemo demo1`
plays the first demo as fast as it can and says the frames a second;
`-dsp 1` takes the other card processor for the sound; `-warp 1 3`,
`-skill 4` and the rest of Doom's own work too.

In the emulator, with the stock software doing the starting as on a unit:

    rnse custom stock.iso V222 sdk/out/sdboot-192.bin sdboot.iso --index 0380
    mkdir card && cp sdk/out/doomboot-192.bin card/DOOM.BIN && cp doom/out/DOOMGAME.BIN doom/out/DOOM1.WAD card/
    rnse card card.img card
    rnse boot sdboot.iso --variant V222 --ignition --installed-index 0370 --sd-card card.img \
        --cycles 7600000000 --shot 21000:title.png --key SETUP@22000 --key SELECT@23000 \
        --key SELECT@24000 --key SELECT@25000 --key NEXT@27000+1500 --knob 1:4@29000 --shot 30000:e1m1.png

The game's messages come out on the debug serial port (the emulator
prints them). The end-to-end test:

    PATH=$PWD/sdk/tools/sh4-docker:$PATH RNSE_DISC=stock.iso RNSE_DOOM=$PWD/doom/out \
        cargo test --release -p rnse-ports --test disc doom_runs_from_an_sd_card

## Two stages

`sdboot` loads a program of at most 0x1c000 bytes into the loader's
place (0x8df00000).  The game is 368 KB of code and data, and wants its
4 MB WAD in memory.  So `DOOM.BIN` (`doom/doomboot`, 28 KB) is a
first stage, as `linuxboot` is for Linux:

1. It takes the faceplate controller over as `sdboot` left it, lit
   (`panel_attach`), and answers it while it reads the card (`sd_idle`
   is `panel_service`): the display key, CD EJECT and RETURN held work
   all the while.
2. It reads `DOOMGAME.BIN` to 0x8c010080 and checks it: the trailer
   `doom/build.sh` puts at its end (`RNSEDOOM`, its length, its CRC-32),
   and the SDK's mark at the image's +0x10.  Then `DOOM1.WAD` to
   0x8c800000, which has to begin with `IWAD`, and `DOOM.ARG` if there
   is one.  At the card processor's pace this is some 15 s on a unit
   (293 KB/s, measured with other programs); 9 s in the emulator.
3. It leaves the game a block at 0x8c00f000 (`handoff.h`): a mark, where
   the WAD is and how long, its own `loader_return`, the slot, the
   arguments.
4. It hands the processor over with `image_enter` (`sdk/src/enter.c`,
   shared with `linuxboot`): interrupts masked, timers, DMA and every
   interrupt source stopped, the operand cache written back over the
   game's room, the instruction cache invalidated, the jump.

`doomboot` stays where it is.  The way back to `sdboot` goes through it:
`loader_return` in the game (RETURN held, or the game's end) calls
`doomboot`'s, through `loader_return_via` (`sdk/src/wayback.c`), since
only a program linked at the loader's place can be in `sdboot`'s
session.  `sdboot` then says `TEST ENDED BY HOLDING RETURN` (or the
status) and waits, as after any program.

Something missing or spoilt on the card is said on the screen
(`DOOMGAME.BIN MISSING OR LONG`, `... IS SPOILT`, `DOOM1.WAD IS NO
IWAD`, with an error number), and after four seconds it is back to
`sdboot`.

## Memory

| from | to | what |
|------|----|------|
| 0x8c00f000 | | the hand-off block (`handoff.h`) |
| 0x8c010080 | 0x8c7b0000 | `DOOMGAME.BIN` (`sdk/link.ld` with LOAD 0x8c010000, SIZE 0x7a0000), its bss, its stack at the top; it ends at about 0x8c0ad000 |
| 0x8c7c0000 | 0x8c800000 | `sdboot`'s session, untouched |
| 0x8c800000 | 0x8cd00000 | the WAD (4,196,020 bytes; room for 5 MB) |
| 0x8cd00000 | 0x8de00000 | the heap: Doom's zone (12 MB, `-mb 12`) and the C library's `malloc` |
| 0x8df00000 | 0x8df20000 | `doomboot`, for the way back |

The WAD is read like a file (`w_file.c`), lumps copied into the zone:
not used in place, since a lump in the WAD need not start on an address
the SH-4 can read a word from, and Doom reads words from lumps.

Files the game writes (the settings, `.default.cfg`; saved games) are
kept in RAM (`sdk/libc/libc.c`) until the unit is switched off:
saving and loading work within one run.

## The picture

Doom draws 320x200 at 8 bits through a palette of 256 colours, which is
what the 192's screen is: the Coral chip's layer L4 as the kit sets it
up, 400x235, 8 bits, a palette of 256 entries of 6 bits a colour (as a
VGA's).  So each frame goes over as it is, centred (at 40, 17), a word
at a time (`screen_blit`, new in the kit: four pixels a word with their
bytes turned round, since the chip is little-endian on a big-endian bus,
`docs/linux.md`), and Doom's palette, with its gamma, straight into the
chip's.  One surface: no double buffering, and nothing waits for the
display's vertical sync.

## The controls

| control | in the game | in a menu |
|---------|-------------|-----------|
| select knob | turn, 10° a detent (a mouse's x) | up, down |
| SELECT (pressing the knob) | fire | Enter; "yes" to a question |
| NEXT, PREV, held | forward, back | right, left (sliders) |
| top left, top right soft key | use (doors, switches), the map | |
| bottom left, bottom right soft key | strafe left, right | |
| NAV, TEL, NAME, MEDIA, RADIO | weapons 1 to 5 | |
| INFO | the next weapon | |
| CAR | pause | |
| SETUP | the menu | back |
| RETURN, short | the menu | back |
| volume knob | the amplifier's volume (0 to 30) | the same |
| EJECT (by the power button) | the display folds down and up | the same |
| CD EJECT | the disc comes out | the same |
| RETURN held 2 s | back to `sdboot` | the same |

Running is always on: there are not keys enough to hold one for it.
The last three are the kit's (`panel_service`, called in every wait of
the game and after every frame, `port_idle`), and mean what they mean
in every program (CLAUDE.md, "Mandatory hardware behavior").

A key pressed and let go while the game does not look at the keys (a
screen wipe, a level loading) still counts: the kit latches presses as
the controller reports them (`panel_pressed`, new in the kit), and the
game gives such a key one tic down.  Without that, a SETUP pressed
for 200 ms as the demo changed screens was lost in the emulator: the
screen wipe draws its frames without reading the keys.

Whether the faceplate controller reports two keys at once (NEXT and
SELECT: moving and firing) is to be seen on a unit; its report is a
bitmap of all the keys, and the emulator sends two at once.

## Sound

The effects are the WAD's DMX lumps, 8-bit samples at 11025 Hz.  Up to
eight play at once, each stepped to 44.1 kHz, scaled by Doom's volume
and stereo separation and mixed (`sound.c`).  The mix goes in blocks of
512 samples into a ring of eight, which the DMA controller plays out of
a card processor's converter one block after the other: `pcm_stream`,
new in the kit's `sdk/src/pcm.c`.  It starts each transfer from the end
of the last one's interrupt (DMTE7 for processor 0, DMTE3 for 1, level
9), and plays a short silence when the next block is not ready, so a
slow frame is not a stutter.  The game mixes in its waits and after
each frame, which leaves 93 ms of slack.  The amplifier takes the card
processors' converter as its source (`radio_source(RADIO_DECODER, …)`),
and the radio processor's keep-alive goes out once a minute
(`panel_service`).

The card processor is the first by default; `-dsp 1` in `DOOM.ARG`
takes the other.  Which of the two reaches the amplifier on a unit is
still to be heard (`sdk/examples/pcmtest`).  The card is not needed
after the start, so the processor of its slot can play.

No music: the WAD's MUS songs want a synthesizer (Chocolate Doom has
an OPL emulator), which is a step of its own.

## Speed and the caches

The game sets the caches as measured fastest on a unit for this kind of
code (`sdbridge/docs/android-auto.md`): P1, where all of it lives, copy-back
instead of write-through, and no index modes (CCR 0x80000105, through
`cache_setup`).  It puts back what it found before every hand-back to
the loader.  With that setting a unit ran a decoder at 92 to 98 % of
the pace the emulator's instruction count gives.

`-timedemo demo1` in the emulator: 5026 tics in 1465, **120 frames a
second** at one instruction a cycle (the game itself is held to 35).
So a unit should have room to spare, unless the frame's copy to the
graphics chip (64000 bytes over the bus each frame) is slower there
than here.

## What it is and is not, so far

Seen in the emulator, with the caches modelled and RAM arbitrary
(`doom_runs_from_an_sd_card`): from the stock software's start through
`sdboot` and `doomboot` to the game; the display folding down and up
while the card is read; the title and the first demo, moving; sound
effects out of the first card processor's converter; SETUP, then
SELECT three times, starting E1M1; NEXT and the knob moving the view;
the display and the disc while it plays; a short RETURN staying in the
game and RETURN held going back to `sdboot`; the watchdog served
throughout (its longest pause 16 ms; the faceplate not answered for at
most 90 ms, at the game's start, and 20 ms while it plays).

On a unit (the bench's V222, the SD loader disc, the card with the
three files beside the other programs): `DOOM` from `sdboot`'s menu
loads and the game runs.

The first build did not: the unit restarted into the stock software at
the end of the load.  The emulator did not show it, also not with
`--strict` and the CPU at half speed.  What changed for the build that
worked is that `image_enter` strobes the watchdog while it writes the
operand cache back over the game's room (7.6 MB, 245,000 `ocbp` with
interrupts masked): an unstrobed purge of RAM had already reset this
unit once, on Linux's way back (`docs/linux.md`), and the emulator
counts an `ocbp` as one cycle, which a unit evidently does not.  The
same build also has the kit's exception screen
(`unexpected`, `sdk/src/common.c`: the exception's code and PC on the
screen until the watchdog resets the unit into its own software,
which keeps eject working; holding the screen up for half a minute by
serving the watchdog from it was tried and taken out again, since it
left eject and the display key dead for as long), the game's
`DOOM IS STARTING` at its first instruction, and `pcm.c` reading a DMA
channel's control register before it clears the end flag, as the SH-4
manual wants.  `-DRNSE_TEST_FAULT` in `EXTRA_CFLAGS` builds a game that
takes an address error at its start, to see that screen.

Not reported from the unit yet: whether the sound is heard (and from
which card processor), two keys held at once, the frame rate.  What the
emulator cannot vouch for there:

- the speed (above), the copy to the graphics chip, tearing without a
  wait for the vertical sync;
- which card processor's converter is heard, and the DMA controller
  paced by the processor's pin with an interrupt per block (the
  emulator runs the processor ahead in bursts);
- two faceplate keys held at once;
- 15 s of reading the card before anything moves.
