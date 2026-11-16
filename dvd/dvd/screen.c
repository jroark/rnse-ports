/* The player's screen.  The menus use the kit's 8-bit surface (its
 * screen_* functions) with a palette of 3-3-2 bits.  Pictures go to
 * two surfaces of their own, one shown while the other is drawn, at 16
 * bits a pixel (5-5-5) by default: the stock software shows 16-bit
 * layers on this chip, but through its extended mode, and that the
 * kit's layer (the loader's "BL") takes 16 bits in this mode is the
 * documented chip's behaviour, not something seen on a unit.  So CAR
 * switches to 8 bits a pixel (the 3-3-2 palette, as sdk/examples/
 * h264bench showed on a unit) should a unit show otherwise.
 *
 * The picture is scaled to the screen keeping its shape (16:9 at
 * 400x225, 4:3 at 313x235; the panel's pixels taken as square), from
 * the top field's lines: 240 of a frame's 480 are as many as the
 * screen has, and one field shows no combing. */
#include "player.h"

#define VRAM 0xb8000000
#define CORAL_DISP 0xb9fd0000
#define MENU_SURFACE 0x00110000
#define VIDEO_SURFACE 0x00200000 /* and + 0x40000 */
#define STRIDE16 832             /* 13 x 64 bytes */
#define STRIDE8 448
#define W 400
#define H 235

int screen_current = -1;
static int back;          /* the surface drawn into: 0 or 1 */
static u16 *lr, *lg, *lb; /* clamped colour components, 16 bits */
static u8 *cr8, *cg8, *cb8; /* the same for 3-3-2 */
static int *ty, *tv_r, *tv_g, *tu_g, *tu_b;
/* A pixel from 5 bits each of U, V and Y: rgb16[u << 10 | v << 5 | y]
 * (and rgb8 for 3-3-2); the screen has 5 bits a colour at most. */
static u16 *rgb16;
static u8 *rgb8;
static u16 xmap[W], ymap[H];
static int map_w, map_h, map_x, map_y, map_sw, map_lines, map_mode;

#define CLAMP_LOW 400 /* the tables cover -400 .. 655 */
#define CLAMP_SIZE 1056

static void layer(u32 mode, u32 surface)
{
    REG32(CORAL_DISP + 0x70) = mode;
    REG32(CORAL_DISP + 0x74) = surface;
    REG32(CORAL_DISP + 0x78) = surface;
}

static u32 surface_of(int which)
{
    return VIDEO_SURFACE + (u32)which * 0x40000;
}

static void clear_surface(int which, int mode)
{
    u32 i, n = mode == SCREEN_VIDEO16 ? STRIDE16 * 240 : STRIDE8 * 240;
    volatile u32 *p = (volatile u32 *)(VRAM + surface_of(which));
    for (i = 0; i < n / 4; i++)
        p[i] = 0;
}

void screen_mode(int mode)
{
    if (mode == screen_current)
        return;
    screen_current = mode;
    if (mode == SCREEN_MENU) {
        layer(0x00070400, MENU_SURFACE);
        return;
    }
    clear_surface(0, mode);
    clear_surface(1, mode);
    map_w = 0;
    back = 1;
    if (mode == SCREEN_VIDEO16)
        layer(0x800d0400, surface_of(0));
    else
        layer(0x00070400, surface_of(0));
}

void screen_init(void)
{
    int i;
    lr = malloc(CLAMP_SIZE * 2);
    lg = malloc(CLAMP_SIZE * 2);
    lb = malloc(CLAMP_SIZE * 2);
    cr8 = malloc(CLAMP_SIZE);
    cg8 = malloc(CLAMP_SIZE);
    cb8 = malloc(CLAMP_SIZE);
    ty = malloc(256 * 4);
    tv_r = malloc(256 * 4);
    tv_g = malloc(256 * 4);
    tu_g = malloc(256 * 4);
    tu_b = malloc(256 * 4);
    for (i = 0; i < CLAMP_SIZE; i++) {
        int v = i - CLAMP_LOW;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        lr[i] = (u16)((v >> 3) << 10);
        lg[i] = (u16)((v >> 3) << 5);
        lb[i] = (u16)(v >> 3);
        cr8[i] = (u8)(v & 0xe0);
        cg8[i] = (u8)((v & 0xe0) >> 3);
        cb8[i] = (u8)(v >> 6);
    }
    /* ITU-R BT.601, studio range; the offset of the clamping tables
     * folded into the luma. */
    for (i = 0; i < 256; i++) {
        ty[i] = ((i - 16) * 298 + 128) / 256 + CLAMP_LOW;
        tv_r[i] = (409 * (i - 128)) / 256;
        tv_g[i] = -(208 * (i - 128)) / 256;
        tu_g[i] = -(100 * (i - 128)) / 256;
        tu_b[i] = (516 * (i - 128)) / 256;
    }
    rgb16 = malloc(32768 * 2);
    rgb8 = malloc(32768);
    for (i = 0; i < 32768; i++) {
        int u = (i >> 10) * 8 + 4, v = ((i >> 5) & 31) * 8 + 4, l = ty[(i & 31) * 8 + 4];
        int r = l + tv_r[v], g = l + tu_g[u] + tv_g[v], b = l + tu_b[u];
        rgb16[i] = (u16)(lr[r] | lg[g] | lb[b]);
        rgb8[i] = (u8)(cr8[r] | cg8[g] | cb8[b]);
    }
    for (i = 0; i < 256; i++)
        screen_palette(i, (u8)((i >> 5) * 255 / 7), (u8)(((i >> 2) & 7) * 255 / 7), (u8)((i & 3) * 255 / 3));
    screen_mode(SCREEN_MENU);
}

static void geometry(const struct frame *f)
{
    int sw = video.stored_width, lines = video.height, x, y;
    (void)f;
    if (map_w && sw == map_sw && lines == map_lines && video.aspect == map_mode)
        return;
    map_sw = sw;
    map_lines = lines;
    map_mode = video.aspect;
    if (video.aspect == 3 || video.aspect == 4) {
        map_w = W;
        map_h = 225;
    } else {
        map_w = 313;
        map_h = H;
    }
    map_x = (W - map_w) / 2;
    map_y = (H - map_h) / 2;
    for (x = 0; x < map_w; x++)
        xmap[x] = (u16)(x * sw / map_w);
    /* Pictures of a frame's height: the top field's lines; those of
     * 288 lines or fewer (MPEG-1 and half-height pictures, one field
     * of their own) all of theirs. */
    for (y = 0; y < map_h; y++)
        ymap[y] = (u16)(lines > 288 ? 2 * (y * (lines / 2) / map_h) : y * lines / map_h);
    clear_surface(0, screen_current);
    clear_surface(1, screen_current);
}

static void convert16(const struct frame *f, u32 base)
{
    int x, y, stride = video.stride, cs = stride / 2;
    for (y = 0; y < map_h; y++) {
        int line = ymap[y];
        const u8 *yr = f->y + line * stride;
        int cl = video.height > 288 ? (line / 2) & ~1 : line / 2;
        const u8 *ur = f->cb + cl * cs, *vr = f->cr + cl * cs;
        volatile u32 *out = (volatile u32 *)(VRAM + base + (u32)(map_y + y) * STRIDE16 + (u32)map_x * 2);
        const u16 *mx = xmap;
        for (x = 0; x < map_w - 1; x += 2, mx += 2) {
            int s0 = mx[0], s1 = mx[1], c = s0 >> 1;
            const u16 *t = rgb16 + ((ur[c] >> 3) << 10 | (vr[c] >> 3) << 5);
            /* Pixel N is the halfword at 2N ^ 2: the first of a pair
             * is the lower half of the long word.  (The pair shares
             * the first one's chroma.) */
            *out++ = (u32)t[yr[s1] >> 3] << 16 | t[yr[s0] >> 3];
        }
    }
}

static void convert8(const struct frame *f, u32 base)
{
    int x, y, stride = video.stride, cs = stride / 2;
    for (y = 0; y < map_h; y++) {
        int line = ymap[y];
        const u8 *yr = f->y + line * stride;
        int cl = video.height > 288 ? (line / 2) & ~1 : line / 2;
        const u8 *ur = f->cb + cl * cs, *vr = f->cr + cl * cs;
        u32 start = (u32)(map_y + y) * STRIDE8 + (u32)map_x;
        for (x = 0; x < map_w; x++) {
            int s = xmap[x], c = s >> 1;
            REG8(VRAM + base + ((start + (u32)x) ^ 3)) = rgb8[(ur[c] >> 3) << 10 | (vr[c] >> 3) << 5 | yr[s] >> 3];
        }
    }
}

/* Text over the picture: white on a dark band. */
static void text(u32 base, int y, const char *s)
{
    int x0 = 4, x, row, col, n = 0;
    while (s[n])
        n++;
    /* The band behind the text, as wide as the text: long words. */
    {
        u32 bytes = screen_current == SCREEN_VIDEO16 ? (u32)(x0 + 6 * n + 4) * 2 : (u32)(x0 + 6 * n + 4);
        u32 stride = screen_current == SCREEN_VIDEO16 ? STRIDE16 : STRIDE8;
        bytes = (bytes + 3) & ~3u;
        for (row = y - 2; row < y + 9; row++) {
            volatile u32 *q = (volatile u32 *)(VRAM + base + (u32)row * stride);
            u32 k;
            for (k = 0; k < bytes / 4; k++)
                q[k] = 0;
        }
    }
    for (x = x0; *s; s++, x += 6) {
        const u8 *g = font_glyph(*s);
        if (!g)
            continue;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col)) {
                    u32 px = (u32)(x + col), py = (u32)(y + row);
                    if (screen_current == SCREEN_VIDEO16)
                        REG16(VRAM + base + py * STRIDE16 + ((px * 2) ^ 2)) = 0x7fff;
                    else
                        REG8(VRAM + base + ((py * STRIDE8 + px) ^ 3)) = 0xff;
                }
    }
}

void screen_video(const struct frame *f, const char *osd1, const char *osd2)
{
    u32 base;
    if (screen_current == SCREEN_MENU)
        return;
    geometry(f);
    base = surface_of(back);
    if (f) {
        if (screen_current == SCREEN_VIDEO16)
            convert16(f, base);
        else
            convert8(f, base);
    }
    if (osd1)
        text(base, H - 22, osd1);
    if (osd2)
        text(base, H - 11, osd2);
    REG32(CORAL_DISP + 0x74) = base;
    REG32(CORAL_DISP + 0x78) = base;
    back ^= 1;
}
