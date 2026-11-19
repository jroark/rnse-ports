# rnse-ports

Programs for the Audi RNS-E 192 (8P0 035 192), started from an SD card
by the SD loader of [rnse-sdk](https://github.com/jroark/rnse-sdk):

- [Doom](docs/doom.md) (the shareware 1.9, on doomgeneric),
- [Quake](docs/quake.md) (the shareware 1.06 data, id's GPL WinQuake),
- [umac](docs/umac.md), a Mac Plus emulator (Musashi 68000),
- [Windows 3.11](docs/win311.md) on Tiny386, an emulated 486 PC,
- [a NES emulator](docs/nes.md) (nofrendo) with controllers on CAN
  through the adapter in [rnse-hardware](https://github.com/jroark/rnse-hardware),
- [a DVD player](docs/dvd.md) for unencrypted discs.

They are built on the kit (`RNSE_SDK`, default: a checkout of rnse-sdk
beside this one) and tested on [rnse](https://github.com/jroark/rnse),
the emulator they were split out of with their history (at rnse commit
0e3a2d4ce5941161e17ff4816126281fbcf80088).

```sh
git clone https://github.com/jroark/rnse-sdk ../rnse-sdk
sudo apt install gcc-sh4-linux-gnu binutils-sh4-linux-gnu   # or: PATH=$PWD/../rnse-sdk/tools/sh4-docker:$PATH
sh tools/build-launchers.sh        # the launchers, the DVD player, the NES emulator
doom/build.sh                      # downloads doomgeneric and the shareware WAD, pinned: doom/out/
RNSE_DISC=$PWD/disc.iso RNSE_DOOM=$PWD/doom/out cargo test --release --test disc doom_runs_from_an_sd_card
```

No firmware, game data, ROMs or disk images are included, and none may
be added: the games' engines and data are downloaded, pinned, by their
build scripts, and a Mac's ROM and disk or a PC's disk image are your
own. Your unit, your discs and your data are your own risk. Every program
here keeps the display opening and the disc ejecting, returns to the SD
loader when RETURN is held for two seconds, and flashes nothing: see
CLAUDE.md, "Mandatory hardware behavior". The DVD player refuses
CSS-encrypted discs, and this repository ships no descrambler.

GPL-2.0-only ([LICENSE](LICENSE)); downloaded and vendored code keeps
its own licence (nofrendo in `nes/nofrendo`, GPLv2; Tiny386 BSD-3;
Musashi's and umac's in their downloads).
