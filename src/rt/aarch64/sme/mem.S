/* rt_memset_sme — Apple M4 SME2.
   Below 2048 bytes the stp loop is already on the store ports, so this
   uses overlapping stores and a 64-byte stp body. From 2048 bytes a
   streaming store of four 64-byte vectors is faster. A zero fill of
   32 KiB or more uses DC ZVA, which matches the large bzero path.
   rt_memset_neon is a different function and stays as it is. */
/* rt_memcpy_sme and rt_memmove_sme stream 2048-byte trips when the
   streaming length is 64. rt_memcmp_sme folds the xors into one
   register and tests it once. A predicate flag check on every trip
   is much slower on this core. */


.arch armv9-a+sme2
.text

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

.globl SYM(rt_memset_sme)
#ifndef __APPLE__
.type SYM(rt_memset_sme), %function
#endif
.p2align 4
SYM(rt_memset_sme):
    mov     x8, x0
    and     w1, w1, #0xff
    /* Streaming mode is entered with the vector file untouched.
       A dup before smstart makes the switch much slower. */
    cmp     x2, #2048
    b.lo    .Lneo_prep
    cbnz    w1, .Lneo_sme
    cmp     x2, #32768
    b.lo    .Lneo_sme

    /* Zero, 32 KiB and up. DC ZVA block is 64 bytes on this core.
       Any other block size, or a prohibited ZVA, uses the streaming path. */
    mrs     x9, dczid_el0
    tbnz    x9, #4, .Lneo_sme
    and     x9, x9, #0xf
    cmp     x9, #4
    b.ne    .Lneo_sme
    movi    v0.16b, #0
    add     x5, x0, x2
    tst     x0, #63
    b.eq    .Lneo_zva
    stp     q0, q0, [x0]
    stp     q0, q0, [x0, #32]
    add     x0, x0, #64
    and     x0, x0, #~63
.Lneo_zva:
    add     x4, x0, #64
    cmp     x4, x5
    b.hi    .Lneo_zva_tail
    dc      zva, x0
    mov     x0, x4
    b       .Lneo_zva
.Lneo_zva_tail:
    cmp     x0, x5
    b.hs    .Lneo_ret
    stp     q0, q0, [x5, #-64]
    stp     q0, q0, [x5, #-32]
    b       .Lneo_ret

.Lneo_sme:
    /* SM only. ZA is not used, and enabling it is the expensive part. */
    smstart sm
    rdvl    x9, #1
    cmp     x9, #64
    b.ne    .Lneo_sme_any
    mov     z0.b, w1
    mov     z1.d, z0.d
    mov     z2.d, z0.d
    mov     z3.d, z0.d
    ptrue   pn8.b
    /* 512 bytes per trip: two 4-vector stores, one index update. */
    cmp     x2, #512
    b.lo    .Lneo_sme_256
.Lneo_sme_loop:
    st1b    { z0.b - z3.b }, pn8, [x0]
    st1b    { z0.b - z3.b }, pn8, [x0, #4, mul vl]
    add     x0, x0, #512
    sub     x2, x2, #512
    cmp     x2, #512
    b.hs    .Lneo_sme_loop
.Lneo_sme_256:
    cmp     x2, #256
    b.lo    .Lneo_sme_tail
    st1b    { z0.b - z3.b }, pn8, [x0]
    add     x0, x0, #256
    sub     x2, x2, #256
.Lneo_sme_tail:
    cbz     x2, .Lneo_sme_done
    mov     x4, #0
.Lneo_sme_rem:
    whilelo p0.b, x4, x2
    b.none  .Lneo_sme_done
    st1b    { z0.b }, p0, [x0, x4]
    incb    x4
    b       .Lneo_sme_rem
.Lneo_sme_any:
    mov     z0.b, w1
    mov     x4, #0
.Lneo_sme_any_loop:
    whilelo p0.b, x4, x2
    b.none  .Lneo_sme_done
    st1b    { z0.b }, p0, [x0, x4]
    incb    x4
    b       .Lneo_sme_any_loop
.Lneo_sme_done:
    smstop  sm
    mov     x0, x8
    ret

    /* n < 2048. Two overlapping stores below 64. At 64 and above, one
       unaligned head if needed, then aligned 64-byte pairs, then one
       pair that ends at the last byte. */
.Lneo_prep:
    orr     w1, w1, w1, lsl #8
    orr     w1, w1, w1, lsl #16
    orr     x3, x1, x1, lsl #32
    dup     v0.2d, x3
.Lneo_stp:
    cmp     x2, #64
    b.hs    .Lneo_ge64
    cmp     x2, #32
    b.hs    .Lneo_ge32
    cmp     x2, #16
    b.hs    .Lneo_ge16
    cmp     x2, #8
    b.hs    .Lneo_ge8
    cmp     x2, #4
    b.hs    .Lneo_ge4
    cbz     x2, .Lneo_ret
    cmp     x2, #2
    b.lo    .Lneo_b1
    strh    w3, [x0]
    add     x4, x0, x2
    strh    w3, [x4, #-2]
    b       .Lneo_ret
.Lneo_b1:
    strb    w3, [x0]
    b       .Lneo_ret
.Lneo_ge4:
    str     w3, [x0]
    add     x4, x0, x2
    str     w3, [x4, #-4]
    b       .Lneo_ret
.Lneo_ge8:
    str     x3, [x0]
    add     x4, x0, x2
    str     x3, [x4, #-8]
    b       .Lneo_ret
.Lneo_ge16:
    str     q0, [x0]
    add     x4, x0, x2
    str     q0, [x4, #-16]
    b       .Lneo_ret
.Lneo_ge32:
    stp     q0, q0, [x0]
    add     x4, x0, x2
    stp     q0, q0, [x4, #-32]
    b       .Lneo_ret
.Lneo_ge64:
    add     x5, x0, x2
    tst     x0, #63
    b.eq    .Lneo_aligned
    stp     q0, q0, [x0]
    stp     q0, q0, [x0, #32]
    add     x0, x0, #64
    and     x0, x0, #~63
    sub     x2, x5, x0
.Lneo_aligned:
    subs    x2, x2, #64
    b.ls    .Lneo_tail
    .p2align 4
.Lneo_loop:
    stp     q0, q0, [x0]
    stp     q0, q0, [x0, #32]
    add     x0, x0, #64
    subs    x2, x2, #64
    b.hi    .Lneo_loop
.Lneo_tail:
    stp     q0, q0, [x5, #-64]
    stp     q0, q0, [x5, #-32]
.Lneo_ret:
    mov     x0, x8
    ret

/* rt_sme_call — fn(x0, x1, x2) between smstart sm and smstop sm.
   x3 is fn. Apple __sme_* bodies have no smstart of their own.
   ZA stays off. The second argument is pointer-sized, so this is
   also the memcpy and memmove trampoline. */
.globl SYM(rt_sme_call)
#ifndef __APPLE__
.type SYM(rt_sme_call), %function
#endif
.p2align 2
SYM(rt_sme_call):
    stp     x29, x30, [sp, #-16]!
    smstart sm
    blr     x3
    smstop  sm
    ldp     x29, x30, [sp], #16
    ret

/* Non-overlapping copy. n < 256 stays on scalar bytes. At 256 and
   above, and only when the streaming length is 64, the body moves
   2048, 1024, 512, then 256 bytes. Each trip loads the whole chunk
   before it stores, so a forward overlap is intact. Other streaming
   lengths use one predicated vector. Bytes past n are not written. */

.globl SYM(rt_memcpy_sme)
#ifndef __APPLE__
.type SYM(rt_memcpy_sme), %function
#endif
.p2align 4
SYM(rt_memcpy_sme):

    mov     x8, x0
    b       .Lsme_fwd

/* Copy backward when dst sits above src and the ranges overlap. */

.globl SYM(rt_memmove_sme)
#ifndef __APPLE__
.type SYM(rt_memmove_sme), %function
#endif
.p2align 4
SYM(rt_memmove_sme):

    mov     x8, x0
    cbz     x2, .Lsme_ret
    cmp     x0, x1
    b.ls    .Lsme_fwd
    sub     x3, x0, x1
    cmp     x3, x2
    b.hs    .Lsme_fwd
    b       .Lsme_back
.Lsme_fwd:
    cmp     x2, #256
    b.lo    .Lsme_byte
    smstart sm
    rdvl    x9, #1
    cmp     x9, #64
    b.ne    .Lsme_fwd_any
    ptrue   pn8.b
.Lf2048:
    cmp     x2, #2048
    b.lo    .Lfnext2048
    ld1b    { z0.b - z3.b }, pn8/z, [x1]
    ld1b    { z4.b - z7.b }, pn8/z, [x1, #4, mul vl]
    ld1b    { z8.b - z11.b }, pn8/z, [x1, #8, mul vl]
    ld1b    { z12.b - z15.b }, pn8/z, [x1, #12, mul vl]
    ld1b    { z16.b - z19.b }, pn8/z, [x1, #16, mul vl]
    ld1b    { z20.b - z23.b }, pn8/z, [x1, #20, mul vl]
    ld1b    { z24.b - z27.b }, pn8/z, [x1, #24, mul vl]
    ld1b    { z28.b - z31.b }, pn8/z, [x1, #28, mul vl]
    st1b    { z0.b - z3.b }, pn8, [x0]
    st1b    { z4.b - z7.b }, pn8, [x0, #4, mul vl]
    st1b    { z8.b - z11.b }, pn8, [x0, #8, mul vl]
    st1b    { z12.b - z15.b }, pn8, [x0, #12, mul vl]
    st1b    { z16.b - z19.b }, pn8, [x0, #16, mul vl]
    st1b    { z20.b - z23.b }, pn8, [x0, #20, mul vl]
    st1b    { z24.b - z27.b }, pn8, [x0, #24, mul vl]
    st1b    { z28.b - z31.b }, pn8, [x0, #28, mul vl]
    add     x1, x1, #2048
    add     x0, x0, #2048
    sub     x2, x2, #2048
    b       .Lf2048
.Lfnext2048:
.Lf1024:
    cmp     x2, #1024
    b.lo    .Lfnext1024
    ld1b    { z0.b - z3.b }, pn8/z, [x1]
    ld1b    { z4.b - z7.b }, pn8/z, [x1, #4, mul vl]
    ld1b    { z8.b - z11.b }, pn8/z, [x1, #8, mul vl]
    ld1b    { z12.b - z15.b }, pn8/z, [x1, #12, mul vl]
    st1b    { z0.b - z3.b }, pn8, [x0]
    st1b    { z4.b - z7.b }, pn8, [x0, #4, mul vl]
    st1b    { z8.b - z11.b }, pn8, [x0, #8, mul vl]
    st1b    { z12.b - z15.b }, pn8, [x0, #12, mul vl]
    add     x1, x1, #1024
    add     x0, x0, #1024
    sub     x2, x2, #1024
    b       .Lf1024
.Lfnext1024:
.Lf512:
    cmp     x2, #512
    b.lo    .Lfnext512
    ld1b    { z0.b - z3.b }, pn8/z, [x1]
    ld1b    { z4.b - z7.b }, pn8/z, [x1, #4, mul vl]
    st1b    { z0.b - z3.b }, pn8, [x0]
    st1b    { z4.b - z7.b }, pn8, [x0, #4, mul vl]
    add     x1, x1, #512
    add     x0, x0, #512
    sub     x2, x2, #512
    b       .Lf512
.Lfnext512:
.Lf256:
    cmp     x2, #256
    b.lo    .Lfnext256
    ld1b    { z0.b - z3.b }, pn8/z, [x1]
    st1b    { z0.b - z3.b }, pn8, [x0]
    add     x1, x1, #256
    add     x0, x0, #256
    sub     x2, x2, #256
    b       .Lf256
.Lfnext256:
    cbz     x2, .Lsme_stop
    mov     x3, #0
.Lsme_ftail:
    whilelo p0.b, x3, x2
    b.eq    .Lsme_stop
    ld1b    { z0.b }, p0/z, [x1, x3]
    st1b    { z0.b }, p0, [x0, x3]
    incb    x3
    b       .Lsme_ftail
.Lsme_fwd_any:
    mov     x3, #0
.Lsme_fwd_any_l:
    whilelo p0.b, x3, x2
    b.eq    .Lsme_stop
    ld1b    { z0.b }, p0/z, [x1, x3]
    st1b    { z0.b }, p0, [x0, x3]
    incb    x3
    b       .Lsme_fwd_any_l
.Lsme_stop:
    smstop  sm
    b       .Lsme_ret
.Lsme_byte:
    cbz     x2, .Lsme_ret
.Lsme_byte_l:
    ldrb    w3, [x1], #1
    strb    w3, [x0], #1
    subs    x2, x2, #1
    b.ne    .Lsme_byte_l
.Lsme_ret:
    mov     x0, x8
    ret
.Lsme_back:
    cmp     x2, #256
    b.lo    .Lsme_bbyte
    smstart sm
    rdvl    x9, #1
    cmp     x9, #64
    b.ne    .Lsme_back_any
    ptrue   pn8.b
    add     x3, x0, x2
    add     x4, x1, x2
.Lb2048:
    cmp     x2, #2048
    b.lo    .Lbnext2048
    sub     x3, x3, #2048
    sub     x4, x4, #2048
    ld1b    { z0.b - z3.b }, pn8/z, [x4]
    ld1b    { z4.b - z7.b }, pn8/z, [x4, #4, mul vl]
    ld1b    { z8.b - z11.b }, pn8/z, [x4, #8, mul vl]
    ld1b    { z12.b - z15.b }, pn8/z, [x4, #12, mul vl]
    ld1b    { z16.b - z19.b }, pn8/z, [x4, #16, mul vl]
    ld1b    { z20.b - z23.b }, pn8/z, [x4, #20, mul vl]
    ld1b    { z24.b - z27.b }, pn8/z, [x4, #24, mul vl]
    ld1b    { z28.b - z31.b }, pn8/z, [x4, #28, mul vl]
    st1b    { z0.b - z3.b }, pn8, [x3]
    st1b    { z4.b - z7.b }, pn8, [x3, #4, mul vl]
    st1b    { z8.b - z11.b }, pn8, [x3, #8, mul vl]
    st1b    { z12.b - z15.b }, pn8, [x3, #12, mul vl]
    st1b    { z16.b - z19.b }, pn8, [x3, #16, mul vl]
    st1b    { z20.b - z23.b }, pn8, [x3, #20, mul vl]
    st1b    { z24.b - z27.b }, pn8, [x3, #24, mul vl]
    st1b    { z28.b - z31.b }, pn8, [x3, #28, mul vl]
    sub     x2, x2, #2048
    b       .Lb2048
.Lbnext2048:
.Lb1024:
    cmp     x2, #1024
    b.lo    .Lbnext1024
    sub     x3, x3, #1024
    sub     x4, x4, #1024
    ld1b    { z0.b - z3.b }, pn8/z, [x4]
    ld1b    { z4.b - z7.b }, pn8/z, [x4, #4, mul vl]
    ld1b    { z8.b - z11.b }, pn8/z, [x4, #8, mul vl]
    ld1b    { z12.b - z15.b }, pn8/z, [x4, #12, mul vl]
    st1b    { z0.b - z3.b }, pn8, [x3]
    st1b    { z4.b - z7.b }, pn8, [x3, #4, mul vl]
    st1b    { z8.b - z11.b }, pn8, [x3, #8, mul vl]
    st1b    { z12.b - z15.b }, pn8, [x3, #12, mul vl]
    sub     x2, x2, #1024
    b       .Lb1024
.Lbnext1024:
.Lb512:
    cmp     x2, #512
    b.lo    .Lbnext512
    sub     x3, x3, #512
    sub     x4, x4, #512
    ld1b    { z0.b - z3.b }, pn8/z, [x4]
    ld1b    { z4.b - z7.b }, pn8/z, [x4, #4, mul vl]
    st1b    { z0.b - z3.b }, pn8, [x3]
    st1b    { z4.b - z7.b }, pn8, [x3, #4, mul vl]
    sub     x2, x2, #512
    b       .Lb512
.Lbnext512:
.Lb256:
    cmp     x2, #256
    b.lo    .Lbnext256
    sub     x3, x3, #256
    sub     x4, x4, #256
    ld1b    { z0.b - z3.b }, pn8/z, [x4]
    st1b    { z0.b - z3.b }, pn8, [x3]
    sub     x2, x2, #256
    b       .Lb256
.Lbnext256:
    smstop  sm
    b       .Lsme_bbyte_at
.Lsme_back_any:
    smstop  sm
.Lsme_bbyte:
    add     x3, x0, x2
    add     x4, x1, x2
.Lsme_bbyte_at:
    cbz     x2, .Lsme_ret
.Lsme_bbyte_l:
    ldrb    w5, [x4, #-1]!
    strb    w5, [x3, #-1]!
    subs    x2, x2, #1
    b.ne    .Lsme_bbyte_l
    b       .Lsme_ret

/* Unsigned byte compare. The timed point is an equal 4096-byte scan.
   The xors are folded into z31 and tested once. Testing predicate
   flags on every trip costs more than the loads on this core. A hit
   drops out of streaming mode and rescans from the start. n < 256
   never enters streaming mode. */

.globl SYM(rt_memcmp_sme)
#ifndef __APPLE__
.type SYM(rt_memcmp_sme), %function
#endif
.p2align 4
SYM(rt_memcmp_sme):

    cbz     x2, .Lcmp0
    cmp     x2, #256
    b.lo    .Lcmp_byte
    smstart sm
    rdvl    x9, #1
    cmp     x9, #64
    b.ne    .Lcmp_any
    ptrue   pn8.b
    ptrue   p7.b
    eor     z31.d, z31.d, z31.d
    mov     x10, x0
    mov     x11, x1
    mov     x12, x2
.Lcmp512:
    cmp     x2, #512
    b.lo    .Lcmp256
    ld1b    { z0.b - z3.b }, pn8/z, [x0]
    ld1b    { z4.b - z7.b }, pn8/z, [x0, #4, mul vl]
    ld1b    { z8.b - z11.b }, pn8/z, [x1]
    ld1b    { z12.b - z15.b }, pn8/z, [x1, #4, mul vl]
    eor     z0.d, z0.d, z8.d
    eor     z1.d, z1.d, z9.d
    eor     z2.d, z2.d, z10.d
    eor     z3.d, z3.d, z11.d
    eor     z4.d, z4.d, z12.d
    eor     z5.d, z5.d, z13.d
    eor     z6.d, z6.d, z14.d
    eor     z7.d, z7.d, z15.d
    orr     z0.d, z0.d, z1.d
    orr     z2.d, z2.d, z3.d
    orr     z4.d, z4.d, z5.d
    orr     z6.d, z6.d, z7.d
    orr     z0.d, z0.d, z2.d
    orr     z4.d, z4.d, z6.d
    orr     z0.d, z0.d, z4.d
    orr     z31.d, z31.d, z0.d
    add     x0, x0, #512
    add     x1, x1, #512
    sub     x2, x2, #512
    b       .Lcmp512
.Lcmp256:
    cmp     x2, #256
    b.lo    .Lcmp_fold
    ld1b    { z0.b - z3.b }, pn8/z, [x0]
    ld1b    { z4.b - z7.b }, pn8/z, [x1]
    eor     z0.d, z0.d, z4.d
    eor     z1.d, z1.d, z5.d
    eor     z2.d, z2.d, z6.d
    eor     z3.d, z3.d, z7.d
    orr     z0.d, z0.d, z1.d
    orr     z2.d, z2.d, z3.d
    orr     z0.d, z0.d, z2.d
    orr     z31.d, z31.d, z0.d
    add     x0, x0, #256
    add     x1, x1, #256
    sub     x2, x2, #256
.Lcmp_fold:
    cmpne   p0.b, p7/z, z31.b, #0
    b.eq    .Lcmp_eqtail
    smstop  sm
    mov     x0, x10
    mov     x1, x11
    mov     x2, x12
    b       .Lcmp_byte
.Lcmp_eqtail:
    cbz     x2, .Lcmp_stop
    mov     x3, #0
.Lcmp_eq_l:
    ldrb    w4, [x0, x3]
    ldrb    w5, [x1, x3]
    cmp     w4, w5
    b.ne    .Lcmp_diff
    add     x3, x3, #1
    cmp     x3, x2
    b.lo    .Lcmp_eq_l
.Lcmp_stop:
    smstop  sm
.Lcmp0:
    mov     w0, #0
    ret
.Lcmp_diff:
    sub     w0, w4, w5
    smstop  sm
    ret
.Lcmp_any:
    smstop  sm
.Lcmp_byte:
    mov     x3, #0
.Lcmp_byte_l:
    ldrb    w4, [x0, x3]
    ldrb    w5, [x1, x3]
    cmp     w4, w5
    b.ne    .Lcmp_diff_ns
    add     x3, x3, #1
    cmp     x3, x2
    b.lo    .Lcmp_byte_l
    mov     w0, #0
    ret
.Lcmp_diff_ns:
    sub     w0, w4, w5
    ret
