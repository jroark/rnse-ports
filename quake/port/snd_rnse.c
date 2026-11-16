/* Quake on the RNS-E 192: the sound card (snd_null.c's place).
 *
 * Quake mixes into a ring that a sound card plays round and round,
 * ahead of where the card is (snd_dma.c).  Here the card is a card
 * processor of the unit's with the kit's program of its own (pcm.c):
 * 16-bit stereo at 44.1 kHz out of its converter, which the amplifier
 * hears as RADIO_DECODER, fed by the DMA controller from the ring with
 * nothing more to do for the game (pcm_loop).  The game writes the ring
 * through P2, past the cache, since the controller reads memory.
 *
 * The processor is the one the card is NOT in: the card's own keeps
 * the installed software's program, which reads PAK0.PAK, and pcm_start
 * would replace it.  `-dsp 0` or `-dsp 1` in QUAKE.ARG says otherwise
 * (which of the two converters reaches the amplifier on a unit is still
 * to be heard: sdk/examples/pcmtest). */
#include "quakedef.h"
#include "rnse.h"
#include "port.h"

#define BLOCK 1024
#define BLOCKS 16
#define FRAMES (BLOCK * BLOCKS) /* 0.37 s: a power of two, as Quake wants */

static s16 ring[2 * FRAMES] __attribute__((aligned(32)));
static dma_t card;
static int started;

qboolean SNDDMA_Init(void)
{
    int dsp = port_slot ? 0 : 1, i = COM_CheckParm("-dsp");
    volatile s16 *out;
    if (i && i + 1 < com_argc)
        dsp = com_argv[i + 1][0] == '1';
    if (dsp == port_slot)
        Con_Printf("sound: processor %d, the card's: no more reads\n", dsp);
    if (pcm_start(dsp)) {
        Con_Printf("sound: card processor %d does not start\n", dsp);
        return false;
    }
    radio_source(RADIO_DECODER, port_volume);
    shm = &card;
    card.splitbuffer = 0;
    card.samplebits = 16;
    card.speed = 44100;
    card.channels = 2;
    card.samples = 2 * FRAMES;
    card.samplepos = 0;
    card.submission_chunk = 1;
    card.buffer = (unsigned char *)((u32)ring | 0x20000000u);
    if (pcm_loop(ring, BLOCK, BLOCKS)) {
        pcm_stop();
        return false;
    }
    out = (volatile s16 *)card.buffer;
    for (i = 0; i < 2 * FRAMES; i++)
        out[i] = 0;
    started = 1;
    printf("quake: sound from card processor %d\n", dsp);
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    card.samplepos = (int)(2 * pcm_loop_pos());
    return card.samplepos;
}

void SNDDMA_Submit(void)
{
}

void sound_stop(void)
{
    if (started)
        pcm_stop();
    started = 0;
}

void SNDDMA_Shutdown(void)
{
    sound_stop();
}
