/* AC-3 (ATSC A/52), decoded and mixed down to two channels: the sound
 * of most DVDs.  Written from the standard; the tables are its tables.
 * Floating point, single precision: this file is built for the SH-4's
 * FPU (build.sh, the example's file `flags`).
 *
 * Not here: E-AC-3, the second channel of dual mono (1+1 plays the
 * first), and the CRCs (a damaged frame decodes as what it says). */
#include "dvd.h"

#define PI 3.14159265358979f

/* ---- the standard's tables ---- */

static const u8 band_start[51] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 31, 34, 37, 40, 43, 46, 49, 55, 61, 67, 73,
    79, 85, 97, 109, 121, 133, 157, 181, 205, 229, 253
};
static u8 masktab[256];

/* The hearing threshold, by band, for 48 kHz, 44.1 kHz and 32 kHz. */
static const u16 hth[50][3] = {
    { 0x4d0, 0x4f0, 0x580 }, { 0x4d0, 0x4f0, 0x580 }, { 0x440, 0x460, 0x4b0 }, { 0x400, 0x410, 0x450 },
    { 0x3e0, 0x3e0, 0x420 }, { 0x3c0, 0x3d0, 0x3f0 }, { 0x3b0, 0x3c0, 0x3e0 }, { 0x3b0, 0x3b0, 0x3d0 },
    { 0x3a0, 0x3b0, 0x3c0 }, { 0x3a0, 0x3a0, 0x3b0 }, { 0x3a0, 0x3a0, 0x3b0 }, { 0x3a0, 0x3a0, 0x3b0 },
    { 0x3a0, 0x3a0, 0x3a0 }, { 0x390, 0x3a0, 0x3a0 }, { 0x390, 0x390, 0x3a0 }, { 0x390, 0x390, 0x3a0 },
    { 0x380, 0x390, 0x3a0 }, { 0x380, 0x380, 0x3a0 }, { 0x370, 0x380, 0x3a0 }, { 0x370, 0x380, 0x3a0 },
    { 0x360, 0x370, 0x390 }, { 0x360, 0x370, 0x390 }, { 0x350, 0x360, 0x390 }, { 0x350, 0x360, 0x390 },
    { 0x340, 0x350, 0x380 }, { 0x340, 0x350, 0x380 }, { 0x330, 0x340, 0x380 }, { 0x320, 0x340, 0x370 },
    { 0x310, 0x320, 0x360 }, { 0x300, 0x310, 0x350 }, { 0x2f0, 0x300, 0x340 }, { 0x2f0, 0x2f0, 0x330 },
    { 0x2f0, 0x2f0, 0x320 }, { 0x2f0, 0x2f0, 0x310 }, { 0x300, 0x2f0, 0x300 }, { 0x310, 0x300, 0x2f0 },
    { 0x340, 0x320, 0x2f0 }, { 0x390, 0x350, 0x2f0 }, { 0x3e0, 0x390, 0x300 }, { 0x420, 0x3e0, 0x310 },
    { 0x460, 0x420, 0x330 }, { 0x490, 0x450, 0x350 }, { 0x4a0, 0x4a0, 0x3c0 }, { 0x460, 0x490, 0x410 },
    { 0x440, 0x460, 0x470 }, { 0x440, 0x440, 0x4a0 }, { 0x520, 0x480, 0x460 }, { 0x800, 0x630, 0x440 },
    { 0x840, 0x840, 0x450 }, { 0x840, 0x840, 0x4e0 },
};

/* Log addition; zero from 210 on. */
static const u8 latab[256] = {
    64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 52, 51, 50, 49, 48, 47, 47, 46, 45, 44, 44,
    43, 42, 41, 41, 40, 39, 38, 38, 37, 36, 36, 35, 35, 34, 33, 33, 32, 32, 31, 30, 30, 29, 29, 28,
    28, 27, 27, 26, 26, 25, 25, 24, 24, 23, 23, 22, 22, 21, 21, 21, 20, 20, 19, 19, 19, 18, 18, 18,
    17, 17, 17, 16, 16, 16, 15, 15, 15, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 12, 11, 11, 11, 11,
    10, 10, 10, 10, 10, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const u8 baptab[64] = {
    0, 1, 1, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10,
    10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15
};
static const u8 slowdec[4] = { 0x0f, 0x11, 0x13, 0x15 };
static const u8 fastdec[4] = { 0x3f, 0x53, 0x67, 0x7b };
static const u16 slowgain[4] = { 0x540, 0x4d8, 0x478, 0x410 };
static const u16 dbpbtab[4] = { 0x000, 0x700, 0x900, 0xb00 };
static const u16 floortab[8] = { 0x2f0, 0x2b0, 0x270, 0x230, 0x1f0, 0x170, 0x0f0, 0xf800 };
static const u16 fastgain[8] = { 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400 };
static const u8 qbits[16] = { 0, 0, 0, 3, 0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16 };
static const u16 bitrates[19] = { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640 };
static const u8 nfchans_of[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };
static const u8 remat_band[5] = { 13, 25, 37, 61, 253 };

/* ---- arithmetic without a library ---- */

static float sine(float x)
{
    /* x in [0, 2 pi): to a quarter turn, then the series. */
    int q;
    float r, r2, s, c;
    while (x < 0)
        x += 2 * PI;
    while (x >= 2 * PI)
        x -= 2 * PI;
    q = (int)(x / (PI / 2) + 0.5f);
    r = x - (float)q * (PI / 2);
    r2 = r * r;
    s = r * (1 - r2 / 6 * (1 - r2 / 20 * (1 - r2 / 42 * (1 - r2 / 72 * (1 - r2 / 110)))));
    c = 1 - r2 / 2 * (1 - r2 / 12 * (1 - r2 / 30 * (1 - r2 / 56 * (1 - r2 / 90))));
    switch (q & 3) {
    case 0: return s;
    case 1: return c;
    case 2: return -s;
    default: return -c;
    }
}

static float cosine(float x)
{
    return sine(x + PI / 2);
}

static float root(float x)
{
    return __builtin_sqrtf(x);
}

/* ---- state ---- */

static float window[256];
static float xcos1[128], xsin1[128], xcos2[64], xsin2[64];
static float fft_c[64], fft_s[64];
static u8 bitrev128[128], bitrev64[64];
static float pow2m[32]; /* 2^-n */

#define CPL 5 /* the coupling channel's arrays */
#define LFE 6

static int acmod, nfchans, lfeon, fscod;
static int blksw[5], dithflag[5];
static int cplinu, chincpl[5], phsflginu, cplbegf, cplendf;
static int cplbndstrc[18], ncplsubnd, ncplbnd;
static float cplco[5][18];
static int phsflg[18];
static int rematflg[4];
static int chbwcod[5];
static int strtmant[7], endmant[7];
static u8 (*exps)[256], (*bap)[256]; /* [7] */
static float (*coef)[256];  /* [7] */
static float (*delay)[256]; /* [6] */
static int sdcycod, fdcycod, sgaincod, dbpbcod, floorcod;
static int csnroffst, fsnroffst[7], fgaincod[7];
static int cplfleak, cplsleak;
/* Delta bit allocation by channel (0..4) and coupling (5). */
static int deltbae[6], deltnseg[6];
static u8 deltoffst[6][8], deltlen[6][8], deltba[6][8];
static float dynrng, dynrng2;
static float clev, slev;
static u32 rnd = 1;

/* ---- bits ---- */

static const u8 *ab;
static u32 apos, aend;

static u32 bits(int n)
{
    u32 p = apos >> 3, w;
    if (!n)
        return 0;
    if (apos + (u32)n > aend) {
        apos = aend;
        return 0;
    }
    w = (u32)ab[p] << 24 | (u32)ab[p + 1] << 16 | (u32)ab[p + 2] << 8 | ab[p + 3];
    w <<= apos & 7;
    apos += (u32)n;
    return w >> (32 - n);
}

/* ---- sizes ---- */

int ac3_frame_size(const u8 *p)
{
    int fs = p[4] >> 6, code = p[4] & 63, br;
    if (p[0] != 0x0b || p[1] != 0x77 || fs == 3 || code > 37 || (p[5] >> 3) > 10)
        return 0;
    br = bitrates[code >> 1];
    if (fs == 0)
        return 4 * br;
    if (fs == 2)
        return 6 * br;
    return 2 * (br * 1536000 / 705600 + (code & 1));
}

/* ---- bit allocation (A/52 7.2.2) ---- */

static int logadd(int a, int b)
{
    int c = a - b, i = (c < 0 ? -c : c) >> 1;
    if (i > 255)
        i = 255;
    return c >= 0 ? a + latab[i] : b + latab[i];
}

static int lowcomp_of(int a, int b0, int b1, int bin)
{
    if (bin < 7) {
        if (b0 + 256 == b1)
            a = 384;
        else if (b0 > b1)
            a = a - 64 > 0 ? a - 64 : 0;
    } else if (bin < 20) {
        if (b0 + 256 == b1)
            a = 320;
        else if (b0 > b1)
            a = a - 64 > 0 ? a - 64 : 0;
    } else {
        a = a - 128 > 0 ? a - 128 : 0;
    }
    return a;
}

static int max(int a, int b)
{
    return a > b ? a : b;
}

/* ch: 0..4 a full channel, CPL, LFE; dch: its delta index (-1 none). */
static void allocate(int ch, int dch, int fastleak, int slowleak)
{
    int start = strtmant[ch], end = endmant[ch];
    int psd[256], bndpsd[50], excite[50], mask[50];
    int i, j, k, lastbin, bin, begin, bndstrt, bndend, lowcomp;
    int sdecay = slowdec[sdcycod], fdecay = fastdec[fdcycod], sgain = slowgain[sgaincod];
    int dbknee = dbpbtab[dbpbcod], floor = floortab[floorcod];
    int fgain = fastgain[fgaincod[ch]];
    int snroffset = (((csnroffst - 15) << 4) + fsnroffst[ch]) << 2;
    if (floor == 0xf800)
        floor = -0x800;
    if (end <= start)
        return;
    if (csnroffst == 0 && fsnroffst[ch] == 0) {
        memset(bap[ch], 0, 256);
        return;
    }
    for (i = start; i < end; i++)
        psd[i] = 3072 - (exps[ch][i] << 7);
    j = start;
    k = masktab[start];
    do {
        lastbin = band_start[k + 1] < end ? band_start[k + 1] : end;
        bndpsd[k] = psd[j++];
        for (; j < lastbin; j++)
            bndpsd[k] = logadd(bndpsd[k], psd[j]);
        k++;
    } while (end > lastbin);

    bndstrt = masktab[start];
    bndend = masktab[end - 1] + 1;
    if (bndstrt == 0) {
        lowcomp = lowcomp_of(0, bndpsd[0], bndpsd[1], 0);
        excite[0] = bndpsd[0] - fgain - lowcomp;
        lowcomp = lowcomp_of(lowcomp, bndpsd[1], bndpsd[2], 1);
        excite[1] = bndpsd[1] - fgain - lowcomp;
        begin = 7;
        for (bin = 2; bin < 7; bin++) {
            if (bndend != 7 || bin != 6)
                lowcomp = lowcomp_of(lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
            fastleak = bndpsd[bin] - fgain;
            slowleak = bndpsd[bin] - sgain;
            excite[bin] = fastleak - lowcomp;
            if (bndend != 7 || bin != 6) {
                if (bndpsd[bin] <= bndpsd[bin + 1]) {
                    begin = bin + 1;
                    break;
                }
            }
        }
        for (bin = begin; bin < (bndend < 22 ? bndend : 22); bin++) {
            if (bndend != 7 || bin != 6)
                lowcomp = lowcomp_of(lowcomp, bndpsd[bin], bndpsd[bin + 1], bin);
            fastleak = max(fastleak - fdecay, bndpsd[bin] - fgain);
            slowleak = max(slowleak - sdecay, bndpsd[bin] - sgain);
            excite[bin] = max(fastleak - lowcomp, slowleak);
        }
        begin = 22;
    } else {
        begin = bndstrt;
    }
    for (bin = begin; bin < bndend; bin++) {
        fastleak = max(fastleak - fdecay, bndpsd[bin] - fgain);
        slowleak = max(slowleak - sdecay, bndpsd[bin] - sgain);
        excite[bin] = max(fastleak, slowleak);
    }
    for (bin = bndstrt; bin < bndend; bin++) {
        if (bndpsd[bin] < dbknee)
            excite[bin] += (dbknee - bndpsd[bin]) >> 2;
        mask[bin] = max(excite[bin], hth[bin][fscod]);
    }
    if (dch >= 0 && deltbae[dch] < 2) {
        int seg, band = 0;
        for (seg = 0; seg < deltnseg[dch] + 1; seg++) {
            int delta, n;
            band += deltoffst[dch][seg];
            delta = deltba[dch][seg] >= 4 ? (deltba[dch][seg] - 3) << 7 : (deltba[dch][seg] - 4) << 7;
            for (n = 0; n < deltlen[dch][seg] && band < 50; n++)
                mask[band++] += delta;
        }
    }
    i = start;
    j = masktab[start];
    do {
        int m;
        lastbin = band_start[j + 1] < end ? band_start[j + 1] : end;
        m = mask[j] - snroffset - floor;
        if (m < 0)
            m = 0;
        m = (m & 0x1fe0) + floor;
        for (; i < lastbin; i++) {
            int a = (psd[i] - m) >> 5;
            bap[ch][i] = baptab[a < 0 ? 0 : a > 63 ? 63 : a];
        }
        j++;
    } while (end > lastbin);
}

/* ---- exponents ---- */

static int exponents(int ch, int strategy, int absexp, int ngrps, int first)
{
    int grpsize = strategy == 3 ? 4 : strategy, i, k, prev = absexp, bin = first;
    if (bin < 256 && ch != CPL)
        exps[ch][bin++] = (u8)absexp;
    for (i = 0; i < ngrps; i++) {
        int g = (int)bits(7), d[3], m;
        if (g > 124)
            return -1;
        d[0] = g / 25 - 2;
        d[1] = (g % 25) / 5 - 2;
        d[2] = g % 5 - 2;
        for (m = 0; m < 3; m++) {
            prev += d[m];
            if (prev < 0 || prev > 24)
                return -1;
            for (k = 0; k < grpsize && bin < 256; k++)
                exps[ch][bin++] = (u8)prev;
        }
    }
    return 0;
}

/* ---- mantissas ---- */

static float g1[3], g2[3], g4[2];
static int g1n, g2n, g4n;

static float mantissa(int b)
{
    int v;
    switch (b) {
    case 1:
        if (!g1n) {
            v = (int)bits(5);
            if (v > 26)
                v = 13;
            g1[0] = (float)(v / 9 - 1) * (2.0f / 3);
            g1[1] = (float)((v % 9) / 3 - 1) * (2.0f / 3);
            g1[2] = (float)(v % 3 - 1) * (2.0f / 3);
            g1n = 3;
        }
        return g1[3 - g1n--];
    case 2:
        if (!g2n) {
            v = (int)bits(7);
            if (v > 124)
                v = 62;
            g2[0] = (float)(v / 25 - 2) * 0.4f;
            g2[1] = (float)((v % 25) / 5 - 2) * 0.4f;
            g2[2] = (float)(v % 5 - 2) * 0.4f;
            g2n = 3;
        }
        return g2[3 - g2n--];
    case 3:
        v = (int)bits(3);
        return v == 7 ? 0 : (float)(2 * v - 6) / 7;
    case 4:
        if (!g4n) {
            v = (int)bits(7);
            if (v > 120)
                v = 60;
            g4[0] = (float)(2 * (v / 11) - 10) / 11;
            g4[1] = (float)(2 * (v % 11) - 10) / 11;
            g4n = 2;
        }
        return g4[2 - g4n--];
    case 5:
        v = (int)bits(4);
        return v == 15 ? 0 : (float)(2 * v - 14) / 15;
    default: {
        int q = qbits[b];
        v = (int)bits(q);
        if (v & (1 << (q - 1)))
            v -= 1 << q;
        return (float)v * pow2m[q - 1];
    }
    }
}

static float dither(void)
{
    rnd = rnd * 1664525u + 1013904223u;
    return (float)(s32)rnd * (0.707f / 2147483648.0f);
}

/* The mantissas of channel ch from bin a to b, scaled by their
 * exponents.  dith: what bap 0 gets. */
static void mantissas(int ch, int a, int b, int dith)
{
    int i;
    for (i = a; i < b; i++) {
        int bb = bap[ch][i];
        float m = bb ? mantissa(bb) : dith ? dither() : 0;
        coef[ch][i] = m * pow2m[exps[ch][i]];
    }
}

/* ---- the inverse transform (A/52 7.9.4) ---- */

static void fft(float *re, float *im, int n)
{
    const u8 *rev = n == 128 ? bitrev128 : bitrev64;
    int i, size, step = 128 / n;
    for (i = 0; i < n; i++) {
        int j = rev[i];
        if (j > i) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (size = 2; size <= n; size <<= 1) {
        int half = size >> 1, tstep = (128 / size) * 1, k;
        (void)step;
        for (k = 0; k < half; k++) {
            /* e^{+j 2 pi k / size}: the inverse transform. */
            float c = fft_c[k * tstep], s = fft_s[k * tstep];
            for (i = k; i < n; i += size) {
                int j = i + half;
                float tr = re[j] * c - im[j] * s, ti = re[j] * s + im[j] * c;
                re[j] = re[i] - tr;
                im[j] = im[i] - ti;
                re[i] += tr;
                im[i] += ti;
            }
        }
    }
}

/* Coefficients X[256] of a channel to 256 samples out (added into
 * out, times gain), with the delay of the channel. */
static void imdct(float *x_in, float *dl, int shortblocks, float *out, float gain)
{
    float zr[128], zi[128], x[512];
    int k, n;
    const int N = 512;
    if (!shortblocks) {
        for (k = 0; k < 128; k++) {
            float a = x_in[N / 2 - 2 * k - 1], b = x_in[2 * k];
            zr[k] = a * xcos1[k] - b * xsin1[k];
            zi[k] = a * xsin1[k] + b * xcos1[k];
        }
        fft(zr, zi, 128);
        for (n = 0; n < 128; n++) {
            float a = zr[n], b = zi[n];
            zr[n] = a * xcos1[n] - b * xsin1[n];
            zi[n] = a * xsin1[n] + b * xcos1[n];
        }
        for (n = 0; n < N / 8; n++) {
            x[2 * n] = -zi[N / 8 + n] * window[2 * n];
            x[2 * n + 1] = zr[N / 8 - n - 1] * window[2 * n + 1];
            x[N / 4 + 2 * n] = -zr[n] * window[N / 4 + 2 * n];
            x[N / 4 + 2 * n + 1] = zi[N / 4 - n - 1] * window[N / 4 + 2 * n + 1];
            x[N / 2 + 2 * n] = -zr[N / 8 + n] * window[N / 2 - 2 * n - 1];
            x[N / 2 + 2 * n + 1] = zi[N / 8 - n - 1] * window[N / 2 - 2 * n - 2];
            x[3 * N / 4 + 2 * n] = zi[n] * window[N / 4 - 2 * n - 1];
            x[3 * N / 4 + 2 * n + 1] = -zr[N / 4 - n - 1] * window[N / 4 - 2 * n - 2];
        }
    } else {
        float z2r[64], z2i[64];
        for (k = 0; k < 64; k++) {
            float a = x_in[2 * (N / 4 - 2 * k - 1)], b = x_in[2 * (2 * k)];
            zr[k] = a * xcos2[k] - b * xsin2[k];
            zi[k] = a * xsin2[k] + b * xcos2[k];
            a = x_in[2 * (N / 4 - 2 * k - 1) + 1];
            b = x_in[2 * (2 * k) + 1];
            z2r[k] = a * xcos2[k] - b * xsin2[k];
            z2i[k] = a * xsin2[k] + b * xcos2[k];
        }
        fft(zr, zi, 64);
        fft(z2r, z2i, 64);
        for (n = 0; n < 64; n++) {
            float a = zr[n], b = zi[n];
            zr[n] = a * xcos2[n] - b * xsin2[n];
            zi[n] = a * xsin2[n] + b * xcos2[n];
            a = z2r[n];
            b = z2i[n];
            z2r[n] = a * xcos2[n] - b * xsin2[n];
            z2i[n] = a * xsin2[n] + b * xcos2[n];
        }
        for (n = 0; n < N / 8; n++) {
            x[2 * n] = -zi[n] * window[2 * n];
            x[2 * n + 1] = zr[N / 8 - n - 1] * window[2 * n + 1];
            x[N / 4 + 2 * n] = -zr[n] * window[N / 4 + 2 * n];
            x[N / 4 + 2 * n + 1] = zi[N / 8 - n - 1] * window[N / 4 + 2 * n + 1];
            x[N / 2 + 2 * n] = -z2r[n] * window[N / 2 - 2 * n - 1];
            x[N / 2 + 2 * n + 1] = z2i[N / 8 - n - 1] * window[N / 2 - 2 * n - 2];
            x[3 * N / 4 + 2 * n] = z2i[n] * window[N / 4 - 2 * n - 1];
            x[3 * N / 4 + 2 * n + 1] = -z2r[N / 8 - n - 1] * window[N / 4 - 2 * n - 2];
        }
    }
    for (n = 0; n < 256; n++) {
        out[n] += gain * 2 * (x[n] + dl[n]);
        dl[n] = x[N / 2 + n];
    }
}

/* ---- a frame ---- */

static int audio_block(int blk, float *left, float *right)
{
    int ch, bnd, i, got_cpl = 0, cend[5];
    int cplexpstr = 0, chexpstr[5] = { 0 }, lfeexpstr = 0;
    float lgain[5], rgain[5];

    for (ch = 0; ch < nfchans; ch++)
        blksw[ch] = (int)bits(1);
    for (ch = 0; ch < nfchans; ch++)
        dithflag[ch] = (int)bits(1);
    if (bits(1)) {
        int v = (s8)bits(8);
        dynrng = (float)((v & 31) | 32) / 32 * ((v >> 5) >= 0 ? (float)(1 << (v >> 5)) : pow2m[-(v >> 5)]);
    } else if (blk == 0) {
        dynrng = 1;
    }
    if (acmod == 0) {
        if (bits(1)) {
            int v = (s8)bits(8);
            dynrng2 = (float)((v & 31) | 32) / 32 * ((v >> 5) >= 0 ? (float)(1 << (v >> 5)) : pow2m[-(v >> 5)]);
        } else if (blk == 0) {
            dynrng2 = 1;
        }
    }
    /* Coupling strategy. */
    if (bits(1)) {
        cplinu = (int)bits(1);
        if (cplinu) {
            for (ch = 0; ch < nfchans; ch++)
                chincpl[ch] = (int)bits(1);
            if (acmod == 2)
                phsflginu = (int)bits(1);
            cplbegf = (int)bits(4);
            cplendf = (int)bits(4);
            ncplsubnd = 3 + cplendf - cplbegf;
            if (ncplsubnd < 1)
                return -1;
            ncplbnd = ncplsubnd;
            cplbndstrc[0] = 0;
            for (bnd = 1; bnd < ncplsubnd; bnd++) {
                cplbndstrc[bnd] = (int)bits(1);
                ncplbnd -= cplbndstrc[bnd];
            }
            strtmant[CPL] = 37 + 12 * cplbegf;
            endmant[CPL] = 37 + 12 * (cplendf + 3);
        } else {
            for (ch = 0; ch < nfchans; ch++)
                chincpl[ch] = 0;
        }
    } else if (blk == 0) {
        return -1;
    }
    if (cplinu) {
        int any = 0;
        for (ch = 0; ch < nfchans; ch++) {
            if (!chincpl[ch])
                continue;
            if (bits(1)) {
                int mstr = (int)bits(2);
                any = 1;
                for (bnd = 0; bnd < ncplbnd; bnd++) {
                    int e = (int)bits(4), m = (int)bits(4);
                    float c = e == 15 ? (float)m / 16 : (float)(m + 16) / 32;
                    cplco[ch][bnd] = c * pow2m[e + 3 * mstr];
                }
            } else if (blk == 0) {
                return -1;
            }
        }
        if (acmod == 2 && phsflginu && any)
            for (bnd = 0; bnd < ncplbnd; bnd++)
                phsflg[bnd] = (int)bits(1);
        else if (!(acmod == 2 && phsflginu))
            for (bnd = 0; bnd < ncplbnd; bnd++)
                phsflg[bnd] = 0;
    }
    if (acmod == 2) {
        if (bits(1)) {
            int n = !cplinu || cplbegf > 2 ? 4 : cplbegf > 0 ? 3 : 2;
            for (i = 0; i < 4; i++)
                rematflg[i] = i < n ? (int)bits(1) : 0;
        } else if (blk == 0) {
            for (i = 0; i < 4; i++)
                rematflg[i] = 0;
        }
    }
    if (cplinu)
        cplexpstr = (int)bits(2);
    for (ch = 0; ch < nfchans; ch++)
        chexpstr[ch] = (int)bits(2);
    if (lfeon)
        lfeexpstr = (int)bits(1);
    for (ch = 0; ch < nfchans; ch++) {
        if (chexpstr[ch] && !chincpl[ch]) {
            chbwcod[ch] = (int)bits(6);
            if (chbwcod[ch] > 60)
                return -1;
        }
    }
    if (cplinu && cplexpstr) {
        int absexp = (int)bits(4) << 1;
        int n = (endmant[CPL] - strtmant[CPL]) / (3 << (cplexpstr - 1));
        if (exponents(CPL, cplexpstr, absexp, n, strtmant[CPL]))
            return -1;
    }
    for (ch = 0; ch < nfchans; ch++) {
        strtmant[ch] = 0;
        if (chexpstr[ch]) {
            int n, absexp;
            endmant[ch] = chincpl[ch] ? 37 + 12 * cplbegf : 37 + 3 * (chbwcod[ch] + 12);
            n = chexpstr[ch] == 1 ? (endmant[ch] - 1) / 3 : chexpstr[ch] == 2 ? (endmant[ch] - 1 + 3) / 6 : (endmant[ch] - 1 + 9) / 12;
            absexp = (int)bits(4);
            if (exponents(ch, chexpstr[ch], absexp, n, 0))
                return -1;
            bits(2); /* gainrng */
        } else if (blk == 0) {
            return -1;
        }
    }
    if (lfeon && lfeexpstr) {
        int absexp = (int)bits(4);
        strtmant[LFE] = 0;
        endmant[LFE] = 7;
        if (exponents(LFE, 1, absexp, 2, 0))
            return -1;
    }
    if (bits(1)) {
        sdcycod = (int)bits(2);
        fdcycod = (int)bits(2);
        sgaincod = (int)bits(2);
        dbpbcod = (int)bits(2);
        floorcod = (int)bits(3);
    } else if (blk == 0) {
        return -1;
    }
    if (bits(1)) {
        csnroffst = (int)bits(6);
        if (cplinu) {
            fsnroffst[CPL] = (int)bits(4);
            fgaincod[CPL] = (int)bits(3);
        }
        for (ch = 0; ch < nfchans; ch++) {
            fsnroffst[ch] = (int)bits(4);
            fgaincod[ch] = (int)bits(3);
        }
        if (lfeon) {
            fsnroffst[LFE] = (int)bits(4);
            fgaincod[LFE] = (int)bits(3);
        }
    } else if (blk == 0) {
        return -1;
    }
    if (cplinu && bits(1)) {
        cplfleak = (int)bits(3);
        cplsleak = (int)bits(3);
    }
    if (blk == 0)
        for (ch = 0; ch < 6; ch++)
            deltbae[ch] = 2;
    if (bits(1)) {
        if (cplinu)
            deltbae[5] = (int)bits(2);
        for (ch = 0; ch < nfchans; ch++)
            deltbae[ch] = (int)bits(2);
        for (ch = 0; ch < 6; ch++) {
            int seg;
            if ((ch == 5 && !cplinu) || (ch < 5 && ch >= nfchans) || deltbae[ch] != 1)
                continue;
            deltnseg[ch] = (int)bits(3);
            for (seg = 0; seg <= deltnseg[ch]; seg++) {
                deltoffst[ch][seg] = (u8)bits(5);
                deltlen[ch][seg] = (u8)bits(4);
                deltba[ch][seg] = (u8)bits(3);
            }
        }
    }
    if (bits(1)) {
        int skipl = (int)bits(9);
        apos += 8 * (u32)skipl;
    }
    if (apos >= aend)
        return -1;

    /* Bit allocation, everything again: cheap enough. */
    if (cplinu)
        allocate(CPL, 5, (cplfleak << 8) + 768, (cplsleak << 8) + 768);
    for (ch = 0; ch < nfchans; ch++)
        allocate(ch, ch, 0, 0);
    if (lfeon)
        allocate(LFE, -1, 0, 0);

    /* Mantissas. */
    g1n = g2n = g4n = 0;
    for (ch = 0; ch < nfchans; ch++) {
        mantissas(ch, 0, endmant[ch], dithflag[ch]);
        if (cplinu && chincpl[ch] && !got_cpl) {
            mantissas(CPL, strtmant[CPL], endmant[CPL], 0);
            got_cpl = 1;
        }
    }
    if (lfeon)
        mantissas(LFE, 0, 7, 0);
    if (apos > aend)
        return -1;

    /* Decoupling. */
    if (cplinu) {
        for (ch = 0; ch < nfchans; ch++) {
            int sb, b = 0, bin = strtmant[CPL];
            if (!chincpl[ch])
                continue;
            for (sb = 0; sb < ncplsubnd; sb++) {
                float c;
                if (sb && !cplbndstrc[sb])
                    b++;
                c = cplco[ch][b] * 8;
                if (acmod == 2 && ch == 1 && phsflg[b])
                    c = -c;
                for (i = 0; i < 12; i++, bin++) {
                    float m = coef[CPL][bin];
                    if (bap[CPL][bin] == 0 && dithflag[ch])
                        m = dither() * pow2m[exps[CPL][bin]];
                    coef[ch][bin] = m * c;
                }
            }
        }
    }
    for (ch = 0; ch < nfchans; ch++)
        cend[ch] = cplinu && chincpl[ch] ? endmant[CPL] : endmant[ch];
    /* Rematrixing. */
    if (acmod == 2) {
        int end = cplinu ? strtmant[CPL] : cend[0] < cend[1] ? cend[0] : cend[1];
        for (bnd = 0; bnd < 4; bnd++) {
            int a = remat_band[bnd], b = remat_band[bnd + 1];
            if (!rematflg[bnd])
                continue;
            if (b > end)
                b = end;
            for (i = a; i < b; i++) {
                float l = coef[0][i], r = coef[1][i];
                coef[0][i] = l + r;
                coef[1][i] = l - r;
            }
        }
    }
    for (ch = 0; ch < nfchans; ch++)
        for (i = cend[ch]; i < 256; i++)
            coef[ch][i] = 0;

    /* Down to two channels, each channel through its own transform
     * (the channels may switch block sizes on their own). */
    switch (acmod) {
    case 0: /* 1+1: the first */
        lgain[0] = rgain[0] = 1;
        lgain[1] = rgain[1] = 0;
        break;
    case 1:
        lgain[0] = rgain[0] = 0.7071f;
        break;
    case 2:
        lgain[0] = 1, rgain[0] = 0;
        lgain[1] = 0, rgain[1] = 1;
        break;
    case 3:
        lgain[0] = 1, rgain[0] = 0;
        lgain[1] = rgain[1] = clev;
        lgain[2] = 0, rgain[2] = 1;
        break;
    case 4:
        lgain[0] = 1, rgain[0] = 0;
        lgain[1] = 0, rgain[1] = 1;
        lgain[2] = rgain[2] = slev * 0.7071f;
        break;
    case 5:
        lgain[0] = 1, rgain[0] = 0;
        lgain[1] = rgain[1] = clev;
        lgain[2] = 0, rgain[2] = 1;
        lgain[3] = rgain[3] = slev * 0.7071f;
        break;
    case 6:
        lgain[0] = 1, rgain[0] = 0;
        lgain[1] = 0, rgain[1] = 1;
        lgain[2] = slev, rgain[2] = 0;
        lgain[3] = 0, rgain[3] = slev;
        break;
    default:
        lgain[0] = 1, rgain[0] = 0;
        lgain[1] = rgain[1] = clev;
        lgain[2] = 0, rgain[2] = 1;
        lgain[3] = slev, rgain[3] = 0;
        lgain[4] = 0, rgain[4] = slev;
        break;
    }
    {
        /* Scaled so that the mix cannot clip: as much as the left
         * output takes in at most. */
        float sum = 0, scale;
        for (ch = 0; ch < nfchans; ch++)
            sum += lgain[ch];
        scale = sum > 1 ? 1 / sum : 1;
        for (i = 0; i < 256; i++)
            left[i] = right[i] = 0;
        for (ch = 0; ch < nfchans; ch++) {
            float t[256], g = (acmod == 0 && ch == 1) ? dynrng2 : dynrng;
            if (lgain[ch] == 0 && rgain[ch] == 0) {
                /* (Kept in step all the same.) */
                for (i = 0; i < 256; i++)
                    t[i] = 0;
                imdct(coef[ch], delay[ch], blksw[ch], t, 0);
                continue;
            }
            for (i = 0; i < 256; i++)
                t[i] = 0;
            imdct(coef[ch], delay[ch], blksw[ch], t, g * scale);
            for (i = 0; i < 256; i++) {
                left[i] += lgain[ch] * t[i];
                right[i] += rgain[ch] * t[i];
            }
        }
    }
    return 0;
}

int ac3_decode(const u8 *frame, int len, s16 *out, int *rate)
{
    static const float cmix[4] = { 0.7071f, 0.5946f, 0.5f, 0.5946f };
    static const float smix[4] = { 0.7071f, 0.5f, 0, 0.5f };
    int size = ac3_frame_size(frame), blk, i;
    if (!size || size > len)
        return -1;
    ab = frame;
    apos = 32;
    aend = 8 * (u32)size;
    fscod = (int)bits(2);
    bits(6);
    if (bits(5) > 10)
        return -1;
    bits(3);
    acmod = (int)bits(3);
    nfchans = nfchans_of[acmod];
    clev = 0.7071f;
    slev = 0.7071f;
    if ((acmod & 1) && acmod != 1)
        clev = cmix[bits(2)];
    if (acmod & 4)
        slev = smix[bits(2)];
    if (acmod == 2)
        bits(2);
    lfeon = (int)bits(1);
    bits(5);
    if (bits(1))
        bits(8);
    if (bits(1))
        bits(8);
    if (bits(1))
        bits(7);
    if (acmod == 0) {
        bits(5);
        if (bits(1))
            bits(8);
        if (bits(1))
            bits(8);
        if (bits(1))
            bits(7);
    }
    bits(2);
    if (bits(1))
        bits(14);
    if (bits(1))
        bits(14);
    if (bits(1)) {
        int n = (int)bits(6) + 1;
        apos += 8 * (u32)n;
    }
    *rate = fscod == 0 ? 48000 : fscod == 1 ? 44100 : 32000;
    for (blk = 0; blk < 6; blk++) {
        float l[256], r[256];
        if (audio_block(blk, l, r)) {
            /* The rest of the frame silent. */
            for (i = blk * 256 * 2; i < 1536 * 2; i++)
                out[i] = 0;
            return 1536;
        }
        for (i = 0; i < 256; i++) {
            float a = l[i] * 32767, b = r[i] * 32767;
            s32 x = (s32)(a < 0 ? a - 0.5f : a + 0.5f), y = (s32)(b < 0 ? b - 0.5f : b + 0.5f);
            out[2 * (blk * 256 + i)] = (s16)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            out[2 * (blk * 256 + i) + 1] = (s16)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
        }
    }
    return 1536;
}

static float bessel_i0(float x)
{
    float sum = 1, term = 1, h = x / 2;
    int k;
    for (k = 1; k < 60; k++) {
        term *= h / (float)k;
        sum += term * term;
    }
    return sum;
}

void ac3_init(void)
{
    int i, k, n, b;
    float sum = 0, total = 0;
    for (b = 0; b < 50; b++)
        for (i = band_start[b]; i < band_start[b + 1]; i++)
            masktab[i] = (u8)b;
    for (i = 253; i < 256; i++)
        masktab[i] = 49;
    /* The Kaiser-Bessel derived window, alpha 5. */
    for (i = 0; i <= 256; i++) {
        float t = (float)i / 128 - 1;
        total += bessel_i0(PI * 5 * root(1 - t * t));
    }
    for (i = 0; i < 256; i++) {
        float t = (float)i / 128 - 1;
        sum += bessel_i0(PI * 5 * root(1 - t * t));
        window[i] = root(sum / total);
    }
    for (k = 0; k < 128; k++) {
        xcos1[k] = -cosine(2 * PI * (float)(8 * k + 1) / 4096);
        xsin1[k] = -sine(2 * PI * (float)(8 * k + 1) / 4096);
    }
    for (k = 0; k < 64; k++) {
        xcos2[k] = -cosine(2 * PI * (float)(8 * k + 1) / 2048);
        xsin2[k] = -sine(2 * PI * (float)(8 * k + 1) / 2048);
        fft_c[k] = cosine(2 * PI * (float)k / 128);
        fft_s[k] = sine(2 * PI * (float)k / 128);
    }
    for (n = 0; n < 128; n++) {
        int r = 0;
        for (i = 0; i < 7; i++)
            if (n & (1 << i))
                r |= 64 >> i;
        bitrev128[n] = (u8)r;
    }
    for (n = 0; n < 64; n++) {
        int r = 0;
        for (i = 0; i < 6; i++)
            if (n & (1 << i))
                r |= 32 >> i;
        bitrev64[n] = (u8)r;
    }
    pow2m[0] = 1;
    for (i = 1; i < 32; i++)
        pow2m[i] = pow2m[i - 1] / 2;
    if (!coef) {
        coef = dvd_alloc(7 * 256 * sizeof(float));
        delay = dvd_alloc(6 * 256 * sizeof(float));
        exps = dvd_alloc(7 * 256);
        bap = dvd_alloc(7 * 256);
    }
    memset(delay, 0, 6 * 256 * sizeof(float));
}
