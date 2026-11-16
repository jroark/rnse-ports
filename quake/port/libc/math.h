/* The mathematics Quake uses (quake/port/libc/libm.c), in single
 * precision inside whatever the type says: the game wants no more, and
 * the SH-4 does single precision fastest.  sqrt and fabs are the FPU's
 * own instructions. */
#ifndef RNSE_QUAKE_MATH_H
#define RNSE_QUAKE_MATH_H

#define M_PI 3.14159265358979323846
#define HUGE_VAL __builtin_huge_val()

double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double floor(double x);
double ceil(double x);
double log(double x);
double exp(double x);
double pow(double x, double y);
double fmod(double x, double y);
float sinf(float x);
float cosf(float x);
float atan2f(float y, float x);
float floorf(float x);
float ceilf(float x);

#define sqrt(x) __builtin_sqrt(x)
#define sqrtf(x) __builtin_sqrtf(x)
#define fabs(x) __builtin_fabs(x)
#define fabsf(x) __builtin_fabsf(x)

double atof(const char *s);

#endif
