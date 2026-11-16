/* The PC's hard disk, kept in RAM: disk.c. */
#ifndef WIN311_DISK_H
#define WIN311_DISK_H
#include <stdint.h>

/* Bytes of the image from offset into buf: 0, or not 0 when it failed. */
typedef int disk_read_fn(uint64_t offset, void *buf, uint32_t n);

/* The memory disk_init wants: a map of 4 bytes a sector, and a pool of
 * disk_slot_bytes() a sector kept (the more, the less is read again). */
uint32_t disk_map_bytes(uint32_t sectors);
uint32_t disk_slot_bytes(void);
int disk_init(uint32_t sectors, disk_read_fn *read, void *map, void *pool, uint32_t pool_bytes);
uint32_t disk_sectors(void);
int disk_read(uint64_t sector, uint8_t *buf, int n);
int disk_write(uint64_t sector, const uint8_t *buf, int n);

struct disk_stats {
    uint32_t hits, misses, fetched, written, evicted, full;
};
const struct disk_stats *disk_stats(void);

#endif
