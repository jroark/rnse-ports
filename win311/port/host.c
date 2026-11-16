/* The PC emulator (Tiny386) on the Mac or Linux, headless: the same
 * machine as on the 192, for quick runs and for checking the port.
 *
 *   pcemu-host [options] tiny386.ini
 *     --shot MS:FILE.png   the screen at MS milliseconds of run time,
 *                          as the PC draws it (640x480 region and all)
 *     --small MS:FILE.png  the same as the 192 would show it (400x240)
 *     --until MS           stop then (default: after the last shot)
 *     --mouse MS:DX,DY,B   a mouse report (B: bit 0 left, bit 1 right)
 *     --key MS:CODE        a key pressed and let go (set-1 scan code, hex)
 *     --virtual            time from the instruction count instead of
 *                          the clock (the same run every time)
 *     --pool MB            RAM for the disk (default 32, as on the 192)
 *
 * The disk (hda of the ini file) is read as on the 192 (disk.c): what
 * the PC writes stays in RAM, and the file is never changed.
 *
 * Built by win311/host.sh; no libraries beyond the C library.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pc.h"
#include "screen.h"
#include "disk.h"

static int virtual_time;
static uint64_t steps;

uint32_t get_uticks(void)
{
    if (virtual_time)
        return (uint32_t)(steps * 1000 / 4); /* steps of 1 ms / 4, a guess */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000000 + (uint32_t)ts.tv_nsec / 1000;
}

/* The PC's clock (misc.c with RNSE): this machine's, in local time. */
void rnse_tm(struct tm *tm)
{
    time_t t = time(NULL);
    localtime_r(&t, tm);
}

void *bigmalloc(size_t size)
{
    return calloc(1, size);
}

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
    FILE *fp = fopen(file, "rb");
    long len;
    if (!fp) {
        fprintf(stderr, "load_rom: cannot open %s\n", file);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    if (fread((char *)phys_mem + addr - (backward ? len : 0), 1, len, fp) != (size_t)len)
        exit(1);
    fclose(fp);
    return (int)len;
}

/* The disk image as the 192 has it (disk.c): read as needed, written
 * to RAM only, so the file is never changed. */
static FILE *image;
static int image_read(uint64_t offset, void *buf, uint32_t n)
{
    if (fseeko(image, (off_t)offset, SEEK_SET))
        return -1;
    return fread(buf, 1, n, image) == n ? 0 : -1;
}

static void open_disk(const char *name, long pool_mb)
{
    image = fopen(name, "rb");
    if (!image) {
        perror(name);
        exit(1);
    }
    fseeko(image, 0, SEEK_END);
    uint32_t sectors = (uint32_t)(ftello(image) / 512);
    uint32_t pool = (uint32_t)(pool_mb << 20);
    if (disk_init(sectors, image_read, malloc(disk_map_bytes(sectors)), malloc(pool), pool)) {
        fprintf(stderr, "%s: no room\n", name);
        exit(1);
    }
}

static void redraw(void *opaque, int x, int y, int w, int h)
{
}

/* PNG without compression (stored deflate blocks): no zlib needed. */
static uint32_t crc_table[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++)
                v = v & 1 ? 0xedb88320 ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static void chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(data, 1, n, f);
    uint32_t c = crc(crc(0, (const uint8_t *)type, 4), data, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}
/* rgb: w*h*3 bytes */
static void write_png(const char *name, const uint8_t *rgb, int w, int h)
{
    size_t row = (size_t)w * 3 + 1, raw_n = row * h;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    size_t blocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t at = 0; at < raw_n; at += 65535) {
        size_t n = raw_n - at < 65535 ? raw_n - at : 65535;
        *q++ = at + n == raw_n;
        *q++ = n; *q++ = n >> 8; *q++ = ~n; *q++ = ~n >> 8;
        memcpy(q, raw + at, n);
        q += n;
    }
    be32(q, (b << 16) | a);
    FILE *f = fopen(name, "wb");
    if (!f) {
        perror(name);
        exit(1);
    }
    uint8_t ihdr[13];
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, z_n);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

enum { EV_SHOT, EV_SMALL, EV_MOUSE, EV_KEY };
struct event {
    long ms;
    int kind;
    const char *file;
    int a, b, c;
};

static struct event events[256];
static int n_events;

static void add(long ms, int kind, const char *file, int a, int b, int c)
{
    if (n_events == 256)
        exit(2);
    events[n_events++] = (struct event){ ms, kind, file, a, b, c };
}

static uint8_t *fb;
static int fb_w = 720, fb_h = 480;

static void shot(PC *pc, const char *file, int small)
{
    struct screen_view v;
    screen_view_get(pc->vga, &v);
    if (!small) {
        if (!v.w) {
            v.w = fb_w, v.h = fb_h, v.x = v.y = 0;
        }
        uint8_t *rgb = malloc((size_t)v.w * v.h * 3);
        for (int y = 0; y < v.h; y++)
            for (int x = 0; x < v.w; x++) {
                uint32_t p = ((uint32_t *)fb)[(v.y + y) * fb_w + v.x + x];
                uint8_t *o = rgb + ((size_t)y * v.w + x) * 3;
                o[0] = p >> 16; o[1] = p >> 8; o[2] = p;
            }
        write_png(file, rgb, v.w, v.h);
        free(rgb);
    } else {
        static uint8_t idx[SCREEN_W * SCREEN_H];
        static uint8_t rgb[SCREEN_W * SCREEN_H * 3];
        struct vga_planar p;
        int first, last;
        /* as the 192 draws it: Windows' mode from the planes */
        if (!vga_planar_get(pc->vga, &p) || !screen_render_planar(&p, idx, SCREEN_W, &first, &last))
            screen_render(&v, fb, fb_w * 4, idx, SCREEN_W);
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            const uint8_t *c = pc_palette[idx[i]];
            rgb[i * 3] = c[0]; rgb[i * 3 + 1] = c[1]; rgb[i * 3 + 2] = c[2];
        }
        write_png(file, rgb, SCREEN_W, SCREEN_H);
    }
    fprintf(stderr, "shot %s (%dx%d mode)\n", file, v.w, v.h);
}

int main(int argc, char **argv)
{
    PCConfig conf;
    long until = -1, pool_mb = 32;
    const char *ini = NULL;
    memset(&conf, 0, sizeof(conf));
    conf.mem_size = 16 * 1024 * 1024;
    conf.vga_mem_size = 256 * 1024;
    conf.width = fb_w;
    conf.height = fb_h;
    conf.cpu_gen = 4;
    conf.fpu = 0;
    for (int i = 1; i < argc; i++) {
        char *s = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL, *c;
        long ms = v ? strtol(v, &c, 10) : 0;
        if (!strcmp(s, "--pool") && v) {
            pool_mb = ms, i++;
        } else if (!strcmp(s, "--virtual")) {
            virtual_time = 1;
        } else if (!strcmp(s, "--until") && v) {
            until = ms, i++;
        } else if ((!strcmp(s, "--shot") || !strcmp(s, "--small")) && v && *c == ':') {
            add(ms, s[2] == 's' && s[3] == 'h' ? EV_SHOT : EV_SMALL, c + 1, 0, 0, 0), i++;
        } else if (!strcmp(s, "--mouse") && v && *c == ':') {
            int dx, dy, b;
            if (sscanf(c + 1, "%d,%d,%d", &dx, &dy, &b) != 3)
                return 2;
            add(ms, EV_MOUSE, NULL, dx, dy, b), i++;
        } else if (!strcmp(s, "--key") && v && *c == ':') {
            add(ms, EV_KEY, NULL, (int)strtol(c + 1, NULL, 16), 0, 0), i++;
        } else if (s[0] != '-' && !ini) {
            ini = s;
        } else {
            fprintf(stderr, "usage: %s [--shot MS:FILE] [--small MS:FILE] [--until MS] "
                    "[--mouse MS:DX,DY,B] [--key MS:HEX] [--virtual] tiny386.ini\n", argv[0]);
            return 2;
        }
    }
    if (!ini)
        return 2;
    int err = ini_parse(ini, parse_conf_ini, &conf);
    if (err) {
        fprintf(stderr, "%s: error %d\n", ini, err);
        return 1;
    }
    if (until < 0)
        for (int i = 0; i < n_events; i++)
            if (events[i].ms + 1 > until)
                until = events[i].ms + 1;
    if (!conf.disks[0]) {
        fprintf(stderr, "%s: no hda\n", ini);
        return 1;
    }
    open_disk(conf.disks[0], pool_mb);
    fb_w = conf.width;
    fb_h = conf.height;
    fb = bigmalloc((size_t)fb_w * fb_h * 4);
    screen_init();
    PC *pc = pc_new(redraw, NULL, fb, &conf);
    load_bios_and_reset(pc);
    uint32_t start = get_uticks();
    pc->boot_start_time = start;
    int done[256] = { 0 };
    for (;;) {
        pc_step(pc);
        pc_vga_step(pc);
        steps++;
        if (pc->shutdown_state == 8)
            break;
        if ((steps & 255) == 0) {
            long now = (long)((get_uticks() - start) / 1000);
            for (int i = 0; i < n_events; i++) {
                struct event *e = &events[i];
                if (done[i] || now < e->ms)
                    continue;
                done[i] = 1;
                if (e->kind == EV_SHOT || e->kind == EV_SMALL) {
                    shot(pc, e->file, e->kind == EV_SMALL);
                } else if (e->kind == EV_MOUSE) {
                    ps2_mouse_event(pc->mouse, e->a, e->b, 0, e->c);
                } else {
                    ps2_put_keycode(pc->kbd, 1, e->a);
                    ps2_put_keycode(pc->kbd, 0, e->a);
                }
            }
            if (until >= 0 && now >= until)
                break;
        }
    }
    const struct disk_stats *ds = disk_stats();
    fprintf(stderr, "disk: %u hits, %u misses, %u sectors read, %u written, %u evicted, %u full\n",
            ds->hits, ds->misses, ds->fetched, ds->written, ds->evicted, ds->full);
    return 0;
}
