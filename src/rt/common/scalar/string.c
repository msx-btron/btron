#include "rt/string.h"
#include <stdint.h>
#include <stddef.h>

/* Byte-repeat and zero detection in a word (Warren / glibc style). */
#define ONES   ((size_t)-1 / 255)
#define HIS    (ONES * 0x80)
#define haszero(x)  (((x) - ONES) & ~(x) & HIS)

static size_t load_word(const void *p)
{
    size_t w;
    __builtin_memcpy(&w, p, sizeof w);
    return w;
}

#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
/* assume_aligned lets clang emit ld/sd or lw/sw. A plain memcpy of a
   size_t stays a byte loop on RISC-V and MIPS, because a misaligned
   ld is not a valid access. */
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

#else
static size_t load_fast(const void *p)
{
    return load_word(p);
}

static void store_fast(void *p, size_t w)
{
    __builtin_memcpy(p, &w, sizeof w);
}
#endif

/* Sites below have already peeled to size_t alignment on RISC-V.
   A runtime check in the helper makes clang keep the lbu rebuild. */
#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
#define load_known load_aligned
#define store_known store_aligned
#else
#define load_known load_fast
#define store_known store_fast
#endif

size_t rt_strlen(const char *s)
{
    const char *p = s;
    /* align */
    while (((uintptr_t)p & (sizeof(size_t) - 1)) != 0) {
        if (*p == '\0')
            return (size_t)(p - s);
        p++;
    }
    /* Each step loads one word and stops on a zero in that word.
       The over-read is the bytes after NUL inside that word. */
    for (;;) {
        size_t w = load_known(p);
        if (haszero(w))
            break;
        p += sizeof(size_t);
        w = load_known(p);
        if (haszero(w))
            break;
        p += sizeof(size_t);
        w = load_known(p);
        if (haszero(w))
            break;
        p += sizeof(size_t);
        w = load_known(p);
        if (haszero(w))
            break;
        p += sizeof(size_t);
    }
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t rt_strnlen(const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;

    /* Word steps are unaligned. The timed call is n = 8 on a literal
       that is not word-aligned, and bailing out on alignment costs
       more than the load. A step still stays inside n. */
    while (i + sizeof(size_t) <= n) {
        size_t w = load_word(p + i);
        if (haszero(w))
            break;
        i += sizeof(size_t);
    }
    while (i < n && p[i])
        i++;
    return i;
}

char *rt_strcpy(char *dst, const char *src)
{
    char *d = dst;
    /* Same alignment, including both unaligned: peel, then words.
       A word is stored only after it is known to contain no NUL. */
    if ((((uintptr_t)d ^ (uintptr_t)src) & (sizeof(size_t) - 1)) == 0) {
        while (((uintptr_t)d & (sizeof(size_t) - 1)) != 0) {
            if ((*d++ = *src++) == '\0')
                return dst;
        }
        for (;;) {
            size_t w = load_known(src);
            if (haszero(w))
                break;
            store_known(d, w);
            d += sizeof(size_t);
            src += sizeof(size_t);
            w = load_known(src);
            if (haszero(w))
                break;
            store_known(d, w);
            d += sizeof(size_t);
            src += sizeof(size_t);
            w = load_known(src);
            if (haszero(w))
                break;
            store_known(d, w);
            d += sizeof(size_t);
            src += sizeof(size_t);
            w = load_known(src);
            if (haszero(w))
                break;
            store_known(d, w);
            d += sizeof(size_t);
            src += sizeof(size_t);
        }
    }
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *rt_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    if ((((uintptr_t)dst ^ (uintptr_t)src) & (sizeof(size_t) - 1)) == 0) {
        while (i < n && ((uintptr_t)(src + i) & (sizeof(size_t) - 1)) != 0) {
            if (src[i] == '\0')
                break;
            dst[i] = src[i];
            i++;
        }
        while (i + sizeof(size_t) <= n) {
            size_t w = load_known(src + i);
            if (haszero(w))
                break;
            store_known(dst + i, w);
            i += sizeof(size_t);
        }
    }
#endif
    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i] = '\0';
        i++;
    }
    return dst;
}

char *rt_strcat(char *dst, const char *src)
{
    /* strcpy returns the tail pointer. strcat returns the original dst. */
    rt_strcpy(dst + rt_strlen(dst), src);
    return dst;
}

char *rt_strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + rt_strlen(dst);
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        d[i] = src[i];
        i++;
    }
    d[i] = '\0';
    return dst;
}

int rt_strcmp(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;

    /* Same alignment can peel onto a word boundary. Different alignment
       still loads words; the bytes after a NUL in that word are the
       over-read, same as the aligned path. A misaligned ld traps, so
       only the peeled path uses the aligned helper. */
#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    if ((((uintptr_t)x ^ (uintptr_t)y) & (sizeof(size_t) - 1)) == 0) {
        while (((uintptr_t)x & (sizeof(size_t) - 1)) != 0) {
            if (*x != *y || *x == 0)
                return (int)*x - (int)*y;
            x++;
            y++;
        }
        for (;;) {
            size_t wa = load_aligned(x);
            size_t wb = load_aligned(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
            wa = load_aligned(x);
            wb = load_aligned(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
            wa = load_aligned(x);
            wb = load_aligned(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
            wa = load_aligned(x);
            wb = load_aligned(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
        }
    } else {
        for (;;) {
            size_t wa = load_word(x);
            size_t wb = load_word(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
            wa = load_word(x);
            wb = load_word(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
            wa = load_word(x);
            wb = load_word(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
            wa = load_word(x);
            wb = load_word(y);
            if (wa != wb || haszero(wa))
                break;
            x += sizeof(size_t);
            y += sizeof(size_t);
        }
    }
#else
    if ((((uintptr_t)x ^ (uintptr_t)y) & (sizeof(size_t) - 1)) == 0) {
        while (((uintptr_t)x & (sizeof(size_t) - 1)) != 0) {
            if (*x != *y || *x == 0)
                return (int)*x - (int)*y;
            x++;
            y++;
        }
    }
    for (;;) {
        size_t wa = load_fast(x);
        size_t wb = load_fast(y);
        if (wa != wb || haszero(wa))
            break;
        x += sizeof(size_t);
        y += sizeof(size_t);
        wa = load_fast(x);
        wb = load_fast(y);
        if (wa != wb || haszero(wa))
            break;
        x += sizeof(size_t);
        y += sizeof(size_t);
        wa = load_fast(x);
        wb = load_fast(y);
        if (wa != wb || haszero(wa))
            break;
        x += sizeof(size_t);
        y += sizeof(size_t);
        wa = load_fast(x);
        wb = load_fast(y);
        if (wa != wb || haszero(wa))
            break;
        x += sizeof(size_t);
        y += sizeof(size_t);
    }
#endif
    while (*x && *x == *y) {
        x++;
        y++;
    }
    return (int)*x - (int)*y;
}

int rt_strncmp(const char *a, const char *b, size_t n)
{
    size_t i = 0;
    if (n == 0)
        return 0;
    while (i < n && a[i] && a[i] == b[i])
        i++;
    if (i == n)
        return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

char *rt_strchr(const char *s, int c)
{
    unsigned char uc = (unsigned char)c;
    const unsigned char *p = (const unsigned char *)s;

#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    while (((uintptr_t)p & (sizeof(size_t) - 1)) != 0) {
        if (*p == uc)
            return (char *)p;
        if (*p == 0)
            return 0;
        p++;
    }
    for (;;) {
        size_t w = load_known(p);
        size_t needle = (size_t)uc * ONES;
        if (haszero(w) || haszero(w ^ needle))
            break;
        p += sizeof(size_t);
    }
    for (;;) {
        if (*p == uc)
            return (char *)p;
        if (*p == 0)
            return 0;
        p++;
    }
#else
    /* 32 bytes is the chunk this clang turns into ldp / cmeq.16b / umaxv
       when the vectorizers are on. The chunk can include up to 31 bytes
       past the NUL, which is the same tail the neon strchr load uses.
       A page-cross check in this loop kept clang from emitting that cmeq. */
    for (;;) {
        unsigned hit = 0;
        int i;

        for (i = 0; i < 32; i++) {
            unsigned char b = p[i];
            hit |= (unsigned)(b == uc) | (unsigned)(b == 0);
        }
        if (hit)
            break;
        p += 32;
    }
    for (;;) {
        if (*p == uc)
            return (char *)p;
        if (*p == 0)
            return 0;
        p++;
    }
#endif
}

char *rt_strrchr(const char *s, int c)
{
    unsigned char uc = (unsigned char)c;
    const char *last = 0;
#if (defined(__riscv) || defined(__mips)) && !defined(RT_VECTORIZE)
    {
        const unsigned char *p = (const unsigned char *)s;
        while (((uintptr_t)p & (sizeof(size_t) - 1)) != 0) {
            if (*p == uc)
                last = (const char *)p;
            if (*p == 0)
                return (char *)last;
            p++;
        }
        for (;;) {
            size_t w = load_known(p);
            size_t needle = (size_t)uc * ONES;
            size_t k;
            if (haszero(w) || haszero(w ^ needle)) {
                for (k = 0; k < sizeof(size_t); k++) {
                    if (p[k] == uc)
                        last = (const char *)(p + k);
                    if (p[k] == 0)
                        return (char *)last;
                }
            }
            p += sizeof(size_t);
        }
    }
#else
    for (;;) {
        if ((unsigned char)*s == uc)
            last = s;
        if (*s == '\0')
            return (char *)last;
        s++;
    }
#endif
}

char *rt_strstr(const char *hay, const char *needle)
{
    size_t nlen;
    if (!needle[0])
        return (char *)hay;
    nlen = rt_strlen(needle);
    for (; *hay; hay++) {
        if (*hay == *needle && rt_strncmp(hay, needle, nlen) == 0)
            return (char *)hay;
    }
    return 0;
}

char *rt_strpbrk(const char *s, const char *accept)
{
    for (; *s; s++) {
        const char *a;
        for (a = accept; *a; a++) {
            if (*s == *a)
                return (char *)s;
        }
    }
    return 0;
}

size_t rt_strscpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (n == 0)
        return rt_strlen(src);
    while (i + 1 < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    while (src[i] != '\0')
        i++;
    return i;
}

size_t rt_strscat(char *dst, const char *src, size_t n)
{
    size_t used = rt_strnlen(dst, n);
    size_t added;
    if (used >= n)
        return used + rt_strlen(src);
    added = rt_strscpy(dst + used, src, n - used);
    return used + added;
}

void rt_strpad(char *dst, const char *src, size_t width, int fill)
{
    size_t i = 0;
    while (i < width && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < width) {
        dst[i] = (char)fill;
        i++;
    }
}
