/* The player's parts that run on the unit (drive.c, sound.c, screen.c,
 * main.c). */
#ifndef PLAYER_H
#define PLAYER_H
#include "rnse.h"
#include "disc.h"

/* drive.c: reads that go on while the player decodes. */
extern u32 drive_errors, drive_sectors, drive_skipped;
void drive_init(u8 *buf, u32 sectors);
void drive_seek(const struct disc_playlist *pl, int cell);
void drive_stop(void);
/* Take what the drive has ready (at most budget sectors) and ask for
 * more when there is room. */
void drive_service(u32 budget);
u32 drive_level(void);       /* sectors waiting in the ring */
int drive_finished(void);    /* the playlist read to its end */
const u8 *drive_sector(int *cell); /* the oldest waiting, or 0 */
void drive_consume(void);

/* sound.c: samples at 44.1 kHz to a card processor, by one long DMA
 * transfer that is written ahead of. */
int sound_start(void);
void sound_stop(void);
void sound_reset(void);      /* everything not yet heard dropped */
/* Before anything that takes long: restarts the transfer at its end,
 * waiting for that if it is near. */
void sound_guard(void);
/* Samples (stereo, at `rate`) to be heard after those before them;
 * pts: the time stamp of the first, if has_pts. */
void sound_write(const s16 *lr, u32 frames, int rate, u32 pts, int has_pts);
u32 sound_ahead(void);       /* frames written and not yet heard */
/* The time stamp being heard now (90 kHz); 0 when there is none. */
int sound_clock(u32 *pts);
extern int sound_ok;

/* scramble.c: where a descrambler goes (none here; see that file and
 * docs/dvd.md).  0 from scramble_open: scrambled sectors are handed to
 * scramble_sector, which unscrambles one in place. */
int scramble_open(const struct disc_playlist *pl, u32 ifo);
int scramble_sector(u8 *sector);

/* screen.c */
enum { SCREEN_MENU, SCREEN_VIDEO16, SCREEN_VIDEO8 };
void screen_init(void);
void screen_mode(int mode);
extern int screen_current;
/* A frame onto the screen (the back surface, then shown), with the
 * lines of the on-screen display over it (0 for none). */
void screen_video(const struct frame *f, const char *osd1, const char *osd2);
/* 3-3-2 colours of the menu's palette. */
#define C_BLACK 0x00
#define C_WHITE 0xff
#define C_GREY 0x92
#define C_BLUE 0x07
#define C_YELLOW 0xfc
#define C_RED 0xe0

#endif
