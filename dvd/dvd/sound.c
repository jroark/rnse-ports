/* Sound for the player: what the decoders give, at 48 kHz mostly, made
 * 44.1 kHz (the card processor's converter runs from a 22.5792 MHz
 * crystal; linear interpolation) and played by the SDK's PCM program
 * (sdk/src/pcm.c) on card processor 0.
 *
 * The processor holds two samples, so the DMA transfer that feeds it
 * must never stop.  One transfer covers a ring of 2M stereo samples
 * (47.5 s, written through P2 so that nothing has to be written back
 * from the cache); the player writes ahead of where it reads, and at
 * the ring's end the transfer is started again from the beginning.
 * sound_guard, called before every long piece of work, does that, and
 * waits for it when the end is less than 150 ms away, so that it is
 * never late by more than a few microseconds.
 *
 * What has been heard is known from the transfer's count, which makes
 * this the player's clock: time stamps of the samples written are kept
 * with their place in the ring. */
#include "player.h"

#define RING (1u << 21) /* frames */
#define AHEAD_MIN 2205  /* 50 ms of silence at least ahead of what plays */

int sound_ok;
static u32 *ring;
static u32 pass, written;
static int running;
static struct { u32 pos, pts; } marks[32];
static int nmarks;
/* The resampler: the last frame of the block before, the position. */
static s16 hist_l, hist_r;
static u32 phase;

static u32 played(void)
{
    u32 left;
    if (!running)
        return 0;
    left = pcm_dma_left();
    if (left)
        return pass * RING + RING - left / 2;
    return (pass + 1) * RING;
}

int sound_start(void)
{
    if (!ring) {
        u8 *m = malloc(RING * 4);
        if (!m)
            return -1;
        ring = (u32 *)(((u32)m & 0x1fffffff) | 0xa0000000);
    }
    if (pcm_start(0))
        return -2;
    memset(ring, 0, RING * 4);
    pass = 0;
    written = AHEAD_MIN;
    nmarks = 0;
    if (pcm_dma((const s16 *)ring, RING))
        return -3;
    running = 1;
    sound_ok = 1;
    return 0;
}

void sound_stop(void)
{
    if (running)
        pcm_stop();
    running = 0;
    sound_ok = 0;
}

void sound_reset(void)
{
    u32 p = played(), i;
    if (!running)
        return;
    /* What was written ahead, silenced. */
    for (i = p + 64; (s32)(written - i) > 0; i++)
        ring[i % RING] = 0;
    written = p + AHEAD_MIN;
    nmarks = 0;
    phase = 0;
    hist_l = hist_r = 0;
}

void sound_guard(void)
{
    u32 p, i;
    if (!running)
        return;
    if (pcm_dma_left() < 2 * 44100 * 15 / 100) {
        /* The end is near: wait for it and start again at once. */
        while (pcm_dma_busy())
            ;
        pass++;
        pcm_dma((const s16 *)ring, RING);
    }
    /* Nothing written for what is about to play: silence. */
    p = played();
    if ((s32)(written - (p + AHEAD_MIN)) < 0) {
        for (i = written; (s32)(p + AHEAD_MIN - i) > 0; i++)
            ring[i % RING] = 0;
        written = p + AHEAD_MIN;
    }
}

u32 sound_ahead(void)
{
    return running ? written - played() : 0;
}

void sound_write(const s16 *lr, u32 frames, int rate, u32 pts, int has_pts)
{
    u32 step, i;
    if (!running || !frames)
        return;
    if (has_pts) {
        if (nmarks == 32) {
            for (i = 0; i < 31; i++)
                marks[i] = marks[i + 1];
            nmarks--;
        }
        marks[nmarks].pos = written;
        marks[nmarks].pts = pts;
        nmarks++;
    }
    if (rate == 44100) {
        for (i = 0; i < frames; i++)
            ring[(written + i) % RING] = (u32)(u16)lr[2 * i] << 16 | (u16)lr[2 * i + 1];
        written += frames;
        return;
    }
    /* Positions in 1/65536 of an input frame; frame 0 is the last one
     * of the block before. */
    step = (u32)(((u32)rate << 16) / 44100);
    for (;;) {
        u32 k = phase >> 16, f = phase & 0xffff;
        s32 l0, r0, l1, r1, l, r;
        if (k >= frames)
            break;
        l0 = k ? lr[2 * (k - 1)] : hist_l;
        r0 = k ? lr[2 * (k - 1) + 1] : hist_r;
        l1 = lr[2 * k];
        r1 = lr[2 * k + 1];
        l = l0 + (((l1 - l0) * (s32)(f >> 1)) >> 15);
        r = r0 + (((r1 - r0) * (s32)(f >> 1)) >> 15);
        ring[written % RING] = (u32)(u16)l << 16 | (u16)r;
        written++;
        phase += step;
    }
    phase -= frames << 16;
    hist_l = lr[2 * (frames - 1)];
    hist_r = lr[2 * (frames - 1) + 1];
}

int sound_clock(u32 *pts)
{
    u32 p = played();
    int i, best = -1;
    if (!running)
        return 0;
    for (i = 0; i < nmarks; i++)
        if ((s32)(marks[i].pos - p) <= 0)
            best = i;
    if (best < 0)
        return 0;
    /* Older marks are not needed any more. */
    if (best > 0) {
        for (i = best; i < nmarks; i++)
            marks[i - best] = marks[i];
        nmarks -= best;
    }
    /* 90 kHz from 44.1 kHz: 300 / 147. */
    *pts = marks[0].pts + (p - marks[0].pos) * 300 / 147;
    return 1;
}
