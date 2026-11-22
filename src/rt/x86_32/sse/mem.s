# Pentium III / Athlon XP, SysV -m32. SSE, not SSE2.
# movups/movaps move 16 bytes. pcmpeqb and integer punpck on XMM are
# SSE2, so memcmp stays on dword compares.
# Destination is aligned to 16, then movaps. Loads stay movups.
# Under 32 bytes, REP MOVSD wins on a P3, so that path never touches XMM.
# XMM0-7 are caller-saved. No MMX, so no EMMS.

.intel_syntax noprefix
.code32
.section .text

.globl rt_memcpy_sse
.type rt_memcpy_sse, @function
.p2align 4
rt_memcpy_sse:
    push ebx
    push esi
    push edi
    mov edi, dword ptr [esp + 16]
    mov esi, dword ptr [esp + 20]
    mov ecx, dword ptr [esp + 24]
    mov eax, edi
    cld
    cmp ecx, 32
    jb .Lmc_gpr
    mov edx, edi
    and edx, 15
    jz .Lmc_body
    neg edx
    add edx, 16
    sub ecx, edx
.Lmc_head:
    mov bl, byte ptr [esi]
    mov byte ptr [edi], bl
    inc esi
    inc edi
    dec edx
    jnz .Lmc_head
.Lmc_body:
    cmp ecx, 64
    jb .Lmc_16
.p2align 4
.Lmc_64:
    movups xmm0, xmmword ptr [esi]
    movups xmm1, xmmword ptr [esi + 16]
    movups xmm2, xmmword ptr [esi + 32]
    movups xmm3, xmmword ptr [esi + 48]
    movaps xmmword ptr [edi], xmm0
    movaps xmmword ptr [edi + 16], xmm1
    movaps xmmword ptr [edi + 32], xmm2
    movaps xmmword ptr [edi + 48], xmm3
    add esi, 64
    add edi, 64
    sub ecx, 64
    cmp ecx, 64
    jae .Lmc_64
.Lmc_16:
    cmp ecx, 16
    jb .Lmc_gpr
.p2align 4
.Lmc_16b:
    movups xmm0, xmmword ptr [esi]
    movaps xmmword ptr [edi], xmm0
    add esi, 16
    add edi, 16
    sub ecx, 16
    cmp ecx, 16
    jae .Lmc_16b
.Lmc_gpr:
    mov edx, ecx
    shr ecx, 2
    rep movsd
    mov ecx, edx
    and ecx, 3
    rep movsb
    pop edi
    pop esi
    pop ebx
    ret

.globl rt_memset_sse
.type rt_memset_sse, @function
.p2align 4
rt_memset_sse:
    push ebx
    push edi
    mov edi, dword ptr [esp + 12]
    movzx edx, byte ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    mov ebx, edi
    cld
    mov eax, edx
    shl edx, 8
    or eax, edx
    mov edx, eax
    shl edx, 16
    or eax, edx
    cmp ecx, 32
    jb .Lms_gpr
    mov edx, edi
    and edx, 15
    jz .Lms_vec
    neg edx
    add edx, 16
    sub ecx, edx
.Lms_head:
    mov byte ptr [edi], al
    inc edi
    dec edx
    jnz .Lms_head
.Lms_vec:
    sub esp, 16
    mov dword ptr [esp], eax
    mov dword ptr [esp + 4], eax
    mov dword ptr [esp + 8], eax
    mov dword ptr [esp + 12], eax
    movups xmm0, xmmword ptr [esp]
    add esp, 16
    cmp ecx, 64
    jb .Lms_16
.p2align 4
.Lms_64:
    movaps xmmword ptr [edi], xmm0
    movaps xmmword ptr [edi + 16], xmm0
    movaps xmmword ptr [edi + 32], xmm0
    movaps xmmword ptr [edi + 48], xmm0
    add edi, 64
    sub ecx, 64
    cmp ecx, 64
    jae .Lms_64
.Lms_16:
    cmp ecx, 16
    jb .Lms_gpr
.p2align 4
.Lms_16b:
    movaps xmmword ptr [edi], xmm0
    add edi, 16
    sub ecx, 16
    cmp ecx, 16
    jae .Lms_16b
.Lms_gpr:
    mov edx, ecx
    shr ecx, 2
    rep stosd
    mov ecx, edx
    and ecx, 3
    rep stosb
    mov eax, ebx
    pop edi
    pop ebx
    ret

# No SSE2 integer compare. Dword equality, then a byte scan on mismatch.
.globl rt_memcmp_sse
.type rt_memcmp_sse, @function
.p2align 4
rt_memcmp_sse:
    push esi
    push edi
    mov edi, dword ptr [esp + 12]
    mov esi, dword ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
.Lmp_d:
    cmp ecx, 4
    jb .Lmp_b
    mov edx, dword ptr [edi]
    cmp edx, dword ptr [esi]
    jne .Lmp_b
    add edi, 4
    add esi, 4
    sub ecx, 4
    jmp .Lmp_d
.Lmp_b:
    test ecx, ecx
    jz .Lmp_eq
    movzx eax, byte ptr [edi]
    movzx edx, byte ptr [esi]
    sub eax, edx
    jne .Lmp_out
    inc edi
    inc esi
    dec ecx
    jmp .Lmp_b
.Lmp_eq:
    xor eax, eax
.Lmp_out:
    pop edi
    pop esi
    ret

.section .note.GNU-stack, "", @progbits
