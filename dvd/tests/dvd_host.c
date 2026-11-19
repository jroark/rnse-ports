/* Host tests of the DVD player's decoders (dvd/dvd): what they
 * decode against what ffmpeg decodes.  dvd/check.sh makes the
 * test material with ffmpeg and runs this.
 *
 *   dvd_host video STREAM.m2v REF.yuv [full]
 *       every frame against ffmpeg's (yuv420p at the coded size), the
 *       reference taken to half width the way the decoder keeps it;
 *       "full" decodes at full width.  Prints the PSNR of the luma.
 *   dvd_host audio KIND STREAM REF.s16 [CHANNELS]
 *       KIND ac3 or mpa: the elementary stream decoded and compared
 *       with ffmpeg's stereo 16-bit samples (signal to noise in dB).
 */
#include "../examples/dvd/disc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *alloc(unsigned long n)
{
    return calloc(1, n);
}

void *dvd_alloc(unsigned long n)
{
    return calloc(1, n);
}

static u8 *slurp(const char *name, long *size)
{
    FILE *f = fopen(name, "rb");
    u8 *d;
    if (!f) {
        perror(name);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = calloc(1, (size_t)*size + 64);
    if (fread(d, 1, (size_t)*size, f) != (size_t)*size)
        exit(2);
    fclose(f);
    return d;
}

static FILE *ref;
static int frames_seen, worst_frame = -1;
static double worst = 1e9, total;
static u8 *refbuf;

static void compare(const struct frame *f)
{
    int w = video.width, h = video.height, sw = video.stored_width, x, y;
    size_t n = (size_t)w * h * 3 / 2;
    double se = 0, psnr;
    if (fread(refbuf, 1, n, ref) != n) {
        printf("frame %d: no reference left\n", frames_seen);
        exit(1);
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < sw; x++) {
            int r = sw == w ? refbuf[y * w + x] : (refbuf[y * w + 2 * x] + refbuf[y * w + 2 * x + 1] + 1) / 2;
            int d = r - f->y[y * video.stride + x];
            se += d * d;
        }
    se /= (double)sw * h;
    psnr = se ? 10 * log10(255.0 * 255.0 / se) : 99;
    if (psnr < worst)
        worst = psnr, worst_frame = frames_seen;
    total += psnr;
    if (getenv("DUMP") && atoi(getenv("DUMP")) == frames_seen) {
        FILE *o = fopen("/tmp/claude-0/frame.pgm", "wb");
        fprintf(o, "P5 %d %d 255\n", sw, 2 * h);
        for (y = 0; y < h; y++)
            fwrite(f->y + y * video.stride, 1, (size_t)sw, o);
        for (y = 0; y < h; y++)
            for (x = 0; x < sw; x++)
                fputc(sw == w ? refbuf[y * w + x] : (refbuf[y * w + 2 * x] + refbuf[y * w + 2 * x + 1] + 1) / 2, o);
        fclose(o);
    }
    if (getenv("VERBOSE"))
        printf("frame %d type %d psnr %.2f picture %u\n", frames_seen, f->type, psnr, video_pictures);
    frames_seen++;
}

static int video_test(int argc, char **argv)
{
    long size;
    u8 *es = slurp(argv[2], &size);
    long i, start = 0;
    int seen_picture = 0;
    struct frame *f;
    ref = fopen(argv[3], "rb");
    if (!ref) {
        perror(argv[3]);
        return 2;
    }
    refbuf = malloc(720 * 576 * 2);
    video_full_width = argc > 4 && !strcmp(argv[4], "full");
    if (video_init(alloc))
        return 2;
    /* Units: a picture with the headers before it. */
    for (i = 0; i + 4 <= size; i++) {
        int code;
        if (es[i] || es[i + 1] || es[i + 2] != 1)
            continue;
        code = es[i + 3];
        if ((code == 0xb3 || code == 0xb8 || code == 0x00) && seen_picture) {
            u8 *unit = malloc((size_t)(i - start) + 16);
            memcpy(unit, es + start, (size_t)(i - start));
            memset(unit + (i - start), 0, 16);
            f = video_decode(unit, (u32)(i - start), 0, 0, 0);
            if (f)
                compare(f);
            free(unit);
            start = i;
            seen_picture = 0;
        }
        if (code == 0x00)
            seen_picture = 1;
    }
    f = video_decode(es + start, (u32)(size - start), 0, 0, 0);
    if (f)
        compare(f);
    if ((f = video_flush()))
        compare(f);
    printf("%s: %dx%d stored %d, mpeg%d, %d frames, %u errors, %u skipped; PSNR average %.2f, worst %.2f (frame %d)\n",
           argv[2], video.width, video.height, video.stored_width, video.mpeg2 ? 2 : 1, frames_seen,
           video_errors, video_skipped, frames_seen ? total / frames_seen : 0, worst, worst_frame);
    /* At full width only the transform's rounding is different, and
     * macroblocks left out in B pictures after field prediction, which
     * ffmpeg predicts from the top field's vector alone (see video.c):
     * B pictures are no references, so that stays where it is.  At
     * half width the references drift within a group of pictures. */
    return frames_seen > 0 && !video_errors &&
           (video_full_width ? total / frames_seen > 55 && worst > 38 : total / frames_seen > 33 && worst > 28) ? 0 : 1;
}

static int audio_test(int argc, char **argv)
{
    long size, rsize, i = 0;
    u8 *es = slurp(argv[3], &size), *refb = slurp(argv[4], &rsize);
    const s16 *refs = (const s16 *)refb;
    long nref = rsize / 2, at = 0;
    int ac3 = !strcmp(argv[2], "ac3"), frames = 0, bad = 0, rate = 0, channels = 2;
    double se = 0, sr = 0, so = 0, cross = 0;
    static s16 out[2 * 1152 * 2 + 2 * 1536];
    if (ac3)
        ac3_init();
    else
        mpa_init();
    while (i + 8 <= size) {
        int n = ac3 ? ac3_frame_size(es + i) : mpa_frame_size(es + i), got, k;
        if (!n) {
            i++;
            continue;
        }
        if (i + n > size)
            break;
        got = ac3 ? ac3_decode(es + i, n, out, &rate) : mpa_decode(es + i, n, out, &rate, &channels);
        if (got < 0) {
            bad++;
            got = 0;
        }
        for (k = 0; k < 2 * got && at < nref; k++, at++) {
            double r = refs[at], o = out[k];
            sr += r * r;
            so += o * o;
            cross += r * o;
        }
        frames++;
        i += n;
    }
    /* The gain that fits best, and what is left over. */
    {
        double g = so ? cross / so : 0;
        long k;
        at = 0;
        se = sr - 2 * g * cross + g * g * so;
        (void)k;
        printf("%s: %d frames (%d bad), %d Hz; gain %.4f, SNR %.1f dB\n", argv[3], frames, bad, rate,
               g, se > 0 ? 10 * log10(sr / se) : 99.0);
        return frames && !bad && g > 0.5 && g < 2 && (se <= 0 || 10 * log10(sr / se) > (argc > 5 ? atof(argv[5]) : 40)) ? 0 : 1;
    }
}

static FILE *iso;

static int read_iso(u32 sector, u32 count, void *buf)
{
    fseek(iso, (long)sector * 2048, SEEK_SET);
    return fread(buf, 2048, count, iso) == count ? 0 : -1;
}

/* The disc as the player sees it: titles, chapters, and each title
 * played through the demultiplexer and the decoders. */
static int disc_test(char **argv)
{
    static struct disc_playlist pl;
    static u8 vbuf[4 << 20], abuf[1 << 20], sector[2048];
    static s16 pcm[2 * 1536];
    int n, t, failed = 0;
    iso = fopen(argv[2], "rb");
    if (!iso) {
        perror(argv[2]);
        return 2;
    }
    disc_read = read_iso;
    n = disc_open();
    printf("%s: %d titles\n", argv[2], n);
    if (n <= 0)
        return 1;
    video_init(alloc);
    ac3_init();
    mpa_init();
    stream_init(vbuf, sizeof vbuf, abuf, sizeof abuf);
    for (t = 0; t < n; t++) {
        int c, frames = 0, audio = 0, r, rate = 0, ch = 2, i;
        u32 first_pts = 0, last_pts = 0;
        int got_pts = 0;
        r = disc_title(t, &pl);
        printf("title %d: VTS %d TTN %d, %d cells, %d chapters (", t + 1, disc_titles[t].vts, disc_titles[t].ttn, pl.ncells, pl.nchapters);
        for (i = 0; i < pl.nchapters; i++)
            printf("%s%d", i ? " " : "", pl.chapter[i]);
        printf("), %u s, %d audio (coding %d), video %04x: %d\n", pl.seconds, pl.naudio, pl.audio[0].coding, pl.video, r);
        if (r) {
            failed = 1;
            continue;
        }
        stream_reset();
        stream_audio_id = -1;
        video_reset();
        for (c = 0; c < pl.ncells; c++) {
            u32 s;
            for (s = pl.cells[c].first; s <= pl.cells[c].last; s++) {
                const u8 *p;
                u32 len, pts;
                int has;
                if (read_iso(s, 1, sector))
                    return 1;
                stream_pack(sector);
                while (stream_video_unit(&p, &len, &pts, &has, 0)) {
                    if (video_decode(p, len, pts, has, 0))
                        frames++;
                    stream_video_consume(len);
                }
                while (stream_audio_frame(&p, &len, &pts, &has)) {
                    if (has) {
                        if (!got_pts)
                            first_pts = pts, got_pts = 1;
                        last_pts = pts;
                    }
                    if (stream_audio_kind == AUDIO_AC3 && ac3_decode(p, (int)len, pcm, &rate) > 0)
                        audio++;
                    else if (stream_audio_kind == AUDIO_MPA && mpa_decode(p, (int)len, pcm, &rate, &ch) > 0)
                        audio++;
                    else if (stream_audio_kind == AUDIO_LPCM)
                        audio++;
                    stream_audio_consume(len);
                }
            }
        }
        {
            const u8 *p;
            u32 len, pts;
            int has;
            while (stream_video_unit(&p, &len, &pts, &has, 1)) {
                if (video_decode(p, len, pts, has, 0))
                    frames++;
                stream_video_consume(len);
            }
            if (video_flush())
                frames++;
        }
        printf("  %d frames %dx%d, %d audio units (kind %d, stream %02x), audio pts %.2f s, scrambled %u\n", frames,
               video.width, video.height, audio, stream_audio_kind, stream_audio_id, (last_pts - first_pts) / 90000.0, stream_scrambled);
        if (!frames || !audio)
            failed = 1;
    }
    return failed;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "disc"))
        return disc_test(argv);
    if (argc >= 5 && !strcmp(argv[1], "audio"))
        return audio_test(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "video"))
        return video_test(argc, argv);
    fprintf(stderr, "usage: see the comment at the top\n");
    return 2;
}
