# Nintendo 64 string kernels for the VR4300, not the RSP vector unit.
# An 8-byte load is used only when the address is 8-byte aligned and the
# 8 bytes stay in the page. strcpy stores a dword only when that dword
# has no NUL. Bounded calls also stop at n. o32 and n64 pass the
# arguments in $a0-$a3 and the result in $v0.

.set noreorder
.set noat
.include "rcp.inc"

.section .text

.globl rt_strlen_rcp
.type rt_strlen_rcp, @function
.p2align 4
rt_strlen_rcp:
    rcp_consts
    move $v1, $a0
1:
    rcp_page8 $a0, 8f
    andi $8, $a0, 7
    bnez $8, 8f
    nop
2:
    rcp_page8 $a0, 8f
    ld $8, 0($a0)
    rcp_haszero $8, $11
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    b 2b
    nop
8:
    lbu $8, 0($a0)
    beqz $8, 9f
    nop
    daddiu $a0, $a0, 1
    b 1b
    nop
9:
    dsubu $v0, $a0, $v1
    jr $ra
    nop

.globl rt_strnlen_rcp
.type rt_strnlen_rcp, @function
.p2align 4
rt_strnlen_rcp:
    rcp_consts
    move $v1, $a0
    rcp_zext_len $a1
    beqz $a1, 9f
    nop
1:
    rcp_if_lt $a1, 8, 8f
    rcp_page8 $a0, 8f
    andi $8, $a0, 7
    bnez $8, 8f
    nop
    ld $8, 0($a0)
    rcp_haszero $8, $11
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, -8
    b 1b
    nop
8:
    beqz $a1, 9f
    nop
    lbu $8, 0($a0)
    beqz $8, 9f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, -1
    b 8b
    nop
9:
    dsubu $v0, $a0, $v1
    jr $ra
    nop

.globl rt_strcpy_rcp
.type rt_strcpy_rcp, @function
.p2align 4
rt_strcpy_rcp:
    rcp_consts
    move $v0, $a0
1:
    rcp_page8 $a1, 8f
    or $8, $a0, $a1
    andi $8, $8, 7
    bnez $8, 8f
    nop
    ld $8, 0($a1)
    rcp_haszero $8, $11
    bnez $11, 8f
    nop
    sd $8, 0($a0)
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    b 1b
    nop
8:
    lbu $8, 0($a1)
    sb $8, 0($a0)
    beqz $8, 9f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    b 1b
    nop
9:
    jr $ra
    nop

.globl rt_strncpy_rcp
.type rt_strncpy_rcp, @function
.p2align 4
rt_strncpy_rcp:
    rcp_consts
    move $v0, $a0
    rcp_zext_len $a2
    beqz $a2, 9f
    nop
1:
    rcp_if_lt $a2, 8, 8f
    rcp_page8 $a1, 8f
    or $8, $a0, $a1
    andi $8, $8, 7
    bnez $8, 8f
    nop
    ld $8, 0($a1)
    rcp_haszero $8, $11
    bnez $11, 8f
    nop
    sd $8, 0($a0)
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    daddiu $a2, $a2, -8
    b 1b
    nop
8:
    beqz $a2, 9f
    nop
    lbu $8, 0($a1)
    beqz $8, 4f
    nop
    sb $8, 0($a0)
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

.globl rt_strcat_rcp
.type rt_strcat_rcp, @function
.p2align 4
rt_strcat_rcp:
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    sd $a0, 8($sp)
    sd $a1, 16($sp)
    jal rt_strlen_rcp
    nop
    ld $a0, 8($sp)
    ld $a1, 16($sp)
    daddu $a0, $a0, $v0
    jal rt_strcpy_rcp
    nop
    ld $v0, 8($sp)
    ld $ra, 0($sp)
    daddiu $sp, $sp, 32
    jr $ra
    nop

.globl rt_strncat_rcp
.type rt_strncat_rcp, @function
.p2align 4
rt_strncat_rcp:
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    sd $a0, 8($sp)
    sd $a1, 16($sp)
    sd $a2, 24($sp)
    jal rt_strlen_rcp
    nop
    ld $a0, 8($sp)
    ld $a1, 16($sp)
    ld $a2, 24($sp)
    daddu $a0, $a0, $v0
    rcp_zext_len $a2
1:
    beqz $a2, 2f
    nop
    lbu $8, 0($a1)
    beqz $8, 2f
    nop
    sb $8, 0($a0)
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

.globl rt_strcmp_rcp
.type rt_strcmp_rcp, @function
.p2align 4
rt_strcmp_rcp:
    rcp_consts
1:
    rcp_page8 $a0, 8f
    rcp_page8 $a1, 8f
    or $8, $a0, $a1
    andi $8, $8, 7
    bnez $8, 8f
    nop
    ld $8, 0($a0)
    ld $9, 0($a1)
    xor $10, $8, $9
    bnez $10, 8f
    nop
    rcp_haszero $8, $11
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    b 1b
    nop
8:
    lbu $8, 0($a0)
    lbu $9, 0($a1)
    bne $8, $9, 7f
    nop
    beqz $8, 7f
    nop
    daddiu $a0, $a0, 1
    daddiu $a1, $a1, 1
    b 1b
    nop
7:
    subu $v0, $8, $9
    jr $ra
    nop

.globl rt_strncmp_rcp
.type rt_strncmp_rcp, @function
.p2align 4
rt_strncmp_rcp:
    rcp_consts
    rcp_zext_len $a2
    beqz $a2, 6f
    nop
1:
    rcp_if_lt $a2, 8, 8f
    rcp_page8 $a0, 8f
    rcp_page8 $a1, 8f
    or $8, $a0, $a1
    andi $8, $8, 7
    bnez $8, 8f
    nop
    ld $8, 0($a0)
    ld $9, 0($a1)
    xor $10, $8, $9
    bnez $10, 8f
    nop
    rcp_haszero $8, $11
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    daddiu $a1, $a1, 8
    daddiu $a2, $a2, -8
    b 1b
    nop
8:
    beqz $a2, 6f
    nop
    lbu $8, 0($a0)
    lbu $9, 0($a1)
    bne $8, $9, 7f
    nop
    beqz $8, 7f
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
    subu $v0, $8, $9
    jr $ra
    nop

.globl rt_strchr_rcp
.type rt_strchr_rcp, @function
.p2align 4
rt_strchr_rcp:
    rcp_consts
    andi $a1, $a1, 255
    move $13, $a1
    dsll $8, $13, 8
    or $13, $13, $8
    dsll $8, $13, 16
    or $13, $13, $8
    dsll32 $8, $13, 0
    or $13, $13, $8
1:
    rcp_page8 $a0, 8f
    andi $8, $a0, 7
    bnez $8, 8f
    nop
    ld $8, 0($a0)
    rcp_haszero $8, $11
    xor $10, $8, $13
    rcp_haszero $10, $9
    or $11, $11, $9
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    b 1b
    nop
8:
    lbu $8, 0($a0)
    beq $8, $a1, 7f
    nop
    beqz $8, 6f
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

.globl rt_strrchr_rcp
.type rt_strrchr_rcp, @function
.p2align 4
rt_strrchr_rcp:
    rcp_consts
    andi $a1, $a1, 255
    move $v0, $zero
    move $13, $a1
    dsll $8, $13, 8
    or $13, $13, $8
    dsll $8, $13, 16
    or $13, $13, $8
    dsll32 $8, $13, 0
    or $13, $13, $8
1:
    rcp_page8 $a0, 8f
    andi $8, $a0, 7
    bnez $8, 8f
    nop
    ld $8, 0($a0)
    rcp_haszero $8, $11
    xor $10, $8, $13
    rcp_haszero $10, $9
    or $11, $11, $9
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    b 1b
    nop
8:
    lbu $8, 0($a0)
    bne $8, $a1, 3f
    nop
    move $v0, $a0
3:
    beqz $8, 9f
    nop
    daddiu $a0, $a0, 1
    b 1b
    nop
9:
    jr $ra
    nop

.globl rt_strstr_rcp
.type rt_strstr_rcp, @function
.p2align 4
rt_strstr_rcp:
    lbu $a2, 0($a1)
    bnez $a2, 1f
    nop
    move $v0, $a0
    jr $ra
    nop
1:
    rcp_consts
    move $13, $a2
    dsll $8, $13, 8
    or $13, $13, $8
    dsll $8, $13, 16
    or $13, $13, $8
    dsll32 $8, $13, 0
    or $13, $13, $8
2:
    rcp_page8 $a0, 8f
    andi $8, $a0, 7
    bnez $8, 8f
    nop
    ld $8, 0($a0)
    rcp_haszero $8, $11
    xor $10, $8, $13
    rcp_haszero $10, $9
    or $11, $11, $9
    bnez $11, 8f
    nop
    daddiu $a0, $a0, 8
    b 2b
    nop
8:
    lbu $8, 0($a0)
    beqz $8, 6f
    nop
    bne $8, $a2, 4f
    nop
    move $14, $a0
    move $15, $a1
3:
    lbu $8, 0($15)
    beqz $8, 7f
    nop
    lbu $9, 0($14)
    bne $8, $9, 4f
    nop
    daddiu $14, $14, 1
    daddiu $15, $15, 1
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

.globl rt_strpbrk_rcp
.type rt_strpbrk_rcp, @function
.p2align 4
rt_strpbrk_rcp:
    daddiu $sp, $sp, -32
    sd $zero, 0($sp)
    sd $zero, 8($sp)
    sd $zero, 16($sp)
    sd $zero, 24($sp)
    lbu $8, 0($a1)
    beqz $8, 6f
    nop
1:
    lbu $8, 0($a1)
    beqz $8, 2f
    nop
    andi $9, $8, 7
    srl $10, $8, 3
    daddu $10, $sp, $10
    lbu $11, 0($10)
    li $12, 1
    sllv $12, $12, $9
    or $11, $11, $12
    sb $11, 0($10)
    daddiu $a1, $a1, 1
    b 1b
    nop
2:
    lbu $8, 0($a0)
    beqz $8, 6f
    nop
    andi $9, $8, 7
    srl $10, $8, 3
    daddu $10, $sp, $10
    lbu $11, 0($10)
    li $12, 1
    sllv $12, $12, $9
    and $11, $11, $12
    bnez $11, 7f
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

.globl rt_strscpy_rcp
.type rt_strscpy_rcp, @function
.p2align 4
rt_strscpy_rcp:
    rcp_zext_len $a2
    bnez $a2, 1f
    nop
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    move $a0, $a1
    jal rt_strlen_rcp
    nop
    ld $ra, 0($sp)
    daddiu $sp, $sp, 32
    jr $ra
    nop
1:
    move $v1, $zero
2:
    daddiu $8, $v1, 1
    sltu $9, $8, $a2
    beqz $9, 3f
    nop
    daddu $10, $a1, $v1
    lbu $11, 0($10)
    beqz $11, 3f
    nop
    daddu $10, $a0, $v1
    sb $11, 0($10)
    daddiu $v1, $v1, 1
    b 2b
    nop
3:
    daddu $10, $a0, $v1
    sb $zero, 0($10)
4:
    daddu $10, $a1, $v1
    lbu $11, 0($10)
    beqz $11, 5f
    nop
    daddiu $v1, $v1, 1
    b 4b
    nop
5:
    move $v0, $v1
    jr $ra
    nop

.globl rt_strscat_rcp
.type rt_strscat_rcp, @function
.p2align 4
rt_strscat_rcp:
    daddiu $sp, $sp, -32
    sd $ra, 0($sp)
    sd $a0, 8($sp)
    sd $a1, 16($sp)
    sd $a2, 24($sp)
    move $a1, $a2
    jal rt_strnlen_rcp
    nop
    ld $a2, 24($sp)
    sltu $8, $v0, $a2
    beqz $8, 2f
    nop
    ld $a0, 8($sp)
    ld $a1, 16($sp)
    daddu $a0, $a0, $v0
    dsubu $a2, $a2, $v0
    sd $v0, 24($sp)
    jal rt_strscpy_rcp
    nop
    ld $8, 24($sp)
    daddu $v0, $v0, $8
    b 9f
    nop
2:
    ld $a0, 16($sp)
    sd $v0, 24($sp)
    jal rt_strlen_rcp
    nop
    ld $8, 24($sp)
    daddu $v0, $v0, $8
9:
    ld $ra, 0($sp)
    daddiu $sp, $sp, 32
    jr $ra
    nop

.globl rt_strpad_rcp
.type rt_strpad_rcp, @function
.p2align 4
rt_strpad_rcp:
    rcp_zext_len $a2
    andi $a3, $a3, 255
    move $v1, $zero
1:
    sltu $8, $v1, $a2
    beqz $8, 9f
    nop
    daddu $9, $a1, $v1
    lbu $10, 0($9)
    beqz $10, 2f
    nop
    daddu $9, $a0, $v1
    sb $10, 0($9)
    daddiu $v1, $v1, 1
    b 1b
    nop
2:
    sltu $8, $v1, $a2
    beqz $8, 9f
    nop
    daddu $9, $a0, $v1
    sb $a3, 0($9)
    daddiu $v1, $v1, 1
    b 2b
    nop
9:
    jr $ra
    nop
