/* Doom on the RNS-E 192: sound effects.  doomgeneric's i_sound.c picks
 * this module (DG_sound_module) for its Sound Blaster; there is no
 * music (DG_music_module does nothing).
 *
 * The effects are the WAD's DMX lumps: unsigned 8-bit samples, mostly
 * at 11025 Hz.  Up to 8 play at once, each stepped at its own rate to
 * 44.1 kHz and mixed, with Doom's volume and stereo separation, into
 * blocks of a ring that the DMA controller plays out of a card
 * processor's converter without gaps (pcm_stream, sdk/src/pcm.c), which
 * the amplifier hears as RADIO_DECODER.  The mixing happens in the
 * game's waits and after each frame (port_idle, sound_pump); 8 blocks
 * of 512 samples are 93 ms of slack for a slow frame.
 *
 * The card processor is the first one by default; `-dsp 1` in DOOM.ARG
 * takes the other (which of the two reaches the amplifier on a unit is
 * to be heard: sdk/examples/pcmtest). */
#include <string.h>
#include "rnse.h"
#include "port.h"

#include "doomtype.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"

#define CHANNELS 8
#define RATE 44100
#define BLOCK 512
#define BLOCKS 8

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static s16 ring[2 * BLOCK * BLOCKS] __attribute__((aligned(32)));
static int started, use_prefix;

static struct channel {
    const u8 *data;     /* the samples */
    u32 length;         /* how many, in 16.16 */
    u32 at, step;       /* where, and how far a sample of ours, in 16.16 */
    int left, right;    /* 0..255 */
} channel[CHANNELS];

struct sound {
    const u8 *data;
    u32 length, rate;
};

/* ---- The module ------------------------------------------------------- */

static boolean sound_init(boolean use_sfx_prefix)
{
    int dsp = 0, i = M_CheckParmWithArgs("-dsp", 1);
    if (i)
        dsp = myargv[i + 1][0] == '1';
    use_prefix = use_sfx_prefix;
    memset(channel, 0, sizeof channel);
    if (pcm_start(dsp)) {
        printf("sound: card processor %d did not take its program\n", dsp);
        return false;
    }
    if (radio_source(RADIO_DECODER, port_volume))
        printf("sound: the amplifier did not answer\n");
    if (pcm_stream(ring, BLOCK, BLOCKS)) {
        pcm_stop();
        return false;
    }
    started = 1;
    printf("sound: card processor %d, %d Hz\n", dsp, RATE);
    return true;
}

void sound_stop(void)
{
    if (started) {
        started = 0;
        pcm_stop();
    }
}

static void sound_shutdown(void)
{
    sound_stop();
}

static int sound_lump(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link)
        sfx = sfx->link;
    if (use_prefix)
        M_snprintf(name, sizeof name, "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(name, DEH_String(sfx->name), sizeof name);
    return W_GetNumForName(name);
}

/* The lump checked and kept (PU_STATIC: the shareware's effects are
 * some 400 KB), as Chocolate Doom's own module does. */
static struct sound *sound_cache(sfxinfo_t *sfx)
{
    struct sound *s = sfx->driver_data;
    const byte *data;
    u32 lumplen, length;
    if (s)
        return s->data ? s : 0;
    s = Z_Malloc(sizeof *s, PU_STATIC, 0);
    memset(s, 0, sizeof *s);
    sfx->driver_data = s;
    data = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    lumplen = (u32)W_LumpLength(sfx->lumpnum);
    if (lumplen < 8 || data[0] != 3 || data[1] != 0)
        return 0;
    length = (u32)data[7] << 24 | (u32)data[6] << 16 | (u32)data[5] << 8 | data[4];
    if (length > lumplen - 8 || length <= 48)
        return 0;
    /* (DMX leaves out the first and last 16 samples.) */
    s->data = data + 8 + 16;
    s->length = length - 32;
    s->rate = (u32)data[3] << 8 | data[2];
    if (!s->rate)
        s->rate = 11025;
    return s;
}

static void sound_params(int c, int vol, int sep)
{
    int left = ((254 - sep) * vol) / 127, right = (sep * vol) / 127;
    if (c < 0 || c >= CHANNELS)
        return;
    channel[c].left = left < 0 ? 0 : left > 255 ? 255 : left;
    channel[c].right = right < 0 ? 0 : right > 255 ? 255 : right;
}

static int sound_start(sfxinfo_t *sfx, int c, int vol, int sep)
{
    struct sound *s;
    if (!started || c < 0 || c >= CHANNELS)
        return -1;
    s = sound_cache(sfx);
    channel[c].data = 0;
    if (!s)
        return -1;
    sound_params(c, vol, sep);
    channel[c].length = s->length << 16;
    channel[c].step = (s->rate << 16) / RATE;
    channel[c].at = 0;
    channel[c].data = s->data;
    return c;
}

static void sound_stop_channel(int c)
{
    if (c >= 0 && c < CHANNELS)
        channel[c].data = 0;
}

static boolean sound_playing(int c)
{
    return c >= 0 && c < CHANNELS && channel[c].data != 0;
}

static void sound_update(void)
{
    sound_pump();
}

static void sound_precache(sfxinfo_t *sounds, int n)
{
    (void)sounds;
    (void)n;
}

/* ---- The mixer -------------------------------------------------------- */

static void mix(s16 *out)
{
    static int l[BLOCK], r[BLOCK];
    int i, c;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    for (c = 0; c < CHANNELS; c++) {
        struct channel *ch = &channel[c];
        const u8 *data = ch->data;
        u32 at = ch->at, step = ch->step, length = ch->length;
        int lv = ch->left, rv = ch->right;
        if (!data)
            continue;
        for (i = 0; i < BLOCK && at < length; i++, at += step) {
            int v = (int)data[at >> 16] - 128;
            l[i] += v * lv;
            r[i] += v * rv;
        }
        ch->at = at;
        if (at >= length)
            ch->data = 0;
    }
    for (i = 0; i < BLOCK; i++) {
        /* 8-bit samples by volumes of up to 255: 16 bits, less a bit
         * of headroom for several at once. */
        int a = l[i] >> 1, b = r[i] >> 1;
        out[2 * i] = (s16)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        out[2 * i + 1] = (s16)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }
}

void sound_pump(void)
{
    s16 *block;
    if (!started)
        return;
    while ((block = pcm_stream_block())) {
        mix(block);
        pcm_stream_ready();
    }
}

static snddevice_t devices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                 SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32 };

sound_module_t DG_sound_module = {
    devices,
    sizeof devices / sizeof devices[0],
    sound_init,
    sound_shutdown,
    sound_lump,
    sound_update,
    sound_params,
    sound_start,
    sound_stop_channel,
    sound_playing,
    sound_precache,
};

/* ---- No music --------------------------------------------------------- */

static boolean music_init(void) { return false; }
static void music_nothing(void) { }
static void music_volume(int v) { (void)v; }
static void *music_register(void *data, int len) { (void)data; (void)len; return 0; }
static void music_unregister(void *h) { (void)h; }
static void music_play(void *h, boolean looping) { (void)h; (void)looping; }
static boolean music_playing(void) { return false; }

music_module_t DG_music_module = {
    devices,
    sizeof devices / sizeof devices[0],
    music_init,
    music_nothing,
    music_volume,
    music_nothing,
    music_nothing,
    music_register,
    music_unregister,
    music_play,
    music_nothing,
    music_playing,
    music_nothing,
};
