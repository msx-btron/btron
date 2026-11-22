/*
 * link_picocalc.ld — Parameterized Real-Hardware Linker Script for PicoCalc
 *
 * Preprocessed or selected via symbols:
 *   BTRON_FLASH_ORIGIN : 0x10000000 (UF2) or 0x10032000 (+200 KiB SD-Boot)
 *   BTRON_FLASH_LENGTH : 4096K (RP2350) / 2048K (RP2040), minus offset
 *   BTRON_RAM_LENGTH   : 512K (RP2350) / 256K (RP2040)
 *
 * Sections:
 *   - .boot2   (RP2040 standalone UF2 only: 256 bytes at 0x10000000)
 *   - .vectors (256-byte aligned Cortex-M vector table)
 *   - .picobin (RP2350 standalone UF2 only: within first 4 KiB)
 *   - .text, .rodata, .data (copied to SRAM), .bss (zeroed in SRAM)
 */

ENTRY(Reset_Handler)

SECTIONS
{
    . = BTRON_FLASH_ORIGIN;

    .boot2 : {
        KEEP(*(.boot2))
    }

    .text : ALIGN(256) {
        __vectors_start__ = .;
        KEEP(*(.vectors))
        KEEP(*(.picobin))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    }

    /* Scratch bank X. VMA is the 4 KB past the striped SRAM. The span kernel
     * is copied there from flash. It is outside the 210 KB assert. */
    __scratch_load__ = .;
    .scratch_x (0x20000000 + BTRON_RAM_LENGTH) : AT(__scratch_load__) {
        __scratch_x_start__ = .;
        *(.scratch_x*)
        . = ALIGN(4);
        __scratch_x_end__ = .;
    }
    __data_load__ = __scratch_load__ + (__scratch_x_end__ - __scratch_x_start__);
    .data 0x20000000 : AT(__data_load__) {
        . = ALIGN(4);
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    }

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    }

    __stack_top__ = 0x20000000 + BTRON_RAM_LENGTH;
    /* Core 1 stack is scratch bank Y. The Quake build nests the camera,
     * a world triangle, and soft-float. One kilobyte was short of that. */
    __core1_stack_top__ = 0x20000000 + BTRON_RAM_LENGTH + 4096 + 4096;
    __sram_used__ = (__data_end__ - __data_start__) + (__bss_end__ - __bss_start__);
    ASSERT(__sram_used__ <= 215040, "PicoCalc SRAM budget exceeded (> 210 KiB)!")
    ASSERT(__bss_end__ <= (__stack_top__ - 4096), "SRAM overflow into MSP stack!")
    ASSERT((__scratch_x_end__ - __scratch_x_start__) <= 4096, "scratch_x exceeds 4 KiB")

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.note*)
        *(.comment*)
    }
}
