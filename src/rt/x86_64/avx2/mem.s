# x86-64 AVX2. 32-byte chunks, dest aligned, vzeroupper on every return
# so the caller can use legacy SSE without a transition penalty.

.intel_syntax noprefix
.section .text

.globl rt_memcpy_avx2
.type rt_memcpy_avx2, @function
.p2align 4
rt_memcpy_avx2:
    mov rax, rdi
    cmp rdx, 32
    jb .Lmc_tail
    mov rcx, rdi
    and rcx, 31
    jz .Lmc_body
    neg rcx
    add rcx, 32
    sub rdx, rcx
.Lmc_head:
    mov r8b, byte ptr [rsi]
    mov byte ptr [rdi], r8b
    inc rsi
    inc rdi
    dec rcx
    jnz .Lmc_head
.Lmc_body:
    cmp rdx, 128
    jb .Lmc_32
.p2align 4
.Lmc_128:
    vmovdqu ymm0, ymmword ptr [rsi]
    vmovdqu ymm1, ymmword ptr [rsi + 32]
    vmovdqu ymm2, ymmword ptr [rsi + 64]
    vmovdqu ymm3, ymmword ptr [rsi + 96]
    vmovdqa ymmword ptr [rdi], ymm0
    vmovdqa ymmword ptr [rdi + 32], ymm1
    vmovdqa ymmword ptr [rdi + 64], ymm2
    vmovdqa ymmword ptr [rdi + 96], ymm3
    add rsi, 128
    add rdi, 128
    sub rdx, 128
    cmp rdx, 128
    jae .Lmc_128
.Lmc_32:
    cmp rdx, 32
    jb .Lmc_tail
.p2align 4
.Lmc_32b:
    vmovdqu ymm0, ymmword ptr [rsi]
    vmovdqa ymmword ptr [rdi], ymm0
    add rsi, 32
    add rdi, 32
    sub rdx, 32
    cmp rdx, 32
    jae .Lmc_32b
.Lmc_tail:
.Lmc_t8:
    cmp rdx, 8
    jb .Lmc_t4
    mov rcx, qword ptr [rsi]
    mov qword ptr [rdi], rcx
    add rsi, 8
    add rdi, 8
    sub rdx, 8
    jmp .Lmc_t8
.Lmc_t4:
    cmp rdx, 4
    jb .Lmc_t2
    mov ecx, dword ptr [rsi]
    mov dword ptr [rdi], ecx
    add rsi, 4
    add rdi, 4
    sub rdx, 4
.Lmc_t2:
    cmp rdx, 2
    jb .Lmc_t1
    mov cx, word ptr [rsi]
    mov word ptr [rdi], cx
    add rsi, 2
    add rdi, 2
    sub rdx, 2
.Lmc_t1:
    test rdx, rdx
    jz .Lmc_ret
    mov cl, byte ptr [rsi]
    mov byte ptr [rdi], cl
.Lmc_ret:
    vzeroupper
    ret

.globl rt_memset_avx2
.type rt_memset_avx2, @function
.p2align 4
rt_memset_avx2:
    mov rax, rdi
    movzx esi, sil
    cmp rdx, 32
    jb .Lms_tail
    movd xmm0, esi
    vpbroadcastb ymm0, xmm0
    mov rcx, rdi
    and rcx, 31
    jz .Lms_body
    neg rcx
    add rcx, 32
    sub rdx, rcx
.Lms_head:
    mov byte ptr [rdi], sil
    inc rdi
    dec rcx
    jnz .Lms_head
.Lms_body:
    cmp rdx, 128
    jb .Lms_32
.p2align 4
.Lms_128:
    vmovdqa ymmword ptr [rdi], ymm0
    vmovdqa ymmword ptr [rdi + 32], ymm0
    vmovdqa ymmword ptr [rdi + 64], ymm0
    vmovdqa ymmword ptr [rdi + 96], ymm0
    add rdi, 128
    sub rdx, 128
    cmp rdx, 128
    jae .Lms_128
.Lms_32:
    cmp rdx, 32
    jb .Lms_tail
.p2align 4
.Lms_32b:
    vmovdqa ymmword ptr [rdi], ymm0
    add rdi, 32
    sub rdx, 32
    cmp rdx, 32
    jae .Lms_32b
.Lms_tail:
    mov r8, rsi
    shl r8, 8
    or r8, rsi
    mov r9, r8
    shl r9, 16
    or r8, r9
    mov r9, r8
    shl r9, 32
    or r8, r9
.Lms_t8:
    cmp rdx, 8
    jb .Lms_t4
    mov qword ptr [rdi], r8
    add rdi, 8
    sub rdx, 8
    jmp .Lms_t8
.Lms_t4:
    cmp rdx, 4
    jb .Lms_t2
    mov dword ptr [rdi], r8d
    add rdi, 4
    sub rdx, 4
.Lms_t2:
    cmp rdx, 2
    jb .Lms_t1
    mov word ptr [rdi], r8w
    add rdi, 2
    sub rdx, 2
.Lms_t1:
    test rdx, rdx
    jz .Lms_ret
    mov byte ptr [rdi], sil
.Lms_ret:
    vzeroupper
    ret

.globl rt_memcmp_avx2
.type rt_memcmp_avx2, @function
.p2align 4
rt_memcmp_avx2:
    cmp rdx, 32
    jb .Lmp_bytes
.p2align 4
.Lmp_32:
    vmovdqu ymm0, ymmword ptr [rdi]
    vmovdqu ymm1, ymmword ptr [rsi]
    vpcmpeqb ymm0, ymm0, ymm1
    vpmovmskb eax, ymm0
    xor eax, -1
    jnz .Lmp_diff
    add rdi, 32
    add rsi, 32
    sub rdx, 32
    cmp rdx, 32
    jae .Lmp_32
.Lmp_bytes:
    test rdx, rdx
    jz .Lmp_eq
.Lmp_b:
    movzx eax, byte ptr [rdi]
    movzx ecx, byte ptr [rsi]
    sub eax, ecx
    jne .Lmp_ret
    inc rdi
    inc rsi
    dec rdx
    jnz .Lmp_b
.Lmp_eq:
    xor eax, eax
    vzeroupper
    ret
.Lmp_diff:
    bsf ecx, eax
    movzx eax, byte ptr [rdi + rcx]
    movzx r8d, byte ptr [rsi + rcx]
    sub eax, r8d
.Lmp_ret:
    vzeroupper
    ret

.section .note.GNU-stack, "", @progbits
