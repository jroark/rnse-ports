/* A PC on the RNS-E 192 (Tiny386), for Windows 3.11: the start, the
 * loop, the screen, the controls and the end.
 *
 * Started by pcboot (sdk/examples/pcboot, WIN311.BIN on the card)
 * through image_enter, with the way back to the SD loader in the
 * hand-off block (handoff.h).  It reads the BIOS (BIOS.ROM, VGABIOS.ROM)
 * and opens the disk image (WIN311.IMG) on the card itself; the disk is
 * read as it is needed and what the PC writes stays in RAM (disk.c).
 * Every pass of the loop answers the faceplate (port_idle), so that the
 * display key, CD EJECT and RETURN held work as everywhere else.
 * See docs/win311.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "rnse.h"
#include "rnse_libc.h"
#include "handoff.h"
#include "pc.h"
#include "screen.h"
#include "disk.h"

#define CCR 0xff00001c
#define GUEST_RAM ((void *)0x8c800000u)
#define GUEST_RAM_SIZE (16u << 20)
#define HEAP 0x8d800000u
#define HEAP_END 0x8df00000u
#define POOL ((void *)0x8e000000u)
#define POOL_SIZE (0x8ff00000u - 0x8e000000u)
#define FB_W 720
#define FB_H 480

static u32 found_ccr;
static int ending;
static struct sd_file image;
static PC *pc;
static uint8_t *fb;
static uint8_t out[SCREEN_W * SCREEN_H] __attribute__((aligned(32)));

/* ---- What Tiny386 wants of its platform ------------------------------ */

/* Tiny386 asks for the time many times a step (the timer, the VGA's
 * retrace, the CMOS clock): the time read once a pass of the main loop,
 * every few thousand instructions, since micros() costs a division on
 * the SH-4. */
static u32 now_us;

uint32_t get_uticks(void)
{
    return now_us;
}

/* The PC's RAM (asked for first) in its fixed place; anything else as
 * big (the VGA's memory) from the heap.  Both cleared. */
void *bigmalloc(size_t size)
{
    static int ram_given;
    void *p;
    if (!ram_given) {
        if (size > GUEST_RAM_SIZE) {
            fprintf(stderr, "guest RAM: %u bytes asked\n", (unsigned)size);
            port_exit(-1);
        }
        ram_given = 1;
        p = GUEST_RAM;
    } else {
        p = malloc(size);
        if (!p) {
            fprintf(stderr, "no room for %u bytes\n", (unsigned)size);
            port_exit(-1);
        }
    }
    memset(p, 0, size);
    return p;
}

static struct rom {
    const char *name, *card;
    void *data;
    u32 size;
} roms[] = {
    { "BIOS.ROM", "BIOS    ROM", 0, 0 },
    { "VGABIOS.ROM", "VGABIOS ROM", 0, 0 },
};

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
    for (unsigned i = 0; i < sizeof roms / sizeof roms[0]; i++)
        if (!strcmp(file, roms[i].name)) {
            u32 n = roms[i].size;
            memcpy((char *)phys_mem + addr - (backward ? n : 0), roms[i].data, n);
            return (int)n;
        }
    fprintf(stderr, "%s missing\n", file);
    port_exit(-1);
}

/* The calendar the PC's clock starts from: the unit's own is not known
 * to the kit, so a fixed date (docs/win311.md), then the time it runs. */
void rnse_tm(struct tm *tm)
{
    static const u8 month_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    u32 s = millis() / 1000, days = s / 86400;
    memset(tm, 0, sizeof *tm);
    tm->tm_sec = s % 60;
    tm->tm_min = s / 60 % 60;
    tm->tm_hour = s / 3600 % 24;
    tm->tm_year = 94;           /* 1 January 1994, a Saturday */
    tm->tm_wday = (6 + days) % 7;
    tm->tm_mon = 0;
    while (days >= month_days[tm->tm_mon]) {
        days -= month_days[tm->tm_mon];
        if (++tm->tm_mon == 12) {
            tm->tm_mon = 0;
            tm->tm_year++;
        }
    }
    tm->tm_mday = days + 1;
}

/* What Tiny386's file-backed disks would call, never reached here (the
 * disk is disk.c's): enough for it to link. */
int fseeko(FILE *f, long offset, int whence)
{
    return fseek(f, offset, whence);
}

long ftello(FILE *f)
{
    return ftell(f);
}

void perror(const char *s)
{
    fprintf(stderr, "%s: error\n", s);
}

static int card_read(uint64_t offset, void *buf, uint32_t n)
{
    return sd_pread(&image, (u32)offset, buf, n);
}

/* ---- The end, and every wait ----------------------------------------- */

static void leave(void)
{
    cache_setup(found_ccr);
}

static void controls(void);

void port_idle(void)
{
    if (ending)
        return;
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
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(8, 8, status ? "THE PC STOPPED" : "THE PC ENDED", 2, 2);
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

static void fail(const char *why, int r)
{
    fprintf(stderr, "%s (%d)\n", why, r);
    port_exit(-1);
}

/* ---- The screen ------------------------------------------------------ */

static int pan_mode;
static int mouse_x = 320, mouse_y = 240; /* where we think the pointer is */

static void redraw(void *opaque, int x, int y, int w, int h)
{
}

/* Windows' mode is drawn from the VGA's planes, only what changed, up
 * to every PLANAR_US; anything else (DOS's text, the 1:1 view) by way of
 * Tiny386's frame buffer, all of it, every OTHER_US. */
#define PLANAR_US 60000
#define OTHER_US 250000

static void show(u32 now)
{
    static u32 last_frame;
    struct vga_planar p;
    struct screen_view v;
    int first, last;
    if (!pan_mode && vga_planar_get(pc->vga, &p)) {
        if (now - last_frame < PLANAR_US)
            return;
        last_frame = now;
        if (screen_render_planar(&p, out, SCREEN_W, &first, &last)) {
            if (first <= last)
                screen_blit(0, first, SCREEN_W, last - first + 1, out + first * SCREEN_W, SCREEN_W);
            return;
        }
    }
    if (now - last_frame < OTHER_US)
        return;
    last_frame = now;
    screen_planar_forget();
    vga_refresh(pc->vga, redraw, NULL, 0);
    screen_view_get(pc->vga, &v);
    /* Nothing new in the frame buffer (DOS's text, mostly standing):
     * nothing to scale.  A sum over the picture costs a few million
     * cycles less than scaling it. */
    {
        static u32 last_sum, last_mode;
        u32 sum = 0, mode = (u32)(v.w << 20 ^ v.h << 8 ^ pan_mode ^ (pan_mode ? mouse_x << 10 ^ mouse_y : 0));
        for (int y = 0; y < v.h; y++) {
            const u32 *row = (const u32 *)(fb + (v.y + y) * FB_W * 4) + v.x;
            for (int x = 0; x < v.w; x += 2)
                sum = (sum << 1 | sum >> 31) ^ row[x] ^ row[x + 1] << 3;
        }
        if (sum == last_sum && mode == last_mode)
            return;
        last_sum = sum;
        last_mode = mode;
    }
    if (pan_mode && v.w > SCREEN_W)
        screen_render_pan(&v, fb, FB_W * 4, mouse_x - SCREEN_W / 2, mouse_y - SCREEN_H / 2,
                          out, SCREEN_W);
    else
        screen_render(&v, fb, FB_W * 4, out, SCREEN_W);
    screen_blit(0, 0, SCREEN_W, SCREEN_H, out, SCREEN_W);
}

/* ---- The controls ---------------------------------------------------- */

/* Linux input key codes, as Tiny386's keyboard takes them. */
enum { K_ESC = 1, K_TAB = 15, K_ENTER = 28, K_LEFTALT = 56 };

static void key(int code, int down)
{
    ps2_put_keycode(pc->kbd, down, code);
}

static void tap(int code)
{
    key(code, 1);
    key(code, 0);
}

/* Mouse steps a detent: more the faster the knob turns.  (Windows
 * doubles a report of more than 2 steps, and quadruples one of more
 * than 5, by its default MouseThreshold1/2 and MouseSpeed: a slow turn
 * moves the pointer 2 pixels a detent, a quick one 8 or more.) */
static int accel(int detents)
{
    int a = detents < 0 ? -detents : detents;
    return detents * (a >= 3 ? 4 : 2);
}

static void controls(void)
{
    static int buttons;
    int dx, dy, b;
    panel_service();
    /* The volume knob across, the select knob up and down. */
    dx = accel(panel_knob(KNOB_VOLUME));
    dy = accel(panel_knob(KNOB_SELECT));
    b = (panel_key(KEY_SELECT) ? 1 : 0) | (panel_key(KEY_SOFT_TR) ? 2 : 0);
    if (dx || dy || b != buttons) {
        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x > 639) mouse_x = 639;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y > 479) mouse_y = 479;
    }
    /* (Every pass: Tiny386 sends what it held back at the mouse's rate.) */
    ps2_mouse_event(pc->mouse, dx, dy, 0, b);
    buttons = b;
    if (panel_pressed(KEY_SOFT_BL))
        tap(K_ENTER);
    if (panel_pressed(KEY_SOFT_BR))
        tap(K_ESC);
    if (panel_pressed(KEY_NEXT))
        tap(K_TAB);
    if (panel_pressed(KEY_PREV)) {
        key(K_LEFTALT, 1);
        tap(K_TAB);
        key(K_LEFTALT, 0);
    }
    if (panel_pressed(KEY_SETUP))
        pan_mode = !pan_mode;
}

/* ---- The start ------------------------------------------------------- */

int main(void)
{
    struct pc_handoff *h = (struct pc_handoff *)PC_HANDOFF;
    PCConfig conf;
    u32 *chain, n, last_input = 0, t0;
    int r;

    board_init();
    console_puts("\nrnse sdk: pcemu\n");
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 16, "THE PC IS STARTING", 1, 2);
    /* Copy-back caches in P1, as Doom's port has them (docs/doom.md). */
    found_ccr = REG32(CCR);
    cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB);
    panel_attach();
    display_idle = port_idle;
    sd_idle = port_idle;
    loader_leave = leave;
    heap_init((void *)HEAP, HEAP_END - HEAP);

    if (h->magic[0] != PC_MAGIC0 || h->magic[1] != PC_MAGIC1)
        fail("not started by pcboot", -1);
    if (h->loader_return)
        loader_return_via = h->loader_return;

    r = sd_attach(h->slot);
    if (r)
        fail("no card", r);
    for (unsigned i = 0; i < sizeof roms / sizeof roms[0]; i++) {
        roms[i].data = malloc(256 << 10);
        r = sd_load(roms[i].card, roms[i].data, 256 << 10, &roms[i].size);
        if (r)
            fail(i ? "VGABIOS.ROM missing" : "BIOS.ROM missing", r);
    }
    r = sd_open("WIN311  IMG", &image, 0, 0);
    if (r)
        fail("WIN311.IMG missing", r);
    chain = malloc(image.clusters * 4);
    r = sd_open("WIN311  IMG", &image, chain, image.clusters);
    if (r)
        fail("WIN311.IMG unreadable", r);
    n = image.size / 512;
    r = disk_init(n, card_read, malloc(disk_map_bytes(n)), POOL, POOL_SIZE);
    if (r)
        fail("no room for the disk", r);
    printf("pcemu: WIN311.IMG %u bytes, card in slot %d\n", (unsigned)image.size, h->slot);

    memset(&conf, 0, sizeof conf);
    conf.mem_size = GUEST_RAM_SIZE;
    conf.vga_mem_size = 256 * 1024;
    conf.width = FB_W;
    conf.height = FB_H;
    conf.cpu_gen = 4;
    conf.fpu = 0;
    conf.bios = "BIOS.ROM";
    conf.vga_bios = "VGABIOS.ROM";
    conf.disks[0] = "WIN311.IMG";

    screen_init();
    for (int i = 0; i < 256; i++)
        screen_palette(i, pc_palette[i][0], pc_palette[i][1], pc_palette[i][2]);
    fb = malloc(FB_W * FB_H * 4);
    memset(fb, 0, FB_W * FB_H * 4);
    pc = pc_new(redraw, NULL, fb, &conf);
    now_us = micros();
    load_bios_and_reset(pc);
    pc->boot_start_time = get_uticks();
    t0 = millis();

    for (;;) {
        u32 now;
        pc_step(pc);
        now = now_us = micros();
        if (vga_step(pc->vga))
            show(now);
        if (now - last_input >= 20000) {
            controls();
            last_input = now;
        }
        if (pc->shutdown_state == 8)
            port_exit(0);
        /* Every 5 s on the console: how fast the PC runs, and the disk. */
        if (millis() - t0 >= 5000) {
            static long last_cycle;
            long c = cpui386_get_cycle(pc->cpu);
            const struct disk_stats *ds = disk_stats();
            printf("pcemu: %ld instructions/s; disk %u misses, %u sectors read, %u written\n",
                   (c - last_cycle) / 5, (unsigned)ds->misses, (unsigned)ds->fetched, (unsigned)ds->written);
            last_cycle = c;
            t0 += 5000;
        }
    }
}
