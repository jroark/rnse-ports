/* Doom on the RNS-E 192: the picture.  Doom draws 320x200 at 8 bits
 * through a palette of 256 colours, which is what the 192's screen is
 * (400x235, the Coral chip's layer L4 as the SDK sets it up), so the
 * frame goes over as it is, centred, a word at a time (screen_blit),
 * and the palette straight into the chip's (six bits a colour, as a
 * VGA's).  Replaces doomgeneric's i_video.c. */
#include <string.h>
#include "rnse.h"
#include "port.h"

#include "config.h"
#include "doomtype.h"
/* (Doom's i_video.h has screen_width and screen_height of its own, for
 * a window's size: not the SDK's, and not used here.) */
#define screen_width doom_screen_width
#define screen_height doom_screen_height
#include "i_video.h"
#undef screen_width
#undef screen_height
#include "tables.h"
#include "v_video.h"
#include "z_zone.h"

#define X0 ((400 - SCREENWIDTH) / 2)  /* 40 */
#define Y0 ((235 - SCREENHEIGHT) / 2) /* 17 */

byte *I_VideoBuffer = NULL;
boolean screensaver_mode = false;
boolean screenvisible = true;
int usemouse = 1;
int usegamma = 0;
float mouse_acceleration = 2.0f;
int mouse_threshold = 10;
int vanilla_keyboard_mapping = 1;

static byte palette_now[256 * 3];

void I_InitInput(void);

void I_InitGraphics(void)
{
    I_VideoBuffer = (byte *)Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);
    screen_palette(0, 0, 0, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screenvisible = true;
    I_InitInput();
}

void I_ShutdownGraphics(void)
{
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    void I_GetEvent(void);
    I_GetEvent();
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    screen_blit(X0, Y0, SCREENWIDTH, SCREENHEIGHT, I_VideoBuffer, SCREENWIDTH);
    port_idle();
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    int i;
    for (i = 0; i < 256; i++) {
        byte r = gammatable[usegamma][palette[3 * i]];
        byte g = gammatable[usegamma][palette[3 * i + 1]];
        byte b = gammatable[usegamma][palette[3 * i + 2]];
        palette_now[3 * i] = r;
        palette_now[3 * i + 1] = g;
        palette_now[3 * i + 2] = b;
        screen_palette(i, r, g, b);
    }
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int i, best = 0, best_diff = 0x7fffffff;
    for (i = 0; i < 256; i++) {
        int dr = r - palette_now[3 * i], dg = g - palette_now[3 * i + 1], db = b - palette_now[3 * i + 2];
        int diff = dr * dr + dg * dg + db * db;
        if (diff < best_diff) {
            best = i;
            best_diff = diff;
            if (!diff)
                break;
        }
    }
    return best;
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

void I_SetWindowTitle(char *title)
{
    (void)title;
}

void I_GraphicsCheckCommandLine(void)
{
}

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
    (void)func;
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables(void)
{
}

void I_DisplayFPSDots(boolean dots_on)
{
    (void)dots_on;
}

void I_CheckIsScreensaver(void)
{
}
