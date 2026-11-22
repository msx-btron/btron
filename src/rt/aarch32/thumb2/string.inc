/* Thumb-2 strings. RT_SUF comes from thumb2/string.S.
   strlen, strcmp, strcpy, and strchr scan a word at a time and do not
   store a word that contains a NUL. A word load is refused when the next
   four bytes would cross a 4 KiB page. Bounded copies are in rest.inc. */
#define RT_CAT(a, b) a##b
#define RT_XCAT(a, b) RT_CAT(a, b)
#define RT_FN(name) RT_XCAT(rt_##name##_, RT_SUF)

.global RT_FN(strlen)
.type RT_FN(strlen), %function
.thumb_func
RT_FN(strlen):
    push {r4-r6, lr}
    mov r4, r0
    ldr r5, =0x01010101
    ldr r6, =0x80808080
1:
    tst r0, #3
    beq 2f
    ldrb r3, [r0]
    cmp r3, #0
    beq 4f
    adds r0, r0, #1
    b 1b
2:
    ubfx r3, r0, #0, #12
    adds r3, r3, #3
    lsrs r3, r3, #12
    cmp r3, #0
    bhi 3f
    ldr r3, [r0]
    sub r1, r3, r5
    bic r1, r1, r3
    ands r1, r1, r6
    bne 3f
    adds r0, r0, #4
    b 2b
3:
    ldrb r3, [r0]
    cmp r3, #0
    beq 4f
    adds r0, r0, #1
    b 3b
4:
    subs r0, r0, r4
    pop {r4-r6, pc}
.ltorg

.global RT_FN(strcpy)
.type RT_FN(strcpy), %function
.thumb_func
RT_FN(strcpy):
    push {r4-r6, lr}
    mov r4, r0
    ldr r5, =0x01010101
    ldr r6, =0x80808080
1:
    eor r3, r0, r1
    tst r3, #3
    bne 3f
    tst r0, #3
    beq 2f
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    beq 4f
    b 1b
2:
    ubfx r3, r1, #0, #12
    adds r3, r3, #3
    lsrs r3, r3, #12
    cmp r3, #0
    bhi 3f
    ldr r3, [r1]
    sub r2, r3, r5
    bic r2, r2, r3
    ands r2, r2, r6
    bne 3f
    str r3, [r0], #4
    adds r1, r1, #4
    b 2b
3:
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    bne 3b
4:
    mov r0, r4
    pop {r4-r6, pc}
.ltorg

.global RT_FN(strcmp)
.type RT_FN(strcmp), %function
.thumb_func
RT_FN(strcmp):
    push {r4-r6, lr}
    ldr r5, =0x01010101
    ldr r6, =0x80808080
1:
    ubfx r3, r0, #0, #12
    adds r3, r3, #3
    lsrs r3, r3, #12
    cmp r3, #0
    bhi 2f
    ubfx r3, r1, #0, #12
    adds r3, r3, #3
    lsrs r3, r3, #12
    cmp r3, #0
    bhi 2f
    ldr r3, [r0]
    ldr r4, [r1]
    cmp r3, r4
    bne 2f
    sub r2, r3, r5
    bic r2, r2, r3
    ands r2, r2, r6
    bne 2f
    adds r0, r0, #4
    adds r1, r1, #4
    b 1b
2:
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne 3f
    cmp r3, #0
    bne 2b
    movs r0, #0
    pop {r4-r6, pc}
3:
    subs r0, r3, r4
    pop {r4-r6, pc}
.ltorg

.global RT_FN(strchr)
.type RT_FN(strchr), %function
.thumb_func
RT_FN(strchr):
    push {r4-r6, lr}
    uxtb r1, r1
    mov r4, r1
    orr r4, r4, r4, lsl #8
    orr r4, r4, r4, lsl #16
    ldr r5, =0x01010101
    ldr r6, =0x80808080
1:
    ubfx r3, r0, #0, #12
    adds r3, r3, #3
    lsrs r3, r3, #12
    cmp r3, #0
    bhi 2f
    tst r0, #3
    bne 2f
    ldr r3, [r0]
    eor r2, r3, r4
    sub r12, r3, r5
    bic r12, r12, r3
    ands r12, r12, r6
    bne 2f
    sub r12, r2, r5
    bic r12, r12, r2
    ands r12, r12, r6
    bne 2f
    adds r0, r0, #4
    b 1b
2:
    ldrb r3, [r0]
    cmp r3, r1
    beq 3f
    cmp r3, #0
    beq 4f
    adds r0, r0, #1
    b 2b
3:
    pop {r4-r6, pc}
4:
    movs r0, #0
    pop {r4-r6, pc}
.ltorg

#include "rest.inc"
