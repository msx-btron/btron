/*
 * B-System (BTRON 3.20) — include/rt/fix16.h
 * Clean-room Q16.16 Fixed-Point Real-Time Numeric Layer for B-Sound Forth (BSF)
 * Pure C99, zero heap allocation, adheres to NASA/JPL Power of Ten rules.
 *
 * Targets Cortex-M0/M0+ (no FPU), Cortex-M55 (Helium/FPU), and POSIX/BTRON hosts.
 * Pitch law: f(y) = 261.625565 * 2^y Hz (C4 at y = 0, 1.0 = 1 octave = 12 semitones).
 */

#ifndef _BTRON_RT_FIX16_H_
#define _BTRON_RT_FIX16_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t fix16_t;

#define FIX16_SHIFT      16
#define FIX16_ONE        ((fix16_t)65536)           /*  1.0       */
#define FIX16_HALF       ((fix16_t)32768)           /*  0.5       */
#define FIX16_ZERO       ((fix16_t)0)               /*  0.0       */
#define FIX16_PI         ((fix16_t)205887)          /*  3.1415926 */
#define FIX16_TWO_PI     ((fix16_t)411775)          /*  6.2831853 */
#define FIX16_HALF_PI    ((fix16_t)102944)          /*  1.5707963 */
#define FIX16_LN2        ((fix16_t)45426)           /*  0.6931472 */
#define FIX16_INV_LN2    ((fix16_t)94548)           /*  1.4426950 */
#define FIX16_C4_HZ      ((fix16_t)17145893)        /* 261.625565 */
#define FIX16_MAX        ((fix16_t)0x7FFFFFFF)
#define FIX16_MIN        ((fix16_t)(-0x7FFFFFFF))
#define FIX16_QUIET      ((fix16_t)0x80000000u)     /* NaN / silence sentinel */

static inline bool fix16_is_quiet(fix16_t x) {
    return x == FIX16_QUIET;
}

static inline fix16_t fix16_from_int(int32_t n) {
    return (fix16_t)(n << FIX16_SHIFT);
}

static inline int32_t fix16_to_int(fix16_t x) {
    return (int32_t)(x >> FIX16_SHIFT);
}

static inline fix16_t fix16_from_float(float f) {
    float scaled = f * 65536.0f;
    if (scaled >= 2147483520.0f) return FIX16_MAX;
    if (scaled <= -2147483520.0f) return FIX16_MIN;
    return (fix16_t)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
}

static inline float fix16_to_float(fix16_t x) {
    return (float)x * (1.0f / 65536.0f);
}

static inline fix16_t fix16_abs(fix16_t x) {
    if (x == FIX16_QUIET) return FIX16_QUIET;
    return (x < 0) ? -x : x;
}

static inline fix16_t fix16_min(fix16_t a, fix16_t b) {
    return (a < b) ? a : b;
}

static inline fix16_t fix16_max(fix16_t a, fix16_t b) {
    return (a > b) ? a : b;
}

static inline fix16_t fix16_clamp(fix16_t x, fix16_t lo, fix16_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline fix16_t fix16_mul(fix16_t a, fix16_t b) {
    int64_t p = ((int64_t)a * (int64_t)b) >> FIX16_SHIFT;
    if (p > (int64_t)FIX16_MAX) return FIX16_MAX;
    if (p < (int64_t)FIX16_MIN) return FIX16_MIN;
    return (fix16_t)p;
}

static inline fix16_t fix16_div(fix16_t a, fix16_t b) {
    if (b == 0) return (a >= 0) ? FIX16_MAX : FIX16_MIN;
    int64_t num = ((int64_t)a) << FIX16_SHIFT;
    int64_t q = num / (int64_t)b;
    if (q > (int64_t)FIX16_MAX) return FIX16_MAX;
    if (q < (int64_t)FIX16_MIN) return FIX16_MIN;
    return (fix16_t)q;
}

/* Floor in Q16.16 (returns Q16.16 integer multiple) */
static inline fix16_t fix16_floor(fix16_t x) {
    return (fix16_t)((uint32_t)x & 0xFFFF0000u);
}

/* Fractional part in [0, 1) */
static inline fix16_t fix16_frac(fix16_t x) {
    return (fix16_t)((uint32_t)x & 0x0000FFFFu);
}

/* Euclidean modulo in [0, b) for b > 0 */
static inline fix16_t fix16_mod(fix16_t a, fix16_t b) {
    if (b <= 0) return 0;
    int32_t r = a % b;
    if (r < 0) r += b;
    return (fix16_t)r;
}

/* Smoothstep F(u) = u^2 * (3 - 2u) for u in [0, 1] */
static inline fix16_t fix16_smoothstep(fix16_t u) {
    u = fix16_clamp(u, 0, FIX16_ONE);
    fix16_t u2 = fix16_mul(u, u);
    fix16_t t = (3 * FIX16_ONE) - (u << 1);
    return fix16_mul(u2, t);
}

/* ── Transcendentals & Synthesis Math (src/rt/common/scalar/fix16.c) ─ */

/* Phase-normalized sine/cosine: phase_norm in Q16.16 where 1.0 = 2*PI */
fix16_t fix16_sin_norm(fix16_t phase_norm);
fix16_t fix16_cos_norm(fix16_t phase_norm);

/* Radian sine/cosine */
fix16_t fix16_sin(fix16_t rad);
fix16_t fix16_cos(fix16_t rad);

/* Exponential, base-2 logarithm, and base-2 power (pitch law critical) */
fix16_t fix16_exp(fix16_t x);
fix16_t fix16_log2(fix16_t x);
fix16_t fix16_pow2(fix16_t x);

/* Soft saturation & B-Sound WaveShaper C_k(x) (Eq. 14) */
fix16_t fix16_tanh(fix16_t x);
fix16_t fix16_waveshaper(fix16_t x, fix16_t k);

/* Pitch law: f(y) = 261.625565 * 2^y Hz */
fix16_t fix16_y_to_hz(fix16_t y);
fix16_t fix16_hz_to_y(fix16_t hz);

/* Analytical helpers matching synth-engine.js */
int32_t fix16_bit(uint32_t mask, int32_t k);
int32_t fix16_digit(uint32_t num, int32_t idx_from_right);
int32_t fix16_diat(int32_t d);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_RT_FIX16_H_ */
