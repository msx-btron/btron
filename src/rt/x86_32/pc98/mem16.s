# PC-98 real mode, 386+. Not the SysV ABI and not linked into verify:
#   ES:DI dest, DS:SI src, CX byte count, DF clear on exit.
#   For memset: ES:DI dest, AL byte, CX count.
# 32-bit string ops need an operand-size prefix; GAS emits it under .code16.
# VRAM variants stay on words so a text cell (chr, attr) is one bus cycle.

.intel_syntax noprefix
.code16
.section .text

.globl rt_memcpy_pc98_real
.type rt_memcpy_pc98_real, @function
.p2align 4
rt_memcpy_pc98_real:
    push si
    push di
    cld
    mov bx, cx
    shr cx, 2
    rep movsd
    mov cx, bx
    and cx, 3
    rep movsb
    pop di
    pop si
    ret

.globl rt_memset_pc98_real
.type rt_memset_pc98_real, @function
.p2align 4
rt_memset_pc98_real:
    push di
    push bx
    cld
    mov ah, al
    mov bx, ax
    shl eax, 16
    mov ax, bx
    mov bx, cx
    shr cx, 2
    rep stosd
    mov cx, bx
    and cx, 3
    rep stosb
    pop bx
    pop di
    ret

.globl rt_memcpy_pc98_vram_real
.type rt_memcpy_pc98_vram_real, @function
.p2align 4
rt_memcpy_pc98_vram_real:
    push si
    push di
    cld
    test di, 1
    jz 1f
    test cx, cx
    jz 2f
    movsb
    dec cx
1:
    mov bx, cx
    shr cx, 1
    rep movsw
    mov cx, bx
    and cx, 1
    rep movsb
2:
    pop di
    pop si
    ret

.globl rt_memset_pc98_vram_real
.type rt_memset_pc98_vram_real, @function
.p2align 4
rt_memset_pc98_vram_real:
    push di
    cld
    test di, 1
    jz 1f
    test cx, cx
    jz 2f
    stosb
    dec cx
1:
    mov ah, al
    mov bx, cx
    shr cx, 1
    rep stosw
    mov cx, bx
    and cx, 1
    rep stosb
2:
    pop di
    ret
