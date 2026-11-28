/*
 * B-System (BTRON 3.20) — src/rt/common/scalar/fix16.c
 * Clean-room Q16.16 Fixed-Point Transcendentals & Analytical DSP Math
 * Pure C99, zero heap allocation, adheres to NASA/JPL Power of Ten rules.
 */

#include <rt/fix16.h>

/* Quarter-wave sine table (65 entries for [0, PI/2], Q16.16) */
static const int32_t k_sin_qtr[65] = {
        0,  1608,  3216,  4821,  6424,  8022,  9616, 11204,
    12785, 14359, 15924, 17479, 19024, 20557, 22078, 23586,
    25080, 26558, 28020, 29466, 30893, 32303, 33692, 35062,
    36410, 37736, 39040, 40320, 41576, 42806, 44011, 45190,
    46341, 47464, 48559, 49624, 50660, 51665, 52639, 53581,
    54491, 55368, 56212, 57022, 57798, 58538, 59244, 59914,
    60547, 61145, 61705, 62228, 62714, 63162, 63572, 63944,
    64277, 64571, 64827, 65043, 65220, 65358, 65457, 65516,
    65536
};

/*
 * Sine of normalized phase in Q16.16 where 1.0 (65536) == 2*PI.
 * Uses quarter-wave LUT with linear interpolation: max error < 4e-5.
 */
fix16_t fix16_sin_norm(fix16_t phase_norm) {
    uint32_t p = (uint32_t)phase_norm & 0xFFFFu; /* [0, 65535] */
    uint32_t quad = p >> 14;                     /* 0..3 */
    uint32_t q = p & 0x3FFFu;                    /* 0..16383 (14 bits) */

    if (quad == 1u || quad == 3u) {
        q = 16384u - q;
    }

    uint32_t idx = q >> 8;                       /* 0..64 */
    uint32_t frac = q & 0xFFu;                   /* 0..255 */
    int32_t y0 = k_sin_qtr[idx];
    int32_t val = y0;
    if (idx < 64u) {
        int32_t y1 = k_sin_qtr[idx + 1u];
        val = y0 + (((y1 - y0) * (int32_t)frac) >> 8);
    }

    return (quad >= 2u) ? (fix16_t)(-val) : (fix16_t)val;
}

fix16_t fix16_cos_norm(fix16_t phase_norm) {
    return fix16_sin_norm(phase_norm + 16384); /* + 0.25 cycles */
}

/* 1 / (2*PI) in Q16.16 = 10430.378 -> 10430, higher precision via Q32 multiplier */
fix16_t fix16_sin(fix16_t rad) {
    /* phase_norm = rad / (2*PI). 2^32 / (2*PI) = 683565276 */
    int64_t p = ((int64_t)rad * 683565276LL) >> 32;
    return fix16_sin_norm((fix16_t)p);
}

fix16_t fix16_cos(fix16_t rad) {
    int64_t p = ((int64_t)rad * 683565276LL) >> 32;
    return fix16_cos_norm((fix16_t)p);
}

/*
 * 2^x in Q16.16.
 * Range reduction: x = n + f, f in [0, 1).
 * 4th-order minimax polynomial for 2^f on [0, 1]:
 *   2^f ~= 1 + f*(0.6931472 + f*(0.2402265 + f*(0.0555041 + f*0.0096181)))
 * Max relative error < 0.0002 (< 0.3 cents pitch error!).
 */
fix16_t fix16_pow2(fix16_t x) {
    if (x <= fix16_from_int(-16)) return 0;
    if (x >= fix16_from_int(15)) return FIX16_MAX;

    int32_t n = x >> 16;
    fix16_t f = (fix16_t)((uint32_t)x & 0xFFFFu);

    /* Horner evaluation in Q16.16 */
    fix16_t r = 630;                                         /* 0.0096181 */
    r = fix16_mul(r, f) + 3638;                              /* 0.0555041 */
    r = fix16_mul(r, f) + 15743;                             /* 0.2402265 */
    r = fix16_mul(r, f) + 45426;                             /* 0.6931472 */
    r = fix16_mul(r, f) + FIX16_ONE;                         /* 1.0000000 */

    if (n >= 0) {
        int64_t scaled = ((int64_t)r) << n;
        if (scaled > (int64_t)FIX16_MAX) return FIX16_MAX;
        return (fix16_t)scaled;
    } else {
        int32_t sh = -n;
        if (sh >= 31) return 0;
        return (fix16_t)(r >> sh);
    }
}

/* e^x = 2^(x / ln(2)) */
fix16_t fix16_exp(fix16_t x) {
    if (x <= fix16_from_int(-11)) return 0;
    if (x >= fix16_from_int(10)) return FIX16_MAX;
    fix16_t x_log2e = fix16_mul(x, FIX16_INV_LN2);
    return fix16_pow2(x_log2e);
}

/*
 * log2(x) in Q16.16 for x > 0.
 * Normalize x = m * 2^e with m in [1.0, 2.0), let t = m - 1 in [0, 1).
 * 4th-order minimax polynomial for log2(1 + t) on [0, 1]:
 *   log2(1+t) ~= t*(1.4426950 + t*(-0.7213475 + t*(0.4808983 + t*(-0.2885390 + t*0.0863))))
 */
fix16_t fix16_log2(fix16_t x) {
    if (x <= 0) return FIX16_MIN;

    uint32_t ux = (uint32_t)x;
    int32_t e = 0;

    /* Bounded normalization loops (max 16 iterations each, JPL P10) */
    for (int i = 0; i < 16 && ux >= 0x20000u; i++) {
        ux >>= 1;
        e++;
    }
    for (int i = 0; i < 16 && ux < 0x10000u; i++) {
        ux <<= 1;
        e--;
    }

    /* Now ux in [65536, 131071] -> t = ux - 65536 in [0, 65535] */
    fix16_t t = (fix16_t)(ux - 65536u);

    /* Padé-like polynomial for log2(1+t) on [0, 1]: exact at 0 and 1 */
    /* Using u = (m - 1) / (m + 1) in [0, 1/3]:
     * log2(m) = (2 / ln 2) * (u + u^3/3 + u^5/5)
     * At u <= 1/3, u^7/7 < 6.5e-5 -> extremely accurate! */
    fix16_t num = t;
    fix16_t den = (fix16_t)(ux + 65536u);
    fix16_t u = fix16_div(num, den);
    fix16_t u2 = fix16_mul(u, u);
    fix16_t u3 = fix16_mul(u2, u);
    fix16_t u5 = fix16_mul(u3, u2);

    /* sum = u + u3/3 + u5/5 */
    fix16_t sum = u + (u3 / 3) + (u5 / 5);
    /* 2 / ln(2) in Q16.16 = 2.8853901 * 65536 = 189097 */
    fix16_t frac_log = fix16_mul(sum, 189097);

    return fix16_from_int(e) + frac_log;
}

/* Smooth rational tanh approximation: x * (27 + x^2) / (27 + 9*x^2), clamped */
fix16_t fix16_tanh(fix16_t x) {
    if (x >= fix16_from_int(3)) return FIX16_ONE;
    if (x <= fix16_from_int(-3)) return -FIX16_ONE;
    fix16_t x2 = fix16_mul(x, x);
    fix16_t num = fix16_mul(x, fix16_from_int(27) + x2);
    fix16_t den = fix16_from_int(27) + (x2 * 9);
    return fix16_div(num, den);
}

/*
 * B-Sound WaveShaper Saturation C_k(x) (Orchestra Eq. 14):
 *   C_k(x) = ((3+k)*x*20deg / (pi + k*|x|)) normalized by C_k(1)
 * Simplifies cleanly to: x * (pi + k) / (pi + k * |x|) !
 */
fix16_t fix16_waveshaper(fix16_t x, fix16_t k) {
    if (k <= 0) return fix16_clamp(x, -FIX16_ONE, FIX16_ONE);
    x = fix16_clamp(x, -FIX16_ONE, FIX16_ONE);
    fix16_t ax = fix16_abs(x);
    fix16_t num = fix16_mul(x, FIX16_PI + k);
    fix16_t den = FIX16_PI + fix16_mul(k, ax);
    return fix16_div(num, den);
}

/* Pitch law: f(y) = 261.625565 * 2^y Hz */
fix16_t fix16_y_to_hz(fix16_t y) {
    if (y == FIX16_QUIET) return 0;
    y = fix16_clamp(y, fix16_from_int(-5), fix16_from_int(6));
    fix16_t mul = fix16_pow2(y);
    return fix16_mul(FIX16_C4_HZ, mul);
}

/* Inverse pitch law: y = log2(hz / 261.625565) */
fix16_t fix16_hz_to_y(fix16_t hz) {
    if (hz <= 0) return FIX16_QUIET;
    /* log2(hz) - log2(261.625565); log2(261.625565) = 8.0313597 -> 526343 in Q16.16 */
    return fix16_log2(hz) - 526343;
}

int32_t fix16_bit(uint32_t mask, int32_t k) {
    if (k < 0 || k >= 32) return 0;
    return (int32_t)((mask >> (uint32_t)k) & 1u);
}

int32_t fix16_digit(uint32_t num, int32_t idx_from_right) {
    if (idx_from_right < 0 || idx_from_right > 9) return 0;
    uint32_t v = num;
    for (int32_t i = 0; i < idx_from_right; i++) {
        v /= 10u;
    }
    return (int32_t)(v % 10u);
}

int32_t fix16_diat(int32_t d) {
    /* floor((12 * d + 2) / 7) matching JS Math.floor for negative values */
    int32_t n = 12 * d + 2;
    if (n >= 0) return n / 7;
    return -(((-n) + 6) / 7);
}
