/* MPEG-1 and MPEG-2 video, main profile at main level, 4:2:0: what a
 * DVD carries.  Written from ISO/IEC 13818-2 (and 11172-2 for MPEG-1).
 *
 * Pictures wider than 400 pixels are kept at half their width (see
 * dvd.h): blocks are transformed from the four lower horizontal
 * frequencies only, and motion compensation works on the reduced
 * frames with a quarter of a stored pixel for the half pixel of the
 * coded picture.  Vertically everything is exact, so field pictures,
 * field prediction and field DCT need nothing special.
 *
 * Not here: scalable and 4:2:2 profiles, MPEG-1 D pictures.  Dual
 * prime prediction is read but predicted from the same-parity field
 * alone (no averaging with the opposite-parity prediction), which only
 * a few encoders use and which drifts a little; see docs/dvd.md. */
#include "dvd.h"

#define MB_QUANT 1
#define MB_FWD 2
#define MB_BWD 4
#define MB_PATTERN 8
#define MB_INTRA 16

struct video_info video;
u32 video_pictures, video_skipped, video_errors;
int video_full_width;
int video_last_type;

/* ---- tables, built at video_init from the codes as the standard
 * lists them (value=code) ---- */

/* Each entry: value << 5 | length; length 0: no such code. */
static u16 *t_mba, *t_cbp, *t_mv, *t_mbtype[3];
static u16 *t_dct14, *t_dct15, *t_dctlong14, *t_dctlong15;

static const char spec_mba[] =
    "1=1 011=2 010=3 0011=4 0010=5 00011=6 00010=7 0000111=8 0000110=9 00001011=10 00001010=11 "
    "00001001=12 00001000=13 00000111=14 00000110=15 0000010111=16 0000010110=17 0000010101=18 "
    "0000010100=19 0000010011=20 0000010010=21 00000100011=22 00000100010=23 00000100001=24 "
    "00000100000=25 00000011111=26 00000011110=27 00000011101=28 00000011100=29 00000011011=30 "
    "00000011010=31 00000011001=32 00000011000=33 00000001000=34 00000001111=35";
static const char spec_mb_i[] = "1=16 01=17";
static const char spec_mb_p[] = "1=10 01=8 001=2 00011=16 00010=11 00001=9 000001=17";
static const char spec_mb_b[] = "10=6 11=14 010=4 011=12 0010=2 0011=10 00011=16 00010=15 000011=11 000010=13 000001=17";
static const char spec_cbp[] =
    "111=60 1101=4 1100=8 1011=16 1010=32 10011=12 10010=48 10001=20 10000=40 01111=28 01110=44 "
    "01101=52 01100=56 01011=1 01010=61 01001=2 01000=62 001111=24 001110=36 001101=3 001100=63 "
    "0010111=5 0010110=9 0010101=17 0010100=33 0010011=6 0010010=10 0010001=18 0010000=34 "
    "00011111=7 00011110=11 00011101=19 00011100=35 00011011=13 00011010=49 00011001=21 00011000=41 "
    "00010111=14 00010110=50 00010101=22 00010100=42 00010011=15 00010010=51 00010001=23 00010000=43 "
    "00001111=25 00001110=37 00001101=26 00001100=38 00001011=29 00001010=45 00001001=53 00001000=57 "
    "00000111=30 00000110=46 00000101=54 00000100=58 000000111=31 000000110=47 000000101=55 "
    "000000100=59 000000011=27 000000010=39 000000001=0";
static const char spec_mv[] =
    "1=0 01=1 001=2 0001=3 000011=4 0000101=5 0000100=6 0000011=7 000001011=8 000001010=9 "
    "000001001=10 0000010001=11 0000010000=12 0000001111=13 0000001110=14 0000001101=15 0000001100=16";
/* DCT coefficients: run,level; E end of block, X escape.  The sign
 * bit follows every run,level code. */
static const char spec_dct14[] =
    "10=E 11=0,1 011=1,1 0100=0,2 0101=2,1 00101=0,3 00111=3,1 00110=4,1 000110=1,2 000111=5,1 "
    "000101=6,1 000100=7,1 0000110=0,4 0000100=2,2 0000111=8,1 0000101=9,1 000001=X "
    "00100110=0,5 00100001=0,6 00100101=1,3 00100100=3,2 00100111=10,1 00100011=11,1 "
    "00100010=12,1 00100000=13,1 0000001010=0,7 0000001100=1,4 0000001011=2,3 0000001111=4,2 "
    "0000001001=5,2 0000001110=14,1 0000001101=15,1 0000001000=16,1 000000011101=0,8 "
    "000000011000=0,9 000000010011=0,10 000000010000=0,11 000000011011=1,5 000000010100=2,4 "
    "000000011100=3,3 000000010010=4,3 000000011110=6,2 000000010101=7,2 000000010001=8,2 "
    "000000011111=17,1 000000011010=18,1 000000011001=19,1 000000010111=20,1 000000010110=21,1 "
    "0000000011010=0,12 0000000011001=0,13 0000000011000=0,14 0000000010111=0,15 "
    "0000000010110=1,6 0000000010101=1,7 0000000010100=2,5 0000000010011=3,4 0000000010010=5,3 "
    "0000000010001=9,2 0000000010000=10,2 0000000011111=22,1 0000000011110=23,1 "
    "0000000011101=24,1 0000000011100=25,1 0000000011011=26,1 00000000011111=0,16 "
    "00000000011110=0,17 00000000011101=0,18 00000000011100=0,19 00000000011011=0,20 "
    "00000000011010=0,21 00000000011001=0,22 00000000011000=0,23 00000000010111=0,24 "
    "00000000010110=0,25 00000000010101=0,26 00000000010100=0,27 00000000010011=0,28 "
    "00000000010010=0,29 00000000010001=0,30 00000000010000=0,31 000000000011000=0,32 "
    "000000000010111=0,33 000000000010110=0,34 000000000010101=0,35 000000000010100=0,36 "
    "000000000010011=0,37 000000000010010=0,38 000000000010001=0,39 000000000010000=0,40 "
    "000000000011111=1,8 000000000011110=1,9 000000000011101=1,10 000000000011100=1,11 "
    "000000000011011=1,12 000000000011010=1,13 000000000011001=1,14 0000000000010011=1,15 "
    "0000000000010010=1,16 0000000000010001=1,17 0000000000010000=1,18 0000000000010100=6,3 "
    "0000000000011010=11,2 0000000000011001=12,2 0000000000011000=13,2 0000000000010111=14,2 "
    "0000000000010110=15,2 0000000000010101=16,2 0000000000011111=27,1 0000000000011110=28,1 "
    "0000000000011101=29,1 0000000000011100=30,1 0000000000011011=31,1";
/* Table B.15 (intra_vlc_format 1): its own codes; those of 12 bits and
 * more are B.14's. */
static const char spec_dct15[] =
    "0110=E 10=0,1 010=1,1 110=0,2 00101=2,1 0111=0,3 00111=3,1 000110=4,1 00110=1,2 000111=5,1 "
    "0000110=6,1 0000100=7,1 11100=0,4 0000111=2,2 0000101=8,1 1111000=9,1 000001=X 11101=0,5 "
    "000101=0,6 1111001=1,3 00100110=3,2 1111010=10,1 00100001=11,1 00100101=12,1 00100100=13,1 "
    "000100=0,7 00100111=1,4 11111100=2,3 11111101=4,2 000000100=5,2 000000101=14,1 "
    "000000111=15,1 0000001101=16,1 1111011=0,8 1111100=0,9 00100011=0,10 00100010=0,11 "
    "00100000=1,5 0000001100=2,4 11111010=0,12 11111011=0,13 11111110=0,14 11111111=0,15";

#define DCT_EOB 0
#define DCT_ESC 1

/* Fill `tab` (2^bits entries) with the codes of `spec` that start with
 * `zeros` zero bits and have at most zeros + bits bits, indexed by the
 * bits after the zeros.  `dct`: values are run,level / E / X. */
static void build(u16 *tab, int bits, int zeros, const char *spec, int dct)
{
    const char *s = spec;
    while (*s) {
        u32 code = 0, value = 0;
        int len = 0, lead = 0, counting = 1;
        while (*s == ' ')
            s++;
        while (*s == '0' || *s == '1') {
            code = code << 1 | (u32)(*s - '0');
            if (counting && *s == '0')
                lead++;
            else
                counting = 0;
            len++;
            s++;
        }
        s++; /* '=' */
        if (*s == 'E') {
            value = DCT_EOB;
            s++;
        } else if (*s == 'X') {
            value = DCT_ESC;
            s++;
        } else {
            u32 a = 0, b = 0;
            while (*s >= '0' && *s <= '9')
                a = a * 10 + (u32)(*s++ - '0');
            if (*s == ',') {
                s++;
                while (*s >= '0' && *s <= '9')
                    b = b * 10 + (u32)(*s++ - '0');
                value = a | b << 5;
            } else {
                value = a;
            }
            (void)dct;
        }
        if (lead >= zeros && len - zeros <= bits && len > 0) {
            int rest = len - zeros, i;
            u32 first = (code & ((1u << rest) - 1)) << (bits - rest);
            for (i = 0; i < 1 << (bits - rest); i++)
                tab[first + (u32)i] = (u16)(value << 5 | (u32)len);
        }
    }
}

static const u8 zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};
static const u8 alternate[64] = {
    0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63
};
static const u8 default_intra[64] = { /* in zigzag order, as transmitted */
    8, 16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83
};
static const u8 nonlinear_q[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22,
    24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112
};

/* ---- the bitstream ---- */

static const u8 *bp, *bend;
static u32 bb;
static int bc;

static void bits_at(const u8 *p, const u8 *end)
{
    bp = p;
    bend = end;
    bb = 0;
    bc = 0;
}

/* At least 25 bits in bb afterwards.  Past the end it reads the
 * padding, which is zeroes (and is looked for). */
static inline void refill(void)
{
    while (bc <= 24) {
        bb |= (u32)*bp++ << (24 - bc);
        bc += 8;
    }
}

static inline u32 show(int n)
{
    refill();
    return bb >> (32 - n);
}

static inline void skip(int n)
{
    bb <<= n;
    bc -= n;
}

static inline u32 get(int n)
{
    u32 v = show(n);
    skip(n);
    return v;
}

static inline u32 get1(void)
{
    return get(1);
}

/* A code of the table (which is indexed by `bits` bits); -1 if none. */
static inline int vlc(const u16 *tab, int bits)
{
    u32 e = tab[show(bits)];
    if (!(e & 31))
        return -1;
    skip((int)(e & 31));
    return (int)(e >> 5);
}

/* ---- the state ---- */

static struct frame frames[3];
static struct frame *fwd, *bwd, *cur, *bframe;
static int have_fwd, have_bwd; /* references that can be predicted from */
static int ref_waiting;        /* bwd has not been shown yet */
static int lw, lh, cwid, chgt; /* stored plane sizes (luma, chroma) */
static int hs;                 /* 1: half width */
static int mbw, mbh;
static int seq_ok;

static u8 q_intra[64], q_inter[64]; /* raster order */
static int qx_intra_set, qx_inter_set;

static struct {
    int type, structure, tff, fpfd, conceal, qst, ivlc, altscan, rff, pframe;
    int dcprec;
    int fcode[2][2];
    int fullpel[2];
    int second_field; /* this field picture is the second of its frame */
    u32 pts;
    int has_pts;
} pic;

static const u8 *scan;
static int quant;          /* quantiser_scale */
static int dc_pred[3];
static int pmv[2][2][2];   /* [r][s][t] */
static int mb_x, mb_y;     /* of the current macroblock (field rows in field pictures) */
static int last_type, last_motion;
static int last_fs[2][2];  /* field_select [r][s] */
static int last_mv[2][2][2];
static int mv_count, mv_format, dmv; /* of the current macroblock */
static int field_pic;      /* picture_structure != frame */
static int parity;         /* of a field picture: 0 top, 1 bottom */

static s16 block[64];


/* ---- inverse transform ---- */

#define K1 4017
#define K2 3784
#define K3 3406
#define K4 2896
#define K5 2276
#define K6 1567
#define K7 799

/* One dimension, eight points: o[k*os] from i[k*is]. */
static inline void idct8(const int *in, int is, int *out, int os, int shift)
{
    int x0 = in[0], x1 = in[is], x2 = in[2 * is], x3 = in[3 * is];
    int x4 = in[4 * is], x5 = in[5 * is], x6 = in[6 * is], x7 = in[7 * is];
    int t0 = (x0 + x4) * K4, t1 = (x0 - x4) * K4;
    int p = x2 * K2 + x6 * K6, q = x2 * K6 - x6 * K2;
    int e0 = t0 + p, e3 = t0 - p, e1 = t1 + q, e2 = t1 - q;
    int o0 = x1 * K1 + x3 * K3 + x5 * K5 + x7 * K7;
    int o1 = x1 * K3 - x3 * K7 - x5 * K1 - x7 * K5;
    int o2 = x1 * K5 - x3 * K1 + x5 * K7 + x7 * K3;
    int o3 = x1 * K7 - x3 * K5 + x5 * K3 - x7 * K1;
    int r = 1 << (shift - 1);
    out[0] = (e0 + o0 + r) >> shift;
    out[7 * os] = (e0 - o0 + r) >> shift;
    out[1 * os] = (e1 + o1 + r) >> shift;
    out[6 * os] = (e1 - o1 + r) >> shift;
    out[2 * os] = (e2 + o2 + r) >> shift;
    out[5 * os] = (e2 - o2 + r) >> shift;
    out[3 * os] = (e3 + o3 + r) >> shift;
    out[4 * os] = (e3 - o3 + r) >> shift;
}

/* Four points from the four lowest of eight frequencies: the values
 * halfway between the pixel pairs of the eight-point transform. */
static inline void idct4(const int *in, int *out, int shift)
{
    int x0 = in[0], x1 = in[1], x2 = in[2], x3 = in[3];
    int a = (x0 + x2) * K4, b = (x0 - x2) * K4;
    int p = x1 * K2 + x3 * K6, q = x1 * K6 - x3 * K2;
    int r = 1 << (shift - 1);
    out[0] = (a + p + r) >> shift;
    out[3] = (a - p + r) >> shift;
    out[1] = (b + q + r) >> shift;
    out[2] = (b - q + r) >> shift;
}

static inline u8 clamp(int v)
{
    return (unsigned)v <= 255 ? (u8)v : v < 0 ? 0 : 255;
}

/* The block in `block` (cols: the columns that may be non-zero, from
 * the left), transformed, put (add = 0) or added to the 8 x 8 (or 4 x 8)
 * pixels at dst.  Clears `block`. */
static void transform(u8 *dst, int stride, int add, int cols, int only_dc)
{
    int tmp[64], out[8];
    int w = 8 >> hs, x, y;
    if (only_dc) {
        int v = (block[0] + 4) >> 3;
        block[0] = 0;
        for (y = 0; y < 8; y++, dst += stride)
            for (x = 0; x < w; x++)
                dst[x] = clamp((add ? dst[x] : 0) + v);
        return;
    }
    /* Columns first: tmp holds eight times the values. */
    for (x = 0; x < cols; x++) {
        int c[8], k, any = 0;
        for (k = 0; k < 8; k++) {
            c[k] = block[8 * k + x];
            any |= k ? c[k] : 0;
        }
        if (!any) {
            int v = (c[0] * K4 + 512) >> 10;
            for (k = 0; k < 8; k++)
                tmp[8 * k + x] = v;
        } else {
            idct8(c, 1, tmp + x, 8, 10);
        }
    }
    for (; x < (hs ? 4 : 8); x++)
        for (y = 0; y < 8; y++)
            tmp[8 * y + x] = 0;
    for (y = 0; y < 8; y++, dst += stride) {
        if (hs)
            idct4(tmp + 8 * y, out, 16);
        else
            idct8(tmp + 8 * y, 1, out, 1, 16);
        if (add)
            for (x = 0; x < w; x++)
                dst[x] = clamp(dst[x] + out[x]);
        else
            for (x = 0; x < w; x++)
                dst[x] = clamp(out[x]);
    }
    memset(block, 0, sizeof block);
}

/* ---- coefficients ---- */

/* Returns the highest column used plus one, or -1 for an error; *dc
 * says whether only the DC coefficient is there. */
static int coefficients(int intra, int cc, int *only_dc)
{
    const u16 *t8 = intra && pic.ivlc ? t_dct15 : t_dct14;
    const u16 *tl = intra && pic.ivlc ? t_dctlong15 : t_dctlong14;
    const u8 *w = intra ? q_intra : q_inter;
    int i, cols = 1, sum = 0, mpeg2 = video.mpeg2, colmax = hs ? 4 : 8;
    *only_dc = 1;
    if (intra) {
        int size, diff = 0;
        u32 b;
        if (cc == 0) {
            b = show(9);
            if (b < 0x100) {
                size = (int)(b >> 7) + 1;
                skip(2);
            } else if (b < 0x1c0) {
                static const u8 three[3] = { 0, 3, 4 };
                size = three[(b >> 6) - 4];
                skip(3);
            } else {
                int n = 3;
                while (n < 9 && (b & (0x100 >> n)))
                    n++;
                if (n == 9) {
                    size = 11;
                    skip(9);
                } else {
                    size = n + 2;
                    skip(n + 1);
                }
            }
        } else {
            b = show(10);
            if (b < 0x300) {
                size = (int)(b >> 8);
                skip(2);
            } else {
                int n = 2;
                while (n < 10 && (b & (0x200 >> n)))
                    n++;
                if (n == 10) {
                    size = 11;
                    skip(10);
                } else {
                    size = n + 1;
                    skip(n + 1);
                }
            }
        }
        if (size) {
            diff = (int)get(size);
            if (!(diff >> (size - 1)))
                diff -= (1 << size) - 1;
        }
        dc_pred[cc] += diff;
        if (mpeg2)
            block[0] = (s16)(dc_pred[cc] << (3 - pic.dcprec));
        else
            block[0] = (s16)(dc_pred[cc] << 3);
        sum = block[0];
        i = 1;
    } else {
        /* The first coefficient: '1s' is run 0, level 1. */
        i = 0;
        if (show(1)) {
            int level = (int)show(2) & 1 ? -1 : 1, v;
            skip(2);
            v = ((2 * level + (level > 0 ? 1 : -1)) * w[0] * quant) / 32;
            if (!mpeg2 && v && !(v & 1))
                v -= v > 0 ? 1 : -1;
            block[0] = (s16)v;
            sum = v;
            i = 1;
        }
    }
    {
        /* The bitstream in registers for the loop: at least 17 bits in
         * cb after a refill, two bytes at a time. */
        u32 cb_ = bb;
        int cn = bc;
        const u8 *cp = bp;
#define CREFILL()                                                         \
    do {                                                                  \
        if (cn <= 16) {                                                   \
            cb_ |= ((u32)cp[0] << 8 | cp[1]) << (16 - cn);                \
            cp += 2;                                                      \
            cn += 16;                                                     \
        }                                                                 \
    } while (0)
#define CSKIP(n) (cb_ <<= (n), cn -= (n))
        const int qs = quant;
        for (;;) {
            u32 e, b;
            int run, level, len, pos, v;
            CREFILL();
            b = cb_ >> 16;
            e = b >= 0x0400 ? t8[b >> 8] : tl[b & 0x3ff];
            len = (int)(e & 31);
            if (!len)
                goto fail;
            run = (int)(e >> 5) & 31;
            level = (int)(e >> 10);
            if (level == 0) {
                CSKIP(len);
                if (run == DCT_EOB)
                    break;
                /* Escape: run 6 bits, then the level. */
                CREFILL();
                run = (int)(cb_ >> 26);
                CSKIP(6);
                CREFILL();
                if (mpeg2) {
                    level = (int)(cb_ >> 20);
                    CSKIP(12);
                    if (level & 0x800)
                        level -= 0x1000;
                    if (!(level & 0x7ff))
                        goto fail;
                } else {
                    level = (int)(cb_ >> 24);
                    CSKIP(8);
                    if (level == 0 || level == 128) {
                        int more;
                        CREFILL();
                        more = (int)(cb_ >> 24);
                        CSKIP(8);
                        level = level ? more - 256 : more;
                    } else if (level > 128) {
                        level -= 256;
                    }
                }
            } else {
                /* The sign is the bit after the code (len <= 16). */
                if ((cb_ << len) & 0x80000000u)
                    level = -level;
                CSKIP(len + 1);
            }
            i += run;
            if (i > 63)
                goto fail;
            pos = scan[i];
            if (intra)
                v = (level * w[pos] * qs) / 16;
            else
                v = ((2 * level + (level > 0 ? 1 : -1)) * w[pos] * qs) / 32;
            if (!mpeg2 && v && !(v & 1))
                v -= v > 0 ? 1 : -1;
            if ((unsigned)(v + 2048) > 4095)
                v = v > 0 ? 2047 : -2048;
            if ((pos & 7) < colmax) {
                block[pos] = (s16)v;
                if ((pos & 7) >= cols)
                    cols = (pos & 7) + 1;
                if (pos)
                    *only_dc = 0;
            }
            sum += v;
            i++;
        }
        bb = cb_;
        bc = cn;
        bp = cp;
        goto done;
    fail:
        bb = cb_;
        bc = cn;
        bp = cp;
        return -1;
#undef CREFILL
#undef CSKIP
    }
done:
    /* Mismatch control (MPEG-2): only meaningful at full size. */
    if (mpeg2 && !hs && !(sum & 1)) {
        block[63] ^= 1;
        *only_dc = 0;
        cols = 8;
    }
    return cols;
}

/* ---- motion compensation ---- */

/* Predict a bw x bh block of a plane (w x h stored samples, addressed
 * with rstride a line) into dst.  (x, y): the block in stored samples
 * and lines of the plane; (mx, my): the vector in half samples of the
 * coded picture.  avg: average with what is there. */
static void predict(u8 *dst, int dstride, const u8 *ref, int rstride, int w, int h,
                    int x, int y, int mx, int my, int bw, int bh, int avg)
{
    int qx = ((x << (1 + hs)) + mx) << (1 - hs), qy = (y << 1) + my;
    int ix = qx >> 2, fx = qx & 3, iy = qy >> 1, fy = qy & 1, i, j;
    const u8 *s;
    /* Vectors are not to point outside the picture, but encoders let
     * field vectors do so at the edges: the samples there are the
     * edge's, repeated, as decoders take them. */
    if (ix < 0 || iy < 0 || ix + bw + 1 > w || iy + bh + 1 > h) {
        static u8 edge[17 * 17];
        for (j = 0; j <= bh; j++) {
            int yy = iy + j < 0 ? 0 : iy + j >= h ? h - 1 : iy + j;
            for (i = 0; i <= bw; i++) {
                int xx = ix + i < 0 ? 0 : ix + i >= w ? w - 1 : ix + i;
                edge[17 * j + i] = ref[yy * rstride + xx];
            }
        }
        ref = edge;
        rstride = 17;
        ix = iy = 0;
    }
    s = ref + iy * rstride + ix;
    /* One loop for each kind of position and for averaging or not,
     * the test outside. */
#define ROWS(expr)                                                    \
    do {                                                              \
        if (avg)                                                      \
            for (j = 0; j < bh; j++, s += rstride, dst += dstride) {  \
                const u8 *t = s + rstride;                            \
                (void)t;                                              \
                for (i = 0; i < bw; i++) {                            \
                    int p = (expr);                                   \
                    dst[i] = (u8)((dst[i] + p + 1) >> 1);             \
                }                                                     \
            }                                                         \
        else                                                          \
            for (j = 0; j < bh; j++, s += rstride, dst += dstride) {  \
                const u8 *t = s + rstride;                            \
                (void)t;                                              \
                for (i = 0; i < bw; i++)                              \
                    dst[i] = (u8)(expr);                              \
            }                                                         \
    } while (0)
    if (!(fx | fy) && !avg && !(((unsigned long)s | (unsigned long)dst | (unsigned long)rstride | (unsigned long)dstride) & 3)) {
        /* A copy, long words at a time (most are: still backgrounds,
         * macroblocks left out). */
        for (j = 0; j < bh; j++, s += rstride, dst += dstride)
            for (i = 0; i < bw; i += 4)
                *(u32 *)(dst + i) = *(const u32 *)(s + i);
        return;
    }
    switch (fx | fy << 2) {
    case 0: ROWS(s[i]); break;
    case 1: ROWS((3 * s[i] + s[i + 1] + 2) >> 2); break;
    case 2: ROWS((s[i] + s[i + 1] + 1) >> 1); break;
    case 3: ROWS((s[i] + 3 * s[i + 1] + 2) >> 2); break;
    case 4: ROWS((s[i] + t[i] + 1) >> 1); break;
    case 5: ROWS((3 * s[i] + s[i + 1] + 3 * t[i] + t[i + 1] + 4) >> 3); break;
    case 6: ROWS((s[i] + s[i + 1] + t[i] + t[i + 1] + 2) >> 2); break;
    default: ROWS((s[i] + 3 * s[i + 1] + t[i] + 3 * t[i + 1] + 4) >> 3); break;
    }
#undef ROWS
}

/* The three planes of a 16 x 16 macroblock (or of one field of it:
 * `field` 0 / 1 the destination's lines of that parity; -1 all lines)
 * predicted from ref, whose field `rfield` is used (-1: as a frame).
 * mx, my: luma vector; height of the luma part bh (16 or 8); y0 the
 * first luma line of the part, in the lines of the addressing used. */
static void predict_mb(const struct frame *ref, int rfield, int dfield, int y0, int bh,
                       int mx, int my, int avg)
{
    int ls = lw, cs = cwid;
    int rls = rfield < 0 ? ls : 2 * ls, rcs = rfield < 0 ? cs : 2 * cs;
    int dls = dfield < 0 ? ls : 2 * ls, dcs = dfield < 0 ? cs : 2 * cs;
    int ph = rfield < 0 ? lh : lh / 2, pch = rfield < 0 ? chgt : chgt / 2;
    int roff = rfield < 0 ? 0 : rfield, doff = dfield < 0 ? 0 : dfield;
    int x = mb_x * (16 >> hs), cx = mb_x * (8 >> hs);
    int cmx = mx / 2, cmy = my / 2;
    u8 *dy = cur->y + doff * ls + y0 * dls + x;
    u8 *dcb = cur->cb + doff * cs + (y0 / 2) * dcs + cx;
    u8 *dcr = cur->cr + doff * cs + (y0 / 2) * dcs + cx;
    predict(dy, dls, ref->y + roff * ls, rls, lw, ph, x, y0, mx, my, 16 >> hs, bh, avg);
    predict(dcb, dcs, ref->cb + roff * cs, rcs, cwid, pch, cx, y0 / 2, cmx, cmy, 8 >> hs, bh / 2, avg);
    predict(dcr, dcs, ref->cr + roff * cs, rcs, cwid, pch, cx, y0 / 2, cmx, cmy, 8 >> hs, bh / 2, avg);
}

/* The reference for direction s (0 forward, 1 backward) and a field
 * select, in a field picture: the first field of this frame, when a
 * P field refers to the other parity of its own frame. */
static const struct frame *field_ref(int s, int fs)
{
    if (s == 0 && pic.type == 2 && pic.second_field && fs != parity)
        return cur;
    return s ? bwd : fwd;
}

/* The prediction of the current macroblock from its motion data. */
static void motion(int type, int motion_type, int vecs[2][2][2], int fs[2][2])
{
    int s, avg = 0;
    for (s = 0; s < 2; s++) {
        int dir = s ? MB_BWD : MB_FWD;
        const struct frame *ref = s ? bwd : fwd;
        if (!(type & dir))
            continue;
        if (!field_pic) {
            int y = mb_y * 16;
            if (motion_type == 2 || video.progressive || pic.fpfd || !video.mpeg2) {
                predict_mb(ref, -1, -1, y, 16, vecs[0][s][0], vecs[0][s][1], avg);
            } else if (motion_type == 1) {
                /* Field prediction: each field of the macroblock from
                 * the selected field of the reference. */
                predict_mb(ref, fs[0][s], 0, y / 2, 8, vecs[0][s][0], vecs[0][s][1], avg);
                predict_mb(ref, fs[1][s], 1, y / 2, 8, vecs[1][s][0], vecs[1][s][1], avg);
            } else {
                /* Dual prime, from the same parity only. */
                predict_mb(ref, 0, 0, y / 2, 8, vecs[0][s][0], vecs[0][s][1], avg);
                predict_mb(ref, 1, 1, y / 2, 8, vecs[0][s][0], vecs[0][s][1], avg);
            }
        } else {
            int y = mb_y * 16;
            if (motion_type == 1) {
                predict_mb(field_ref(s, fs[0][s]), fs[0][s], parity, y, 16, vecs[0][s][0], vecs[0][s][1], avg);
            } else if (motion_type == 2) {
                predict_mb(field_ref(s, fs[0][s]), fs[0][s], parity, y, 8, vecs[0][s][0], vecs[0][s][1], avg);
                predict_mb(field_ref(s, fs[1][s]), fs[1][s], parity, y + 8, 8, vecs[1][s][0], vecs[1][s][1], avg);
            } else {
                predict_mb(field_ref(s, parity), parity, parity, y, 16, vecs[0][s][0], vecs[0][s][1], avg);
            }
        }
        avg = 1;
    }
}

/* ---- motion vectors ---- */

static int motion_component(int fcode, int pred)
{
    int code = vlc(t_mv, 10), r = fcode - 1, f = 1 << r, delta, v;
    int low = -16 * f, high = 16 * f - 1, range = 32 * f;
    if (code < 0)
        return pred; /* the slice ends in an error anyway */
    if (code && get1())
        code = -code;
    if (f == 1 || code == 0) {
        delta = code;
    } else {
        int residual = (int)get(r);
        delta = ((code < 0 ? -code : code) - 1) * f + residual + 1;
        if (code < 0)
            delta = -delta;
    }
    v = pred + delta;
    if (v < low)
        v += range;
    if (v > high)
        v -= range;
    return v;
}

static void read_vector(int r, int s, int vecs[2][2][2])
{
    int fx = pic.fcode[s][0], fy = pic.fcode[s][1];
    int frame_field = !field_pic && mv_format == 0; /* field vector in a frame picture */
    int v;
    v = motion_component(fx, pmv[r][s][0]);
    pmv[r][s][0] = v;
    vecs[r][s][0] = v;
    if (dmv)
        (void)(show(1) ? get(2) : get(1));
    if (frame_field) {
        v = motion_component(fy, pmv[r][s][1] >> 1);
        pmv[r][s][1] = v * 2;
    } else {
        v = motion_component(fy, pmv[r][s][1]);
        pmv[r][s][1] = v;
    }
    vecs[r][s][1] = v;
    if (dmv)
        (void)(show(1) ? get(2) : get(1));
    if (!video.mpeg2 && pic.fullpel[s]) {
        vecs[r][s][0] <<= 1;
        vecs[r][s][1] <<= 1;
    }
}

/* mv_format: 0 field, 1 frame. */
static void read_vectors(int s, int vecs[2][2][2], int fs[2][2])
{
    if (mv_count == 1) {
        if (mv_format == 0 && !dmv)
            fs[0][s] = (int)get1();
        read_vector(0, s, vecs);
        /* The second set as the first (used by later skipped
         * macroblocks and as the predictor). */
        pmv[1][s][0] = pmv[0][s][0];
        pmv[1][s][1] = pmv[0][s][1];
    } else {
        fs[0][s] = (int)get1();
        read_vector(0, s, vecs);
        fs[1][s] = (int)get1();
        read_vector(1, s, vecs);
    }
}

static void reset_pmv(void)
{
    memset(pmv, 0, sizeof pmv);
}

static void reset_dc(void)
{
    int v = video.mpeg2 ? 128 << pic.dcprec : 128;
    dc_pred[0] = dc_pred[1] = dc_pred[2] = v;
}

/* ---- macroblocks ---- */

static void blocks(int type, int cbp, int dct_field)
{
    int b, intra = type & MB_INTRA;
    int ls = lw, cs = cwid;
    int fl = field_pic ? 2 * ls : ls, fc = field_pic ? 2 * cs : cs;
    int poff = field_pic ? parity : 0;
    u8 *y = cur->y + poff * ls + mb_y * 16 * fl + mb_x * (16 >> hs);
    u8 *cb = cur->cb + poff * cs + mb_y * 8 * fc + mb_x * (8 >> hs);
    u8 *cr = cur->cr + poff * cs + mb_y * 8 * fc + mb_x * (8 >> hs);
    for (b = 0; b < 6; b++) {
        int cols, only_dc;
        u8 *dst;
        int stride;
        if (!(cbp & (32 >> b)))
            continue;
        cols = coefficients(intra, b < 4 ? 0 : b - 3, &only_dc);
        if (cols < 0) {
            memset(block, 0, sizeof block);
            video_errors++;
            return;
        }
        if (b < 4) {
            if (dct_field) {
                dst = y + (b & 1) * (8 >> hs) + (b >> 1) * fl;
                stride = 2 * fl;
            } else {
                dst = y + (b & 1) * (8 >> hs) + (b >> 1) * 8 * fl;
                stride = fl;
            }
        } else {
            dst = b == 4 ? cb : cr;
            stride = fc;
        }
        transform(dst, stride, !intra, cols, only_dc);
    }
}

/* A macroblock left out: P, the reference as it is; B, the
 * prediction of the one before. */
static void skipped(void)
{
    int vecs[2][2][2];
    if (pic.type == 2) {
        int zero[2][2] = { { parity, parity }, { parity, parity } };
        memset(vecs, 0, sizeof vecs);
        reset_pmv();
        motion(MB_FWD, field_pic ? 1 : 2, vecs, zero);
        last_type = MB_FWD;
        last_motion = field_pic ? 1 : 2;
        memset(last_mv, 0, sizeof last_mv);
    } else {
        motion(last_type, last_motion, last_mv, last_fs);
    }
    reset_dc();
}

static int macroblock(void)
{
    int type = vlc(t_mbtype[pic.type - 1], 6);
    int motion_type = 2, dct_field = 0, cbp = 0, s;
    int vecs[2][2][2];
    int fs[2][2];
    if (type < 0)
        return -1;
    if (type & (MB_FWD | MB_BWD)) {
        if (video.mpeg2) {
            if (field_pic)
                motion_type = (int)get(2);
            else if (!pic.fpfd)
                motion_type = (int)get(2);
            else
                motion_type = 2;
        }
    }
    if (!field_pic && video.mpeg2 && !pic.fpfd && (type & (MB_INTRA | MB_PATTERN)))
        dct_field = (int)get1();
    if (type & MB_QUANT) {
        int code = (int)get(5);
        quant = video.mpeg2 ? (pic.qst ? nonlinear_q[code] : code * 2) : code * 2;
    }
    /* What the vectors are: field_pic motion_type 1 field, 2 16x8,
     * 3 dual prime; frame pictures 1 field, 2 frame, 3 dual prime. */
    if (field_pic) {
        mv_count = motion_type == 2 ? 2 : 1;
        mv_format = 0;
        dmv = motion_type == 3;
    } else {
        mv_count = motion_type == 1 ? 2 : 1;
        mv_format = motion_type == 2 ? 1 : 0;
        dmv = motion_type == 3;
    }
    fs[0][0] = fs[0][1] = fs[1][0] = fs[1][1] = parity;
    memset(vecs, 0, sizeof vecs);
    if (type & MB_INTRA) {
        if (pic.conceal) {
            /* Vectors for concealment: read and dropped. */
            if (!field_pic) {
                mv_count = 1;
                mv_format = 1;
            } else {
                mv_count = 1;
                mv_format = 0;
            }
            dmv = 0;
            read_vectors(0, vecs, fs);
            get1();
        } else {
            reset_pmv();
        }
        blocks(type, 63, dct_field);
        last_type = MB_INTRA;
        return 0;
    }
    for (s = 0; s < 2; s++)
        if (type & (s ? MB_BWD : MB_FWD))
            read_vectors(s, vecs, fs);
    if (type & MB_PATTERN) {
        cbp = vlc(t_cbp, 9);
        if (cbp < 0)
            return -1;
    }
    reset_dc();
    if (pic.type == 2 && !(type & MB_FWD)) {
        /* P without vectors: from the reference at zero. */
        reset_pmv();
        type |= MB_FWD;
        motion_type = field_pic ? 1 : 2;
        memset(vecs, 0, sizeof vecs);
    }
    motion(type, motion_type, vecs, fs);
    memcpy(last_mv, vecs, sizeof vecs);
    memcpy(last_fs, fs, sizeof fs);
    last_type = type & (MB_FWD | MB_BWD);
    last_motion = motion_type;
    if (cbp)
        blocks(type, cbp, dct_field);
    return 0;
}

static void slice(const u8 *p, const u8 *end, int row)
{
    int addr, rows = field_pic ? mbh / 2 : mbh, total;
    bits_at(p, end);
    quant = (int)get(5);
    quant = video.mpeg2 ? (pic.qst ? nonlinear_q[quant] : quant * 2) : quant * 2;
    while (get1())
        get(8);
    reset_pmv();
    reset_dc();
    last_type = 0;
    total = rows * mbw;
    addr = row * mbw - 1;
    for (;;) {
        int inc = 0, code;
        for (;;) {
            code = vlc(t_mba, 11);
            if (code < 0)
                goto bad;
            if (code == 35)
                continue;
            if (code == 34) {
                inc += 33;
                continue;
            }
            inc += code;
            break;
        }
        if (addr >= row * mbw) {
            /* Macroblocks left out in between (none at a slice's start). */
            while (--inc > 0) {
                addr++;
                if (addr >= total || pic.type == 1)
                    goto bad;
                if (++mb_x == mbw)
                    mb_x = 0, mb_y++;
                skipped();
            }
            addr++;
            if (++mb_x == mbw)
                mb_x = 0, mb_y++;
        } else {
            addr += inc;
            if (addr >= total)
                goto bad;
            mb_x = addr % mbw;
            mb_y = addr / mbw;
        }
        if (addr >= total)
            goto bad;
        if (macroblock())
            goto bad;
        if (bp > end + 4)
            goto bad;
        /* The slice ends where 23 zero bits begin. */
        if (show(23) == 0)
            return;
    }
bad:
    video_errors++;
}

/* ---- headers ---- */

static void set_matrices(void)
{
    int i;
    for (i = 0; i < 64; i++) {
        if (!qx_intra_set)
            q_intra[zigzag[i]] = default_intra[i];
        if (!qx_inter_set)
            q_inter[zigzag[i]] = 16;
    }
}

static void sequence_header(const u8 *p, const u8 *end)
{
    int i, w, h;
    bits_at(p, end);
    w = (int)get(12);
    h = (int)get(12);
    video.aspect = (int)get(4);
    video.rate = (int)get(4);
    get(16);
    get(3);  /* bit rate (18 bits), marker */
    get(10); /* vbv */
    get1();  /* constrained */
    qx_intra_set = qx_inter_set = 0;
    set_matrices();
    if (get1()) {
        for (i = 0; i < 64; i++)
            q_intra[zigzag[i]] = (u8)get(8);
        qx_intra_set = 1;
    }
    if (get1()) {
        for (i = 0; i < 64; i++)
            q_inter[zigzag[i]] = (u8)get(8);
        qx_inter_set = 1;
    }
    /* (Until a sequence extension says otherwise: MPEG-1.) */
    video.mpeg2 = 0;
    video.progressive = 1;
    if (w < 16 || h < 16 || w > 720 || h > 576) {
        seq_ok = 0;
        return;
    }
    if (w != video.width || h != video.height) {
        video.width = w;
        video.height = h;
        video_reset();
    }
    seq_ok = 1;
}

static void geometry(void)
{
    int i;
    mbw = (video.width + 15) / 16;
    mbh = video.progressive ? (video.height + 15) / 16 : 2 * ((video.height + 31) / 32);
    hs = video.width > 400 && !video_full_width;
    lw = mbw * 16 >> hs;
    lh = mbh * 16;
    cwid = lw / 2;
    chgt = lh / 2;
    video.stored_width = (video.width + hs) >> hs;
    video.stride = lw;
    video.lines = lh;
    for (i = 0; i < 3; i++) {
        /* Black to start with. */
        memset(frames[i].y, 16, (unsigned long)(lw * lh));
        memset(frames[i].cb, 128, (unsigned long)(cwid * chgt));
        memset(frames[i].cr, 128, (unsigned long)(cwid * chgt));
    }
}

/* Macroblocks of the sequence as it is now; geometry() when the frames
 * are laid out for another. */
static void check_geometry(void)
{
    int w = (video.width + 15) / 16;
    int h = video.progressive ? (video.height + 15) / 16 : 2 * ((video.height + 31) / 32);
    if (!lw || w != mbw || h != mbh)
        geometry();
}

static void extension(const u8 *p, const u8 *end)
{
    int id, i;
    bits_at(p, end);
    id = (int)get(4);
    if (id == 1) { /* sequence */
        int chroma;
        get(8);
        video.progressive = (int)get1();
        chroma = (int)get(2);
        if (chroma != 1 || get(2) || get(2))
            seq_ok = 0;
        video.mpeg2 = 1;
    } else if (id == 3) { /* quant matrices */
        if (get1()) {
            for (i = 0; i < 64; i++)
                q_intra[zigzag[i]] = (u8)get(8);
            qx_intra_set = 1;
        }
        if (get1()) {
            for (i = 0; i < 64; i++)
                q_inter[zigzag[i]] = (u8)get(8);
            qx_inter_set = 1;
        }
    } else if (id == 8) { /* picture coding */
        pic.fcode[0][0] = (int)get(4);
        pic.fcode[0][1] = (int)get(4);
        pic.fcode[1][0] = (int)get(4);
        pic.fcode[1][1] = (int)get(4);
        pic.dcprec = (int)get(2);
        pic.structure = (int)get(2);
        pic.tff = (int)get1();
        pic.fpfd = (int)get1();
        pic.conceal = (int)get1();
        pic.qst = (int)get1();
        pic.ivlc = (int)get1();
        pic.altscan = (int)get1();
        pic.rff = (int)get1();
        get1(); /* chroma_420_type */
        pic.pframe = (int)get1();
    }
}

static void picture_header(const u8 *p, const u8 *end)
{
    bits_at(p, end);
    get(10);
    pic.type = (int)get(3);
    get(16);
    pic.fcode[0][0] = pic.fcode[0][1] = pic.fcode[1][0] = pic.fcode[1][1] = 1;
    pic.fullpel[0] = pic.fullpel[1] = 0;
    if (pic.type == 2 || pic.type == 3) {
        pic.fullpel[0] = (int)get1();
        pic.fcode[0][0] = pic.fcode[0][1] = (int)get(3);
    }
    if (pic.type == 3) {
        pic.fullpel[1] = (int)get1();
        pic.fcode[1][0] = pic.fcode[1][1] = (int)get(3);
    }
    /* MPEG-1 defaults; a picture coding extension overrides them. */
    pic.structure = 3;
    pic.fpfd = 1;
    pic.conceal = 0;
    pic.qst = 0;
    pic.ivlc = 0;
    pic.altscan = 0;
    pic.rff = 0;
    pic.tff = 0;
    pic.dcprec = 0;
    pic.pframe = 1;
}

static const u16 period2[9] = { 6006, 7507, 7500, 7200, 6006, 6000, 3600, 3003, 3000 };

/* After a whole frame (both fields): what to show. */
static struct frame *finished(void)
{
    u32 d = period2[video.rate < 9 ? video.rate : 0];
    /* Twice the frame period, in 90 kHz; repeat_first_field adds a
     * field (a whole frame in a progressive sequence). */
    if (video.progressive && video.mpeg2)
        cur->duration = pic.rff ? (pic.tff ? 3 * d / 2 : d) : d / 2;
    else
        cur->duration = d * (2 + (u32)pic.rff) / 4;
    cur->type = pic.type;
    video_pictures++;
    if (pic.type == 3)
        return cur;
    have_bwd = 1;
    if (ref_waiting) {
        ref_waiting = 1;
        return fwd;
    }
    ref_waiting = 1;
    return 0;
}

static struct frame *start_picture(int skip_b)
{
    if (pic.structure != 3 && pic.second_field)
        return cur; /* the second field goes into the first's frame */
    if (pic.type == 3) {
        if (skip_b || !have_fwd || !have_bwd) {
            video_skipped++;
            return 0;
        }
        cur = bframe;
    } else {
        struct frame *t = fwd;
        fwd = bwd;
        bwd = t;
        cur = bwd;
        have_fwd = have_bwd;
        have_bwd = 0;
        if (pic.type == 2 && !have_fwd) {
            /* A P picture with nothing to predict from (a jump into
             * the stream): skipped too. */
            video_skipped++;
            return 0;
        }
    }
    cur->pts = pic.pts;
    cur->has_pts = pic.has_pts;
    return cur;
}

/* ---- the unit ---- */

struct frame *video_decode(const u8 *data, u32 len, u32 pts, int has_pts, int skip_b)
{
    const u8 *p = data, *end = data + len;
    struct frame *out = 0;
    int in_picture = 0, decoding = 0;
    video_last_type = 0;
    while (p + 4 <= end) {
        const u8 *q;
        int code;
        if (p[0] || p[1] || p[2] != 1) {
            p++;
            continue;
        }
        code = p[3];
        p += 4;
        /* Where the next start code is: the end of this piece. */
        for (q = p; q + 3 <= end && (q[0] || q[1] || q[2] != 1); q++)
            ;
        if (q + 3 > end)
            q = end;
        if (code == 0xb3) {
            sequence_header(p, q);
        } else if (code == 0xb5) {
            extension(p, q);
        } else if (code == 0x00) {
            if (!seq_ok)
                return out;
            picture_header(p, q);
            in_picture = 1;
            video_last_type = pic.type;
        } else if (code >= 0x01 && code <= 0xaf && in_picture) {
            if (!decoding) {
                if (pic.type < 1 || pic.type > 3)
                    return out;
                check_geometry();
                field_pic = pic.structure != 3;
                parity = pic.structure == 2;
                scan = pic.altscan ? alternate : zigzag;
                pic.pts = pts;
                pic.has_pts = has_pts;
                if (!start_picture(skip_b))
                    return out;
                decoding = 1;
            }
            if (code - 1 < (field_pic ? mbh / 2 : mbh))
                slice(p, q, code - 1);
        }
        p = q;
    }
    if (decoding) {
        if (field_pic && !pic.second_field) {
            pic.second_field = 1;
        } else {
            pic.second_field = 0;
            out = finished();
        }
    }
    return out;
}

struct frame *video_flush(void)
{
    if (ref_waiting) {
        ref_waiting = 0;
        return bwd;
    }
    return 0;
}

void video_reset(void)
{
    have_fwd = have_bwd = 0;
    ref_waiting = 0;
    pic.second_field = 0;
}

int video_init(void *(*alloc)(unsigned long))
{
    static const struct { u16 **t; int bits, zeros; const char *spec; } t[] = {
        { &t_mba, 11, 0, spec_mba }, { &t_cbp, 9, 0, spec_cbp }, { &t_mv, 10, 0, spec_mv },
        { &t_mbtype[0], 6, 0, spec_mb_i }, { &t_mbtype[1], 6, 0, spec_mb_p }, { &t_mbtype[2], 6, 0, spec_mb_b },
        { &t_dct14, 8, 0, spec_dct14 }, { &t_dct15, 8, 0, spec_dct15 },
        { &t_dctlong14, 10, 6, spec_dct14 },
    };
    unsigned i;
    u32 size = 720 * 576;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        unsigned long n = 2ul << t[i].bits;
        *t[i].t = alloc(n);
        if (!*t[i].t)
            return -1;
        memset(*t[i].t, 0, n);
        build(*t[i].t, t[i].bits, t[i].zeros, t[i].spec, 0);
    }
    t_dctlong15 = alloc(2048);
    if (!t_dctlong15)
        return -1;
    memcpy(t_dctlong15, t_dctlong14, 2048);
    build(t_dctlong15, 10, 6, spec_dct15, 0);
    for (i = 0; i < 3; i++) {
        /* (Lines of margin above and below: vectors that point a little
         * outside are clamped, but reads stay inside regardless.) */
        u8 *m = alloc(size * 3 / 2 + 4096);
        if (!m)
            return -1;
        frames[i].y = m + 1024;
        frames[i].cb = frames[i].y + size;
        frames[i].cr = frames[i].cb + size / 4;
    }
    fwd = &frames[0];
    bwd = &frames[1];
    bframe = &frames[2];
    cur = bwd;
    memset(&video, 0, sizeof video);
    lw = 0;
    seq_ok = 0;
    video_reset();
    return 0;
}
