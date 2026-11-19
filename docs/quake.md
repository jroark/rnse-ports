# Quake on the 192

The shareware Quake (1.06 data, the 1.09 engine), run on the 192 as a
program of the SDK's, started from an SD card by the SD loader
(`sdboot`, [sd-loader.md](sd-loader.md)), the way Doom is
([doom.md](doom.md)): no operating system, the game on the kit's own
drivers for the screen, the faceplate, the display, the card and the
sound.  The engine is id Software's GPL release of WinQuake (its
software renderer, in C); `quake/port/` is the layer that the PC's
`sys_`, `vid_`, `in_` and `snd_` files were, for the 192.

Unlike Doom's WAD, the game's data (`PAK0.PAK`, 18 MB) is not loaded
into RAM first: the game reads it from the card as it needs it, a few
MB at a map's start, through a new pair of calls of the kit's
(`sd_open`, `sd_pread`).

So far it has run in the emulator only; what that does and does not
vouch for is at the end of this page.

## Building

    PATH=$PWD/sdk/tools/sh4-docker:$PATH sdk/build.sh 192 quakeboot   # sdk/out/quakeboot-192.bin
    quake/build.sh                                               # quake/out/QUAKEGAM.BIN, PAK0.PAK

`quake/build.sh` downloads id's source (github.com/id-Software/Quake)
at a pinned commit and the shareware archive `quake106.zip`, checks both
against their SHA-256, takes `PAK0.PAK` out of the archive's
`resource.1` (an LHA archive, which `bsdtar` reads; macOS has it), checks
that against its SHA-256 as well (MD5 `5906e5998fc3d896ddaf5e6a62e03abb`,
18,689,235 bytes), applies `quake/patches/` and builds in the container
of `sdk/tools/sh4-docker` (`RNSE_QUAKE_NATIVE=1`: with an
`sh4-linux-gnu-gcc` on the PATH instead).  `QUAKE_PAK=FILE` takes the
PAK from a file instead; it has to be the same file.  Out come
`quake/out/QUAKEGAM.BIN`, `quake/out/quakegame.elf` (for
`rnse boot --gdb`) and `quake/out/PAK0.PAK`.

Neither the source nor the data is in the repository, and neither may
be: the engine is GPL-2.0 (so is `QUAKEGAM.BIN`; whoever hands the
binary on owes its source, which is the pinned commit plus
`quake/patches/`, `quake/port/` and the parts of `sdk/libc` and
the kit that are linked in), and the data is id Software's shareware.
`.gitignore` keeps `quake/work/`, `quake/out/` and `*.pak` out.

What is ours (MIT, as the rest):

| file | what |
|------|------|
| `sdk/examples/quakeboot/` | the first stage, `QUAKE.BIN`, and `handoff.h`, what it leaves the game |
| `quake/port/sys_rnse.c` | the start, the clock, the files (`PAK0.PAK` from the card), the waits (`port_idle`), errors and the end |
| `quake/port/vid_rnse.c` | the picture and the palette |
| `quake/port/in_rnse.c` | the faceplate as keyboard and mouse |
| `quake/port/snd_rnse.c` | the sound card: a card processor, fed by DMA from Quake's ring |
| `quake/port/libc/` | what Quake wants beyond the kit's C library (`sdk/libc`, shared with Doom): `math.h` and its functions, `setjmp`/`longjmp`, `rand`, the POSIX calls of the console log |
| `quake/patches/0001-com-filebase.patch` | a bug of the engine's that the PC never showed (below) |
| `tests/disc.rs` `quake_runs_from_an_sd_card` | the end-to-end test |

Shared with Doom and changed for Quake: `sdk/libc/printf.c` knows
`%f`, `%g` and `%e` (as `%f`) when built with the FPU, so Doom's build is
as it was; `sdk/libc/libc.c` has files whose bytes come from a
function of the program's (`files_add_reader`: the PAK, for the demos,
which Quake reads with `fopen`).

### The compiler and the FPU

Quake is floating point throughout; the SH-4 has an FPU.  The game is
built with `-m4` (the cross compiler at hand knows no
`-m4-single-only`): double precision between functions, the compiler
switching FPSCR's PR bit to single where floats are used, which costs
a few instructions at the start and end of such a function.
`-fsingle-precision-constant` keeps `x * 0.5` a single-precision sum as
on the PC, and `-fno-math-errno` makes `sqrt` the FPU's own
instruction.  The game sets FPSCR to 0x000c0000 at its start (PR,
denormals as zero, rounding to the nearest: the processor's reset
value rounds towards zero).  Integer division stays with the kit's
routines (`-mdiv=call-table`, `sdk/src/libgcc.S`), and the kit's files
are built without the FPU as always, so no interrupt handler touches
its registers and none has to save them.  The two kinds of object file
link together without complaint.

`quake/port/libc/libm.c` does sine, cosine, arctangent, logarithm,
exponential, power, floor and ceiling in single precision by polynomials
over a reduced range, whatever type the declaration says: the game wants
no more of them.

A compiler built for `-m4-single-only` (as the Dreamcast's homebrew
uses) would save the PR switching; whether that is worth a toolchain of
our own is for the speed measured on a unit to say.

## The card

    QUAKE.BIN       sdk/out/quakeboot-192.bin
    QUAKEGAM.BIN    quake/out/QUAKEGAM.BIN
    PAK0.PAK        quake/out/PAK0.PAK
    QUAKE.ARG       (optional) more arguments for the game, one line

in the top directory of a FAT card beside `sdboot` (as `RNSE.BIN`, or
on the loader disc), and whatever else is there.  The PAK needs a card
with 19 MB free: the bench's 32 MB card has not, its 512 MB card has.
`sdboot` lists `QUAKE.BIN` in its menu; `QUAKEGAM.BIN` is too big to be
a program of its own and is left out of the menu.  `QUAKE.ARG` is read
as Quake's command line: `+timedemo demo1` plays the first demo as fast
as it can and says the frames a second; `-dsp 0` or `-dsp 1` chooses the
card processor for the sound (below); `-mem N` gives the game N MB
instead of 20; `+map e1m1`, `-nosound` and the rest of Quake's own work
too.

The card stays in its slot while the game runs: the game reads from it.
Taken out, the game's next read fails and ends it with the error on the
screen (RETURN goes back to the loader).

In the emulator, with the stock software doing the starting as on a unit:

    rnse custom stock.iso V222 sdk/out/sdboot-192.bin sdboot.iso --index 0380
    mkdir card && cp sdk/out/quakeboot-192.bin card/QUAKE.BIN && cp quake/out/QUAKEGAM.BIN quake/out/PAK0.PAK card/
    rnse card card.img card
    rnse boot sdboot.iso --variant V222 --ignition --installed-index 0370 --sd-card card.img \
        --cycles 15000000000 --shot 50000:demo.png --key SETUP@52000 --key SELECT@53000 \
        --key SELECT@54000 --key NEXT@62000+1500 --knob 1:4@64000 --shot 65000:start.png

(the times depend on how long the stock software takes to start
`sdboot`; the test below waits for the game's messages instead).  The
game's console comes out on the debug serial port, which the emulator
prints; it lags behind the game by what the port has still to send.
The end-to-end test:

    PATH=$PWD/sdk/tools/sh4-docker:$PATH RNSE_DISC=stock.iso RNSE_QUAKE=$PWD/quake/out \
        cargo test --release --test disc quake_runs_from_an_sd_card

## Two stages

As for Doom: `sdboot` loads at most 0x1c000 bytes into the loader's
place (0x8df00000), and the game is 345 KB.  `QUAKE.BIN`
(`sdk/examples/quakeboot`, 28 KB, made from `doomboot`):

1. takes the faceplate controller over as `sdboot` left it, lit, and
   answers it while it reads the card (the display key, CD EJECT and
   RETURN held work all the while);
2. reads `QUAKEGAM.BIN` to 0x8c010080 and checks it: the trailer
   `quake/build.sh` puts at its end (`RNSEQUAK`, its length, its
   CRC-32) and the kit's mark at the image's +0x10.  It makes sure
   `PAK0.PAK` is on the card, and reads `QUAKE.ARG` if there is one;
3. leaves the game a block at 0x8c00f000 (`handoff.h`): a mark, its own
   `loader_return`, the card's slot, the PAK's size, the arguments;
4. hands over with `image_enter`.

It stays where it is, and is the game's way back to `sdboot`, through
`loader_return_via` (as `doomboot` is).  Something missing or spoilt is
said on the screen, and after four seconds it is back to `sdboot`.

## The data, from the card

The game starts by adopting the card as `quakeboot` left it
(`sd_attach`: the card processor still runs the installed software's
program, which reads sectors, and nothing is reset) and opening
`PAK0.PAK` with `sd_open`, which walks the file's chain of clusters
once into an array the game allocates (9126 clusters on the test's
card; 4 bytes each).  From then on a read of the file is a read of the
sectors it covers (`sd_pread`): whole sectors straight to where they
go, the ends of a range through a sector of the kit's own.

The kit's FAT code now keeps the sector of the cluster table it read
last apart from the one for directories, so that walking a chain reads
each sector of the table once instead of once a cluster.  `sd_load`
gains from that as well.

Quake reads its files whole, one `Sys_FileRead` each (a map, a model, a
sound), but a demo a few bytes at a time through `fopen` and `fread`.
`sys_rnse.c` keeps 8 KB of the PAK at hand for reads under 4 KB, so
that a demo reads each sector once.  The card answers at some 293 KB/s
(as measured on a unit with other programs), so a map's start reads for
some seconds; the game shows its disc icon in the corner meanwhile, and
the faceplate is answered between sectors (`sd_idle`).

Files the game writes (`config.cfg` at its end, saved games) are kept
in RAM until the unit is switched off.

## Memory

| from | to | what |
|------|----|------|
| 0x8c00f000 | | the hand-off block (`handoff.h`) |
| 0x8c010080 | 0x8c7b0000 | `QUAKEGAM.BIN` (`sdk/link.ld`, LOAD 0x8c010000, SIZE 0x7a0000), its bss (the frame, the z-buffer, a 512 KB surface cache, the sound ring), its stack at the top; the bss ends at about 0x8c19c000 |
| 0x8c7c0000 | 0x8c800000 | `sdboot`'s session, untouched |
| 0x8c800000 | 0x8de00000 | the heap: Quake's hunk (20 MB, `-mem`), the PAK's cluster chain, the C library's `malloc` |
| 0x8df00000 | 0x8df20000 | `quakeboot`, for the way back |

Shareware Quake was made to run in 8 MB; with sounds resampled to
44.1 kHz (four times the 11 kHz of the files) it wants more, and 20 MB
leaves room.

## The picture

Quake's software renderer draws 320x200 at 8 bits through a palette,
which is the 192's screen as the kit sets it up (400x235, 8 bits, a
palette of 256): each frame goes over whole, centred (at 40, 17), with
`screen_blit`, and the game's palette, with its gamma and the flashes of
damage and pickups, straight into the chip's (`VID_SetPalette`,
`VID_ShiftPalette`).  The disc icon the game shows while it reads goes
straight to the screen (`D_BeginDirectRect`), as on the PC.  The video
options menu says that there is the one mode.

## The controls

| control | in the game | in a menu or the console |
|---------|-------------|--------------------------|
| select knob | turn, 10° a detent | up, down |
| SELECT (pressing the knob) | fire | Enter |
| NEXT, PREV, held | forward, back | right, left (sliders) |
| top left soft key | jump | "y" (yes to a question) |
| top right soft key | the next weapon | "n" |
| bottom left, bottom right soft key | strafe left, right | |
| NAV, TEL, NAME, MEDIA, RADIO | weapons 1 to 5 | |
| INFO | the scores | |
| CAR | pause | |
| SETUP | the menu | back |
| RETURN, short | the menu | back |
| volume knob | the amplifier's volume (0 to 30) | the same |
| EJECT (by the power button) | the display folds down and up | the same |
| CD EJECT | the disc comes out | the same |
| RETURN held 2 s | back to `sdboot` | the same |

The keys are those the PAK's own `default.cfg` binds (CTRL, the arrows,
SPACE, `/`, `,` and `.`, the digits, TAB, PAUSE, ESCAPE), so nothing is
rebound and the options menu's key setup works as on the PC.  Running
is always on (`cl_forwardspeed 400`), and the view levels itself
(`lookspring 1`).  Turning is the knob's, not the left and right arrow
keys': a detent is 10°, at most 30° a frame.  The last three controls
are the kit's (`panel_service`), as in every program.  A key pressed and
let go while the game was busy (a map loading) still counts for one
frame (`panel_pressed`).  Whether the faceplate reports two keys at
once (NEXT and SELECT) is open, as for Doom.

## Sound

Quake mixes into a ring that a sound card plays round and round, a
little ahead of where the card is (`snd_dma.c`).  Here the card is a
card processor with the kit's program of its own (`pcm.c`), 16-bit
stereo at 44.1 kHz out of its converter, fed by the DMA controller from
the ring, block after block with no end (`pcm_loop`, new in the kit:
`pcm_stream` with every block always ready), and `pcm_loop_pos` says
where it is, from the block it plays and the DMA channel's count.  The
ring is 16 blocks of 1024 frames (0.37 s); the game writes it through
P2, past the cache, since the controller reads memory.  Quake mixes at
44.1 kHz itself (its sounds resampled when they are loaded) and 0.15 s
ahead (`_snd_mixahead`), so a frame of up to that long is heard without
a gap; the mixing costs little, being a few channels of
multiply-and-add a sample.

The processor is the one the card is **not** in: the card's own runs the
installed software's program, which reads the PAK, and `pcm_start`
would replace it.  With the card in the first slot, as in the test, the
sound is the second processor's.  Which of the two converters reaches
the amplifier on a unit is still to be heard (`sdk/examples/pcmtest`);
if only one does, the card goes in the other slot.  `-dsp N` overrides
the choice (taking the card's own ends the game's reads).

No music: Quake's is CD audio, tracks 2 and up of the game's disc.  The
unit has a CD drive with audio output to the amplifier, so that would
be a step of its own.

## Speed

`+timedemo demo1` in the emulator, with the caches modelled: 969 frames
in 19.6 s, **49.4 frames a second** at one instruction a cycle.  The
game sets the caches as Doom does (CCR 0x80000105: copy-back, no index
modes) and puts back what it found before every hand-back.  A unit will
be slower than that count: floating-point latencies, memory, and the
frame's copy to the graphics chip are all counted as one cycle an
instruction here.

## A bug of the engine's

`COM_FileBase` (`common.c`) finds a file's base name by looking back
from the extension for a `/`, without stopping at the start of the name.
For a name without a directory (`progs.dat`, at every map's start) it
went on into whatever lay before the string until it met a `/` or a
zero byte, and copied all of that into the caller's 32-byte buffer on
the stack.  On the PC a zero byte happened to come first; here it did
not, the copy went over `COM_LoadFile`'s return address, and the game
jumped to 0x73001a88 (an illegal instruction: `unexpected exception:
EXPEVT=00000180`) at the menu or the first map.
`quake/patches/0001-com-filebase.patch` rewrites the function to look
within the name only.  It was found with the emulator's new
`--watch-store` (the CPU's stores to an address, with their PC), which
showed `strncpy` from `COM_FileBase` writing the return address's
slot.

## What it is and is not, so far

Seen in the emulator, with the caches modelled and RAM arbitrary
(`quake_runs_from_an_sd_card`): from the stock software's start through
`sdboot` and `quakeboot` to the game; the PAK read from the card on
demand (the engine checks `progs.dat` against its CRC); the console,
the demos playing and moving, with sound out of the second card
processor's converter; the display folding down and up while the demo
plays and reads the card; SETUP, SELECT twice: a new game on the start
map; NEXT and the knob moving the view; CD EJECT; a short RETURN
staying in the game and RETURN held going back to `sdboot`; the
watchdog served throughout, map loads included.

Not seen on a unit yet.  What the emulator cannot vouch for:

- the speed (above);
- which card processor's converter is heard, and the card's reads by
  one processor while the other plays (the two are independent here);
- the 512 MB card read through the stock program while the game runs,
  for minutes, and how long a map's start takes at the unit's pace;
- two faceplate keys held at once.
