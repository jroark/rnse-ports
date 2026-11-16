/* An extension point for descrambling CSS, which this player does not
 * do (docs/dvd.md, "Scrambled discs").  The two functions here are
 * weak: a file of one's own in this directory that defines them takes
 * their place, and build.sh compiles every .c file here, so nothing
 * else changes.
 *
 * scramble_open is called when a title is about to be played, before
 * any of its sectors are read, with the title's cells (sectors of the
 * disc) and the sector of its title set's IFO.  The drive can be talked
 * to with cd_command (sdk/include/rnse.h); a drive that wants to be
 * authenticated refuses reads of scrambled sectors until it has been,
 * and the player's reads retry three times and then pass over a sector.
 * 0: scrambled sectors can be handled; anything else: the title is
 * refused as without this file.
 *
 * scramble_sector is called with a sector (a pack, 2048 bytes, in the
 * player's read-ahead, writable) whose PES packets are marked as
 * scrambled.  It must leave the sector as it would have been
 * unscrambled, the marks in the PES headers cleared (bits 5-4 of the
 * byte after the packet length): the player checks them again and
 * refuses the title if any is still set.  0 when done.
 *
 * The defaults below say "not available".  SCRAMBLE_TEST builds an
 * implementation for tests/dvd.rs only: it clears the marks of a test
 * disc whose data was never scrambled, which shows that the hooks are
 * called where they should be, and descrambles nothing. */
#include "player.h"

#ifdef SCRAMBLE_TEST
int scramble_open(const struct disc_playlist *pl, u32 ifo)
{
    (void)pl;
    (void)ifo;
    console_puts("scramble_open\n");
    return 0;
}

int scramble_sector(u8 *sector)
{
    u32 o = 14 + (sector[13] & 7);
    while (o + 9 <= 2048 && !sector[o] && !sector[o + 1] && sector[o + 2] == 1) {
        u32 len = (u32)sector[o + 4] << 8 | sector[o + 5];
        if ((sector[o + 6] & 0xc0) == 0x80)
            sector[o + 6] &= (u8)~0x30;
        o += 6 + len;
    }
    return 0;
}
#else
__attribute__((weak)) int scramble_open(const struct disc_playlist *pl, u32 ifo)
{
    (void)pl;
    (void)ifo;
    return -1;
}

__attribute__((weak)) int scramble_sector(u8 *sector)
{
    (void)sector;
    return -1;
}
#endif
