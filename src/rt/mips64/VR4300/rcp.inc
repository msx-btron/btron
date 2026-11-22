# VR4300 helpers. The Nintendo 64 CPU is MIPS III: 64-bit GPRs, ld/sd,
# no MSA, no MMI, no RSP VU. These macros stay inside the integer pipe.
# o32 lengths are sign-extended; n64 lengths are already zero-extended.
#
# Temporaries are $8-$15 (O32 t0 through t7). Clang's n64 assembler
# binds the symbolic name t0 to GPR 12 and folds t4 into that same
# GPR, so those names are not used in this directory. t8 and t9
# ($24 and $25) are the same name in o32 and n64.

.macro rcp_zext_len reg
    .ifdef RT_MIPS_ILP32
    dsll32 \reg, \reg, 0
    dsrl32 \reg, \reg, 0
    .endif
.endm

# Branch to \bad when an 8-byte load at \ptr would cross a page.
# Offset 4088 stays inside the page. Clobbers $14.
.macro rcp_page8 ptr, bad
    andi $14, \ptr, 0xfff
    sltiu $14, $14, 4089
    beqz $14, \bad
    nop
.endm

# Branch to \label when the unsigned register is below a small immediate.
# A length at or above 2^32 is not small. Clobbers $15.
.macro rcp_if_lt reg, imm, label
    dsrl32 $15, \reg, 0
    beqz $15, 91\@f
    nop
    b 92\@f
    nop
91\@:
    sltiu $15, \reg, \imm
    bnez $15, \label
    nop
92\@:
.endm

# ONES in $t8, HIS in $t9. Clobbers $12.
.macro rcp_consts
    lui $t8, 0x0101
    ori $t8, $t8, 0x0101
    dsll32 $12, $t8, 0
    or $t8, $t8, $12
    dsll $t9, $t8, 7
.endm

# dst = haszero(src). $t8/$t9 hold the constants. Clobbers $12.
.macro rcp_haszero src, dst
    dsubu \dst, \src, $t8
    nor $12, \src, $zero
    and \dst, \dst, $12
    and \dst, \dst, $t9
.endm
