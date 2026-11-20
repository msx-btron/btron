/* Freestanding stdlib.h shim for PicoCalc TinyGL */
#ifndef _PICO_SHIM_STDLIB_H_
#define _PICO_SHIM_STDLIB_H_
#include <stddef.h>
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void  free(void *ptr);
void  exit(int status);
void  abort(void);
static inline int abs(int x) { return x < 0 ? -x : x; }
#endif
