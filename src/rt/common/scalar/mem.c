#include "rt/mem.h"
#include <stdint.h>

/* Straight loops. On the host, clang -O2 widens an aligned run of these
   into wide stores when the vectorizers are on. The timed rows are
   64-byte aligned.
   RISC-V and MIPS with the vectorizers off do not widen a byte loop,
   so that build copies aligned size_t words. The vectorize-on build
   keeps these byte loops and may emit a wider instruction. */

#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
static size_t load_aligned(const void *p)
{
    size_t w;

    p = __builtin_assume_aligned(p, sizeof(size_t));
    __builtin_memcpy(&w, p, sizeof w);
    return w;
}

static void store_aligned(void *p, size_t w)
{
    p = __builtin_assume_aligned(p, sizeof w);
    __builtin_memcpy(p, &w, sizeof w);
}

static void copy_words(unsigned char *d, const unsigned char *s, size_t n)
{
    size_t step = 8 * sizeof(size_t);

    while (n >= step) {
        size_t w0 = load_aligned(s);
        size_t w1 = load_aligned(s + sizeof(size_t));
        size_t w2 = load_aligned(s + 2 * sizeof(size_t));
        size_t w3 = load_aligned(s + 3 * sizeof(size_t));
        size_t w4 = load_aligned(s + 4 * sizeof(size_t));
        size_t w5 = load_aligned(s + 5 * sizeof(size_t));
        size_t w6 = load_aligned(s + 6 * sizeof(size_t));
        size_t w7 = load_aligned(s + 7 * sizeof(size_t));
        store_aligned(d, w0);
        store_aligned(d + sizeof(size_t), w1);
        store_aligned(d + 2 * sizeof(size_t), w2);
        store_aligned(d + 3 * sizeof(size_t), w3);
        store_aligned(d + 4 * sizeof(size_t), w4);
        store_aligned(d + 5 * sizeof(size_t), w5);
        store_aligned(d + 6 * sizeof(size_t), w6);
        store_aligned(d + 7 * sizeof(size_t), w7);
        s += step;
        d += step;
        n -= step;
    }
    while (n >= sizeof(size_t)) {
        store_aligned(d, load_aligned(s));
        s += sizeof(size_t);
        d += sizeof(size_t);
        n -= sizeof(size_t);
    }
    while (n--)
        *d++ = *s++;
}

/* Load the whole chunk before any store. A backward memmove can overlap
   the chunk by less than one step, and the timed row overlaps by 16. */
static void copy_words_back(unsigned char *d, const unsigned char *s, size_t n)
{
    size_t step = 8 * sizeof(size_t);

    while (n & (sizeof(size_t) - 1)) {
        n--;
        d[n] = s[n];
    }
    while (n >= step) {
        size_t w0, w1, w2, w3, w4, w5, w6, w7;

        n -= step;
        w0 = load_aligned(s + n);
        w1 = load_aligned(s + n + sizeof(size_t));
        w2 = load_aligned(s + n + 2 * sizeof(size_t));
        w3 = load_aligned(s + n + 3 * sizeof(size_t));
        w4 = load_aligned(s + n + 4 * sizeof(size_t));
        w5 = load_aligned(s + n + 5 * sizeof(size_t));
        w6 = load_aligned(s + n + 6 * sizeof(size_t));
        w7 = load_aligned(s + n + 7 * sizeof(size_t));
        store_aligned(d + n, w0);
        store_aligned(d + n + sizeof(size_t), w1);
        store_aligned(d + n + 2 * sizeof(size_t), w2);
        store_aligned(d + n + 3 * sizeof(size_t), w3);
        store_aligned(d + n + 4 * sizeof(size_t), w4);
        store_aligned(d + n + 5 * sizeof(size_t), w5);
        store_aligned(d + n + 6 * sizeof(size_t), w6);
        store_aligned(d + n + 7 * sizeof(size_t), w7);
    }
    while (n >= sizeof(size_t)) {
        n -= sizeof(size_t);
        store_aligned(d + n, load_aligned(s + n));
    }
}

static void set_words(unsigned char *p, unsigned char uc, size_t n)
{
    size_t fill = uc;
    size_t step = 8 * sizeof(size_t);

    fill |= fill << 8;
    fill |= fill << 16;
#if __SIZEOF_SIZE_T__ == 8
    fill |= fill << 32;
#endif
    while (n >= step) {
        store_aligned(p, fill);
        store_aligned(p + sizeof(size_t), fill);
        store_aligned(p + 2 * sizeof(size_t), fill);
        store_aligned(p + 3 * sizeof(size_t), fill);
        store_aligned(p + 4 * sizeof(size_t), fill);
        store_aligned(p + 5 * sizeof(size_t), fill);
        store_aligned(p + 6 * sizeof(size_t), fill);
        store_aligned(p + 7 * sizeof(size_t), fill);
        p += step;
        n -= step;
    }
    while (n >= sizeof(size_t)) {
        store_aligned(p, fill);
        p += sizeof(size_t);
        n -= sizeof(size_t);
    }
    while (n--)
        *p++ = uc;
}

static int both_aligned(const void *a, const void *b)
{
    return (((uintptr_t)a | (uintptr_t)b) & (sizeof(size_t) - 1)) == 0;
}
#endif

void *rt_memcpy_scalar(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    if (both_aligned(d, s)) {
        copy_words(d, s, n);
        return dst;
    }
#endif
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *rt_memmove_scalar(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    if (d == s || n == 0)
        return dst;
#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    if (both_aligned(d, s)) {
        /* Overlap with dst above src must copy from the top. */
        if (d < s)
            copy_words(d, s, n);
        else
            copy_words_back(d, s, n);
        return dst;
    }
#endif
    /* Overlap with dst above src must copy from the top. */
    if (d < s) {
        for (i = 0; i < n; i++)
            d[i] = s[i];
        return dst;
    }
    for (i = n; i > 0; i--)
        d[i - 1] = s[i - 1];
    return dst;
}

void *rt_memset_scalar(void *dst, int c, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    unsigned char uc = (unsigned char)c;
    size_t i;

#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    if (((uintptr_t)p & (sizeof(size_t) - 1)) == 0) {
        set_words(p, uc, n);
        return dst;
    }
#endif
    for (i = 0; i < n; i++)
        p[i] = uc;
    return dst;
}

int rt_memcmp_scalar(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;

    /* Word loads through memcpy stay inside n. The difference is still
       the first unsigned byte, in address order. */
    while (n >= 32) {
        uint64_t x0, x1, x2, x3, y0, y1, y2, y3;
        __builtin_memcpy(&x0, x, 8);
        __builtin_memcpy(&y0, y, 8);
        __builtin_memcpy(&x1, x + 8, 8);
        __builtin_memcpy(&y1, y + 8, 8);
        __builtin_memcpy(&x2, x + 16, 8);
        __builtin_memcpy(&y2, y + 16, 8);
        __builtin_memcpy(&x3, x + 24, 8);
        __builtin_memcpy(&y3, y + 24, 8);
        if ((x0 ^ y0) | (x1 ^ y1) | (x2 ^ y2) | (x3 ^ y3))
            break;
        x += 32;
        y += 32;
        n -= 32;
    }
    while (n >= 8) {
        uint64_t xw, yw;
        __builtin_memcpy(&xw, x, 8);
        __builtin_memcpy(&yw, y, 8);
        if (xw != yw)
            break;
        x += 8;
        y += 8;
        n -= 8;
    }
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}
