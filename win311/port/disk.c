/* The PC's hard disk: an image read as it is needed (from the SD card on
 * the 192, from a file on the host) and kept in RAM, sector by sector.
 * What the PC writes goes to RAM only and stays there until the unit is
 * switched off or the program ends; the image itself is never written.
 * When the pool is full, sectors read but not written make room (a
 * clock over the pool); written ones are kept, and when nothing but
 * those is left a write fails as a disk error would.  See disk.h. */
#include <string.h>
#include "disk.h"

#define CHUNK 8 /* sectors read at once: 4 KiB, a cluster of the PC's FAT */

static struct {
    uint32_t sectors;
    disk_read_fn *read;
    uint8_t *pool;
    uint32_t slots;
    uint32_t *map;          /* sector -> slot + 1, 0: not in RAM */
    uint32_t *owner;        /* slot -> sector, ~0: free */
    uint8_t *flags;         /* slot: bit 0 written, bit 1 used lately */
    uint32_t hand, free_from;
    struct disk_stats stats;
} d;

static uint8_t chunk_buf[CHUNK * 512] __attribute__((aligned(32)));

uint32_t disk_map_bytes(uint32_t sectors)
{
    return sectors * 4;
}

uint32_t disk_slot_bytes(void)
{
    return 512 + 4 + 1;
}

int disk_init(uint32_t sectors, disk_read_fn *read, void *map, void *pool, uint32_t pool_bytes)
{
    uint32_t i;
    memset(&d, 0, sizeof d);
    d.sectors = sectors;
    d.read = read;
    d.map = map;
    d.slots = pool_bytes / disk_slot_bytes();
    if (d.slots < CHUNK * 4)
        return -1;
    d.pool = pool;
    d.owner = (uint32_t *)(d.pool + d.slots * 512);
    d.flags = (uint8_t *)(d.owner + d.slots);
    memset(d.map, 0, disk_map_bytes(sectors));
    for (i = 0; i < d.slots; i++)
        d.owner[i] = ~0u;
    memset(d.flags, 0, d.slots);
    return 0;
}

uint32_t disk_sectors(void)
{
    return d.sectors;
}

const struct disk_stats *disk_stats(void)
{
    return &d.stats;
}

/* A slot for a sector: a free one while there are, then the clock's. */
static int take_slot(uint32_t *slot)
{
    uint32_t tries;
    if (d.free_from < d.slots) {
        *slot = d.free_from++;
        return 0;
    }
    for (tries = 0; tries < 2 * d.slots; tries++) {
        uint32_t s = d.hand;
        d.hand = d.hand + 1 == d.slots ? 0 : d.hand + 1;
        if (d.flags[s] & 1)
            continue;               /* written: kept */
        if (d.flags[s] & 2) {
            d.flags[s] &= ~2;       /* a second chance */
            continue;
        }
        if (d.owner[s] != ~0u)
            d.map[d.owner[s]] = 0;
        d.owner[s] = ~0u;
        d.stats.evicted++;
        *slot = s;
        return 0;
    }
    d.stats.full++;
    return -1;
}

static void bind(uint32_t sector, uint32_t slot)
{
    d.owner[slot] = sector;
    d.map[sector] = slot + 1;
    d.flags[slot] = 2;
}

/* The chunk around a sector from the image, its sectors not in RAM yet
 * put there. */
static int fetch(uint32_t sector)
{
    uint32_t first = sector - sector % CHUNK, n = CHUNK, i;
    if (first + n > d.sectors)
        n = d.sectors - first;
    if (d.read((uint64_t)first * 512, chunk_buf, n * 512))
        return -1;
    d.stats.fetched += n;
    for (i = 0; i < n; i++) {
        uint32_t s = first + i, slot;
        if (d.map[s])
            continue;               /* (written meanwhile: newer) */
        if (take_slot(&slot))
            return s == sector ? -1 : 0;
        bind(s, slot);
        memcpy(d.pool + slot * 512, chunk_buf + i * 512, 512);
    }
    return 0;
}

int disk_read(uint64_t sector, uint8_t *buf, int n)
{
    for (; n > 0; n--, sector++, buf += 512) {
        uint32_t m;
        if (sector >= d.sectors)
            return -1;
        m = d.map[sector];
        if (!m) {
            d.stats.misses++;
            if (fetch((uint32_t)sector))
                return -1;
            m = d.map[sector];
            if (!m)
                return -1;
        } else {
            d.stats.hits++;
        }
        d.flags[m - 1] |= 2;
        memcpy(buf, d.pool + (m - 1) * 512, 512);
    }
    return 0;
}

int disk_write(uint64_t sector, const uint8_t *buf, int n)
{
    for (; n > 0; n--, sector++, buf += 512) {
        uint32_t m, slot;
        if (sector >= d.sectors)
            return -1;
        m = d.map[sector];
        if (m) {
            slot = m - 1;
            if (!(d.flags[slot] & 1))
                d.stats.written++;
        } else {
            if (take_slot(&slot))
                return -1;
            bind((uint32_t)sector, slot);
            d.stats.written++;
        }
        d.flags[slot] |= 3;
        memcpy(d.pool + slot * 512, buf, 512);
    }
    return 0;
}

/* The block device of Tiny386's IDE model (win311/patches: ide.c with
 * RNSE): one disk, whatever its name. */
int64_t rnse_disk_sectors(const char *filename)
{
    (void)filename;
    return d.sectors;
}

int rnse_disk_read(const char *filename, uint64_t sector, uint8_t *buf, int n)
{
    (void)filename;
    return disk_read(sector, buf, n);
}

int rnse_disk_write(const char *filename, uint64_t sector, const uint8_t *buf, int n)
{
    (void)filename;
    return disk_write(sector, buf, n);
}
