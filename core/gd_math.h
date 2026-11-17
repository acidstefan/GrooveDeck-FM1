/* GrooveDeck FM-1 - schnelle Naeherungen fuer die Audio-Engine.
   Auf dem FM-1 (pi32v2, Single-Precision-FPU) sind exp/pow/tanh/sin aus der libm teuer;
   die Engine nutzt deshalb diese Funktionen. Genauigkeit reicht fuer Audio (Fehler < 0,1 %). */
#ifndef GD_MATH_H
#define GD_MATH_H
#include <stdint.h>

#define GD_PI 3.14159265358979f
#define GD_TWO_PI 6.28318530718f

static inline float gd_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int gd_clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float gd_minf(float a, float b) { return a < b ? a : b; }
static inline float gd_maxf(float a, float b) { return a > b ? a : b; }
static inline float gd_absf(float x) { return x < 0.0f ? -x : x; }
static inline int gd_roundi(float x) { return x >= 0.0f ? (int)(x + 0.5f) : -(int)(0.5f - x); }
static inline float gd_floorf(float x) { int i = (int)x; return (float)(x < (float)i ? i - 1 : i); }
/* Ganzzahliges floor/ceil ueber Abschneiden: (long)floorf() erzeugt sonst einen FPU-Rundungsbefehl
   (E53F x2F/x3F), den der fm1-emulator nicht kennt. */
static inline long gd_floorl(float x) { long i = (long)x; return x < (float)i ? i - 1 : i; }
static inline long gd_ceill(float x) { long i = (long)x; return x > (float)i ? i + 1 : i; }

/* 2^x: Rundung auf die naechste ganze Zahl, Rest f in [-0.5, 0.5] per Taylorreihe (Grad 6, Fehler < 2e-7).
   Exakt 1 bei x = 0 - wichtig fuer Abklingkoeffizienten nahe 1. */
static inline float gd_exp2(float x)
{
    union { float f; int32_t i; } u;
    float n, t;
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    n = gd_floorf(x + 0.5f);
    t = (x - n) * 0.693147181f;
    u.f = 1.0f + t * (1.0f + t * (0.5f + t * (0.166666667f + t * (0.0416666667f + t * (0.00833333333f + t * 0.00138888889f)))));
    u.i += (int32_t)n << 23;
    return u.f;
}

/* log2(x) fuer x > 0: Mantisse auf [0.707, 1.414), atanh-Reihe (Fehler < 1e-7, exakt 0 bei x = 1) */
static inline float gd_log2(float x)
{
    union { float f; int32_t i; } u;
    float m, e, t, t2;
    if (x <= 0.0f) return -126.0f;
    u.f = x;
    e = (float)(((u.i >> 23) & 255) - 127);
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;
    m = u.f; /* 1..2 */
    if (m > 1.41421356f) { m *= 0.5f; e += 1.0f; }
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    return e + 2.88539008f * t * (1.0f + t2 * (0.333333333f + t2 * (0.2f + t2 * (0.142857143f + t2 * 0.111111111f))));
}

static inline float gd_exp(float x) { return gd_exp2(x * 1.44269504f); }
static inline float gd_powf(float b, float e) { return gd_exp2(e * gd_log2(b)); }

/* tanh, rationale Naeherung (Pade 7/6), fuer |x| > 4,97 geklemmt */
static inline float gd_tanh(float x)
{
    float x2;
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}

/* sin(2*pi*ph) fuer ph in [0,1) */
static inline float gd_sin01(float ph)
{
    float x, x2;
    ph -= gd_floorf(ph);
    /* auf [-0.25, 0.25] falten */
    if (ph > 0.75f) ph -= 1.0f;
    else if (ph > 0.25f) ph = 0.5f - ph;
    x = ph * GD_TWO_PI;
    x2 = x * x;
    return x * (1.0f + x2 * (-0.166666667f + x2 * (0.00833333333f + x2 * (-0.000198412698f + x2 * 2.75573192e-6f))));
}
static inline float gd_cos01(float ph) { return gd_sin01(ph + 0.25f); }

/* tan(x) fuer 0 <= x < ~1.45 (Filterkoeffizienten, x = pi*fc/sr) */
static inline float gd_tan(float x)
{
    float s = gd_sin01(x / GD_TWO_PI), c = gd_cos01(x / GD_TWO_PI);
    return c > 1e-6f ? s / c : 1e6f;
}

static inline float gd_sqrt(float x)
{
    union { float f; int32_t i; } u;
    float y;
    if (x <= 0.0f) return 0.0f;
    u.f = x;
    u.i = 0x1FBD1DF5 + (u.i >> 1);
    y = u.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}

static inline float gd_db2lin(float db) { return gd_exp2(db * 0.166096405f); } /* 10^(db/20) */
static inline float gd_lin2db(float g) { return 6.02059991f * gd_log2(g); }

/* xorshift32, Rueckgabe -1..1 */
static inline float gd_rnd(uint32_t *x)
{
    uint32_t v = *x;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    *x = v;
    return (float)v * (2.0f / 4294967295.0f) - 1.0f;
}
#endif
