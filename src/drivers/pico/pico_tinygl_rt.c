/*
 * pico_tinygl_rt.c — Freestanding math and libc shims for the PicoCalc image.
 *
 * The 96 KiB TinyGL bump arena left with TinyGL in phase 3. malloc returns
 * NULL. sinf, cosf, and sqrtf stay: GLGears precompute and the PicoGL door
 * call them. There is no 112x96 RGBA buffer.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>

void pico_tgl_arena_reset(void)
{
}

uint32_t pico_tgl_arena_used(void)
{
    return 0;
}

void *malloc(size_t size)
{
    (void)size;
    return NULL;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    uint8_t *p = (uint8_t *)malloc(total);
    if (p) {
        for (size_t i = 0; i < total; i++) p[i] = 0;
    }
    return p;
}

void free(void *ptr)
{
    (void)ptr;
}

/* ── Minimal Freestanding libc Shims (memcpy/memset in pico_aeabi.c) ───── */

typedef struct _PICO_FILE FILE;
static int s_dummy_file;
FILE *stderr = (FILE *)&s_dummy_file;
FILE *stdout = (FILE *)&s_dummy_file;

void exit(int status) { (void)status; for (;;) { __asm__ volatile ("wfi"); } }
void abort(void)      { for (;;) { __asm__ volatile ("wfi"); } }

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && (*a == *b)) { a++; b++; n--; }
    return n ? ((int)(uint8_t)*a - (int)(uint8_t)*b) : 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0') { }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != '\0') { }
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strstr(const char *hay, const char *ndl)
{
    if (!*ndl) return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = ndl;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)hay;
    }
    return NULL;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    (void)fmt;
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
}

/* ── Portable IEEE-754 Single-Precision Soft-Float for Cortex-M0+ (RP2040) ─ */

#if defined(PICO_BOARD_RP2040)

static inline uint32_t f2u(float f)    { union { float f; uint32_t u; } x = { f }; return x.u; }

#if !defined(PICO_QEMU_SOFTFP)
/* RP2040 BootROM Qfplib-M0+ fast single-precision float library (5-8x faster than C) */
typedef void *(*rom_lookup_fn)(uint16_t *table, uint32_t code);
typedef float    (*sf_ff_f)(float, float);
typedef float    (*sf_f_f)(float);
typedef int32_t  (*sf_f_i)(float);
typedef uint32_t (*sf_f_u)(float);
typedef float    (*sf_i_f)(int32_t);
typedef float    (*sf_u_f)(uint32_t);

static const uint32_t *rp2040_sf_table(void)
{
    static const uint32_t *s_sf = NULL;
    if (!s_sf) {
        uint16_t *data_tbl = (uint16_t *)(uintptr_t)(*(const uint16_t *)0x00000016UL);
        rom_lookup_fn lookup = (rom_lookup_fn)(uintptr_t)(*(const uint16_t *)0x00000018UL);
        s_sf = (const uint32_t *)lookup(data_tbl, (uint32_t)'S' | ((uint32_t)'F' << 8));
    }
    return s_sf;
}

float    __aeabi_fadd(float a, float b)  { return ((sf_ff_f)(uintptr_t)rp2040_sf_table()[0])(a, b); }
float    __aeabi_fsub(float a, float b)  { return ((sf_ff_f)(uintptr_t)rp2040_sf_table()[1])(a, b); }
float    __aeabi_frsub(float a, float b) { return ((sf_ff_f)(uintptr_t)rp2040_sf_table()[1])(b, a); }
float    __aeabi_fmul(float a, float b)  { return ((sf_ff_f)(uintptr_t)rp2040_sf_table()[2])(a, b); }
float    __aeabi_fdiv(float a, float b)  { return ((sf_ff_f)(uintptr_t)rp2040_sf_table()[3])(a, b); }
int32_t  __aeabi_f2iz(float a)           { return ((sf_f_i)(uintptr_t)rp2040_sf_table()[7])(a); }
uint32_t __aeabi_f2uiz(float a)          { return ((sf_f_u)(uintptr_t)rp2040_sf_table()[9])(a); }
float    __aeabi_i2f(int32_t a)          { return ((sf_i_f)(uintptr_t)rp2040_sf_table()[11])(a); }
float    __aeabi_ui2f(uint32_t a)        { return ((sf_u_f)(uintptr_t)rp2040_sf_table()[13])(a); }
#else
static inline float u2f(uint32_t u) { union { float f; uint32_t u; } x; x.u = u; return x.f; }
static float pack_f32(uint32_t sign, int32_t exp, uint32_t sig)
{
    if (sig == 0u) return u2f(sign);
    while (sig >= (1u << 24)) { sig >>= 1; exp++; }
    while (sig <  (1u << 23)) { sig <<= 1; exp--; }
    if (exp <= 0)   return u2f(sign);
    if (exp >= 255) return u2f(sign | 0x7F800000u);
    return u2f(sign | ((uint32_t)exp << 23) | (sig & 0x7FFFFFu));
}

float __aeabi_fadd(float a, float b)
{
    uint32_t ua = f2u(a), ub = f2u(b);
    uint32_t sa = ua & 0x80000000u, sb = ub & 0x80000000u;
    int32_t  ea = (int32_t)((ua >> 23) & 0xFFu);
    int32_t  eb = (int32_t)((ub >> 23) & 0xFFu);
    uint32_t ma = (ea ? ((ua & 0x7FFFFFu) | 0x800000u) : 0u);
    uint32_t mb = (eb ? ((ub & 0x7FFFFFu) | 0x800000u) : 0u);

    if (ea == 0) return b;
    if (eb == 0) return a;
    if (ea < eb || (ea == eb && ma < mb)) {
        uint32_t ts = sa; sa = sb; sb = ts;
        int32_t  te = ea; ea = eb; eb = te;
        uint32_t tm = ma; ma = mb; mb = tm;
    }
    int32_t diff = ea - eb;
    if (diff > 24) return u2f(sa | ((uint32_t)ea << 23) | (ma & 0x7FFFFFu));
    mb >>= diff;
    if (sa == sb) {
        ma += mb;
    } else {
        ma -= mb;
        if (ma == 0u) return u2f(0u);
    }
    return pack_f32(sa, ea, ma);
}

float __aeabi_fsub(float a, float b)  { return __aeabi_fadd(a, u2f(f2u(b) ^ 0x80000000u)); }
float __aeabi_frsub(float a, float b) { return __aeabi_fsub(b, a); }

float __aeabi_fmul(float a, float b)
{
    uint32_t ua = f2u(a), ub = f2u(b);
    uint32_t sign = (ua ^ ub) & 0x80000000u;
    int32_t  ea = (int32_t)((ua >> 23) & 0xFFu);
    int32_t  eb = (int32_t)((ub >> 23) & 0xFFu);
    if (ea == 0 || eb == 0) return u2f(sign);
    uint32_t ma = (ua & 0x7FFFFFu) | 0x800000u;
    uint32_t mb = (ub & 0x7FFFFFu) | 0x800000u;
    /* 24x24 -> 48-bit product using pure 32-bit multiplies (zero __aeabi_lmul) */
    uint32_t ah = ma >> 12, al = ma & 0xFFFu;
    uint32_t bh = mb >> 12, bl = mb & 0xFFFu;
    uint32_t hi = ah * bh;
    uint32_t mid = ah * bl + al * bh;
    uint32_t lo = al * bl;
    uint32_t res = (hi << 1) + (mid >> 11) + ((lo >> 23) & 1u);
    int32_t  exp = ea + eb - 127;
    if (res & (1u << 24)) {
        res >>= 1;
        exp++;
    }
    return pack_f32(sign, exp, res);
}

float __aeabi_fdiv(float a, float b)
{
    uint32_t ua = f2u(a), ub = f2u(b);
    uint32_t sign = (ua ^ ub) & 0x80000000u;
    int32_t  ea = (int32_t)((ua >> 23) & 0xFFu);
    int32_t  eb = (int32_t)((ub >> 23) & 0xFFu);
    if (ea == 0) return u2f(sign);
    if (eb == 0) return u2f(sign | 0x7F800000u);
    uint32_t ma = (ua & 0x7FFFFFu) | 0x800000u;
    uint32_t mb = (ub & 0x7FFFFFu) | 0x800000u;
    int32_t  exp = ea - eb + 127;
    if (ma < mb) { ma <<= 1; exp--; }
    uint32_t q = 0;
    for (int i = 23; i >= 0; i--) {
        if (ma >= mb) {
            ma -= mb;
            q |= (1u << i);
        }
        ma <<= 1;
    }
    return pack_f32(sign, exp, q);
}

int32_t __aeabi_f2iz(float a)
{
    uint32_t ua = f2u(a);
    uint32_t sign = ua & 0x80000000u;
    int32_t  exp  = (int32_t)((ua >> 23) & 0xFFu) - 127;
    uint32_t mant = (ua & 0x7FFFFFu) | 0x800000u;
    if (exp < 0) return 0;
    if (exp >= 31) return sign ? (-2147483647 - 1) : 2147483647;
    uint32_t val = (exp >= 23) ? (mant << (exp - 23)) : (mant >> (23 - exp));
    return sign ? -(int32_t)val : (int32_t)val;
}

uint32_t __aeabi_f2uiz(float a)
{
    uint32_t ua = f2u(a);
    if (ua & 0x80000000u) return 0u;
    int32_t  exp  = (int32_t)((ua >> 23) & 0xFFu) - 127;
    uint32_t mant = (ua & 0x7FFFFFu) | 0x800000u;
    if (exp < 0) return 0u;
    if (exp >= 32) return 0xFFFFFFFFu;
    return (exp >= 23) ? (mant << (exp - 23)) : (mant >> (23 - exp));
}

float __aeabi_ui2f(uint32_t v)
{
    if (v == 0u) return u2f(0u);
    int32_t exp = 127 + 23;
    while (v >= (1u << 24)) { v >>= 1; exp++; }
    while (v <  (1u << 23)) { v <<= 1; exp--; }
    return u2f(((uint32_t)exp << 23) | (v & 0x7FFFFFu));
}

float __aeabi_i2f(int32_t v)
{
    if (v == 0) return u2f(0u);
    uint32_t sign = 0u;
    uint32_t uv;
    if (v < 0) { sign = 0x80000000u; uv = (uint32_t)(-(v + 1)) + 1u; }
    else       { uv = (uint32_t)v; }
    return u2f(f2u(__aeabi_ui2f(uv)) | sign);
}
#endif /* !PICO_QEMU_SOFTFP */

static int fcmp_raw(float a, float b)
{
    uint32_t ua = f2u(a), ub = f2u(b);
    int32_t sa = (ua & 0x80000000u) ? -(int32_t)(ua & 0x7FFFFFFFu) : (int32_t)ua;
    int32_t sb = (ub & 0x80000000u) ? -(int32_t)(ub & 0x7FFFFFFFu) : (int32_t)ub;
    if (sa < sb) return -1;
    if (sa > sb) return  1;
    return 0;
}

int __aeabi_fcmpeq(float a, float b) { return fcmp_raw(a, b) == 0; }
int __aeabi_fcmplt(float a, float b) { return fcmp_raw(a, b) <  0; }
int __aeabi_fcmple(float a, float b) { return fcmp_raw(a, b) <= 0; }
int __aeabi_fcmpge(float a, float b) { return fcmp_raw(a, b) >= 0; }
int __aeabi_fcmpgt(float a, float b) { return fcmp_raw(a, b) >  0; }
int __aeabi_fcmpun(float a, float b)
{
    return ((f2u(a) & 0x7FFFFFFFu) > 0x7F800000u) ||
           ((f2u(b) & 0x7FFFFFFFu) > 0x7F800000u);
}

#endif /* PICO_BOARD_RP2040 */

/* ── Compact IEEE-754 Double Helpers (RP2040 & RP2350 FPv5-SP) ─────────── */

static float d_to_f(double d)
{
    volatile union { double d; uint32_t w[2]; } ud;
    union { float f; uint32_t u; } uf;
    uint32_t lo, hi, sign, mant;
    int32_t exp;
    ud.d = d;
    lo   = ud.w[0];
    hi   = ud.w[1];
    sign = hi & 0x80000000u;
    if (((hi & 0x7FFFFFFFu) | lo) == 0u) {
        uf.u = sign;
        return uf.f;
    }
    exp  = (int32_t)((hi >> 20) & 0x7FFu) - 1023 + 127;
    mant = ((hi & 0xFFFFFu) << 3) | (lo >> 29);
    if (exp <= 0) {
        uf.u = sign;
    } else if (exp >= 255) {
        uf.u = sign | 0x7F800000u;
    } else {
        uf.u = sign | ((uint32_t)exp << 23) | mant;
    }
    return uf.f;
}

static double f_to_d(float f)
{
    union { float f; uint32_t u; } uf = { f };
    volatile union { double d; uint32_t w[2]; } ud;
    uint32_t sign = uf.u & 0x80000000u;
    uint32_t fexp = (uf.u >> 23) & 0xFFu;
    uint32_t fman = uf.u & 0x7FFFFFu;
    if (fexp == 0u) {
        ud.w[0] = 0u;
        ud.w[1] = sign;
    } else if (fexp == 255u) {
        ud.w[0] = fman << 29;
        ud.w[1] = sign | (0x7FFu << 20) | (fman >> 3);
    } else {
        uint32_t dexp = fexp - 127u + 1023u;
        ud.w[0] = fman << 29;
        ud.w[1] = sign | (dexp << 20) | (fman >> 3);
    }
    return ud.d;
}

float    __aeabi_d2f(double a)            { return d_to_f(a); }
double   __aeabi_f2d(float a)             { return f_to_d(a); }
double   __aeabi_i2d(int32_t a)           { return f_to_d((float)a); }
double   __aeabi_ui2d(uint32_t a)         { return f_to_d((float)a); }
int32_t  __aeabi_d2iz(double a)           { return (int32_t)d_to_f(a); }
uint32_t __aeabi_d2uiz(double a)          { return (uint32_t)d_to_f(a); }
double   __aeabi_dadd(double a, double b) { return f_to_d(d_to_f(a) + d_to_f(b)); }
double   __aeabi_dsub(double a, double b) { return f_to_d(d_to_f(a) - d_to_f(b)); }
double   __aeabi_drsub(double a, double b){ return f_to_d(d_to_f(b) - d_to_f(a)); }
double   __aeabi_dmul(double a, double b) { return f_to_d(d_to_f(a) * d_to_f(b)); }
double   __aeabi_ddiv(double a, double b) { return f_to_d(d_to_f(a) / d_to_f(b)); }

int __aeabi_dcmpeq(double a, double b) { return d_to_f(a) == d_to_f(b); }
int __aeabi_dcmplt(double a, double b) { return d_to_f(a) <  d_to_f(b); }
int __aeabi_dcmple(double a, double b) { return d_to_f(a) <= d_to_f(b); }
int __aeabi_dcmpge(double a, double b) { return d_to_f(a) >= d_to_f(b); }
int __aeabi_dcmpgt(double a, double b) { return d_to_f(a) >  d_to_f(b); }

/* ── Freestanding Single-Precision Math (Portable on RP2040 & RP2350) ──── */

float fabsf(float x) { return x < 0.0f ? -x : x; }

float floorf(float x)
{
    int32_t i = (int32_t)x;
    return ((float)i > x) ? (float)(i - 1) : (float)i;
}

float ceilf(float x)
{
    int32_t i = (int32_t)x;
    return ((float)i < x) ? (float)(i + 1) : (float)i;
}

#if defined(PICO_BOARD_RP2040) && !defined(PICO_QEMU_SOFTFP)
float sqrtf(float x) { return x <= 0.0f ? 0.0f : ((sf_f_f)(uintptr_t)rp2040_sf_table()[6])(x); }
float cosf(float x)  { return ((sf_f_f)(uintptr_t)rp2040_sf_table()[15])(x); }
float sinf(float x)  { return ((sf_f_f)(uintptr_t)rp2040_sf_table()[16])(x); }
#else
#if defined(PICO_BOARD_RP2040)
float sqrtf(float x)
{
    if (x <= 0.0f) return 0.0f;
    /* Fast IEEE-754 initial estimate + 4 Newton-Raphson iterations */
    union { float f; uint32_t u; } u = { x };
    u.u = (u.u >> 1) + 0x1FC00000u;
    float r = u.f;
    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    r = 0.5f * (r + x / r);
    return r;
}
#else
float sqrtf(float x)
{
    float r;
    if (x <= 0.0f) return 0.0f;
    __asm__ volatile ("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
}
#endif

static float wrap_pi(float x)
{
    const float TWO_PI = 6.28318530718f;
    const float PI     = 3.14159265359f;
    int32_t k = (int32_t)(x * (1.0f / TWO_PI));
    x -= (float)k * TWO_PI;
    if (x >  PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    return x;
}

float sinf(float x)
{
    x = wrap_pi(x);
    if (x >  1.57079632679f) x =  3.14159265359f - x;
    if (x < -1.57079632679f) x = -3.14159265359f - x;
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
}

float cosf(float x)
{
    return sinf(x + 1.57079632679f);
}
#endif

float tanf(float x)
{
    float c = cosf(x);
    return (fabsf(c) < 1e-6f) ? 0.0f : (sinf(x) / c);
}

float powf(float base, float exp)
{
    int32_t n = (int32_t)exp;
    float res = 1.0f;
    if (base <= 0.0f) return 0.0f;
    if (n < 0) { base = 1.0f / base; n = -n; }
    while (n > 0) {
        if (n & 1) res *= base;
        base *= base;
        n >>= 1;
    }
    return res;
}

double sin(double x)            { return (double)sinf((float)x); }
double cos(double x)            { return (double)cosf((float)x); }
double tan(double x)            { return (double)tanf((float)x); }
double sqrt(double x)           { return (double)sqrtf((float)x); }
double fabs(double x)           { return (double)fabsf((float)x); }
double floor(double x)          { return (double)floorf((float)x); }
double ceil(double x)           { return (double)ceilf((float)x); }
double pow(double x, double y)  { return (double)powf((float)x, (float)y); }
