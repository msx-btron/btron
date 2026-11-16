.text
.globl _start
_start:
    lis   4, msg@ha
    addi  4, 4, msg@l
    li    3, 0              /* SYS_TTYP_PPU_STDOUT */
    li    5, msglen
    lis   6, written@ha
    addi  6, 6, written@l
    li    0, 403            /* sys_tty_write */
    sc

    li    0, 3              /* sys_process_exit */
    li    3, 0
    sc
    b     .

.section .rodata
msg:
    .ascii "BTRON PPU hello\n"
msglen = . - msg

.section .bss
.lcomm written, 4
