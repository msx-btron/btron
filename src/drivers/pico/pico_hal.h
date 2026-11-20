/*
 * pico_hal.h — Clean-room Hardware Abstraction Layer for B-System Pico / PicoCalc
 *
 * Shared contract between:
 *   - hal_qemu_mps2.c (Phase 1: QEMU mps2-an521 Cortex-M33 / mps2-an385 Cortex-M3)
 *   - hal_rp2xxx.c    (Phase 3: Real RP2350 Pico 2 / RP2040 Pico 1 silicon)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_PICO_HAL_H_
#define _BTRON_PICO_HAL_H_

#include <stdint.h>
#include <stddef.h>

/* Cortex-M System Control Space (ARMv6-M / ARMv8-M Architecture Reference Manual) */
#define CM_SCS_BASE         0xE000E000UL
#define CM_SYST_CSR         (*(volatile uint32_t *)(CM_SCS_BASE + 0x010UL))
#define CM_SYST_RVR         (*(volatile uint32_t *)(CM_SCS_BASE + 0x014UL))
#define CM_SYST_CVR         (*(volatile uint32_t *)(CM_SCS_BASE + 0x018UL))
#define CM_SCB_ICSR         (*(volatile uint32_t *)(CM_SCS_BASE + 0xD04UL))
#define CM_SCB_VTOR         (*(volatile uint32_t *)(CM_SCS_BASE + 0xD08UL))
#define CM_SCB_AIRCR        (*(volatile uint32_t *)(CM_SCS_BASE + 0xD0CUL))
#define CM_SCB_SHPR2        (*(volatile uint32_t *)(CM_SCS_BASE + 0xD1CUL))
#define CM_SCB_SHPR3        (*(volatile uint32_t *)(CM_SCS_BASE + 0xD20UL))
#define CM_SCB_CFSR         (*(volatile uint32_t *)(CM_SCS_BASE + 0xD28UL))
#define CM_SCB_HFSR         (*(volatile uint32_t *)(CM_SCS_BASE + 0xD2CUL))

#define CM_ICSR_PENDSVSET   (1UL << 28)
#define CM_SYST_ENABLE      (1UL << 0)
#define CM_SYST_TICKINT     (1UL << 1)
#define CM_SYST_CLKSOURCE   (1UL << 2)
#define CM_AIRCR_SYSRESET   (0x05FA0004UL)

/* Platform HAL (implemented by hal_qemu_mps2.c or hal_rp2xxx.c) */
void        pico_hal_early_init(void);
void        pico_hal_uart_putc(char c);
void        pico_hal_uart_puts(const char *s);
int         pico_hal_uart_getc_nonblock(void);
uint32_t    pico_hal_sysclk_hz(void);
uint32_t    pico_hal_board_sram_bytes(void);
uint32_t    pico_hal_board_flash_bytes(void);
const char *pico_hal_board_name(void);
const char *pico_hal_cpu_name(void);
void        pico_hal_reboot(int to_bootsel);

/* Optional raw-display HardFault hook (no-op in QEMU, red SPI banner on PicoCalc) */
void        pico_hal_panic_display(const char *reason, uint32_t pc,
                                   uint32_t lr, uint32_t psr, uint32_t cfsr);

/* Kernel entry & RTOS scheduler hooks (implemented by core_pico.c) */
void        pico_kernel_entry(void);
uint32_t   *pico_sched_switch(uint32_t *cur_psp);
void        pico_systick_isr(void);
void        pico_hardfault_c(uint32_t *frame);

/* Optional UI core hook (weak no-op in core_pico.c, overridden by core_segui.c) */
void        pico_ui_start(void);

#endif /* _BTRON_PICO_HAL_H_ */
