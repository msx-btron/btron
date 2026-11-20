/*
 * hal_qemu_mps2.c — QEMU Cortex-M HAL for Raw Pico Series (`make run-pico`)
 *
 * Machines:
 *   - RP2350 profile (default): qemu-system-arm -M mps2-an521 (Dual Cortex-M33)
 *       CMSDK APB UART0 Secure base: 0x50200000
 *   - RP2040 profile (PICO_BOARD=rp2040): qemu-system-arm -M mps2-an385 (Cortex-M3)
 *       CMSDK APB UART0 base: 0x40004000
 *
 * Spec: ARM CoreLink SSE-200 / CMSDK APB UART Technical Reference Manual.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "pico_hal.h"

#if defined(PICO_BOARD_RP2040)
#define CMSDK_UART0_BASE    0x40004000UL
#define PICO_QEMU_BOARD     "QEMU mps2-an385 (RP2040 / Pico 1 Profile)"
#define PICO_QEMU_CPU       "ARM Cortex-M0+ (Thumb-1 on MPS2-AN385)"
#define PICO_QEMU_SRAM      (264UL * 1024UL)
#define PICO_QEMU_FLASH     (2048UL * 1024UL)
#else
#define CMSDK_UART0_BASE    0x50200000UL
#define PICO_QEMU_BOARD     "QEMU mps2-an521 (RP2350 / Pico 2 Profile)"
#define PICO_QEMU_CPU       "ARM Cortex-M33 (ARMv8-M Mainline + FPv5-SP)"
#define PICO_QEMU_SRAM      (520UL * 1024UL)
#define PICO_QEMU_FLASH     (4096UL * 1024UL)
#endif

#define UART_DATA           (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x00UL))
#define UART_STATE          (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x04UL))
#define UART_CTRL           (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x08UL))
#define UART_INTCLEAR       (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x0CUL))
#define UART_BAUDDIV        (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x10UL))

#define UART_STATE_TXFULL   (1UL << 0)
#define UART_STATE_RXFULL   (1UL << 1)
#define UART_CTRL_TXEN      (1UL << 0)
#define UART_CTRL_RXEN      (1UL << 1)

/* MPS2 FPGA system clock = 25 MHz */
#define MPS2_SYSCLK_HZ      25000000UL

void pico_hal_early_init(void)
{
    UART_CTRL     = 0;
    UART_INTCLEAR = 0x0FUL;
    UART_BAUDDIV  = MPS2_SYSCLK_HZ / 115200UL; /* 217 (>= 16 required by CMSDK) */
    UART_CTRL     = UART_CTRL_TXEN | UART_CTRL_RXEN;
}

void pico_hal_uart_putc(char c)
{
    if (c == '\n') {
        while (UART_STATE & UART_STATE_TXFULL) { }
        UART_DATA = '\r';
    }
    while (UART_STATE & UART_STATE_TXFULL) { }
    UART_DATA = (uint32_t)(uint8_t)c;
}

void pico_hal_uart_puts(const char *s)
{
    if (!s) return;
    while (*s) {
        pico_hal_uart_putc(*s++);
    }
}

int pico_hal_uart_getc_nonblock(void)
{
    if (UART_STATE & UART_STATE_RXFULL) {
        return (int)(UART_DATA & 0xFFUL);
    }
    return -1;
}

uint32_t pico_hal_sysclk_hz(void)
{
    return MPS2_SYSCLK_HZ;
}

uint32_t pico_hal_board_sram_bytes(void)
{
    return PICO_QEMU_SRAM;
}

uint32_t pico_hal_board_flash_bytes(void)
{
    return PICO_QEMU_FLASH;
}

const char *pico_hal_board_name(void)
{
    return PICO_QEMU_BOARD;
}

const char *pico_hal_cpu_name(void)
{
    return PICO_QEMU_CPU;
}

void pico_hal_reboot(int to_bootsel)
{
    (void)to_bootsel;
    pico_hal_uart_puts("\n[HAL ] Requesting Cortex-M AIRCR system reset...\n");
    __asm__ volatile ("dsb" ::: "memory");
    CM_SCB_AIRCR = CM_AIRCR_SYSRESET;
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

void pico_hal_panic_display(const char *reason, uint32_t pc,
                            uint32_t lr, uint32_t psr, uint32_t cfsr)
{
    (void)reason; (void)pc; (void)lr; (void)psr; (void)cfsr;
}
