/* umac on the RNS-E 192: a Mac Plus, its start, its run and its end.
 *
 * Started by macboot (umac/macboot) through image_enter, with
 * the Mac Plus ROM and the disk image in RAM and the way back to the SD
 * loader in the hand-off block (handoff.h).  The SDK's start-up code
 * has run anew: vectors, the 10 ms tick that also strobes the watchdog,
 * the screen as the loader left it.
 *
 * umac (umac/build.sh: Matt Evans's emulator around Musashi's 68000)
 * runs in steps of 40,000 68000 cycles (umac_loop: 5 ms of the Mac's
 * time); between two of them the faceplate is answered and read
 * (input_poll), so that the display key, CD EJECT and RETURN held work
 * as everywhere else (CLAUDE.md, "Mandatory hardware behavior").  By
 * the unit's clock (micros) the Mac gets its vertical blank 60 times a
 * second, when the screen is brought over too, and its one-second
 * tick.  The Mac's time otherwise is what umac counts, so it runs as
 * fast as the SH-4 takes it.
 *
 * The disk is a copy in RAM: what the Mac writes to it lasts until the
 * unit is switched off or the Mac ends (the SDK does not write cards).
 * An eject (the Finder's Shut Down, or Eject) ends it, back to the
 * loader; so does a fault umac cannot go on from, after its message
 * has been on the screen.  See docs/umac.md. */
#include <stdio.h>
#include <string.h>
#include "rnse.h"
#include "handoff.h"
#include "umac.h"
#include "rom.h"
#include "port.h"

#define CCR 0xff00001c
#define VBLANK_US 16667u
static u32 found_ccr;
static int ending;

/* Before the loader comes back: the caches as umac found them,
 * everything written back. */
static void leave(void)
{
    cache_setup(found_ccr);
}

void port_idle(void)
{
    if (!ending)
        panel_service();
}

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
            if (*text == '\n')
                continue;
        }
        line[n++] = (char)(*text >= 'a' && *text <= 'z' ? *text - 32 : *text);
    }
}

void port_exit(int status)
{
    ending = 1;
    if (status == 0) {
        leave();
        loader_return(0);
    }
    /* An error (or no loader): what went to stderr, on the screen. */
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(8, 8, status ? "THE MAC STOPPED" : "THE MAC ENDED", 2, 2);
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

static void fail(const char *why)
{
    fprintf(stderr, "%s\n", why);
    port_exit(-1);
}

int main(void)
{
    struct mac_handoff *h = (struct mac_handoff *)MAC_HANDOFF;
    disc_descr_t discs[DISC_NUM_DRIVES];
    u32 last_vblank, last_second, now;
    int r;

    board_init();
    console_puts("\nrnse sdk: umac\n");
    /* At once, so that a unit shows that umac was entered. */
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 16, "MAC IS STARTING", 1, 2);
    /* The caches as Doom has them (docs/doom.md, "Speed and the
     * caches"): P1, where all of this lives, copy-back. */
    found_ccr = REG32(CCR);
    cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB);
    /* The faceplate controller as the loader left it, lit. */
    panel_attach();
    display_idle = port_idle;
    loader_leave = leave;

    if (h->magic[0] != MAC_MAGIC0 || h->magic[1] != MAC_MAGIC1)
        fail("not started by macboot: no ROM, no disk");
    if (h->loader_return)
        loader_return_via = h->loader_return;
    if (rom_patch((u8 *)h->rom))
        fail("the ROM is no Mac Plus v3 ROM");

    memset((void *)MAC_RAM, 0, MAC_RAM_SIZE);
    memset(discs, 0, sizeof discs);
    discs[0].base = (u8 *)h->disk;
    discs[0].size = h->disk_size;
    discs[0].read_only = 0;
    umac_init((void *)MAC_RAM, (void *)h->rom, discs);
    video_init();
    printf("umac: %ux%u, %u KB, disk of %u KB\n", DISP_WIDTH, DISP_HEIGHT, UMAC_MEMSIZE,
           (unsigned)(h->disk_size >> 10));

    last_vblank = last_second = micros();
    for (;;) {
        input_poll();
        if ((r = umac_loop()) != 0)
            break;
        now = micros();
        if (now - last_vblank >= VBLANK_US) {
            /* (Late by more than a frame, it catches up at once rather
             * than with a burst of vertical blanks.) */
            last_vblank = now - last_vblank >= 2 * VBLANK_US ? now : last_vblank + VBLANK_US;
            umac_vsync_event();
            video_update(0);
        }
        if (now - last_second >= 1000000u) {
            last_second += 1000000u;
            umac_1hz_event();
        }
    }
    /* A fault (2) said its say on stderr; an eject is the Mac's own end. */
    port_exit(r == 2 ? -1 : 0);
}
