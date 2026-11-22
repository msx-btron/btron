# RV32GCV. Same vsetvli e8 m8 loop as RV64: VLEN does not follow XLEN,
# so one vle8/vse8 still moves VL bytes and stops on n.
# a0-a7 and t0 are XLEN wide (32 here). Do not assemble this into an
# RV64 link; the symbol names match the RV64 file on purpose.

.section .text
.attribute arch, "rv32gcv"

.globl rt_memcpy_rvv
.type rt_memcpy_rvv, @function
.p2align 2
rt_memcpy_rvv:
    mv t1, a0
    beqz a2, 9f
    # One VLMAX vsetvli for the bulk. The tail sets vl to the remainder
    # so the last vle8 stops at n.
    vsetvli t0, x0, e8, m8, ta, ma
    bltu a2, t0, 2f
1:
    vle8.v v0, (a1)
    vse8.v v0, (a0)
    add a0, a0, t0
    add a1, a1, t0
    sub a2, a2, t0
    bgeu a2, t0, 1b
    beqz a2, 9f
2:
    vsetvli t0, a2, e8, m8, ta, ma
    vle8.v v0, (a1)
    vse8.v v0, (a0)
9:
    mv a0, t1
    ret

.globl rt_memset_rvv
.type rt_memset_rvv, @function
.p2align 2
rt_memset_rvv:
    mv t1, a0
    beqz a2, 9f
    andi a1, a1, 255
    vsetvli t0, x0, e8, m8, ta, ma
    vmv.v.x v0, a1
    bltu a2, t0, 2f
1:
    vse8.v v0, (a0)
    add a0, a0, t0
    sub a2, a2, t0
    bgeu a2, t0, 1b
    beqz a2, 9f
2:
    # tu keeps the splat in the lanes the tail does not write.
    vsetvli x0, a2, e8, m8, tu, mu
    vse8.v v0, (a0)
9:
    mv a0, t1
    ret

.globl rt_memcmp_rvv
.type rt_memcmp_rvv, @function
.p2align 2
rt_memcmp_rvv:
    beqz a2, 3f
    vsetvli t0, x0, e8, m8, ta, ma
    bltu a2, t0, 4f
1:
    vle8.v v8, (a0)
    vle8.v v16, (a1)
    vmsne.vv v0, v8, v16
    vfirst.m t1, v0
    bgez t1, 2f
    add a0, a0, t0
    add a1, a1, t0
    sub a2, a2, t0
    bgeu a2, t0, 1b
    beqz a2, 3f
4:
    vsetvli t0, a2, e8, m8, ta, ma
    vle8.v v8, (a0)
    vle8.v v16, (a1)
    vmsne.vv v0, v8, v16
    vfirst.m t1, v0
    bltz t1, 3f
2:
    add a0, a0, t1
    add a1, a1, t1
    lbu t2, (a0)
    lbu t3, (a1)
    sub a0, t2, t3
    ret
3:
    li a0, 0
    ret
