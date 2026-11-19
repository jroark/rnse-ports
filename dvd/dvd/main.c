/* A DVD player for the 192, bare metal: started from an SD card by the
 * SD loader (sdk/examples/sdboot) as one of its programs, it plays
 * DVD-Video discs from the unit's drive.  See docs/dvd.md.
 *
 *   CD EJECT                the disc out (the display key opens the
 *                           screen in front of the drive first)
 *   select knob, NEXT, PREV a title in the list; while playing, the
 *                           chapter after or before
 *   SELECT                  play the title; while playing, pause
 *   volume knob             louder, quieter
 *   SETUP                   the next sound track
 *   INFO                    what is played, and how fast, over the picture
 *   CAR                     16 or 8 bits a pixel for the picture
 *   RETURN                  back to the list; held, back to the SD loader
 *
 * Unencrypted discs only: discs scrambled with CSS are recognised and
 * refused, unless a descrambler has been put in scramble.c's place
 * (docs/dvd.md, "Scrambled discs").  No menus and no subtitles. */
#include "player.h"

/* RAM for everything big: below the stock loader's place, above the
 * way back's scratch and the SD loader's reserved block, through P0
 * (as sdk/examples/h264bench uses it). */
#define HEAP 0x0c800000
#define HEAP_END 0x0dcf0000
#define RING_SECTORS 2048 /* 4 MB of disc read ahead */

static struct disc_playlist *pl_mem;
#define playlist (*pl_mem)
static char text[96];
static u32 found_ccr;
static int volume = 15;

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put_dec(char *p, u32 v, int digits)
{
    char t[10];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v || n < digits);
    while (n)
        *p++ = t[--n];
    return p;
}

static char *put_time(char *p, u32 s)
{
    p = put_dec(p, s / 3600, 1);
    *p++ = ':';
    p = put_dec(p, s / 60 % 60, 2);
    *p++ = ':';
    return put_dec(p, s % 60, 2);
}

static void *alloc(unsigned long n)
{
    return malloc(n);
}

void *dvd_alloc(unsigned long n)
{
    void *p = malloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

/* ---- keys ---- */

static int was[48];

static int pressed(int key)
{
    int down = panel_key(key), p = down && !was[key];
    was[key] = down;
    return p;
}

/* The keys and the drive's key; 1 when the disc was sent out. */
static int keys(void)
{
    int eject;
    panel_poll();
    eject = panel_key(KEY_CD_EJECT) && !was[KEY_CD_EJECT];
    if (eject)
        drive_stop();
    panel_service();
    was[KEY_CD_EJECT] = panel_key(KEY_CD_EJECT);
    return eject;
}

static void leave(void)
{
    drive_stop();
    sound_stop();
    radio_source(RADIO_TUNER, volume);
    screen_mode(SCREEN_MENU);
    cache_setup(found_ccr);
}

/* ---- the menu screen ---- */

static void line(int row, const char *s, u8 colour, u8 back)
{
    int y = 6 + 12 * row;
    screen_fill(0, y - 2, screen_width, 11, back);
    screen_text(6, y, s, colour, 1);
}

static void message(const char *a, const char *b, const char *c)
{
    screen_mode(SCREEN_MENU);
    screen_fill(0, 0, screen_width, screen_height, C_BLACK);
    screen_text(6, 6, "DVD PLAYER", C_YELLOW, 2);
    if (a)
        screen_text(6, 40, a, C_WHITE, 1);
    if (b)
        screen_text(6, 56, b, C_WHITE, 1);
    if (c)
        screen_text(6, 72, c, C_GREY, 1);
    screen_text(6, 214, "CD EJECT: DISC OUT.  HOLD RETURN: SD LOADER", C_GREY, 1);
    console_puts(a ? a : "");
    console_puts("\n");
}

/* ---- the drive, between discs ---- */

/* Asks the drive every 300 ms whether a disc can be read, telling it
 * to start one it has loaded (the 192's does not by itself; sdk/src/
 * cd.c, cd_wait_ready).  1 when one can be read. */
static int disc_ready(void)
{
    static const u8 test_unit_ready[12] = { 0x00 };
    static const u8 request_sense[12] = { 0x03, 0, 0, 0, 18 };
    static const u8 start_unit[12] = { 0x1b, 0, 0, 0, 1 };
    static u32 asked, started;
    u8 sense[18];
    int r;
    if (millis() - asked < 300)
        return 0;
    asked = millis();
    r = cd_command(test_unit_ready, 0, 0);
    if (r == 0)
        return 1;
    if (cd_command(request_sense, sense, sizeof sense) == 0 && (sense[2] & 15) == 2 && sense[12] == 0x04 &&
        millis() - started > 500) {
        cd_command(start_unit, 0, 0);
        started = millis();
    }
    return 0;
}

static int read_sectors(u32 sector, u32 count, void *buf)
{
    int tries;
    for (tries = 0; tries < 3; tries++) {
        if (cd_read(sector, count, buf) == 0)
            return 0;
        panel_service();
    }
    return -1;
}

/* ---- titles ---- */

static const char *coding_name(int c)
{
    switch (c) {
    case 0: return "AC3";
    case 2: case 3: return "MPEG";
    case 4: return "LPCM";
    case 6: return "DTS";
    default: return "?";
    }
}

static void title_row(int row, int t, int chosen)
{
    char *p = text;
    struct disc_title *d = &disc_titles[t];
    p = put_str(p, "TITLE ");
    p = put_dec(p, (u32)t + 1, 2);
    p = put_str(p, "  ");
    p = put_time(p, d->seconds);
    p = put_str(p, "  ");
    p = put_dec(p, d->chapters, 1);
    p = put_str(p, d->chapters == 1 ? " CHAPTER" : " CHAPTERS");
    *p = 0;
    line(row, text, chosen ? C_BLACK : C_WHITE, chosen ? C_YELLOW : C_BLACK);
}

static void title_list(int chosen)
{
    int first = chosen > 12 ? chosen - 12 : 0, t;
    screen_mode(SCREEN_MENU);
    screen_fill(0, 0, screen_width, screen_height, C_BLACK);
    line(0, "DVD PLAYER: CHOOSE A TITLE", C_YELLOW, C_BLACK);
    for (t = first; t < disc_ntitles && t < first + 14; t++)
        title_row(2 + t - first, t, t == chosen);
    line(17, "KNOB: CHOOSE.  SELECT: PLAY.  CD EJECT: DISC OUT", C_GREY, C_BLACK);
    line(18, "HOLD RETURN: BACK TO THE SD LOADER", C_GREY, C_BLACK);
}

/* ---- playing ---- */

static struct {
    int title, chapter, cell;
    int paused, osd, audio_index;
    struct frame *pending;
    u32 due, last_due, last_dur;
    int have_due;
    int skip_b, to_i;
    u32 shown, shown_at, fps10, counted;
    u32 audio_frames, audio_rate;
    u32 sectors_at, kbs;
    int local;           /* the clock runs from the timer, from: */
    u32 local_pts, local_us;
    int scrambled, audio_bad;
    int descramble;          /* scramble_open said yes for this title */
    /* Where in the video stream each cell begins (the reading runs
     * seconds ahead of the picture): the chapter shown follows the
     * pictures. */
    struct { u32 pos; int cell; } cells[16];
    int ncells, demux_cell;
    u32 cell_pts, shown_pts;   /* the first picture's of the cell, the one shown */
    int cell_pts_set;
} play;

static int audio_id_of(int i)
{
    switch (playlist.audio[i].coding) {
    case 0: return 0x80 + i;
    case 2: case 3: return 0xc0 + i;
    case 4: return 0xa0 + i;
    case 6: return 0x88 + i;
    default: return -1;
    }
}

static void begin(int chapter)
{
    if (chapter < 0)
        chapter = 0;
    if (chapter >= playlist.nchapters)
        chapter = playlist.nchapters - 1;
    play.chapter = chapter;
    play.cell = playlist.chapter[chapter];
    drive_seek(&playlist, play.cell);
    stream_reset();
    stream_audio_id = playlist.naudio ? audio_id_of(play.audio_index) : -1;
    video_reset();
    sound_reset();
    play.pending = 0;
    play.ncells = 0;
    play.demux_cell = play.cell;
    play.cell_pts_set = 0;
    play.have_due = 0;
    play.skip_b = 0;
    play.to_i = 0;
    play.local = 0;
    play.paused = 0;
}

static u32 timer_clock(void)
{
    u32 now = micros(), d = now - play.local_us;
    /* Kept short, so that the arithmetic does not overflow. */
    if (d > 10000000) {
        play.local_pts += d / 100 * 9;
        play.local_us = now;
        d = 0;
    }
    return play.local_pts + d * 9 / 100;
}

/* The time now: the sound's.  A picture far from it, more than 2 s
 * ahead or 20 s behind, is a jump in the time stamps (they start
 * again in many a title's cells), and the timer keeps the time from
 * that picture on until the sound has caught up with the jump; a
 * picture less far behind is only late, and the player catches up
 * (step).  Without sound, the timer from the first picture. */
static int clock_now(u32 frame_pts, u32 *now)
{
    u32 a;
    if (sound_clock(&a)) {
        s32 d = (s32)(frame_pts - a);
        if (d < 2 * 90000 && d > -20 * 90000 && (!play.local || (d < 90000 && d > -90000))) {
            play.local = 0;
            *now = a;
            return 1;
        }
    }
    if (!play.local) {
        /* Waiting for the sound to start: not for long. */
        if (sound_ok && stream_audio_kind != AUDIO_OTHER && play.audio_frames == 0 && drive_level() < 1500 && !drive_finished())
            return 0;
        play.local = 1;
        play.local_pts = frame_pts;
        play.local_us = micros();
    }
    *now = timer_clock();
    return 1;
}

static void osd_lines(const char **a, const char **b)
{
    static char l1[80], l2[80];
    char *p = l1;
    u32 secs = playlist.cells[play.cell].at;
    if (play.cell_pts_set && (s32)(play.shown_pts - play.cell_pts) > 0)
        secs += (play.shown_pts - play.cell_pts) / 90000;
    p = put_str(p, "TITLE ");
    p = put_dec(p, (u32)play.title + 1, 1);
    p = put_str(p, "  CHAPTER ");
    p = put_dec(p, (u32)play.chapter + 1, 1);
    *p++ = '/';
    p = put_dec(p, (u32)playlist.nchapters, 1);
    p = put_str(p, "  ");
    p = put_time(p, secs);
    p = put_str(p, " / ");
    p = put_time(p, playlist.seconds);
    if (play.paused)
        p = put_str(p, "  PAUSED");
    *p = 0;
    p = l2;
    if (playlist.naudio) {
        p = put_str(p, coding_name(playlist.audio[play.audio_index].coding));
        *p++ = ' ';
        if (playlist.audio[play.audio_index].lang[0] >= 'a') {
            *p++ = playlist.audio[play.audio_index].lang[0];
            *p++ = playlist.audio[play.audio_index].lang[1];
            *p++ = ' ';
        }
    } else {
        p = put_str(p, "NO SOUND ");
    }
    p = put_dec(p, play.fps10 / 10, 1);
    *p++ = '.';
    p = put_dec(p, play.fps10 % 10, 1);
    p = put_str(p, " FPS  B LEFT OUT ");
    p = put_dec(p, video_skipped, 1);
    p = put_str(p, "  DISC ");
    p = put_dec(p, play.kbs, 1);
    p = put_str(p, " KB/S  ");
    p = put_dec(p, drive_level() * 100 / RING_SECTORS, 1);
    *p++ = '%';
    *p = 0;
    *a = l1;
    *b = l2;
}

static s16 *pcm; /* 2 x 1536 */

/* Sound out of the stream, up to half a second ahead. */
static void decode_audio(void)
{
    const u8 *p;
    u32 len, pts;
    int has;
    while (sound_ahead() < 22050 && stream_audio_frame(&p, &len, &pts, &has)) {
        int n = 0, rate = 48000, ch = 2;
        sound_guard();
        if (stream_audio_kind == AUDIO_AC3) {
            n = ac3_decode(p, (int)len, pcm, &rate);
        } else if (stream_audio_kind == AUDIO_MPA) {
            n = mpa_decode(p, (int)len, pcm, &rate, &ch);
        } else if (stream_audio_kind == AUDIO_LPCM && stream_lpcm_bits == 16) {
            /* Big-endian samples, interleaved; the first two channels. */
            u32 frame = 2 * (u32)stream_lpcm_channels, i;
            n = (int)(len / frame);
            if (n > 1536)
                n = 1536;
            for (i = 0; i < (u32)n; i++) {
                const u8 *s = p + i * frame;
                pcm[2 * i] = (s16)(s[0] << 8 | s[1]);
                pcm[2 * i + 1] = stream_lpcm_channels > 1 ? (s16)(s[2] << 8 | s[3]) : pcm[2 * i];
            }
            rate = stream_lpcm_rate;
        } else {
            play.audio_bad = 1;
        }
        stream_audio_consume(len);
        if (n > 0) {
            sound_write(pcm, (u32)n, rate, pts, has);
            play.audio_frames += (u32)n;
            play.audio_rate = (u32)rate;
        }
    }
}

/* The picture type of a unit (3 bits after the temporal reference). */
static int unit_type(const u8 *p, u32 len)
{
    u32 i;
    for (i = 0; i + 6 <= len; i++)
        if (!p[i] && !p[i + 1] && p[i + 2] == 1 && p[i + 3] == 0)
            return (p[i + 5] >> 3) & 7;
    return 0;
}

static void show(void)
{
    const char *a = 0, *b = 0;
    if (play.osd)
        osd_lines(&a, &b);
    sound_guard();
    screen_video(play.pending, a, b);
    play.shown++;
    if (millis() - play.shown_at >= 2000) {
        u32 ms = millis() - play.shown_at;
        play.fps10 = (play.shown - play.counted) * 10000 / ms;
        play.counted = play.shown;
        play.shown_at = millis();
        play.kbs = (drive_sectors - play.sectors_at) * 2 * 1000 / ms;
        play.sectors_at = drive_sectors;
        console_puts("fps*10 ");
        console_dec(play.fps10);
        console_puts(" pictures ");
        console_dec(video_pictures);
        console_puts(" B left out ");
        console_dec(video_skipped);
        console_puts(" KB/s ");
        console_dec(play.kbs);
        console_puts("\n");
    }
}

/* One round of the player: 0 while it plays, 1 at the end. */
static int step(void)
{
    const u8 *p;
    u32 len, pts;
    int has, c;

    sound_guard();
    drive_service(32);
    /* Packs to the elementary streams while they want more. */
    while (es_level(&stream_video) < 512 * 1024 && es_level(&stream_audio) < 96 * 1024 && (p = drive_sector(&c))) {
        if (stream_pack_scrambled(p) &&
            (!play.descramble || scramble_sector((u8 *)p) || stream_pack_scrambled(p))) {
            play.scrambled = 1;
            return 1;
        }
        stream_pack(p);
        if (c != play.demux_cell && play.ncells < 16) {
            play.cells[play.ncells].pos = stream_video.end;
            play.cells[play.ncells].cell = c;
            play.ncells++;
            play.demux_cell = c;
        }
        drive_consume();
    }
    if (play.paused)
        return 0;
    decode_audio();
    if (!play.pending) {
        int end = drive_finished() && !drive_level();
        if (stream_video_unit(&p, &len, &pts, &has, end)) {
            int type = unit_type(p, len), k = 0;
            while (k < play.ncells && (s32)(play.cells[k].pos - stream_video.start) <= 0) {
                play.cell = play.cells[k].cell;
                k++;
            }
            if (k) {
                int j;
                for (j = k; j < play.ncells; j++)
                    play.cells[j - k] = play.cells[j];
                play.ncells -= k;
                play.cell_pts_set = 0;
                while (play.chapter + 1 < playlist.nchapters && playlist.chapter[play.chapter + 1] <= play.cell)
                    play.chapter++;
            }
            if (play.to_i && type != 1) {
                stream_video_consume(len);
                return 0;
            }
            if (play.to_i) {
                video_reset();
                play.to_i = 0;
            }
            sound_guard();
            play.pending = video_decode(p, len, pts, has, play.skip_b);
            stream_video_consume(len);
            if (play.pending && !play.cell_pts_set && play.pending->has_pts) {
                play.cell_pts = play.pending->pts;
                play.cell_pts_set = 1;
            }
            if (play.pending) {
                if (play.pending->has_pts && play.have_due &&
                    (s32)(play.pending->pts - (play.last_due + play.last_dur)) > -3 * 90000 &&
                    (s32)(play.pending->pts - (play.last_due + play.last_dur)) < 3 * 90000)
                    play.due = play.pending->pts;
                else if (play.pending->has_pts || !play.have_due)
                    play.due = play.pending->has_pts ? play.pending->pts : 0;
                else
                    play.due = play.last_due + play.last_dur;
                play.have_due = 1;
                play.last_due = play.due;
                play.last_dur = play.pending->duration;
            }
        } else if (end) {
            struct frame *f = video_flush();
            if (f) {
                play.pending = f;
                play.due = play.last_due + play.last_dur;
                return 0;
            }
            /* The sound to its end as well. */
            return sound_ahead() <= 2205 + 512 || !sound_ok;
        }
    } else {
        u32 now;
        if (!clock_now(play.due, &now))
            return 0;
        if ((s32)(now - play.due) >= -90 * 2) {
            s32 late = (s32)(now - play.due);
            /* Behind: B pictures are left out until it catches up, and
             * past half a second everything up to the next I picture. */
            play.skip_b = late > (s32)(2 * play.last_dur);
            if (late > 45000)
                play.to_i = 1;
            play.shown_pts = play.due;
            show();
            play.pending = 0;
        }
    }
    return 0;
}

static int play_title(int t)
{
    int r;
    u32 osd_until = 0;
    message("READING THE TITLE", 0, 0);
    r = disc_title(t, &playlist);
    if (r)
        return r;
    play.title = t;
    play.audio_index = 0;
    play.shown = play.counted = 0;
    play.shown_at = millis();
    play.sectors_at = drive_sectors;
    play.audio_frames = 0;
    play.scrambled = 0;
    play.descramble = scramble_open(&playlist, disc_titles[t].ifo) == 0;
    play.audio_bad = 0;
    play.osd = 0;
    for (r = 0; r < playlist.naudio && audio_id_of(r) >= 0 && playlist.audio[r].coding == 6; r++)
        ;
    play.audio_index = r < playlist.naudio ? r : 0;
    screen_mode(SCREEN_VIDEO16);
    begin(0);
    for (;;) {
        int turn;
        if (keys())
            return 1;
        if (panel_return_short())
            break;
        turn = panel_knob(KNOB_SELECT) + pressed(KEY_NEXT) - pressed(KEY_PREV);
        if (turn) {
            /* Past the last chapter: the title is over. */
            if (play.chapter + turn >= playlist.nchapters)
                break;
            begin(play.chapter + turn);
            osd_until = millis() + 3000;
        }
        {
            int v = panel_knob(KNOB_VOLUME);
            if (v) {
                volume += v;
                volume = volume < 0 ? 0 : volume > 30 ? 30 : volume;
                radio_volume(volume);
            }
        }
        if (pressed(KEY_SELECT)) {
            play.paused = !play.paused;
            if (play.paused) {
                sound_reset();
                screen_video(0, "PAUSED", 0);
            } else {
                play.local = 0;
                play.have_due = 0;
            }
        }
        if (pressed(KEY_SETUP) && playlist.naudio > 1) {
            play.audio_index = (play.audio_index + 1) % playlist.naudio;
            stream_audio_id = audio_id_of(play.audio_index);
            stream_audio.start = stream_audio.end;
            stream_audio.nstamps = 0;
            sound_reset();
            osd_until = millis() + 3000;
        }
        if (pressed(KEY_INFO))
            play.osd = !play.osd;
        if (pressed(KEY_CAR))
            screen_mode(screen_current == SCREEN_VIDEO16 ? SCREEN_VIDEO8 : SCREEN_VIDEO16);
        if (osd_until && (s32)(millis() - osd_until) > 0) {
            osd_until = 0;
            if (play.osd == 2)
                play.osd = 0;
        } else if (osd_until && !play.osd) {
            play.osd = 2;
        }
        if (step())
            break;
    }
    drive_stop();
    sound_reset();
    if (play.scrambled) {
        message("THIS DISC IS ENCRYPTED WITH CSS.", play.descramble ? "THE DESCRAMBLER COULD NOT UNSCRAMBLE IT." : "THIS PLAYER HAS NO DESCRAMBLER: DOCS/DVD.MD.", "PRESS RETURN.");
        while (!panel_return_short())
            if (keys())
                return 1;
    }
    return 0;
}

/* ---- the disc ---- */

static void wait_for_disc(void)
{
    message("INSERT A DVD-VIDEO DISC.", "THE DISPLAY KEY OPENS THE SCREEN, CD EJECT TAKES A DISC OUT.", 0);
    for (;;) {
        keys();
        if (disc_ready())
            return;
        cpu_idle();
    }
}

static void wait_for_removal(void)
{
    /* Until the drive has no disc it can read. */
    static const u8 test_unit_ready[12] = { 0x00 };
    u32 asked = millis();
    for (;;) {
        keys();
        if (millis() - asked > 500) {
            asked = millis();
            if (cd_command(test_unit_ready, 0, 0) != 0)
                return;
        }
        cpu_idle();
    }
}

int main(void)
{
    int n, t, chosen = 0;
    board_init();
    console_puts("\nrnse sdk: dvd\n");
    if (screen_width != 400) {
        console_puts("the 192 only\n");
        return 1;
    }
    /* The FPU for the sound decoders, which are built with -m4: GCC's
     * code for it takes FPSCR.PR to be set (double precision) between
     * functions and clears it inside those that work in single
     * precision.  Round to zero, denormals as zero, as the stock
     * software has it.  (lds r0, fpscr as its opcode: the assembler
     * takes no FPU instructions in this file's -m4-nofpu.) */
    {
        register u32 r0 __asm__("r0") = 0x000c0001;
        __asm__ volatile(".short 0x406a" : : "r"(r0));
    }
    found_ccr = REG32(0xff00001c);
    loader_leave = leave;
    if (loader_active())
        panel_attach();
    else
        panel_init();
    sd_idle = panel_service;
    display_idle = panel_service;
    pcm_idle = panel_service;
    heap_init((void *)HEAP, HEAP_END - HEAP);
    screen_init();
    message("STARTING", 0, 0);
    if (REG32(0xff000010) & 1) {
        message("ADDRESSES ARE TRANSLATED (MMUCR.AT): CANNOT RUN", 0, 0);
        for (;;)
            keys();
    }
    /* The caches as the decoders want them (sdbridge/docs/android-auto.md): P1
     * copied back, no index modes; put back on the way out. */
    cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB);
    if (video_init(alloc)) {
        message("NOT ENOUGH MEMORY", 0, 0);
        for (;;)
            keys();
    }
    pl_mem = dvd_alloc(sizeof *pl_mem);
    pcm = dvd_alloc(2 * 1536 * 2);
    ac3_init();
    mpa_init();
    stream_init(malloc(3 << 20), 3 << 20, malloc(512 << 10), 512 << 10);
    drive_init(malloc(RING_SECTORS * 2048), RING_SECTORS);
    disc_read = read_sectors;
    cd_init();
    {
        u8 st[57];
        /* The volume the unit had; a quiet one would be taken for no
         * sound at all. */
        if (radio_status(st, sizeof st) > 34 && st[34] <= 30)
            volume = st[34] < 8 ? 12 : st[34];
    }
    radio_source(RADIO_DECODER, volume);
    n = sound_start();
    if (n) {
        console_puts("no sound: ");
        console_hex((u32)n);
        console_puts("\n");
    }

    for (;;) {
        wait_for_disc();
        message("READING THE DISC", 0, 0);
        n = disc_open();
        if (n <= 0) {
            message(n == DISC_NO_VIDEO ? "THIS IS NOT A DVD-VIDEO DISC." : "THE DISC CANNOT BE READ.",
                    "CD EJECT TAKES IT OUT.", 0);
            wait_for_removal();
            continue;
        }
        for (t = 0; t < n; t++) {
            disc_title(t, &playlist);
            keys();
        }
        chosen = 0;
        title_list(chosen);
        for (;;) {
            int turn;
            if (keys())
                break;
            turn = panel_knob(KNOB_SELECT) + pressed(KEY_NEXT) - pressed(KEY_PREV);
            if (turn) {
                chosen += turn;
                chosen = chosen < 0 ? 0 : chosen >= n ? n - 1 : chosen;
                title_list(chosen);
            }
            if (pressed(KEY_SELECT)) {
                if (play_title(chosen))
                    break;
                title_list(chosen);
            }
            panel_return_short();
            cpu_idle();
        }
        wait_for_removal();
    }
}
