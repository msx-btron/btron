/* ARMv6 ARM-state strings. A word is loaded only when it is aligned
   and the four bytes stay inside one 4 KiB page. A word is stored
   only when it contains no NUL.
   Arm Optimized Routines memset, strcmp, and strlen at v25.01 are
   Thumb-2 and are illegal on ARM1176, so those libc cells stay N/A.
   https://github.com/ARM-software/optimized-routines/tree/v25.01/string/arm
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.arch armv6
.cpu arm1176jzf-s
.arm
#define RT_FN(name) rt_##name##_arm1176

.global RT_FN(strlen)
.type RT_FN(strlen), %function
RT_FN(strlen):
    push {r4-r6, lr}
    mov r4, r0
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lla:
    tst r0, #3
    beq .Llw
    ldrb r3, [r0]
    cmp r3, #0
    beq .Lld
    add r0, r0, #1
    b .Lla
.Llw:
    mov r3, r0, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #3
    movs r3, r3, lsr #12
    bne .Llb
    ldr r3, [r0]
    sub r1, r3, r5
    bic r1, r1, r3
    tst r1, r6
    bne .Llb
    add r0, r0, #4
    b .Llw
.Llb:
    ldrb r3, [r0]
    cmp r3, #0
    beq .Lld
    add r0, r0, #1
    b .Llb
.Lld:
    sub r0, r0, r4
    pop {r4-r6, pc}
.ltorg

.global RT_FN(strcpy)
.type RT_FN(strcpy), %function
RT_FN(strcpy):
    push {r4-r6, lr}
    mov r4, r0
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lca:
    eor r3, r0, r1
    tst r3, #3
    bne .Lcb
    tst r0, #3
    beq .Lcw
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    beq .Lcd
    b .Lca
.Lcw:
    mov r3, r1, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #3
    movs r3, r3, lsr #12
    bne .Lcb
    ldr r3, [r1]
    sub r2, r3, r5
    bic r2, r2, r3
    tst r2, r6
    bne .Lcb
    str r3, [r0], #4
    add r1, r1, #4
    b .Lcw
.Lcb:
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    bne .Lcb
.Lcd:
    mov r0, r4
    pop {r4-r6, pc}
.ltorg

.global RT_FN(strcmp)
.type RT_FN(strcmp), %function
RT_FN(strcmp):
    push {r4-r6, lr}
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lpa:
    eor r3, r0, r1
    tst r3, #3
    bne .Lpb
    tst r0, #3
    beq .Lpw
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne .Lpd
    cmp r3, #0
    beq .Lp0
    b .Lpa
.Lpw:
    mov r3, r0, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #3
    movs r3, r3, lsr #12
    bne .Lpb
    mov r3, r1, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #3
    movs r3, r3, lsr #12
    bne .Lpb
    ldr r3, [r0]
    ldr r4, [r1]
    cmp r3, r4
    bne .Lpb
    sub r2, r3, r5
    bic r2, r2, r3
    tst r2, r6
    bne .Lpb
    add r0, r0, #4
    add r1, r1, #4
    b .Lpa
.Lpb:
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne .Lpd
    cmp r3, #0
    bne .Lpb
.Lp0:
    mov r0, #0
    pop {r4-r6, pc}
.Lpd:
    sub r0, r3, r4
    pop {r4-r6, pc}
.ltorg

.global RT_FN(strchr)
.type RT_FN(strchr), %function
RT_FN(strchr):
    push {r4-r6, lr}
    uxtb r1, r1
    orr r4, r1, r1, lsl #8
    orr r4, r4, r4, lsl #16
    ldr r5, =0x01010101
    ldr r6, =0x80808080
.Lha:
    tst r0, #3
    bne .Lhb
    mov r3, r0, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #3
    movs r3, r3, lsr #12
    bne .Lhb
    ldr r3, [r0]
    eor r2, r3, r4
    sub ip, r3, r5
    bic ip, ip, r3
    tst ip, r6
    bne .Lhb
    sub ip, r2, r5
    bic ip, ip, r2
    tst ip, r6
    bne .Lhb
    add r0, r0, #4
    b .Lha
.Lhb:
    ldrb r3, [r0]
    cmp r3, r1
    beq .Lhd
    cmp r3, #0
    beq .Lhn
    add r0, r0, #1
    b .Lhb
.Lhd:
    pop {r4-r6, pc}
.Lhn:
    mov r0, #0
    pop {r4-r6, pc}
.ltorg

#include "../arm/rest.inc"
