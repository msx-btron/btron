# RV32GCV string plane. Same vsetvli e8 m8 loops as RV64: VLEN does not
# follow XLEN, so one vle8/vse8 still moves VL bytes. A scan to NUL uses
# vle8ff and stops before a fault. A bounded call does not pass n.
# a0-a7 and t0-t6 are XLEN wide (32 here). Do not assemble this into an
# RV64 link; the symbol names match the RV64 file on purpose.
# sx/lx are the only XLEN-specific stores; strscat is the only caller.

.section .text
.attribute arch, "rv32gcv"

.macro sx reg, off, base
    sw \reg, \off(\base)
.endm
.macro lx reg, off, base
    lw \reg, \off(\base)
.endm

# vec from ptr. nvl is the vl just set. sa and sb are scratch.
# vle8 when the group ends on this page (bleu, so the last byte of the
# page is included). vle8ff when the group would cross, so a fault stops
# the load. Labels 70/71 are local and may repeat.
.macro vload vec, ptr, nvl, sa, sb
    li \sa, 4095
    and \sa, \ptr, \sa
    add \sa, \sa, \nvl
    li \sb, 4096
    bleu \sa, \sb, 70f
    vle8ff.v \vec, (\ptr)
    j 71f
70:
    vle8.v \vec, (\ptr)
71:
.endm

.globl rt_strlen_rvv
.type rt_strlen_rvv, @function
.p2align 2
rt_strlen_rvv:
    mv t0, a0
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t4, t5
    csrr t1, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 2f
    add a0, a0, t1
    j 1b
2:
    add a0, a0, t2
    sub a0, a0, t0
    ret

.globl rt_strnlen_rvv
.type rt_strnlen_rvv, @function
.p2align 2
rt_strnlen_rvv:
    mv t0, a0
    beqz a1, 3f
1:
    vsetvli t1, a1, e8, m8, ta, ma
    vle8.v v8, (a0)
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 2f
    add a0, a0, t1
    sub a1, a1, t1
    bnez a1, 1b
    sub a0, a0, t0
    ret
2:
    add a0, a0, t2
    sub a0, a0, t0
    ret
3:
    li a0, 0
    ret

.globl rt_strcpy_rvv
.type rt_strcpy_rvv, @function
.p2align 2
rt_strcpy_rvv:
    mv t0, a0
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a1, t6, t4, t5
    csrr t1, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 2f
    vse8.v v8, (a0)
    add a0, a0, t1
    add a1, a1, t1
    j 1b
2:
    addi t2, t2, 1
    vsetvli x0, t2, e8, m8, tu, mu
    vse8.v v8, (a0)
    mv a0, t0
    ret

.globl rt_strncpy_rvv
.type rt_strncpy_rvv, @function
.p2align 2
rt_strncpy_rvv:
    mv t0, a0
    beqz a2, 9f
1:
    vsetvli t1, a2, e8, m8, ta, ma
    vload v8, a1, t1, t4, t5
    csrr t1, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 2f
    vse8.v v8, (a0)
    add a0, a0, t1
    add a1, a1, t1
    sub a2, a2, t1
    bnez a2, 1b
    j 9f
2:
    addi t3, t2, 1
    vsetvli x0, t3, e8, m8, tu, mu
    vse8.v v8, (a0)
    add a0, a0, t3
    sub a2, a2, t3
    beqz a2, 9f
    vsetvli t6, x0, e8, m8, ta, ma
    vmv.v.x v8, zero
3:
    vsetvli t1, a2, e8, m8, ta, ma
    vse8.v v8, (a0)
    add a0, a0, t1
    sub a2, a2, t1
    bnez a2, 3b
9:
    mv a0, t0
    ret

.globl rt_strcat_rvv
.type rt_strcat_rvv, @function
.p2align 2
rt_strcat_rvv:
    mv t0, a0
    mv t1, a1
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t4, t5
    csrr t2, vl
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    bgez t3, 2f
    add a0, a0, t2
    j 1b
2:
    add a0, a0, t3
    mv a1, t1
3:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a1, t6, t4, t5
    csrr t2, vl
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    bgez t3, 4f
    vse8.v v8, (a0)
    add a0, a0, t2
    add a1, a1, t2
    j 3b
4:
    addi t3, t3, 1
    vsetvli x0, t3, e8, m8, tu, mu
    vse8.v v8, (a0)
    mv a0, t0
    ret

.globl rt_strncat_rvv
.type rt_strncat_rvv, @function
.p2align 2
rt_strncat_rvv:
    mv t0, a0
    mv t1, a1
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t4, t5
    csrr t2, vl
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    bgez t3, 2f
    add a0, a0, t2
    j 1b
2:
    add a0, a0, t3
    beqz a2, 6f
3:
    vsetvli t2, a2, e8, m8, ta, ma
    vload v8, a1, t2, t4, t5
    csrr t2, vl
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    bgez t3, 4f
    vse8.v v8, (a0)
    add a0, a0, t2
    add a1, a1, t2
    sub a2, a2, t2
    bnez a2, 3b
    j 6f
4:
    beqz t3, 6f
    vsetvli x0, t3, e8, m8, tu, mu
    vse8.v v8, (a0)
    add a0, a0, t3
6:
    sb zero, 0(a0)
    mv a0, t0
    ret

.globl rt_strcmp_rvv
.type rt_strcmp_rvv, @function
.p2align 2
rt_strcmp_rvv:
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t2, t5
    csrr t1, vl
    vsetvli t2, t1, e8, m8, tu, mu
    vload v16, a1, t2, t5, t6
    csrr t1, vl
    vmsne.vv v0, v8, v16
    vfirst.m t3, v0
    vmseq.vi v0, v8, 0
    vfirst.m t4, v0
    bltz t3, 5f
    bltz t4, 3f
    bleu t3, t4, 3f
    j 4f
5:
    bgez t4, 4f
    add a0, a0, t1
    add a1, a1, t1
    j 1b
3:
    add a0, a0, t3
    add a1, a1, t3
    lbu t1, (a0)
    lbu t2, (a1)
    sub a0, t1, t2
    ret
4:
    li a0, 0
    ret

.globl rt_strncmp_rvv
.type rt_strncmp_rvv, @function
.p2align 2
rt_strncmp_rvv:
    beqz a2, 4f
1:
    vsetvli t0, a2, e8, m8, ta, ma
    vload v8, a0, t0, t2, t5
    csrr t1, vl
    vsetvli t2, t1, e8, m8, tu, mu
    vload v16, a1, t2, t5, t6
    csrr t1, vl
    vmsne.vv v0, v8, v16
    vfirst.m t3, v0
    vmseq.vi v0, v8, 0
    vfirst.m t4, v0
    bltz t3, 5f
    bltz t4, 3f
    bleu t3, t4, 3f
    j 4f
5:
    bgez t4, 4f
    add a0, a0, t1
    add a1, a1, t1
    sub a2, a2, t1
    bnez a2, 1b
4:
    li a0, 0
    ret
3:
    add a0, a0, t3
    add a1, a1, t3
    lbu t1, (a0)
    lbu t2, (a1)
    sub a0, t1, t2
    ret

.globl rt_strchr_rvv
.type rt_strchr_rvv, @function
.p2align 2
rt_strchr_rvv:
    andi a1, a1, 255
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t4, t5
    csrr t1, vl
    vmseq.vx v0, v8, a1
    vfirst.m t2, v0
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    li t4, -1
    bltz t2, 2f
    mv t4, t2
2:
    bltz t3, 3f
    bltz t4, 4f
    bgeu t3, t4, 3f
4:
    mv t4, t3
3:
    bltz t4, 5f
    add a0, a0, t4
    lbu t5, (a0)
    bne t5, a1, 6f
    ret
6:
    li a0, 0
    ret
5:
    add a0, a0, t1
    j 1b

.globl rt_strrchr_rvv
.type rt_strrchr_rvv, @function
.p2align 2
rt_strrchr_rvv:
    andi a1, a1, 255
    li t0, 0
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t5, t4
    csrr t1, vl
    mv t2, a0
    add t3, a0, t1
2:
    lbu t4, (t2)
    bne t4, a1, 3f
    mv t0, t2
3:
    beqz t4, 4f
    addi t2, t2, 1
    bltu t2, t3, 2b
    mv a0, t3
    j 1b
4:
    mv a0, t0
    ret

.globl rt_strstr_rvv
.type rt_strstr_rvv, @function
.p2align 2
rt_strstr_rvv:
    lbu t0, (a1)
    bnez t0, 1f
    ret
1:
    mv t5, a0
    mv a0, a1
2:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t2, t3
    csrr t1, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 3f
    add a0, a0, t1
    j 2b
3:
    sub t6, a0, a1
    add t6, t6, t2
    mv a0, t5
    lbu t0, (a1)
4:
    vsetvli t5, x0, e8, m8, ta, ma
    vload v8, a0, t5, t2, t3
    csrr t1, vl
    vmseq.vx v0, v8, t0
    vfirst.m t2, v0
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    li t4, -1
    bltz t2, 5f
    mv t4, t2
5:
    bltz t3, 6f
    bltz t4, 7f
    bgeu t3, t4, 6f
7:
    mv t4, t3
6:
    bltz t4, 8f
    add a0, a0, t4
    lbu t1, (a0)
    beqz t1, 9f
    mv t2, a0
    mv t3, a1
    mv t4, t6
10:
    vsetvli t1, t4, e8, m8, ta, ma
    vload v8, t2, t1, a2, t5
    csrr t1, vl
    vsetvli x0, t1, e8, m8, tu, mu
    vle8.v v16, (t3)
    vmsne.vv v0, v8, v16
    vfirst.m a2, v0
    bgez a2, 11f
    sub t4, t4, t1
    beqz t4, 12f
    add t2, t2, t1
    add t3, t3, t1
    j 10b
11:
    addi a0, a0, 1
    j 4b
12:
    ret
8:
    add a0, a0, t1
    j 4b
9:
    li a0, 0
    ret

.globl rt_strpbrk_rvv
.type rt_strpbrk_rvv, @function
.p2align 2
rt_strpbrk_rvv:
    lbu t0, (a1)
    beqz t0, 9f
1:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t4, t5
    csrr t1, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    li t3, -1
    mv t4, a1
2:
    lbu t5, (t4)
    beqz t5, 3f
    vmseq.vx v0, v8, t5
    vfirst.m t6, v0
    bltz t6, 4f
    bltz t2, 6f
    bgeu t6, t2, 4f
6:
    bltz t3, 7f
    bgeu t6, t3, 4f
7:
    mv t3, t6
4:
    addi t4, t4, 1
    j 2b
3:
    bgez t3, 8f
    bgez t2, 9f
    add a0, a0, t1
    j 1b
8:
    add a0, a0, t3
    ret
9:
    li a0, 0
    ret

.globl rt_strscpy_rvv
.type rt_strscpy_rvv, @function
.p2align 2
rt_strscpy_rvv:
    mv t0, a0
    mv t1, a1
    li t4, 0
    beqz a2, 8f
    addi t3, a2, -1
    beqz t3, 7f
1:
    sub t5, t3, t4
    add a0, t1, t4
    vsetvli t6, t5, e8, m8, ta, ma
    vload v8, a0, t6, t2, t5
    csrr t6, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 2f
    add a0, t0, t4
    vse8.v v8, (a0)
    add t4, t4, t6
    bne t4, t3, 1b
    j 7f
2:
    beqz t2, 3f
    vsetvli x0, t2, e8, m8, tu, mu
    add a0, t0, t4
    vse8.v v8, (a0)
    add t4, t4, t2
3:
    add a0, t0, t4
    sb zero, 0(a0)
    mv a0, t4
    ret
7:
    add a0, t0, t4
    sb zero, 0(a0)
8:
    add a0, t1, t4
6:
    vsetvli t6, x0, e8, m8, ta, ma
    vload v8, a0, t6, t2, t5
    csrr t6, vl
    vmseq.vi v0, v8, 0
    vfirst.m t2, v0
    bgez t2, 5f
    add a0, a0, t6
    j 6b
5:
    add a0, a0, t2
    sub a0, a0, t1
    ret

.globl rt_strscat_rvv
.type rt_strscat_rvv, @function
.p2align 2
rt_strscat_rvv:
    addi sp, sp, -32
    sx ra, 24, sp
    sx a0, 16, sp
    sx a1, 8, sp
    sx a2, 0, sp
    mv a1, a2
    call rt_strnlen_rvv
    lx t2, 0, sp
    bgeu a0, t2, 1f
    mv t3, a0
    lx t0, 16, sp
    sub a2, t2, t3
    add a0, t0, t3
    lx a1, 8, sp
    sx t3, 0, sp
    call rt_strscpy_rvv
    lx t3, 0, sp
    add a0, a0, t3
    j 2f
1:
    sx a0, 0, sp
    lx a0, 8, sp
    call rt_strlen_rvv
    lx t3, 0, sp
    add a0, a0, t3
2:
    lx ra, 24, sp
    addi sp, sp, 32
    ret

.globl rt_strpad_rvv
.type rt_strpad_rvv, @function
.p2align 2
rt_strpad_rvv:
    andi a3, a3, 255
    beqz a2, 9f
    mv t1, a2
1:
    vsetvli t2, t1, e8, m8, ta, ma
    vload v8, a1, t2, t0, t3
    csrr t2, vl
    vmseq.vi v0, v8, 0
    vfirst.m t3, v0
    bgez t3, 2f
    vse8.v v8, (a0)
    add a0, a0, t2
    add a1, a1, t2
    sub t1, t1, t2
    bnez t1, 1b
    j 9f
2:
    beqz t3, 3f
    vsetvli x0, t3, e8, m8, tu, mu
    vse8.v v8, (a0)
    add a0, a0, t3
    sub t1, t1, t3
    beqz t1, 9f
3:
    vsetvli t6, x0, e8, m8, ta, ma
    vmv.v.x v8, a3
4:
    vsetvli t2, t1, e8, m8, ta, ma
    vse8.v v8, (a0)
    add a0, a0, t2
    sub t1, t1, t2
    bnez t1, 4b
9:
    ret
