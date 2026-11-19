# A Mac Plus on the 192

[umac](https://github.com/evansm7/umac), Matt Evans's Macintosh Plus
emulator around Karl Stenerud's
[Musashi](https://github.com/kstenerud/Musashi) 68000, run on the 192
as a program of the SDK's, started from an SD card by the SD loader
(`sdboot`, [sd-loader.md](sd-loader.md)), as Doom is
([doom.md](doom.md)): no other operating system underneath, the Mac on
the kit's own drivers for the screen, the faceplate and the display.
The Mac runs its own ROM and its own System from a disk image on the
card; `umac/port/` is the layer between umac and the kit.

Seen in the emulator, not yet on a unit (at the end of this page).

## Building

    PATH=$PWD/sdk/tools/sh4-docker:$PATH sdk/build.sh 192 macboot   # sdk/out/macboot-192.bin
    umac/build.sh                                             # umac/out/MACEMU.BIN

`umac/build.sh` downloads umac and its Musashi at pinned commits into
`umac/work/dl`, checks both against their SHA-256, applies
`umac/patches/`, generates Musashi's opcode handlers with the host's
`cc` (`HOSTCC`), and builds in the container of `sdk/tools/sh4-docker`
(`RNSE_UMAC_NATIVE=1`: with an `sh4-linux-gnu-gcc` on the PATH
instead).  Out come `umac/out/MACEMU.BIN` and `umac/out/macemu.elf`
(for `rnse boot --gdb`).

The Mac's ROM and System are not downloaded and must never be in the
repository: they are Apple's.  Bring a Mac Plus ROM (the third
version, 128 KiB, beginning with its checksum `4D1F8172`; umac's
patches are for that one alone) and a raw image of a Mac disk with a
System that boots, 400K or 800K floppies or bigger (not a Disk Copy
container, not compressed).  System 2.0 (Finder 4.1) and System 3.2
(Finder 5.3) have been seen to boot.

What is whose:

| file | what | licence |
|------|------|---------|
| `sdk/examples/macboot/` | the first stage, `MAC.BIN`, and `handoff.h`, what it leaves umac | MIT, as the rest |
| `umac/port/main.c` | the start, the run loop, the clock, the end | MIT |
| `umac/port/video.c` | the picture | MIT |
| `umac/port/input.c` | the faceplate as mouse and keys | MIT |
| `sdk/libc/` | the C library (shared with Doom), and `setjmp.h` for umac's fatal errors | MIT |
| `umac/patches/0001-rnse.patch` | umac and Musashi on a big-endian host without an operating system | as the files it patches |
| `tests/disc.rs` `mac_runs_from_an_sd_card` | the end-to-end test | MIT |

umac is MIT, but its replacement floppy driver (`disc.c`, the 68000
`sonydrv`) comes from Basilisk II and its key codes (`keymap.h`) from
Mini vMac, both GPLv2; Musashi is MIT.  So `MACEMU.BIN` is GPLv2 as a
whole, and whoever hands it on owes its source: the pinned commits plus
`umac/patches/` and `umac/port/`.  `.gitignore` keeps `umac/work/`,
`umac/out/` and ROM and disk images out.

## The patch

What umac and Musashi need to run here, and no more:

- `machw.h`: a 16-bit read of the Mac's memory at an even address is a
  plain load on a big-endian host (upstream swaps the bytes, as for a
  little-endian one); long reads are unsigned.
- `rom.c`: the ROM's version read byte by byte (upstream's big-endian
  branch does not compile, and its test of `BYTE_ORDER` is undefined
  without a C library).
- `disc.c`: a transfer is checked against the end of the disk and of
  the Mac's memory, and reports the bytes it moved (upstream reports 0).
- `main.c`: the keyboard's transitions in a queue (upstream keeps one, so
  that Command and a key in quick succession lose one); an eject ends
  the session (`umac_loop` returns 1); a fatal error returns 2 instead
  of running on; no disassembler.
- `m68kconf.h`: `ENABLE_DASM=0` really turns the disassembler off.
- Musashi: shifts of `1` that reach bit 31 made unsigned; its FPU and MMU
  left out for a 68000.

The fixes to `disc.c`, the keyboard queue, the ROM's version and the
shifts are the same as in the Glaze OS port of umac (the BE-300).

## The card

    MAC.BIN         sdk/out/macboot-192.bin
    MACEMU.BIN      umac/out/MACEMU.BIN
    MACPLUS.ROM     the Mac Plus ROM (yours)
    MAC.DSK         the disk image (yours)

on a FAT card beside `sdboot` (as `RNSE.BIN`, or on the loader disc).
`sdboot` lists `MAC.BIN` in its menu, or runs it at once if it is the
only program; `MACEMU.BIN` is too big to be a program of its own and is
left out of the menu, as `DOOMGAME.BIN` is.

In the emulator, with the stock software doing the starting as on a unit
(here the North American disc `RNSE_NA_FW_0320_0350`, whose V222 index
is 0350):

    rnse custom stock.iso V222 sdk/out/sdboot-192.bin sdboot.iso --index 0360
    mkdir card && cp sdk/out/macboot-192.bin card/MAC.BIN && cp umac/out/MACEMU.BIN card/
    cp plus-v3.rom card/MACPLUS.ROM && cp system.dsk card/MAC.DSK
    rnse card card.img card
    rnse boot sdboot.iso --variant V222 --ignition --installed-index 0350 --sd-card card.img \
        --cycles 8000000000 --shot 20000:finder.png

The Finder is there by 20 s after the start, a few seconds after umac
has begun (2 s of that go to reading the card, with a 400K disk).  (`--strict` keeps the stock software here from
starting the loader at all; the end-to-end test models the caches and
arbitrary RAM itself.)  To open the disk, turn the select knob to the
right, quickly and then slowly, until the pointer is on its icon, a few
detents of the volume knob to go down onto it, and two presses of
SELECT: `--knob 1:1@MS` and `--key SELECT@MS+100`, as in the test.

umac's messages come out on the debug serial port (the emulator prints
them).  The end-to-end test:

    PATH=$PWD/sdk/tools/sh4-docker:$PATH RNSE_DISC=stock.iso RNSE_UMAC=$PWD/umac/out \
        RNSE_MAC_ROM=plus-v3.rom RNSE_MAC_DISK=system.dsk \
        cargo test --release --test disc mac_runs_from_an_sd_card

It starts the Mac as on a unit, with the caches modelled and RAM
arbitrary, waits for the Finder (its menu bar's line over the
desktop's pattern), moves the pointer with both knobs, folds the
display down and up, holds RETURN back to `sdboot`, and checks the
watchdog and the caches throughout.  Screenshots go to `out/umac/`.

## Two stages

The same as Doom's: `sdboot` loads a program of at most 0x1c000 bytes
into the loader's place, and umac is 511 KB with the ROM and the disk
besides.  So `MAC.BIN` (`sdk/examples/macboot`, 27 KB) is a first stage:

1. It takes the faceplate controller over as `sdboot` left it, lit
   (`panel_attach`), and answers it while it reads the card (`sd_idle`
   is `panel_service`): the display key, CD EJECT and RETURN held work
   all the while.
2. It reads `MACEMU.BIN` to 0x8c010080 and checks it: the trailer
   `umac/build.sh` puts at its end (`RNSEUMAC`, its length, its CRC-32)
   and the SDK's mark at the image's +0x10.  Then `MACPLUS.ROM` to
   0x8c800000, which has to be 128 KiB and begin with `4D1F8172`, and
   `MAC.DSK` to 0x8cd00000, a multiple of 512 bytes and at most 16 MiB.
   2.2 s in the emulator for the 400K disk.
3. It leaves umac a block at 0x8c00f000 (`handoff.h`): a mark, where the
   ROM and the disk are, the disk's length, its own `loader_return`, the
   slot.
4. It hands the processor over with `image_enter`.

The way back to `sdboot` goes through `macboot`, as Doom's goes through
`doomboot`.  Something missing or spoilt is said on the screen
(`MACEMU.BIN MISSING OR LONG`, `... IS SPOILT`, `MACPLUS.ROM IS NO PLUS
V3 ROM`, `MAC.DSK IS NO DISK IMAGE`, with an error number), and after
four seconds it is back to `sdboot`.

## Memory

| from | to | what |
|------|----|------|
| 0x8c00f000 | | the hand-off block (`handoff.h`) |
| 0x8c010080 | 0x8c7b0000 | `MACEMU.BIN` (`sdk/link.ld` with LOAD 0x8c010000, SIZE 0x7a0000), its bss, its stack at the top; it ends at about 0x8c0ab000 |
| 0x8c7c0000 | 0x8c800000 | `sdboot`'s session, untouched |
| 0x8c800000 | 0x8c820000 | the Mac's ROM, which umac patches in place |
| 0x8c900000 | 0x8cd00000 | the Mac's RAM, 4 MiB (a Plus's most), cleared at the start |
| 0x8cd00000 | 0x8dd00000 | the disk image |
| 0x8df00000 | 0x8df20000 | `macboot`, for the way back |

The disk is used in place, a copy in RAM: what the Mac writes to it
lasts until it ends or the unit is switched off, since the kit does not
write cards.

## The picture

umac's ROM patch gives the Mac a screen of any size whose width is a
multiple of 32 and that fits in 64 KB; here 384 x 235, the most that
fits the 192's 400 x 235 (the Coral chip's layer L4, 8 bits through a
palette).  The Mac's own code, the ROM and the System alike, takes
the size from there: the menu bar, the desktop, windows and alerts fit
themselves to it.  Programs that assume a Plus's 512 x 342 (MacPaint)
may be cut off.  The Mac sits 1:1 at x = 8, with an 8-pixel black
margin either side.

Each byte of the Mac's frame buffer becomes eight pixels through a
table, and only the lines that changed since the last frame go over the
bus (`screen_blit`): all of it is 90 KB, and a Mac at rest changes
nothing.  The screen is brought over with each of the Mac's vertical
blanks, 60 a second by the unit's clock; nothing waits for the
display's own.

## The controls

| control | the Mac |
|---------|---------|
| select knob | the pointer, left and right (clockwise: right) |
| volume knob | the pointer, up and down (clockwise: down) |
| SELECT (pressing the knob) | the mouse button; held, it drags |
| bottom right soft key | Return |
| bottom left soft key | Delete (backspace) |
| top right soft key | Tab |
| top left soft key | Command for the next of these keys, sticky (an orange mark in the left margin while it is armed; pressed again, it is disarmed) |
| SETUP | Command-period, the Mac's cancel |
| EJECT (by the power button) | the display folds down and up |
| CD EJECT | the disc comes out |
| RETURN held 2 s | back to `sdboot` |

umac has no sound, so the volume knob is free for the pointer.  A
detent moves the pointer 2 pixels when the knob turns slowly, and up to
12 when the detents come less than 90 ms apart; the Mac's own mouse
acceleration comes on top.  umac gives the Mac at most one step of the
mouse every 5 ms of its time and drops what piles up beyond 30 pixels,
so a fast spin of the knob is cut short rather than overshooting
after the knob has stopped.  A press of SELECT too short for the
faceplate's report to show it down (`panel_pressed`) is still a click,
held down for 60 ms, so that the Mac's tick sees it; two such presses
are a double click.  Shift-click and Command-click are missing.

The arrow keys are missing too: the Plus's keyboard sends them through
its keypad prefix, which umac's keyboard does not model.  There is no
typing beyond the keys above (the knob as a typewriter, as Linux has it,
would be next).

The last three controls in the table are the kit's (`panel_service`,
called before every 5 ms step of the Mac) and mean what they mean in
every program.

## Speed

umac runs the 68000 in steps of 40,000 of its cycles, 5 ms of a Mac
Plus's time, and between steps answers the faceplate and brings the
screen over.  The Mac's clock is what umac counts, not the unit's, so it
runs as fast as the SH-4 takes it: in the emulator, at one instruction a
cycle, 12.8 s of the Mac's time went by in 5 s of the unit's, about
2.6 times a Mac Plus.  The caches are set as Doom has them (P1
copy-back, CCR 0x80000105), with which a unit ran a decoder at 92 to
98 % of the emulator's pace (`docs/doom.md`, "Speed and the caches").
The Mac's vertical blank and its one-second tick go by the unit's
clock.

## What it is and is not, so far

Seen in the emulator (`mac_runs_from_an_sd_card` and the scripted runs
above): from the stock software's start through `sdboot` and
`macboot` to the Mac; the happy Mac and the Finder at 384 x 235 with
System 2.0 (400K) and System 3.2 (800K); the pointer on both knobs, the
disk's icon selected with a click and opened with a double click; the
sticky Command's mark; Delete renaming the disk; the display folding
down and up under the Mac; RETURN held going back to `sdboot`; the
watchdog served throughout (the test allows a pause of up to 40 ms) and
nothing stale from the caches.

umac says `SonyControl:accRun: Not supported!` while the Mac starts
and `Ignoring write ... to address 00adbef9` once; neither stops it.

Not seen on a unit yet; what the emulator cannot vouch for there:

- the speed (above);
- the step of the pointer on a real knob, and whether its faceplate
  reports detents closely enough together to speed it up;
- a unit's `ocbp` over the 7.6 MB of umac's room in `image_enter`,
  which needed the watchdog strobed for Doom.
