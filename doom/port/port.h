/* Between the parts of Doom's port to the RNS-E 192 (doom/port). */
#ifndef RNSE_DOOM_PORT_H
#define RNSE_DOOM_PORT_H

/* main.c: what every wait does (the faceplate, the display key, CD
 * EJECT, RETURN held, the sound kept fed), and the end of the game:
 * status 0 back to the loader at once, anything else after the error
 * (error_text, what went to stderr) has been on the screen.  These and
 * the C library's own (error_text, files_add_fixed) are declared in
 * sdk/libc/rnse_libc.h. */
#include "rnse_libc.h"

/* i_sound.c: the mixer, from port_idle. */
void sound_pump(void);
void sound_stop(void);

/* i_input.c: the amplifier's volume, which the volume knob sets. */
extern int port_volume;

#endif
