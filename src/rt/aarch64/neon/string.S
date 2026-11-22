/* AArch64 NEON string kernels.
   Darwin assembler: only numeric local labels (1:, 2:) for intra-fn branches. */

.text

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

/* ---- strlen ---- */
.globl SYM(rt_strlen_neon)
#ifndef __APPLE__
.type SYM(rt_strlen_neon), %function
#endif
.p2align 5
SYM(rt_strlen_neon):
    mov     x9, x0
    and     x2, x0, #0xfff
    cmp     x2, #0xff0
    b.hi    10f

20:
    ldp     q0, q1, [x0]
    cmeq    v2.16b, v0.16b, #0
    cmeq    v3.16b, v1.16b, #0
    orr     v4.16b, v2.16b, v3.16b
    umaxv   b5, v4.16b
    umov    w3, v5.b[0]
    cbnz    w3, 30f
    add     x0, x0, #32
    and     x2, x0, #0xfff
    cmp     x2, #0xff0
    b.ls    20b
    b       10f

30:
    umaxv   b5, v2.16b
    umov    w3, v5.b[0]
    cbnz    w3, 40f
    add     x0, x0, #16
    mov     v2.16b, v3.16b
40:
    shrn    v2.8b, v2.8h, #4
    fmov    x2, d2
    rbit    x2, x2
    clz     x2, x2
    add     x0, x0, x2, lsr #2
    sub     x0, x0, x9
    ret

10:
    ldrb    w3, [x0]
    cbz     w3, 11f
    add     x0, x0, #1
    and     x2, x0, #0xfff
    cbnz    x2, 10b
    b       20b
11:
    sub     x0, x0, x9
    ret

/* ---- strcmp ----
   A zero lane after cmeq/and is a mismatch or a NUL. Matched nonzero
   bytes stay nonzero, so one uminv per half covers both checks.
   The tail is the first zero lane, then one unsigned subtract.
   Over-read is one 32-byte chunk, same as the previous body. */
.globl SYM(rt_strcmp_neon)
#ifndef __APPLE__
.type SYM(rt_strcmp_neon), %function
#endif
.p2align 5
SYM(rt_strcmp_neon):
    .p2align 4
50:
    ldp     q0, q1, [x0], #32
    ldp     q2, q3, [x1], #32
    cmeq    v4.16b, v0.16b, v2.16b
    cmeq    v5.16b, v1.16b, v3.16b
    and     v0.16b, v0.16b, v4.16b
    and     v1.16b, v1.16b, v5.16b
    uminv   b4, v0.16b
    uminv   b5, v1.16b
    fmov    w2, s4
    fmov    w3, s5
    ands    w4, w2, w3
    b.ne    50b
    cbnz    w2, 70f
    sub     x0, x0, #16
    sub     x1, x1, #16
    b       60f
70:
    mov     v0.16b, v1.16b
60:
    eor     v1.16b, v1.16b, v1.16b
    cmhi    v0.16b, v0.16b, v1.16b
    ldr     q1, 51f
    orr     v0.16b, v0.16b, v1.16b
    uminv   b0, v0.16b
    fmov    w2, s0
    sub     x2, x2, #16
    ldrb    w3, [x0, x2]
    ldrb    w4, [x1, x2]
    sub     w0, w3, w4
    ret
    .p2align 4
51:
    .byte   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

/* ---- strchr ---- */
.globl SYM(rt_strchr_neon)
#ifndef __APPLE__
.type SYM(rt_strchr_neon), %function
#endif
.p2align 5
SYM(rt_strchr_neon):
    and     w1, w1, #0xff
    dup     v1.16b, w1
    and     x2, x0, #0xfff
    cmp     x2, #0xff0
    b.hi    90f

80:
    ldp     q0, q2, [x0]
    cmeq    v3.16b, v0.16b, v1.16b
    cmeq    v4.16b, v2.16b, v1.16b
    cmeq    v5.16b, v0.16b, #0
    cmeq    v6.16b, v2.16b, #0
    orr     v3.16b, v3.16b, v5.16b
    orr     v4.16b, v4.16b, v6.16b
    orr     v7.16b, v3.16b, v4.16b
    umaxv   b16, v7.16b
    umov    w3, v16.b[0]
    cbnz    w3, 85f
    add     x0, x0, #32
    and     x2, x0, #0xfff
    cmp     x2, #0xff0
    b.ls    80b
    b       90f

85:
    umaxv   b16, v3.16b
    umov    w3, v16.b[0]
    cbnz    w3, 86f
    add     x0, x0, #16
    mov     v3.16b, v4.16b
    mov     v0.16b, v2.16b
86:
    shrn    v3.8b, v3.8h, #4
    fmov    x2, d3
    rbit    x2, x2
    clz     x2, x2
    lsr     x2, x2, #2
    ldrb    w3, [x0, x2]
    cmp     w3, w1
    b.ne    91f
    add     x0, x0, x2
    ret

90:
    ldrb    w3, [x0]
    cmp     w3, w1
    b.eq    92f
    cbz     w3, 91f
    add     x0, x0, #1
    and     x2, x0, #0xfff
    cbnz    x2, 90b
    b       80b
91:
    mov     x0, #0
    ret
92:
    ret
