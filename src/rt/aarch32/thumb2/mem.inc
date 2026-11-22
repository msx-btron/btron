/* Thumb-2 memcpy, memset, memcmp.
   thumb2/mem.S sets RT_SUF, RT_UNALIGNED, RT_PLD, and RT_WIDE.
   A multi-register store is used only when the destination is aligned.
   An unaligned word load is used only when RT_UNALIGNED is 1.
   LDM is never used on an unaligned address. PLD is M7 only.
   Opponents are named on the wrapper. M7 is not the M3 body. */
#define RT_CAT(a, b) a##b
#define RT_XCAT(a, b) RT_CAT(a, b)
#define RT_FN(name) RT_XCAT(rt_##name##_, RT_SUF)

.global RT_FN(memcpy)
.type RT_FN(memcpy), %function
.thumb_func
RT_FN(memcpy):
    cmp r2, #0
    bne 1f
    bx lr
1:
#if RT_WIDE
    push {r4-r11, lr}
#else
    push {r4-r6, lr}
#endif
    mov r12, r0
    eor r3, r0, r1
    tst r3, #3
    bne 2f
    b 3f
2:
#if RT_UNALIGNED
    b 4f
#else
    b 8f
#endif
3:
    cmp r2, #0
    beq 9f
    tst r0, #3
    beq 5f
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    subs r2, r2, #1
    b 3b
4:
    cmp r2, #0
    beq 9f
    tst r0, #3
    beq 6f
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    subs r2, r2, #1
    b 4b
5:
#if RT_WIDE
    cmp r2, #32
    blo 7f
10:
#if RT_PLD
    pld [r1, #64]
#endif
    ldm r1!, {r3-r10}
    stm r0!, {r3-r10}
    subs r2, r2, #32
    cmp r2, #32
    bhs 10b
#endif
7:
    cmp r2, #16
    blo 6f
11:
#if RT_PLD
    pld [r1, #32]
#endif
    ldm r1!, {r3-r6}
    stm r0!, {r3-r6}
    subs r2, r2, #16
    cmp r2, #16
    bhs 11b
6:
    cmp r2, #4
    blo 8f
12:
    ldr r3, [r1], #4
    str r3, [r0], #4
    subs r2, r2, #4
    cmp r2, #4
    bhs 12b
8:
    cmp r2, #0
    beq 9f
13:
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    subs r2, r2, #1
    bne 13b
9:
    mov r0, r12
#if RT_WIDE
    pop {r4-r11, pc}
#else
    pop {r4-r6, pc}
#endif

.global RT_FN(memset)
.type RT_FN(memset), %function
.thumb_func
RT_FN(memset):
    cmp r2, #0
    bne 1f
    bx lr
1:
#if RT_WIDE
    push {r4-r11, lr}
#else
    push {r4-r6, lr}
#endif
    mov r12, r0
    uxtb r1, r1
    orr r1, r1, r1, lsl #8
    orr r1, r1, r1, lsl #16
2:
    cmp r2, #0
    beq 9f
    tst r0, #3
    beq 3f
    strb r1, [r0], #1
    subs r2, r2, #1
    b 2b
3:
#if RT_WIDE
    cmp r2, #32
    blo 4f
    mov r3, r1
    mov r4, r1
    mov r5, r1
    mov r6, r1
    mov r7, r1
    mov r8, r1
    mov r9, r1
    mov r10, r1
5:
    stm r0!, {r3-r10}
    subs r2, r2, #32
    cmp r2, #32
    bhs 5b
#endif
4:
    cmp r2, #16
    blo 6f
    mov r3, r1
    mov r4, r1
    mov r5, r1
    mov r6, r1
7:
    stm r0!, {r3-r6}
    subs r2, r2, #16
    cmp r2, #16
    bhs 7b
6:
    cmp r2, #4
    blo 8f
10:
    str r1, [r0], #4
    subs r2, r2, #4
    cmp r2, #4
    bhs 10b
8:
    cmp r2, #0
    beq 9f
11:
    strb r1, [r0], #1
    subs r2, r2, #1
    bne 11b
9:
    mov r0, r12
#if RT_WIDE
    pop {r4-r11, pc}
#else
    pop {r4-r6, pc}
#endif

.global RT_FN(memcmp)
.type RT_FN(memcmp), %function
.thumb_func
RT_FN(memcmp):
    push {r4, lr}
1:
    cmp r2, #4
    blo 2f
    ldr r3, [r0], #4
    ldr r4, [r1], #4
    cmp r3, r4
    bne 3f
    subs r2, r2, #4
    b 1b
3:
    subs r0, r0, #4
    subs r1, r1, #4
    adds r2, r2, #4
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
.ltorg
