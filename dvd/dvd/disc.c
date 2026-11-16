/* What is on a DVD-Video disc: VIDEO_TS.IFO found through the file
 * system (ISO 9660, or UDF, which every DVD-Video disc has), its table
 * of titles, and for a title the cells of its program chains, as
 * sectors of the disc, with the chapters among them.
 *
 * Not here: menus and the navigation commands that run them, angles
 * other than the first, parental levels.  A title plays its program
 * chains in the order of its chapters. */
#include "disc.h"

int (*disc_read)(u32 sector, u32 count, void *buf);
struct disc_title disc_titles[DISC_MAX_TITLES];
int disc_ntitles;

static u8 *sec; /* 2048 bytes; and the buffers below, 4096 each */
static u8 *dir, *pgc, *ptt;

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u32 be16(const u8 *p)
{
    return (u32)p[0] << 8 | p[1];
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 le16(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8;
}

static int same(const u8 *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        u8 c = a[i];
        if (c >= 'a' && c <= 'z')
            c = (u8)(c - 'a' + 'A');
        if (c != (u8)b[i])
            return 0;
    }
    return !b[n];
}

/* ---- ISO 9660 ---- */

/* An entry of the directory at (sector, size) by name; 0 when found. */
static int iso_find(u32 dir, u32 size, const char *name, u32 *at, u32 *len)
{
    int n = 0;
    while (name[n])
        n++;
    for (; size; dir++, size -= size > 2048 ? 2048 : size) {
        u32 o = 0;
        if (disc_read(dir, 1, sec))
            return -1;
        while (o < 2048 && sec[o]) {
            const u8 *r = sec + o;
            int l = r[32];
            if ((l == n || (l > n && r[33 + n] == ';')) && same(r + 33, name, n)) {
                *at = le32(r + 2);
                *len = le32(r + 10);
                return 0;
            }
            o += r[0];
        }
    }
    return -2;
}

static int iso_ifo(u32 *at)
{
    u32 dir, size, len;
    if (disc_read(16, 1, sec) || sec[0] != 1 || sec[1] != 'C' || sec[2] != 'D' || sec[5] != '1')
        return -1;
    dir = le32(sec + 156 + 2);
    size = le32(sec + 156 + 10);
    if (iso_find(dir, size, "VIDEO_TS", &dir, &size))
        return -2;
    return iso_find(dir, size, "VIDEO_TS.IFO", at, &len);
}

/* ---- UDF (1.02, as DVD-Video has it) ---- */

static u32 part_start;

/* The first extent of the file whose entry is at logical block lbn. */
static int udf_extent(u32 lbn, u32 *at, u32 *len)
{
    u32 ea, o;
    int kind;
    if (disc_read(part_start + lbn, 1, sec))
        return -1;
    if (le16(sec) == 261) { /* file entry */
        ea = le32(sec + 168);
        o = 176 + ea;
    } else if (le16(sec) == 266) { /* extended file entry */
        ea = le32(sec + 208);
        o = 216 + ea;
    } else {
        return -2;
    }
    kind = sec[34] & 7;
    if (o + 16 > 2048)
        return -3;
    if (kind == 0) { /* short */
        *len = le32(sec + o) & 0x3fffffff;
        *at = part_start + le32(sec + o + 4);
    } else if (kind == 1) { /* long */
        *len = le32(sec + o) & 0x3fffffff;
        *at = part_start + le32(sec + o + 4);
    } else {
        return -4;
    }
    return 0;
}

/* In the directory whose entry is at lbn: the entry of `name`. */
static int udf_lookup(u32 lbn, const char *name, u32 *found)
{
    u32 at, len, o = 0, s;
    int n = 0;
    while (name[n])
        n++;
    if (udf_extent(lbn, &at, &len))
        return -1;
    /* Directories of a DVD fit in two sectors many times over; the
     * identifiers are read one sector at a time with the next one
     * behind it, so that none is cut. */
    for (s = 0; s * 2048 < len; s++) {
        if (disc_read(at + s, 1, dir) || ((s + 1) * 2048 < len && disc_read(at + s + 1, 1, dir + 2048)))
            return -1;
        while (o < 2048 && o + 38 < len - s * 2048) {
            const u8 *f = dir + o;
            u32 liu, lfi, size;
            if (le16(f) != 257)
                return -2;
            lfi = f[19];
            liu = le16(f + 36);
            size = (38 + liu + lfi + 3) & ~3u;
            if (lfi > 1 && (int)lfi - 1 == n && same(f + 38 + liu + 1, name, n)) {
                *found = le32(f + 24);
                return 0;
            }
            o += size;
        }
        o -= 2048;
    }
    return -2;
}

static int udf_ifo(u32 *ifo)
{
    u32 vds, n, i, fsd = 0, root, d, f, len;
    if (disc_read(256, 1, sec) || le16(sec) != 2)
        return -1;
    vds = le32(sec + 20);
    n = le32(sec + 16) / 2048;
    part_start = 0;
    for (i = 0; i < n && i < 64; i++) {
        u32 tag;
        if (disc_read(vds + i, 1, sec))
            return -1;
        tag = le16(sec);
        if (tag == 5)
            part_start = le32(sec + 188);
        else if (tag == 6)
            fsd = le32(sec + 252);
        else if (tag == 8)
            break;
    }
    if (!part_start || disc_read(part_start + fsd, 1, sec) || le16(sec) != 256)
        return -2;
    root = le32(sec + 404);
    if (udf_lookup(root, "VIDEO_TS", &d) || udf_lookup(d, "VIDEO_TS.IFO", &f))
        return -3;
    return udf_extent(f, ifo, &len);
}

/* ---- IFO ---- */

static u32 bcd(u8 b)
{
    return (u32)(b >> 4) * 10 + (b & 15);
}

static u32 seconds_of(const u8 *t)
{
    return bcd(t[0]) * 3600 + bcd(t[1]) * 60 + bcd(t[2]);
}

int disc_open(void)
{
    u32 ifo, srpt, n, i;
    if (!sec) {
        sec = dvd_alloc(2048);
        dir = dvd_alloc(4096);
        pgc = dvd_alloc(4096);
        ptt = dvd_alloc(4096);
    }
    disc_ntitles = 0;
    if (iso_ifo(&ifo) && udf_ifo(&ifo))
        return DISC_NO_VIDEO;
    if (disc_read(ifo, 1, sec))
        return DISC_UNREADABLE;
    if (!same(sec, "DVDVIDEO-VMG", 12))
        return DISC_NO_VIDEO;
    srpt = ifo + be32(sec + 0xc4);
    if (disc_read(srpt, 1, sec))
        return DISC_UNREADABLE;
    n = be16(sec);
    for (i = 0; i < n && i < DISC_MAX_TITLES && 8 + 12 * i + 12 <= 2048; i++) {
        const u8 *e = sec + 8 + 12 * i;
        struct disc_title *t = &disc_titles[disc_ntitles++];
        t->chapters = (u16)be16(e + 2);
        t->vts = e[6];
        t->ttn = e[7];
        t->ifo = ifo + be32(e + 8); /* counted from VIDEO_TS.IFO */
        t->seconds = 0;
    }
    return disc_ntitles;
}

/* The program chain `pgcn` (1..) of the title set: its cells added to
 * the list, its programs' first cells into *first (by program number,
 * indices into the list); and its length. */
static int add_pgc(struct disc_playlist *pl, u32 ifo, u32 vobs, u32 pgcit, u32 pgcn,
                   u16 *first, int *nprog, u32 *secs)
{
    u32 within = 0, *secs_in = &within;
    u32 off, cells, progs, map, info, c, base = (u32)pl->ncells;
    u8 cellmap[256];
    (void)ifo;
    if (disc_read(pgcit, 1, sec))
        return -1;
    if (pgcn < 1 || pgcn > be16(sec))
        return -1;
    off = be32(sec + 8 + 8 * (pgcn - 1) + 4);
    /* A program chain is at most a few kilobytes: two sectors. */
    if (disc_read(pgcit + off / 2048, 2, pgc))
        return -1;
    {
        const u8 *p = pgc + off % 2048;
        progs = p[2];
        cells = p[3];
        *secs = seconds_of(p + 4);
        for (c = 0; c < 8; c++)
            pl->audio_control[c] = (u16)be16(p + 0x0c + 2 * c);
        map = be16(p + 0xe6);
        info = be16(p + 0xe8);
        if (off % 2048 + info + 24 * cells > 4096)
            return -1;
        for (c = 0; c < cells; c++) {
            const u8 *ci = p + info + 24 * c;
            int type = (ci[0] >> 4) & 3, mode = ci[0] >> 6;
            /* An angle block: its first cell only. */
            if (type == 1 && mode != 1) {
                cellmap[c] = 0xff;
                continue;
            }
            if (pl->ncells >= DISC_MAX_CELLS) {
                cellmap[c] = 0xff;
                continue;
            }
            cellmap[c] = (u8)(pl->ncells - base);
            pl->cells[pl->ncells].at = pl->seconds + *secs_in;
            *secs_in += seconds_of(ci + 4);
            pl->cells[pl->ncells].first = vobs + be32(ci + 8);
            pl->cells[pl->ncells].last = vobs + be32(ci + 20);
            pl->ncells++;
        }
        *nprog = (int)progs;
        for (c = 0; c < progs && c < 255; c++) {
            u32 entry = p[map + c];
            u32 k = entry >= 1 && entry <= cells ? entry - 1 : 0;
            while (k < cells && cellmap[k] == 0xff)
                k++;
            first[c] = (u16)(base + (k < cells ? cellmap[k] : 0));
        }
    }
    return 0;
}

int disc_title(int index, struct disc_playlist *pl)
{
    struct disc_title *t = &disc_titles[index];
    u32 vobs, pttsrpt, pgcit, ntt, toff, tend, i, k, done[8];
    int ndone = 0;
    pl->ncells = 0;
    pl->nchapters = 0;
    pl->seconds = 0;
    pl->naudio = 0;
    if (disc_read(t->ifo, 1, sec) || !same(sec, "DVDVIDEO-VTS", 12))
        return DISC_UNREADABLE;
    vobs = t->ifo + be32(sec + 0xc4);
    pttsrpt = t->ifo + be32(sec + 0xc8);
    pgcit = t->ifo + be32(sec + 0xcc);
    pl->video = (u16)be16(sec + 0x200);
    pl->naudio = (int)be16(sec + 0x202);
    if (pl->naudio > 8)
        pl->naudio = 8;
    for (i = 0; i < (u32)pl->naudio; i++) {
        const u8 *a = sec + 0x204 + 8 * i;
        pl->audio[i].coding = a[0] >> 5;
        pl->audio[i].channels = (a[1] & 7) + 1;
        pl->audio[i].lang[0] = (char)a[2];
        pl->audio[i].lang[1] = (char)a[3];
        pl->audio[i].lang[2] = 0;
    }
    if (disc_read(pttsrpt, 2, ptt))
        return DISC_UNREADABLE;
    ntt = be16(ptt);
    if (t->ttn < 1 || t->ttn > ntt || 8 + 4 * t->ttn > 2048)
        return DISC_UNREADABLE;
    toff = be32(ptt + 8 + 4 * (t->ttn - 1));
    tend = t->ttn < ntt ? be32(ptt + 8 + 4 * t->ttn) : be32(ptt + 4) + 1;
    if (toff >= 4096)
        return DISC_UNREADABLE;
    if (tend > 4096)
        tend = 4096;
    /* Chapter by chapter: the program chain and program it starts. */
    for (k = toff; k + 4 <= tend && pl->nchapters < DISC_MAX_CHAPTERS; k += 4) {
        u32 pgcn = be16(ptt + k), pgn = be16(ptt + k + 2), d;
        static u16 first[256];
        static int nprog;
        static u32 last_pgcn;
        static int last_base;
        for (d = 0; d < (u32)ndone && done[d] != pgcn; d++)
            ;
        if (d == (u32)ndone) {
            u32 secs = 0;
            if (ndone == 8)
                break;
            done[ndone++] = pgcn;
            last_base = pl->ncells;
            if (add_pgc(pl, t->ifo, vobs, pgcit, pgcn, first, &nprog, &secs))
                return DISC_UNREADABLE;
            pl->seconds += secs;
            last_pgcn = pgcn;
        }
        (void)last_base;
        if (pgcn == last_pgcn && pgn >= 1 && (int)pgn <= nprog)
            pl->chapter[pl->nchapters++] = first[pgn - 1];
    }
    if (!pl->nchapters && pl->ncells)
        pl->chapter[pl->nchapters++] = 0;
    t->seconds = pl->seconds;
    return pl->ncells ? 0 : DISC_UNREADABLE;
}
