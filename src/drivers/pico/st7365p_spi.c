/*
 * st7365p_spi.c — Sitronix ST7365P 320x320 IPS SPI1 Driver for PicoCalc
 *
 * Verified against clockworkpi/PicoCalc (Code/pico_multi_booter/sd_boot/lcdspi)
 * and ST7365P_SPEC_V1.0.pdf:
 *   - SPI1 @ 25 MHz (Mode 0): GP10 SCK, GP11 TX, GP12 RX, GP13 CS, GP14 DC, GP15 RST
 *   - MADCTL (0x36) = 0x48 (MX | BGR), INVON (0x21) for IPS panel
 *   - COLMOD (0x3A) = 0x55 (RGB565 16-bpp) with PICOCALC_LCD_RGB666=1 fallback (0x66)
 *   - DMA Channel 0 streaming for 320x16 ping-pong bands (0% CPU stall)
 *   - Battery sleep: DISPOFF (0x28) + SLPIN (0x10) / SLPOUT (0x11) + DISPON (0x29)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "pico_hal.h"
#include "pico_regs.h"

#define ST_SLPIN    0x10u
#define ST_SLPOUT   0x11u
#define ST_INVON    0x21u
#define ST_DISPOFF  0x28u
#define ST_DISPON   0x29u
#define ST_CASET    0x2Au
#define ST_RASET    0x2Bu
#define ST_RAMWR    0x2Cu
#define ST_MADCTL   0x36u
#define ST_COLMOD   0x3Au

static volatile int s_dma_busy = 0;
/* One slice of work while the busy bit is set. It must return without
 * touching SPI, DMA, or GPIO. A null slice leaves the wait as a spin. */
static void (*s_wait_slice)(void);

void st7365p_set_wait_slice(void (*fn)(void))
{
    s_wait_slice = fn;
}

static void spi1_wait_idle(void)
{
    while (SPI1_SR & SPI_SR_BSY) { }
}

static void spi1_tx_byte(uint8_t b)
{
    while (!(SPI1_SR & SPI_SR_TNF)) { }
    SPI1_DR = b;
}

#if defined(PICO_BOARD_RP2040)
#define DMA_CTRL_BUSY_BIT   (1UL << 24)
#define DMA_CTRL_TREQ_SHIFT 15u
#else
#define DMA_CTRL_BUSY_BIT   (1UL << 26) /* RP2350 Datasheet Table 1244 */
#define DMA_CTRL_TREQ_SHIFT 17u
#endif

void st7365p_dma_wait(void)
{
    if (!s_dma_busy) return;
    while (DMA_CH0_CTRL_TRIG & DMA_CTRL_BUSY_BIT) {
        void (*fn)(void) = s_wait_slice;
        if (fn) fn();
    }
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_CS, 1);
    s_dma_busy = 0;
}

static void lcd_cmd(uint8_t cmd)
{
    st7365p_dma_wait();
    pico_gpio_put(PICOCALC_LCD_DC, 0);
    pico_gpio_put(PICOCALC_LCD_CS, 0);
    spi1_tx_byte(cmd);
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_CS, 1);
}

static void lcd_data(uint8_t d)
{
    pico_gpio_put(PICOCALC_LCD_DC, 1);
    pico_gpio_put(PICOCALC_LCD_CS, 0);
    spi1_tx_byte(d);
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_CS, 1);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, int len)
{
    st7365p_dma_wait();
    pico_gpio_put(PICOCALC_LCD_DC, 0);
    pico_gpio_put(PICOCALC_LCD_CS, 0);
    spi1_tx_byte(cmd);
    spi1_wait_idle();
    if (len > 0 && data) {
        pico_gpio_put(PICOCALC_LCD_DC, 1);
        for (int i = 0; i < len; i++) spi1_tx_byte(data[i]);
        spi1_wait_idle();
    }
    pico_gpio_put(PICOCALC_LCD_CS, 1);
}

void st7365p_init(void)
{
    /* 1. Configure GPIO pins (RP2350 ISO=0 cleared inside pico_gpio_*) */
    pico_gpio_set_func(PICOCALC_LCD_SCK, GPIO_FUNC_SPI, 0);
    pico_gpio_set_func(PICOCALC_LCD_TX,  GPIO_FUNC_SPI, 0);
    pico_gpio_set_func(PICOCALC_LCD_RX,  GPIO_FUNC_SPI, 0);
    pico_gpio_init_out(PICOCALC_LCD_CS,  1);
    pico_gpio_init_out(PICOCALC_LCD_DC,  1);
    pico_gpio_init_out(PICOCALC_LCD_RST, 1);

    /* 2. Configure PL022 SPI1 @ ~31.25-37.5 MHz (Mode 0, 8-bit)
     *    RP2350 (150 MHz): CPSR=2, SCR=1 -> 150 / (2 * 2) = 37.5 MHz
     *    RP2040 (125 MHz): CPSR=2, SCR=1 -> 125 / (2 * 2) = 31.25 MHz */
    SPI1_CR1   = 0;
    SPI1_CPSR  = 2UL;
    SPI1_CR0   = (1UL << 8) | 0x07UL; /* SCR=1, DSS=8-bit, CPOL=0, CPHA=0 */
    SPI1_DMACR = (1UL << 1);          /* TXDMAE */
    SPI1_CR1   = (1UL << 1);          /* SSE = 1 */

    /* 3. Hardware Reset Pulse (verified from clockworkpi/PicoCalc lcdspi.c) */
    pico_gpio_put(PICOCALC_LCD_RST, 1);
    pico_delay_ms(10);
    pico_gpio_put(PICOCALC_LCD_RST, 0);
    pico_delay_ms(15);
    pico_gpio_put(PICOCALC_LCD_RST, 1);
    pico_delay_ms(120);

    /* 4. Verified PicoCalc ST7365P Init Sequence */
    static const uint8_t pgam[] = {
        0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
        0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F
    };
    static const uint8_t ngam[] = {
        0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
        0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F
    };
    static const uint8_t pwr1[] = { 0x17, 0x15 };
    static const uint8_t vcom[] = { 0x00, 0x12, 0x80 };
    static const uint8_t dfc[]  = { 0x02, 0x02, 0x3B };
    static const uint8_t adj3[] = { 0xA9, 0x51, 0x2C, 0x82 };

    lcd_cmd_data(0xE0, pgam, (int)sizeof(pgam));
    lcd_cmd_data(0xE1, ngam, (int)sizeof(ngam));
    lcd_cmd_data(0xC0, pwr1, (int)sizeof(pwr1));
    lcd_cmd(0xC1); lcd_data(0x41);
    lcd_cmd_data(0xC5, vcom, (int)sizeof(vcom));
    lcd_cmd(ST_MADCTL); lcd_data(0x48); /* MX | BGR (PicoCalc upright) */
#if defined(PICOCALC_LCD_RGB666)
    lcd_cmd(ST_COLMOD); lcd_data(0x66); /* 18-bit RGB666 fallback */
#else
    lcd_cmd(ST_COLMOD); lcd_data(0x55); /* 16-bit RGB565 (fast 2B/px) */
#endif
    lcd_cmd(0xB0); lcd_data(0x00);
    lcd_cmd(0xB1); lcd_data(0xA0);
    lcd_cmd(ST_INVON);                  /* IPS panel requires inversion ON */
    lcd_cmd(0xB4); lcd_data(0x02);
    lcd_cmd_data(0xB6, dfc, (int)sizeof(dfc));
    lcd_cmd(0xB7); lcd_data(0xC6);
    lcd_cmd(0xE9); lcd_data(0x00);
    lcd_cmd_data(0xF7, adj3, (int)sizeof(adj3));

    lcd_cmd(ST_SLPOUT);
    pico_delay_ms(120);
    lcd_cmd(ST_DISPON);
    pico_delay_ms(50);
}

static void st7365p_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t raset[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    lcd_cmd_data(ST_CASET, caset, 4);
    lcd_cmd_data(ST_RASET, raset, 4);
}

void st7365p_flush_band_dma(uint16_t y0, uint16_t h, const uint16_t *rgb565_be)
{
    uint32_t nbytes = (uint32_t)320u * (uint32_t)h * 2u;

    /* The caller paints the idle strip first. This wait is the previous band.
     * SPI is one bus, so the window cannot be programmed while pixels clock. */
    st7365p_dma_wait();
    st7365p_set_window(0, y0, 319, (uint16_t)(y0 + h - 1u));

    pico_gpio_put(PICOCALC_LCD_DC, 0);
    pico_gpio_put(PICOCALC_LCD_CS, 0);
    spi1_tx_byte(ST_RAMWR);
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_DC, 1);

#if defined(PICOCALC_LCD_RGB666)
    /* Blocking 3-byte RGB666 conversion fallback */
    const uint8_t *p = (const uint8_t *)rgb565_be;
    for (uint32_t i = 0; i < (uint32_t)320u * (uint32_t)h; i++) {
        uint16_t c = ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        spi1_tx_byte((uint8_t)((c >> 8) & 0xF8u));
        spi1_tx_byte((uint8_t)((c >> 3) & 0xFCu));
        spi1_tx_byte((uint8_t)((c << 3) & 0xF8u));
    }
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_CS, 1);
#else
    /* Fire DMA Channel 0: byte transfers, INCR_READ=1, INCR_WRITE=0, DREQ=SPI1_TX */
    DMA_CH0_READ_ADDR   = (uint32_t)(uintptr_t)rgb565_be;
    DMA_CH0_WRITE_ADDR  = (uint32_t)(uintptr_t)&SPI1_DR;
    DMA_CH0_TRANS_COUNT = nbytes;
    s_dma_busy = 1;
    DMA_CH0_CTRL_TRIG   = (1UL << 0)              /* EN             */
                        | (0UL << 2)              /* DATA_SIZE=8bit */
                        | (1UL << 4)              /* INCR_READ      */
                        | ((uint32_t)DREQ_SPI1_TX << DMA_CTRL_TREQ_SHIFT);
#endif
}

void st7365p_sleep(int sleep_on)
{
    st7365p_dma_wait();
    if (sleep_on) {
        lcd_cmd(ST_DISPOFF);
        lcd_cmd(ST_SLPIN);
    } else {
        lcd_cmd(ST_SLPOUT);
        pico_delay_ms(120);
        lcd_cmd(ST_DISPON);
    }
}

/* Compact 3x5 bitmap glyphs for 0-9, A-F, space, '=' (18 glyphs, 54 B Flash) */
static const uint8_t s_hex_font[18][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, /* 0..3 */
    {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2}, /* 4..7 */
    {7,5,7,5,7}, {7,5,7,1,7}, {2,5,7,5,5}, {6,5,6,5,6}, /* 8..B */
    {7,4,4,4,7}, {6,5,5,5,6}, {7,4,7,4,7}, {7,4,7,4,4}, /* C..F */
    {0,0,0,0,0}, {0,7,0,7,0}                            /* ' ', '=' */
};

static int hex_glyph_idx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c == '=') return 17;
    return 16;
}

static void fmt_hex8(char *dst, uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        dst[i] = hx[v & 0xFu];
        v >>= 4;
    }
}

/* G4 Safeguard: Raw blocking red fault banner with hex PC & LR (zero DMA/RTOS) */
void pico_hal_panic_display(const char *reason, uint32_t pc,
                            uint32_t lr, uint32_t psr, uint32_t cfsr)
{
    char msg[28];
    (void)reason; (void)psr; (void)cfsr;
    s_dma_busy = 0;

    /* Format: "PC=XXXXXXXX LR=XXXXXXXX" (23 chars * 12px = 276px <= 320px) */
    msg[0] = 'F'; msg[1] = ' ';
    msg[2] = 'C'; msg[3] = '=';
    fmt_hex8(&msg[4], pc);
    msg[12] = ' ';
    msg[13] = 'B'; msg[14] = '=';
    fmt_hex8(&msg[15], lr);
    msg[23] = '\0';

    st7365p_set_window(0, 0, 319, 31);
    pico_gpio_put(PICOCALC_LCD_DC, 0);
    pico_gpio_put(PICOCALC_LCD_CS, 0);
    spi1_tx_byte(ST_RAMWR);
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_DC, 1);

    /* Render 320x32 red banner with 3x-scaled white hex PC & LR (rows 8..22) */
    for (int y = 0; y < 32; y++) {
        int gy = (y >= 8 && y < 23) ? ((y - 8) / 3) : -1;
        for (int x = 0; x < 320; x++) {
            int lit = 0;
            if (gy >= 0 && x >= 16) {
                int rel = x - 16;
                int ch_i = rel / 12;
                int gx = (rel % 12) / 3;
                if (ch_i < 23 && gx < 3) {
                    uint8_t row = s_hex_font[hex_glyph_idx(msg[ch_i])][gy];
                    if (row & (1u << (2 - gx))) lit = 1;
                }
            }
            if (lit) {
                spi1_tx_byte(0xFFu);
                spi1_tx_byte(0xFFu);
            } else {
                spi1_tx_byte(0xD1u);
                spi1_tx_byte(0xC5u);
            }
        }
    }
    spi1_wait_idle();
    pico_gpio_put(PICOCALC_LCD_CS, 1);
}
