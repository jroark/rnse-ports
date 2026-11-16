/* The PC's screen on the 192's (400x235 of 8 bits through a palette):
 * the whole picture scaled down by averaging, or a part of it pixel for
 * pixel.  Shared by the unit's build and the host's. */
#ifndef WIN311_SCREEN_H
#define WIN311_SCREEN_H
#include <stdint.h>

#define SCREEN_W 400
#define SCREEN_H 235

/* What the PC shows now: where its picture is in Tiny386's frame
 * buffer (32 bits a pixel, 0x00RRGGBB) and how big; w 0: nothing. */
struct screen_view {
    int x, y, w, h;
};
struct VGAState;
void screen_view_get(struct VGAState *vga, struct screen_view *v);

/* The fixed palette: six levels a channel (those of the 16 VGA colours
 * among them, so that 1:1 is exact) and a ramp of greys. */
extern uint8_t pc_palette[256][3];
void screen_init(void);

/* The whole picture into SCREEN_W x SCREEN_H, each pixel the average
 * of the ones it covers. */
void screen_render(const struct screen_view *v, const uint8_t *fb, int pitch,
                   uint8_t *out, int out_pitch);
/* SCREEN_W x SCREEN_H of it from (x0, y0), pixel for pixel. */
void screen_render_pan(const struct screen_view *v, const uint8_t *fb, int pitch,
                       int x0, int y0, uint8_t *out, int out_pitch);
/* Windows' mode (16 colours in planes, 640 wide), straight from the
 * VGA's memory, without Tiny386 drawing its frame buffer: only the
 * groups of 8 pixels that changed since the last call are drawn again.
 * 0 when the picture is not that mode (then nothing is drawn); else
 * first..last are the rows that changed (first > last: none). */
struct vga_planar;
int screen_render_planar(const struct vga_planar *p, uint8_t *out, int out_pitch,
                         int *first, int *last);
/* Draw everything anew at the next screen_render_planar. */
void screen_planar_forget(void);

#endif
