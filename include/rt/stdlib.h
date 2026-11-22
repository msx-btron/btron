#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include_next <stdlib.h>
#else
#ifndef _GL_STDLIB_H_
#define _GL_STDLIB_H_

#include <rt/libstr.h>

extern void* Imalloc(size_t size);
extern void* Icalloc(size_t nmemb, size_t size);
extern void  Ifree(void *ptr);
extern void* Irealloc(void *ptr, size_t size);

#undef malloc
#undef calloc
#undef free
#undef realloc

static inline void* malloc(size_t size) { return Imalloc(size); }
static inline void* calloc(size_t nmemb, size_t size) { return Icalloc(nmemb, size); }
static inline void  free(void *ptr) { Ifree(ptr); }
static inline void* realloc(void *ptr, size_t size) { return Irealloc(ptr, size); }
static inline void  exit(int status) { (void)status; }
/* hlt is an x86 halt.  Every other freestanding target, including the
 * Emotion Engine, rejects that mnemonic, so the spin has to be empty. */
static inline void  abort(void) {
#if defined(__i386__) || defined(__x86_64__)
    for (;;) { __asm__ volatile("hlt"); }
#else
    for (;;) { __asm__ volatile(""); }
#endif
}
/* No process environment on a freestanding image.  Callers treat NULL
 * as "the variable is unset". */
static inline char *getenv(const char *name) { (void)name; return 0; }

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define strtoull tkl_strtoull

static inline long strtol(const char *nptr, char **endptr, int base) {
    const char *p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }
    long v = (long)tkl_strtoul(p, endptr, base);
    return neg ? -v : v;
}

static inline long long strtoll(const char *nptr, char **endptr, int base) {
    const char *p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }
    long long v = (long long)tkl_strtoull(p, endptr, base);
    return neg ? -v : v;
}

static inline double strtod(const char *nptr, char **endptr) {
    const char *p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    double sign = 1.0;
    if (*p == '-') { sign = -1.0; p++; }
    else if (*p == '+') { p++; }
    double val = 0.0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10.0 + (double)(*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        double frac = 0.1;
        while (*p >= '0' && *p <= '9') {
            val += (double)(*p - '0') * frac;
            frac *= 0.1;
            p++;
        }
    }
    if (endptr) *endptr = (char*)p;
    return sign * val;
}

static inline double atof(const char *nptr) {
    return strtod(nptr, (char**)0);
}

static inline float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

/* Hosted builds get rand() from libc; the Quake sources call it for weapon
 * spread.  Same 24-bit LCG as the QC random() builtin so both targets jitter
 * alike. */
static inline int rand(void) {
    static unsigned s_rnd = 0xDEADBEEFu;
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return (int)(s_rnd >> 8);
}

/* Freestanding bounded in-place sort (Shell sort with Ciura gaps) */
static inline void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0) return;
    size_t gaps[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
    size_t num_gaps = sizeof(gaps) / sizeof(gaps[0]);
    char *bytes = (char *)base;
    for (size_t g = 0; g < num_gaps; g++) {
        size_t gap = gaps[g];
        if (gap >= nmemb) continue;
        for (size_t i = gap; i < nmemb; i++) {
            for (size_t j = i; j >= gap; j -= gap) {
                char *a = bytes + (j - gap) * size;
                char *b = bytes + j * size;
                if (compar(a, b) > 0) {
                    for (size_t k = 0; k < size; k++) {
                        char tmp = a[k];
                        a[k] = b[k];
                        b[k] = tmp;
                    }
                } else {
                    break;
                }
            }
        }
    }
}


/* B-RT. One declaration set. ISO names above stay the public calls. */
/* Numeric conversion, sort, and optional freestanding allocator.
   Matches the tkl_* / I* plane on rt.btron.pro: no locale, no heap
   required for conversion or qsort. */

int               rt_atoi(const char *s);
unsigned long     rt_strtoul(const char *s, char **end, int base);
unsigned long long rt_strtoull(const char *s, char **end, int base);
long              rt_strtol(const char *s, char **end, int base);
long long         rt_strtoll(const char *s, char **end, int base);

/* Iterative introsort (no recursion). Explicit stack ≤ 64 frames.
   JPL Power-of-10 friendly: bounded loops, fixed stack, no malloc.
   Aimed at host-libc parity or better on random integer keys. */
void              rt_qsort(void *base, size_t nmemb, size_t size,
                           int (*cmp)(const void *, const void *));

/* Ciura shell sort — O(1) stack, original BTRON/rt plane.
   Kept in the library. The oracle does not time it. */
void              rt_qsort_simple(void *base, size_t nmemb, size_t size,
                                  int (*cmp)(const void *, const void *));

int               rt_abs(int x);
long              rt_labs(long x);

void              rt_alloc_init(void *pool, size_t bytes);
void             *rt_malloc(size_t n);
void             *rt_calloc(size_t nmemb, size_t size);
void             *rt_realloc(void *p, size_t n);
void              rt_free(void *p);
#endif /* _GL_STDLIB_H_ */
#endif /* HOSTED */

