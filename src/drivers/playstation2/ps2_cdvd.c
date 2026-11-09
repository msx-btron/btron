/*
 * ps2_cdvd.c — Cleanroom EE CDVD reader (NCMD ReadCd + IOP DMA ch3)
 *
 * Register map is the public IOP CD/DVD block at physical 0x1F402000,
 * reached from the EE through the uncached KSEG1 alias 0xBF402000 —
 * the same window the OHCI driver uses at 0xBF801600.  Data DMA is
 * IOP channel 3 into the last 32 KiB of IOP RAM (0x001F0000), which
 * sits above the USB shadow claimed from the bottom of the 2 MiB.
 *
 * Specification-derived (ps2tek CDVD NCMD / IOP DMA).  No vendor SDK.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_cdvd.h"
#include "ps2_iopram.h"
#include "ps2_irq.h"
#include "ps2_console.h"

#include <stdio.h>

extern uint32_t ps2_count_read(void);

#define CDVD_BASE       0xBF402000UL
#define CDVD8(off)      (*(volatile uint8_t *)(uintptr_t)(CDVD_BASE + (off)))

#define CDVD_NCMD       0x04u
#define CDVD_NSTAT      0x05u   /* write: param byte; read: NSTATUS */
#define CDVD_HOWTO      0x06u
#define CDVD_BREAK      0x07u
#define CDVD_ISTAT      0x08u
#define CDVD_STATUS     0x0Au
#define CDVD_DISKTYPE   0x0Fu

#define CDVD_NCMD_READCD  0x06u
#define CDVD_READY_MASK   0xC0u
#define CDVD_READY_VAL    0x40u
#define CDVD_HOWTO_2048   0x80u

#define CDVD_TYPE_NONE    0x00u
#define CDVD_TYPE_PS2_CD  0x12u
#define CDVD_TYPE_PS2_DVD 0x14u

#define IOP_DMA3_MADR   0xBF8010B0UL
#define IOP_DMA3_BCR    0xBF8010B4UL
#define IOP_DMA3_CHCR   0xBF8010B8UL
#define IOP_DPCR        0xBF8010F0UL
#define IOP32(addr)     (*(volatile uint32_t *)(uintptr_t)(addr))

/* Last 32 KiB of 2 MiB IOP RAM: 16 × 2048-byte sectors.  USB claims
 * 2 KiB from the bottom, so the two working sets do not overlap. */
#define CDVD_IOP_BUF    0x001F0000u
#define CDVD_CHUNK_SEC  16u
#define CDVD_CHCR_GO    0x41000200u
#define CDVD_DPCR_CH3   0x00008000u

#define SPINUP_US       8000000u   /* first seek after tray: ~333 ms typical */
#define CMD_US          2000000u

static int     s_inited;
static int     s_ready;
static uint8_t s_disk_type;
static int     s_count_runs = -1;

static int count_running(void)
{
    if (s_count_runs < 0) {
        uint32_t a = ps2_count_read();
        for (volatile int i = 0; i < 8000; i++) { }
        s_count_runs = (ps2_count_read() != a) ? 1 : 0;
    }
    return s_count_runs;
}

static void wait_us(uint32_t us)
{
    if (!count_running()) {
        for (volatile uint32_t i = 0; i < us * 50u; i++) {
            __asm__ volatile("nop");
        }
        return;
    }
    {
        uint32_t start = ps2_count_read();
        uint32_t budget = us * btron_ee_ticks_per_us();
        uint32_t spins = 0;
        while ((uint32_t)(ps2_count_read() - start) < budget) {
            if (++spins > 20000000u) break;
        }
    }
}

static int wait_nready(uint32_t timeout_us)
{
    uint32_t start;
    uint32_t budget;
    uint32_t spins = 0;

    if (!count_running()) {
        for (spins = 0; spins < timeout_us * 20u; spins++) {
            if ((CDVD8(CDVD_NSTAT) & CDVD_READY_MASK) == CDVD_READY_VAL)
                return 1;
            __asm__ volatile("nop");
        }
        return (CDVD8(CDVD_NSTAT) & CDVD_READY_MASK) == CDVD_READY_VAL;
    }

    start  = ps2_count_read();
    budget = timeout_us * btron_ee_ticks_per_us();
    for (;;) {
        if ((CDVD8(CDVD_NSTAT) & CDVD_READY_MASK) == CDVD_READY_VAL)
            return 1;
        if ((uint32_t)(ps2_count_read() - start) >= budget) return 0;
        if (++spins > 40000000u) return 0;
    }
}

static void dma3_enable(void)
{
    IOP32(IOP_DPCR) = IOP32(IOP_DPCR) | CDVD_DPCR_CH3;
}

static int dma3_wait(uint32_t timeout_us)
{
    uint32_t start;
    uint32_t budget;
    uint32_t spins = 0;

    if (!count_running()) {
        for (spins = 0; spins < timeout_us * 20u; spins++) {
            if ((IOP32(IOP_DMA3_CHCR) & 0x01000000u) == 0) return 1;
        }
        return (IOP32(IOP_DMA3_CHCR) & 0x01000000u) == 0;
    }
    start  = ps2_count_read();
    budget = timeout_us * btron_ee_ticks_per_us();
    for (;;) {
        if ((IOP32(IOP_DMA3_CHCR) & 0x01000000u) == 0) return 1;
        if ((uint32_t)(ps2_count_read() - start) >= budget) return 0;
        if (++spins > 40000000u) return 0;
    }
}

static void dma3_setup(uint32_t nsectors)
{
    uint32_t blkwords = 32u;                 /* 128-byte cells */
    uint32_t blkcount = 16u * nsectors;      /* 16 cells × 128 = 2048 */

    if (IOP32(IOP_DMA3_CHCR) & 0x01000000u)
        CDVD8(CDVD_BREAK) = 1;

    IOP32(IOP_DMA3_MADR) = CDVD_IOP_BUF;
    IOP32(IOP_DMA3_BCR)  = (blkcount << 16) | (blkwords & 0xFFFFu);
    IOP32(IOP_DMA3_CHCR) = CDVD_CHCR_GO;
}

static void send_readcd(uint32_t lba, uint32_t nsectors)
{
    uint8_t p[11];
    int i;

    p[0]  = (uint8_t)(lba);
    p[1]  = (uint8_t)(lba >> 8);
    p[2]  = (uint8_t)(lba >> 16);
    p[3]  = (uint8_t)(lba >> 24);
    p[4]  = (uint8_t)(nsectors);
    p[5]  = (uint8_t)(nsectors >> 8);
    p[6]  = (uint8_t)(nsectors >> 16);
    p[7]  = (uint8_t)(nsectors >> 24);
    p[8]  = 16;     /* try count */
    p[9]  = 0;      /* speed (drive default) */
    p[10] = 0;      /* 2048-byte user data */

    CDVD8(CDVD_HOWTO) = CDVD_HOWTO_2048;
    CDVD8(CDVD_ISTAT) = 0x1F;   /* ack pending */

    for (i = 0; i < 11; i++)
        CDVD8(CDVD_NSTAT) = p[i];
    CDVD8(CDVD_NCMD) = CDVD_NCMD_READCD;
}

static int read_chunk_to_iop(uint32_t lba, uint32_t nsectors)
{
    if (nsectors == 0 || nsectors > CDVD_CHUNK_SEC) return 0;
    if (!wait_nready(CMD_US)) return 0;

    dma3_enable();
    dma3_setup(nsectors);
    send_readcd(lba, nsectors);

    if (!dma3_wait(CMD_US)) return 0;
    if (!wait_nready(CMD_US)) return 0;
    return 1;
}

static void copy_iop(uint8_t *dst, uint32_t off, uint32_t n)
{
    volatile uint8_t *src;
    uint32_t i;

    src = (volatile uint8_t *)(uintptr_t)(PS2_IOP_KSEG1 + CDVD_IOP_BUF + off);
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

int ps2_cdvd_init(void)
{
    char msg[80];

    if (s_inited) return s_ready;
    s_inited = 1;

    dma3_enable();
    wait_us(1000);

    if (!wait_nready(SPINUP_US)) {
        uart_puts_raw("[CDVD] NSTATUS not ready (no disc or CDVDMAN busy)\n");
        s_ready = 0;
        return 0;
    }

    s_disk_type = CDVD8(CDVD_DISKTYPE);
    if (s_disk_type == CDVD_TYPE_NONE) {
        uart_puts_raw("[CDVD] tray empty\n");
        s_ready = 0;
        return 0;
    }

    s_ready = 1;
    snprintf(msg, sizeof(msg), "[CDVD] disk type 0x%02x, ReadCd 2048-byte\n",
             (unsigned)s_disk_type);
    uart_puts_raw(msg);
    (void)CDVD_TYPE_PS2_CD;
    (void)CDVD_TYPE_PS2_DVD;
    return 1;
}

uint8_t ps2_cdvd_disk_type(void)
{
    return s_disk_type;
}

int ps2_cdvd_ready(void)
{
    return s_ready;
}

int ps2_cdvd_read_sectors(uint32_t lba, uint32_t nsectors, void *dst)
{
    uint8_t *out = (uint8_t *)dst;

    if (!dst || nsectors == 0) return 0;
    if (!s_ready && !ps2_cdvd_init()) return 0;

    while (nsectors) {
        uint32_t n = nsectors > CDVD_CHUNK_SEC ? CDVD_CHUNK_SEC : nsectors;
        if (!read_chunk_to_iop(lba, n)) return 0;
        copy_iop(out, 0, n * PS2_ISO_SECTOR);
        lba      += n;
        nsectors -= n;
        out      += n * PS2_ISO_SECTOR;
    }
    return 1;
}

int ps2_cdvd_read_bytes(uint32_t start_lba, uint32_t offset,
                        uint32_t len, void *dst)
{
    uint8_t *out = (uint8_t *)dst;
    uint32_t remaining = len;
    uint32_t lba = start_lba + (offset / PS2_ISO_SECTOR);
    uint32_t skip = offset % PS2_ISO_SECTOR;

    if (!dst) return 0;
    if (len == 0) return 1;
    if (!s_ready && !ps2_cdvd_init()) return 0;

    while (remaining) {
        uint32_t need = remaining + skip;
        uint32_t nsec = (need + PS2_ISO_SECTOR - 1u) / PS2_ISO_SECTOR;
        uint32_t copy;

        if (nsec > CDVD_CHUNK_SEC) nsec = CDVD_CHUNK_SEC;
        if (!read_chunk_to_iop(lba, nsec)) return 0;

        copy = nsec * PS2_ISO_SECTOR - skip;
        if (copy > remaining) copy = remaining;
        copy_iop(out, skip, copy);
        out += copy;
        remaining -= copy;
        lba += nsec;
        skip = 0;
    }
    return 1;
}

int ps2_cdvd_iso_read(void *ctx, uint32_t lba, uint32_t nsectors, void *dst)
{
    (void)ctx;
    return ps2_cdvd_read_sectors(lba, nsectors, dst);
}
