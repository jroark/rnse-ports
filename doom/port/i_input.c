/* Doom on the RNS-E 192: the faceplate as Doom's keyboard and mouse.
 * Replaces doomgeneric's i_input.c.  docs/doom.md, "Controls":
 *
 *   select knob        turn (as a mouse would)    in menus: up, down
 *   SELECT (pressing)  fire                       Enter
 *   NEXT, PREV held    forward, back              right, left
 *   soft keys          top: use, the map; bottom: strafe left, right
 *   NAV TEL NAME MEDIA RADIO   weapons 1 to 5
 *   INFO               the next weapon            CAR   pause
 *   SETUP, RETURN      the menu (Escape)          back
 *   volume knob        the amplifier's volume
 *
 * EJECT, CD EJECT and RETURN held for two seconds are the SDK's
 * (panel_service, from port_idle) and mean what they mean everywhere:
 * the display, the disc, the way back to the loader.  Running is
 * always on: there are not keys enough to hold one for it. */
#include <stdio.h>
#include "rnse.h"
#include "port.h"

#include "doomkeys.h"
#include "doomstat.h"
#include "d_event.h"
#include "m_controls.h"

void D_PostEvent(event_t *ev);
extern int joybspeed;

/* A knob's detent of turning: 10 degrees (mouse x is 1/8 of a BAM
 * step of 2^16 to the circle, at the default sensitivity). */
#define DETENT_TURN 228
/* At most this much a tic, so that a quick turn is a turn and not a
 * jump. */
#define TURN_PER_TIC (3 * DETENT_TURN)

int port_volume = 12;

struct binding {
    int key;            /* the SDK's KEY_* */
    int *game, *menu;   /* Doom's key in play and in a menu (0: none) */
    int game_fixed, menu_fixed;
};

static int key_next_weapon_code = ']';

static const struct binding bindings[] = {
    { KEY_SELECT, &key_fire, 0, 0, KEY_ENTER },
    { KEY_NEXT, &key_up, 0, 0, KEY_RIGHTARROW },
    { KEY_PREV, &key_down, 0, 0, KEY_LEFTARROW },
    { KEY_SOFT_TL, &key_use, 0, 0, 0 },
    { KEY_SOFT_TR, &key_map_toggle, 0, 0, 0 },
    { KEY_SOFT_BL, &key_strafeleft, 0, 0, 0 },
    { KEY_SOFT_BR, &key_straferight, 0, 0, 0 },
    { KEY_NAV, 0, 0, '1', 0 },
    { KEY_TEL, 0, 0, '2', 0 },
    { KEY_NAME, 0, 0, '3', 0 },
    { KEY_MEDIA, 0, 0, '4', 0 },
    { KEY_RADIO, 0, 0, '5', 0 },
    { KEY_INFO, &key_next_weapon_code, 0, 0, 0 },
    { KEY_CAR, &key_pause, 0, 0, 0 },
    { KEY_SETUP, 0, 0, KEY_ESCAPE, KEY_ESCAPE },
};
#define BINDINGS (int)(sizeof bindings / sizeof bindings[0])

/* What each key sent when it went down, so that it is let go as the
 * same key even when a menu opened or closed meanwhile. */
static int sent[BINDINGS];
static char release_next[BINDINGS];
static int turn_left; /* mouse units still to turn */

static void post(evtype_t type, int key)
{
    event_t ev = { 0 };
#ifdef RNSE_DEBUG_INPUT
    printf("input: %s %d\n", type == ev_keydown ? "down" : "up", key);
#endif
    ev.type = type;
    ev.data1 = key;
    ev.data2 = type == ev_keydown && key > 0 && key < 128 ? key : 0;
    D_PostEvent(&ev);
}

static void tap(int key)
{
    post(ev_keydown, key);
    post(ev_keyup, key);
}

void I_InitInput(void)
{
    /* Running always; the next weapon on a key of ours; a prompt's
     * "yes" on SELECT, as Enter is everything else in a menu. */
    joybspeed = 29;
    key_nextweapon = key_next_weapon_code;
    key_menu_confirm = KEY_ENTER;
}

void I_GetEvent(void)
{
    int i, turn, volume;

    port_idle(); /* panel_service: the faceplate polled */
#ifdef RNSE_DEBUG_INPUT
    {
        static u32 was;
        u32 now = 0;
        for (i = 0; i < 48; i++)
            if (panel_key(i))
                now = now * 64 + (u32)i + 1;
        if (now != was)
            printf("input: keys %08x\n", now);
        was = now;
    }
#endif

    for (i = 0; i < BINDINGS; i++) {
        const struct binding *b = &bindings[i];
        int down = panel_key(b->key), tapped = panel_pressed(b->key);
        if (sent[i] && (!down || release_next[i])) {
            post(ev_keyup, sent[i]);
            sent[i] = 0;
            release_next[i] = 0;
        }
        if (!sent[i] && (down || tapped)) {
            int k = menuactive ? (b->menu ? *b->menu : b->menu_fixed)
                               : (b->game ? *b->game : b->game_fixed);
            if (k > 0) {
                post(ev_keydown, k);
                sent[i] = k;
                /* Pressed and let go while the game was busy (a level
                 * loading): down for one tic. */
                release_next[i] = !down;
            }
        }
    }
    if (panel_return_short())
        tap(KEY_ESCAPE);

    turn = panel_knob(KNOB_SELECT);
    if (menuactive) {
        turn_left = 0;
        for (; turn > 0; turn--)
            tap(KEY_DOWNARROW);
        for (; turn < 0; turn++)
            tap(KEY_UPARROW);
    } else {
        turn_left += turn * DETENT_TURN;
        if (turn_left) {
            event_t ev = { 0 };
            int now = turn_left > TURN_PER_TIC ? TURN_PER_TIC
                    : turn_left < -TURN_PER_TIC ? -TURN_PER_TIC : turn_left;
            turn_left -= now;
            ev.type = ev_mouse;
            ev.data2 = now;
            D_PostEvent(&ev);
        }
    }

    /* A level begun by the player (not a demo), for the log: what the
     * end-to-end test waits for. */
    {
        static int last;
        int now = gamestate == GS_LEVEL && !demoplayback ? gameepisode * 10 + gamemap : 0;
        if (now && now != last)
            printf("doom: playing E%dM%d\n", gameepisode, gamemap);
        last = now;
    }

    volume = panel_knob(KNOB_VOLUME);
    if (volume) {
        port_volume += volume;
        if (port_volume < 0)
            port_volume = 0;
        if (port_volume > 30)
            port_volume = 30;
        radio_volume(port_volume);
    }
}
