/* AArch32 NEON strings. A 16-byte load is refused when it would cross
   a 4 KiB page. A vector is stored only when it contains no NUL.
   The awkward bounded copies are the ARM byte routines in rest.inc.
   strcmp: https://github.com/ARM-software/optimized-routines/blob/v25.01/string/arm/strcmp.S
   strlen: https://github.com/ARM-software/optimized-routines/blob/v25.01/string/arm/strlen-armv6t2.S
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.cpu cortex-a7
.arch armv7-a
.fpu neon-vfpv4
.arm
#define RT_FN(name) rt_##name##_a32

.global RT_FN(strlen)
.type RT_FN(strlen), %function
RT_FN(strlen):
    mov ip, r0
.Ll1:
    mov r3, r0, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #15
    movs r3, r3, lsr #12
    bne .Ll2
    vld1.8 {q0}, [r0]
    vceq.i8 q1, q0, #0
    vorr d2, d2, d3
    vmov r2, r3, d2
    orrs r2, r2, r3
    bne .Ll2
    add r0, r0, #16
    b .Ll1
.Ll2:
    mov r2, #16
.Ll3:
    ldrb r3, [r0]
    cmp r3, #0
    beq .Ll4
    add r0, r0, #1
    subs r2, r2, #1
    bne .Ll3
    b .Ll1
.Ll4:
    sub r0, r0, ip
    bx lr

.global RT_FN(strcpy)
.type RT_FN(strcpy), %function
RT_FN(strcpy):
    mov ip, r0
.Lc1:
    mov r3, r1, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #15
    movs r3, r3, lsr #12
    bne .Lc2
    vld1.8 {q0}, [r1]
    vceq.i8 q1, q0, #0
    vorr d2, d2, d3
    vmov r2, r3, d2
    orrs r2, r2, r3
    bne .Lc2
    vst1.8 {q0}, [r0]!
    add r1, r1, #16
    b .Lc1
.Lc2:
    mov r2, #16
.Lc3:
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    beq .Lc4
    subs r2, r2, #1
    bne .Lc3
    b .Lc1
.Lc4:
    mov r0, ip
    bx lr

.global RT_FN(strcmp)
.type RT_FN(strcmp), %function
RT_FN(strcmp):
.Lp1:
    mov r3, r0, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #15
    movs r3, r3, lsr #12
    bne .Lp2
    mov r3, r1, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #15
    movs r3, r3, lsr #12
    bne .Lp2
    vld1.8 {q0}, [r0]
    vld1.8 {q1}, [r1]
    veor q2, q0, q1
    vorr d4, d4, d5
    vmov r2, r3, d4
    orrs r2, r2, r3
    bne .Lp2
    vceq.i8 q1, q0, #0
    vorr d2, d2, d3
    vmov r2, r3, d2
    orrs r2, r2, r3
    bne .Lp2
    add r0, r0, #16
    add r1, r1, #16
    b .Lp1
.Lp2:
    mov r2, #16
.Lp3:
    ldrb r3, [r0], #1
    ldrb ip, [r1], #1
    cmp r3, ip
    bne .Lp4
    cmp r3, #0
    beq .Lp0
    subs r2, r2, #1
    bne .Lp3
    b .Lp1
.Lp0:
    mov r0, #0
    bx lr
.Lp4:
    sub r0, r3, ip
    bx lr

.global RT_FN(strchr)
.type RT_FN(strchr), %function
RT_FN(strchr):
    uxtb r1, r1
    vdup.8 q3, r1
.Lh1:
    mov r3, r0, lsl #20
    mov r3, r3, lsr #20
    adds r3, r3, #15
    movs r3, r3, lsr #12
    bne .Lh2
    vld1.8 {q0}, [r0]
    vceq.i8 q1, q0, q3
    vceq.i8 q2, q0, #0
    vorr q1, q1, q2
    vorr d2, d2, d3
    vmov r2, r3, d2
    orrs r2, r2, r3
    bne .Lh2
    add r0, r0, #16
    b .Lh1
.Lh2:
    mov r2, #16
.Lh3:
    ldrb r3, [r0]
    cmp r3, r1
    beq .Lh4
    cmp r3, #0
    beq .Lh5
    add r0, r0, #1
    subs r2, r2, #1
    bne .Lh3
    b .Lh1
.Lh4:
    bx lr
.Lh5:
    mov r0, #0
    bx lr

#include "../arm/rest.inc"
