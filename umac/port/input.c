/* umac on the RNS-E 192: the faceplate as the Mac's mouse and a few of
 * its keys.
 *
 * The select knob moves the pointer left and right, the volume knob up
 * and down (umac has no sound, so that knob is free), clockwise to the
 * right and down; the faster a knob turns, the further a detent goes.
 * SELECT, the knob's press, is the button: held, it drags.  A press too
 * short to have been seen down (panel_pressed) still becomes a click,
 * held for long enough that the Mac's tick sees it.
 *
 * Keys: the soft keys are Return (bottom right), Delete (bottom left),
 * Tab (top right) and Command (top left), the last sticky: it holds
 * Command down for the next of these keys, with a mark in the left
 * margin while armed, and a second press disarms it.  SETUP is
 * Command-period, the Mac's cancel.  The arrow keys are left out: the
 * Plus's keyboard sends them through its keypad prefix, which umac's
 * keyboard does not model.  RETURN, the display key and CD EJECT keep
 * the kit's meaning (panel_service).  See docs/umac.md, "The controls". */
#include "rnse.h"
#include "umac.h"
#include "keymap.h"
#include "port.h"

#define CLICK_US 60000   /* a latched click: down this long, then up as long */
#define FAST_US 90000    /* detents closer than this speed the pointer up */
#define STEP_MIN 2       /* pixels a detent, slowly */
#define STEP_MAX 12      /* ... and at the fastest */

struct knob {
    u32 last;
    int step;
};
static struct knob knob_x, knob_y;
static int command_armed;
static int clicks;             /* latched presses still to be given */
static int click_phase;        /* 0 idle, 1 down, 2 up (the gap) */
static u32 click_at;

static int detents(struct knob *k, int n, u32 now)
{
    if (!n)
        return 0;
    if (now - k->last < FAST_US) {
        if (k->step < STEP_MAX)
            k->step++;
    } else {
        k->step = STEP_MIN;
    }
    k->last = now;
    return n * k->step;
}

static void key(int code, int down)
{
    umac_kbd_event((u8)((code << 1) | 1), down);
}

static void disarm(void)
{
    if (command_armed) {
        command_armed = 0;
        video_mark(0);
    }
}

/* A key typed, with Command if it is armed (or forced).  umac queues
 * the transitions and the Mac polls them in order. */
static void type(int code, int with_command)
{
    int c = command_armed || with_command;
    if (c)
        key(MKC_Command, 1);
    key(code, 1);
    key(code, 0);
    if (c)
        key(MKC_Command, 0);
    disarm();
}

void input_poll(void)
{
    u32 now = micros();
    int dx, dy, held, button;

    panel_service();

    dx = detents(&knob_x, panel_knob(KNOB_SELECT), now);
    dy = detents(&knob_y, panel_knob(KNOB_VOLUME), now);

    /* The button: held as long as SELECT is, or a latched click. */
    held = panel_key(KEY_SELECT);
    if (panel_pressed(KEY_SELECT) && clicks < 2)
        clicks++;
    if (held) {
        clicks = 0;
        click_phase = 0;
    } else if (click_phase == 0 && clicks) {
        clicks--;
        click_phase = 1;
        click_at = now;
    } else if (click_phase == 1 && now - click_at >= CLICK_US) {
        click_phase = 2;
        click_at = now;
    } else if (click_phase == 2 && now - click_at >= CLICK_US) {
        click_phase = 0;
    }
    button = held || click_phase == 1;
    /* umac's y goes up. */
    umac_mouse(dx, -dy, button);

    if (panel_pressed(KEY_SOFT_TL)) {
        command_armed = !command_armed;
        video_mark(command_armed);
    }
    if (panel_pressed(KEY_SOFT_BR))
        type(MKC_Return, 0);
    if (panel_pressed(KEY_SOFT_BL))
        type(MKC_BackSpace, 0);
    if (panel_pressed(KEY_SOFT_TR))
        type(MKC_Tab, 0);
    if (panel_pressed(KEY_SETUP))
        type(MKC_Period, 1);
}
