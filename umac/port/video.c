/* umac on the RNS-E 192: the picture.
 *
 * umac's ROM patch makes the Mac's screen 384 x 235 (DISP_WIDTH,
 * DISP_HEIGHT; the width has to be a multiple of 32), one bit a pixel,
 * the leftmost in the top bit, 1 black.  The 192's screen is 400 x 235
 * at 8 bits through a palette, so the Mac sits 1:1 at x = 8 with an
 * 8-pixel margin either side.  Each byte of the frame buffer becomes
 * eight pixels through a table of two words, and only the lines that
 * changed since the last update go over the bus (screen_blit): the
 * whole picture is 90 KB, and a Mac at rest changes nothing. */
#include <string.h>
#include "rnse.h"
#include "umac.h"
#include "port.h"

#define ROW_BYTES (DISP_WIDTH / 8)

static u32 expand[256][2];
static u8 shadow[DISP_HEIGHT][ROW_BYTES];
static u32 line[DISP_HEIGHT][DISP_WIDTH / 4];

void video_init(void)
{
    int b, i;
    screen_palette(C_WHITE, 255, 255, 255);
    screen_palette(C_BLACK, 0, 0, 0);
    screen_palette(C_MARK, 255, 96, 0);
    /* Big-endian words: the first pixel in the top byte (screen_blit
     * turns the bytes round for the chip itself). */
    for (b = 0; b < 256; b++)
        for (i = 0; i < 8; i++) {
            u32 c = (b & (0x80 >> i)) ? C_BLACK : C_WHITE;
            expand[b][i >> 2] |= c << (8 * (3 - (i & 3)));
        }
    screen_fill(0, 0, screen_width, screen_height, C_BLACK);
    video_update(1);
}

void video_update(int full)
{
    const u8 *fb = ram_get_base() + umac_get_fb_offset();
    int y = 0;
    while (y < DISP_HEIGHT) {
        int first, x;
        /* The next run of changed lines, expanded as it is found. */
        while (y < DISP_HEIGHT && !full && !memcmp(shadow[y], fb + y * ROW_BYTES, ROW_BYTES))
            y++;
        first = y;
        while (y < DISP_HEIGHT && (full || memcmp(shadow[y], fb + y * ROW_BYTES, ROW_BYTES))) {
            const u8 *src = fb + y * ROW_BYTES;
            u32 *dst = line[y];
            memcpy(shadow[y], src, ROW_BYTES);
            for (x = 0; x < ROW_BYTES; x++) {
                *dst++ = expand[src[x]][0];
                *dst++ = expand[src[x]][1];
            }
            y++;
        }
        if (y > first)
            screen_blit(MAC_X0, first, DISP_WIDTH, y - first, (const u8 *)line[first], DISP_WIDTH);
    }
}

void video_mark(int on)
{
    screen_fill(1, 1, MAC_X0 - 2, 12, on ? C_MARK : C_BLACK);
}
