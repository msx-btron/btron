/* Thumb-2 strings. One kernel for Cortex-M3, M4, M7, M33, and M35P.
   The guest passes -DRT_SUF. The integer routines do not use PLD or DSP.
   strlen: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/strlen-thumb2-Os.S
   strcmp: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/strcmp-armv7m.S
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.thumb
#ifndef RT_SUF
#error RT_SUF required
#endif
#include "string.inc"
