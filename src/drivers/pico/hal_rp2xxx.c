/*
 * hal_rp2xxx.c — Real-Silicon HAL for RP2350 (Pico 2, Default) & RP2040 (Pico 1)
 *
 * Implements:
 *   - 12 MHz XOSC -> PLL_SYS (150 MHz RP2350 | 125 MHz RP2040)
 *   - RP2350 TICKS generator (0x40108000) for TIMER0 (1 MHz) & PROC0 SysTick
 *   - RP2350 PADS_BANK0 ISO=0 pad un-isolation on every configured GPIO
 *   - Battery-first peripheral gating (only active peripherals un-reset)
 *   - PL011 UART0 (GP0 TX / GP1 RX @ 115200 8N1)
 *   - BootROM BOOTSEL reboot + Watchdog reset
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "pico_hal.h"
#include "pico_regs.h"

static void unreset_block(uint32_t mask)
{
    REG32_CLR(RESETS_RESET) = mask;
    while ((REG32(RESETS_RESET_DONE) & mask) != mask) { }
}

static void reset_and_unreset(uint32_t mask)
{
    REG32_SET(RESETS_RESET) = mask;
    __asm__ volatile ("dsb" ::: "memory");
    unreset_block(mask);
}

void pico_gpio_set_func(uint32_t pin, uint32_t func, int pull_up)
{
    uint32_t pad = PAD_IE | PAD_DRIVE_8MA;
    if (pull_up) pad |= PAD_PUE;
    /* RP2350 G2 Safeguard: writing pad without PAD_ISO clears bit 8 (ISO=0) */
    PAD_GPIO(pin)     = pad;
    IO_GPIO_CTRL(pin) = func & 0x1FUL;
}

void pico_gpio_init_out(uint32_t pin, int initial_high)
{
    pico_gpio_set_func(pin, GPIO_FUNC_SIO, 0);
    if (initial_high) SIO_GPIO_OUT_SET = (1UL << pin);
    else              SIO_GPIO_OUT_CLR = (1UL << pin);
    SIO_GPIO_OE_SET = (1UL << pin);
}

void pico_gpio_put(uint32_t pin, int high)
{
    if (high) SIO_GPIO_OUT_SET = (1UL << pin);
    else      SIO_GPIO_OUT_CLR = (1UL << pin);
}

int pico_gpio_get(uint32_t pin)
{
    return (int)((SIO_GPIO_IN >> pin) & 1UL);
}

void pico_delay_us(uint32_t us)
{
    uint32_t start = TIMER0_TIMERAWL;
    while ((uint32_t)(TIMER0_TIMERAWL - start) < us) { }
}

void pico_delay_ms(uint32_t ms)
{
    while (ms--) {
        pico_delay_us(1000u);
    }
}

static void clocks_init_12mhz_pll(void)
{
    /* 1. Switch CLK_SYS cleanly to CLK_REF (ROSC) before touching PLL */
    CLK_SYS_CTRL = 0;
    while ((CLK_SYS_SELECTED & 1UL) == 0UL) { }

    /* 2. Start 12 MHz XOSC (skip if already stable from PicoCalc Bootloader) */
    if ((XOSC_STATUS & XOSC_STATUS_STABLE) == 0UL) {
        XOSC_STARTUP = 47u; /* ~1 ms startup delay at 12 MHz */
        XOSC_CTRL    = XOSC_CTRL_ENABLE;
        while ((XOSC_STATUS & XOSC_STATUS_STABLE) == 0UL) { }
    }

    /* 3. Switch CLK_REF to XOSC (src = 2) */
    CLK_REF_CTRL = 2UL;
    while ((CLK_REF_SELECTED & (1UL << 2)) == 0UL) { }

    /* 4. Configure PLL_SYS:
     *    RP2350: 12 MHz * 125 = 1500 MHz VCO / (5 * 2) = 150 MHz
     *    RP2040: 12 MHz * 125 = 1500 MHz VCO / (6 * 2) = 125 MHz */
    reset_and_unreset(RESET_PLL_SYS_BIT);
    PLL_SYS_CS        = 1UL;   /* REFDIV = 1 */
    PLL_SYS_FBDIV_INT = 125UL; /* VCO = 1500 MHz */
    PLL_SYS_PWR       = 0x20UL;/* Power up VCO & main (PD=0, VCOPD=0, POSTDIVPD=1) */
    while ((PLL_SYS_CS & PLL_CS_LOCK) == 0UL) { }

#if defined(PICO_BOARD_RP2040)
    PLL_SYS_PRIM = (6UL << 16) | (2UL << 12); /* 125 MHz */
#else
    PLL_SYS_PRIM = (5UL << 16) | (2UL << 12); /* 150 MHz */
#endif
    PLL_SYS_PWR = 0UL; /* Power up post-dividers */

    /* 5. Switch CLK_SYS to AUX (PLL_SYS) */
    CLK_SYS_CTRL = 1UL;
    while ((CLK_SYS_SELECTED & (1UL << 1)) == 0UL) { }

    /* 6. Enable CLK_PERI from CLK_SYS (drives UART0 & SPI1) */
    CLK_PERI_CTRL = (1UL << 11);

    /* 7. Enable 1 MHz tick generator for TIMER0 & SysTick */
#if defined(PICO_BOARD_RP2040)
    WATCHDOG_TICK = (1UL << 9) | 12UL;
#else
    /* RP2350 G1 Safeguard: enable TICKS block for TIMER0, PROC0, WATCHDOG */
    TICKS_TIMER0_CYCLES = 12UL;
    TICKS_TIMER0_CTRL   = 1UL;
    TICKS_PROC0_CYCLES  = 1UL;
    TICKS_PROC0_CTRL    = 1UL;
    TICKS_WD_CYCLES     = 12UL;
    TICKS_WD_CTRL       = 1UL;
#endif
}

static void uart0_init_115200(void)
{
    /* Reset UART0 cleanly (safe after pico_multi_booter handoff) */
    reset_and_unreset(RESET_UART0_BIT);

    UART0_CR = 0;
    /* BAUDDIV = CLK_PERI / (16 * 115200)
     *   150 MHz: 81.3802 -> IBRD=81, FBRD=24
     *   125 MHz: 67.8168 -> IBRD=67, FBRD=52 */
#if defined(PICO_BOARD_RP2040)
    UART0_IBRD = 67UL;
    UART0_FBRD = 52UL;
#else
    UART0_IBRD = 81UL;
    UART0_FBRD = 24UL;
#endif
    UART0_LCR_H = (3UL << 5) | (1UL << 4); /* 8N1 + FIFO enable */
    UART0_CR    = (1UL << 9) | (1UL << 8) | (1UL << 0); /* RXE | TXE | UARTEN */

    pico_gpio_set_func(PICOCALC_UART_TX, GPIO_FUNC_UART, 0);
    pico_gpio_set_func(PICOCALC_UART_RX, GPIO_FUNC_UART, 1);
}

void pico_hal_early_init(void)
{
    clocks_init_12mhz_pll();

    /* Battery-first peripheral gating: un-reset ONLY active hardware blocks */
    unreset_block(RESET_IO_BANK0_BIT | RESET_PADS_BANK0_BIT | RESET_TIMER_BIT);

    /* Cleanly reset DMA, SPI1 & I2C1 in case PicoCalc Bootloader left them active */
    reset_and_unreset(RESET_DMA_BIT | RESET_SPI1_BIT | RESET_I2C1_BIT);

    uart0_init_115200();
}

void pico_hal_uart_putc(char c)
{
    if (c == '\n') {
        while (UART0_FR & UART_FR_TXFF) { }
        UART0_DR = '\r';
    }
    while (UART0_FR & UART_FR_TXFF) { }
    UART0_DR = (uint32_t)(uint8_t)c;
}

void pico_hal_uart_puts(const char *s)
{
    if (!s) return;
    while (*s) pico_hal_uart_putc(*s++);
}

int pico_hal_uart_getc_nonblock(void)
{
    if (!(UART0_FR & UART_FR_RXFE)) {
        return (int)(UART0_DR & 0xFFUL);
    }
    return -1;
}

uint32_t pico_hal_sysclk_hz(void)         { return PICO_SYSCLK_HZ; }
uint32_t pico_hal_board_sram_bytes(void)  { return PICO_SRAM_BYTES; }
uint32_t pico_hal_board_flash_bytes(void) { return PICO_FLASH_BYTES; }

const char *pico_hal_board_name(void)
{
#if defined(PICO_BOARD_RP2040)
    return "PicoCalc RP2040 (Pico 1)";
#else
    return "PicoCalc RP2350 (Pico 2)";
#endif
}

const char *pico_hal_cpu_name(void)
{
#if defined(PICO_BOARD_RP2040)
    return "ARM Cortex-M0+ 125MHz";
#else
    return "ARM Cortex-M33 150MHz FPv5";
#endif
}

void pico_hal_reboot(int to_bootsel)
{
    if (to_bootsel) {
#if defined(PICO_BOARD_RP2040)
        /* RP2040 BootROM table lookup for 'UB' (reset_usb_boot) */
        typedef void *(*rom_lookup_fn)(uint16_t *table, uint32_t code);
        typedef void (*usb_boot_fn)(uint32_t gpio_mask, uint32_t disable_mask);
        uint16_t *func_table = (uint16_t *)(uintptr_t)(*(uint16_t *)0x00000014UL);
        rom_lookup_fn lookup = (rom_lookup_fn)(uintptr_t)(*(uint16_t *)0x00000018UL);
        usb_boot_fn fn = (usb_boot_fn)lookup(func_table, (uint32_t)'U' | ((uint32_t)'B' << 8));
        if (fn) fn(0, 0);
#else
        /* RP2350 BootROM lookup for 'RB' (rom_reboot, flags=0x0002 BOOTSEL) */
        typedef void *(*rom_lookup_fn)(uint32_t code, uint32_t mask);
        typedef int (*rom_reboot_fn)(uint32_t flags, uint32_t delay_ms,
                                     uint32_t p0, uint32_t p1);
        rom_lookup_fn lookup = (rom_lookup_fn)(uintptr_t)(*(uint16_t *)0x00000018UL);
        rom_reboot_fn fn = (rom_reboot_fn)lookup((uint32_t)'R' | ((uint32_t)'B' << 8), 0x0004u);
        if (fn) fn(0x0002UL, 10u, 0u, 0u);
#endif
    }
    /* Standard Watchdog / AIRCR reboot fallback */
    WATCHDOG_LOAD = 1000u;
    WATCHDOG_CTRL = (1UL << 30); /* ENABLE */
    CM_SCB_AIRCR  = CM_AIRCR_SYSRESET;
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
