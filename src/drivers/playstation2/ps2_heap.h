/*
 * ps2_heap.h - PlayStation 2 Cleanroom Kernel Heap Allocator
 *
 * Implements a first-fit RDRAM allocator with block coalescing, reentrant tolerance,
 * and memory owner telemetry for B-System on PS2.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_HEAP_H
#define PS2_HEAP_H

#include <stddef.h>
#include <stdint.h>

#if defined(BTRON_PS2_MSX) && (BTRON_PS2_MSX)
#define PS2_HEAP_SIZE (7500 * 1024)
#else
#define PS2_HEAP_SIZE (11 * 1024 * 1024)
#endif

void *Imalloc(size_t size);
void  Ifree(void *ptr);
void *Icalloc(size_t nmemb, size_t size);
void *Irealloc(void *ptr, size_t size);

void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void   ps2_heap_holders(const char *why);
size_t ps2_heap_largest_hole(void);
size_t ps2_heap_used_bytes(void);
size_t ps2_heap_peak_bytes(void);

#if defined(BTRON_PS2_PROBE) || defined(BTRON_PS2_BENCH)
#define PS2_HEAP_ALIGN  16u
#define PS2_HEAP_HDR    16u
#define PS2_HEAP_BLOCKS_MAX (PS2_HEAP_SIZE / (PS2_HEAP_HDR + PS2_HEAP_ALIGN))

typedef struct ps2_blk {
    struct ps2_blk *next;
    size_t          size;
    unsigned        used;
    unsigned        ra;
} ps2_blk;

extern uint8_t s_ps2_heap[PS2_HEAP_SIZE];
extern ps2_blk *s_heap_list;
#endif

#endif /* PS2_HEAP_H */
