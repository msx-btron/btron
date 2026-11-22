# RV32 integer strings. ilp32: a0-a3 arguments, a0 return.
# Words are 4 bytes. A word load is aligned, so it stays in one page.
# A word store contains no NUL. Different alignments stay a byte loop.
# No vector opcodes. Do not link this object with the RV64 file.

.section .text
.attribute arch, "rv32gc"

.macro load_ones
    li t0, 0x01010101
    slli t1, t0, 7
.endm

# Branch to lab when w holds a zero byte. w must not be t4 or t5.
.macro brz_zero w, lab
    sub t4, \w, t0
    not t5, \w
    and t4, t4, t5
    and t4, t4, t1
    bnez t4, \lab
.endm

# Splat the low byte of src into dst. src and dst are different registers.
.macro splat_byte src, dst
    slli \dst, \src, 8
    or \dst, \src, \dst
    slli t4, \dst, 16
    or \dst, \dst, t4
.endm

# tmp nonzero when w contains a zero byte or the splat in a2.
# Clobbers scratch and tmp2. Leaves w and a2 alone.
.macro mark w, tmp, scratch, tmp2
    sub \tmp, \w, t0
    not \scratch, \w
    and \tmp, \tmp, \scratch
    and \tmp, \tmp, t1
    xor \scratch, \w, a2
    sub \tmp2, \scratch, t0
    not \scratch, \scratch
    and \tmp2, \tmp2, \scratch
    and \tmp2, \tmp2, t1
    or \tmp, \tmp, \tmp2
.endm

# Leaves a1 and a2 unchanged. strcat and strncat pass src and n through.
# A 16-byte load runs only while those bytes stay in the current page.
.globl rt_strlen_rv32
.type rt_strlen_rv32, @function
.p2align 2
rt_strlen_rv32:
    mv t6, a0
    load_ones
.Lsl_peel:
    andi t2, a0, 3
    beqz t2, .Lsl_page
    lbu t3, 0(a0)
    beqz t3, .Lsl_done
    addi a0, a0, 1
    j .Lsl_peel
.Lsl_page:
    lw a4, 0(a0)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsl_byte
    addi a0, a0, 4
    lui t2, 1
    srli t3, a0, 12
    slli t3, t3, 12
    add t5, t3, t2
    sub t2, t5, a0
    li t3, 16
    bltu t2, t3, .Lsl_short
    andi t2, t2, -16
    add a3, a0, t2
    j .Lsl_fast
.p2align 7
.Lsl_fast:
    lw a4, 0(a0)
    lw a5, 4(a0)
    lw a6, 8(a0)
    lw a7, 12(a0)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    sub t3, a5, t0
    not t4, a5
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a6, t0
    not t4, a6
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a7, t0
    not t4, a7
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    bnez t2, .Lsl_which
    addi a0, a0, 16
    bne a0, a3, .Lsl_fast
.Lsl_short:
    beq a0, t5, .Lsl_page
    lw a4, 0(a0)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsl_byte
    addi a0, a0, 4
    j .Lsl_short
.Lsl_which:
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsl_byte
    addi a0, a0, 4
    sub t2, a5, t0
    not t3, a5
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsl_byte
    addi a0, a0, 4
    sub t2, a6, t0
    not t3, a6
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsl_byte
    addi a0, a0, 4
.Lsl_byte:
    lbu t3, 0(a0)
    beqz t3, .Lsl_done
    addi a0, a0, 1
    j .Lsl_byte
.Lsl_done:
    sub a0, a0, t6
    ret

.globl rt_strnlen_rv32
.type rt_strnlen_rv32, @function
.p2align 2
rt_strnlen_rv32:
    mv t6, a0
    beqz a1, .Lsn_zero
    load_ones
.Lsn_peel:
    andi t2, a0, 3
    beqz t2, .Lsn_word
    beqz a1, .Lsn_done
    lbu t3, 0(a0)
    beqz t3, .Lsn_done
    addi a0, a0, 1
    addi a1, a1, -1
    j .Lsn_peel
.p2align 6
.Lsn_word:
    li t2, 4
    bltu a1, t2, .Lsn_byte
    lw t3, 0(a0)
    brz_zero t3, .Lsn_byte
    addi a0, a0, 4
    addi a1, a1, -4
    j .Lsn_word
.Lsn_byte:
    beqz a1, .Lsn_done
    lbu t3, 0(a0)
    beqz t3, .Lsn_done
    addi a0, a0, 1
    addi a1, a1, -1
    j .Lsn_byte
.Lsn_done:
    sub a0, a0, t6
    ret
.Lsn_zero:
    li a0, 0
    ret

.globl rt_strcpy_rv32
.type rt_strcpy_rv32, @function
.p2align 2
rt_strcpy_rv32:
    mv t6, a0
    load_ones
    xor t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lsc_byte
.Lsc_peel:
    andi t2, a0, 3
    beqz t2, .Lsc_word
    lbu t3, 0(a1)
    sb t3, 0(a0)
    beqz t3, .Lsc_done
    addi a0, a0, 1
    addi a1, a1, 1
    j .Lsc_peel
.Lsc_word:
    lui t2, 1
    srli t3, a1, 12
    slli t3, t3, 12
    add t5, t3, t2
    sub t2, t5, a1
    li t3, 16
    bltu t2, t3, .Lsc_one
    andi t2, t2, -16
    add a3, a1, t2
    j .Lsc_fast
.p2align 7
.Lsc_fast:
    lw a4, 0(a1)
    lw a5, 4(a1)
    lw a6, 8(a1)
    lw a7, 12(a1)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    sub t3, a5, t0
    not t4, a5
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a6, t0
    not t4, a6
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a7, t0
    not t4, a7
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    bnez t2, .Lsc_slow
    sw a4, 0(a0)
    sw a5, 4(a0)
    sw a6, 8(a0)
    sw a7, 12(a0)
    addi a0, a0, 16
    addi a1, a1, 16
    bne a1, a3, .Lsc_fast
    j .Lsc_one
.Lsc_one:
    beq a1, t5, .Lsc_word
    lw a4, 0(a1)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsc_byte
    sw a4, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    j .Lsc_one
.Lsc_slow:
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsc_byte
    sw a4, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    sub t2, a5, t0
    not t3, a5
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsc_byte
    sw a5, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    sub t2, a6, t0
    not t3, a6
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsc_byte
    sw a6, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
.Lsc_byte:
    lbu t3, 0(a1)
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    bnez t3, .Lsc_byte
.Lsc_done:
    mv a0, t6
    ret

# 16-byte chunks only while src+16 stays in this page and inside n.
# A chunk is stored only when none of its words contains a NUL.
.globl rt_strncpy_rv32
.type rt_strncpy_rv32, @function
.p2align 2
rt_strncpy_rv32:
    mv t6, a0
    beqz a2, .Lsy_done
    load_ones
    xor t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lsy_byte
.Lsy_peel:
    andi t2, a0, 3
    beqz t2, .Lsy_page
    beqz a2, .Lsy_done
    lbu t3, 0(a1)
    beqz t3, .Lsy_pad
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lsy_peel
.Lsy_page:
    beqz a2, .Lsy_done
    lui t2, 1
    srli t3, a1, 12
    slli t3, t3, 12
    add a3, t3, t2
    sub t2, a3, a1
    li t3, 16
    bltu t2, t3, .Lsy_one
    bltu a2, t3, .Lsy_one
    mv t4, a2
    bltu t2, a2, .Lsy_room
.Lsy_span:
    andi t4, t4, -16
    add t5, a1, t4
    j .Lsy_fast
.p2align 7
.Lsy_fast:
    lw a4, 0(a1)
    lw a5, 4(a1)
    lw a6, 8(a1)
    lw a7, 12(a1)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    sub t3, a5, t0
    not t4, a5
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a6, t0
    not t4, a6
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a7, t0
    not t4, a7
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    bnez t2, .Lsy_one
    sw a4, 0(a0)
    sw a5, 4(a0)
    sw a6, 8(a0)
    sw a7, 12(a0)
    addi a0, a0, 16
    addi a1, a1, 16
    addi a2, a2, -16
    bne a1, t5, .Lsy_fast
    beq a1, a3, .Lsy_page
.Lsy_one:
    li t3, 4
    bltu a2, t3, .Lsy_byte
    beq a1, a3, .Lsy_page
    lw a4, 0(a1)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsy_byte
    sw a4, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    j .Lsy_one
.Lsy_byte:
    beqz a2, .Lsy_done
    lbu t3, 0(a1)
    beqz t3, .Lsy_pad
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lsy_byte
.Lsy_pad:
    andi t2, a0, 3
    bnez t2, .Lsy_padb
    li t3, 16
    bltu a2, t3, .Lsy_pad4
    j .Lsy_pad16
.p2align 6
.Lsy_pad16:
    sw zero, 0(a0)
    sw zero, 4(a0)
    sw zero, 8(a0)
    sw zero, 12(a0)
    addi a0, a0, 16
    addi a2, a2, -16
    bgeu a2, t3, .Lsy_pad16
.Lsy_pad4:
    li t2, 4
.Lsy_padw:
    bltu a2, t2, .Lsy_padb
    sw zero, 0(a0)
    addi a0, a0, 4
    addi a2, a2, -4
    j .Lsy_padw
.Lsy_padb:
    beqz a2, .Lsy_done
    sb zero, 0(a0)
    addi a0, a0, 1
    addi a2, a2, -1
    j .Lsy_padb
.Lsy_done:
    mv a0, t6
    ret
.Lsy_room:
    mv t4, t2
    j .Lsy_span

.globl rt_strcat_rv32
.type rt_strcat_rv32, @function
.p2align 2
rt_strcat_rv32:
    addi sp, sp, -16
    sw ra, 8(sp)
    sw s0, 0(sp)
    mv s0, a0
    jal rt_strlen_rv32
    add a0, s0, a0
    jal rt_strcpy_rv32
    mv a0, s0
    lw ra, 8(sp)
    lw s0, 0(sp)
    addi sp, sp, 16
    ret

.globl rt_strncat_rv32
.type rt_strncat_rv32, @function
.p2align 2
rt_strncat_rv32:
    addi sp, sp, -16
    sw ra, 8(sp)
    sw s0, 0(sp)
    mv s0, a0
    jal rt_strlen_rv32
    add a0, s0, a0
    beqz a2, .Lsnc_nul
    load_ones
    xor t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lsnc_byte
.Lsnc_peel:
    andi t2, a0, 3
    beqz t2, .Lsnc_page
    beqz a2, .Lsnc_nul
    lbu t3, 0(a1)
    beqz t3, .Lsnc_nul
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lsnc_peel
.Lsnc_page:
    beqz a2, .Lsnc_nul
    lui t2, 1
    srli t3, a1, 12
    slli t3, t3, 12
    add a3, t3, t2
    sub t2, a3, a1
    li t3, 16
    bltu t2, t3, .Lsnc_one
    bltu a2, t3, .Lsnc_one
    mv t4, a2
    bltu t2, a2, .Lsnc_room
.Lsnc_span:
    andi t4, t4, -16
    add t5, a1, t4
    j .Lsnc_fast
.p2align 7
.Lsnc_fast:
    lw a4, 0(a1)
    lw a5, 4(a1)
    lw a6, 8(a1)
    lw a7, 12(a1)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    sub t3, a5, t0
    not t4, a5
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a6, t0
    not t4, a6
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    sub t3, a7, t0
    not t4, a7
    and t3, t3, t4
    and t3, t3, t1
    or t2, t2, t3
    bnez t2, .Lsnc_one
    sw a4, 0(a0)
    sw a5, 4(a0)
    sw a6, 8(a0)
    sw a7, 12(a0)
    addi a0, a0, 16
    addi a1, a1, 16
    addi a2, a2, -16
    bne a1, t5, .Lsnc_fast
    beq a1, a3, .Lsnc_page
.Lsnc_one:
    li t3, 4
    bltu a2, t3, .Lsnc_byte
    beq a1, a3, .Lsnc_page
    lw a4, 0(a1)
    sub t2, a4, t0
    not t3, a4
    and t2, t2, t3
    and t2, t2, t1
    bnez t2, .Lsnc_byte
    sw a4, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    j .Lsnc_one
.Lsnc_byte:
    beqz a2, .Lsnc_nul
    lbu t3, 0(a1)
    beqz t3, .Lsnc_nul
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    bnez a2, .Lsnc_byte
.Lsnc_nul:
    sb zero, 0(a0)
    mv a0, s0
    lw ra, 8(sp)
    lw s0, 0(sp)
    addi sp, sp, 16
    ret
.Lsnc_room:
    mv t4, t2
    j .Lsnc_span

.globl rt_strcmp_rv32
.type rt_strcmp_rv32, @function
.p2align 2
rt_strcmp_rv32:
    load_ones
    xor t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lsm_byte
.Lsm_peel:
    andi t2, a0, 3
    beqz t2, .Lsm_word
    lbu t3, 0(a0)
    lbu t6, 0(a1)
    bne t3, t6, .Lsm_diff
    beqz t3, .Lsm_eq
    addi a0, a0, 1
    addi a1, a1, 1
    j .Lsm_peel
.p2align 6
.Lsm_word:
    lw t3, 0(a0)
    lw t6, 0(a1)
    bne t3, t6, .Lsm_byte
    brz_zero t3, .Lsm_byte
    addi a0, a0, 4
    addi a1, a1, 4
    j .Lsm_word
.Lsm_byte:
    lbu t3, 0(a0)
    lbu t6, 0(a1)
    bne t3, t6, .Lsm_diff
    beqz t3, .Lsm_eq
    addi a0, a0, 1
    addi a1, a1, 1
    j .Lsm_byte
.Lsm_diff:
    sub a0, t3, t6
    ret
.Lsm_eq:
    li a0, 0
    ret

.globl rt_strncmp_rv32
.type rt_strncmp_rv32, @function
.p2align 2
rt_strncmp_rv32:
    beqz a2, .Lsnm_eq
    load_ones
    xor t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lsnm_byte
.Lsnm_peel:
    andi t2, a0, 3
    beqz t2, .Lsnm_word
    beqz a2, .Lsnm_eq
    lbu t3, 0(a0)
    lbu t6, 0(a1)
    bne t3, t6, .Lsnm_diff
    beqz t3, .Lsnm_eq
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lsnm_peel
.p2align 7
.Lsnm_word:
    li t2, 4
    bltu a2, t2, .Lsnm_byte
    lw t3, 0(a0)
    lw t6, 0(a1)
    bne t3, t6, .Lsnm_byte
    brz_zero t3, .Lsnm_byte
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    j .Lsnm_word
.Lsnm_byte:
    beqz a2, .Lsnm_eq
    lbu t3, 0(a0)
    lbu t6, 0(a1)
    bne t3, t6, .Lsnm_diff
    beqz t3, .Lsnm_eq
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lsnm_byte
.Lsnm_diff:
    sub a0, t3, t6
    ret
.Lsnm_eq:
    li a0, 0
    ret

.globl rt_strchr_rv32
.type rt_strchr_rv32, @function
.p2align 2
rt_strchr_rv32:
    andi a1, a1, 255
    load_ones
    splat_byte a1, a2
.Lsh_peel:
    andi t6, a0, 3
    beqz t6, .Lsh_page
    lbu t3, 0(a0)
    beq t3, a1, .Lsh_yes
    beqz t3, .Lsh_no
    addi a0, a0, 1
    j .Lsh_peel
.Lsh_page:
    lui t2, 1
    srli t3, a0, 12
    slli t3, t3, 12
    add t5, t3, t2
    sub t2, t5, a0
    li t3, 16
    bltu t2, t3, .Lsh_short
    andi t2, t2, -16
    add a3, a0, t2
    j .Lsh_fast
.p2align 8
.Lsh_fast:
    lw a4, 0(a0)
    lw a5, 4(a0)
    lw a6, 8(a0)
    lw a7, 12(a0)
    mark a4, t2, t3, t4
    mark a5, t3, t4, t6
    or t2, t2, t3
    mark a6, t3, t4, t6
    or t2, t2, t3
    mark a7, t3, t4, t6
    or t2, t2, t3
    bnez t2, .Lsh_which
    addi a0, a0, 16
    bne a0, a3, .Lsh_fast
.Lsh_short:
    beq a0, t5, .Lsh_page
    lw a4, 0(a0)
    mark a4, t2, t3, t4
    bnez t2, .Lsh_byte
    addi a0, a0, 4
    j .Lsh_short
.Lsh_which:
    mark a4, t2, t3, t4
    bnez t2, .Lsh_byte
    addi a0, a0, 4
    mark a5, t2, t3, t4
    bnez t2, .Lsh_byte
    addi a0, a0, 4
    mark a6, t2, t3, t4
    bnez t2, .Lsh_byte
    addi a0, a0, 4
.Lsh_byte:
    lbu t3, 0(a0)
    beq t3, a1, .Lsh_yes
    beqz t3, .Lsh_no
    addi a0, a0, 1
    j .Lsh_byte
.Lsh_yes:
    ret
.Lsh_no:
    li a0, 0
    ret

.globl rt_strrchr_rv32
.type rt_strrchr_rv32, @function
.p2align 2
rt_strrchr_rv32:
    addi sp, sp, -16
    sw ra, 12(sp)
    andi a1, a1, 255
    mv a2, a0
    jal rt_strlen_rv32
    add a0, a2, a0
    beqz a1, .Lsr_out
    beq a0, a2, .Lsr_none
    load_ones
    splat_byte a1, a3
    li t6, 4
.Lsr_align:
    andi t2, a0, 3
    beqz t2, .Lsr_wchk
    addi a0, a0, -1
    lbu t3, 0(a0)
    beq t3, a1, .Lsr_out
    bne a0, a2, .Lsr_align
    j .Lsr_none
.p2align 6
.Lsr_wchk:
    sub t2, a0, a2
    bltu t2, t6, .Lsr_head
    addi a0, a0, -4
    lw t3, 0(a0)
    xor t3, t3, a3
    sub t4, t3, t0
    not t5, t3
    and t4, t4, t5
    and t4, t4, t1
    bnez t4, .Lsr_scan
    j .Lsr_wchk
.Lsr_scan:
    addi a4, a0, 4
.Lsr_sb:
    addi a4, a4, -1
    lbu t3, 0(a4)
    beq t3, a1, .Lsr_hit
    bne a4, a0, .Lsr_sb
    j .Lsr_wchk
.Lsr_hit:
    mv a0, a4
    j .Lsr_out
.Lsr_head:
    beq a0, a2, .Lsr_none
    addi a0, a0, -1
    lbu t3, 0(a0)
    beq t3, a1, .Lsr_out
    j .Lsr_head
.Lsr_none:
    li a0, 0
.Lsr_out:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

.globl rt_strstr_rv32
.type rt_strstr_rv32, @function
.p2align 2
rt_strstr_rv32:
    lbu t0, 0(a1)
    beqz t0, .Lss_ret
    lbu t1, 1(a1)
    bnez t1, .Lss_long
    mv a1, t0
    j rt_strchr_rv32
.Lss_long:
    addi sp, sp, -48
    sw ra, 40(sp)
    sw s0, 32(sp)
    sw s1, 24(sp)
    sw s2, 16(sp)
    sw s3, 8(sp)
    mv s0, a0
    mv s1, a1
    mv s3, t0
    mv a0, a1
    jal rt_strlen_rv32
    mv s2, a0
.Lss_loop:
    lbu t0, 0(s0)
    beqz t0, .Lss_no
    bne t0, s3, .Lss_next
    mv a0, s0
    mv a1, s1
    mv a2, s2
    jal rt_strncmp_rv32
    beqz a0, .Lss_yes
.Lss_next:
    addi s0, s0, 1
    j .Lss_loop
.Lss_yes:
    mv a0, s0
    j .Lss_out
.Lss_no:
    li a0, 0
.Lss_out:
    lw ra, 40(sp)
    lw s0, 32(sp)
    lw s1, 24(sp)
    lw s2, 16(sp)
    lw s3, 8(sp)
    addi sp, sp, 48
    ret
.Lss_ret:
    ret

.globl rt_strpbrk_rv32
.type rt_strpbrk_rv32, @function
.p2align 2
rt_strpbrk_rv32:
    lbu t0, 0(a1)
    beqz t0, .Lsp_no
    j .Lsp_s
.p2align 7
.Lsp_s:
    lbu t0, 0(a0)
    beqz t0, .Lsp_no
    mv t1, a1
.Lsp_a:
    lbu t2, 0(t1)
    beqz t2, .Lsp_next
    beq t0, t2, .Lsp_yes
    addi t1, t1, 1
    j .Lsp_a
.Lsp_next:
    addi a0, a0, 1
    j .Lsp_s
.Lsp_yes:
    ret
.Lsp_no:
    li a0, 0
    ret

.globl rt_strscpy_rv32
.type rt_strscpy_rv32, @function
.p2align 2
rt_strscpy_rv32:
    beqz a2, .Lscy_all
    addi sp, sp, -16
    sw ra, 8(sp)
    sw s0, 0(sp)
    li s0, 0
    load_ones
    xor t3, a0, a1
    andi t3, t3, 3
    bnez t3, .Lscy_byte
.Lscy_peel:
    andi t3, a0, 3
    beqz t3, .Lscy_word
    li t3, 1
    bgeu t3, a2, .Lscy_nul
    lbu t3, 0(a1)
    beqz t3, .Lscy_nul
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    addi s0, s0, 1
    j .Lscy_peel
.Lscy_word:
    # A stored word has no NUL, and one byte stays for the terminator.
    li t6, 5
    bltu a2, t6, .Lscy_byte
    lw t3, 0(a1)
    brz_zero t3, .Lscy_byte
    sw t3, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    addi s0, s0, 4
    j .Lscy_word
.Lscy_byte:
    li t3, 1
    bgeu t3, a2, .Lscy_nul
    lbu t3, 0(a1)
    beqz t3, .Lscy_nul
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    addi s0, s0, 1
    j .Lscy_byte
.Lscy_nul:
    sb zero, 0(a0)
    mv a0, a1
    jal rt_strlen_rv32
    add a0, a0, s0
    lw ra, 8(sp)
    lw s0, 0(sp)
    addi sp, sp, 16
    ret
.Lscy_all:
    mv a0, a1
    j rt_strlen_rv32

.globl rt_strscat_rv32
.type rt_strscat_rv32, @function
.p2align 2
rt_strscat_rv32:
    addi sp, sp, -48
    sw ra, 40(sp)
    sw s0, 32(sp)
    sw s1, 24(sp)
    sw s2, 16(sp)
    sw s3, 8(sp)
    mv s0, a0
    mv s1, a1
    mv s2, a2
    mv a1, a2
    jal rt_strnlen_rv32
    mv s3, a0
    bltu a0, s2, .Lsca_room
    mv a0, s1
    jal rt_strlen_rv32
    add a0, a0, s3
    j .Lsca_out
.Lsca_room:
    add a0, s0, s3
    mv a1, s1
    sub a2, s2, s3
    jal rt_strscpy_rv32
    add a0, a0, s3
.Lsca_out:
    lw ra, 40(sp)
    lw s0, 32(sp)
    lw s1, 24(sp)
    lw s2, 16(sp)
    lw s3, 8(sp)
    addi sp, sp, 48
    ret

.globl rt_strpad_rv32
.type rt_strpad_rv32, @function
.p2align 2
rt_strpad_rv32:
    andi a3, a3, 255
    beqz a2, .Lspd_done
    load_ones
    xor t2, a0, a1
    andi t2, t2, 3
    bnez t2, .Lspd_byte
.Lspd_peel:
    andi t2, a0, 3
    beqz t2, .Lspd_word
    beqz a2, .Lspd_done
    lbu t3, 0(a1)
    beqz t3, .Lspd_fill
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lspd_peel
.Lspd_word:
    li t2, 4
    bltu a2, t2, .Lspd_byte
    lw t3, 0(a1)
    brz_zero t3, .Lspd_byte
    sw t3, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    j .Lspd_word
.Lspd_byte:
    beqz a2, .Lspd_done
    lbu t3, 0(a1)
    beqz t3, .Lspd_fill
    sb t3, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lspd_byte
.Lspd_fill:
    splat_byte a3, t2
.Lspd_falign:
    andi t3, a0, 3
    beqz t3, .Lspd_fword
    beqz a2, .Lspd_done
    sb a3, 0(a0)
    addi a0, a0, 1
    addi a2, a2, -1
    j .Lspd_falign
.Lspd_fword:
    li t3, 4
    bltu a2, t3, .Lspd_fbyte
    sw t2, 0(a0)
    addi a0, a0, 4
    addi a2, a2, -4
    j .Lspd_fword
.Lspd_fbyte:
    beqz a2, .Lspd_done
    sb a3, 0(a0)
    addi a0, a0, 1
    addi a2, a2, -1
    j .Lspd_fbyte
.Lspd_done:
    ret
