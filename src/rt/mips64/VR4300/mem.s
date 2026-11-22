# Nintendo 64 memory kernels for the VR4300, not the RSP.
# The RSP is a separate core. Its scalar unit is 32-bit and has no ld/sd,
# and its vector unit only sees the 4 KiB DMEM. A game calls these from
# the VR4300. The fast path is two ld/sd per 16 bytes, unrolled to 64.
# A 16-byte cache line must not be written by both this CPU and the RCP.
# n64 and o32 pass the three arguments in $a0-$a2 and take the result in $v0.

.set noreorder
.set noat
.include "rcp.inc"

.section .text

.globl rt_memcpy_rcp
.type rt_memcpy_rcp, @function
.p2align 4
rt_memcpy_rcp:
    move $v0, $a0
    rcp_zext_len $a2
    beqz $a2, 9f
    nop
    or $8, $a0, $a1
    andi $8, $8, 7
    beqz $8, 3f
    nop
    xor $9, $a0, $a1
    andi $9, $9, 7
    bnez $9, 8f
    nop
1:
    andi $8, $a0, 7
    beqz $8, 3f
    nop
    beqz $a2, 9f
    nop
    lbu $8, 0($a1)
    sb $8, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
3:
    rcp_if_lt $a2, 64, 4f
    ld $8, 0($a1)
    ld $9, 8($a1)
    ld $10, 16($a1)
    ld $11, 24($a1)
    ld $12, 32($a1)
    ld $13, 40($a1)
    ld $14, 48($a1)
    ld $15, 56($a1)
    sd $8, 0($a0)
    sd $9, 8($a0)
    sd $10, 16($a0)
    sd $11, 24($a0)
    sd $12, 32($a0)
    sd $13, 40($a0)
    sd $14, 48($a0)
    sd $15, 56($a0)
    daddiu $a0, $a0, 64
    daddiu $a1, $a1, 64
    daddiu $a2, $a2, -64
    b 3b
    nop
4:
    rcp_if_lt $a2, 16, 5f
    ld $8, 0($a1)
    ld $9, 8($a1)
    sd $8, 0($a0)
    sd $9, 8($a0)
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    daddiu $a2, $a2, -16
    b 4b
    nop
5:
    rcp_if_lt $a2, 8, 8f
    ld $8, 0($a1)
    sd $8, 0($a0)
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    daddiu $a2, $a2, -8
    b 5b
    nop
8:
    beqz $a2, 9f
    nop
    lbu $8, 0($a1)
    sb $8, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 8b
    nop
9:
    jr $ra
    nop

.globl rt_memset_rcp
.type rt_memset_rcp, @function
.p2align 4
rt_memset_rcp:
    move $v0, $a0
    rcp_zext_len $a2
    andi $a1, $a1, 255
    move $12, $a1
    dsll $8, $12, 8
    or $12, $12, $8
    dsll $8, $12, 16
    or $12, $12, $8
    dsll32 $8, $12, 0
    or $12, $12, $8
    beqz $a2, 9f
    nop
    andi $8, $a0, 7
    beqz $8, 3f
    nop
1:
    andi $8, $a0, 7
    beqz $8, 3f
    nop
    beqz $a2, 9f
    nop
    sb $a1, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
3:
    rcp_if_lt $a2, 64, 4f
    sd $12, 0($a0)
    sd $12, 8($a0)
    sd $12, 16($a0)
    sd $12, 24($a0)
    sd $12, 32($a0)
    sd $12, 40($a0)
    sd $12, 48($a0)
    sd $12, 56($a0)
    daddiu $a0, $a0, 64
    daddiu $a2, $a2, -64
    b 3b
    nop
4:
    rcp_if_lt $a2, 16, 5f
    sd $12, 0($a0)
    sd $12, 8($a0)
    daddiu $a0, $a0, 16
    daddiu $a2, $a2, -16
    b 4b
    nop
5:
    rcp_if_lt $a2, 8, 8f
    sd $12, 0($a0)
    daddiu $a0, $a0, 8
    daddiu $a2, $a2, -8
    b 5b
    nop
8:
    beqz $a2, 9f
    nop
    sb $a1, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a2, $a2, -1
    b 8b
    nop
9:
    jr $ra
    nop

.globl rt_memcmp_rcp
.type rt_memcmp_rcp, @function
.p2align 4
rt_memcmp_rcp:
    move $v0, $zero
    rcp_zext_len $a2
    beqz $a2, 9f
    nop
    or $8, $a0, $a1
    andi $8, $8, 7
    beqz $8, 3f
    nop
    xor $9, $a0, $a1
    andi $9, $9, 7
    bnez $9, 8f
    nop
1:
    andi $8, $a0, 7
    beqz $8, 3f
    nop
    beqz $a2, 9f
    nop
    lbu $8, 0($a0)
    lbu $9, 0($a1)
    bne $8, $9, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
3:
    rcp_if_lt $a2, 8, 8f
    ld $8, 0($a0)
    ld $9, 0($a1)
    bne $8, $9, 6f
    nop
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    daddiu $a2, $a2, -8
    b 3b
    nop
6:
    lbu $8, 0($a0)
    lbu $9, 0($a1)
    bne $8, $9, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    bnez $a2, 6b
    nop
    jr $ra
    nop
7:
    subu $v0, $8, $9
    jr $ra
    nop
8:
    beqz $a2, 9f
    nop
    lbu $8, 0($a0)
    lbu $9, 0($a1)
    bne $8, $9, 7b
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 8b
    nop
9:
    jr $ra
    nop
