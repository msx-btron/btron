# AArch64 SVE. One predicate-length chunk per iteration, so the same code
# is correct for 128-bit through 2048-bit vectors and never touches past n.
# whilelo + incb is the canonical "copy n bytes" pattern.

.arch armv8-a+sve
.section .text

.globl rt_memcpy_sve
.type rt_memcpy_sve, %function
.p2align 4
rt_memcpy_sve:
    mov x8, x0
    mov x3, #0
1:
    whilelo p0.b, x3, x2
    b.none 2f
    ld1b z0.b, p0/z, [x1, x3]
    st1b z0.b, p0, [x0, x3]
    incb x3
    b 1b
2:
    mov x0, x8
    ret

.globl rt_memset_sve
.type rt_memset_sve, %function
.p2align 4
rt_memset_sve:
    mov x8, x0
    and w1, w1, #0xff
    mov z0.b, w1
    mov x3, #0
1:
    whilelo p0.b, x3, x2
    b.none 2f
    st1b z0.b, p0, [x0, x3]
    incb x3
    b 1b
2:
    mov x0, x8
    ret

.globl rt_memcmp_sve
.type rt_memcmp_sve, %function
.p2align 4
rt_memcmp_sve:
    mov x3, #0
1:
    whilelo p0.b, x3, x2
    b.none 3f
    ld1b z0.b, p0/z, [x0, x3]
    ld1b z1.b, p0/z, [x1, x3]
    cmpne p1.b, p0/z, z0.b, z1.b
    b.none 2f
    brkb p2.b, p0/z, p1.b
    incp x3, p2.b
    ldrb w9, [x0, x3]
    ldrb w10, [x1, x3]
    sub w0, w9, w10
    ret
2:
    incb x3
    b 1b
3:
    mov w0, #0
    ret
