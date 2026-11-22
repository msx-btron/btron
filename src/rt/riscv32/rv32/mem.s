# RV32 integer memory. ilp32: a0-a2 are arguments, a0 is the return.
# Words are 4 bytes. A word move happens only when both pointers are
# aligned, so a strict core does not take a misaligned fault.
# Different alignments stay a byte loop. No vector opcodes.
# Do not link this object with the RV64 file.

.section .text
.attribute arch, "rv32gc"

.globl rt_memcpy_rv32
.type rt_memcpy_rv32, @function
.p2align 2
rt_memcpy_rv32:
    mv t6, a0
    beqz a2, .Lmc_done
    j .Lmc_body
.Lmc_done:
    mv a0, t6
    ret

.Lmc_body:
    xor t0, a0, a1
    andi t0, t0, 3
    bnez t0, .Lmc_bytes
    andi t0, a0, 3
    beqz t0, .Lmc_words
    li t1, 4
    sub t1, t1, t0
    bltu a2, t1, .Lmc_bytes
.Lmc_peel:
    beqz t1, .Lmc_words
    lbu t0, 0(a1)
    sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    addi t1, t1, -1
    j .Lmc_peel
.Lmc_words:
    andi t3, a2, 63
    sub a2, a2, t3
    add t5, a0, a2
    beq a0, t5, .Lmc_rest
.Lmc_chunk:
    lw t0, 0(a1)
    lw t1, 4(a1)
    lw t2, 8(a1)
    lw t4, 12(a1)
    lw a3, 16(a1)
    lw a4, 20(a1)
    lw a5, 24(a1)
    lw a6, 28(a1)
    sw t0, 0(a0)
    sw t1, 4(a0)
    sw t2, 8(a0)
    sw t4, 12(a0)
    sw a3, 16(a0)
    sw a4, 20(a0)
    sw a5, 24(a0)
    sw a6, 28(a0)
    lw t0, 32(a1)
    lw t1, 36(a1)
    lw t2, 40(a1)
    lw t4, 44(a1)
    lw a3, 48(a1)
    lw a4, 52(a1)
    lw a5, 56(a1)
    lw a6, 60(a1)
    sw t0, 32(a0)
    sw t1, 36(a0)
    sw t2, 40(a0)
    sw t4, 44(a0)
    sw a3, 48(a0)
    sw a4, 52(a0)
    sw a5, 56(a0)
    sw a6, 60(a0)
    addi a0, a0, 64
    addi a1, a1, 64
    bne a0, t5, .Lmc_chunk
.Lmc_rest:
    mv a2, t3
.Lmc_one:
    li t3, 4
    bltu a2, t3, .Lmc_bytes
    lw t0, 0(a1)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    j .Lmc_one
.Lmc_bytes:
    beqz a2, .Lmc_done
    lbu t0, 0(a1)
    sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lmc_bytes

.globl rt_memmove_rv32
.type rt_memmove_rv32, @function
.p2align 2
rt_memmove_rv32:
    mv t6, a0
    beqz a2, .Lmv_done
    beq a0, a1, .Lmv_done
    bltu a0, a1, .Lmc_body
    xor t0, a0, a1
    andi t0, t0, 3
    bnez t0, .Lmv_bytes
    andi t0, a0, 3
    bnez t0, .Lmv_bytes
    add t4, a0, a2
    add t5, a1, a2
    andi t1, a2, 3
.Lmv_peel:
    beqz t1, .Lmv_words
    addi t4, t4, -1
    addi t5, t5, -1
    lbu t0, 0(t5)
    sb t0, 0(t4)
    addi t1, t1, -1
    addi a2, a2, -1
    j .Lmv_peel
.Lmv_words:
    andi t0, a2, 31
    add a3, a0, t0
    beq t4, a3, .Lmv_w4
.Lmv_32:
    addi t4, t4, -32
    addi t5, t5, -32
    lw t0, 0(t5)
    lw t1, 4(t5)
    lw t2, 8(t5)
    lw a4, 12(t5)
    lw a5, 16(t5)
    lw a6, 20(t5)
    lw a7, 24(t5)
    lw t3, 28(t5)
    sw t0, 0(t4)
    sw t1, 4(t4)
    sw t2, 8(t4)
    sw a4, 12(t4)
    sw a5, 16(t4)
    sw a6, 20(t4)
    sw a7, 24(t4)
    sw t3, 28(t4)
    bne t4, a3, .Lmv_32
.Lmv_w4:
    beq t4, a0, .Lmv_done
    addi t4, t4, -4
    addi t5, t5, -4
    lw t0, 0(t5)
    sw t0, 0(t4)
    j .Lmv_w4
.Lmv_bytes:
    add t4, a0, a2
    add t5, a1, a2
.Lmv_byte:
    beqz a2, .Lmv_done
    addi t4, t4, -1
    addi t5, t5, -1
    lbu t0, 0(t5)
    sb t0, 0(t4)
    addi a2, a2, -1
    j .Lmv_byte
.Lmv_done:
    mv a0, t6
    ret

.globl rt_memset_rv32
.type rt_memset_rv32, @function
.p2align 2
rt_memset_rv32:
    mv t6, a0
    beqz a2, .Lms_done
    andi a1, a1, 255
    slli t0, a1, 8
    or a1, a1, t0
    slli t0, a1, 16
    or a1, a1, t0
    andi t1, a0, 3
    beqz t1, .Lms_words
    li t3, 4
    sub t1, t3, t1
    bltu a2, t1, .Lms_bytes
.Lms_peel:
    beqz t1, .Lms_words
    sb a1, 0(a0)
    addi a0, a0, 1
    addi a2, a2, -1
    addi t1, t1, -1
    j .Lms_peel
.Lms_words:
    add t4, a0, a2
    andi t0, a2, 127
    sub t5, t4, t0
    beq a0, t5, .Lms_rest
.Lms_chunk:
    sw a1, 0(a0)
    sw a1, 4(a0)
    sw a1, 8(a0)
    sw a1, 12(a0)
    sw a1, 16(a0)
    sw a1, 20(a0)
    sw a1, 24(a0)
    sw a1, 28(a0)
    sw a1, 32(a0)
    sw a1, 36(a0)
    sw a1, 40(a0)
    sw a1, 44(a0)
    sw a1, 48(a0)
    sw a1, 52(a0)
    sw a1, 56(a0)
    sw a1, 60(a0)
    sw a1, 64(a0)
    sw a1, 68(a0)
    sw a1, 72(a0)
    sw a1, 76(a0)
    sw a1, 80(a0)
    sw a1, 84(a0)
    sw a1, 88(a0)
    sw a1, 92(a0)
    sw a1, 96(a0)
    sw a1, 100(a0)
    sw a1, 104(a0)
    sw a1, 108(a0)
    sw a1, 112(a0)
    sw a1, 116(a0)
    sw a1, 120(a0)
    sw a1, 124(a0)
    addi a0, a0, 128
    bne a0, t5, .Lms_chunk
.Lms_rest:
    sub a2, t4, a0
.Lms_one:
    li t3, 4
    bltu a2, t3, .Lms_bytes
    sw a1, 0(a0)
    addi a0, a0, 4
    addi a2, a2, -4
    j .Lms_one
.Lms_bytes:
    beqz a2, .Lms_done
    sb a1, 0(a0)
    addi a0, a0, 1
    addi a2, a2, -1
    j .Lms_bytes
.Lms_done:
    mv a0, t6
    ret

.globl rt_memcmp_rv32
.type rt_memcmp_rv32, @function
.p2align 2
rt_memcmp_rv32:
    beqz a2, .Lmp_eq
    xor t0, a0, a1
    andi t0, t0, 3
    bnez t0, .Lmp_bytes
    andi t0, a0, 3
    beqz t0, .Lmp_words
    li t1, 4
    sub t1, t1, t0
    bltu a2, t1, .Lmp_bytes
.Lmp_peel:
    beqz t1, .Lmp_words
    lbu t0, 0(a0)
    lbu t2, 0(a1)
    bne t0, t2, .Lmp_diff
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    addi t1, t1, -1
    j .Lmp_peel
.Lmp_words:
    li t3, 4
.Lmp_word:
    bltu a2, t3, .Lmp_bytes
    lw t0, 0(a0)
    lw t2, 0(a1)
    bne t0, t2, .Lmp_bytes
    addi a0, a0, 4
    addi a1, a1, 4
    addi a2, a2, -4
    j .Lmp_word
.Lmp_bytes:
    beqz a2, .Lmp_eq
    lbu t0, 0(a0)
    lbu t2, 0(a1)
    bne t0, t2, .Lmp_diff
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lmp_bytes
.Lmp_diff:
    sub a0, t0, t2
    ret
.Lmp_eq:
    li a0, 0
    ret
