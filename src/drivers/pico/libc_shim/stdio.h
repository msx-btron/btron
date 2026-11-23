/* Freestanding stdio.h shim for PicoCalc TinyGL */
#ifndef _PICO_SHIM_STDIO_H_
#define _PICO_SHIM_STDIO_H_
#include <stddef.h>
#include <stdarg.h>
typedef struct _PICO_FILE FILE;
extern FILE *stderr;
extern FILE *stdout;
static inline int printf(const char *fmt, ...) { (void)fmt; return 0; } /* flawfinder: ignore */
static inline int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; } /* flawfinder: ignore */
static inline int vfprintf(FILE *f, const char *fmt, va_list ap) { (void)f; (void)fmt; (void)ap; return 0; } /* flawfinder: ignore */
#endif
