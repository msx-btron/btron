/* Freestanding string.h shim for PicoCalc TinyGL */
#ifndef _PICO_SHIM_STRING_H_
#define _PICO_SHIM_STRING_H_
#include <stddef.h>
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
#endif
