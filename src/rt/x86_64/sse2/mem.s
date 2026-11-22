# x86-64 SSE2. System V: memcpy(dst,src,n) / memcmp(a,b,n) / memset(dst,c,n).
# Dest is aligned to 16, then movdqa stores. Loads stay movdqu so a
# mismatched src alignment is still correct. No NT stores: rt copies are
# usually reused, and movntdq needs a size threshold plus sfence.
# Never reads or writes past n.

.intel_syntax noprefix
.section .text

.globl rt_memcpy_sse2
.type rt_memcpy_sse2, @function
.p2align 4
rt_memcpy_sse2:
    mov rax, rdi
    cmp rdx, 16
    jb .Lmc_tail
    mov rcx, rdi
    and rcx, 15
    jz .Lmc_body
    neg rcx
    add rcx, 16
    sub rdx, rcx
.Lmc_head:
    mov r8b, byte ptr [rsi]
    mov byte ptr [rdi], r8b
    inc rsi
    inc rdi
    dec rcx
    jnz .Lmc_head
.Lmc_body:
    cmp rdx, 64
    jb .Lmc_16
.p2align 4
.Lmc_64:
    movdqu xmm0, xmmword ptr [rsi]
    movdqu xmm1, xmmword ptr [rsi + 16]
    movdqu xmm2, xmmword ptr [rsi + 32]
    movdqu xmm3, xmmword ptr [rsi + 48]
    movdqa xmmword ptr [rdi], xmm0
    movdqa xmmword ptr [rdi + 16], xmm1
    movdqa xmmword ptr [rdi + 32], xmm2
    movdqa xmmword ptr [rdi + 48], xmm3
    add rsi, 64
    add rdi, 64
    sub rdx, 64
    cmp rdx, 64
    jae .Lmc_64
.Lmc_16:
    cmp rdx, 16
    jb .Lmc_tail
.p2align 4
.Lmc_16b:
    movdqu xmm0, xmmword ptr [rsi]
    movdqa xmmword ptr [rdi], xmm0
    add rsi, 16
    add rdi, 16
    sub rdx, 16
    cmp rdx, 16
    jae .Lmc_16b
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
    ret

.globl rt_memset_sse2
.type rt_memset_sse2, @function
.p2align 4
rt_memset_sse2:
    mov rax, rdi
    movzx esi, sil
    cmp rdx, 16
    jb .Lms_tail
    movd xmm0, esi
    punpcklbw xmm0, xmm0
    punpcklwd xmm0, xmm0
    pshufd xmm0, xmm0, 0
    mov rcx, rdi
    and rcx, 15
    jz .Lms_body
    neg rcx
    add rcx, 16
    sub rdx, rcx
.Lms_head:
    mov byte ptr [rdi], sil
    inc rdi
    dec rcx
    jnz .Lms_head
.Lms_body:
    cmp rdx, 64
    jb .Lms_16
.p2align 4
.Lms_64:
    movdqa xmmword ptr [rdi], xmm0
    movdqa xmmword ptr [rdi + 16], xmm0
    movdqa xmmword ptr [rdi + 32], xmm0
    movdqa xmmword ptr [rdi + 48], xmm0
    add rdi, 64
    sub rdx, 64
    cmp rdx, 64
    jae .Lms_64
.Lms_16:
    cmp rdx, 16
    jb .Lms_tail
.p2align 4
.Lms_16b:
    movdqa xmmword ptr [rdi], xmm0
    add rdi, 16
    sub rdx, 16
    cmp rdx, 16
    jae .Lms_16b
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
    ret

.globl rt_memcmp_sse2
.type rt_memcmp_sse2, @function
.p2align 4
rt_memcmp_sse2:
    cmp rdx, 16
    jb .Lmp_bytes
.p2align 4
.Lmp_16:
    movdqu xmm0, xmmword ptr [rdi]
    movdqu xmm1, xmmword ptr [rsi]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    jnz .Lmp_diff
    add rdi, 16
    add rsi, 16
    sub rdx, 16
    cmp rdx, 16
    jae .Lmp_16
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
    ret
.Lmp_diff:
    bsf ecx, eax
    movzx eax, byte ptr [rdi + rcx]
    movzx r8d, byte ptr [rsi + rcx]
    sub eax, r8d
.Lmp_ret:
    ret

.section .note.GNU-stack, "", @progbits
