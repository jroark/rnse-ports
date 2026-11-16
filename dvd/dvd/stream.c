/* The program stream of a DVD (ISO/IEC 13818-1): packs of one sector
 * each, PES packets in them, and out of those the video and one audio
 * stream as elementary streams with their time stamps.  Navigation
 * packs (private stream 2) and subpictures are passed over. */
#include "disc.h"

struct es stream_video, stream_audio;
int stream_audio_id = -1, stream_audio_kind;
int stream_lpcm_bits, stream_lpcm_rate, stream_lpcm_channels;
u32 stream_scrambled, stream_bad;

/* The video unit being looked for. */
static u32 vscan;
static int vseen;
static u32 vpic;

u32 es_level(const struct es *e)
{
    return e->end - e->start;
}

/* memcpy towards lower addresses, overlapping. */
static void move_down(void *to, const void *from, u32 n)
{
    u8 *d = to;
    const u8 *s = from;
    while (n--)
        *d++ = *s++;
}

static void es_clear(struct es *e)
{
    e->base = e->start = e->end = 0;
    e->nstamps = 0;
    memset(e->buf, 0, 16);
}

void stream_init(u8 *video_buf, u32 video_size, u8 *audio_buf, u32 audio_size)
{
    stream_video.buf = video_buf;
    stream_video.size = video_size;
    stream_audio.buf = audio_buf;
    stream_audio.size = audio_size;
    stream_reset();
}

void stream_reset(void)
{
    es_clear(&stream_video);
    es_clear(&stream_audio);
    vscan = 0;
    vseen = 0;
    stream_scrambled = 0;
}

/* Bytes in, compacting the buffer when they would not fit behind what
 * is there; 16 zero bytes always follow the end. */
static void es_put(struct es *e, const u8 *d, u32 n, int has_pts, u32 pts)
{
    u32 used = e->end - e->base;
    if (used + n + 16 > e->size) {
        u32 keep = e->end - e->start, gap = e->start - e->base;
        /* (The player keeps the buffers far fuller than it lets them
         * get, so that this is rare and does not overlap.) */
        if (gap >= keep)
            memcpy(e->buf, e->buf + gap, keep);
        else
            move_down(e->buf, e->buf + gap, keep);
        e->base = e->start;
        used = keep;
        if (used + n + 16 > e->size)
            return; /* nobody takes the data out: dropped */
    }
    if (has_pts) {
        if (e->nstamps == 64) {
            move_down(e->stamps, e->stamps + 1, sizeof e->stamps[0] * 63);
            e->nstamps--;
        }
        e->stamps[e->nstamps].pos = e->end;
        e->stamps[e->nstamps].pts = pts;
        e->nstamps++;
    }
    memcpy(e->buf + used, d, n);
    e->end += n;
    memset(e->buf + used + n, 0, 16);
}

/* The stamp for something that starts at pos: the last of those at or
 * before it, which are used up. */
static int stamp(struct es *e, u32 pos, u32 *pts)
{
    int i = 0, found = 0;
    while (i < e->nstamps && (s32)(e->stamps[i].pos - pos) <= 0) {
        *pts = e->stamps[i].pts;
        found = 1;
        i++;
    }
    if (i) {
        move_down(e->stamps, e->stamps + i, sizeof e->stamps[0] * (unsigned)(e->nstamps - i));
        e->nstamps -= i;
    }
    return found;
}

static int audio_kind_of(int id)
{
    if (id >= 0x80 && id <= 0x87)
        return AUDIO_AC3;
    if (id >= 0xa0 && id <= 0xa7)
        return AUDIO_LPCM;
    if (id >= 0xc0 && id <= 0xc7)
        return AUDIO_MPA;
    return AUDIO_OTHER;
}

static void audio_payload(int id, const u8 *d, u32 n, int has_pts, u32 pts)
{
    int kind = audio_kind_of(id);
    if (kind == AUDIO_OTHER)
        return;
    if (stream_audio_id < 0) {
        stream_audio_id = id;
        stream_audio_kind = kind;
    }
    if (id != stream_audio_id)
        return;
    stream_audio_kind = kind;
    es_put(&stream_audio, d, n, has_pts, pts);
}

int stream_pack_scrambled(const u8 *p)
{
    u32 o;
    if (p[0] || p[1] || p[2] != 1 || p[3] != 0xba || (p[4] & 0xc0) != 0x40)
        return 0;
    for (o = 14 + (p[13] & 7); o + 9 <= 2048 && !p[o] && !p[o + 1] && p[o + 2] == 1;
         o += 6 + ((u32)p[o + 4] << 8 | p[o + 5]))
        if ((p[o + 6] & 0xc0) == 0x80 && (p[o + 6] & 0x30))
            return 1;
    return 0;
}

int stream_pack(const u8 *p)
{
    u32 o;
    if (stream_pack_scrambled(p)) {
        stream_scrambled++;
        return 1;
    }
    if (p[0] || p[1] || p[2] != 1 || p[3] != 0xba) {
        stream_bad++;
        return 0;
    }
    if ((p[4] & 0xc0) == 0x40)
        o = 14 + (p[13] & 7);
    else if ((p[4] & 0xf0) == 0x20)
        o = 12;
    else {
        stream_bad++;
        return 0;
    }
    while (o + 6 <= 2048) {
        u32 sid, len, pl, next, data, pts = 0;
        int has_pts = 0;
        if (p[o] || p[o + 1] || p[o + 2] != 1)
            break;
        sid = p[o + 3];
        if (sid == 0xb9)
            break;
        len = (u32)p[o + 4] << 8 | p[o + 5];
        pl = o + 6;
        next = pl + len;
        if (next > 2048)
            next = 2048;
        o = next;
        if (sid != 0xbd && sid != 0xe0 && (sid & 0xf8) != 0xc0)
            continue;
        if ((p[pl] & 0xc0) == 0x80) {
            /* MPEG-2 PES header. */
            if (p[pl] & 0x30) {
                stream_scrambled++;
                return 1;
            }
            if (p[pl + 1] & 0x80) {
                const u8 *t = p + pl + 3;
                pts = (u32)((t[0] >> 1) & 3) << 30 | (u32)t[1] << 22 | (u32)(t[2] >> 1) << 15 | (u32)t[3] << 7 | t[4] >> 1;
                has_pts = 1;
            }
            data = pl + 3 + p[pl + 2];
        } else {
            /* MPEG-1. */
            data = pl;
            while (data < next && p[data] == 0xff)
                data++;
            if (data < next && (p[data] & 0xc0) == 0x40)
                data += 2;
            if (data < next && (p[data] & 0xe0) == 0x20) {
                const u8 *t = p + data;
                pts = (u32)((t[0] >> 1) & 3) << 30 | (u32)t[1] << 22 | (u32)(t[2] >> 1) << 15 | (u32)t[3] << 7 | t[4] >> 1;
                has_pts = 1;
                data += (p[data] & 0xf0) == 0x30 ? 10 : 5;
            } else {
                data++;
            }
        }
        if (data >= next)
            continue;
        if (sid == 0xe0) {
            es_put(&stream_video, p + data, next - data, has_pts, pts);
        } else if (sid == 0xbd) {
            int sub = p[data];
            if (sub >= 0x80 && sub <= 0x8f && data + 4 <= next) {
                audio_payload(sub, p + data + 4, next - data - 4, has_pts, pts);
            } else if (sub >= 0xa0 && sub <= 0xa7 && data + 7 <= next) {
                u8 info = p[data + 5];
                if (stream_audio_id < 0 || stream_audio_id == sub) {
                    stream_lpcm_bits = 16 + 4 * (info >> 6);
                    stream_lpcm_rate = (info & 0x30) ? 96000 : 48000;
                    stream_lpcm_channels = (info & 7) + 1;
                }
                audio_payload(sub, p + data + 7, next - data - 7, has_pts, pts);
            }
        } else {
            audio_payload((int)sid, p + data, next - data, has_pts, pts);
        }
    }
    return 0;
}

int stream_video_unit(const u8 **pp, u32 *len, u32 *pts, int *has_pts, int end_of_stream)
{
    struct es *e = &stream_video;
    const u8 *b = e->buf - e->base; /* b[pos] for a stream position */
    u32 boundary = 0;
    int found = 0;
    if (vscan - e->start > e->end - e->start)
        vscan = e->start, vseen = 0;
    while ((s32)(e->end - vscan) >= 4) {
        if (b[vscan] == 0 && b[vscan + 1] == 0 && b[vscan + 2] == 1) {
            u8 c = b[vscan + 3];
            if (c == 0x00 || c == 0xb3 || c == 0xb8 || c == 0xb7) {
                if (vseen) {
                    boundary = vscan;
                    found = 1;
                    break;
                }
                if (c == 0x00) {
                    vseen = 1;
                    vpic = vscan;
                }
            }
            vscan += 3;
        } else if (b[vscan + 2] > 1) {
            vscan += 3; /* no start code begins in these three */
        } else {
            vscan++;
        }
    }
    if (!found) {
        if (!end_of_stream || !vseen || e->end == e->start)
            return 0;
        boundary = e->end;
    }
    *pp = b + e->start;
    *len = boundary - e->start;
    *has_pts = stamp(e, vpic, pts);
    return 1;
}

void stream_video_consume(u32 len)
{
    stream_video.start += len;
    vscan = stream_video.start;
    vseen = 0;
}

int stream_audio_frame(const u8 **pp, u32 *len, u32 *pts, int *has_pts)
{
    struct es *e = &stream_audio;
    const u8 *b = e->buf - e->base;
    u32 n = 0;
    for (;;) {
        u32 at = e->start, left = e->end - at;
        if (left < 8)
            return 0;
        if (stream_audio_kind == AUDIO_LPCM) {
            /* Ten milliseconds of whole samples. */
            u32 frame = (u32)stream_lpcm_channels * ((u32)stream_lpcm_bits / 8) * 2;
            u32 want = frame * (u32)(stream_lpcm_rate / 200);
            if (stream_lpcm_bits != 16)
                want = (u32)stream_lpcm_channels * (u32)stream_lpcm_bits * 2 / 8 * (u32)(stream_lpcm_rate / 200);
            if (!want || left < want)
                return 0;
            n = want;
            break;
        }
        n = stream_audio_kind == AUDIO_AC3 ? (u32)ac3_frame_size(b + at) :
            stream_audio_kind == AUDIO_MPA ? (u32)mpa_frame_size(b + at) : 0;
        if (n) {
            if (left < n)
                return 0;
            break;
        }
        e->start++; /* not at a frame: look on */
    }
    *pp = b + e->start;
    *len = n;
    *has_pts = stamp(e, e->start, pts);
    return 1;
}

void stream_audio_consume(u32 len)
{
    stream_audio.start += len;
}
