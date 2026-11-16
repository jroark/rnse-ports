/* Between the parts of Quake's port to the RNS-E 192 (quake/port), and
 * what Doom's C library (doom/port/libc, which the game shares) wants of
 * a port. */
#ifndef RNSE_QUAKE_PORT_H
#define RNSE_QUAKE_PORT_H

/* sys_rnse.c: what every wait does (the faceplate, the display key, CD
 * EJECT, RETURN held), and the end of the game: status 0 back to the
 * loader at once, anything else after the error (error_text, what went
 * to stderr) has been on the screen. */
void port_idle(void);
void port_exit(int status) __attribute__((noreturn));
/* The card's slot (handoff), and the sound's processor: the other. */
extern int port_slot;

/* doom/port/libc/libc.c */
extern char error_text[1024];
void files_add_fixed(const char *name, void *data, long size);
void files_add_reader(const char *name, long size, long (*read_at)(long at, void *buf, long n));

/* in_rnse.c: the amplifier's volume, which the volume knob sets. */
extern int port_volume;

#endif
