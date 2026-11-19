/* What quakeboot (quake/quakeboot) leaves for the game it starts
 * (quake/): at a fixed address below the game, in cached RAM.  See
 * docs/quake.md, "Memory". */
#ifndef QUAKE_HANDOFF_H
#define QUAKE_HANDOFF_H

#define QUAKE_HANDOFF 0x8c00f000u
#define QUAKE_LOAD 0x8c010000u      /* the game's LOAD (its image from +0x80) */
#define QUAKE_ROOM_END 0x8c7b0000u  /* LOAD + SIZE: image, bss and stack below */
#define QUAKE_HEAP 0x8c800000u      /* Quake's hunk and the C library's malloc */
#define QUAKE_HEAP_END 0x8de00000u

#define QUAKE_MAGIC0 0x524e5345u    /* "RNSE" */
#define QUAKE_MAGIC1 0x5155414bu    /* "QUAK" */

struct quake_handoff {
    u32 magic[2];
    /* quakeboot's loader_return, for loader_return_via (0: none). */
    void (*loader_return)(int status);
    /* The card's slot, as sd_attach wants it: PAK0.PAK is read from it
     * while the game runs, and the sound is the other processor's. */
    int slot;
    u32 pak_size;
    /* QUAKE.ARG from the card: more arguments, NUL-terminated. */
    char args[256];
};

#endif
