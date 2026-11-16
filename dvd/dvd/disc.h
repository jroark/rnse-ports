/* The disc (disc.c) and the stream on it (stream.c). */
#ifndef DISC_H
#define DISC_H
#include "dvd.h"

#define DISC_MAX_TITLES 99
#define DISC_MAX_CELLS 600
#define DISC_MAX_CHAPTERS 99
#define DISC_NO_VIDEO -100   /* no VIDEO_TS.IFO: not a DVD-Video disc */
#define DISC_UNREADABLE -101

/* Sectors of 2048 bytes; 0 when read. */
extern int (*disc_read)(u32 sector, u32 count, void *buf);

struct disc_title {
    u32 ifo;          /* sector of the title set's IFO */
    u16 chapters;
    u8 vts, ttn;      /* title set, title within it */
    u32 seconds;      /* once disc_title has read it */
};
extern struct disc_title disc_titles[DISC_MAX_TITLES];
extern int disc_ntitles;

struct disc_cell {
    u32 first, last; /* sectors, the last one included */
    u32 at;          /* seconds from the title's start */
};
struct disc_playlist {
    int ncells, nchapters;
    struct disc_cell cells[DISC_MAX_CELLS];
    u16 chapter[DISC_MAX_CHAPTERS]; /* first cell of each */
    u32 seconds;
    u16 video;        /* video attributes: bits 13-12 0 NTSC 1 PAL, 11-10 aspect 0 4:3 3 16:9 */
    int naudio;
    struct {
        u8 coding;    /* 0 AC-3, 2 MPEG-1, 3 MPEG-2, 4 LPCM, 6 DTS */
        u8 channels;
        char lang[3];
    } audio[8];
    u16 audio_control[8]; /* by stream: bit 15 there, bits 12-8 its number */
};

/* The titles into disc_titles: their number, or DISC_*. */
int disc_open(void);
/* A title's cells (index into disc_titles): 0, or DISC_*. */
int disc_title(int index, struct disc_playlist *pl);

/* ---- stream.c: the program stream ---- */

/* An elementary stream as it comes out of the packs: bytes between
 * start and end, counted from the beginning of the stream (they wrap
 * after 4 GB, which is fine for differences), the time stamps that
 * came with them. */
struct es {
    u8 *buf;
    u32 size;   /* of buf */
    u32 base;   /* stream position of buf[0] */
    u32 start, end;
    struct { u32 pos, pts; } stamps[64];
    int nstamps;
};
extern struct es stream_video, stream_audio;

enum { AUDIO_NONE, AUDIO_AC3, AUDIO_MPA, AUDIO_LPCM, AUDIO_OTHER };
/* The audio stream that is played: private stream 1's substream
 * (0x80.. AC-3, 0xa0.. LPCM, 0x88.. DTS) or 0xc0..0xc7 (MPEG); -1:
 * the first that comes. */
extern int stream_audio_id;
extern int stream_audio_kind;
/* LPCM: bits a sample, rate, channels (from the stream). */
extern int stream_lpcm_bits, stream_lpcm_rate, stream_lpcm_channels;
/* Counted: packs with scrambled data (CSS), packs not understood. */
extern u32 stream_scrambled, stream_bad;

void stream_init(u8 *video_buf, u32 video_size, u8 *audio_buf, u32 audio_size);
void stream_reset(void);
/* One pack (a sector).  0, or 1 when its data is scrambled (and
 * nothing of it was taken). */
int stream_pack(const u8 *p);
/* Whether a pack has PES packets marked as scrambled. */
int stream_pack_scrambled(const u8 *p);
/* The next whole video unit (headers and one picture, see
 * video_decode) at *p; 0 when there is none yet (more packs wanted).
 * end_of_stream: what is left counts as whole. */
int stream_video_unit(const u8 **p, u32 *len, u32 *pts, int *has_pts, int end_of_stream);
void stream_video_consume(u32 len);
/* The next audio frame (AC-3, MPEG) or block of LPCM: its bytes and
 * time stamp; 0 when there is none yet. */
int stream_audio_frame(const u8 **p, u32 *len, u32 *pts, int *has_pts);
void stream_audio_consume(u32 len);
u32 es_level(const struct es *e);

#endif
