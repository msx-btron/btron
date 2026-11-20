/*
 * pico_aeabi.c — Minimal Freestanding ARM EABI Integer Divide & Memory Builtins
 *
 * Needed for `-ffreestanding -nostdlib` on ARMv6-M (RP2040 Cortex-M0+, which
 * lacks hardware UDIV/SDIV) and for compiler-generated memcpy/memset calls.
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
