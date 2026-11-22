/* Thumb-2 memory. One kernel for Cortex-M3, M4, M7, M33, and M35P.
   The guest passes -DRT_SUF, -DRT_PLD, and -DRT_WIDE. -mcpu selects
   the core the assembler checks. M3, M4, M33, and M35P use
   RT_PLD 0 and RT_WIDE 0. M7 uses RT_PLD 1 and RT_WIDE 1.
   pld is UNDEFINED on Cortex-M3 and Cortex-M4, so those guests
   must not set RT_PLD. Unaligned LDR is legal when CCR.UNALIGN_TRP
   is clear. LDM stays aligned.
   memcpy: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/memcpy-armv7m.S
   memcpy: https://github.com/NuttX/nuttx/blob/6b31918b429e6b2351bfa03f005704d123cb70e9/libs/libc/machine/arm/armv7-m/gnu/arch_memcpy.S
   memcpy: https://github.com/llvm/llvm-project/blob/a080750a1e8b5b93f7a93dd6df88ce9b1e3a4fe0/libc/src/string/memory_utils/arm/inline_memcpy.h
   memset: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/memset-thumb2.S
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.thumb
#ifndef RT_SUF
#error RT_SUF required
#endif
#ifndef RT_PLD
#define RT_PLD 0
#endif
#ifndef RT_WIDE
#define RT_WIDE 0
#endif
#define RT_UNALIGNED 1
#include "mem.inc"
