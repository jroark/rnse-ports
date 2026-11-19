/* What doomboot (doom/doomboot) leaves for the game it starts
 * (doom/): at a fixed address below the game, in cached RAM.  See
 * docs/doom.md, "Memory". */
#ifndef DOOM_HANDOFF_H
#define DOOM_HANDOFF_H

#define DOOM_HANDOFF 0x8c00f000u
#define DOOM_LOAD 0x8c010000u      /* the game's LOAD (its image from +0x80) */
#define DOOM_ROOM_END 0x8c7b0000u  /* LOAD + SIZE: image, bss and stack below */
#define DOOM_WAD 0x8c800000u
#define DOOM_WAD_ROOM 0x00500000u
#define DOOM_HEAP 0x8cd00000u
#define DOOM_HEAP_END 0x8de00000u

#define DOOM_MAGIC0 0x524e5345u    /* "RNSE" */
#define DOOM_MAGIC1 0x444f4f4du    /* "DOOM" */

struct doom_handoff {
    u32 magic[2];
    u32 wad, wad_size;
    /* doomboot's loader_return, for loader_return_via (0: none). */
    void (*loader_return)(int status);
    int slot;
    /* DOOM.ARG from the card: more arguments, NUL-terminated. */
    char args[256];
};

#endif
