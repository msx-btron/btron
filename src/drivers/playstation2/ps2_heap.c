/*
 * ps2_heap.c - PlayStation 2 Cleanroom Kernel Heap Allocator
 *
 * Implements a first-fit RDRAM allocator with block coalescing, reentrant tolerance,
 * and memory owner telemetry for B-System on PS2.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_heap.h"
#include <libstr.h>

extern void ps2_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define ps2_stack_probe_depth() (0u)
#define PS2_STACK_PROBE_BYTES (512u * 1024u)

static uint8_t s_ps2_heap[PS2_HEAP_SIZE] __attribute__((aligned(64)));

#define PS2_HEAP_ALIGN  16u
#define PS2_HEAP_HDR    16u

typedef struct ps2_blk {
    struct ps2_blk *next;
    size_t          size;
    unsigned        used;
    unsigned        ra;
} ps2_blk;

static ps2_blk *s_heap_list;

typedef char ps2_heap_header_is_16[(sizeof(ps2_blk) == PS2_HEAP_HDR) ? 1 : -1];

static size_t   s_heap_used;
static size_t   s_heap_peak;

uint32_t heap_ptr = 0x00200000;
#define PS2_HEAP_LOGLIM (96u * 1024u)
#ifndef PS2_HEAP_BLOCKS_MAX
#define PS2_HEAP_BLOCKS_MAX (PS2_HEAP_SIZE / (PS2_HEAP_HDR + PS2_HEAP_ALIGN))
#endif

static void ps2_heap_init(void)
{
    s_heap_list = (ps2_blk *)s_ps2_heap;
    s_heap_list->next = NULL;
    s_heap_list->size = PS2_HEAP_SIZE - PS2_HEAP_HDR;
    s_heap_list->used = 0;
    s_heap_used = 0;
    s_heap_peak = 0;
}

static size_t ps2_heap_largest_free(void)
{
    size_t largest = 0;
    for (const ps2_blk *f = s_heap_list; f; f = f->next)
        if (!f->used && f->size > largest) largest = f->size;
    return largest;
}

size_t ps2_heap_largest_hole(void)
{
    return ps2_heap_largest_free();
}

size_t ps2_heap_used_bytes(void)
{
    return s_heap_used;
}

size_t ps2_heap_peak_bytes(void)
{
    return s_heap_peak;
}

#define PS2_HEAP_HOLDER_SLOTS 24
static unsigned s_heap_ooms;
static unsigned s_heap_oom_site;
static size_t   s_heap_oom_size;
static size_t   s_heap_census_mark;

void ps2_heap_holders(const char *why)
{
    unsigned ras[PS2_HEAP_HOLDER_SLOTS];
    size_t   tot[PS2_HEAP_HOLDER_SLOTS];
    size_t   big[PS2_HEAP_HOLDER_SLOTS];
    unsigned cnt[PS2_HEAP_HOLDER_SLOTS];
    int n = 0;
    size_t rest_tot = 0, rest_big = 0;
    unsigned rest_cnt = 0;

    s_heap_census_mark = s_heap_used;

    for (const ps2_blk *b = s_heap_list; b; b = b->next) {
        int i, hit = -1;
        if (!b->used) continue;
        for (i = 0; i < n; i++) if (ras[i] == b->ra) { hit = i; break; }
        if (hit < 0 && n < PS2_HEAP_HOLDER_SLOTS) {
            hit = n++;
            ras[hit] = b->ra; tot[hit] = 0; cnt[hit] = 0; big[hit] = 0;
        }
        if (hit < 0) {
            rest_tot += b->size; rest_cnt++;
            if (b->size > rest_big) rest_big = b->size;
            continue;
        }
        tot[hit] += b->size; cnt[hit]++;
        if (b->size > big[hit]) big[hit] = b->size;
    }

    ps2_kprintf("[HEAP] holder census %s: %d site(s), used=%u kB of %u kB, failed asks=%u, "
                "stack used=%u kB of %u kB probed\n", why, n,
                (unsigned int)(s_heap_used / 1024u), (unsigned int)(PS2_HEAP_SIZE / 1024u),
                s_heap_ooms, ps2_stack_probe_depth(), (unsigned int)(PS2_STACK_PROBE_BYTES / 1024u));
    if (s_heap_ooms) {
        ps2_kprintf("[HEAP]   last failed ask: %u kB from 0x%x\n",
                    (unsigned int)(s_heap_oom_size / 1024u), s_heap_oom_site);
    }
    for (int pass = 0; pass < n; pass++) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (tot[i] == 0u) continue;
            if (best < 0 || tot[i] > tot[best]) best = i;
        }
        if (best < 0) break;
        ps2_kprintf("[HEAP]   0x%x holds %u kB in %u block(s), biggest %u kB\n",
                    ras[best], (unsigned int)(tot[best] / 1024u), cnt[best],
                    (unsigned int)(big[best] / 1024u));
        tot[best] = 0u;
    }
    if (rest_cnt) {
        ps2_kprintf("[HEAP]   +%u block(s) of %u kB (biggest %u kB) at sites past the %d slots"
                    " -- raise PS2_HEAP_HOLDER_SLOTS to name them\n",
                    rest_cnt, (unsigned int)(rest_tot / 1024u), (unsigned int)(rest_big / 1024u),
                    PS2_HEAP_HOLDER_SLOTS);
    }
}

static int s_heap_census_done;

static void* ps2_heap_take(size_t size, const void *ra)
{
    const unsigned site = (unsigned)(uintptr_t)ra;

    if (size == 0) return NULL;
    size = (size + PS2_HEAP_ALIGN - 1u) & ~(size_t)(PS2_HEAP_ALIGN - 1u);
    if (!s_heap_list) ps2_heap_init();

    for (ps2_blk *b = s_heap_list; b; b = b->next) {
        if (b->used || b->size < size) continue;

        if (b->size >= size + PS2_HEAP_HDR + PS2_HEAP_ALIGN) {
            ps2_blk *tail = (ps2_blk *)(void *)((uint8_t *)b + PS2_HEAP_HDR + size);
            tail->next = b->next;
            tail->size = b->size - size - PS2_HEAP_HDR;
            tail->used = 0;
            b->next = tail;
            b->size = size;
        }
        b->used = 1;
        b->ra = site;
        s_heap_used += b->size;
        if (s_heap_used > s_heap_peak) s_heap_peak = s_heap_used;
        if (s_heap_census_mark && s_heap_used + PS2_HEAP_SIZE / 10u <= s_heap_census_mark)
            s_heap_census_done = 0;

        if (b->size >= PS2_HEAP_LOGLIM) {
            ps2_kprintf("[HEAP] +%u kB -> 0x%x used=%u kB peak=%u kB largest_free=%u kB\n",
                        (unsigned int)(b->size / 1024u), (unsigned int)(uintptr_t)(b + 1),
                        (unsigned int)(s_heap_used / 1024u), (unsigned int)(s_heap_peak / 1024u),
                        (unsigned int)(ps2_heap_largest_free() / 1024u));
        }
        return (void *)(b + 1);
    }

    {
        const size_t hole = ps2_heap_largest_free();

        s_heap_ooms++;
        s_heap_oom_site = site;
        s_heap_oom_size = size;
        ps2_kprintf("[HEAP] out of memory asking %u kB from 0x%x: used=%u kB in %u kB, largest free hole %u kB\n",
                    (unsigned int)(size / 1024u), site,
                    (unsigned int)(s_heap_used / 1024u),
                    (unsigned int)(PS2_HEAP_SIZE / 1024u),
                    (unsigned int)(hole / 1024u));
        if (!s_heap_census_done &&
            (s_heap_used >= (PS2_HEAP_SIZE / 4u) * 3u || hole < 64u * 1024u)) {
            s_heap_census_done = 1;
            ps2_heap_holders("at the first unservable ask");
        }
    }
    return NULL;
}

void* Imalloc(size_t size)
{
    return ps2_heap_take(size, __builtin_return_address(0));
}

void* Icalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = ps2_heap_take(total, __builtin_return_address(0));
    if (p) tkl_memset(p, 0, total);
    return p;
}

void Ifree(void *ptr)
{
    if (!ptr) return;

    for (ps2_blk *b = s_heap_list, *prev = NULL; b; prev = b, b = b->next) {
        if ((void *)(b + 1) != ptr) continue;
        if (!b->used) break;

        b->used = 0;
        s_heap_used -= b->size;
        if (b->size >= PS2_HEAP_LOGLIM) {
            ps2_kprintf("[HEAP] -%u kB at 0x%x returned, used=%u kB\n",
                        (unsigned int)(b->size / 1024u), (unsigned int)(uintptr_t)ptr,
                        (unsigned int)(s_heap_used / 1024u));
        }

        if (b->next && !b->next->used) {
            ps2_blk *n = b->next;
            b->size += PS2_HEAP_HDR + n->size;
            b->next = n->next;
        }
        if (prev && !prev->used) {
            prev->size += PS2_HEAP_HDR + b->size;
            prev->next = b->next;
        }
        return;
    }
    ps2_kprintf("[HEAP] free(0x%x) is not a pool block\n", (unsigned int)(uintptr_t)ptr);
}

void* malloc(size_t sz)
{
    return ps2_heap_take(sz, __builtin_return_address(0));
}

void* calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p = ps2_heap_take(total, __builtin_return_address(0));
    if (p) tkl_memset(p, 0, total);
    return p;
}

void free(void *p)
{
    Ifree(p);
}

void *Irealloc(void *ptr, size_t size)
{
    ps2_blk *b;
    void *n;
    size_t old;
    unsigned steps;

    if (!ptr)
        return ps2_heap_take(size, __builtin_return_address(0));
    if (size == 0) {
        Ifree(ptr);
        return NULL;
    }
    b = NULL;
    steps = 0;
    for (ps2_blk *c = s_heap_list; c && steps < PS2_HEAP_BLOCKS_MAX; c = c->next) {
        steps++;
        if ((void *)(c + 1) == ptr) {
            b = c;
            break;
        }
    }
    if (!b || !b->used)
        return NULL;
    old = b->size;
    if (size <= old)
        return ptr;
    n = ps2_heap_take(size, __builtin_return_address(0));
    if (!n)
        return NULL;
    tkl_memcpy(n, ptr, old);
    Ifree(ptr);
    return n;
}

void *realloc(void *ptr, size_t size)
{
    return Irealloc(ptr, size);
}
