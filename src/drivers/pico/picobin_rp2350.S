/*
 * picobin_rp2350.S — Minimal RP2350 Secure ARM IMAGE_DEF PICOBIN Block
 *
 * Spec: RP2350 Datasheet Section 5.9 (Metadata Blocks / IMAGE_DEF).
 * Must reside within the first 4 KiB of Flash so the RP2350 BootROM
 * recognizes this binary as a valid Executable Secure ARM RP2350 image.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

    .syntax unified
    .thumb
    .section .picobin, "a", %progbits
    .align 2
    .global __picobin_start__
__picobin_start__:
    .word 0xFFFFDED3             /* PICOBIN_BLOCK_MARKER_START             */
    .byte 0x42                   /* PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE      */
    .byte 0x01                   /* 1-word item size                       */
    .hword 0x1021                /* EXE (1) | Secure (2) | ARM (0) | RP2350(1) */
    .byte 0xFF                   /* PICOBIN_BLOCK_ITEM_2BS_LAST            */
    .hword 0x0001                /* total items size = 1 word              */
    .byte 0x00                   /* pad                                    */
    .word 0x00000000             /* relative next-block link (0 = self)    */
    .word 0xAB123579             /* PICOBIN_BLOCK_MARKER_END               */
