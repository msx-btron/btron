# NEC PC-9801 / PC-9821, i386, flat 32-bit SysV (DOS extender or 386 host).
#
# Main RAM: same strategy as a contemporary 386 AT. REP MOVSD / STOSD after
# aligning the destination. The PC-98 local bus penalises a split dword
# harder than an AT chipset, so the align-to-4 head is not optional.
#
# Graphics / text VRAM (text typically A000:0000, G-VRAM A800:0000 or
# E000:0000 depending on mode): the GDC and the 16-bit video plane want
# word stores. A 32-bit write can tear a character cell or hit one plane
# twice. rt_*_pc98_vram therefore uses MOVSW / STOSW only.
# mem16.s is the real-mode far-pointer form of the same idea.

.intel_syntax noprefix
.code32
.section .text

.globl rt_memcpy_pc98
.type rt_memcpy_pc98, @function
.p2align 4
rt_memcpy_pc98:
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

.globl rt_memset_pc98
.type rt_memset_pc98, @function
.p2align 4
rt_memset_pc98:
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

.globl rt_memcmp_pc98
.type rt_memcmp_pc98, @function
.p2align 4
rt_memcmp_pc98:
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

# Word path for PC-98 VRAM. Odd destination gets one leading byte so every
# MOVSW lands on a character/plane boundary. Odd count gets one tail byte.
.globl rt_memcpy_pc98_vram
.type rt_memcpy_pc98_vram, @function
.p2align 4
rt_memcpy_pc98_vram:
    push esi
    push edi
    mov edi, dword ptr [esp + 12]
    mov esi, dword ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    mov eax, edi
    cld
    test edi, 1
    jz .Lmv_even
    test ecx, ecx
    jz .Lmv_out
    movsb
    dec ecx
.Lmv_even:
    mov edx, ecx
    shr ecx, 1
    rep movsw
    mov ecx, edx
    and ecx, 1
    rep movsb
.Lmv_out:
    pop edi
    pop esi
    ret

.globl rt_memset_pc98_vram
.type rt_memset_pc98_vram, @function
.p2align 4
rt_memset_pc98_vram:
    push ebx
    push edi
    mov edi, dword ptr [esp + 12]
    movzx edx, byte ptr [esp + 16]
    mov ecx, dword ptr [esp + 20]
    mov ebx, edi
    cld
    test edi, 1
    jz .Lsv_even
    test ecx, ecx
    jz .Lsv_out
    mov byte ptr [edi], dl
    inc edi
    dec ecx
.Lsv_even:
    mov eax, edx
    shl eax, 8
    or eax, edx
    mov edx, ecx
    shr ecx, 1
    rep stosw
    mov ecx, edx
    and ecx, 1
    rep stosb
.Lsv_out:
    mov eax, ebx
    pop edi
    pop ebx
    ret

.section .note.GNU-stack, "", @progbits
