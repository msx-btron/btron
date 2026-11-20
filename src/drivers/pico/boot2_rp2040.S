/*
 * boot2_rp2040.S — Clean-Room 256-Byte RP2040 Generic-03 XIP Stage-2 Bootloader
 *
 * Spec: RP2040 Datasheet Section 2.8.1.3 (Flash Second Stage).
 * Configures XIP SSI for standard SPI Read (0x03) with CLKDIV=4 (compatible
 * with 100% of Winbond/GigaDevice/ISSI flash chips on all Pico 1 boards),
 * sets VTOR = 0x10000100, loads MSP & Reset_Handler, and jumps to the OS.
 *
 * The trailing 4 bytes (offset 0xFC..0xFF) are patched with the MPEG-2 CRC32
 * of bytes 0x00..0xFB by scripts/tool_mk_uf2.py.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

    .syntax unified
    .cpu cortex-m0plus
    .thumb

    .section .boot2, "ax", %progbits
    .align 2
    .global __boot2_start__
__boot2_start__:
    /* Configure XIP SSI (0x18000000) for standard 0x03 continuous read */
    ldr r3, =0x18000000          /* XIP_SSI_BASE                           */
    movs r0, #0
    str r0, [r3, #0x08]          /* SSI->SSIENR = 0 (disable to configure) */

    movs r0, #4
    str r0, [r3, #0x14]          /* SSI->BAUDR = 4 (safe universal divider)*/

    /* CTRLR0: DFS_32=31 (32-bit), TMOD=3 (EEPROM read) */
    ldr r0, =((31 << 16) | (3 << 8))
    str r0, [r3, #0x00]

    /* CTRLR1: NDF=0 (1 x 32-bit frame per XIP read) */
    movs r0, #0
    str r0, [r3, #0x04]

    /* SPI_CTRLR0 @ +0xF4: XIP_CMD=0x03, INST_L=2 (8-bit), ADDR_L=6 (24-bit) */
    ldr r0, =((0x03 << 24) | (2 << 8) | (6 << 2))
    ldr r1, =0x180000F4
    str r0, [r1]

    /* Enable SSI */
    movs r0, #1
    str r0, [r3, #0x08]          /* SSI->SSIENR = 1                        */

    /* Vector table sits immediately after boot2 at 0x10000100 */
    ldr r0, =0x10000100
    ldr r1, =0xE000ED08          /* SCB->VTOR                              */
    str r0, [r1]
    ldmia r0!, {r1, r2}          /* r1 = initial MSP, r2 = Reset_Handler   */
    msr msp, r1
    bx r2

    .ltorg
    /* Pad to 252 bytes; bytes 252..255 reserved for MPEG-2 CRC32 */
    .org 252, 0x00
    .word 0x00000000
