/*
 * src/drivers/bcm283x/gpu/v3d_init.c — VideoCore VI (V3D 4.2) Hardware Initialization
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Implements:
 * 1. Mailbox power on and clock configuration (800 MHz) for V3D
 * 2. Hardware register identification and verification
 * 3. Graceful fallback for QEMU / non-V3D environments
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <drivers/broadcom/v3d_regs.h>
#include <drivers/broadcom/v3d.h>
#include <drivers/broadcom/v3d_qpu.h>
#include <btron/arm64_mem.h>
#include <btron/libc_shim.h>

extern uintptr_t g_mmio_base;
extern void uart_puts_raw(const char *s);
extern void fb_log(const char *fmt, ...);

#define MBOX_STATUS     0x18
#define MBOX_EMPTY      0x40000000
#define MBOX_FULL       0x80000000
#define MBOX_WRITE      0x20
#define MBOX_READ       0x00
#define MBOX_CH_PROP    8

static v3d_info_t s_v3d_info = {
    .ident0 = 0,
    .ident1 = 0,
    .ident2 = 0,
    .ident3 = 0,
    .clock_hz = 0,
    .qpu_count = 0,
    .initialized = false,
    .hardware_present = false,
};

static inline void v3d_write32(uint32_t offset, uint32_t val) {
    volatile uint32_t *reg = (volatile uint32_t *)(V3D_PHYS_BASE + offset);
    *reg = val;
    __asm__ volatile("dmb sy" : : : "memory");
}

static inline uint32_t v3d_read32(uint32_t offset) {
    volatile uint32_t *reg = (volatile uint32_t *)(V3D_PHYS_BASE + offset);
    uint32_t val = *reg;
    __asm__ volatile("dmb sy" : : : "memory");
    return val;
}

static int v3d_mbox_call(volatile uint32_t *buf, uint32_t len_words) {
    uintptr_t mbox_base = g_mmio_base + 0x0000B880UL;
    volatile uint32_t *status_reg = (volatile uint32_t *)(mbox_base + MBOX_STATUS);
    volatile uint32_t *write_reg  = (volatile uint32_t *)(mbox_base + MBOX_WRITE);
    volatile uint32_t *read_reg   = (volatile uint32_t *)(mbox_base + MBOX_READ);

    uint32_t mbox_addr = (uint32_t)(uintptr_t)buf;
    __asm__ volatile("dsb sy" : : : "memory");

    int to = 50000;
    while ((*status_reg & MBOX_FULL) && --to > 0) {
        __asm__ volatile("nop");
    }
    if (to <= 0) return -1;

    *write_reg = ((mbox_addr & 0xFFFFFFF0u) | MBOX_CH_PROP);

    to = 50000;
    while (--to > 0) {
        int inner = 50000;
        while ((*status_reg & MBOX_EMPTY) && --inner > 0) {
            __asm__ volatile("nop");
        }
        if (inner <= 0) return -2;

        uint32_t res = *read_reg;
        if ((res & 0xFu) == MBOX_CH_PROP) {
            break;
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
    return (buf[1] == 0x80000000u) ? 0 : -3;
}

int v3d_init(void) {
    if (s_v3d_info.initialized) {
        return s_v3d_info.hardware_present ? 0 : -1;
    }
    s_v3d_info.initialized = true;

    /* 1. Request VideoCore firmware to power on V3D and set clock to 800 MHz */
    volatile uint32_t *mbox = (volatile uint32_t *)BTRON_NOCACHE_MBOX_ARM;

    /* Tag 1: Set Power State (Device 5 = V3D, State 3 = ON + WAIT) */
    /* Tag 2: Set Clock Rate (Clock 5 = V3D, 800 MHz = 800000000 Hz) */
    /* Tag 3: Get Clock Rate */
    mbox[0] = 22 * sizeof(uint32_t);
    mbox[1] = 0;

    mbox[2] = RPI_MBOX_TAG_SET_POWER_STATE;
    mbox[3] = 8;
    mbox[4] = 8;
    mbox[5] = RPI_MBOX_DEVICE_V3D;
    mbox[6] = 3;

    mbox[7] = RPI_MBOX_TAG_SET_CLOCK_RATE;
    mbox[8] = 12;
    mbox[9] = 12;
    mbox[10] = RPI_MBOX_CLOCK_V3D;
    mbox[11] = 800000000u;
    mbox[12] = 0; /* skip setting turbo */

    mbox[13] = RPI_MBOX_TAG_GET_CLOCK_RATE;
    mbox[14] = 8;
    mbox[15] = 0;
    mbox[16] = RPI_MBOX_CLOCK_V3D;
    mbox[17] = 0;

    mbox[18] = 0; /* end tag */

    int rc = v3d_mbox_call(mbox, 19);
    if (rc == 0) {
        s_v3d_info.clock_hz = mbox[17];
    } else {
        s_v3d_info.clock_hz = 0;
    }

    /* 2. Probe V3D Hardware Registers */
    uint32_t id0 = v3d_read32(V3D_IDENT0);
    uint32_t tech = (id0 >> 24) & 0xFFu;

    if (tech == 0x04) {
        /* V3D 4.2 Hardware Present! */
        s_v3d_info.ident0 = id0;
        s_v3d_info.ident1 = v3d_read32(V3D_IDENT1);
        s_v3d_info.ident2 = v3d_read32(V3D_IDENT2);
        s_v3d_info.ident3 = v3d_read32(V3D_IDENT3);

        uint32_t slices = (s_v3d_info.ident1 >> 8) & 0x0Fu;
        uint32_t qpus_per_slice = s_v3d_info.ident1 & 0x0Fu;
        if (slices == 0) slices = 2;
        if (qpus_per_slice == 0) qpus_per_slice = 4;
        s_v3d_info.qpu_count = slices * qpus_per_slice;
        s_v3d_info.hardware_present = true;

        /* Reset L2 Cache and Control Lists */
        v3d_write32(V3D_L2CACTL, 0x04u);
        v3d_write32(V3D_SLCACTL, 0x04u);
        v3d_write32(V3D_CLE_CT0CS, V3D_CLE_CTnCS_RESET);
        v3d_write32(V3D_CLE_CT1CS, V3D_CLE_CTnCS_RESET);

        v3d_qpu_init();

        uart_puts_raw("[V3D] VideoCore VI (V3D 4.2) hardware active!\n");
        return 0;
    }

    /* Hardware not present or running under QEMU raspi4b emulator */
    s_v3d_info.hardware_present = false;
    uart_puts_raw("[V3D] Hardware 3D core not detected; using TinyGL software rasterizer.\n");
    return -1;
}

bool v3d_is_available(void) {
    if (!s_v3d_info.initialized) {
        v3d_init();
    }
    return s_v3d_info.hardware_present;
}

const v3d_info_t *v3d_get_info(void) {
    if (!s_v3d_info.initialized) {
        v3d_init();
    }
    return &s_v3d_info;
}

