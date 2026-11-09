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
#  if defined(BTRON_PS2_QUAKE) && (BTRON_PS2_QUAKE)
#    include <btron/ps2_tile.h>
     /* Live working set only.  PAK/ROMs stay on the disc (CD/DVD). */
#    define PS2_HEAP_SIZE (PS2_BUDGET_HEAP_KB * 1024)
#  else
#    define PS2_HEAP_SIZE (7500 * 1024)
#  endif
#elif defined(BTRON_PS2_QUAKE) && (BTRON_PS2_QUAKE)
#define PS2_HEAP_SIZE (9 * 1024 * 1024)
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

#endif /* PS2_HEAP_H */
