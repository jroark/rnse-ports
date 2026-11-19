/* Quake on the RNS-E 192: the game's start, its time, its files, its
 * waits and its end (sys_null.c's place).
 *
 * Started by quakeboot (sdk/examples/quakeboot) through image_enter,
 * with the card it came from still as the installed software started
 * it and the way back to the SD loader in the hand-off block
 * (handoff.h).  The SDK's start-up code has run anew: vectors, the
 * 10 ms tick that also strobes the watchdog, the screen as the loader
 * left it.  From here on the faceplate is answered in every wait and
 * between the card's sectors (port_idle), so that the display key, CD
 * EJECT and RETURN held work as everywhere else.
 *
 * PAK0.PAK stays on the card: its chain of clusters is read once, and
 * then the game's reads go to the sectors they need (sd_pread).  Other
 * files (config.cfg, saved games) live in RAM for the session, in the C
 * library shared with Doom (sdk/libc).  See docs/quake.md. */
#include <ctype.h>
#include <strings.h>
#include "quakedef.h"
#include "rnse.h"
#include "handoff.h"
#include "port.h"

#define MAX_ARGS 24
#define CCR 0xff00001c
/* FPSCR as -m4 code expects it between functions: double precision
 * (PR), denormals taken as zero (DN: else they trap), rounding to the
 * nearest, no exceptions. */
#define FPSCR_GAME 0x000c0000u
#define HUNK_MB 20

int port_slot;
qboolean isDedicated = false;
static int ending;
static u32 found_ccr;
static struct sd_file pak;
static char *args[MAX_ARGS + 1];

void sound_stop(void);

static void set_fpscr(u32 v)
{
    __asm__ volatile("lds %0, fpscr" : : "r"(v));
}

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
    /* (-DRNSE_DEBUG_IDLE in EXTRA_CFLAGS: the longest the faceplate went
     * unanswered so far, each time it grows.) */
    static u32 last, worst;
    u32 now = millis();
    if (last && now - last > worst) {
        worst = now - last;
        printf("idle: %u ms without the faceplate\n", (unsigned)worst);
    }
    last = now;
#endif
    if (!ending)
        panel_service();
}

/* ---- Time ------------------------------------------------------------- */

/* micros() wraps every 71 minutes: its steps added up here instead (it
 * is called far more often than that). */
static u32 last_us;
static double seconds;

double Sys_FloatTime(void)
{
    u32 now = micros();
    seconds += (double)(now - last_us) * 1e-6;
    last_us = now;
    return seconds;
}

void Sys_Sleep(void)
{
    port_idle();
    cpu_idle();
}

void Sys_HighFPPrecision(void)
{
}

void Sys_LowFPPrecision(void)
{
}

void Sys_SetFPCW(void)
{
}

/* ---- Files ------------------------------------------------------------ */

#define HANDLES 16
static struct {
    int used, is_pak;
    FILE *f;
    u32 at;
} handles[HANDLES];

/* PAK0.PAK's bytes from the card.  The game reads its files whole, in
 * one go, but a demo it reads a few bytes at a time (stdio, from
 * COM_FOpenFile): small reads are served from 8 KB of the file kept at
 * hand, so that every sector is read once and not once a read. */
#define WINDOW 8192
static u8 window[WINDOW] __attribute__((aligned(32)));
static u32 window_at, window_len;

static int pak_read(u32 at, void *dest, u32 n)
{
    u8 *out = dest;
    int r;
    if (at >= pak.size)
        return 0;
    if (n > pak.size - at)
        n = pak.size - at;
    if (n >= WINDOW / 2)
        return sd_pread(&pak, at, dest, n) ? -1 : (int)n;
    while (n) {
        u32 take;
        if (!(window_len && at >= window_at && at < window_at + window_len)) {
            window_len = 0;
            window_at = at & ~511u;
            take = pak.size - window_at < WINDOW ? pak.size - window_at : WINDOW;
            r = sd_pread(&pak, window_at, window, take);
            if (r)
                return -1;
            window_len = take;
        }
        take = window_at + window_len - at;
        if (take > n)
            take = n;
        memcpy(out, window + (at - window_at), take);
        out += take;
        at += take;
        n -= take;
    }
    return (int)(out - (u8 *)dest);
}

static long pak_read_at(long at, void *buf, long n)
{
    return pak_read((u32)at, buf, (u32)n);
}

static int is_pak0(const char *path)
{
    int n = (int)strlen(path);
    return n >= 8 && !strcasecmp(path + n - 8, "pak0.pak");
}

static int new_handle(void)
{
    int i;
    for (i = 1; i < HANDLES; i++)
        if (!handles[i].used) {
            memset(&handles[i], 0, sizeof handles[i]);
            handles[i].used = 1;
            return i;
        }
    Sys_Error("out of file handles");
    return -1;
}

static long file_length(FILE *f)
{
    long at = ftell(f), end;
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, at, SEEK_SET);
    return end;
}

int Sys_FileOpenRead(char *path, int *hndl)
{
    int i;
    FILE *f;
    if (is_pak0(path)) {
        i = new_handle();
        handles[i].is_pak = 1;
        *hndl = i;
        return (int)pak.size;
    }
    f = fopen(path, "rb");
    if (!f) {
        *hndl = -1;
        return -1;
    }
    i = new_handle();
    handles[i].f = f;
    *hndl = i;
    return (int)file_length(f);
}

int Sys_FileOpenWrite(char *path)
{
    int i;
    FILE *f = fopen(path, "wb");
    if (!f)
        Sys_Error("Error opening %s", path);
    i = new_handle();
    handles[i].f = f;
    return i;
}

void Sys_FileClose(int handle)
{
    if (handles[handle].f)
        fclose(handles[handle].f);
    handles[handle].used = 0;
}

void Sys_FileSeek(int handle, int position)
{
    if (handles[handle].is_pak)
        handles[handle].at = (u32)position;
    else
        fseek(handles[handle].f, position, SEEK_SET);
}

int Sys_FileRead(int handle, void *dest, int count)
{
    if (handles[handle].is_pak) {
        u32 at = handles[handle].at;
        int n = pak_read(at, dest, (u32)count);
        if (n < 0)
            Sys_Error("PAK0.PAK: the card did not read at %u", (unsigned)at);
        handles[handle].at = at + (u32)n;
        return n;
    }
    return (int)fread(dest, 1, (size_t)count, handles[handle].f);
}

int Sys_FileWrite(int handle, void *data, int count)
{
    if (handles[handle].is_pak)
        return 0;
    return (int)fwrite(data, 1, (size_t)count, handles[handle].f);
}

int Sys_FileTime(char *path)
{
    FILE *f;
    if (is_pak0(path))
        return 1;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    fclose(f);
    return 1;
}

void Sys_mkdir(char *path)
{
}

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length)
{
}

/* ---- Messages and the end --------------------------------------------- */

void Sys_Printf(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void Sys_DebugLog(char *file, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

char *Sys_ConsoleInput(void)
{
    return NULL;
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
    screen_palette(2, 200, 120, 40);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(8, 8, status ? "QUAKE STOPPED" : "QUAKE ENDED", 2, 2);
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

void Sys_Error(char *error, ...)
{
    static char text[512];
    va_list ap;
    va_start(ap, error);
    vsnprintf(text, sizeof text, error, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", text);
    port_exit(1);
}

void Sys_Quit(void)
{
    port_exit(0);
}

/* ---- The start -------------------------------------------------------- */

static void fail(const char *why, int r)
{
    fprintf(stderr, "%s (%d)\n", why, r);
    port_exit(-1);
}

int main(void)
{
    struct quake_handoff *h = (struct quake_handoff *)QUAKE_HANDOFF;
    static quakeparms_t parms;
    double now, then;
    u32 *chain;
    int n = 0, r, mb = HUNK_MB;
    char *p;

    board_init();
    set_fpscr(FPSCR_GAME);
    console_puts("\nrnse sdk: quake\n");
    /* At once, so that a unit shows that the game was entered. */
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 16, "QUAKE IS STARTING", 1, 2);
    /* The caches as measured fastest on a unit (sdbridge/docs/android-auto.md):
     * stores through P1, where all of the game is, kept in the cache
     * (copy-back) instead of written through, and no index modes. */
    found_ccr = REG32(CCR);
    cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB);
    /* The faceplate controller as the loader left it, lit. */
    panel_attach();
    display_idle = port_idle;
    loader_leave = leave;
    sd_idle = port_idle;
    heap_init((void *)QUAKE_HEAP, QUAKE_HEAP_END - QUAKE_HEAP);
    last_us = micros();

    if (h->magic[0] != QUAKE_MAGIC0 || h->magic[1] != QUAKE_MAGIC1)
        fail("not started by quakeboot", 0);
    if (h->loader_return)
        loader_return_via = h->loader_return;
    port_slot = h->slot;

    /* The card as quakeboot left it, and the data's chain of clusters. */
    r = sd_attach(port_slot);
    if (r)
        fail("the card does not answer", r);
    r = sd_open("PAK0    PAK", &pak, 0, 0);
    if (r)
        fail("no PAK0.PAK on the card", r);
    chain = malloc(4 * pak.clusters);
    if (!chain)
        fail("no memory for PAK0.PAK's clusters", (int)pak.clusters);
    r = sd_open("PAK0    PAK", &pak, chain, pak.clusters);
    if (r)
        fail("PAK0.PAK unreadable", r);
    files_add_reader("pak0.pak", (long)pak.size, pak_read_at);
    printf("quake: PAK0.PAK %u bytes in %u clusters, card in slot %d\n", (unsigned)pak.size,
           (unsigned)pak.clusters, port_slot);

    args[n++] = "quake";
    /* QUAKE.ARG's words after it. */
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
    COM_InitArgv(n, args);
    r = COM_CheckParm("-mem");
    if (r && r + 1 < com_argc)
        mb = Q_atoi(com_argv[r + 1]);
    parms.memsize = mb * 1024 * 1024;
    parms.membase = malloc(parms.memsize);
    if (!parms.membase)
        fail("no memory for the hunk", mb);
    parms.basedir = ".";
    parms.argc = com_argc;
    parms.argv = com_argv;

    Host_Init(&parms);
    /* Running always (there are not keys enough to hold one for it), the
     * view levelled after looking up or down, and mixing far enough
     * ahead for a slow frame. */
    Cbuf_AddText("cl_forwardspeed 400\ncl_backspeed 400\nlookspring 1\n_snd_mixahead 0.15\n");

    then = Sys_FloatTime() - 0.1;
    for (;;) {
        now = Sys_FloatTime();
        Host_Frame(now - then);
        then = now;
    }
}
