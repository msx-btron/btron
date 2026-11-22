# Emotion Engine memory kernels. LQ/SQ move 16 bytes when both addresses
# are 16-byte aligned; the R5900 forces those addresses down to a line,
# so a misaligned pair is copied with ld/sd or bytes. A quad that is
# stored is a full line the caller asked for. n64 and o32 pass the
# three arguments in $a0-$a2 and take the result in $v0.

.set noreorder
.set noat
.include "ee.inc"

.section .text

.globl rt_memcpy_ee
.type rt_memcpy_ee, @function
.p2align 4
rt_memcpy_ee:
    move $v0, $a0
    ee_zext_len $a2
    beqz $a2, 9f
    nop
    or $t0, $a0, $a1
    andi $t0, $t0, 15
    beqz $t0, 3f
    nop
    xor $t1, $a0, $a1
    andi $t1, $t1, 15
    bnez $t1, 8f
    nop
1:
    andi $t0, $a0, 15
    beqz $t0, 3f
    nop
    beqz $a2, 9f
    nop
    lbu $t0, 0($a1)
    sb $t0, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
3:
    ee_if_lt $a2, 64, 4f
    ee_lq 8, 5, 0
    ee_lq 9, 5, 16
    ee_lq 10, 5, 32
    ee_lq 11, 5, 48
    ee_sq 8, 4, 0
    ee_sq 9, 4, 16
    ee_sq 10, 4, 32
    ee_sq 11, 4, 48
    daddiu $a0, $a0, 64
    daddiu $a1, $a1, 64
    daddiu $a2, $a2, -64
    b 3b
    nop
4:
    ee_if_lt $a2, 16, 5f
    ee_lq 8, 5, 0
    ee_sq 8, 4, 0
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    daddiu $a2, $a2, -16
    b 4b
    nop
5:
    ee_if_lt $a2, 8, 8f
    ld $t0, 0($a1)
    sd $t0, 0($a0)
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    daddiu $a2, $a2, -8
    b 5b
    nop
8:
    beqz $a2, 9f
    nop
    lbu $t0, 0($a1)
    sb $t0, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 8b
    nop
9:
    jr $ra
    nop

.globl rt_memset_ee
.type rt_memset_ee, @function
.p2align 4
rt_memset_ee:
    move $v0, $a0
    ee_zext_len $a2
    andi $a1, $a1, 255
    move $t4, $a1
    dsll $t0, $t4, 8
    or $t4, $t4, $t0
    dsll $t0, $t4, 16
    or $t4, $t4, $t0
    dsll $t0, $t4, 32
    or $t4, $t4, $t0
    ee_pcpyld 12, 12, 12
    beqz $a2, 9f
    nop
    andi $t0, $a0, 15
    beqz $t0, 3f
    nop
1:
    andi $t0, $a0, 15
    beqz $t0, 3f
    nop
    beqz $a2, 9f
    nop
    sb $a1, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
3:
    ee_if_lt $a2, 64, 4f
    ee_sq 12, 4, 0
    ee_sq 12, 4, 16
    ee_sq 12, 4, 32
    ee_sq 12, 4, 48
    daddiu $a0, $a0, 64
    daddiu $a2, $a2, -64
    b 3b
    nop
4:
    ee_if_lt $a2, 16, 5f
    ee_sq 12, 4, 0
    daddiu $a0, $a0, 16
    daddiu $a2, $a2, -16
    b 4b
    nop
5:
    ee_if_lt $a2, 8, 8f
    sd $t4, 0($a0)
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

.globl rt_memcmp_ee
.type rt_memcmp_ee, @function
.p2align 4
rt_memcmp_ee:
    move $v0, $zero
    ee_zext_len $a2
    beqz $a2, 9f
    nop
    or $t0, $a0, $a1
    andi $t0, $t0, 15
    beqz $t0, 3f
    nop
    xor $t1, $a0, $a1
    andi $t1, $t1, 15
    bnez $t1, 8f
    nop
1:
    andi $t0, $a0, 15
    beqz $t0, 3f
    nop
    beqz $a2, 9f
    nop
    lbu $t0, 0($a0)
    lbu $t1, 0($a1)
    bne $t0, $t1, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
3:
    ee_if_lt $a2, 16, 5f
    ee_lq 8, 4, 0
    ee_lq 9, 5, 0
    ee_pxor 10, 8, 9
    ee_pcpyud 11, 10, 10
    or $t4, $t2, $t3
    bnez $t4, 6f
    nop
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    daddiu $a2, $a2, -16
    b 3b
    nop
5:
    ee_if_lt $a2, 8, 8f
    ld $t0, 0($a0)
    ld $t1, 0($a1)
    bne $t0, $t1, 6f
    nop
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    daddiu $a2, $a2, -8
    b 5b
    nop
6:
    /* The wide load stayed inside n. Walk those bytes. */
    lbu $t0, 0($a0)
    lbu $t1, 0($a1)
    bne $t0, $t1, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    bnez $a2, 6b
    nop
    jr $ra
    nop
7:
    subu $v0, $t0, $t1
    jr $ra
    nop
8:
    beqz $a2, 9f
    nop
    lbu $t0, 0($a0)
    lbu $t1, 0($a1)
    bne $t0, $t1, 7b
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 8b
    nop
9:
    jr $ra
    nop
