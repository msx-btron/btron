/* AArch32 Advanced SIMD. One object for every core that executes
   AArch32 NEON: Cortex-A5, A7, A8, A9, A15, and A53 or A72 in AArch32.
   Pi 2 (both revisions), Pi 3, Pi 3A+, Pi 3B+, Zero 2 W, CM3,
   Pi 4, Pi 400, CM4, and STM32MP15. Pi 5 is AArch64 only.
   Symbols are rt_*_a32. rt_*_neon is the AArch64 kernel.
   VLD1 and VST1 accept an unaligned address. d0-d7 are caller-saved.
   memcpy: https://github.com/ARM-software/optimized-routines/blob/v25.01/string/arm/memcpy.S
   memset: https://github.com/ARM-software/optimized-routines/blob/v25.01/string/arm/memset.S
   That tag has no memcmp. glibc's armv7 IFUNC selects the same family:
   https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/arm/armv7/multiarch/memcpy.S
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.cpu cortex-a7
.arch armv7-a
.fpu neon-vfpv4
.arm

.global rt_memcpy_a32
.type rt_memcpy_a32, %function
rt_memcpy_a32:
    cmp r2, #0
    bxeq lr
    mov ip, r0
.Lmc64:
    cmp r2, #64
    blo .Lmc32
    vld1.8 {d0-d3}, [r1]!
    vld1.8 {d4-d7}, [r1]!
    vst1.8 {d0-d3}, [r0]!
    vst1.8 {d4-d7}, [r0]!
    subs r2, r2, #64
    b .Lmc64
.Lmc32:
    cmp r2, #32
    blo .Lmc16
    vld1.8 {d0-d3}, [r1]!
    vst1.8 {d0-d3}, [r0]!
    subs r2, r2, #32
.Lmc16:
    cmp r2, #16
    blo .Lmcb
    vld1.8 {d0-d1}, [r1]!
    vst1.8 {d0-d1}, [r0]!
    subs r2, r2, #16
    b .Lmc16
.Lmcb:
    cmp r2, #0
    beq .Lmcd
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    subs r2, r2, #1
    b .Lmcb
.Lmcd:
    mov r0, ip
    bx lr

.global rt_memset_a32
.type rt_memset_a32, %function
rt_memset_a32:
    cmp r2, #0
    bxeq lr
    mov ip, r0
    vdup.8 q0, r1
    vdup.8 q1, r1
    vdup.8 q2, r1
    vdup.8 q3, r1
.Lms64:
    cmp r2, #64
    blo .Lms32
    vst1.8 {d0-d3}, [r0]!
    vst1.8 {d4-d7}, [r0]!
    subs r2, r2, #64
    b .Lms64
.Lms32:
    cmp r2, #32
    blo .Lms16
    vst1.8 {d0-d3}, [r0]!
    subs r2, r2, #32
.Lms16:
    cmp r2, #16
    blo .Lmsb
    vst1.8 {d0-d1}, [r0]!
    subs r2, r2, #16
    b .Lms16
.Lmsb:
    cmp r2, #0
    beq .Lmsd
    strb r1, [r0], #1
    subs r2, r2, #1
    b .Lmsb
.Lmsd:
    mov r0, ip
    bx lr

.global rt_memcmp_a32
.type rt_memcmp_a32, %function
rt_memcmp_a32:
    cmp r2, #16
    blo .Lmpb
.Lmp16:
    vld1.8 {q0}, [r0]!
    vld1.8 {q1}, [r1]!
    veor q2, q0, q1
    vorr d4, d4, d5
    vmov r3, ip, d4
    orrs r3, r3, ip
    bne .Lmpr
    subs r2, r2, #16
    cmp r2, #16
    bhs .Lmp16
.Lmpb:
    cmp r2, #0
    beq .Lmp0
    ldrb r3, [r0], #1
    ldrb ip, [r1], #1
    subs r2, r2, #1
    cmp r3, ip
    beq .Lmpb
    sub r0, r3, ip
    bx lr
.Lmpr:
    sub r0, r0, #16
    sub r1, r1, #16
    b .Lmpb
.Lmp0:
    mov r0, #0
    bx lr
