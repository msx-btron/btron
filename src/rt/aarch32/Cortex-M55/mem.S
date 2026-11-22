/* ARMv8.1-M Helium for Cortex-M55 and Cortex-M85. STM32N6.
   Tail-predicated VLDRB/VSTRB copies the last partial vector
   without a byte epilogue. q0-q3 only.
   CPACR must allow CP10 and CP11 before the first MVE instruction.
   The bench startup does that for this image.
   No published Helium memcpy, memset, or strlen in Arm Optimized
   Routines, picolibc, newlib, or LLVM libc. libc stays N/A except
   picolibc strnlen.c when that C file links.
   https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/string/strnlen.c
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.cpu cortex-m55
.thumb

.global rt_memcpy_m55
.type rt_memcpy_m55, %function
.thumb_func
rt_memcpy_m55:
    cmp r2, #0
    bne 1f
    bx lr
1:
    push {lr}
    mov r3, r0
    wlstp.8 lr, r2, 2f
3:
    vldrb.u8 q0, [r1], #16
    vstrb.8 q0, [r0], #16
    letp lr, 3b
2:
    mov r0, r3
    pop {pc}

.global rt_memset_m55
.type rt_memset_m55, %function
.thumb_func
rt_memset_m55:
    cmp r2, #0
    bne 1f
    bx lr
1:
    push {lr}
    mov r3, r0
    vdup.8 q0, r1
    wlstp.8 lr, r2, 2f
3:
    vstrb.8 q0, [r0], #16
    letp lr, 3b
2:
    mov r0, r3
    pop {pc}

.global rt_memcmp_m55
.type rt_memcmp_m55, %function
.thumb_func
rt_memcmp_m55:
    push {r4, lr}
1:
    cmp r2, #16
    blo 2f
    vldrb.u8 q0, [r0]
    vldrb.u8 q1, [r1]
    veor q2, q0, q1
    vmaxv.u8 r3, q2
    cmp r3, #0
    bne 2f
    adds r0, r0, #16
    adds r1, r1, #16
    subs r2, r2, #16
    b 1b
2:
    cmp r2, #0
    beq 4f
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    subs r2, r2, #1
    cmp r3, r4
    beq 2b
    subs r0, r3, r4
    pop {r4, pc}
4:
    movs r0, #0
    pop {r4, pc}
