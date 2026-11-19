# NES on the RNS-E 192

Bare-metal C application for the 192 with V222 0371 installed DSP firmware.
No Linux, SD writes, cartridge save persistence, or firmware flashing.
The ROM browser reads up to 128 `.NES` files in the FAT card root, with 8.3 names.
NTSC iNES mappers 0, 1, 2, 3, 4 and 7 are enabled; image limit is 4 MiB.
Compatibility with individual commercial games and real-time performance on a
physical unit require testing; mapper presence is not a compatibility guarantee.

## Build and run

```
PATH="$PWD/sdk/tools/sh4-docker:$PATH" nes/build.sh
```

Alternatively run `nes/build.sh` in the `rnse-sh4-cross` container, mounting
this checkout and setting its SDK directory as the working directory.
Copy `sdk/out/nes/RNSE.BIN` and `sdk/out/nes/NESCORE.BIN` plus your ROM files
to a supported FAT card's root. To preserve an existing RNSE.BIN, name the new
launcher NES.BIN instead and choose NES.BIN in the loader menu.
Boot the existing SD-loader disc. No stock
firmware or commercial ROMs are distributed here.

The small RNSE.BIN is a launcher at the standard SDK load address. NESCORE.BIN
contains the normal SDK entry table, linked at 0x8c100080; it is padded past
112 KiB so that the existing SD loader treats it as data, not a runnable menu
entry. Never truncate that padding when copying to the card.

The right knob chooses a ROM and SELECT runs it. A short RETURN exits to the
browser; another short RETURN exits the browser to the loader. Hold RETURN for
two seconds to invoke the existing loader recovery. Display-open and eject
continue through `panel_service`. The left knob controls volume during play.
When no valid CAN controller packet has arrived, or the adapter has been silent
for 200 ms, the faceplate automatically controls player 1. A fresh CAN packet
immediately restores both external controllers; their states take priority even
when no buttons are pressed. Player 2 is released during faceplate fallback.
The SELECT press used to launch a ROM must be released before faceplate gameplay
input activates.

| Faceplate | NES player 1 |
|---|---|
| Top-left soft key | Up |
| Bottom-left soft key | Down |
| PREV / NEXT | Left / Right |
| Top-right / bottom-right soft key | A / B |
| INFO | Select |
| Press select knob | Start |

RETURN still exits, and volume, display-open and eject retain their functions.
Fallback detects a missing/stale adapter; an unplugged pad on a still-transmitting
adapter cannot be detected by this protocol.

Two NES controller states arrive on configurable standard CAN ID 0x6e3;
see `hardware/nes-can/README.md` for wiring and programming.

## Memory and timing

| Region | Use |
|---|---|
| 0x8c100000–0x8c300000 | Runtime, BSS, stack (linker bounded) |
| 0x8c7c0000–0x8c800000 | Existing loader recovery; untouched |
| 0x8c800000–0x8cc00000 | ROM image, bounded at load |
| 0x8cc00000–0x8d400000 | Resettable per-game allocation arena |
| 0x8df00000–0x8df20000 | Small launcher / original loader region |

Cached and uncached aliases refer to the same physical storage. The launcher
loads through an uncached address and invalidates caches before jumping to the
runtime. `SDK_SESSION_LOAD=0x8c100000` explicitly permits the second-stage runtime
to return through its launcher's existing session; the original loader address,
control-block validation and snapshot checksum remain checked. ROMs are read completely before the card DSP switches to PCM output;
exiting play stops PCM and reinitializes SD. No card access occurs during play.
Audio uses 735 mono samples per nominal 60 Hz frame, copied to stereo and fed
through alternating DMA buffers. Up to two video frames can be skipped in a row.
Serial diagnostics count completed frames and detected DMA starvation.
Nofrendo's APU uses Q8 accumulators; CPU word reads are bytewise for SH-4 alignment.

## Verification

From `ports/` (the kit beside it, as in rnse):

```
cc -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I../sdk/include -Ines/nes nes/tests/protocol.c \
  nes/nes/protocol.c -o /tmp/nes-protocol-test
/tmp/nes-protocol-test
PATH="$PWD/../rnse-sdk/tools/sh4-docker:$PATH" RNSE_DISC=/path/to/stock.iso \
  cargo test --release -p rnse-ports --test disc nes_bare_metal_sd_video_audio -- --nocapture
```

Run `python3 nes/package.py` after `nes/build.sh` to assemble `out/nes-release/`
(the payloads, nofrendo's licences, the corresponding source and SHA256SUMS).
The adapter's firmware and CAD archive are packaged by rnse's
`hardware/nes-can/package.py` (rnse-hardware).

The integration test generates its own homebrew cartridge. Firmware-backed tests
need the local firmware image and cross-compiler; a run without RNSE_DISC skips
this test. Physical acceptance requires both controllers, unplug/reconnect,
CAN loss, repeated game switches, sustained video/audio, volume, display/eject,
and loader recovery. The controller adapter has not been fabricated or electrically
validated.

### Real-unit status

The first real-unit attempt with the 512 MB FAT16 card showed a black screen for
a few seconds and returned to the loader with `TEST RETURNED: 1`. Hardware startup
is unresolved. Status 1 is the launcher's failure to initialize either SD slot;
the runtime also uses status 1 for SD-mount failure after its error screen is
dismissed. Emulator success does not establish successful boot on the unit.

The launcher currently calls `sd_init` on both slots, while the existing SD loader
tries `sd_attach` before resetting the card processor. Reusing that initialized
card is a candidate fix, not a confirmed diagnosis. The unchanged build and
Metroid were copied to the 32 MB FAT16 card for comparison; its hardware result
is pending. Test apps were removed from that card because the loader menu has a
nine-entry limit.

Verified on 2026-11-15 in the RNS-E emulator: generated cartridge video and audio,
both CAN pad snapshots, faceplate fallback and CAN takeover, 200 ms timeout, a second launch after restoring SD,
and long-RETURN recovery to the original loader. The integration test used the
local V222 image with strict cache/watchdog checks; flash stayed unchanged.
All 83 Rust library tests and the sanitizer-backed cartridge/protocol tests pass.
This verifies the generated NROM fixture; other enabled mappers and physical
real-time performance have not been benchmarked here.

A user-provided Metroid (USA), mapper 1 cartridge was also smoke-tested through
the SD loader, title screen, Start/Continue menu and opening Brinstar scene.
The optional `nes_user_rom_from_sd_loader` test reads `RNSE_NES_ROM` from outside
the repository and writes screenshots under `sdk/out/nes/`; it does not commit
or package the ROM. This is an initial-scene smoke test, not a full playthrough.
