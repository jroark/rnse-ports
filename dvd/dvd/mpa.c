/* MPEG-1 audio layer II (ISO/IEC 11172-3), the other sound of DVDs
 * (PAL discs especially), at 32, 44.1 and 48 kHz, mono or two
 * channels.  Written from the standard; the synthesis window is its
 * table B.3 (scaled by 2^16, half of it: the rest follows by its
 * symmetry).  Floating point, single precision, like ac3.c.
 *
 * Not here: layers I and III, MPEG-2's lower sampling rates and its
 * multichannel extension (the two channels compatible with MPEG-1,
 * which such a stream begins with, are played). */
#include "dvd.h"

#define PI 3.14159265358979f

static const s32 enwindow[257] = {
    0, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -3,
    -3, -4, -4, -5, -5, -6, -7, -7, -8, -9, -10, -11,
    -13, -14, -16, -17, -19, -21, -24, -26, -29, -31, -35, -38,
    -41, -45, -49, -53, -58, -63, -68, -73, -79, -85, -91, -97,
    -104, -111, -117, -125, -132, -139, -147, -154, -161, -169, -176, -183,
    -190, -196, -202, -208, 213, 218, 222, 225, 227, 228, 228, 227,
    224, 221, 215, 208, 200, 189, 177, 163, 146, 127, 106, 83,
    57, 29, -2, -36, -72, -111, -153, -197, -244, -294, -347, -401,
    -459, -519, -581, -645, -711, -779, -848, -919, -991, -1064, -1137, -1210,
    -1283, -1356, -1428, -1498, -1567, -1634, -1698, -1759, -1817, -1870, -1919, -1962,
    -2001, -2032, -2057, -2075, -2085, -2087, -2080, -2063, 2037, 2000, 1952, 1893,
    1822, 1739, 1644, 1535, 1414, 1280, 1131, 970, 794, 605, 402, 185,
    -45, -288, -545, -814, -1095, -1388, -1692, -2006, -2330, -2663, -3004, -3351,
    -3705, -4063, -4425, -4788, -5153, -5517, -5879, -6237, -6589, -6935, -7271, -7597,
    -7910, -8209, -8491, -8755, -8998, -9219, -9416, -9585, -9727, -9838, -9916, -9959,
    -9966, -9935, -9863, -9750, -9592, -9389, -9139, -8840, -8492, -8092, -7640, -7134,
    6574, 5959, 5288, 4561, 3776, 2935, 2037, 1082, 70, -998, -2122, -3300,
    -4533, -5818, -7154, -8540, -9975, -11455, -12980, -14548, -16155, -17799, -19478, -21189,
    -22929, -24694, -26482, -28289, -30112, -31947, -33791, -35640, -37489, -39336, -41176, -43006,
    -44821, -46617, -48390, -50137, -51853, -53534, -55178, -56778, -58333, -59838, -61289, -62684,
    -64019, -65290, -66494, -67629, -68692, -69679, -70590, -71420, -72169, -72835, -73415, -73908,
    -74313, -74630, -74856, -74992, 75038,
};

static float *window;        /* [512] */
static float (*matrix)[32];  /* [64]: N[i][k] = cos((16 + i)(2k + 1) pi / 64) */
static float scalefactor[64];
static float (*v)[1024];     /* [2] */
static int voff[2];

static const u16 bitrates[15] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 };
static const u16 rates[3] = { 44100, 48000, 32000 };

/* Quantisation classes: levels, and bits a sample (grouped classes:
 * bits for three samples, negative). */
static const u16 levels[17] = { 0, 3, 5, 7, 9, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767 };
static const s8 class_bits[17] = { 0, -5, -7, 3, -10, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
#define L65535 17 /* 16 bits, 65535 levels */

/* The allocation tables: for each kind of subband, the bits of its
 * allocation and the class of each allocation (1..). */
static const u8 alloc_a0[16] = { 4, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, L65535 };
static const u8 alloc_a1[16] = { 4, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, L65535 };
static const u8 alloc_a2[8] = { 3, 1, 2, 3, 4, 5, 6, L65535 };
static const u8 alloc_a3[4] = { 2, 1, 2, L65535 };
static const u8 alloc_c0[16] = { 4, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const u8 alloc_c1[8] = { 3, 1, 2, 4, 5, 6, 7, 8 };

static const u8 *table_of(int table, int sb)
{
    switch (table) {
    case 0: /* 27 subbands */
    case 1: /* 30 */
        return sb < 3 ? alloc_a0 : sb < 11 ? alloc_a1 : sb < 23 ? alloc_a2 : alloc_a3;
    default: /* 8 or 12 */
        return sb < 2 ? alloc_c0 : alloc_c1;
    }
}

static float sine(float x)
{
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

static const u8 *mb;
static u32 mpos, mend;

static u32 bits(int n)
{
    u32 p = mpos >> 3, w;
    if (!n)
        return 0;
    if (mpos + (u32)n > mend) {
        mpos = mend;
        return 0;
    }
    w = (u32)mb[p] << 24 | (u32)mb[p + 1] << 16 | (u32)mb[p + 2] << 8 | mb[p + 3];
    w <<= mpos & 7;
    mpos += (u32)n;
    return w >> (32 - n);
}

int mpa_frame_size(const u8 *p)
{
    int br, fs;
    /* Sync, MPEG-1, layer II. */
    if (p[0] != 0xff || (p[1] & 0xfe) != 0xfc)
        return 0;
    br = p[2] >> 4;
    fs = (p[2] >> 2) & 3;
    if (br == 0 || br == 15 || fs == 3)
        return 0;
    return 144000 * bitrates[br] / rates[fs] + ((p[2] >> 1) & 1);
}

/* 32 subband samples of a channel to 32 samples out (ISO 11172-3,
 * the synthesis filterbank of figure A.2). */
static void synthesis(int ch, const float *s, s16 *out, int step)
{
    float *vv = v[ch];
    int i, j, k, o;
    voff[ch] = (voff[ch] - 64) & 1023;
    o = voff[ch];
    for (i = 0; i < 64; i++) {
        float sum = 0;
        for (k = 0; k < 32; k++)
            sum += matrix[i][k] * s[k];
        vv[(o + i) & 1023] = sum;
    }
    for (j = 0; j < 32; j++) {
        float sum = 0;
        for (i = 0; i < 8; i++) {
            sum += window[64 * i + j] * vv[(o + 128 * i + j) & 1023];
            sum += window[64 * i + 32 + j] * vv[(o + 128 * i + 96 + j) & 1023];
        }
        sum *= 32767;
        k = (int)(sum < 0 ? sum - 0.5f : sum + 0.5f);
        out[j * step] = (s16)(k > 32767 ? 32767 : k < -32768 ? -32768 : k);
    }
}

int mpa_decode(const u8 *frame, int len, s16 *out, int *rate, int *channels)
{
    int size = mpa_frame_size(frame), nch, mode, bound, table, sblimit, br, fs;
    int sb, ch, gr, i;
    u8 alloc[2][32], scfsi[2][32], scf[2][32][3];
    float s[2][3][32];
    if (!size || size > len)
        return -1;
    mb = frame;
    mpos = 32;
    mend = 8 * (u32)size;
    br = frame[2] >> 4;
    fs = (frame[2] >> 2) & 3;
    mode = frame[3] >> 6;
    nch = mode == 3 ? 1 : 2;
    bound = mode == 1 ? 4 * ((frame[3] >> 4) & 3) + 4 : 32;
    if (!(frame[1] & 1))
        mpos += 16; /* CRC */
    {
        int per = bitrates[br] / nch;
        if ((fs == 1 && per >= 56) || (per >= 56 && per <= 80))
            table = 0;
        else if (fs != 1 && per >= 96)
            table = 1;
        else if (fs != 2 && per <= 48)
            table = 2;
        else
            table = 3;
    }
    sblimit = table == 0 ? 27 : table == 1 ? 30 : table == 2 ? 8 : 12;
    if (bound > sblimit)
        bound = sblimit;
    for (sb = 0; sb < sblimit; sb++) {
        const u8 *t = table_of(table, sb);
        for (ch = 0; ch < nch; ch++) {
            if (sb >= bound && ch == 1) {
                alloc[1][sb] = alloc[0][sb];
                continue;
            }
            i = (int)bits(t[0]);
            alloc[ch][sb] = (u8)(i ? t[i] : 0);
        }
    }
    for (sb = 0; sb < sblimit; sb++)
        for (ch = 0; ch < nch; ch++)
            if (alloc[ch][sb])
                scfsi[ch][sb] = (u8)bits(2);
    for (sb = 0; sb < sblimit; sb++)
        for (ch = 0; ch < nch; ch++) {
            u8 *f = scf[ch][sb];
            if (!alloc[ch][sb])
                continue;
            switch (scfsi[ch][sb]) {
            case 0:
                f[0] = (u8)bits(6);
                f[1] = (u8)bits(6);
                f[2] = (u8)bits(6);
                break;
            case 1:
                f[0] = f[1] = (u8)bits(6);
                f[2] = (u8)bits(6);
                break;
            case 2:
                f[0] = f[1] = f[2] = (u8)bits(6);
                break;
            default:
                f[0] = (u8)bits(6);
                f[1] = f[2] = (u8)bits(6);
                break;
            }
        }
    for (gr = 0; gr < 12; gr++) {
        int part = gr >> 2;
        for (sb = 0; sb < 32; sb++)
            for (ch = 0; ch < 2; ch++)
                s[ch][0][sb] = s[ch][1][sb] = s[ch][2][sb] = 0;
        for (sb = 0; sb < sblimit; sb++) {
            for (ch = 0; ch < nch; ch++) {
                int c = alloc[ch][sb], n, b, k;
                int code[3];
                if (!c)
                    continue;
                if (sb >= bound && ch == 1) {
                    /* The samples are the first channel's, scaled by
                     * this channel's own factors. */
                    for (k = 0; k < 3; k++) {
                        float f0 = scalefactor[scf[0][sb][part]];
                        float f1 = scalefactor[scf[1][sb][part]];
                        s[1][k][sb] = f0 ? s[0][k][sb] / f0 * f1 : 0;
                    }
                    continue;
                }
                if (c == L65535) {
                    n = 65535;
                    b = 16;
                } else {
                    n = levels[c];
                    b = class_bits[c];
                }
                if (b < 0) {
                    int g = (int)bits(-b);
                    code[0] = g % n;
                    g /= n;
                    code[1] = g % n;
                    code[2] = g / n;
                } else {
                    code[0] = (int)bits(b);
                    code[1] = (int)bits(b);
                    code[2] = (int)bits(b);
                }
                for (k = 0; k < 3; k++) {
                    int cc = code[k] < n ? code[k] : n - 1;
                    s[ch][k][sb] = (float)(2 * cc - (n - 1)) / (float)n * scalefactor[scf[ch][sb][part]];
                }
            }
        }
        for (i = 0; i < 3; i++) {
            s16 *o = out + 2 * (32 * (3 * gr + i));
            int k;
            synthesis(0, s[0][i], o, 2);
            if (nch == 2)
                synthesis(1, s[1][i], o + 1, 2);
            else
                for (k = 0; k < 32; k++)
                    o[2 * k + 1] = o[2 * k];
        }
    }
    *rate = rates[fs];
    *channels = nch;
    return 1152;
}

void mpa_init(void)
{
    int i, k;
    if (!window) {
        window = dvd_alloc(512 * sizeof(float));
        matrix = dvd_alloc(64 * 32 * sizeof(float));
        v = dvd_alloc(2 * 1024 * sizeof(float));
    }
    for (i = 0; i < 257; i++) {
        float w = (float)enwindow[i] / 65536;
        window[i] = w;
        if (i & 63)
            w = -w;
        if (i)
            window[512 - i] = w;
    }
    for (i = 0; i < 64; i++)
        for (k = 0; k < 32; k++)
            matrix[i][k] = sine((float)((16 + i) * (2 * k + 1) % 128) * PI / 64 + PI / 2);
    scalefactor[0] = 2;
    for (i = 1; i < 63; i++) {
        /* 2^(1 - i/3): each third a factor of 2^(1/3). */
        scalefactor[i] = scalefactor[i - 1] * 0.79370052598f;
    }
    scalefactor[63] = 0;
    memset(v, 0, 2 * 1024 * sizeof(float));
    voff[0] = voff[1] = 0;
}
