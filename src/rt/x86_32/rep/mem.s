# 80386 flat SysV (-m32). REP MOVSD / STOSD is the fastest general copy on
# the 386 and 486: the microcode overlaps the bus, and an unrolled MOV loop
# cannot beat it. Dest is aligned to 4 first (unaligned dwords take two bus
# cycles). DF is cleared on exit, as the ABI requires. No cmov, no MMX.

.intel_syntax noprefix
.code32
.section .text

.globl rt_memcpy_i386
.type rt_memcpy_i386, @function
.p2align 4
rt_memcpy_i386:
    push ebx
    push esi
    push edi
    mov edi, dword ptr [esp + 16]
    mov esi, dword ptr [esp + 20]
    mov ecx, dword ptr [esp + 24]
    mov eax, edi
    cld
    cmp ecx, 4
    jb .Lmc_bytes
    mov edx, edi
    and edx, 3
    jz .Lmc_rep
    neg edx
    add edx, 4
    sub ecx, edx
.Lmc_head:
    mov bl, byte ptr [esi]
    mov byte ptr [edi], bl
    inc esi
    inc edi
    dec edx
    jnz .Lmc_head
.Lmc_rep:
    mov edx, ecx
    shr ecx, 2
    rep movsd
    mov ecx, edx
    and ecx, 3
.Lmc_bytes:
    rep movsb
    pop edi
    pop esi
    pop ebx
    ret

.globl rt_memset_i386
.type rt_memset_i386, @function
.p2align 4
rt_memset_i386:
    push ebx
    push edi
    mov edi, dword ptr [esp + 12]
    movzx edx, byte ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    mov ebx, edi
    cld
    cmp ecx, 4
    jb .Lms_bytes
    mov eax, edi
    and eax, 3
    jz .Lms_splat
    neg eax
    add eax, 4
    sub ecx, eax
.Lms_head:
    mov byte ptr [edi], dl
    inc edi
    dec eax
    jnz .Lms_head
.Lms_splat:
    # 386 has no cheap imul-by-0x01010101. Build the dword with shifts.
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
    mov edx, eax
.Lms_bytes:
    mov eax, edx
    rep stosb
    mov eax, ebx
    pop edi
    pop ebx
    ret

.globl rt_memcmp_i386
.type rt_memcmp_i386, @function
.p2align 4
rt_memcmp_i386:
    push esi
    push edi
    mov edi, dword ptr [esp + 12]
    mov esi, dword ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    cld
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
