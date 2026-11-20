/*
 * i2ckbd_stm32.c — STM32 Southbridge Keyboard / PMU / Backlight Driver (0x1F)
 *
 * Verified against clockworkpi/PicoCalc (Code/pico_multi_booter/sd_boot/i2ckbd):
 *   - I2C1: GP6 SDA, GP7 SCL (internal pull-ups, 10 kHz bus speed)
 *   - 7-bit address: 0x1F
 *   - Registers:
 *       0x05 (REG_ID_BKL): LCD backlight (write 0x85, level)
 *       0x09 (REG_ID_FIF): Keyboard FIFO (write 0x09, read 2B u16)
 *       0x0A (REG_ID_BK2): Keyboard backlight (write 0x8A, level)
 *       0x0B (REG_ID_BAT): Battery % (write 0x0B, read 2B u16)
 *       0x0E (REG_ID_OFF): Power-off delay seconds (write 0x8E, secs)
 *   - G3 Safeguard: 9-clock bit-bang SDA unstick if GP6 is held LOW on boot.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "pico_regs.h"

#define STM32_I2C_ADDR      0x1Fu
#define REG_ID_BKL          0x05u
#define REG_ID_FIF          0x09u
#define REG_ID_BK2          0x0Au
#define REG_ID_BAT          0x0Bu
#define REG_ID_OFF          0x0Eu
#define REG_WRITE_BIT       0x80u

static int s_i2c_ready = 0;
static int s_ctrl_held = 0;

/* G3 Safeguard: release stuck-low SDA by clocking 9 SCL pulses + STOP */
static void i2c1_bus_recover(void)
{
    pico_gpio_set_func(PICOCALC_I2C_SDA, GPIO_FUNC_SIO, 1);
    SIO_GPIO_OE_CLR = (1UL << PICOCALC_I2C_SDA);
    pico_delay_us(20);
    if (pico_gpio_get(PICOCALC_I2C_SDA) == 0) {
        pico_gpio_init_out(PICOCALC_I2C_SCL, 1);
        for (int i = 0; i < 9; i++) {
            pico_gpio_put(PICOCALC_I2C_SCL, 0);
            pico_delay_us(50);
            pico_gpio_put(PICOCALC_I2C_SCL, 1);
            pico_delay_us(50);
        }
        /* Generate STOP: SDA low -> SCL high -> SDA high */
        pico_gpio_init_out(PICOCALC_I2C_SDA, 0);
        pico_delay_us(50);
        pico_gpio_put(PICOCALC_I2C_SCL, 1);
        pico_delay_us(50);
        pico_gpio_put(PICOCALC_I2C_SDA, 1);
        pico_delay_us(50);
    }
}

void i2ckbd_init(void)
{
    uint32_t period;

    i2c1_bus_recover();

    pico_gpio_set_func(PICOCALC_I2C_SDA, GPIO_FUNC_I2C, 1);
    pico_gpio_set_func(PICOCALC_I2C_SCL, GPIO_FUNC_I2C, 1);

    /* Disable DW_apb_i2c to configure */
    I2C1_ENABLE = 0;
    while (I2C1_ENABLE_STATUS & 1UL) { }

    /* Master mode, 7-bit addr, Standard speed (10 kHz), restart enable */
    I2C1_CON = (1UL << 0) | (1UL << 1) | (1UL << 5) | (1UL << 6);
    I2C1_TAR = STM32_I2C_ADDR;

    /* 10 kHz SCL period = PICO_SYSCLK_HZ / 10000 */
    period = PICO_SYSCLK_HZ / 10000UL;
    I2C1_SS_SCL_HCNT = period / 2UL;
    I2C1_SS_SCL_LCNT = period - (period / 2UL);

    (void)I2C1_CLR_INTR;
    I2C1_ENABLE = 1;
    while (!(I2C1_ENABLE_STATUS & 1UL)) { }
    s_i2c_ready = 1;
}

static int i2c1_write_bytes(const uint8_t *buf, int len)
{
    uint32_t t0 = TIMER0_TIMERAWL;
    if (!s_i2c_ready || len <= 0) return -1;
    (void)I2C1_CLR_TX_ABRT;

    for (int i = 0; i < len; i++) {
        uint32_t cmd = buf[i];
        if (i == len - 1) cmd |= (1UL << 9); /* STOP */
        while (!(I2C1_STATUS & (1UL << 1))) { /* TFNF */
            if ((uint32_t)(TIMER0_TIMERAWL - t0) > 20000u) return -1;
        }
        I2C1_DATA_CMD = cmd;
    }
    /* Wait for TX empty + bus idle */
    while (!(I2C1_STATUS & (1UL << 2)) || (I2C1_STATUS & (1UL << 5))) {
        if (I2C1_RAW_INTR_STAT & (1UL << 6)) { /* TX_ABRT */
            (void)I2C1_CLR_TX_ABRT;
            return -1;
        }
        if ((uint32_t)(TIMER0_TIMERAWL - t0) > 25000u) return -1;
    }
    return 0;
}

static int i2c1_read_u16(uint8_t reg, uint16_t *out)
{
    uint32_t t0;
    uint8_t b0, b1;
    if (!s_i2c_ready || !out) return -1;
    if (i2c1_write_bytes(&reg, 1) < 0) return -1;

    pico_delay_us(1500u); /* STM32 southbridge response window */
    (void)I2C1_CLR_TX_ABRT;

    I2C1_DATA_CMD = (1UL << 8);                 /* READ byte 0 */
    I2C1_DATA_CMD = (1UL << 8) | (1UL << 9);    /* READ byte 1 + STOP */

    t0 = TIMER0_TIMERAWL;
    while (I2C1_RXFLR < 2UL) {
        if (I2C1_RAW_INTR_STAT & (1UL << 6)) {
            (void)I2C1_CLR_TX_ABRT;
            return -1;
        }
        if ((uint32_t)(TIMER0_TIMERAWL - t0) > 25000u) return -1;
    }
    b0 = (uint8_t)(I2C1_DATA_CMD & 0xFFUL);
    b1 = (uint8_t)(I2C1_DATA_CMD & 0xFFUL);
    *out = (uint16_t)b0 | ((uint16_t)b1 << 8);
    return 0;
}

int i2ckbd_poll_key(void)
{
    uint16_t raw = 0;
    if (i2c1_read_u16(REG_ID_FIF, &raw) < 0 || raw == 0) return -1;
    if (raw == 0x7E03u) { s_ctrl_held = 0; return -1; }
    if (raw == 0x7E02u) { s_ctrl_held = 1; return -1; }
    if ((raw & 0xFFu) == 1u) { /* Key pressed */
        int c = (int)(raw >> 8);
        if (s_ctrl_held && c >= 'a' && c <= 'z') c = c - 'a' + 1;
        return c;
    }
    return -1;
}

int i2ckbd_read_battery(void)
{
    uint16_t raw = 0;
    int pct;
    if (i2c1_read_u16(REG_ID_BAT, &raw) < 0) return -1;
    pct = (int)((raw >> 8) & 0x7Fu);
    if (pct > 100) pct = 100;
    return pct;
}

void i2ckbd_set_lcd_backlight(uint8_t level)
{
    uint8_t msg[2] = { (uint8_t)(REG_ID_BKL | REG_WRITE_BIT), level };
    (void)i2c1_write_bytes(msg, 2);
}

void i2ckbd_set_kbd_backlight(uint8_t level)
{
    uint8_t msg[2] = { (uint8_t)(REG_ID_BK2 | REG_WRITE_BIT), level };
    (void)i2c1_write_bytes(msg, 2);
}

void i2ckbd_power_off(void)
{
    uint8_t msg[2] = { (uint8_t)(REG_ID_OFF | REG_WRITE_BIT), 1u };
    (void)i2c1_write_bytes(msg, 2);
}
