# x86-64 AVX-512F + AVX-512BW. 64-byte ZMM, dest aligned to 64.
# vzeroupper does not drop ZMM state; a short kernel that writes ZMM
# leaves it dirty on purpose (same tradeoff as glibc's large-copy path).
# Callers that mix tight SSE should stay on the AVX2 tier for small n.

.intel_syntax noprefix
.section .text

.globl rt_memcpy_avx512
.type rt_memcpy_avx512, @function
.p2align 4
rt_memcpy_avx512:
    mov rax, rdi
    cmp rdx, 64
    jb .Lmc_tail
    mov rcx, rdi
    and rcx, 63
    jz .Lmc_body
    neg rcx
    add rcx, 64
    sub rdx, rcx
.Lmc_head:
    mov r8b, byte ptr [rsi]
    mov byte ptr [rdi], r8b
    inc rsi
    inc rdi
    dec rcx
    jnz .Lmc_head
.Lmc_body:
    cmp rdx, 256
    jb .Lmc_64
.p2align 4
.Lmc_256:
    vmovdqu64 zmm0, zmmword ptr [rsi]
    vmovdqu64 zmm1, zmmword ptr [rsi + 64]
    vmovdqu64 zmm2, zmmword ptr [rsi + 128]
    vmovdqu64 zmm3, zmmword ptr [rsi + 192]
    vmovdqa64 zmmword ptr [rdi], zmm0
    vmovdqa64 zmmword ptr [rdi + 64], zmm1
    vmovdqa64 zmmword ptr [rdi + 128], zmm2
    vmovdqa64 zmmword ptr [rdi + 192], zmm3
    add rsi, 256
    add rdi, 256
    sub rdx, 256
    cmp rdx, 256
    jae .Lmc_256
.Lmc_64:
    cmp rdx, 64
    jb .Lmc_tail
.p2align 4
.Lmc_64b:
    vmovdqu64 zmm0, zmmword ptr [rsi]
    vmovdqa64 zmmword ptr [rdi], zmm0
    add rsi, 64
    add rdi, 64
    sub rdx, 64
    cmp rdx, 64
    jae .Lmc_64b
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

.globl rt_memset_avx512
.type rt_memset_avx512, @function
.p2align 4
rt_memset_avx512:
    mov rax, rdi
    movzx esi, sil
    cmp rdx, 64
    jb .Lms_tail
    movd xmm0, esi
    vpbroadcastb zmm0, xmm0
    mov rcx, rdi
    and rcx, 63
    jz .Lms_body
    neg rcx
    add rcx, 64
    sub rdx, rcx
.Lms_head:
    mov byte ptr [rdi], sil
    inc rdi
    dec rcx
    jnz .Lms_head
.Lms_body:
    cmp rdx, 256
    jb .Lms_64
.p2align 4
.Lms_256:
    vmovdqa64 zmmword ptr [rdi], zmm0
    vmovdqa64 zmmword ptr [rdi + 64], zmm0
    vmovdqa64 zmmword ptr [rdi + 128], zmm0
    vmovdqa64 zmmword ptr [rdi + 192], zmm0
    add rdi, 256
    sub rdx, 256
    cmp rdx, 256
    jae .Lms_256
.Lms_64:
    cmp rdx, 64
    jb .Lms_tail
.p2align 4
.Lms_64b:
    vmovdqa64 zmmword ptr [rdi], zmm0
    add rdi, 64
    sub rdx, 64
    cmp rdx, 64
    jae .Lms_64b
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

.globl rt_memcmp_avx512
.type rt_memcmp_avx512, @function
.p2align 4
rt_memcmp_avx512:
    cmp rdx, 64
    jb .Lmp_bytes
.p2align 4
.Lmp_64:
    vmovdqu8 zmm0, zmmword ptr [rdi]
    vmovdqu8 zmm1, zmmword ptr [rsi]
    vpcmpneqb k1, zmm0, zmm1
    kmovq rax, k1
    test rax, rax
    jnz .Lmp_diff
    add rdi, 64
    add rsi, 64
    sub rdx, 64
    cmp rdx, 64
    jae .Lmp_64
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
    bsf rcx, rax
    movzx eax, byte ptr [rdi + rcx]
    movzx r8d, byte ptr [rsi + rcx]
    sub eax, r8d
.Lmp_ret:
    vzeroupper
    ret

.section .note.GNU-stack, "", @progbits
