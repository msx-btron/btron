#ifndef RT_MEM_H
#define RT_MEM_H

#include <stddef.h>

/* Non-overlapping, like ISO C memcpy. Overlap is undefined.
   memset converts c to unsigned char. memcmp compares unsigned bytes.
   Every routine returns dst (memcpy/memset) or the usual <0/0/>0 (memcmp).

   Tiers (pick one at compile time, or probe once):
     scalar     portable C99, word-at-a-time
     i386       80386 REP MOVSD/STOSD (best general RAM copy on 386/486)
     pc98       same RAM path, plus 16-bit VRAM path (MOVSW/STOSW)
     pc98 real  .code16 helpers, BIOS register convention, not the ELF ABI
     mmx        Pentium MMX movq, EMMS on every exit that touched MMX
     sse        i386 XMM movups/movaps (Pentium III; no SSE2 integer ops)
     sse2       x86-64, 16-byte, dest-aligned movdqa
     avx        x86-64, 32-byte, AVX1, vzeroupper on every return
     avx2       x86-64, 32-byte, vzeroupper on every return
     avx512     x86-64, 64-byte ZMM, AVX-512F+BW; leaves ZMM state dirty
     neon/sve   AArch64
     sme        Apple SME2. memcpy, memmove, memset, memcmp.
                The proved neon memset stays separate.
     rvv        RV32 or RV64 V, m8 e8 (one object per XLEN).
                memcpy, memset, memcmp. memmove stays scalar.
     rv32       RV32 integer. lw/sw only when both pointers are aligned.
                memcpy, memmove, memset, memcmp. No vector opcodes.
     rv64       RV64 integer. ld/sd only when both pointers are aligned.
                memcpy, memmove, memset, memcmp. No vector opcodes.
                Do not link the rv32 object with the rv64 object.
     ee         PlayStation 2 R5900. LQ/SQ and the MMI subset.
                memcpy, memset, memcmp. memmove stays scalar.
     rcp        Nintendo 64 VR4300. ld/sd, not RSP microcode.
                memcpy, memset, memcmp. memmove stays scalar.
     arm1176    ARMv6 ARM state. Pi 1, Pi Zero, CM1. No NEON.
     a32        AArch32 Advanced SIMD. Pi 2, Pi 3, Zero 2, Pi 4,
                STM32MP15. Not Pi 5. Not the AArch64 rt_*_neon symbols.
     m0         ARMv6-M Thumb-1. Cortex-M0, M0+, M1, M23.
                STM32C0, F0, G0, L0, U0, WB0.
     m3         ARMv7-M Thumb-2. STM32F1, F2, L1.
     m4         ARMv7E-M integer. STM32F3, F4, G4, L4, L4+, WB, WL,
                and the M4 core of H7. DSP is not an ordered memcmp.
     m7         ARMv7E-M with PLD and a 32-byte copy. STM32F7, H7 M7.
     m33        ARMv8-M Mainline integer. STM32H5, L5, U5, U3, WBA, C5.
     m55        ARMv8.1-M Helium. STM32N6.
                memmove stays scalar on every AArch32 kernel.
*/

void *rt_memcpy_scalar(void *dst, const void *src, size_t n);
void *rt_memmove_scalar(void *dst, const void *src, size_t n);
void *rt_memset_scalar(void *dst, int c, size_t n);
int   rt_memcmp_scalar(const void *a, const void *b, size_t n);

void *rt_memcpy_i386(void *dst, const void *src, size_t n);
void *rt_memset_i386(void *dst, int c, size_t n);
int   rt_memcmp_i386(const void *a, const void *b, size_t n);

void *rt_memcpy_pc98(void *dst, const void *src, size_t n);
void *rt_memset_pc98(void *dst, int c, size_t n);
int   rt_memcmp_pc98(const void *a, const void *b, size_t n);
void *rt_memcpy_pc98_vram(void *dst, const void *src, size_t n);
void *rt_memset_pc98_vram(void *dst, int c, size_t n);

void *rt_memcpy_mmx(void *dst, const void *src, size_t n);
void *rt_memset_mmx(void *dst, int c, size_t n);
int   rt_memcmp_mmx(const void *a, const void *b, size_t n);

void *rt_memcpy_sse(void *dst, const void *src, size_t n);
void *rt_memset_sse(void *dst, int c, size_t n);
int   rt_memcmp_sse(const void *a, const void *b, size_t n);

void *rt_memcpy_sse2(void *dst, const void *src, size_t n);
void *rt_memset_sse2(void *dst, int c, size_t n);
int   rt_memcmp_sse2(const void *a, const void *b, size_t n);

void *rt_memcpy_avx(void *dst, const void *src, size_t n);
void *rt_memset_avx(void *dst, int c, size_t n);
int   rt_memcmp_avx(const void *a, const void *b, size_t n);

void *rt_memcpy_avx2(void *dst, const void *src, size_t n);
void *rt_memset_avx2(void *dst, int c, size_t n);
int   rt_memcmp_avx2(const void *a, const void *b, size_t n);

void *rt_memcpy_avx512(void *dst, const void *src, size_t n);
void *rt_memset_avx512(void *dst, int c, size_t n);
int   rt_memcmp_avx512(const void *a, const void *b, size_t n);

void *rt_memcpy_neon(void *dst, const void *src, size_t n);
void *rt_memset_neon(void *dst, int c, size_t n);
int   rt_memcmp_neon(const void *a, const void *b, size_t n);

void *rt_memcpy_sve(void *dst, const void *src, size_t n);
void *rt_memset_sve(void *dst, int c, size_t n);
int   rt_memcmp_sve(const void *a, const void *b, size_t n);

void *rt_memcpy_sme(void *dst, const void *src, size_t n);
void *rt_memmove_sme(void *dst, const void *src, size_t n);
void *rt_memset_sme(void *dst, int c, size_t n);
int   rt_memcmp_sme(const void *a, const void *b, size_t n);

void *rt_memcpy_rvv(void *dst, const void *src, size_t n);
void *rt_memset_rvv(void *dst, int c, size_t n);
int   rt_memcmp_rvv(const void *a, const void *b, size_t n);

void *rt_memcpy_rv32(void *dst, const void *src, size_t n);
void *rt_memmove_rv32(void *dst, const void *src, size_t n);
void *rt_memset_rv32(void *dst, int c, size_t n);
int   rt_memcmp_rv32(const void *a, const void *b, size_t n);

void *rt_memcpy_rv64(void *dst, const void *src, size_t n);
void *rt_memmove_rv64(void *dst, const void *src, size_t n);
void *rt_memset_rv64(void *dst, int c, size_t n);
int   rt_memcmp_rv64(const void *a, const void *b, size_t n);

void *rt_memcpy_ee(void *dst, const void *src, size_t n);
void *rt_memset_ee(void *dst, int c, size_t n);
int   rt_memcmp_ee(const void *a, const void *b, size_t n);

void *rt_memcpy_rcp(void *dst, const void *src, size_t n);
void *rt_memset_rcp(void *dst, int c, size_t n);
int   rt_memcmp_rcp(const void *a, const void *b, size_t n);

void *rt_memcpy_arm1176(void *dst, const void *src, size_t n);
void *rt_memset_arm1176(void *dst, int c, size_t n);
int   rt_memcmp_arm1176(const void *a, const void *b, size_t n);

void *rt_memcpy_a32(void *dst, const void *src, size_t n);
void *rt_memset_a32(void *dst, int c, size_t n);
int   rt_memcmp_a32(const void *a, const void *b, size_t n);

void *rt_memcpy_m0(void *dst, const void *src, size_t n);
void *rt_memset_m0(void *dst, int c, size_t n);
int   rt_memcmp_m0(const void *a, const void *b, size_t n);

void *rt_memcpy_m3(void *dst, const void *src, size_t n);
void *rt_memset_m3(void *dst, int c, size_t n);
int   rt_memcmp_m3(const void *a, const void *b, size_t n);

void *rt_memcpy_m4(void *dst, const void *src, size_t n);
void *rt_memset_m4(void *dst, int c, size_t n);
int   rt_memcmp_m4(const void *a, const void *b, size_t n);

void *rt_memcpy_m7(void *dst, const void *src, size_t n);
void *rt_memset_m7(void *dst, int c, size_t n);
int   rt_memcmp_m7(const void *a, const void *b, size_t n);

void *rt_memcpy_m33(void *dst, const void *src, size_t n);
void *rt_memset_m33(void *dst, int c, size_t n);
int   rt_memcmp_m33(const void *a, const void *b, size_t n);

void *rt_memcpy_m55(void *dst, const void *src, size_t n);
void *rt_memset_m55(void *dst, int c, size_t n);
int   rt_memcmp_m55(const void *a, const void *b, size_t n);

#endif
