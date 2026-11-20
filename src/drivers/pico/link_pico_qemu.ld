/*
 * link_pico_qemu.ld — Linker Script for Raw Pico Series QEMU (`make run-pico`)
 *
 * Compatible with both:
 *   - qemu-system-arm -M mps2-an521 (Dual Cortex-M33, RP2350 profile)
 *   - qemu-system-arm -M mps2-an385 (Cortex-M3, RP2040 Thumb-1 profile)
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS
{
    .text : {
        __vectors_start__ = .;
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    __data_load__ = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Dedicated 4 KiB MSP stack at top of SRAM */
    __stack_top__ = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end__ <= (__stack_top__ - 4096), "SRAM overflow: .bss collides with MSP stack")
}
