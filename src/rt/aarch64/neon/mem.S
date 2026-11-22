/* AArch64 Advanced SIMD — size-classed memcpy, memset, memcmp.
   Darwin C symbols carry a leading underscore; Linux does not. */

.text

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

/* ---- memcpy: 0-15 scalar, 16-63 q-path, 64-127 one 64-byte step, 128+ eight Q ---- */
.globl SYM(rt_memcpy_neon)
#ifndef __APPLE__
.type SYM(rt_memcpy_neon), %function
#endif
.p2align 4
SYM(rt_memcpy_neon):
    mov x8, x0
    cmp x2, #16
    b.lo .Lmc_small
    cmp x2, #64
    b.lo .Lmc_mid
    cmp x2, #128
    b.lo .Lmc_64

    /* 128-byte body. Four loads then four stores, one cache line on this core. */
.p2align 4
.Lmc_128:
    ldp q0, q1, [x1]
    ldp q2, q3, [x1, #32]
    ldp q4, q5, [x1, #64]
    ldp q6, q7, [x1, #96]
    add x1, x1, #128
    stp q0, q1, [x0]
    stp q2, q3, [x0, #32]
    stp q4, q5, [x0, #64]
    stp q6, q7, [x0, #96]
    add x0, x0, #128
    sub x2, x2, #128
    cmp x2, #128
    b.hs .Lmc_128

    /* 64-byte body, also the entry for 64..127. */
.p2align 4
.Lmc_64:
    cmp x2, #64
    b.lo .Lmc_lt64
    ldp q0, q1, [x1]
    ldp q2, q3, [x1, #32]
    add x1, x1, #64
    stp q0, q1, [x0]
    stp q2, q3, [x0, #32]
    add x0, x0, #64
    sub x2, x2, #64
    cmp x2, #64
    b.hs .Lmc_64
.Lmc_lt64:
    cbz x2, .Lmc_ret
    cmp x2, #16
    b.lo .Lmc_small_ptr

.Lmc_mid:
    /* 16..63 */
    cmp x2, #32
    b.lo 1f
    ldp q0, q1, [x1], #32
    stp q0, q1, [x0], #32
    sub x2, x2, #32
1:
    cmp x2, #16
    b.lo .Lmc_small_ptr
    ldr q0, [x1], #16
    str q0, [x0], #16
    sub x2, x2, #16
    cbz x2, .Lmc_ret

.Lmc_small_ptr:
    /* tail with advanced pointers */
    cmp x2, #8
    b.lo 1f
    ldr x9, [x1], #8
    str x9, [x0], #8
    sub x2, x2, #8
1:
    cmp x2, #4
    b.lo 2f
    ldr w9, [x1], #4
    str w9, [x0], #4
    sub x2, x2, #4
2:
    cmp x2, #2
    b.lo 3f
    ldrh w9, [x1], #2
    strh w9, [x0], #2
    sub x2, x2, #2
3:
    cbz x2, .Lmc_ret
    ldrb w9, [x1]
    strb w9, [x0]
.Lmc_ret:
    mov x0, x8
    ret

    /* tiny: n < 16, pointers still at start */
.Lmc_small:
    tbz x2, #3, 1f          /* if n < 8 */
    ldr x9, [x1]
    str x9, [x0]
    sub x2, x2, #8
    add x1, x1, #8
    add x0, x0, #8
1:
    tbz x2, #2, 2f
    ldr w9, [x1]
    str w9, [x0]
    sub x2, x2, #4
    add x1, x1, #4
    add x0, x0, #4
2:
    tbz x2, #1, 3f
    ldrh w9, [x1]
    strh w9, [x0]
    sub x2, x2, #2
    add x1, x1, #2
    add x0, x0, #2
3:
    tbz x2, #0, .Lmc_ret
    ldrb w9, [x1]
    strb w9, [x0]
    b .Lmc_ret

/* ---- memset ---- */
.globl SYM(rt_memset_neon)
#ifndef __APPLE__
.type SYM(rt_memset_neon), %function
#endif
.p2align 4
SYM(rt_memset_neon):
    mov x8, x0
    and w1, w1, #0xff
    orr w1, w1, w1, lsl #8
    orr w1, w1, w1, lsl #16
    orr x3, x1, x1, lsl #32
    dup v0.2d, x3
    cmp x2, #16
    b.lo .Lms_small
    cmp x2, #64
    b.lo .Lms_mid
.p2align 4
.Lms_64:
    stp q0, q0, [x0]
    stp q0, q0, [x0, #32]
    add x0, x0, #64
    sub x2, x2, #64
    cmp x2, #64
    b.hs .Lms_64
    cbz x2, .Lms_ret
    cmp x2, #16
    b.lo .Lms_tail
.Lms_mid:
    str q0, [x0], #16
    sub x2, x2, #16
    cmp x2, #16
    b.hs .Lms_mid
    cbz x2, .Lms_ret
.Lms_tail:
    cmp x2, #8
    b.lo 1f
    str x3, [x0], #8
    sub x2, x2, #8
1:
    cmp x2, #4
    b.lo 2f
    str w3, [x0], #4
    sub x2, x2, #4
2:
    cmp x2, #2
    b.lo 3f
    strh w3, [x0], #2
    sub x2, x2, #2
3:
    cbz x2, .Lms_ret
    strb w3, [x0]
.Lms_ret:
    mov x0, x8
    ret
.Lms_small:
    tbz x2, #3, 1f
    str x3, [x0], #8
    sub x2, x2, #8
1:
    tbz x2, #2, 2f
    str w3, [x0], #4
    sub x2, x2, #4
2:
    tbz x2, #1, 3f
    strh w3, [x0], #2
    sub x2, x2, #2
3:
    tbz x2, #0, .Lms_ret
    strb w3, [x0]
    b .Lms_ret

/* ---- memcmp: 32-byte eor+umaxv ---- */
.globl SYM(rt_memcmp_neon)
#ifndef __APPLE__
.type SYM(rt_memcmp_neon), %function
#endif
.p2align 4
SYM(rt_memcmp_neon):
    cmp x2, #32
    b.lo .Lmp_16
.p2align 4
.Lmp_32:
    ldp q0, q1, [x0]
    ldp q2, q3, [x1]
    eor v0.16b, v0.16b, v2.16b
    eor v1.16b, v1.16b, v3.16b
    orr v0.16b, v0.16b, v1.16b
    umaxv b4, v0.16b
    umov w9, v4.b[0]
    cbnz w9, .Lmp_b
    add x0, x0, #32
    add x1, x1, #32
    sub x2, x2, #32
    cmp x2, #32
    b.hs .Lmp_32
.Lmp_16:
    cmp x2, #16
    b.lo .Lmp_8
    ldr q0, [x0]
    ldr q1, [x1]
    eor v0.16b, v0.16b, v1.16b
    umaxv b4, v0.16b
    umov w9, v4.b[0]
    cbnz w9, .Lmp_b
    add x0, x0, #16
    add x1, x1, #16
    sub x2, x2, #16
    b .Lmp_16
.Lmp_8:
    cmp x2, #8
    b.lo .Lmp_b
    ldr x9, [x0]
    ldr x10, [x1]
    eor x11, x9, x10
    cbnz x11, .Lmp_b
    add x0, x0, #8
    add x1, x1, #8
    sub x2, x2, #8
    b .Lmp_8
.Lmp_b:
    cbz x2, .Lmp_eq
.Lmp_loop:
    ldrb w9, [x0], #1
    ldrb w10, [x1], #1
    subs w9, w9, w10
    b.ne .Lmp_ret
    subs x2, x2, #1
    b.ne .Lmp_loop
.Lmp_eq:
    mov w0, #0
    ret
.Lmp_ret:
    mov w0, w9
    ret
