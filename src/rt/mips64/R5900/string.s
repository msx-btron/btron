# Emotion Engine string kernels. A 16-byte load is used only when the
# address is 16-byte aligned and the 16 bytes stay in the page. strcpy
# stores a quad only when that quad has no NUL. Bounded calls also stop
# at n. o32 and n64 pass the arguments in $a0-$a3 and the result in $v0.

.set noreorder
.set noat
.include "ee.inc"

.section .text

.globl rt_strlen_ee
.type rt_strlen_ee, @function
.p2align 4
rt_strlen_ee:
    move $v1, $a0
1:
    ee_page16 $a0, 8f
    andi $t0, $a0, 15
    bnez $t0, 8f
    nop
2:
    ee_page16 $a0, 8f
    ee_lq 8, 4, 0
    ee_pceqb 9, 8, 0
    ee_pcpyud 10, 9, 9
    or $t3, $t1, $t2
    bnez $t3, 8f
    nop
    daddiu $a0, $a0, 16
    b 2b
    nop
8:
    lbu $t0, 0($a0)
    beqz $t0, 9f
    nop
    daddiu $a0, $a0, 1
    b 1b
    nop
9:
    dsubu $v0, $a0, $v1
    jr $ra
    nop

.globl rt_strnlen_ee
.type rt_strnlen_ee, @function
.p2align 4
rt_strnlen_ee:
    move $v1, $a0
    ee_zext_len $a1
    beqz $a1, 9f
    nop
1:
    ee_if_lt $a1, 16, 8f
    ee_page16 $a0, 8f
    andi $t0, $a0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 4, 0
    ee_pceqb 9, 8, 0
    ee_pcpyud 10, 9, 9
    or $t3, $t1, $t2
    bnez $t3, 8f
    nop
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, -16
    b 1b
    nop
8:
    beqz $a1, 9f
    nop
    lbu $t0, 0($a0)
    beqz $t0, 9f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, -1
    b 8b
    nop
9:
    dsubu $v0, $a0, $v1
    jr $ra
    nop

.globl rt_strcpy_ee
.type rt_strcpy_ee, @function
.p2align 4
rt_strcpy_ee:
    move $v0, $a0
1:
    ee_page16 $a1, 8f
    or $t0, $a0, $a1
    andi $t0, $t0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 5, 0
    ee_pceqb 9, 8, 0
    ee_pcpyud 10, 9, 9
    or $t3, $t1, $t2
    bnez $t3, 8f
    nop
    ee_sq 8, 4, 0
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    b 1b
    nop
8:
    lbu $t0, 0($a1)
    sb $t0, 0($a0)
    beqz $t0, 9f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    b 1b
    nop
9:
    jr $ra
    nop

.globl rt_strncpy_ee
.type rt_strncpy_ee, @function
.p2align 4
rt_strncpy_ee:
    move $v0, $a0
    ee_zext_len $a2
    beqz $a2, 9f
    nop
1:
    ee_if_lt $a2, 16, 8f
    ee_page16 $a1, 8f
    or $t0, $a0, $a1
    andi $t0, $t0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 5, 0
    ee_pceqb 9, 8, 0
    ee_pcpyud 10, 9, 9
    or $t3, $t1, $t2
    bnez $t3, 8f
    nop
    ee_sq 8, 4, 0
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    daddiu $a2, $a2, -16
    b 1b
    nop
8:
    beqz $a2, 9f
    nop
    lbu $t0, 0($a1)
    beqz $t0, 4f
    nop
    sb $t0, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
4:
    sb $zero, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a2, $a2, -1
    bnez $a2, 4b
    nop
9:
    jr $ra
    nop

.globl rt_strcat_ee
.type rt_strcat_ee, @function
.p2align 4
rt_strcat_ee:
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    sd $a0, 8($sp)
    sd $a1, 16($sp)
    jal rt_strlen_ee
    nop
    ld $a0, 8($sp)
    ld $a1, 16($sp)
    daddu $a0, $a0, $v0
    jal rt_strcpy_ee
    nop
    ld $v0, 8($sp)
    ld $ra, 0($sp)
    daddiu $sp, $sp, 32
    jr $ra
    nop

.globl rt_strncat_ee
.type rt_strncat_ee, @function
.p2align 4
rt_strncat_ee:
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    sd $a0, 8($sp)
    sd $a1, 16($sp)
    sd $a2, 24($sp)
    jal rt_strlen_ee
    nop
    ld $a0, 8($sp)
    ld $a1, 16($sp)
    ld $a2, 24($sp)
    daddu $a0, $a0, $v0
    ee_zext_len $a2
1:
    beqz $a2, 2f
    nop
    lbu $t0, 0($a1)
    beqz $t0, 2f
    nop
    sb $t0, 0($a0)
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
2:
    sb $zero, 0($a0)
    ld $v0, 8($sp)
    ld $ra, 0($sp)
    daddiu $sp, $sp, 32
    jr $ra
    nop

.globl rt_strcmp_ee
.type rt_strcmp_ee, @function
.p2align 4
rt_strcmp_ee:
1:
    ee_page16 $a0, 8f
    ee_page16 $a1, 8f
    or $t0, $a0, $a1
    andi $t0, $t0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 4, 0
    ee_lq 9, 5, 0
    ee_pxor 10, 8, 9
    ee_pcpyud 11, 10, 10
    or $t4, $t2, $t3
    bnez $t4, 8f
    nop
    ee_pceqb 9, 8, 0
    ee_pcpyud 10, 9, 9
    or $t3, $t1, $t2
    bnez $t3, 8f
    nop
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    b 1b
    nop
8:
    lbu $t0, 0($a0)
    lbu $t1, 0($a1)
    bne $t0, $t1, 7f
    nop
    beqz $t0, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    b 1b
    nop
7:
    subu $v0, $t0, $t1
    jr $ra
    nop

.globl rt_strncmp_ee
.type rt_strncmp_ee, @function
.p2align 4
rt_strncmp_ee:
    ee_zext_len $a2
    beqz $a2, 6f
    nop
1:
    ee_if_lt $a2, 16, 8f
    ee_page16 $a0, 8f
    ee_page16 $a1, 8f
    or $t0, $a0, $a1
    andi $t0, $t0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 4, 0
    ee_lq 9, 5, 0
    ee_pxor 10, 8, 9
    ee_pcpyud 11, 10, 10
    or $t4, $t2, $t3
    bnez $t4, 8f
    nop
    ee_pceqb 9, 8, 0
    ee_pcpyud 10, 9, 9
    or $t3, $t1, $t2
    bnez $t3, 8f
    nop
    daddiu $a0, $a0, 16
    daddiu $a1, $a1, 16
    daddiu $a2, $a2, -16
    b 1b
    nop
8:
    beqz $a2, 6f
    nop
    lbu $t0, 0($a0)
    lbu $t1, 0($a1)
    bne $t0, $t1, 7f
    nop
    beqz $t0, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    daddiu $a2, $a2, -1
    b 1b
    nop
6:
    move $v0, $zero
    jr $ra
    nop
7:
    subu $v0, $t0, $t1
    jr $ra
    nop

.globl rt_strchr_ee
.type rt_strchr_ee, @function
.p2align 4
rt_strchr_ee:
    andi $a1, $a1, 255
    move $t5, $a1
    dsll $t0, $t5, 8
    or $t5, $t5, $t0
    dsll $t0, $t5, 16
    or $t5, $t5, $t0
    dsll $t0, $t5, 32
    or $t5, $t5, $t0
    ee_pcpyld 13, 13, 13
1:
    ee_page16 $a0, 8f
    andi $t0, $a0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 4, 0
    ee_pceqb 9, 8, 0
    ee_pceqb 10, 8, 13
    ee_pcpyud 11, 9, 9
    or $t4, $t1, $t3
    ee_pcpyud 11, 10, 10
    or $t6, $t2, $t3
    or $t4, $t4, $t6
    bnez $t4, 8f
    nop
    daddiu $a0, $a0, 16
    b 1b
    nop
8:
    lbu $t0, 0($a0)
    beq $t0, $a1, 7f
    nop
    beqz $t0, 6f
    nop
    daddiu $a0, $a0, 1
    b 1b
    nop
7:
    move $v0, $a0
    jr $ra
    nop
6:
    move $v0, $zero
    jr $ra
    nop

.globl rt_strrchr_ee
.type rt_strrchr_ee, @function
.p2align 4
rt_strrchr_ee:
    andi $a1, $a1, 255
    move $v0, $zero
    move $t5, $a1
    dsll $t0, $t5, 8
    or $t5, $t5, $t0
    dsll $t0, $t5, 16
    or $t5, $t5, $t0
    dsll $t0, $t5, 32
    or $t5, $t5, $t0
    ee_pcpyld 13, 13, 13
1:
    ee_page16 $a0, 8f
    andi $t0, $a0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 4, 0
    ee_pceqb 9, 8, 0
    ee_pceqb 10, 8, 13
    ee_pcpyud 11, 9, 9
    or $t4, $t1, $t3
    ee_pcpyud 11, 10, 10
    or $t6, $t2, $t3
    or $t4, $t4, $t6
    bnez $t4, 8f
    nop
    daddiu $a0, $a0, 16
    b 1b
    nop
8:
    lbu $t0, 0($a0)
    bne $t0, $a1, 3f
    nop
    move $v0, $a0
3:
    beqz $t0, 9f
    nop
    daddiu $a0, $a0, 1
    b 1b
    nop
9:
    jr $ra
    nop

.globl rt_strstr_ee
.type rt_strstr_ee, @function
.p2align 4
rt_strstr_ee:
    lbu $a2, 0($a1)
    bnez $a2, 1f
    nop
    move $v0, $a0
    jr $ra
    nop
1:
    move $t5, $a2
    dsll $t0, $t5, 8
    or $t5, $t5, $t0
    dsll $t0, $t5, 16
    or $t5, $t5, $t0
    dsll $t0, $t5, 32
    or $t5, $t5, $t0
    ee_pcpyld 13, 13, 13
2:
    ee_page16 $a0, 8f
    andi $t0, $a0, 15
    bnez $t0, 8f
    nop
    ee_lq 8, 4, 0
    ee_pceqb 9, 8, 0
    ee_pceqb 10, 8, 13
    ee_pcpyud 11, 9, 9
    or $t4, $t1, $t3
    ee_pcpyud 11, 10, 10
    or $t6, $t2, $t3
    or $t4, $t4, $t6
    bnez $t4, 8f
    nop
    daddiu $a0, $a0, 16
    b 2b
    nop
8:
    lbu $t0, 0($a0)
    beqz $t0, 6f
    nop
    bne $t0, $a2, 4f
    nop
    move $t8, $a0
    move $t9, $a1
3:
    lbu $t0, 0($t9)
    beqz $t0, 7f
    nop
    lbu $t1, 0($t8)
    bne $t0, $t1, 4f
    nop
    daddiu $t8, $t8, 1
    daddiu $t9, $t9, 1
    b 3b
    nop
4:
    daddiu $a0, $a0, 1
    b 2b
    nop
7:
    move $v0, $a0
    jr $ra
    nop
6:
    move $v0, $zero
    jr $ra
    nop

.globl rt_strpbrk_ee
.type rt_strpbrk_ee, @function
.p2align 4
rt_strpbrk_ee:
    daddiu $sp, $sp, -32
    sd $zero, 0($sp)
    sd $zero, 8($sp)
    sd $zero, 16($sp)
    sd $zero, 24($sp)
    lbu $t0, 0($a1)
    beqz $t0, 6f
    nop
1:
    lbu $t0, 0($a1)
    beqz $t0, 2f
    nop
    andi $t1, $t0, 7
    srl $t2, $t0, 3
    addu $t2, $sp, $t2
    lbu $t3, 0($t2)
    li $t4, 1
    sllv $t4, $t4, $t1
    or $t3, $t3, $t4
    sb $t3, 0($t2)
    daddiu $a1, $a1, 1
    b 1b
    nop
2:
    lbu $t0, 0($a0)
    beqz $t0, 6f
    nop
    andi $t1, $t0, 7
    srl $t2, $t0, 3
    addu $t2, $sp, $t2
    lbu $t3, 0($t2)
    li $t4, 1
    sllv $t4, $t4, $t1
    and $t3, $t3, $t4
    bnez $t3, 7f
    nop
    daddiu $a0, $a0, 1
    b 2b
    nop
7:
    move $v0, $a0
    daddiu $sp, $sp, 32
    jr $ra
    nop
6:
    move $v0, $zero
    daddiu $sp, $sp, 32
    jr $ra
    nop

.globl rt_strscpy_ee
.type rt_strscpy_ee, @function
.p2align 4
rt_strscpy_ee:
    ee_zext_len $a2
    bnez $a2, 1f
    nop
    daddiu $sp, $sp, -16
    sd $ra, 0($sp)
    move $a0, $a1
    jal rt_strlen_ee
    nop
    ld $ra, 0($sp)
    daddiu $sp, $sp, 16
    jr $ra
    nop
1:
    move $v1, $zero
2:
    daddiu $t0, $v1, 1
    sltu $t1, $t0, $a2
    beqz $t1, 3f
    nop
    daddu $t2, $a1, $v1
    lbu $t3, 0($t2)
    beqz $t3, 3f
    nop
    daddu $t2, $a0, $v1
    sb $t3, 0($t2)
    daddiu $v1, $v1, 1
    b 2b
    nop
3:
    daddu $t2, $a0, $v1
    sb $zero, 0($t2)
4:
    daddu $t2, $a1, $v1
    lbu $t3, 0($t2)
    beqz $t3, 5f
    nop
    daddiu $v1, $v1, 1
    b 4b
    nop
5:
    move $v0, $v1
    jr $ra
    nop

.globl rt_strscat_ee
.type rt_strscat_ee, @function
.p2align 4
rt_strscat_ee:
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    sd $a0, 8($sp)
    sd $a1, 16($sp)
    sd $a2, 24($sp)
    move $a1, $a2
    jal rt_strnlen_ee
    nop
    ld $a2, 24($sp)
    sltu $t0, $v0, $a2
    beqz $t0, 2f
    nop
    ld $a0, 8($sp)
    ld $a1, 16($sp)
    daddu $a0, $a0, $v0
    dsubu $a2, $a2, $v0
    sd $v0, 24($sp)
    jal rt_strscpy_ee
    nop
    ld $t0, 24($sp)
    daddu $v0, $v0, $t0
    b 9f
    nop
2:
    ld $a0, 16($sp)
    sd $v0, 24($sp)
    jal rt_strlen_ee
    nop
    ld $t0, 24($sp)
    daddu $v0, $v0, $t0
9:
    ld $ra, 0($sp)
    daddiu $sp, $sp, 32
    jr $ra
    nop

.globl rt_strpad_ee
.type rt_strpad_ee, @function
.p2align 4
rt_strpad_ee:
    ee_zext_len $a2
    andi $a3, $a3, 255
    move $v1, $zero
1:
    sltu $t0, $v1, $a2
    beqz $t0, 9f
    nop
    daddu $t1, $a1, $v1
    lbu $t2, 0($t1)
    beqz $t2, 2f
    nop
    daddu $t1, $a0, $v1
    sb $t2, 0($t1)
    daddiu $v1, $v1, 1
    b 1b
    nop
2:
    sltu $t0, $v1, $a2
    beqz $t0, 9f
    nop
    daddu $t1, $a0, $v1
    sb $a3, 0($t1)
    daddiu $v1, $v1, 1
    b 2b
    nop
9:
    jr $ra
    nop
