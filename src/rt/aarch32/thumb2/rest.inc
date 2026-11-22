/* Byte strings. A bounded copy does not read past a NUL and does not
   write past n. strcat and strscat call the word scanners above. */

.global RT_FN(strnlen)
.type RT_FN(strnlen), %function
.thumb_func
RT_FN(strnlen):
    push {r4, lr}
    movs r2, #0
1:
    cmp r2, r1
    beq 2f
    ldrb r3, [r0, r2]
    cmp r3, #0
    beq 2f
    adds r2, r2, #1
    b 1b
2:
    mov r0, r2
    pop {r4, pc}

.global RT_FN(strncpy)
.type RT_FN(strncpy), %function
.thumb_func
RT_FN(strncpy):
    push {r4, lr}
    mov r4, r0
1:
    cmp r2, #0
    beq 3f
    ldrb r3, [r1]
    cmp r3, #0
    beq 2f
    strb r3, [r0], #1
    adds r1, r1, #1
    subs r2, r2, #1
    b 1b
2:
    movs r3, #0
    strb r3, [r0], #1
    subs r2, r2, #1
    b 1b
3:
    mov r0, r4
    pop {r4, pc}

.global RT_FN(strcat)
.type RT_FN(strcat), %function
.thumb_func
RT_FN(strcat):
    push {r4, r5, lr}
    mov r4, r0
    mov r5, r1
    bl RT_FN(strlen)
    add r0, r4, r0
    mov r1, r5
    bl RT_FN(strcpy)
    mov r0, r4
    pop {r4, r5, pc}

.global RT_FN(strncat)
.type RT_FN(strncat), %function
.thumb_func
RT_FN(strncat):
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl RT_FN(strlen)
    add r7, r4, r0
    movs r3, #0
1:
    cmp r3, r6
    beq 2f
    ldrb r2, [r5, r3]
    cmp r2, #0
    beq 2f
    strb r2, [r7, r3]
    adds r3, r3, #1
    b 1b
2:
    movs r2, #0
    strb r2, [r7, r3]
    mov r0, r4
    pop {r4-r7, pc}

.global RT_FN(strncmp)
.type RT_FN(strncmp), %function
.thumb_func
RT_FN(strncmp):
    push {r4, lr}
    cmp r2, #0
    beq 3f
1:
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    subs r2, r2, #1
    cmp r3, r4
    bne 2f
    cmp r3, #0
    beq 3f
    cmp r2, #0
    bne 1b
3:
    movs r0, #0
    pop {r4, pc}
2:
    subs r0, r3, r4
    pop {r4, pc}

.global RT_FN(strrchr)
.type RT_FN(strrchr), %function
.thumb_func
RT_FN(strrchr):
    push {r4, lr}
    uxtb r1, r1
    movs r2, #0
1:
    ldrb r3, [r0]
    cmp r3, r1
    bne 2f
    mov r2, r0
2:
    cmp r3, #0
    beq 3f
    adds r0, r0, #1
    b 1b
3:
    mov r0, r2
    pop {r4, pc}

.global RT_FN(strstr)
.type RT_FN(strstr), %function
.thumb_func
RT_FN(strstr):
    ldrb r3, [r1]
    cmp r3, #0
    bne 1f
    bx lr
1:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
2:
    ldrb r3, [r4]
    cmp r3, #0
    beq 6f
    ldrb r6, [r5]
    cmp r3, r6
    bne 5f
    mov r0, r4
    mov r1, r5
3:
    ldrb r6, [r1], #1
    cmp r6, #0
    beq 4f
    ldrb r7, [r0], #1
    cmp r7, r6
    bne 5f
    b 3b
4:
    mov r0, r4
    pop {r4-r7, pc}
5:
    adds r4, r4, #1
    b 2b
6:
    movs r0, #0
    pop {r4-r7, pc}

.global RT_FN(strpbrk)
.type RT_FN(strpbrk), %function
.thumb_func
RT_FN(strpbrk):
    ldrb r3, [r1]
    cmp r3, #0
    bne 1f
    movs r0, #0
    bx lr
1:
    push {r4-r6, lr}
    mov r4, r0
    mov r5, r1
2:
    ldrb r6, [r4]
    cmp r6, #0
    beq 5f
    mov r1, r5
3:
    ldrb r3, [r1], #1
    cmp r3, #0
    beq 4f
    cmp r6, r3
    beq 6f
    b 3b
4:
    adds r4, r4, #1
    b 2b
5:
    movs r0, #0
    pop {r4-r6, pc}
6:
    mov r0, r4
    pop {r4-r6, pc}

.global RT_FN(strscpy)
.type RT_FN(strscpy), %function
.thumb_func
RT_FN(strscpy):
    cmp r2, #0
    bne 1f
    mov r0, r1
    b RT_FN(strlen)
1:
    /* src is a C string. Copy min(strlen, n-1) with the wide memcpy,
       then one NUL. The return is the full length. */
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r0, r1
    bl RT_FN(strlen)
    mov r7, r0
    subs r6, r6, #1
    cmp r7, r6
    bhi 2f
    mov r6, r7
2:
    mov r0, r4
    mov r1, r5
    mov r2, r6
    bl RT_FN(memcpy)
    movs r3, #0
    strb r3, [r4, r6]
    mov r0, r7
    pop {r4-r7, pc}

.global RT_FN(strscat)
.type RT_FN(strscat), %function
.thumb_func
RT_FN(strscat):
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r1, r2
    bl RT_FN(strnlen)
    mov r7, r0
    cmp r7, r6
    blo 1f
    mov r0, r5
    bl RT_FN(strlen)
    add r0, r7, r0
    pop {r4-r7, pc}
1:
    add r0, r4, r7
    mov r1, r5
    sub r2, r6, r7
    bl RT_FN(strscpy)
    add r0, r7, r0
    pop {r4-r7, pc}

.global RT_FN(strpad)
.type RT_FN(strpad), %function
.thumb_func
RT_FN(strpad):
    /* Bounded scan, then the wide copy and the wide fill.
       No extra NUL when src is longer than width. */
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    uxtb r7, r3
    mov r0, r5
    mov r1, r6
    bl RT_FN(strnlen)
    mov r2, r0
    push {r2}
    mov r0, r4
    mov r1, r5
    bl RT_FN(memcpy)
    pop {r2}
    cmp r2, r6
    bhs 1f
    subs r3, r6, r2
    adds r0, r4, r2
    mov r1, r7
    mov r2, r3
    bl RT_FN(memset)
1:
    pop {r4-r7, pc}
.ltorg
