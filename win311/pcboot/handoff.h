/* What pcboot (sdk/examples/pcboot) leaves for the PC emulator it starts
 * (win311/): at a fixed address below it, in cached RAM.  See
 * docs/win311.md, "Memory". */
#ifndef PC_HANDOFF_H
#define PC_HANDOFF_H

#define PC_HANDOFF 0x8c00f000u
#define PC_LOAD 0x8c010000u      /* PCEMU.BIN's LOAD (its image from +0x80) */
#define PC_ROOM_END 0x8c400000u  /* LOAD + SIZE: image, bss and stack below */

#define PC_MAGIC0 0x524e5345u    /* "RNSE" */
#define PC_MAGIC1 0x5043454du    /* "PCEM" */

struct pc_handoff {
    u32 magic[2];
    /* pcboot's loader_return, for loader_return_via (0: none). */
    void (*loader_return)(int status);
    /* The slot of the card PCEMU.BIN came from. */
    int slot;
};

#endif
