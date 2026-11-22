#include "rt/stdlib.h"
#include "rt/string.h"
#include "rt/mem.h"

#include <stdint.h>
#include <limits.h>

int rt_abs(int x)
{
    return x < 0 ? -x : x;
}

long rt_labs(long x)
{
    return x < 0 ? -x : x;
}

static int dig(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

/* 0x is a hex prefix only when a hex digit follows. Otherwise the 0 is a digit. */
static int hex_prefix(const char *p)
{
    int d;
    if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X'))
        return 0;
    d = dig(p[2]);
    return d >= 0 && d < 16;
}

static unsigned long long parse_u(const char *s, char **end, int base, int *neg)
{
    const char *p = s;
    unsigned long long acc = 0;
    int any = 0;
    int ov = 0;
    *neg = 0;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v')
        p++;
    if (*p == '+' || *p == '-') {
        if (*p == '-') *neg = 1;
        p++;
    }
    if (base == 0) {
        if (*p == '0') {
            if (hex_prefix(p)) {
                base = 16;
                p += 2;
            } else {
                base = 8;
                p++;
                any = 1;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && hex_prefix(p)) {
        p += 2;
    }
    if (base < 2 || base > 36) {
        if (end) *end = (char *)s;
        return 0;
    }
    for (;;) {
        int d = dig(*p);
        if (d < 0 || d >= base)
            break;
        if (!ov) {
            if (acc > (ULLONG_MAX - (unsigned long long)d) / (unsigned)base)
                ov = 1;
            else
                acc = acc * (unsigned)base + (unsigned)d;
        }
        p++;
        any = 1;
    }
    if (ov)
        acc = ULLONG_MAX;
    if (end) *end = (char *)(any ? p : s);
    return acc;
}

int rt_atoi(const char *s)
{
    char *end;
    int neg = 0;
    unsigned long long v = parse_u(s, &end, 10, &neg);
    if (neg) return -(int)v;
    return (int)v;
}

unsigned long rt_strtoul(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long long v = parse_u(s, end, base, &neg);
    if (neg) return (unsigned long)(0UL - (unsigned long)v);
    if (v > (unsigned long long)ULONG_MAX)
        return ULONG_MAX;
    return (unsigned long)v;
}

unsigned long long rt_strtoull(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long long v = parse_u(s, end, base, &neg);
    if (neg) return 0ULL - v;
    return v;
}

long rt_strtol(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long long v = parse_u(s, end, base, &neg);
    if (neg) return -(long)v;
    return (long)v;
}

long long rt_strtoll(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long long v = parse_u(s, end, base, &neg);
    if (neg) return -(long long)v;
    return (long long)v;
}

/* Ciura shell sort — fixed gap table, O(1) stack (rt.btron.pro).
   Uses a 256-byte stack temp when size fits; otherwise swaps by rotating
   one element through a single-byte shift (still O(1) stack). */
static const size_t ciura[] = { 701, 301, 132, 57, 23, 10, 4, 1 };

static void el_copy(unsigned char *d, const unsigned char *s, size_t n)
{
    size_t k;
    for (k = 0; k < n; k++)
        d[k] = s[k];
}

void rt_qsort_simple(void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *))
{
    unsigned char *b = (unsigned char *)base;
    unsigned char tmp[256];
    size_t i, j, gi;
    int use_tmp = size <= sizeof tmp;

    if (nmemb < 2 || size == 0)
        return;

    for (gi = 0; gi < sizeof ciura / sizeof ciura[0]; gi++) {
        size_t gap = ciura[gi];
        if (gap >= nmemb)
            continue;
        for (i = gap; i < nmemb; i++) {
            if (use_tmp) {
                el_copy(tmp, b + i * size, size);
                j = i;
                while (j >= gap && cmp(b + (j - gap) * size, tmp) > 0) {
                    el_copy(b + j * size, b + (j - gap) * size, size);
                    j -= gap;
                }
                el_copy(b + j * size, tmp, size);
            } else {
                /* Rotate element i down the gap chain using one-byte temp. */
                j = i;
                while (j >= gap && cmp(b + (j - gap) * size, b + j * size) > 0) {
                    size_t k;
                    for (k = 0; k < size; k++) {
                        unsigned char c = b[j * size + k];
                        b[j * size + k] = b[(j - gap) * size + k];
                        b[(j - gap) * size + k] = c;
                    }
                    j -= gap;
                }
            }
        }
    }
}


/* ========================================================================
   rt_qsort — iterative introsort
   ------------------------------------------------------------------------
   NASA JPL Power of Ten / BTRON constraints observed:
     - no recursion (explicit stack, max 64 frames)
     - all loops have fixed upper bounds derived from nmemb / depth
     - no dynamic allocation
     - single compilation unit, no libc sort dependency
   Algorithm: quicksort with median-of-three, insertion sort for small
   partitions (≤ 16), heapsort fallback when depth exceeds 2·⌊log2 n⌋.
   ======================================================================== */

enum { NEO_INS = 16, NEO_STACK = 64, NEO_TMP = 256 };

static void neo_swap(unsigned char *a, unsigned char *b, size_t n)
{
    unsigned char tmp[NEO_TMP];
    size_t k;
    if (n <= sizeof tmp) {
        for (k = 0; k < n; k++) tmp[k] = a[k];
        for (k = 0; k < n; k++) a[k] = b[k];
        for (k = 0; k < n; k++) b[k] = tmp[k];
        return;
    }
    for (k = 0; k < n; k++) {
        unsigned char c = a[k];
        a[k] = b[k];
        b[k] = c;
    }
}

static void neo_insertion(unsigned char *b, size_t lo, size_t hi, size_t size,
                          int (*cmp)(const void *, const void *))
{
    size_t i, j;
    unsigned char tmp[NEO_TMP];
    int small = size <= sizeof tmp;

    for (i = lo + 1; i <= hi; i++) {
        j = i;
        if (small) {
            size_t k;
            for (k = 0; k < size; k++) tmp[k] = b[i * size + k];
            while (j > lo && cmp(b + (j - 1) * size, tmp) > 0) {
                size_t t;
                for (t = 0; t < size; t++)
                    b[j * size + t] = b[(j - 1) * size + t];
                j--;
            }
            for (k = 0; k < size; k++) b[j * size + k] = tmp[k];
        } else {
            while (j > lo && cmp(b + (j - 1) * size, b + j * size) > 0) {
                neo_swap(b + (j - 1) * size, b + j * size, size);
                j--;
            }
        }
    }
}

static void neo_sift_down(unsigned char *b, size_t start, size_t end, size_t size,
                          int (*cmp)(const void *, const void *))
{
    size_t root = start;
    /* end is inclusive; bound: at most end iterations */
    while (root * 2 + 1 <= end) {
        size_t child = root * 2 + 1;
        size_t swap_i = root;
        if (cmp(b + swap_i * size, b + child * size) < 0)
            swap_i = child;
        if (child + 1 <= end && cmp(b + swap_i * size, b + (child + 1) * size) < 0)
            swap_i = child + 1;
        if (swap_i == root)
            return;
        neo_swap(b + root * size, b + swap_i * size, size);
        root = swap_i;
    }
}

static void neo_heapsort(unsigned char *b, size_t lo, size_t hi, size_t size,
                         int (*cmp)(const void *, const void *))
{
    size_t n, i;
    if (hi <= lo)
        return;
    /* Work on a compact view [0..n) via pointer offset */
    b += lo * size;
    n = hi - lo + 1;
    i = n / 2;
    while (i > 0) {
        i--;
        neo_sift_down(b, i, n - 1, size, cmp);
    }
    i = n;
    while (i > 1) {
        i--;
        neo_swap(b, b + i * size, size);
        neo_sift_down(b, 0, i - 1, size, cmp);
    }
}

static size_t neo_ilog2(size_t n)
{
    size_t d = 0;
    while (n > 1) {
        n >>= 1;
        d++;
    }
    return d;
}

/* Partition [lo, hi], return final pivot index. Median-of-three. */
static size_t neo_partition(unsigned char *b, size_t lo, size_t hi, size_t size,
                            int (*cmp)(const void *, const void *))
{
    size_t mid = lo + (hi - lo) / 2;
    unsigned char *p;

    /* sort lo, mid, hi */
    if (cmp(b + mid * size, b + lo * size) < 0)
        neo_swap(b + mid * size, b + lo * size, size);
    if (cmp(b + hi * size, b + lo * size) < 0)
        neo_swap(b + hi * size, b + lo * size, size);
    if (cmp(b + hi * size, b + mid * size) < 0)
        neo_swap(b + hi * size, b + mid * size, size);

    /* pivot = mid, move to hi-1 */
    neo_swap(b + mid * size, b + (hi - 1) * size, size);
    p = b + (hi - 1) * size;
    {
        size_t i = lo;
        size_t j = hi - 1;
        for (;;) {
            do { i++; } while (cmp(b + i * size, p) < 0);
            do { j--; } while (cmp(b + j * size, p) > 0);
            if (i >= j)
                break;
            neo_swap(b + i * size, b + j * size, size);
        }
        neo_swap(b + i * size, b + (hi - 1) * size, size);
        return i;
    }
}

/* Fast path for int-sized keys (oracle / common RT case). */
static void neo_qsort_int(int *a, size_t nmemb,
                          int (*cmp)(const void *, const void *))
{
    struct { size_t lo, hi; size_t depth; } stk[NEO_STACK];
    size_t sp, max_depth;

    max_depth = 2 * neo_ilog2(nmemb) + 2;
    stk[0].lo = 0;
    stk[0].hi = nmemb - 1;
    stk[0].depth = max_depth;
    sp = 1;

    while (sp > 0) {
        size_t lo, hi, depth, n, i, j;
        int pivot;
        sp--;
        lo = stk[sp].lo;
        hi = stk[sp].hi;
        depth = stk[sp].depth;
        if (hi <= lo)
            continue;
        n = hi - lo + 1;

        if (n <= 24) {
            /* insertion */
            for (i = lo + 1; i <= hi; i++) {
                int v = a[i];
                j = i;
                while (j > lo && cmp(&a[j - 1], &v) > 0) {
                    a[j] = a[j - 1];
                    j--;
                }
                a[j] = v;
            }
            continue;
        }
        if (depth == 0) {
            neo_heapsort((unsigned char *)a, lo, hi, sizeof(int), cmp);
            continue;
        }
        /* median-of-three into a[hi] */
        {
            size_t mid = lo + (hi - lo) / 2;
            if (cmp(&a[mid], &a[lo]) < 0) { int t = a[mid]; a[mid] = a[lo]; a[lo] = t; }
            if (cmp(&a[hi],  &a[lo]) < 0) { int t = a[hi];  a[hi]  = a[lo]; a[lo] = t; }
            if (cmp(&a[hi],  &a[mid]) < 0) { int t = a[hi];  a[hi]  = a[mid]; a[mid] = t; }
            pivot = a[mid];
            a[mid] = a[hi - 1];
            a[hi - 1] = pivot;
        }
        i = lo;
        j = hi - 1;
        for (;;) {
            do { i++; } while (cmp(&a[i], &pivot) < 0);
            do { j--; } while (cmp(&a[j], &pivot) > 0);
            if (i >= j)
                break;
            { int t = a[i]; a[i] = a[j]; a[j] = t; }
        }
        a[hi - 1] = a[i];
        a[i] = pivot;

        if (i > lo + 1 && sp < NEO_STACK) {
            stk[sp].lo = lo; stk[sp].hi = i - 1; stk[sp].depth = depth - 1; sp++;
        } else if (i > lo + 1) {
            neo_heapsort((unsigned char *)a, lo, i - 1, sizeof(int), cmp);
        }
        if (i + 1 < hi && sp < NEO_STACK) {
            stk[sp].lo = i + 1; stk[sp].hi = hi; stk[sp].depth = depth - 1; sp++;
        } else if (i + 1 < hi) {
            neo_heapsort((unsigned char *)a, i + 1, hi, sizeof(int), cmp);
        }
    }
}

void rt_qsort(void *base, size_t nmemb, size_t size,
                  int (*cmp)(const void *, const void *))
{
    unsigned char *b = (unsigned char *)base;
    struct { size_t lo, hi; size_t depth; } stk[NEO_STACK];
    size_t sp = 0;
    size_t max_depth;

    if (nmemb < 2 || size == 0 || !cmp)
        return;

    if (size == sizeof(int) && ((uintptr_t)base % sizeof(int)) == 0) {
        neo_qsort_int((int *)base, nmemb, cmp);
        return;
    }

    max_depth = 2 * neo_ilog2(nmemb) + 1;
    if (max_depth < 2)
        max_depth = 2;

    stk[0].lo = 0;
    stk[0].hi = nmemb - 1;
    stk[0].depth = max_depth;
    sp = 1;

    while (sp > 0) {
        size_t lo, hi, depth, n;
        sp--;
        lo = stk[sp].lo;
        hi = stk[sp].hi;
        depth = stk[sp].depth;
        if (hi <= lo)
            continue;
        n = hi - lo + 1;

        if (n <= NEO_INS) {
            neo_insertion(b, lo, hi, size, cmp);
            continue;
        }
        if (depth == 0) {
            neo_heapsort(b, lo, hi, size, cmp);
            continue;
        }
        {
            size_t p = neo_partition(b, lo, hi, size, cmp);
            if (p > lo) {
                if (sp < NEO_STACK) {
                    stk[sp].lo = lo;
                    stk[sp].hi = p - 1;
                    stk[sp].depth = depth - 1;
                    sp++;
                } else {
                    neo_heapsort(b, lo, p - 1, size, cmp);
                }
            }
            if (p < hi) {
                if (sp < NEO_STACK) {
                    stk[sp].lo = p + 1;
                    stk[sp].hi = hi;
                    stk[sp].depth = depth - 1;
                    sp++;
                } else {
                    neo_heapsort(b, p + 1, hi, size, cmp);
                }
            }
        }
    }
}

/* ---- optional first-fit pool ---- */
enum { HDR = 8 }; /* size_t size | 1=free */

static unsigned char *g_pool;
static size_t g_pool_sz;

void rt_alloc_init(void *pool, size_t bytes)
{
    size_t *h;
    g_pool = (unsigned char *)pool;
    g_pool_sz = bytes;
    if (!g_pool || bytes < HDR + HDR)
        return;
    h = (size_t *)g_pool;
    h[0] = (bytes - HDR) | 1u; /* free block covering the pool */
}

static size_t blk_size(size_t *h) { return h[0] & ~(size_t)1; }
static int blk_free(size_t *h) { return (int)(h[0] & 1u); }

void *rt_malloc(size_t n)
{
    unsigned char *p;
    size_t need, *h;
    if (!g_pool || n == 0)
        return 0;
    need = (n + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
    if (need < sizeof(size_t)) need = sizeof(size_t);
    p = g_pool;
    while (p + HDR <= g_pool + g_pool_sz) {
        h = (size_t *)p;
        if (blk_free(h) && blk_size(h) >= need) {
            size_t rest = blk_size(h) - need;
            if (rest >= HDR + sizeof(size_t)) {
                size_t *nH = (size_t *)(p + HDR + need);
                nH[0] = rest | 1u;
                h[0] = need; /* allocated */
            } else {
                h[0] = blk_size(h); /* consume whole */
            }
            return p + HDR;
        }
        p += HDR + blk_size(h);
    }
    return 0;
}

void rt_free(void *ptr)
{
    size_t *h;
    if (!ptr || !g_pool)
        return;
    h = (size_t *)((unsigned char *)ptr - HDR);
    h[0] |= 1u;
    /* greedy coalesce forward */
    {
        unsigned char *p = (unsigned char *)h;
        size_t *next;
        for (;;) {
            next = (size_t *)(p + HDR + blk_size((size_t *)p));
            if ((unsigned char *)next + HDR > g_pool + g_pool_sz)
                break;
            if (!blk_free(next))
                break;
            ((size_t *)p)[0] = (blk_size((size_t *)p) + HDR + blk_size(next)) | 1u;
        }
    }
}

void *rt_calloc(size_t nmemb, size_t size)
{
    size_t n;
    void *p;
    if (size && nmemb > (size_t)-1 / size)
        return 0;
    n = nmemb * size;
    p = rt_malloc(n);
    if (p)
        rt_memset_scalar(p, 0, n);
    return p;
}

void *rt_realloc(void *ptr, size_t n)
{
    size_t *h;
    size_t old;
    void *q;
    if (!ptr)
        return rt_malloc(n);
    if (n == 0) {
        rt_free(ptr);
        return 0;
    }
    h = (size_t *)((unsigned char *)ptr - HDR);
    old = blk_size(h);
    if (old >= n)
        return ptr;
    q = rt_malloc(n);
    if (!q)
        return 0;
    rt_memcpy_scalar(q, ptr, old);
    rt_free(ptr);
    return q;
}
