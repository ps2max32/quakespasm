/* ps2_math.h: included before every source of the port (-include, after
   ps2_stdio.h). The R5900 has a single-precision FPU only: a double is a
   call into compiler-rt's soft float. Quake 3 is float code, but C makes it
   double where it calls the math.h functions (sin, cos, sqrt, atan2, ...
   take and return double); picolibc's float versions (sinf, acosf, ...)
   use doubles inside too. Here the math.h names go to float versions:
   sqrt to the FPU's sqrt.s, the trigonometric ones to short polynomials
   (errors below 1e-6 for sin/cos, 1e-5 radians for atan2), plenty for a
   game. The other source of doubles, unsuffixed literals (0.5), is made
   float by -cl-single-precision-constant (Makefile). */

#ifndef PS2_MATH_H
#define PS2_MATH_H

#include <math.h>

#define PS2_PI 3.14159265358979f

/* sin and cos of x brought into [-pi/2, pi/2] around a multiple of pi */
static inline float ps2_sinf(float x) {
  float q = x * (1.0f / PS2_PI);
  float n = q >= 0 ? (float)(int)(q + 0.5f) : (float)(int)(q - 0.5f);
  float r = x - n * PS2_PI; /* [-pi/2, pi/2] */
  float r2 = r * r;
  /* minimax, odd terms to r^9 */
  float s = r * (1.0f + r2 * (-1.6666667e-1f + r2 * (8.3333310e-3f +
                 r2 * (-1.9840874e-4f + r2 * 2.7525562e-6f))));
  return ((int)n & 1) ? -s : s;
}

static inline float ps2_cosf(float x) { return ps2_sinf(x + 0.5f * PS2_PI); }

static inline float ps2_tanf(float x) { return ps2_sinf(x) / ps2_cosf(x); }

/* atan of t in [-1, 1] (minimax to t^11) */
static inline float ps2_atan1(float t) {
  float t2 = t * t;
  return t * (0.99997726f + t2 * (-0.33262347f + t2 * (0.19354346f +
              t2 * (-0.11643287f + t2 * (0.05265332f + t2 * -0.01172120f)))));
}

static inline float ps2_atan2f(float y, float x) {
  float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  if (ax == 0 && ay == 0)
    return 0;
  float a = ay <= ax ? ps2_atan1(ay / ax) : 0.5f * PS2_PI - ps2_atan1(ax / ay);
  if (x < 0)
    a = PS2_PI - a;
  return y < 0 ? -a : a;
}

static inline float ps2_atanf(float x) { return ps2_atan2f(x, 1.0f); }

static inline float ps2_asinf(float x) {
  float c = 1.0f - x * x;
  return ps2_atan2f(x, __builtin_sqrtf(c > 0 ? c : 0));
}

static inline float ps2_acosf(float x) {
  float s = 1.0f - x * x;
  return ps2_atan2f(__builtin_sqrtf(s > 0 ? s : 0), x);
}

#define sin(x) ps2_sinf(x)
#define cos(x) ps2_cosf(x)
#define tan(x) ps2_tanf(x)
#define atan(x) ps2_atanf(x)
#define atan2(y, x) ps2_atan2f((y), (x))
#define asin(x) ps2_asinf(x)
#define acos(x) ps2_acosf(x)
#define sinf(x) ps2_sinf(x)
#define cosf(x) ps2_cosf(x)
#define tanf(x) ps2_tanf(x)
#define atanf(x) ps2_atanf(x)
#define atan2f(y, x) ps2_atan2f((y), (x))
#define asinf(x) ps2_asinf(x)
#define acosf(x) ps2_acosf(x)
#define sqrt(x) __builtin_sqrtf(x)
#define fabs(x) __builtin_fabsf(x)
#define floor(x) floorf(x)
#define ceil(x) ceilf(x)
#define pow(x, y) powf((x), (y))
#undef M_PI
#define M_PI 3.14159265358979323846f

#endif
