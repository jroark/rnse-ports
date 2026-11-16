/* The DVD player's parts, between themselves.  The decoders (video.c,
 * ac3.c, mpa.c) and the disc's structures (disc.c, stream.c) use
 * nothing of the kit, so that sdk/tests/dvd_host.c can build them on a
 * computer and compare what they decode with ffmpeg. */
#ifndef DVD_H
#define DVD_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;
typedef signed short s16;
typedef signed char s8;

void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
/* Zeroed memory that is kept (the program provides it): the decoders'
 * larger buffers are not in the program's own 128 KiB. */
void *dvd_alloc(unsigned long n);

/* ---- video.c: MPEG-1 and MPEG-2 video (main profile, 4:2:0) ----
 *
 * Pictures wider than 400 pixels are decoded at half their width
 * (720 -> 360): a 4x8 inverse transform of each block's lower half of
 * horizontal frequencies, motion vectors at a quarter of a stored
 * pixel.  The height is kept, so fields stay exact.  The references
 * drift a little from what the encoder had, within a group of
 * pictures; the screen has 400 pixels a line anyway. */
struct frame {
    u8 *y, *cb, *cr;
    u32 pts;      /* 90 kHz, valid when has_pts */
    int has_pts;
    u32 duration; /* 90 kHz */
    int type;     /* 1 I, 2 P, 3 B */
};

struct video_info {
    int width, height;  /* coded size (horizontal_size, vertical_size) */
    int stored_width;   /* luma samples a line in the frames: width or width / 2 */
    int stride;         /* bytes a luma line; chroma has half */
    int lines;          /* luma lines in the frames */
    int aspect;         /* aspect_ratio_information: 2 4:3, 3 16:9 */
    int rate;           /* frame_rate_code */
    int mpeg2;
    int progressive;    /* progressive_sequence */
};

/* Memory for the frames comes from alloc (three of at most 720x576). */
int video_init(void *(*alloc)(unsigned long));
void video_reset(void);  /* forget the references (after a jump) */
/* One unit of the elementary stream: from a start code on, the headers
 * that come before a picture, the picture and its slices.  `data` must
 * be followed by at least 8 bytes that can be read (zeroes).  skip_b:
 * leave B pictures out (nothing refers to them).  Returns a frame to
 * show now, in display order, or 0. */
struct frame *video_decode(const u8 *data, u32 len, u32 pts, int has_pts, int skip_b);
/* The last reference picture, at the end of the stream. */
struct frame *video_flush(void);
extern struct video_info video;
/* Counters: pictures decoded, B pictures left out, slices with errors. */
extern u32 video_pictures, video_skipped, video_errors;
/* What video_decode saw last: picture type (0 none). */
extern int video_last_type;
/* Set before the first picture: always at full width (for the tests). */
extern int video_full_width;

/* ---- audio: samples out, 16 bits, interleaved ---- */
#define AUDIO_MAX_FRAMES 1536 /* samples a channel in one decoded unit */

/* ac3.c: AC-3 (A/52), mixed down to two channels.  Frames start with
 * 0x0b77.  ac3_frame_size: bytes of the frame that starts at p (needs
 * 6 bytes), 0 when it is no frame.  ac3_decode: 1536 stereo samples to
 * out, their rate in *rate; the number of samples, or below 0. */
int ac3_frame_size(const u8 *p);
int ac3_decode(const u8 *frame, int len, s16 *out, int *rate);
void ac3_init(void);

/* mpa.c: MPEG-1 audio layers I and II (layer III is not on DVDs). */
int mpa_frame_size(const u8 *p);
int mpa_decode(const u8 *frame, int len, s16 *out, int *rate, int *channels);
void mpa_init(void);

#endif
