/* Doom on the RNS-E 192: the game's start, its waits and its end.
 *
 * Started by doomboot (sdk/examples/doomboot) through image_enter, with
 * the WAD in RAM and the way back to the SD loader in the hand-off
 * block (handoff.h).  The SDK's start-up code has run anew: vectors,
 * the 10 ms tick that also strobes the watchdog, the screen as the
 * loader left it.  From here on the faceplate is answered in every
 * wait (port_idle), so that the display key, CD EJECT and RETURN held
 * work as everywhere else (CLAUDE.md, "Mandatory hardware behavior").
 * See docs/doom.md. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rnse.h"
#include "handoff.h"
#include "port.h"

#include "m_argv.h"

void M_FindResponseFile(void);
void D_DoomMain(void);
void doomgeneric_Tick(void);

#define MAX_ARGS 24
#define CCR 0xff00001c
static char *args[MAX_ARGS + 1];
static int ending;
static u32 found_ccr;

/* Before the loader comes back (RETURN held, or the end): the sound
 * stopped, and the caches as the game found them, everything written
 * back. */
static void leave(void)
{
    sound_stop();
    cache_setup(found_ccr);
}

void port_idle(void)
{
#ifdef RNSE_DEBUG_IDLE
    static u32 last, worst;
    u32 now = millis();
    if (last && now - last > worst) {
        worst = now - last;
        printf("idle: %u ms without the faceplate\n", (unsigned)worst);
    }
    last = now;
#endif
    if (ending)
        return;
    panel_service();
    sound_pump();
}

/* ---- Time (doomgeneric's i_timer.c asks for milliseconds) ------------- */

uint32_t DG_GetTicksMs(void)
{
    /* micros() wraps every 71 minutes: its steps added up here instead
     * (it is called far more often than that). */
    static u32 last, us, ms;
    u32 now = micros();
    us += now - last;
    last = now;
    if (us >= 1000) {
        ms += us / 1000;
        us %= 1000;
    }
    return ms;
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t t = DG_GetTicksMs();
    do {
        port_idle();
        if (DG_GetTicksMs() - t < ms)
            cpu_idle();
    } while (DG_GetTicksMs() - t < ms);
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

/* ---- The end ---------------------------------------------------------- */

static void text_lines(const char *text, int y, u8 colour)
{
    char line[64];
    int n = 0;
    for (;; text++) {
        if (*text == '\n' || !*text || n == 60) {
            line[n] = 0;
            if (n) {
                screen_text(8, y, line, colour, 1);
                y += 10;
            }
            n = 0;
            if (!*text || y > screen_height - 30)
                return;
            if (*text != '\n')
                line[n++] = (char)toupper((unsigned char)*text);
        } else {
            line[n++] = (char)toupper((unsigned char)*text);
        }
    }
}

void port_exit(int status)
{
    ending = 1;
    if (status == 0) {
        leave();
        loader_return(0);
    }
    sound_stop();
    /* An error (or no loader): what went to stderr, on the screen. */
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(8, 8, status ? "DOOM STOPPED" : "DOOM ENDED", 2, 2);
    text_lines(error_text, 32, 1);
    screen_text(8, screen_height - 16, loader_active() ? "RETURN: BACK TO THE LOADER" : "NO LOADER TO RETURN TO", 2, 1);
    for (;;) {
        panel_service();
        if (panel_return_short()) {
            leave();
            loader_return(status ? status : 1);
        }
        cpu_idle();
    }
}

/* ---- The start -------------------------------------------------------- */

static void fail(const char *why)
{
    fprintf(stderr, "%s\n", why);
    port_exit(-1);
}

int main(void)
{
    struct doom_handoff *h = (struct doom_handoff *)DOOM_HANDOFF;
    int n = 0;
    char *p;

    board_init();
    console_puts("\nrnse sdk: doom\n");
    /* At once, so that a unit shows that the game was entered. */
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 16, "DOOM IS STARTING", 1, 2);
#ifdef RNSE_TEST_FAULT
    /* (A test of the SDK's exception screen: an address error.) */
    {
        static volatile u32 odd = 0x8c010081;
        (void)*(volatile u32 *)odd;
    }
#endif
    /* The caches as measured fastest on a unit (docs/android-auto.md):
     * stores through P1, where all of the game is, kept in the cache
     * (copy-back) instead of written through, and no index modes.
     * Without it a unit runs this kind of code at half the pace. */
    found_ccr = REG32(CCR);
    cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB);
    /* The faceplate controller as the loader left it, lit. */
    panel_attach();
    display_idle = port_idle;
    loader_leave = leave;
    heap_init((void *)DOOM_HEAP, DOOM_HEAP_END - DOOM_HEAP);

    if (h->magic[0] != DOOM_MAGIC0 || h->magic[1] != DOOM_MAGIC1)
        fail("not started by doomboot: no WAD");
    if (h->loader_return)
        loader_return_via = h->loader_return;
    files_add_fixed("DOOM1.WAD", (void *)h->wad, (long)h->wad_size);
    port_idle();

    args[n++] = "doom";
    args[n++] = "-iwad";
    args[n++] = "DOOM1.WAD";
    /* The zone: 12 MiB of the 17 the heap has. */
    args[n++] = "-mb";
    args[n++] = "12";
    /* DOOM.ARG's words after them. */
    h->args[sizeof h->args - 1] = 0;
    for (p = h->args; *p && n < MAX_ARGS;) {
        while (*p == ' ' || *p == '\t')
            *p++ = 0;
        if (!*p)
            break;
        args[n++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    args[n] = 0;
    myargc = n;
    myargv = args;

    M_FindResponseFile();
    D_DoomMain();
    for (;;)
        doomgeneric_Tick();
}
