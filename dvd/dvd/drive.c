/* The drive for the player: reads that run while the program decodes.
 * A READ (10) of up to 32 sectors is sent, and from then on each call
 * of drive_service takes the sectors the drive has ready (it says so
 * with DRQ) into a ring, without waiting for the rest.  The SDK's
 * cd_read waits for every sector, which at the 192's measured 5.5 ms a
 * sector of a CD would leave the decoders standing.
 *
 * The playlist's cells are read one after the other; drive_seek starts
 * again at a cell. */
#include "player.h"

#define BSY 0x80
#define DRQ 0x08
#define ERR 0x01

static u8 *ring;
static u32 ring_n;          /* sectors */
static u32 head, tail;      /* sectors put in, taken out (counts) */
static const struct disc_playlist *list;
static int cell;            /* of the next sector to ask for */
static u32 next;            /* that sector */
static int busy;            /* a command runs */
static u32 cmd_lba, cmd_count, cmd_got, cmd_start;
static int retries;
static u32 junk[512];

u32 drive_errors, drive_sectors, drive_skipped;
/* For each sector in the ring, its cell, so that the player knows the
 * chapter it is in. */
static u16 *cells;

void drive_init(u8 *buf, u32 sectors)
{
    ring = buf;
    ring_n = sectors;
    cells = (u16 *)malloc(sectors * 2);
    head = tail = 0;
    busy = 0;
}

static int wait_clear(u8 bits, u32 ms)
{
    u32 start = millis();
    while (cd_alt_status() & bits)
        if (millis() - start > ms)
            return -1;
    return 0;
}

/* Everything the drive still has for a running command, thrown away
 * (to stop in the middle of one). */
static void drain(void)
{
    u32 start = millis();
    while (busy && millis() - start < 10000) {
        u8 st;
        u32 n, k;
        if (cd_alt_status() & BSY) {
            panel_service();
            continue;
        }
        st = cd_reg_read(7);
        if ((st & ERR) || !(st & DRQ)) {
            busy = 0;
            break;
        }
        n = (u32)cd_reg_read(4) | (u32)cd_reg_read(5) << 8;
        for (k = 0; k < n; k += 2)
            junk[(k / 2) & 511] = cd_data_read();
    }
    busy = 0;
}

void drive_seek(const struct disc_playlist *pl, int c)
{
    drain();
    list = pl;
    cell = c;
    next = c < pl->ncells ? pl->cells[c].first : 0;
    head = tail = 0;
    retries = 0;
}

void drive_stop(void)
{
    drain();
    list = 0;
    head = tail = 0;
}

u32 drive_level(void)
{
    return head - tail;
}

int drive_finished(void)
{
    return !busy && (!list || cell >= list->ncells);
}

const u8 *drive_sector(int *c)
{
    if (head == tail)
        return 0;
    if (c)
        *c = cells[tail % ring_n];
    return ring + (tail % ring_n) * 2048;
}

void drive_consume(void)
{
    if (head != tail)
        tail++;
}

static int issue(void)
{
    u8 cmd[12] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    u32 left = list->cells[cell].last - next + 1, space = ring_n - (head - tail), n, i;
    n = left < 32 ? left : 32;
    if (n > space)
        n = space;
    /* (Not across the end of the ring: one slot after the other.) */
    if (!n)
        return 0;
    if (wait_clear(BSY, 50))
        return -1;
    cd_reg_write(6, 0xa0);
    if (wait_clear(BSY | DRQ, 50))
        return -1;
    cd_reg_write(1, 0x00);
    cd_reg_write(4, 0x00);
    cd_reg_write(5, 0x08);
    cd_reg_write(7, 0xa0);
    cd_alt_status();
    if (wait_clear(BSY, 100) || !(cd_alt_status() & DRQ))
        return -1;
    cmd[2] = (u8)(next >> 24);
    cmd[3] = (u8)(next >> 16);
    cmd[4] = (u8)(next >> 8);
    cmd[5] = (u8)next;
    cmd[8] = (u8)n;
    for (i = 0; i < 12; i += 2)
        cd_data_write((u16)(cmd[i] << 8 | cmd[i + 1]));
    cmd_lba = next;
    cmd_count = n;
    cmd_got = 0;
    cmd_start = millis();
    busy = 1;
    return 0;
}

/* Past a sector that cannot be read, in the end. */
static void advance(u32 sectors)
{
    next += sectors;
    while (list && cell < list->ncells && next > list->cells[cell].last) {
        cell++;
        if (cell < list->ncells)
            next = list->cells[cell].first;
    }
}

void drive_service(u32 budget)
{
    if (!list)
        return;
    while (busy && budget) {
        u8 st;
        u32 n, k;
        u16 *to;
        if (cd_alt_status() & BSY) {
            if (millis() - cmd_start > 20000) {
                /* A drive that does not come back: start over there. */
                busy = 0;
                drive_errors++;
            }
            return;
        }
        st = cd_reg_read(7);
        if (st & ERR) {
            busy = 0;
            drive_errors++;
            /* Again from the first sector not received; three times,
             * then that sector is left out. */
            next = cmd_lba + cmd_got;
            if (++retries > 3) {
                retries = 0;
                next++;
                drive_skipped++;
                advance(0);
            }
            return;
        }
        if (!(st & DRQ)) {
            busy = 0;
            if (cmd_got < cmd_count) {
                /* Fewer than asked for: ask for the rest. */
                next = cmd_lba + cmd_got;
            }
            break;
        }
        n = (u32)cd_reg_read(4) | (u32)cd_reg_read(5) << 8;
        to = (u16 *)(ring + (head % ring_n) * 2048);
        for (k = 0; k < n && k < 2048; k += 2)
            *to++ = cd_data_read();
        for (; k < n; k += 2)
            cd_data_read();
        cells[head % ring_n] = (u16)cell;
        head++;
        cmd_got++;
        drive_sectors++;
        retries = 0;
        budget--;
        if (cmd_got == cmd_count) {
            /* The drive drops BSY and DRQ by itself now. */
            advance(cmd_count);
        }
    }
    if (!busy && cell < list->ncells && ring_n - (head - tail) >= 32) {
        if (next < list->cells[cell].first || next > list->cells[cell].last)
            advance(0);
        if (cell < list->ncells && issue())
            drive_errors++;
    }
}
