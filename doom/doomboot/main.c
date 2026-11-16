/* Doom from an SD card (192): the first stage, after sdk/examples/sdboot.
 *
 * Copied to the card as DOOM.BIN (sdboot's menu lists it), beside
 * DOOMGAME.BIN and DOOM1.WAD as doom/build.sh makes them, and perhaps
 * DOOM.ARG, a line of more arguments for the game (`-timedemo demo1`).
 * The game is far bigger than the loader's place, so this program reads
 * it to 0x8c010080 and the WAD to 0x8c800000, checks both, leaves the
 * game what it needs at 0x8c00f000 (handoff.h) and enters it
 * (image_enter: everything stopped, the caches seen to).
 *
 * It stays where it is, in the loader's place, and is the way back: the
 * game's loader_return goes through this program's (loader_return_via),
 * which puts sdboot back as for any program from the card.  RETURN held
 * for two seconds, here or in the game, does that.  The display key and
 * CD EJECT work all the while (panel_service, from sd_idle while the
 * card is read).
 *
 * Something missing or spoilt is said on the screen, and after a few
 * seconds it is back to sdboot.  See docs/doom.md. */
#include "rnse.h"
#include "handoff.h"

#define SESSION ((volatile u32 *)0xac7c0000)
#define SESSION_MAGIC 0x53444c31u
#define TRAILER 16 /* "RNSEDOOM", the length before it, its CRC-32 */

static int row;
static u32 sectors, expect;
static char arg_sector[512] __attribute__((aligned(32)));

static void say(const char *text, u8 colour)
{
    screen_fill(0, 70 + 18 * row, screen_width, 16, 0);
    screen_text(16, 70 + 18 * row, text, colour, 2);
    console_puts(text);
    console_puts("\n");
    row++;
}

static void number(const char *what, s32 v)
{
    char text[40], *p = text, t[12];
    int n = 0;
    u32 u = v < 0 ? (u32)-v : (u32)v;
    while (*what)
        *p++ = *what++;
    *p++ = ' ';
    if (v < 0)
        *p++ = '-';
    do {
        t[n++] = (char)('0' + umod(u, 10));
        u = udiv(u, 10);
    } while (u);
    while (n)
        *p++ = t[--n];
    *p = 0;
    say(text, 1);
}

/* A bar under the text, from the sectors read so far. */
static void progress(void)
{
    u32 w = expect ? udiv(sectors * 512u, udiv(expect, 360) + 1) : 0;
    if (w > 360)
        w = 360;
    screen_fill(20, 214, (int)w, 6, 2);
}

static void controls(void)
{
    panel_service(); /* the faceplate, the display key, CD EJECT, RETURN held */
    if (++sectors % 64 == 0)
        progress();
}

static void wait_ms(u32 ms)
{
    u32 t = millis();
    while (millis() - t < ms) {
        panel_service();
        cpu_idle();
    }
}

static u32 crc32(const u8 *p, u32 n)
{
    static const u32 t[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    u32 c = 0xffffffffu, i;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        c = (c >> 4) ^ t[c & 15];
        c = (c >> 4) ^ t[c & 15];
        if (!(i & 0xffff))
            panel_service();
    }
    return ~c;
}

/* A file of the card to `to`, the bar following it: 0 or sd_load's. */
static int load(const char *name, void *to, u32 room, u32 *size)
{
    int r;
    /* (With no room, sd_load says how long the file is.) */
    r = sd_load(name, to, 0, &expect);
    if (r != -3 && r != 0)
        return r;
    sectors = 0;
    screen_fill(20, 214, 360, 6, 3);
    r = sd_load(name, to, room, size);
    progress();
    return r;
}

static void give_up(const char *why, int r)
{
    say(why, 2);
    number("ERROR", r);
    say("BACK TO THE LOADER", 1);
    wait_ms(4000);
    loader_return(r ? r : -1);
    say("NO LOADER TO RETURN TO", 2);
    for (;;)
        wait_ms(1000);
}

int main(void)
{
    struct doom_handoff *h = (struct doom_handoff *)DOOM_HANDOFF;
    const u8 *game = (const u8 *)(DOOM_LOAD + 0x80);
    const u32 *trailer;
    u32 size = 0, wad_size = 0, entry, n, t0;
    int slot = -1, r = -2, i;

    board_init();
    console_puts("\nrnse sdk: doomboot\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_palette(3, 60, 60, 60);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 16, "DOOM", 2, 6);
    /* The faceplate controller as sdboot left it, lit. */
    panel_attach();
    sd_idle = controls;
    display_idle = panel_service;
    if (screen_width != 400)
        give_up("THE 192 ONLY", -1);

    if (SESSION[0] == SESSION_MAGIC && SESSION[1] == ~SESSION_MAGIC && SESSION[3] < 2)
        slot = (int)SESSION[3];
    /* The card sdboot read this program from; else the first there. */
    for (i = 0; i < 2 && r; i++) {
        int s = slot >= 0 ? slot : i;
        if (slot >= 0 && i)
            break;
        if (slot < 0 && !sd_present(s))
            continue;
        r = sd_attach(s);
        if (r)
            r = sd_init(s);
        if (!r)
            slot = s;
    }
    if (r)
        give_up("NO CARD", r);

    t0 = millis();
    say("READING DOOMGAME.BIN", 1);
    r = load("DOOMGAMEBIN", (void *)game, DOOM_ROOM_END - 0x4000 - (u32)game, &size);
    if (r)
        give_up("DOOMGAME.BIN MISSING OR LONG", r);
    trailer = (const u32 *)(game + size - TRAILER);
    n = trailer[2];
    if (size < 0x1000 || (size & 3) || trailer[0] != DOOM_MAGIC0 || trailer[1] != DOOM_MAGIC1 ||
        n != size - TRAILER || ((const u32 *)game)[4] != DOOM_MAGIC0)
        give_up("DOOMGAME.BIN IS NOT THE GAME", -20);
    if (crc32(game, n) != trailer[3])
        give_up("DOOMGAME.BIN IS SPOILT", -21);
    entry = ((const u32 *)game)[0];
    if (entry < (u32)game || entry >= (u32)game + n || (entry & 1))
        give_up("DOOMGAME.BIN IS NOT THE GAME", -22);

    row = 0;
    say("READING DOOM1.WAD", 1);
    r = load("DOOM1   WAD", (void *)DOOM_WAD, DOOM_WAD_ROOM, &wad_size);
    if (r)
        give_up("DOOM1.WAD MISSING OR LONG", r);
    if (wad_size < 12 || ((const u32 *)DOOM_WAD)[0] != 0x49574144u) /* "IWAD" */
        give_up("DOOM1.WAD IS NO IWAD", -23);

    for (i = 0; i < (int)sizeof h->args; i++)
        h->args[i] = 0;
    if (sd_load("DOOM    ARG", arg_sector, sizeof arg_sector, &n) == 0)
        for (i = 0; i < (int)n && i < (int)sizeof h->args - 1; i++)
            h->args[i] = arg_sector[i] == '\r' || arg_sector[i] == '\n' ? ' ' : arg_sector[i];
    h->magic[0] = DOOM_MAGIC0;
    h->magic[1] = DOOM_MAGIC1;
    h->wad = DOOM_WAD;
    h->wad_size = wad_size;
    h->loader_return = loader_active() ? loader_return : 0;
    h->slot = slot;

    row = 1;
    number("MS", (s32)(millis() - t0));
    say("STARTING", 2);
    /* The game takes the motor over from a standstill. */
    if (display_state() == DISPLAY_OPENING || display_state() == DISPLAY_CLOSING)
        display_wait(9000);
    display_halt();
    image_enter(entry, DOOM_LOAD, DOOM_ROOM_END);
    return -31;
}
