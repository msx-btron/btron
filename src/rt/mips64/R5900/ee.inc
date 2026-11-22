# R5900 encodings. Clang's assembler has no MMI mnemonics, so the
# instructions are the words QEMU's tx79.decode accepts. PCSX2 executes
# the same words. Register numbers are the hardware numbers, not names.
#
#   LQ  011110 base rt offset
#   SQ  011111 base rt offset
#   MMI 011100 rs rt rd function
#   PCEQB  0x2A8   PCPYLD 0x389   PCPYUD 0x3A9   PXOR 0x4C9

.macro ee_lq rt, base, off
    .word 0x78000000 | ((\base) << 21) | ((\rt) << 16) | (\off)
.endm

.macro ee_sq rt, base, off
    .word 0x7c000000 | ((\base) << 21) | ((\rt) << 16) | (\off)
.endm

.macro ee_pceqb rd, rs, rt
    .word 0x70000000 | ((\rs) << 21) | ((\rt) << 16) | ((\rd) << 11) | 0x2a8
.endm

.macro ee_pcpyld rd, rs, rt
    .word 0x70000000 | ((\rs) << 21) | ((\rt) << 16) | ((\rd) << 11) | 0x389
.endm

.macro ee_pcpyud rd, rs, rt
    .word 0x70000000 | ((\rs) << 21) | ((\rt) << 16) | ((\rd) << 11) | 0x3a9
.endm

.macro ee_pxor rd, rs, rt
    .word 0x70000000 | ((\rs) << 21) | ((\rt) << 16) | ((\rd) << 11) | 0x4c9
.endm

# Unsigned length from an o32/n32 caller is sign-extended. A 64-bit
# compare wants the zero-extended value. n64 already passes that.
.macro ee_zext_len reg
    .ifdef RT_MIPS_ILP32
    dsll32 \reg, \reg, 0
    dsrl32 \reg, \reg, 0
    .endif
.endm

# Branch to \bad when a 16-byte load at \ptr would cross a page.
# An aligned load at offset 4080 stays inside the page. Clobbers $t6.
.macro ee_page16 ptr, bad
    andi $t6, \ptr, 0xfff
    sltiu $t6, $t6, 4081
    beqz $t6, \bad
    nop
.endm

# Branch to \label when the unsigned register is below a small immediate.
# Clobbers $t7.
.macro ee_if_lt reg, imm, label
    dsrl32 $t7, \reg, 0
    beqz $t7, 91\@f
    nop
    b 92\@f
    nop
91\@:
    sltiu $t7, \reg, \imm
    bnez $t7, \label
    nop
92\@:
.endm
