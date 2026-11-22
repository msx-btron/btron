/* Helium strings for Cortex-M55 and Cortex-M85.
   A 16-byte load stays inside one 4 KiB page.
   vminv finds a NUL. A match is a lane that xors to zero against
   the splatted character. Bounded copies are Thumb-2 bytes.
   No published Helium strlen or strcmp. The only libc string that
   links here is picolibc strnlen.c:
   https://github.com/picolibc/picolibc/blob/1.8.10/newlib/libc/string/strnlen.c
   Index: third_party/aarch32/SOURCES.md */
.syntax unified
.cpu cortex-m55
.thumb
#define RT_SUF m55
#define RT_CAT(a, b) a##b
#define RT_XCAT(a, b) RT_CAT(a, b)
#define RT_FN(name) RT_XCAT(rt_##name##_, RT_SUF)

.global RT_FN(strlen)
.type RT_FN(strlen), %function
.thumb_func
RT_FN(strlen):
    push {r4, lr}
    mov r4, r0
.Ll1:
    ubfx r3, r0, #0, #12
    adds r3, r3, #15
    lsrs r3, r3, #12
    bne .Ll2
    vldrb.u8 q0, [r0]
    vminv.u8 r3, q0
    cmp r3, #0
    beq .Ll2
    adds r0, r0, #16
    b .Ll1
.Ll2:
    movs r2, #16
.Ll3:
    ldrb r3, [r0]
    cmp r3, #0
    beq .Ll4
    adds r0, r0, #1
    subs r2, r2, #1
    bne .Ll3
    b .Ll1
.Ll4:
    subs r0, r0, r4
    pop {r4, pc}

.global RT_FN(strcpy)
.type RT_FN(strcpy), %function
.thumb_func
RT_FN(strcpy):
    push {r4, lr}
    mov r4, r0
.Lc1:
    ubfx r3, r1, #0, #12
    adds r3, r3, #15
    lsrs r3, r3, #12
    bne .Lc2
    vldrb.u8 q0, [r1]
    vminv.u8 r3, q0
    cmp r3, #0
    beq .Lc2
    vstrb.8 q0, [r0], #16
    adds r1, r1, #16
    b .Lc1
.Lc2:
    movs r2, #16
.Lc3:
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    beq .Lc4
    subs r2, r2, #1
    bne .Lc3
    b .Lc1
.Lc4:
    mov r0, r4
    pop {r4, pc}

.global RT_FN(strcmp)
.type RT_FN(strcmp), %function
.thumb_func
RT_FN(strcmp):
    push {r4, lr}
.Lp1:
    ubfx r3, r0, #0, #12
    adds r3, r3, #15
    lsrs r3, r3, #12
    bne .Lp2
    ubfx r3, r1, #0, #12
    adds r3, r3, #15
    lsrs r3, r3, #12
    bne .Lp2
    vldrb.u8 q0, [r0]
    vldrb.u8 q1, [r1]
    veor q2, q0, q1
    vmaxv.u8 r3, q2
    cmp r3, #0
    bne .Lp2
    vminv.u8 r3, q0
    cmp r3, #0
    beq .Lp2
    adds r0, r0, #16
    adds r1, r1, #16
    b .Lp1
.Lp2:
    movs r2, #16
.Lp3:
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne .Lp4
    cmp r3, #0
    beq .Lp0
    subs r2, r2, #1
    bne .Lp3
    b .Lp1
.Lp0:
    movs r0, #0
    pop {r4, pc}
.Lp4:
    subs r0, r3, r4
    pop {r4, pc}

.global RT_FN(strchr)
.type RT_FN(strchr), %function
.thumb_func
RT_FN(strchr):
    push {r4, lr}
    uxtb r1, r1
    vdup.8 q1, r1
.Lh1:
    ubfx r3, r0, #0, #12
    adds r3, r3, #15
    lsrs r3, r3, #12
    bne .Lh2
    vldrb.u8 q0, [r0]
    veor q2, q0, q1
    vminv.u8 r3, q2
    cmp r3, #0
    beq .Lh2
    vminv.u8 r3, q0
    cmp r3, #0
    beq .Lh2
    adds r0, r0, #16
    b .Lh1
.Lh2:
    movs r2, #16
.Lh3:
    ldrb r3, [r0]
    cmp r3, r1
    beq .Lh4
    cmp r3, #0
    beq .Lh5
    adds r0, r0, #1
    subs r2, r2, #1
    bne .Lh3
    b .Lh1
.Lh4:
    pop {r4, pc}
.Lh5:
    movs r0, #0
    pop {r4, pc}

#include "../thumb2/rest.inc"
