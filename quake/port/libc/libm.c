/* The mathematics Quake uses, for the RNS-E (math.h): computed in single
 * precision, which is all the game wants of them (angles, gamma, the
 * odd distance), whatever the declared type.  Polynomials over a
 * reduced range; good to a few units in the last place of a float. */
#include <ctype.h>
#include <math.h>

#define PI 3.14159265f
#define HALF_PI 1.57079633f

union bits {
    float f;
    unsigned u;
    int i;
};

/* x as k quarter turns and a rest within an eighth of one: the rest's
 * sine and cosine by their series, then put in the right quadrant. */
static void sincos_f(float x, float *s, float *c)
{
    /* pi/2 in two parts, so that k * pi/2 is taken off nearly exactly. */
    const float hi = 1.5703125f, lo = 4.83826794e-4f;
    float k = x * 0.636619772f, r, r2, sr, cr;
    int q;
    q = (int)(k >= 0 ? k + 0.5f : k - 0.5f);
    r = (x - (float)q * hi) - (float)q * lo;
    r2 = r * r;
    sr = r + r * r2 * (-1.66666667e-1f + r2 * (8.33333333e-3f + r2 * (-1.98412698e-4f + r2 * 2.75573192e-6f)));
    cr = 1.0f + r2 * (-0.5f + r2 * (4.16666667e-2f + r2 * (-1.38888889e-3f + r2 * 2.48015873e-5f)));
    switch (q & 3) {
    case 0: *s = sr; *c = cr; break;
    case 1: *s = cr; *c = -sr; break;
    case 2: *s = -sr; *c = -cr; break;
    default: *s = -cr; *c = sr; break;
    }
}

float sinf(float x)
{
    float s, c;
    sincos_f(x, &s, &c);
    return s;
}

float cosf(float x)
{
    float s, c;
    sincos_f(x, &s, &c);
    return c;
}

double sin(double x) { return sinf((float)x); }
double cos(double x) { return cosf((float)x); }

double tan(double x)
{
    float s, c;
    sincos_f((float)x, &s, &c);
    return s / c;
}

/* For |x| <= 1: by the angle of pi/6 when x is above tan(pi/12). */
static float atan_unit(float x)
{
    const float tan_pi_12 = 0.267949192f, root3 = 1.73205081f;
    float offset = 0, x2;
    int neg = x < 0;
    if (neg)
        x = -x;
    if (x > tan_pi_12) {
        x = (x * root3 - 1.0f) / (root3 + x);
        offset = PI / 6;
    }
    x2 = x * x;
    x = offset + x * (1.0f + x2 * (-3.33333333e-1f + x2 * (2.0e-1f + x2 * (-1.42857143e-1f + x2 * 1.11111111e-1f))));
    return neg ? -x : x;
}

static float atan_f(float x)
{
    if (x > 1.0f)
        return HALF_PI - atan_unit(1.0f / x);
    if (x < -1.0f)
        return -HALF_PI - atan_unit(1.0f / x);
    return atan_unit(x);
}

double atan(double x) { return atan_f((float)x); }

float atan2f(float y, float x)
{
    if (x > 0)
        return atan_f(y / x);
    if (x < 0)
        return y >= 0 ? atan_f(y / x) + PI : atan_f(y / x) - PI;
    if (y > 0)
        return HALF_PI;
    if (y < 0)
        return -HALF_PI;
    return 0;
}

double atan2(double y, double x) { return atan2f((float)y, (float)x); }

/* Above 2^23 a float has no fraction (and NaN goes through). */
float floorf(float x)
{
    float t;
    if (!(x > -8388608.0f && x < 8388608.0f))
        return x;
    t = (float)(int)x;
    return t > x ? t - 1.0f : t;
}

float ceilf(float x)
{
    float t;
    if (!(x > -8388608.0f && x < 8388608.0f))
        return x;
    t = (float)(int)x;
    return t < x ? t + 1.0f : t;
}

double floor(double x) { return floorf((float)x); }
double ceil(double x) { return ceilf((float)x); }

double fmod(double x, double y)
{
    float a = (float)x, b = (float)y, n;
    if (b == 0)
        return 0;
    n = a / b;
    n = n < 0 ? ceilf(n) : floorf(n);
    return a - n * b;
}

/* ln x = e ln 2 + ln m, m in [sqrt(1/2), sqrt(2)), by atanh's series. */
static float log_f(float x)
{
    union bits b;
    int e;
    float m, s, s2;
    if (!(x > 0))
        return x == 0 ? -__builtin_huge_valf() : (x - x) / (x - x);
    b.f = x;
    e = (int)((b.u >> 23) & 0xff) - 127;
    b.u = (b.u & 0x007fffff) | 0x3f800000;
    m = b.f;
    if (m > 1.41421356f) {
        m *= 0.5f;
        e++;
    }
    s = (m - 1.0f) / (m + 1.0f);
    s2 = s * s;
    return (float)e * 0.693147181f + 2.0f * s * (1.0f + s2 * (3.33333333e-1f + s2 * (2.0e-1f + s2 * (1.42857143e-1f + s2 * 1.11111111e-1f))));
}

/* e^x = 2^k e^r, |r| <= ln 2 / 2. */
static float exp_f(float x)
{
    union bits b;
    float kf, r;
    int k;
    if (x > 88.0f)
        return __builtin_huge_valf();
    if (x < -87.0f)
        return 0;
    kf = x * 1.44269504f;
    k = (int)(kf >= 0 ? kf + 0.5f : kf - 0.5f);
    r = x - (float)k * 0.693147181f;
    r = 1.0f + r * (1.0f + r * (0.5f + r * (1.66666667e-1f + r * (4.16666667e-2f + r * (8.33333333e-3f + r * 1.38888889e-3f)))));
    b.i = (k + 127) << 23;
    return r * b.f;
}

double log(double x) { return log_f((float)x); }
double exp(double x) { return exp_f((float)x); }

double pow(double x, double y)
{
    float a = (float)x, p = (float)y;
    if (p == 0)
        return 1;
    if (a == 0)
        return 0;
    if (a < 0) {
        /* Whole powers only, as C has it. */
        int n = (int)p;
        float v = exp_f(p * log_f(-a));
        return (float)n == p ? (n & 1 ? -v : v) : (a - a) / (a - a);
    }
    return exp_f(p * log_f(a));
}

double atof(const char *s)
{
    float v = 0, scale = 1;
    int neg = 0, e = 0, eneg = 0;
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    while (isdigit((unsigned char)*s))
        v = v * 10 + (float)(*s++ - '0');
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) {
            scale *= 0.1f;
            v += scale * (float)(*s++ - '0');
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-' || *s == '+')
            eneg = *s++ == '-';
        while (isdigit((unsigned char)*s))
            e = e * 10 + *s++ - '0';
        while (e--)
            v = eneg ? v * 0.1f : v * 10;
    }
    return neg ? -v : v;
}
