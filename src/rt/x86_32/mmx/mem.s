# Pentium MMX, SysV -m32. movq is 8 bytes / instruction and pairs poorly
# with integer ops, so the fast path is an unrolled aligned movq loop.
# Misaligned movq splits and is slower than a plain dword MOV on a Pentium,
# so MMX is used only when (dst - src) is 8-aligned (memcpy) or for aligned
# stores (memset). EMMS on every path that touched an MM register: the x87
# stack is otherwise dead for the caller. Below 32 bytes, GPR is faster
# because EMMS itself is ~10+ clocks.

.intel_syntax noprefix
.code32
.section .text

.globl rt_memcpy_mmx
.type rt_memcpy_mmx, @function
.p2align 4
rt_memcpy_mmx:
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
    xor edx, esi
    test edx, 7
    jnz .Lmc_gpr
    mov edx, edi
    and edx, 7
    jz .Lmc_body
    neg edx
    add edx, 8
    sub ecx, edx
.Lmc_head:
    mov bl, byte ptr [esi]
    mov byte ptr [edi], bl
    inc esi
    inc edi
    dec edx
    jnz .Lmc_head
.Lmc_body:
    cmp ecx, 32
    jb .Lmc_mmx_done
.p2align 4
.Lmc_32:
    movq mm0, qword ptr [esi]
    movq mm1, qword ptr [esi + 8]
    movq mm2, qword ptr [esi + 16]
    movq mm3, qword ptr [esi + 24]
    movq qword ptr [edi], mm0
    movq qword ptr [edi + 8], mm1
    movq qword ptr [edi + 16], mm2
    movq qword ptr [edi + 24], mm3
    add esi, 32
    add edi, 32
    sub ecx, 32
    cmp ecx, 32
    jae .Lmc_32
.Lmc_mmx_done:
    emms
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

.globl rt_memset_mmx
.type rt_memset_mmx, @function
.p2align 4
rt_memset_mmx:
    push ebx
    push edi
    mov edi, dword ptr [esp + 12]
    movzx edx, byte ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    mov ebx, edi
    cld
    cmp ecx, 32
    jb .Lms_gpr
    mov eax, edi
    and eax, 7
    jz .Lms_splat
    neg eax
    add eax, 8
    sub ecx, eax
.Lms_head:
    mov byte ptr [edi], dl
    inc edi
    dec eax
    jnz .Lms_head
.Lms_splat:
    movd mm0, edx
    punpcklbw mm0, mm0
    punpcklwd mm0, mm0
    punpckldq mm0, mm0
    cmp ecx, 32
    jb .Lms_mmx_done
.p2align 4
.Lms_32:
    movq qword ptr [edi], mm0
    movq qword ptr [edi + 8], mm0
    movq qword ptr [edi + 16], mm0
    movq qword ptr [edi + 24], mm0
    add edi, 32
    sub ecx, 32
    cmp ecx, 32
    jae .Lms_32
.Lms_mmx_done:
    emms
.Lms_gpr:
    mov eax, edx
    shl edx, 8
    or eax, edx
    mov edx, eax
    shl edx, 16
    or eax, edx
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

.globl rt_memcmp_mmx
.type rt_memcmp_mmx, @function
.p2align 4
rt_memcmp_mmx:
    push esi
    push edi
    mov edi, dword ptr [esp + 12]
    mov esi, dword ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    cmp ecx, 32
    jb .Lmp_gpr
    mov edx, edi
    or edx, esi
    test edx, 7
    jnz .Lmp_gpr
.Lmp_8:
    cmp ecx, 8
    jb .Lmp_tail
    movq mm0, qword ptr [edi]
    movq mm1, qword ptr [esi]
    pcmpeqb mm0, mm1
    movd eax, mm0
    cmp eax, -1
    jne .Lmp_mmx_diff
    psrlq mm0, 32
    movd eax, mm0
    cmp eax, -1
    jne .Lmp_mmx_diff
    add edi, 8
    add esi, 8
    sub ecx, 8
    jmp .Lmp_8
.Lmp_mmx_diff:
    emms
    jmp .Lmp_gpr
.Lmp_tail:
    emms
.Lmp_gpr:
    cmp ecx, 4
    jb .Lmp_b
    mov edx, dword ptr [edi]
    cmp edx, dword ptr [esi]
    jne .Lmp_b
    add edi, 4
    add esi, 4
    sub ecx, 4
    jmp .Lmp_gpr
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
