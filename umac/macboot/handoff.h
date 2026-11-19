/* What macboot (umac/macboot) leaves for umac (umac/), the
 * Mac Plus emulator it starts: at a fixed address below it, in cached
 * RAM.  See docs/umac.md, "Memory". */
#ifndef MAC_HANDOFF_H
#define MAC_HANDOFF_H

#define MAC_HANDOFF 0x8c00f000u
#define MAC_LOAD 0x8c010000u      /* umac's LOAD (its image from +0x80) */
#define MAC_ROOM_END 0x8c7b0000u  /* LOAD + SIZE: image, bss and stack below */
#define MAC_ROM 0x8c800000u       /* the Mac Plus ROM, 128 KiB */
#define MAC_ROM_SIZE 0x00020000u
#define MAC_RAM 0x8c900000u       /* the Mac's 4 MiB of RAM */
#define MAC_RAM_SIZE 0x00400000u
#define MAC_DISK 0x8cd00000u      /* the disk image, as read from the card */
#define MAC_DISK_ROOM 0x01000000u

/* The ROM umac's patches are written for: the Mac Plus's third,
 * whose first long word is its checksum. */
#define MAC_PLUS_V3 0x4d1f8172u

#define MAC_MAGIC0 0x524e5345u    /* "RNSE" */
#define MAC_MAGIC1 0x554d4143u    /* "UMAC" */

struct mac_handoff {
    u32 magic[2];
    u32 rom, disk, disk_size;
    /* macboot's loader_return, for loader_return_via (0: none). */
    void (*loader_return)(int status);
    int slot;
};

#endif
