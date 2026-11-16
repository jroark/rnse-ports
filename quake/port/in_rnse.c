/* Quake on the RNS-E 192: the faceplate as Quake's keyboard and mouse
 * (in_null.c's place, and Sys_SendKeyEvents).  docs/quake.md,
 * "Controls".  The keys are those default.cfg (in PAK0.PAK) binds:
 *
 *   select knob        turn                      in menus: up, down
 *   SELECT (pressing)  fire (CTRL)               Enter
 *   NEXT, PREV held    forward, back (arrows)    right, left (sliders)
 *   soft keys          top: jump, next weapon; bottom: strafe left, right
 *                      in menus: top left "y", top right "n"
 *   NAV TEL NAME MEDIA RADIO   weapons 1 to 5
 *   INFO               the scores (TAB)          CAR   pause
 *   SETUP, RETURN      the menu (Escape)         back
 *   volume knob        the amplifier's volume
 *
 * EJECT, CD EJECT and RETURN held for two seconds are the SDK's
 * (panel_service, from port_idle) and mean what they mean everywhere:
 * the display, the disc, the way back to the loader. */
#include "quakedef.h"
#include "rnse.h"
#include "port.h"

/* A detent of the knob: this many degrees of turning, at most three
 * detents' worth a frame, so that a quick turn is a turn. */
#define DETENT 10.0f
#define MOST 3

int port_volume = 12;

struct binding {
    int key;        /* the SDK's KEY_* */
    int game, menu; /* Quake's key in play and elsewhere (0: none) */
};

static const struct binding bindings[] = {
    { KEY_SELECT, K_CTRL, K_ENTER },
    { KEY_NEXT, K_UPARROW, K_RIGHTARROW },
    { KEY_PREV, K_DOWNARROW, K_LEFTARROW },
    { KEY_SOFT_TL, K_SPACE, 'y' },
    { KEY_SOFT_TR, '/', 'n' },
    { KEY_SOFT_BL, ',', 0 },
    { KEY_SOFT_BR, '.', 0 },
    { KEY_NAV, '1', 0 },
    { KEY_TEL, '2', 0 },
    { KEY_NAME, '3', 0 },
    { KEY_MEDIA, '4', 0 },
    { KEY_RADIO, '5', 0 },
    { KEY_INFO, K_TAB, 0 },
    { KEY_CAR, K_PAUSE, 0 },
    { KEY_SETUP, K_ESCAPE, K_ESCAPE },
};
#define BINDINGS (int)(sizeof bindings / sizeof bindings[0])

/* What each key sent when it went down, so that it is let go as the
 * same key even when a menu opened or closed meanwhile. */
static int sent[BINDINGS];
static char release_next[BINDINGS];
static int turn_left; /* detents still to turn */

static void tap(int key)
{
    Key_Event(key, true);
    Key_Event(key, false);
}

void IN_Init(void)
{
}

void IN_Shutdown(void)
{
}

void IN_Commands(void)
{
}

void IN_Move(usercmd_t *cmd)
{
    int now;
    if (key_dest != key_game) {
        turn_left = 0;
        return;
    }
    if (!turn_left)
        return;
    now = turn_left > MOST ? MOST : turn_left < -MOST ? -MOST : turn_left;
    turn_left -= now;
    /* The knob turned right is the player turned right: yaw down. */
    cl.viewangles[YAW] = anglemod(cl.viewangles[YAW] - now * DETENT);
}

void Sys_SendKeyEvents(void)
{
    int i, turn, volume, in_game;

    port_idle(); /* panel_service: the faceplate polled */
    in_game = key_dest == key_game;
    for (i = 0; i < BINDINGS; i++) {
        const struct binding *b = &bindings[i];
        int down = panel_key(b->key), tapped = panel_pressed(b->key);
        if (sent[i] && (!down || release_next[i])) {
            Key_Event(sent[i], false);
            sent[i] = 0;
            release_next[i] = 0;
        }
        if (!sent[i] && (down || tapped)) {
            int k = in_game ? b->game : b->menu;
            if (k > 0) {
                Key_Event(k, true);
                sent[i] = k;
                /* Pressed and let go while the game was busy (a level
                 * loading): down for one frame. */
                release_next[i] = !down;
            }
        }
    }
    if (panel_return_short())
        tap(K_ESCAPE);

    turn = panel_knob(KNOB_SELECT);
    if (!in_game) {
        for (; turn > 0; turn--)
            tap(K_DOWNARROW);
        for (; turn < 0; turn++)
            tap(K_UPARROW);
    } else {
        turn_left += turn;
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

    /* A map begun by the player (not a demo), for the log: what the
     * end-to-end test waits for. */
    {
        static char last[64];
        if (sv.active && !cls.demoplayback && strcmp(last, sv.name)) {
            printf("quake: playing %s\n", sv.name);
            strncpy(last, sv.name, sizeof last - 1);
        }
    }
}
