/*
 * pico_regs.h — Clean-Room RP2350 (Default) & RP2040 MMIO Register Definitions
 *
 * Spec references:
 *   - RP2350 Datasheet (Raspberry Pi Ltd.): Sections 2.2, 8.1, 8.5, 9.10, 12.1-12.3
 *   - RP2040 Datasheet (Raspberry Pi Ltd.): Sections 2.2, 2.14, 2.15, 2.19, 4.2-4.4
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_PICO_REGS_H_
#define _BTRON_PICO_REGS_H_

#include <stdint.h>

#define REG32(addr)         (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG32_SET(addr)     (*(volatile uint32_t *)(uintptr_t)((addr) + 0x2000UL))
#define REG32_CLR(addr)     (*(volatile uint32_t *)(uintptr_t)((addr) + 0x3000UL))

#if defined(PICO_BOARD_RP2040)
/* ── RP2040 Peripheral Bases ───────────────────────────────────────────── */
#define CLOCKS_BASE         0x40008000UL
#define RESETS_BASE         0x4000C000UL
#define IO_BANK0_BASE       0x40014000UL
#define PADS_BANK0_BASE     0x4001C000UL
#define XOSC_BASE           0x40024000UL
#define PLL_SYS_BASE        0x40028000UL
#define UART0_BASE          0x40034000UL
#define SPI0_BASE           0x4003C000UL
#define SPI1_BASE           0x40040000UL
#define I2C0_BASE           0x40044000UL
#define I2C1_BASE           0x40048000UL
#define TIMER0_BASE         0x40054000UL
#define WATCHDOG_BASE       0x40058000UL

/* RP2040 RESETS bit positions */
#define RESET_DMA_BIT       (1UL << 2)
#define RESET_I2C1_BIT      (1UL << 4)
#define RESET_IO_BANK0_BIT  (1UL << 5)
#define RESET_PADS_BANK0_BIT (1UL << 8)
#define RESET_PLL_SYS_BIT   (1UL << 12)
#define RESET_SPI1_BIT      (1UL << 17)
#define RESET_TIMER_BIT     (1UL << 21)
#define RESET_UART0_BIT     (1UL << 22)

#define PICO_SYSCLK_HZ      125000000UL
#define PICO_SRAM_BYTES     (264UL * 1024UL)
#define PICO_FLASH_BYTES    (2048UL * 1024UL)
#define GPIO_FUNC_SPI       1u
#define GPIO_FUNC_UART      2u
#define GPIO_FUNC_I2C       3u
#define GPIO_FUNC_SIO       5u

#else
/* ── RP2350 Peripheral Bases (Default: Pico 2) ─────────────────────────── */
#define CLOCKS_BASE         0x40010000UL
#define RESETS_BASE         0x40020000UL
#define IO_BANK0_BASE       0x40028000UL
#define PADS_BANK0_BASE     0x40038000UL
#define XOSC_BASE           0x40048000UL
#define PLL_SYS_BASE        0x40050000UL
#define UART0_BASE          0x40070000UL
#define SPI0_BASE           0x40080000UL
#define SPI1_BASE           0x40088000UL
#define I2C0_BASE           0x40090000UL
#define I2C1_BASE           0x40098000UL
#define TIMER0_BASE         0x400B0000UL
#define WATCHDOG_BASE       0x400D8000UL
#define TICKS_BASE          0x40108000UL /* RP2350 dedicated tick generator */

/* RP2350 RESETS bit positions (RP2350 Datasheet Table 579) */
#define RESET_DMA_BIT       (1UL << 2)
#define RESET_I2C1_BIT      (1UL << 5)
#define RESET_IO_BANK0_BIT  (1UL << 6)
#define RESET_PADS_BANK0_BIT (1UL << 9)
#define RESET_PLL_SYS_BIT   (1UL << 14)
#define RESET_SPI1_BIT      (1UL << 19)
#define RESET_TIMER_BIT     (1UL << 23)
#define RESET_UART0_BIT     (1UL << 26)

/* RP2350 TICKS registers (Section 8.5.3) */
#define TICKS_PROC0_CTRL    REG32(TICKS_BASE + 0x00UL)
#define TICKS_PROC0_CYCLES  REG32(TICKS_BASE + 0x04UL)
#define TICKS_TIMER0_CTRL   REG32(TICKS_BASE + 0x18UL)
#define TICKS_TIMER0_CYCLES REG32(TICKS_BASE + 0x1CUL)
#define TICKS_WD_CTRL       REG32(TICKS_BASE + 0x30UL)
#define TICKS_WD_CYCLES     REG32(TICKS_BASE + 0x34UL)

#define PICO_SYSCLK_HZ      150000000UL
#define PICO_SRAM_BYTES     (520UL * 1024UL)
#define PICO_FLASH_BYTES    (4096UL * 1024UL)
#define GPIO_FUNC_SPI       1u
#define GPIO_FUNC_UART      2u
#define GPIO_FUNC_I2C       3u
#define GPIO_FUNC_SIO       5u
#endif

/* Shared Bases (Identical on RP2350 & RP2040) */
#define DMA_BASE            0x50000000UL
#define SIO_BASE            0xD0000000UL

/* ── RESETS ────────────────────────────────────────────────────────────── */
#define RESETS_RESET        (RESETS_BASE + 0x00UL)
#define RESETS_RESET_DONE   (RESETS_BASE + 0x08UL)

/* ── XOSC (12 MHz Crystal) ─────────────────────────────────────────────── */
#define XOSC_CTRL           REG32(XOSC_BASE + 0x00UL)
#define XOSC_STATUS         REG32(XOSC_BASE + 0x04UL)
#define XOSC_STARTUP        REG32(XOSC_BASE + 0x0CUL)
#define XOSC_CTRL_ENABLE    0x00FABAA0UL
#define XOSC_STATUS_STABLE  (1UL << 31)

/* ── PLL_SYS ───────────────────────────────────────────────────────────── */
#define PLL_SYS_CS          REG32(PLL_SYS_BASE + 0x00UL)
#define PLL_SYS_PWR         REG32(PLL_SYS_BASE + 0x04UL)
#define PLL_SYS_FBDIV_INT   REG32(PLL_SYS_BASE + 0x08UL)
#define PLL_SYS_PRIM        REG32(PLL_SYS_BASE + 0x0CUL)
#define PLL_CS_LOCK         (1UL << 31)

/* ── CLOCKS ────────────────────────────────────────────────────────────── */
#define CLK_REF_CTRL        REG32(CLOCKS_BASE + 0x30UL)
#define CLK_REF_SELECTED    REG32(CLOCKS_BASE + 0x38UL)
#define CLK_SYS_CTRL        REG32(CLOCKS_BASE + 0x3CUL)
#define CLK_SYS_SELECTED    REG32(CLOCKS_BASE + 0x44UL)
#define CLK_PERI_CTRL       REG32(CLOCKS_BASE + 0x48UL)

/* ── IO_BANK0 & PADS_BANK0 ─────────────────────────────────────────────── */
#define IO_GPIO_CTRL(pin)   REG32(IO_BANK0_BASE + 0x04UL + (uint32_t)(pin) * 8UL)
#define PAD_GPIO(pin)       REG32(PADS_BANK0_BASE + 0x04UL + (uint32_t)(pin) * 4UL)
#define PAD_IE              (1UL << 6)
#define PAD_OD              (1UL << 7)
#define PAD_PUE             (1UL << 3)
#define PAD_PDE             (1UL << 2)
#define PAD_DRIVE_8MA       (2UL << 4)
#define PAD_ISO             (1UL << 8)  /* RP2350 pad isolation bit (MUST clear!) */

/* ── SIO GPIO (GP0..GP31) ──────────────────────────────────────────────── */
#define SIO_GPIO_IN         REG32(SIO_BASE + 0x004UL)
#define SIO_GPIO_OUT_SET    REG32(SIO_BASE + 0x014UL)
#define SIO_GPIO_OUT_CLR    REG32(SIO_BASE + 0x018UL)
#define SIO_GPIO_OE_SET     REG32(SIO_BASE + 0x024UL)
#define SIO_GPIO_OE_CLR     REG32(SIO_BASE + 0x028UL)

/* ── PL011 UART0 ───────────────────────────────────────────────────────── */
#define UART0_DR            REG32(UART0_BASE + 0x00UL)
#define UART0_FR            REG32(UART0_BASE + 0x18UL)
#define UART0_IBRD          REG32(UART0_BASE + 0x24UL)
#define UART0_FBRD          REG32(UART0_BASE + 0x28UL)
#define UART0_LCR_H         REG32(UART0_BASE + 0x2CUL)
#define UART0_CR            REG32(UART0_BASE + 0x30UL)
#define UART_FR_TXFF        (1UL << 5)
#define UART_FR_RXFE        (1UL << 4)

/* ── PL022 SPI1 (PicoCalc ST7365P Display) ─────────────────────────────── */
#define SPI1_CR0            REG32(SPI1_BASE + 0x00UL)
#define SPI1_CR1            REG32(SPI1_BASE + 0x04UL)
#define SPI1_DR             REG32(SPI1_BASE + 0x08UL)
#define SPI1_SR             REG32(SPI1_BASE + 0x0CUL)
#define SPI1_CPSR           REG32(SPI1_BASE + 0x10UL)
#define SPI1_DMACR          REG32(SPI1_BASE + 0x24UL)
#define SPI_SR_TNF          (1UL << 1)
#define SPI_SR_BSY          (1UL << 4)

/* ── DW_apb_i2c I2C1 (PicoCalc STM32 Keyboard/PMU 0x1F) ────────────────── */
#define I2C1_CON            REG32(I2C1_BASE + 0x00UL)
#define I2C1_TAR            REG32(I2C1_BASE + 0x04UL)
#define I2C1_DATA_CMD       REG32(I2C1_BASE + 0x10UL)
#define I2C1_SS_SCL_HCNT    REG32(I2C1_BASE + 0x14UL)
#define I2C1_SS_SCL_LCNT    REG32(I2C1_BASE + 0x18UL)
#define I2C1_CLR_INTR       REG32(I2C1_BASE + 0x40UL)
#define I2C1_CLR_TX_ABRT    REG32(I2C1_BASE + 0x54UL)
#define I2C1_ENABLE         REG32(I2C1_BASE + 0x6CUL)
#define I2C1_STATUS         REG32(I2C1_BASE + 0x70UL)
#define I2C1_TXFLR          REG32(I2C1_BASE + 0x74UL)
#define I2C1_RXFLR          REG32(I2C1_BASE + 0x78UL)
#define I2C1_RAW_INTR_STAT  REG32(I2C1_BASE + 0x34UL)
#define I2C1_ENABLE_STATUS  REG32(I2C1_BASE + 0x9CUL)

/* ── TIMER0 (1 MHz microsecond counter) ────────────────────────────────── */
#define TIMER0_TIMERAWL     REG32(TIMER0_BASE + 0x28UL)

/* ── DMA Channel 0 (SPI1 TX Strip Streaming) ───────────────────────────── */
#define DMA_CH0_READ_ADDR   REG32(DMA_BASE + 0x00UL)
#define DMA_CH0_WRITE_ADDR  REG32(DMA_BASE + 0x04UL)
#define DMA_CH0_TRANS_COUNT REG32(DMA_BASE + 0x08UL)
#define DMA_CH0_CTRL_TRIG   REG32(DMA_BASE + 0x0CUL)

#if defined(PICO_BOARD_RP2040)
#define DREQ_SPI1_TX        18u
#else
#define DREQ_SPI1_TX        26u /* RP2350 Datasheet Table 1236: SPI1_TX = 26 */
#endif

/* ── WATCHDOG ──────────────────────────────────────────────────────────── */
#define WATCHDOG_CTRL       REG32(WATCHDOG_BASE + 0x00UL)
#define WATCHDOG_LOAD       REG32(WATCHDOG_BASE + 0x04UL)
#define WATCHDOG_TICK       REG32(WATCHDOG_BASE + 0x2CUL)

/* ── PicoCalc Verified Pinout (clockworkpi/PicoCalc) ───────────────────── */
#define PICOCALC_UART_TX    0u
#define PICOCALC_UART_RX    1u
#define PICOCALC_I2C_SDA    6u
#define PICOCALC_I2C_SCL    7u
#define PICOCALC_LCD_SCK    10u
#define PICOCALC_LCD_TX     11u
#define PICOCALC_LCD_RX     12u
#define PICOCALC_LCD_CS     13u
#define PICOCALC_LCD_DC     14u
#define PICOCALC_LCD_RST    15u

/* HAL helper functions implemented by hal_rp2xxx.c */
void pico_gpio_set_func(uint32_t pin, uint32_t func, int pull_up);
void pico_gpio_init_out(uint32_t pin, int initial_high);
void pico_gpio_put(uint32_t pin, int high);
int  pico_gpio_get(uint32_t pin);
void pico_delay_us(uint32_t us);
void pico_delay_ms(uint32_t ms);

#endif /* _BTRON_PICO_REGS_H_ */
