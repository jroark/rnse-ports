/* Quake on the RNS-E 192: the picture (vid_null.c's place).
 *
 * The game draws 320x200 at 8 bits a pixel, through a palette, as the
 * PC's mode 13h had it; the 192's screen (Coral layer L4) is 400x235 at
 * 8 bits through 256 palette entries, so the frame goes onto it 1:1,
 * in the middle (screen_blit, sdk/src/hw192.c), and the game's palette
 * is the screen's.  The disc that the game shows while it reads its
 * data goes straight to the screen (D_BeginDirectRect). */
#include "quakedef.h"
#include "d_local.h"
#include "rnse.h"
#include "port.h"

#define BASEWIDTH 320
#define BASEHEIGHT 200
#define X0 ((400 - BASEWIDTH) / 2)
#define Y0 ((235 - BASEHEIGHT) / 2)

viddef_t vid;
static byte vid_buffer[BASEWIDTH * BASEHEIGHT] __attribute__((aligned(32)));
static short zbuffer[BASEWIDTH * BASEHEIGHT] __attribute__((aligned(32)));
static byte surfcache[512 * 1024] __attribute__((aligned(32)));
unsigned short d_8to16table[256];
unsigned d_8to24table[256];

/* menu.c's own, which menu.h does not declare. */
void M_Print(int cx, int cy, char *str);
void M_Menu_Options_f(void);

static void video_menu_draw(void)
{
    M_Print(16, 32, "The RNS-E shows Quake");
    M_Print(16, 40, "at 320x200 only.");
}

static void video_menu_key(int key)
{
    if (key == K_ESCAPE || key == K_ENTER)
        M_Menu_Options_f();
}

void (*vid_menudrawfn)(void) = video_menu_draw;
void (*vid_menukeyfn)(int key) = video_menu_key;

void VID_SetPalette(unsigned char *palette)
{
    int i;
    for (i = 0; i < 256; i++)
        screen_palette(i, palette[3 * i], palette[3 * i + 1], palette[3 * i + 2]);
}

void VID_ShiftPalette(unsigned char *palette)
{
    VID_SetPalette(palette);
}

void VID_Init(unsigned char *palette)
{
    vid.maxwarpwidth = vid.width = vid.conwidth = BASEWIDTH;
    vid.maxwarpheight = vid.height = vid.conheight = BASEHEIGHT;
    vid.aspect = 1.0;
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = vid_buffer;
    vid.rowbytes = vid.conrowbytes = BASEWIDTH;

    d_pzbuffer = zbuffer;
    D_InitCaches(surfcache, sizeof surfcache);
    screen_fill(0, 0, screen_width, screen_height, 0);
    VID_SetPalette(palette);
}

void VID_Shutdown(void)
{
}

void VID_Update(vrect_t *rects)
{
    /* The whole frame each time: 64 KB, which is quicker to copy than
     * the rectangles are to sort out. */
    (void)rects;
    screen_blit(X0, Y0, BASEWIDTH, BASEHEIGHT, vid_buffer, BASEWIDTH);
}

int VID_SetMode(int modenum, unsigned char *palette)
{
    return modenum == 0;
}

void VID_HandlePause(qboolean pause)
{
}

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height)
{
    screen_blit(X0 + x, Y0 + y, width, height, pbitmap, width);
}

void D_EndDirectRect(int x, int y, int width, int height)
{
    screen_blit(X0 + x, Y0 + y, width, height, vid_buffer + y * BASEWIDTH + x, BASEWIDTH);
}
