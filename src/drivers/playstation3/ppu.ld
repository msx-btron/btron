OUTPUT_ARCH(powerpc:common64)
OUTPUT_FORMAT(elf64-powerpc)
ENTRY(_start)

SECTIONS {
    . = 0x10000;
    .text : { *(.text) *(.rodata) }
    .bss  : { *(.bss) *(COMMON) }
}