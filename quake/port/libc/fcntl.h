/* Quake's console log (Con_DebugLog, with -condebug) opens a file this
 * way; there is no such file system here, so open fails (posix.c). */
#ifndef RNSE_QUAKE_FCNTL_H
#define RNSE_QUAKE_FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_APPEND 0x400
int open(const char *path, int flags, ...);
int close(int fd);
int write(int fd, const void *buf, unsigned long n);
int read(int fd, void *buf, unsigned long n);
#endif
