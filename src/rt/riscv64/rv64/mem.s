# RV64 integer memory. lp64: a0-a2 are arguments, a0 is the return.
# Words are 8 bytes. A word move happens only when both pointers are
# aligned, so a strict core does not take a misaligned fault.
# Different alignments stay a byte loop. No vector opcodes.

.section .text
.attribute arch, "rv64gc"

.globl rt_memcpy_rv64
.type rt_memcpy_rv64, @function
.p2align 2
rt_memcpy_rv64:
    mv t6, a0
    beqz a2, .Lmc_done
    j .Lmc_body
.Lmc_done:
    mv a0, t6
    ret

.Lmc_body:
    xor t0, a0, a1
    andi t0, t0, 7
    bnez t0, .Lmc_bytes
    andi t0, a0, 7
    beqz t0, .Lmc_words
    li t1, 8
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
    li t3, 32
.Lmc_chunk:
    bltu a2, t3, .Lmc_one
    ld t0, 0(a1)
    ld t1, 8(a1)
    ld t2, 16(a1)
    ld t4, 24(a1)
    sd t0, 0(a0)
    sd t1, 8(a0)
    sd t2, 16(a0)
    sd t4, 24(a0)
    addi a0, a0, 32
    addi a1, a1, 32
    addi a2, a2, -32
    j .Lmc_chunk
.Lmc_one:
    li t3, 8
    bltu a2, t3, .Lmc_bytes
    ld t0, 0(a1)
    sd t0, 0(a0)
    addi a0, a0, 8
    addi a1, a1, 8
    addi a2, a2, -8
    j .Lmc_one
.Lmc_bytes:
    beqz a2, .Lmc_done
    lbu t0, 0(a1)
    sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    j .Lmc_bytes

.globl rt_memmove_rv64
.type rt_memmove_rv64, @function
.p2align 2
rt_memmove_rv64:
    mv t6, a0
    beqz a2, .Lmv_done
    beq a0, a1, .Lmv_done
    bltu a0, a1, .Lmc_body
    xor t0, a0, a1
    andi t0, t0, 7
    bnez t0, .Lmv_bytes
    andi t0, a0, 7
    bnez t0, .Lmv_bytes
    add t4, a0, a2
    add t5, a1, a2
    andi t1, a2, 7
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
    andi t0, a2, 63
    add a3, a0, t0
    beq t4, a3, .Lmv_w8
.Lmv_64:
    addi t4, t4, -64
    addi t5, t5, -64
    ld t0, 0(t5)
    ld t1, 8(t5)
    ld t2, 16(t5)
    ld t3, 24(t5)
    ld a4, 32(t5)
    ld a5, 40(t5)
    ld a6, 48(t5)
    ld a7, 56(t5)
    sd t0, 0(t4)
    sd t1, 8(t4)
    sd t2, 16(t4)
    sd t3, 24(t4)
    sd a4, 32(t4)
    sd a5, 40(t4)
    sd a6, 48(t4)
    sd a7, 56(t4)
    bne t4, a3, .Lmv_64
.Lmv_w8:
    beq t4, a0, .Lmv_done
    addi t4, t4, -8
    addi t5, t5, -8
    ld t0, 0(t5)
    sd t0, 0(t4)
    j .Lmv_w8
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

.globl rt_memset_rv64
.type rt_memset_rv64, @function
.p2align 2
rt_memset_rv64:
    mv t6, a0
    beqz a2, .Lms_done
    andi a1, a1, 255
    slli t0, a1, 8
    or a1, a1, t0
    slli t0, a1, 16
    or a1, a1, t0
    slli t0, a1, 32
    or a1, a1, t0
    andi t1, a0, 7
    beqz t1, .Lms_words
    li t3, 8
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
    sd a1, 0(a0)
    sd a1, 8(a0)
    sd a1, 16(a0)
    sd a1, 24(a0)
    sd a1, 32(a0)
    sd a1, 40(a0)
    sd a1, 48(a0)
    sd a1, 56(a0)
    sd a1, 64(a0)
    sd a1, 72(a0)
    sd a1, 80(a0)
    sd a1, 88(a0)
    sd a1, 96(a0)
    sd a1, 104(a0)
    sd a1, 112(a0)
    sd a1, 120(a0)
    addi a0, a0, 128
    bne a0, t5, .Lms_chunk
.Lms_rest:
    sub a2, t4, a0
.Lms_one:
    li t3, 8
    bltu a2, t3, .Lms_bytes
    sd a1, 0(a0)
    addi a0, a0, 8
    addi a2, a2, -8
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

.globl rt_memcmp_rv64
.type rt_memcmp_rv64, @function
.p2align 2
rt_memcmp_rv64:
    beqz a2, .Lmp_eq
    xor t0, a0, a1
    andi t0, t0, 7
    bnez t0, .Lmp_bytes
    andi t0, a0, 7
    beqz t0, .Lmp_words
    li t1, 8
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
    li t3, 8
.Lmp_word:
    bltu a2, t3, .Lmp_bytes
    ld t0, 0(a0)
    ld t2, 0(a1)
    bne t0, t2, .Lmp_bytes
    addi a0, a0, 8
    addi a1, a1, 8
    addi a2, a2, -8
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
