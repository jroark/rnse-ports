/* The few POSIX calls Quake makes outside its Sys_ layer (fcntl.h):
 * none of them has a file to go to on the RNS-E. */
#include <fcntl.h>

int open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    return -1;
}

int close(int fd)
{
    (void)fd;
    return -1;
}

int write(int fd, const void *buf, unsigned long n)
{
    (void)fd;
    (void)buf;
    (void)n;
    return -1;
}

int read(int fd, void *buf, unsigned long n)
{
    (void)fd;
    (void)buf;
    (void)n;
    return -1;
}

/* rand as C's own example has it: the game's effects want nothing
 * better. */
static unsigned long next = 1;

int rand(void)
{
    next = next * 1103515245 + 12345;
    return (int)((next / 65536) % 32768);
}

void srand(unsigned seed)
{
    next = seed;
}
