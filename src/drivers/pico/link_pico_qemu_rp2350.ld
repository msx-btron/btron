/*
 * link_pico_qemu_rp2350.ld — QEMU mps2-an521 (Dual Cortex-M33 Secure)
 *
 * SSE-200 Secure Memory Map:
 *   - 0x10000000 (4 MiB): Secure ZBT SSRAM1 (Code — matches RP2350 Flash base!)
 *   - 0x30000000 (128 KiB): Secure Internal SRAM Banks 0..3
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x30000000, LENGTH = 128K
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

    __stack_top__ = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end__ <= (__stack_top__ - 4096), "SRAM overflow: .bss collides with MSP stack")
}
