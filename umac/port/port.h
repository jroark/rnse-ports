/* Between the parts of umac's port to the RNS-E 192 (umac/port). */
#ifndef RNSE_UMAC_PORT_H
#define RNSE_UMAC_PORT_H

/* main.c: port_idle (every wait: the faceplate, the display key, CD
 * EJECT, RETURN held) and port_exit (the end), as sdk/libc wants them. */
#include "rnse_libc.h"

/* Where the Mac's 384 x 235 sits on the 400 x 235 screen. */
#define MAC_X0 ((400 - DISP_WIDTH) / 2)

/* Palette entries this port uses. */
#define C_WHITE 0
#define C_BLACK 1
#define C_MARK 2 /* the sticky Command key's mark in the left margin */

/* video.c: the screen set up, and the Mac's frame buffer brought over
 * where it changed (all of it with full set). */
void video_init(void);
void video_update(int full);
/* The mark for an armed Command key, on or off. */
void video_mark(int on);

/* input.c: the faceplate read and handed to the Mac, once a pass. */
void input_poll(void);

#endif
