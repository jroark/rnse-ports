/* See screen.h. */
#include <string.h>
#include "screen.h"
#include "vga.h"

void screen_view_get(struct VGAState *vga, struct screen_view *v)
{
    vga_picture_rect(vga, &v->x, &v->y, &v->w, &v->h);
}

uint8_t pc_palette[256][3];

/* Six levels a channel: 0, 128, 192 and 255 are the VGA's own (the
 * colours Windows draws with), 64 and 160 between them for averages. */
static const uint8_t level[6] = { 0, 64, 128, 160, 192, 255 };
static uint8_t to_level[256];  /* channel value -> level index */
#define GREYS 40
static uint8_t grey_index[256]; /* grey value -> palette index */

static int nearest_level(int v)
{
    int best = 0;
    for (int i = 1; i < 6; i++) {
        int d = v - level[i], e = v - level[best];
        if (d * d < e * e)
            best = i;
    }
    return best;
}

void screen_init(void)
{
    int n = 0;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++, n++) {
                pc_palette[n][0] = level[r];
                pc_palette[n][1] = level[g];
                pc_palette[n][2] = level[b];
            }
    for (int i = 0; i < GREYS; i++) {
        uint8_t v = (uint8_t)((i + 1) * 255 / (GREYS + 1));
        pc_palette[216 + i][0] = pc_palette[216 + i][1] = pc_palette[216 + i][2] = v;
    }
    for (int v = 0; v < 256; v++)
        to_level[v] = (uint8_t)nearest_level(v);
    /* Greys: the nearest of the ramp and the cube's own six. */
    for (int v = 0; v < 256; v++) {
        int best = to_level[v] * 43, bd = v - level[to_level[v]];
        bd *= bd;
        for (int i = 0; i < GREYS; i++) {
            int d = v - pc_palette[216 + i][0];
            if (d * d < bd)
                bd = d * d, best = 216 + i;
        }
        grey_index[v] = (uint8_t)best;
    }
}

static inline uint8_t index_of(uint32_t r, uint32_t g, uint32_t b)
{
    if (r == g && g == b)
        return grey_index[r];
    return (uint8_t)(to_level[r] * 36 + to_level[g] * 6 + to_level[b]);
}

static inline uint8_t index_of_pixel(uint32_t p)
{
    return index_of((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff);
}

/* ---- Any picture, from Tiny386's frame buffer ------------------------- */

/* Area averaging in 1/16ths of a source pixel; the averages without a
 * division a pixel (the SH-4 has no divider): sums multiplied by
 * reciprocals of the column's and the row's weights. */
void screen_render(const struct screen_view *v, const uint8_t *fb, int pitch,
                   uint8_t *out, int out_pitch)
{
    int w = v->w, h = v->h;
    fb += v->y * pitch + v->x * 4;
    if (w <= 0 || h <= 0 || w > 1024 || h > 768) {
        for (int y = 0; y < SCREEN_H; y++)
            memset(out + y * out_pitch, 0, SCREEN_W);
        return;
    }
    /* Step in source pixels per output pixel, 16.16 fixed point. */
    uint32_t sx = ((uint32_t)w << 16) / SCREEN_W, sy = ((uint32_t)h << 16) / SCREEN_H;
    static uint16_t c0[SCREEN_W], c1[SCREEN_W];
    static uint8_t w0[SCREEN_W], w1[SCREEN_W];
    static uint32_t inv_x[SCREEN_W];
    static int last_w;
    if (w != last_w) {
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t a = x * sx, b = a + sx, sum = 0;  /* 16.16 */
            c0[x] = a >> 16;
            c1[x] = (b - 1) >> 16;
            if (c0[x] == c1[x]) {
                w0[x] = (b - a) >> 12; w1[x] = 0;
            } else {
                w0[x] = (0x10000 - (a & 0xffff)) >> 12;
                w1[x] = ((((b - 1) & 0xffff) + 1) + 0xfff) >> 12;
            }
            for (int c = c0[x]; c <= c1[x]; c++)
                sum += c == c0[x] ? w0[x] : c == c1[x] ? w1[x] : 16;
            inv_x[x] = sum ? (1u << 16) / sum : 0;
        }
        last_w = w;
    }
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t a = y * sy, b = a + sy, wsum = 0, wy[8];
        int r0 = a >> 16, r1 = (b - 1) >> 16;
        if (r1 - r0 > 7)
            r1 = r0 + 7;
        for (int r = r0; r <= r1; r++) {
            if (r0 == r1) wy[r - r0] = (b - a) >> 12;
            else if (r == r0) wy[r - r0] = (0x10000 - (a & 0xffff)) >> 12;
            else if (r == r1) wy[r - r0] = ((((b - 1) & 0xffff) + 1) + 0xfff) >> 12;
            else wy[r - r0] = 16;
            wsum += wy[r - r0];
        }
        uint32_t inv_y = wsum ? (1u << 16) / wsum : 0;
        uint8_t *o = out + y * out_pitch;
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t R = 0, G = 0, B = 0;
            for (int r = r0; r <= r1; r++) {
                const uint32_t *row = (const uint32_t *)(fb + r * pitch);
                uint32_t rr = 0, gg = 0, bb = 0;
                for (int c = c0[x]; c <= c1[x]; c++) {
                    uint32_t wx = c == c0[x] ? w0[x] : c == c1[x] ? w1[x] : 16, p = row[c];
                    rr += ((p >> 16) & 0xff) * wx;
                    gg += ((p >> 8) & 0xff) * wx;
                    bb += (p & 0xff) * wx;
                }
                R += rr * wy[r - r0];
                G += gg * wy[r - r0];
                B += bb * wy[r - r0];
            }
            /* sums of at most 255 x 16 x 16 x (a few) pixels */
            R = ((R >> 4) * inv_x[x] >> 12) * inv_y >> 16;
            G = ((G >> 4) * inv_x[x] >> 12) * inv_y >> 16;
            B = ((B >> 4) * inv_x[x] >> 12) * inv_y >> 16;
            o[x] = index_of(R > 255 ? 255 : R, G > 255 ? 255 : G, B > 255 ? 255 : B);
        }
    }
}

void screen_render_pan(const struct screen_view *v, const uint8_t *fb, int pitch,
                       int x0, int y0, uint8_t *out, int out_pitch)
{
    fb += v->y * pitch + v->x * 4;
    if (x0 > v->w - SCREEN_W) x0 = v->w - SCREEN_W;
    if (y0 > v->h - SCREEN_H) y0 = v->h - SCREEN_H;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t *o = out + y * out_pitch;
        if (y0 + y >= v->h) {
            memset(o, 0, SCREEN_W);
            continue;
        }
        const uint32_t *row = (const uint32_t *)(fb + (y0 + y) * pitch) + x0;
        for (int x = 0; x < SCREEN_W; x++)
            o[x] = x0 + x < v->w ? index_of_pixel(row[x]) : 0;
    }
}

/* ---- Windows' mode, from the VGA's planes ----------------------------- */

/* A byte of a plane -> its eight bits, each in the lowest bit of a
 * nibble, the leftmost pixel (bit 7) in the highest nibble. */
static uint32_t expand[256];
/* What was drawn from: each line's words (four planes, eight pixels). */
#define MAX_LINES 480
#define MAX_GROUPS 80
static uint32_t seen[MAX_LINES][MAX_GROUPS];
static uint32_t seen_palette[16];
static int seen_valid, seen_h;

static const uint32_t *line_words(const struct vga_planar *p, int y)
{
    uint32_t at = (p->start + (uint32_t)y * p->line_offset) & 0x3ffff;
    if (at + MAX_GROUPS * 4 > p->vram_size || (at & 3))
        return NULL;
    return (const uint32_t *)(p->vram + at);
}

/* The nibbles of a group's eight pixels, from its word (plane 0 the
 * first byte in memory, whatever the host's byte order). */
static inline uint32_t pixels_of(const uint32_t *word)
{
    const uint8_t *b = (const uint8_t *)word;
    return expand[b[0]] | expand[b[1]] << 1 | expand[b[2]] << 2 | expand[b[3]] << 3;
}

int screen_render_planar(const struct vga_planar *p, uint8_t *out, int out_pitch,
                         int *first, int *last)
{
    /* 640 across, 8 pixels -> 5: each source pixel's weight in each
     * output pixel, in fifths of a pixel (each output pixel's sum: 8). */
    static const uint8_t wx[5][8] = {
        { 5, 3, 0, 0, 0, 0, 0, 0 },
        { 0, 2, 5, 1, 0, 0, 0, 0 },
        { 0, 0, 0, 4, 4, 0, 0, 0 },
        { 0, 0, 0, 0, 1, 5, 2, 0 },
        { 0, 0, 0, 0, 0, 0, 3, 5 },
    };
    /* Down: the 2 or 3 lines a row covers, weighted 2 2 or 1 2 1 (sum 4). */
    static const uint8_t wy[4][3] = { { 0 }, { 4, 0, 0 }, { 2, 2, 0 }, { 1, 2, 1 } };
    uint32_t rb[16], g[16];
    *first = SCREEN_H;
    *last = -1;
    if (p->w != 640 || p->h > MAX_LINES || p->h < SCREEN_H)
        return 0;
    if (!expand[1])
        for (int v = 0; v < 256; v++) {
            uint32_t e = 0;
            for (int bit = 0; bit < 8; bit++)
                if (v & (0x80 >> bit))
                    e |= 1u << (4 * (7 - bit));
            expand[v] = e;
        }
    if (!seen_valid || seen_h != p->h || memcmp(seen_palette, p->palette, sizeof seen_palette)) {
        memcpy(seen_palette, p->palette, sizeof seen_palette);
        seen_valid = 1;
        seen_h = p->h;
        memset(seen, 0x55, sizeof seen);
        /* (a word no line has: the planes' bytes then differ from it in
         * at least one place for every group drawn below) */
        for (int y = 0; y < MAX_LINES; y++)
            for (int x = 0; x < MAX_GROUPS; x++)
                seen[y][x] = ~(line_words(p, y < p->h ? y : 0) ? line_words(p, y < p->h ? y : 0)[x] : 0);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t c = p->palette[i];
        rb[i] = c & 0xff00ff;
        g[i] = (c >> 8) & 0xff;
    }
    for (int y = 0; y < SCREEN_H; y++) {
        int a = y * p->h / SCREEN_H, b = (y + 1) * p->h / SCREEN_H;
        int n = b - a > 3 ? 3 : b - a < 1 ? 1 : b - a;
        const uint32_t *src[3];
        int row_changed = 0;
        for (int k = 0; k < n; k++) {
            src[k] = line_words(p, a + k);
            if (!src[k])
                return 0;
        }
        uint8_t *o = out + y * out_pitch;
        for (int gx = 0; gx < MAX_GROUPS; gx++) {
            int same = 1;
            for (int k = 0; k < n; k++)
                if (src[k][gx] != seen[a + k][gx]) {
                    same = 0;
                    break;
                }
            if (same)
                continue;
            uint32_t srb[5] = { 0 }, sg[5] = { 0 };
            for (int k = 0; k < n; k++) {
                uint32_t px = pixels_of(&src[k][gx]), wk = wy[n][k];
                seen[a + k][gx] = src[k][gx];
                for (int i = 0; i < 8; i++) {
                    uint32_t c = (px >> (4 * (7 - i))) & 15;
                    for (int j = 0; j < 5; j++)
                        if (wx[j][i]) {
                            uint32_t w = wx[j][i] * wk;
                            srb[j] += rb[c] * w;
                            sg[j] += g[c] * w;
                        }
                }
            }
            /* each output pixel's weights add up to 8 x 4 = 32 */
            for (int j = 0; j < 5; j++)
                o[gx * 5 + j] = index_of(srb[j] >> 21, sg[j] >> 5, (srb[j] & 0xffff) >> 5);
            row_changed = 1;
        }
        if (row_changed) {
            if (y < *first)
                *first = y;
            *last = y;
        }
    }
    return 1;
}

void screen_planar_forget(void)
{
    seen_valid = 0;
}
