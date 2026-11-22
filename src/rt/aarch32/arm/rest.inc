/* ARM-state byte strings. RT_FN is set by the caller.
   Bounded copies stay on bytes. strcat and strscat call the scanners. */

.global RT_FN(strnlen)
.type RT_FN(strnlen), %function
RT_FN(strnlen):
    mov r2, #0
1:
    cmp r2, r1
    beq 2f
    ldrb r3, [r0, r2]
    cmp r3, #0
    beq 2f
    add r2, r2, #1
    b 1b
2:
    mov r0, r2
    bx lr

.global RT_FN(strncpy)
.type RT_FN(strncpy), %function
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
    add r1, r1, #1
    subs r2, r2, #1
    b 1b
2:
    mov r3, #0
    strb r3, [r0], #1
    subs r2, r2, #1
    b 1b
3:
    mov r0, r4
    pop {r4, pc}

.global RT_FN(strcat)
.type RT_FN(strcat), %function
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
RT_FN(strncat):
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    bl RT_FN(strlen)
    add r7, r4, r0
    mov r3, #0
1:
    cmp r3, r6
    beq 2f
    ldrb r2, [r5, r3]
    cmp r2, #0
    beq 2f
    strb r2, [r7, r3]
    add r3, r3, #1
    b 1b
2:
    mov r2, #0
    strb r2, [r7, r3]
    mov r0, r4
    pop {r4-r7, pc}

.global RT_FN(strncmp)
.type RT_FN(strncmp), %function
RT_FN(strncmp):
    cmp r2, #0
    beq 3f
1:
    ldrb r3, [r0], #1
    ldrb ip, [r1], #1
    subs r2, r2, #1
    cmp r3, ip
    bne 2f
    cmp r3, #0
    beq 3f
    cmp r2, #0
    bne 1b
3:
    mov r0, #0
    bx lr
2:
    sub r0, r3, ip
    bx lr

.global RT_FN(strrchr)
.type RT_FN(strrchr), %function
RT_FN(strrchr):
    uxtb r1, r1
    mov r2, #0
1:
    ldrb r3, [r0]
    cmp r3, r1
    moveq r2, r0
    cmp r3, #0
    beq 2f
    add r0, r0, #1
    b 1b
2:
    mov r0, r2
    bx lr

.global RT_FN(strstr)
.type RT_FN(strstr), %function
RT_FN(strstr):
    ldrb r3, [r1]
    cmp r3, #0
    bxeq lr
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
1:
    ldrb r3, [r4]
    cmp r3, #0
    beq 4f
    ldrb r6, [r5]
    cmp r3, r6
    bne 3f
    mov r0, r4
    mov r1, r5
2:
    ldrb r6, [r1], #1
    cmp r6, #0
    beq 5f
    ldrb r7, [r0], #1
    cmp r7, r6
    bne 3f
    b 2b
5:
    mov r0, r4
    pop {r4-r7, pc}
3:
    add r4, r4, #1
    b 1b
4:
    mov r0, #0
    pop {r4-r7, pc}

.global RT_FN(strpbrk)
.type RT_FN(strpbrk), %function
RT_FN(strpbrk):
    ldrb r3, [r1]
    cmp r3, #0
    moveq r0, #0
    bxeq lr
    push {r4-r6, lr}
    mov r4, r0
    mov r5, r1
1:
    ldrb r6, [r4]
    cmp r6, #0
    beq 4f
    mov r1, r5
2:
    ldrb r3, [r1], #1
    cmp r3, #0
    beq 3f
    cmp r6, r3
    beq 5f
    b 2b
3:
    add r4, r4, #1
    b 1b
4:
    mov r0, #0
    pop {r4-r6, pc}
5:
    mov r0, r4
    pop {r4-r6, pc}

.global RT_FN(strscpy)
.type RT_FN(strscpy), %function
RT_FN(strscpy):
    cmp r2, #0
    bne 1f
    mov r0, r1
    b RT_FN(strlen)
1:
    push {r4-r7, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r0, r1
    bl RT_FN(strlen)
    mov r7, r0
    sub r6, r6, #1
    cmp r7, r6
    bhi 2f
    mov r6, r7
2:
    mov r0, r4
    mov r1, r5
    mov r2, r6
    bl RT_FN(memcpy)
    mov r3, #0
    strb r3, [r4, r6]
    mov r0, r7
    pop {r4-r7, pc}

.global RT_FN(strscat)
.type RT_FN(strscat), %function
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
RT_FN(strpad):
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
    sub r3, r6, r2
    add r0, r4, r2
    mov r1, r7
    mov r2, r3
    bl RT_FN(memset)
1:
    pop {r4-r7, pc}
