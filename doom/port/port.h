/* Between the parts of Doom's port to the RNS-E 192 (doom/port). */
#ifndef RNSE_DOOM_PORT_H
#define RNSE_DOOM_PORT_H

/* main.c: what every wait does (the faceplate, the display key, CD
 * EJECT, RETURN held, the sound kept fed), and the end of the game:
 * status 0 back to the loader at once, anything else after the error
 * (error_text, what went to stderr) has been on the screen. */
void port_idle(void);
void port_exit(int status) __attribute__((noreturn));

/* libc.c */
extern char error_text[1024];
void files_add_fixed(const char *name, void *data, long size);

/* i_sound.c: the mixer, from port_idle. */
void sound_pump(void);
void sound_stop(void);

/* i_input.c: the amplifier's volume, which the volume knob sets. */
extern int port_volume;

#endif
