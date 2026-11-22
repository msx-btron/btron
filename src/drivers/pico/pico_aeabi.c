/*
 * pico_aeabi.c — Minimal Freestanding ARM EABI Integer Divide & Memory Builtins
 *
 * Needed for `-ffreestanding -nostdlib` on ARMv6-M (RP2040 Cortex-M0+, which
 * lacks hardware UDIV/SDIV and has only a 32×32→32 multiply) and for
 * compiler-generated memcpy/memset calls. 64-bit products are four 16×16
 * multiplies. 64-bit division is restoring division on 32-bit limbs.
 * __aeabi_ldivmod / __aeabi_uldivmod return the quotient in r0:r1 and the
 * remainder in r2:r3; clang reads r2:r3 for the `%` operator.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t quot;
    uint32_t rem;
} udivmod_t;

static udivmod_t udivmod32(uint32_t n, uint32_t d)
{
    udivmod_t r = { 0u, 0u };
    if (d == 0u) return r;
    for (int i = 31; i >= 0; i--) {
        r.rem = (r.rem << 1u) | ((n >> (uint32_t)i) & 1u);
        if (r.rem >= d) {
            r.rem -= d;
            r.quot |= (1u << (uint32_t)i);
        }
    }
    return r;
}

uint32_t __aeabi_uidiv(uint32_t n, uint32_t d)
{
    return udivmod32(n, d).quot;
}

uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d)
{
    udivmod_t r = udivmod32(n, d);
    return ((uint64_t)r.rem << 32u) | (uint64_t)r.quot;
}

int32_t __aeabi_idiv(int32_t n, int32_t d)
{
    int neg = (n < 0) ^ (d < 0);
    uint32_t un = (n < 0) ? (uint32_t)(-(int64_t)n) : (uint32_t)n;
    uint32_t ud = (d < 0) ? (uint32_t)(-(int64_t)d) : (uint32_t)d;
    uint32_t q  = udivmod32(un, ud).quot;
    return neg ? -(int32_t)q : (int32_t)q;
}

uint64_t __aeabi_idivmod(int32_t n, int32_t d)
{
    int qneg = (n < 0) ^ (d < 0);
    int rneg = (n < 0);
    uint32_t un = (n < 0) ? (uint32_t)(-(int64_t)n) : (uint32_t)n;
    uint32_t ud = (d < 0) ? (uint32_t)(-(int64_t)d) : (uint32_t)d;
    udivmod_t r = udivmod32(un, ud);
    int32_t sq = qneg ? -(int32_t)r.quot : (int32_t)r.quot;
    int32_t sr = rneg ? -(int32_t)r.rem  : (int32_t)r.rem;
    return ((uint64_t)(uint32_t)sr << 32u) | (uint64_t)(uint32_t)sq;
}

typedef union {
    int64_t s;
    uint64_t u;
    struct { uint32_t lo, hi; } p;
} i64p;

static uint32_t mul32(uint32_t a, uint32_t b)
{
    uint32_t r = b;
    __asm__ volatile("muls %0, %1, %0" : "+l"(r) : "l"(a));
    return r;
}

static void mul_wide(uint32_t a, uint32_t b, uint32_t *lo, uint32_t *hi)
{
    uint32_t a0 = a & 0xffffu, a1 = a >> 16;
    uint32_t b0 = b & 0xffffu, b1 = b >> 16;
    uint32_t p00 = mul32(a0, b0);
    uint32_t p01 = mul32(a0, b1);
    uint32_t p10 = mul32(a1, b0);
    uint32_t p11 = mul32(a1, b1);
    uint32_t mid = (p00 >> 16) + (p01 & 0xffffu) + (p10 & 0xffffu);
    *lo = (p00 & 0xffffu) | (mid << 16);
    *hi = p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16);
}

int64_t __aeabi_lmul(int64_t a, int64_t b)
{
    i64p aa, bb, rr;
    uint32_t lo, hi;
    aa.s = a;
    bb.s = b;
    mul_wide(aa.p.lo, bb.p.lo, &lo, &hi);
    hi += mul32(aa.p.lo, bb.p.hi);
    hi += mul32(aa.p.hi, bb.p.lo);
    rr.p.lo = lo;
    rr.p.hi = hi;
    return rr.s;
}

static int limb_ge(uint32_t ah, uint32_t al, uint32_t bh, uint32_t bl)
{
    return (ah > bh) || (ah == bh && al >= bl);
}

static void limb_sub(uint32_t *h, uint32_t *l, uint32_t bh, uint32_t bl)
{
    uint32_t borrow = (*l < bl);
    *l -= bl;
    *h -= bh + borrow;
}

/* (nh:nl) / d with nh < d, so the quotient fits in 32 bits. */
static void div64_32(uint32_t nh, uint32_t nl, uint32_t d, uint32_t *q, uint32_t *r)
{
    uint32_t rem = nh;
    uint32_t quot = 0;
    int i;
    for (i = 31; i >= 0; i--) {
        uint32_t bit = (nl >> (uint32_t)i) & 1u;
        uint32_t carry = rem >> 31;
        rem = (rem << 1) | bit;
        if (carry || rem >= d) {
            rem -= d;
            quot |= 1u << (uint32_t)i;
        }
    }
    *q = quot;
    *r = rem;
}

static void udivmod64(uint32_t nl, uint32_t nh, uint32_t dl, uint32_t dh,
                      uint32_t *ql, uint32_t *qh, uint32_t *rl, uint32_t *rh)
{
    uint32_t rq_l = 0, rq_h = 0, rr_l = 0, rr_h = 0;
    int i;
    if ((dl | dh) == 0u) {
        *ql = *qh = *rl = *rh = 0;
        return;
    }
    if (dh == 0u && nh == 0u) {
        udivmod_t u = udivmod32(nl, dl);
        *ql = u.quot;
        *qh = 0;
        *rl = u.rem;
        *rh = 0;
        return;
    }
    if (dh == 0u && nh < dl) {
        uint32_t q, r;
        div64_32(nh, nl, dl, &q, &r);
        *ql = q;
        *qh = 0;
        *rl = r;
        *rh = 0;
        return;
    }
    for (i = 63; i >= 0; i--) {
        uint32_t bit = (i >= 32) ? ((nh >> (uint32_t)(i - 32)) & 1u)
                                 : ((nl >> (uint32_t)i) & 1u);
        uint32_t top = rr_h >> 31;
        rr_h = (rr_h << 1) | (rr_l >> 31);
        rr_l = (rr_l << 1) | bit;
        if (top || limb_ge(rr_h, rr_l, dh, dl)) {
            limb_sub(&rr_h, &rr_l, dh, dl);
            if (i >= 32) rq_h |= 1u << (uint32_t)(i - 32);
            else         rq_l |= 1u << (uint32_t)i;
        }
    }
    *ql = rq_l;
    *qh = rq_h;
    *rl = rr_l;
    *rh = rr_h;
}

static int fits_i32(uint32_t lo, uint32_t hi)
{
    if (hi == 0u) return lo <= 0x7fffffffu;
    if (hi == 0xffffffffu) return lo >= 0x80000000u;
    return 0;
}

static void limb_neg(uint32_t *lo, uint32_t *hi)
{
    uint32_t old = *lo;
    *lo = 0u - *lo;
    *hi = ~*hi + (old == 0u ? 1u : 0u);
}

static void idivmod64(uint32_t nl, uint32_t nh, uint32_t dl, uint32_t dh,
                      uint32_t *ql, uint32_t *qh, uint32_t *rl, uint32_t *rh)
{
    uint32_t unl, unh, udl, udh, uql, uqh, url, urh;
    int nneg, dneg;
    if ((dl | dh) == 0u) {
        *ql = *qh = *rl = *rh = 0;
        return;
    }
    if (fits_i32(nl, nh) && fits_i32(dl, dh)) {
        int32_t n = (int32_t)nl;
        int32_t d = (int32_t)dl;
        uint32_t sq, sr;
        int qneg, rneg;
        udivmod_t u;
        if (n == (int32_t)0x80000000 && d == -1) {
            *ql = 0x80000000u;
            *qh = 0;
            *rl = 0;
            *rh = 0;
            return;
        }
        qneg = (n < 0) ^ (d < 0);
        rneg = (n < 0);
        unl = (uint32_t)n;
        udl = (uint32_t)d;
        if (n < 0) unl = 0u - unl;
        if (d < 0) udl = 0u - udl;
        u = udivmod32(unl, udl);
        sq = qneg ? (0u - u.quot) : u.quot;
        sr = rneg ? (0u - u.rem) : u.rem;
        *ql = sq;
        *qh = (sq & 0x80000000u) ? 0xffffffffu : 0u;
        *rl = sr;
        *rh = (sr & 0x80000000u) ? 0xffffffffu : 0u;
        return;
    }
    nneg = (int)((nh >> 31) & 1u);
    dneg = (int)((dh >> 31) & 1u);
    unl = nl;
    unh = nh;
    udl = dl;
    udh = dh;
    if (nneg) limb_neg(&unl, &unh);
    if (dneg) limb_neg(&udl, &udh);
    udivmod64(unl, unh, udl, udh, &uql, &uqh, &url, &urh);
    if (nneg ^ dneg) limb_neg(&uql, &uqh);
    if (nneg) limb_neg(&url, &urh);
    *ql = uql;
    *qh = uqh;
    *rl = url;
    *rh = urh;
}

__attribute__((used))
static void sdivmod_out(uint32_t nl, uint32_t nh, uint32_t dl, uint32_t dh,
                        uint32_t *o)
{
    idivmod64(nl, nh, dl, dh, &o[0], &o[1], &o[2], &o[3]);
}

__attribute__((used))
static void udivmod_out(uint32_t nl, uint32_t nh, uint32_t dl, uint32_t dh,
                        uint32_t *o)
{
    udivmod64(nl, nh, dl, dh, &o[0], &o[1], &o[2], &o[3]);
}

__attribute__((naked)) void __aeabi_ldivmod(void)
{
    __asm__ volatile(
        "push {r4, lr}\n\t"
        "sub sp, sp, #24\n\t"
        "mov r4, sp\n\t"
        "adds r4, r4, #8\n\t"
        "str r4, [sp]\n\t"
        "bl sdivmod_out\n\t"
        "ldr r0, [sp, #8]\n\t"
        "ldr r1, [sp, #12]\n\t"
        "ldr r2, [sp, #16]\n\t"
        "ldr r3, [sp, #20]\n\t"
        "add sp, sp, #24\n\t"
        "pop {r4, pc}\n\t"
    );
}

__attribute__((naked)) void __aeabi_uldivmod(void)
{
    __asm__ volatile(
        "push {r4, lr}\n\t"
        "sub sp, sp, #24\n\t"
        "mov r4, sp\n\t"
        "adds r4, r4, #8\n\t"
        "str r4, [sp]\n\t"
        "bl udivmod_out\n\t"
        "ldr r0, [sp, #8]\n\t"
        "ldr r1, [sp, #12]\n\t"
        "ldr r2, [sp, #16]\n\t"
        "ldr r3, [sp, #20]\n\t"
        "add sp, sp, #24\n\t"
        "pop {r4, pc}\n\t"
    );
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void __aeabi_memclr(void *dst, size_t n)        { (void)memset(dst, 0, n); }
void __aeabi_memclr4(void *dst, size_t n)       { (void)memset(dst, 0, n); }
void __aeabi_memclr8(void *dst, size_t n)       { (void)memset(dst, 0, n); }
void __aeabi_memset(void *dst, size_t n, int c) { (void)memset(dst, c, n); }
void __aeabi_memset4(void *dst, size_t n, int c){ (void)memset(dst, c, n); }
void __aeabi_memcpy(void *d, const void *s, size_t n)  { (void)memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { (void)memcpy(d, s, n); }
