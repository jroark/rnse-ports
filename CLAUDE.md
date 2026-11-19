# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Ports for the Audi RNS-E 192 (SH7750R, variants `V220` Europe and `V222` North America), started from an SD card by the SD loader (`sdboot`) of the kit, rnse-sdk (https://github.com/jroark/rnse-sdk): Doom, Quake, umac (a Mac Plus), Windows 3.11 on Tiny386, a NES emulator and a DVD player. Each is a C program built on the kit (`RNSE_SDK`, default `../rnse-sdk`) and tested on the emulator, rnse (https://github.com/jroark/rnse). It was split out of rnse with its history.

Firmware is copyrighted and must never be committed. `.gitignore` blocks `*.iso`, `*.bin`, `*.KWI`, `*.MOT`, `/firmware/`, `/out/`; do not add firmware-derived files (flash dumps, extracted modules, SD card images) to the repository under other names either.
The same goes for game data and other people's software: WADs, PAKs, Mac ROMs and disks, PC disk images and NES ROMs are never committed; the build scripts download only what they pin (engine sources, the shareware data), checked against their SHA-256, into `*/work/`.

## Mandatory hardware behavior

- **Always preserve physical ejection.** Every custom firmware, loader,
  diagnostic and test payload must support opening/folding down the display
  and ejecting the disc. Keep faceplate polling and eject handling responsive
  on normal screens, error screens and during long-running operations. Do not
  repurpose the display/open key without another accessible physical way to
  open the display and reach the disc-eject control. SDK `KEY_DISPLAY` is
  emulator `EJECT`; `KEY_CD_EJECT` is a separate key. Verify the actual eject
  command path before providing hardware test builds, including across handoffs.
- **Hold RETURN for two seconds to return to the loader.** Every payload
  launched by the SD loader must implement this shortcut from every screen,
  close active apps and stop playback before calling `loader_return(status)`.
  A short RETURN retains its normal back/cancel behavior; a long hold must not
  also trigger that short action or immediately relaunch the payload after
  returning. Return directly to the resident loader without resetting or
  invoking an updater. Outside a loader session, report that return is
  unavailable and remain operable, with eject still working.
- **No firmware-flash option unless the user explicitly asks for it.** Default
  custom loaders, test payloads and UIs must neither flash firmware nor expose
  any updater/install/restore/erase option or hidden button sequence that can
  do so. This includes double-SETUP, `stock_update()`, firmware removal and
  automatic updater handoffs. Permission to build, test, recover, return to
  the loader or eject a disc is not permission to flash. Existing updater
  mechanisms described elsewhere in this file are technical background, not
  authorization to expose or invoke them. Include a flash option only when
  explicitly requested by the user.

Hardware incident reported 2026-11-15: double-SETUP on the original loader
invoked an updater and flashed firmware whose eject button did not work. The
disc was trapped and the user had to disassemble the RNS-E. These requirements
apply to all future hardware builds.
## Commands

```sh
sh tools/build-launchers.sh            # the launchers (doom/doomboot, ...), dvd/dvd, nes/: no downloads
doom/build.sh                          # doom/out/: DOOMGAME.BIN, DOOM1.WAD (downloads, pinned)
quake/build.sh; umac/build.sh; win311/build.sh   # the same for the others
sh dvd/check.sh                        # the DVD decoders against ffmpeg, the player on the emulator
cargo test --release                   # the emulator tests: skip without RNSE_DISC (below)
RNSE_DISC=$PWD/disc.iso RNSE_DOOM=$PWD/doom/out cargo test --release --test disc doom_runs_from_an_sd_card
```

Without an SH-4 GCC on the PATH install `gcc-sh4-linux-gnu binutils-sh4-linux-gnu` (Debian/Ubuntu) or put the kit's `tools/sh4-docker` on the PATH. `tests/disc.rs` needs `RNSE_DISC` (an update disc image, firmware: never committed) and each test its data: `RNSE_DOOM`, `RNSE_QUAKE`, `RNSE_UMAC` with `RNSE_MAC_ROM` and `RNSE_MAC_DISK`, `RNSE_WIN311`, `RNSE_DVD` (made by `dvd/check.sh`), and for the NES `nes/build.sh` run first. Without them the tests return early and report success, which proves nothing. A crate's tests run in its own directory, so give these variables absolute paths.

## Layout

- `doom/`, `quake/`, `umac/`, `win311/`: each game's `build.sh` (downloads pinned, builds with the kit's files and `libc/`), `port/` (its drivers on the kit), `patches/`, and its launcher (`doom/doomboot`, `quake/quakeboot`, `umac/macboot`, `win311/pcboot`), the SD card's `RNSE.BIN`-style program that loads the game and is its way back to the loader.
- `nes/`: the emulator (`nes/nes`, nofrendo vendored in `nes/nofrendo`), its launcher `nes/nesboot`, `nes/build.sh`, the adapter protocol's tests (`nes/tests/protocol.c`) and `nes/package.py` (its release). This repository owns the protocol (`nes/nes/protocol.h`); the adapter's firmware in rnse-hardware follows it.
- `dvd/`: the player (`dvd/dvd`: MPEG-1/2, AC-3, layer II, LPCM, ISO 9660/UDF, the IFO's titles and chapters), `dvd/check.sh`, `dvd/tests/dvd_host.c`.
- `docs/`: one document each. `tests/`: the emulator tests (crate `rnse-ports`; `rnse` pinned in `Cargo.toml` and `Cargo.lock`).

A `docs/....md` or `src/....rs` named in a comment that is not in this repository is in rnse (`docs/hardware.md`, `docs/boot-rom.md`, ...) or, for the kit's own (`docs/custom-firmware.md`, `docs/sd-loader.md`, `src/sd.c`, ...), in rnse-sdk.

## Things that are easy to miss

- **DVD: no descrambler.** CSS is recognised and refused; only the empty weak hooks of `dvd/dvd/scramble.c` are shipped, for one's own. Keep it that way.
- **The way back.** Each port's document says how RETURN held for two seconds gets back to `sdboot` and how EJECT is kept working while it loads and runs; a game hands back through its launcher. Check both on any change to start-up or input code, on the emulator (`--key RETURN@MS+2500`, `--key EJECT@MS`).
- **Comparing builds byte for byte** (when code moves, say) needs the build date and the source paths pinned (an `-include` of a header fixing `__DATE__`/`__TIME__`, `-fmacro-prefix-map` in `EXTRA_CFLAGS`): the games put them in their binaries, so two builds in one place differ without it.
- An example's file `flags` gives single files their own compiler options (`-m4` for FPU code, as Quake's); `place` links a program outside the loader's place.
