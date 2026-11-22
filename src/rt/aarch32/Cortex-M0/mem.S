/* ARMv6-M Thumb-1. Cortex-M0, M0+, M1, and ARMv8-M Baseline M23.
   No unaligned access, no CBZ, no wide post-index. A word load is
   used only when the address is aligned. STM32C0, F0, G0, L0, U0, WB0.
   Opponents are under third_party/aarch32/, indexed by SOURCES.md.
   memcpy: https://github.com/visenri/memcpy_armv6m/blob/2dc57cc1d5944f181db11a0491dba5a00f7c350f/memops_opt/memcpy_armv6m.S
   memcpy: https://github.com/llvm/llvm-project/blob/a080750a1e8b5b93f7a93dd6df88ce9b1e3a4fe0/libc/src/string/memory_utils/arm/inline_memcpy.h
   memset: https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/machine/arm/memset-thumb.S
   picolibc memcpy.S has no Thumb-1 memcpy. */
.syntax unified
.cpu cortex-m0
.thumb

.global rt_memcpy_m0
.type rt_memcpy_m0, %function
.thumb_func
rt_memcpy_m0:
    cmp r2, #0
    bne 1f
    bx lr
1:
    push {r4-r7, lr}
    mov r4, r0
    mov r3, r0
    eors r3, r1
    movs r5, #3
    ands r3, r5
    bne 8f
3:
    cmp r2, #0
    beq 9f
    mov r3, r0
    ands r3, r5
    beq 5f
    ldrb r3, [r1]
    strb r3, [r0]
    adds r0, r0, #1
    adds r1, r1, #1
    subs r2, r2, #1
    b 3b
5:
    cmp r2, #16
    blo 6f
4:
    ldm r1!, {r3, r5, r6, r7}
    stm r0!, {r3, r5, r6, r7}
    subs r2, r2, #16
    cmp r2, #16
    bhs 4b
6:
    cmp r2, #4
    blo 8f
7:
    ldr r3, [r1]
    str r3, [r0]
    adds r0, r0, #4
    adds r1, r1, #4
    subs r2, r2, #4
    cmp r2, #4
    bhs 7b
8:
    cmp r2, #0
    beq 9f
    ldrb r3, [r1]
    strb r3, [r0]
    adds r0, r0, #1
    adds r1, r1, #1
    subs r2, r2, #1
    bne 8b
9:
    mov r0, r4
    pop {r4-r7, pc}

.global rt_memset_m0
.type rt_memset_m0, %function
.thumb_func
rt_memset_m0:
    cmp r2, #0
    bne 1f
    bx lr
1:
    push {r4-r7, lr}
    mov r4, r0
    uxtb r1, r1
    lsls r3, r1, #8
    orrs r1, r3
    lsls r3, r1, #16
    orrs r1, r3
    movs r5, #3
2:
    cmp r2, #0
    beq 9f
    mov r3, r0
    ands r3, r5
    beq 3f
    strb r1, [r0]
    adds r0, r0, #1
    subs r2, r2, #1
    b 2b
3:
    cmp r2, #16
    blo 6f
    mov r3, r1
    mov r6, r1
    mov r7, r1
4:
    stm r0!, {r1, r3, r6, r7}
    subs r2, r2, #16
    cmp r2, #16
    bhs 4b
6:
    cmp r2, #4
    blo 8f
7:
    str r1, [r0]
    adds r0, r0, #4
    subs r2, r2, #4
    cmp r2, #4
    bhs 7b
8:
    cmp r2, #0
    beq 9f
    strb r1, [r0]
    adds r0, r0, #1
    subs r2, r2, #1
    bne 8b
9:
    mov r0, r4
    pop {r4-r7, pc}

.global rt_memcmp_m0
.type rt_memcmp_m0, %function
.thumb_func
rt_memcmp_m0:
    push {r4, lr}
    cmp r2, #0
    beq 4f
    movs r3, #3
    ands r3, r0
    bne 2f
    movs r3, #3
    ands r3, r1
    bne 2f
1:
    cmp r2, #4
    blo 2f
    ldr r3, [r0]
    ldr r4, [r1]
    cmp r3, r4
    bne 2f
    adds r0, r0, #4
    adds r1, r1, #4
    subs r2, r2, #4
    b 1b
2:
    cmp r2, #0
    beq 4f
    ldrb r3, [r0]
    ldrb r4, [r1]
    adds r0, r0, #1
    adds r1, r1, #1
    subs r2, r2, #1
    cmp r3, r4
    beq 2b
    subs r0, r3, r4
    pop {r4, pc}
4:
    movs r0, #0
    pop {r4, pc}
