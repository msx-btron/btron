/*
 * ps2_cdvd.c — EE CDVD sector reader
 *
 * Retail path: CDVDFSV handshake then NCMD READ.
 *   sceCdInit      0x80000592
 *   sceCdDiskReady 0x8000059A
 *   sceCdSearchFile 0x80000597
 *   sceCdRead      0x80000595 NCMD 1
 * EE MMIO at 0xBF402000 is IOP-private; it is the last-resort fallback.
 *
 * Protocol from public libcdvd-rpc.h.  No vendor C.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_cdvd.h"
#include "ps2_iopram.h"
#include "ps2_irq.h"
#include "ps2_console.h"
#include "ps2_sif.h"
#include "ps2_usb.h"

#include <libstr.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern uint32_t ps2_count_read();

#define CDVD_BASE       0xBF402000UL
#define CDVD8(off)      (*(volatile uint8_t *)(uintptr_t)(CDVD_BASE + (off)))

#define CDVD_NCMD       0x04u
#define CDVD_NSTAT      0x05u
#define CDVD_HOWTO      0x06u
#define CDVD_BREAK      0x07u
#define CDVD_ISTAT      0x08u
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

#define CDVD_IOP_BUF    0x001F0000u
#define CDVD_CHUNK_SEC  8u
#define CDVD_CHCR_GO    0x41000200u
#define CDVD_DPCR_CH3   0x00008000u

#define SPINUP_US       200000u
#define CMD_US          500000u

#define CDVD_RPC_INIT       0x80000592u
#define CDVD_RPC_SCMD       0x80000593u
#define CDVD_RPC_NCMD       0x80000595u
#define CDVD_RPC_SEARCH     0x80000597u
#define CDVD_RPC_DISKREADY  0x8000059Au
#define CDVD_NCMD_READ      1
#define CDVD_NCMD_DISKREADY 14
#define CDVD_NCMD_SEND      32
#define CDVD_SCMD_DISKTYPE  3
#define CDVD_SCMD_MMODE     34
#define CDVD_MMODE_CD       1
#define CDVD_MMODE_DVD      2
#define CDVD_INIT_INIT      0
#define CDVD_READY_WAIT     0
#define CDVD_COMPLETE       2
#define CDVD_NOTREADY       6
#define CDVD_SPIN_NOM       1u
#define CDVD_UNCACHED       0x20000000u

struct cdvd_ncmd_read {
    uint32_t lbn;
    uint32_t sectors;
    uint32_t paddr;
    uint32_t rmode;       /* trycount | spindlctrl<<8 | datapattern<<16 */
    uint32_t remnant;
    uint32_t readpos;
    uint32_t pad[2];      /* 32-byte SIF extra; 16-byte units */
};

struct cdvd_remnant {
    uint32_t size1;
    uint32_t size2;
    void    *dest1;
    void    *dest2;
    uint8_t  src1[64];
    uint8_t  src2[64];
} __attribute__((aligned(64)));

/* Old-library SearchFile packet (sceCdlFILE + path[256] + eedest). */
struct cdvd_cdlfile {
    uint32_t lsn;
    uint32_t size;
    char     name[16];
    uint8_t  date[8];
};

struct cdvd_search_pkt {
    struct cdvd_cdlfile fp;
    char     path[256];
    void    *eedest;
};

struct cdvd_init_recv {
    int32_t result;
    int32_t cdvdfsv_ver;
    int32_t cdvdman_ver;
    int32_t debug_mode;
};

typedef char cdvd_cdlfile_sz[(sizeof(struct cdvd_cdlfile) == 32) ? 1 : -1];
typedef char cdvd_search_sz[(sizeof(struct cdvd_search_pkt) == 292) ? 1 : -1];
typedef char cdvd_ncmd_sz[(sizeof(struct cdvd_ncmd_read) == 32) ? 1 : -1];

static int     s_inited;
static int     s_ready;
static int     s_rpc;
static int     s_search_ok;
static uint8_t s_disk_type;
static int     s_count_runs = -1;
static int32_t s_cdvdfsv_ver;

static ps2_sif_rpc_t          s_init;
static ps2_sif_rpc_t          s_scmd;
static ps2_sif_rpc_t          s_diskready;
static ps2_sif_rpc_t          s_search;
static ps2_sif_rpc_t          s_ncmd;
static int32_t                s_scmd_arg   __attribute__((aligned(64)));
static int32_t                s_scmd_recv[4] __attribute__((aligned(64)));
static int32_t                s_init_mode  __attribute__((aligned(64)));
static struct cdvd_init_recv  s_init_recv  __attribute__((aligned(64)));
static int32_t                s_dr_mode    __attribute__((aligned(64)));
static int32_t                s_dr_recv    __attribute__((aligned(64)));
static struct cdvd_search_pkt s_spkt       __attribute__((aligned(64)));
static int32_t                s_srecv      __attribute__((aligned(64)));
static struct cdvd_ncmd_read  s_narg       __attribute__((aligned(64)));
static struct cdvd_remnant    s_nrem       __attribute__((aligned(64)));
static uint32_t               s_npos       __attribute__((aligned(64)));
static int32_t                s_nrecv[4]   __attribute__((aligned(64)));
static uint8_t                s_xfer[CDVD_CHUNK_SEC * PS2_ISO_SECTOR]
                                      __attribute__((aligned(64)));

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
            if ((spins & 2047u) == 0u) ps2_usb_poll();
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
    uint32_t blkwords = 32u;
    uint32_t blkcount = 16u * nsectors;

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
    p[8]  = 16;
    p[9]  = 0;
    p[10] = 0;

    CDVD8(CDVD_HOWTO) = CDVD_HOWTO_2048;
    CDVD8(CDVD_ISTAT) = 0x1F;

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

static int rpc_scmd_bind(void)
{
    if (s_scmd.server) return 1;
    if (!ps2_sif_bind(CDVD_RPC_SCMD, &s_scmd)) {
        uart_puts_raw("[CDVD] SCMD 0x80000593 not bound\n");
        return 0;
    }
    uart_puts_raw("[CDVD] SCMD 0x80000593 bound\n");
    return 1;
}

static int rpc_mmode(int media)
{
    volatile int32_t *r;
    char msg[64];

    if (!rpc_scmd_bind()) return 0;
    s_scmd_arg = media;
    tkl_memset(s_scmd_recv, 0, sizeof(s_scmd_recv));
    if (!ps2_sif_call_rmode(&s_scmd, CDVD_SCMD_MMODE,
                            &s_scmd_arg, 4, s_scmd_recv, 4, 1, 0, 0)) {
        uart_puts_raw("[CDVD] sceCdMmode CALL failed\n");
        return 0;
    }
    r = (volatile int32_t *)((uintptr_t)s_scmd_recv | CDVD_UNCACHED);
    snprintf(msg, sizeof(msg), "[CDVD] sceCdMmode(%d)=%d\n", media, (int)*r);
    uart_puts_raw(msg);
    return *r != 0;
}

static int disk_is_playstation(uint8_t t)
{
    return t == 0x10u || t == 0x11u || t == 0x12u || t == 0x13u || t == 0x14u;
}

static int rpc_disktype(void)
{
    volatile int32_t *r;
    char msg[64];

    if (!rpc_scmd_bind()) return 0;
    tkl_memset(s_scmd_recv, 0, sizeof(s_scmd_recv));
    if (!ps2_sif_call_rmode(&s_scmd, CDVD_SCMD_DISKTYPE,
                            0, 0, s_scmd_recv, 4, 1, 0, 0))
        return 0;
    r = (volatile int32_t *)((uintptr_t)s_scmd_recv | CDVD_UNCACHED);
    snprintf(msg, sizeof(msg), "[CDVD] DiskType=%d\n", (int)*r);
    uart_puts_raw(msg);
    s_disk_type = (uint8_t)*r;
    return 1;
}

/* DETECT 1..4: tray spinning.  0 is empty.  0x10..0x14 is a PS disc. */
static int rpc_disktype_wait(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        if (!rpc_disktype()) return 0;
        if (disk_is_playstation(s_disk_type) || s_disk_type == CDVD_TYPE_NONE)
            return 1;
        wait_us(250000u);
    }
    return 1;
}

static int rpc_ncmd_diskready(void)
{
    volatile int32_t *r;
    char msg[64];

    tkl_memset(s_nrecv, 0, sizeof(s_nrecv));
    if (!ps2_sif_call_rmode(&s_ncmd, CDVD_NCMD_DISKREADY,
                            0, 0, s_nrecv, 4, 1, 0, 0)) {
        uart_puts_raw("[CDVD] NCMD 14 CALL failed\n");
        return 0;
    }
    r = (volatile int32_t *)((uintptr_t)s_nrecv | CDVD_UNCACHED);
    if (*r != CDVD_COMPLETE) {
        snprintf(msg, sizeof(msg), "[CDVD] NCMD14 DiskReady=%d\n", (int)*r);
        uart_puts_raw(msg);
    }
    return *r == CDVD_COMPLETE;
}

static int rpc_ncmd_diskready_wait(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (rpc_ncmd_diskready()) return 1;
        wait_us(250000u);
    }
    uart_puts_raw("[CDVD] NCMD 14 never COMPLETE\n");
    return 0;
}

static int rpc_fill_xfer(uint32_t lba, uint32_t nsectors)
{
    volatile int32_t *ret;
    volatile uint32_t *pos;
    volatile uint8_t *uc;
    char msg[96];

    if (!s_rpc || nsectors == 0 || nsectors > CDVD_CHUNK_SEC) return 0;

    /* Sony sceCdRead always issues NCMD 14 first.  Without it readee
     * returns in a few ms with no sector DMA (PVD stays BSS zeros). */
    (void)rpc_ncmd_diskready();

    tkl_memset(&s_narg, 0, sizeof(s_narg));
    tkl_memset(&s_nrem, 0, sizeof(s_nrem));
    tkl_memset(s_nrecv, 0, sizeof(s_nrecv));
    s_npos = 0;
    /* Poison: a missed DMA leaves 0xA5, not a blank-looking PVD. */
    tkl_memset(s_xfer, 0xA5, nsectors * PS2_ISO_SECTOR);
    s_narg.lbn     = lba;
    s_narg.sectors = nsectors;
    s_narg.paddr   = (uint32_t)(uintptr_t)s_xfer; /* cached physical */
    /* trycount=16, SCECdSpinNom=1, SCECdSecS2048=0 */
    s_narg.rmode   = 16u | (CDVD_SPIN_NOM << 8);
    s_narg.remnant = (uint32_t)(uintptr_t)&s_nrem;
    s_narg.readpos = (uint32_t)(uintptr_t)&s_npos;
    ps2_sif_flush_dcache();

    if (!ps2_sif_call_rmode(&s_ncmd, CDVD_NCMD_READ,
                            &s_narg, CDVD_NCMD_SEND,
                            s_nrecv, 16, 1, 0, 0))
        return 0;

    ps2_sif_invalidate_dcache();
    ret = (volatile int32_t *)((uintptr_t)s_nrecv | CDVD_UNCACHED);
    pos = (volatile uint32_t *)((uintptr_t)&s_npos | CDVD_UNCACHED);
    uc  = (volatile uint8_t *)((uintptr_t)s_xfer | CDVD_UNCACHED);
    snprintf(msg, sizeof(msg),
             "[CDVD] READ lba=%u n=%u ret=%d pos=%u xfer0=%02x\n",
             (unsigned)lba, (unsigned)nsectors,
             (int)*ret, (unsigned)*pos, (unsigned)uc[0]);
    uart_puts_raw(msg);
    return 1;
}

static void rpc_copy_from_xfer(void *dst, uint32_t skip, uint32_t n)
{
    volatile uint8_t *uc;
    struct cdvd_remnant *u;
    intptr_t off;

    uc = (volatile uint8_t *)((uintptr_t)s_xfer | CDVD_UNCACHED);
    tkl_memcpy(dst, (const void *)(uc + skip), n);

    u = (struct cdvd_remnant *)((uintptr_t)&s_nrem | CDVD_UNCACHED);
    if (u->size1 && u->dest1) {
        off = (uint8_t *)u->dest1 - s_xfer - (intptr_t)skip;
        if (off >= 0 && off + (intptr_t)u->size1 <= (intptr_t)n)
            tkl_memcpy((uint8_t *)dst + off, u->src1, u->size1);
    }
    if (u->size2 && u->dest2) {
        off = (uint8_t *)u->dest2 - s_xfer - (intptr_t)skip;
        if (off >= 0 && off + (intptr_t)u->size2 <= (intptr_t)n)
            tkl_memcpy((uint8_t *)dst + off, u->src2, u->size2);
    }
}

static int rpc_read_sectors(uint32_t lba, uint32_t nsectors, void *dst)
{
    if (!rpc_fill_xfer(lba, nsectors)) return 0;
    rpc_copy_from_xfer(dst, 0, nsectors * PS2_ISO_SECTOR);
    return 1;
}

static void hex16(const char *tag, const uint8_t *p)
{
    char msg[80];
    snprintf(msg, sizeof(msg),
             "%s %02x %02x %02x %02x %02x %02x %02x %02x "
             "%02x %02x %02x %02x %02x %02x %02x %02x\n",
             tag,
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
             p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    uart_puts_raw(msg);
}

static int rpc_cdinit(void)
{
    volatile struct cdvd_init_recv *r;
    char msg[96];

    if (!ps2_sif_bind(CDVD_RPC_INIT, &s_init)) {
        uart_puts_raw("[CDVD] sceCdInit 0x80000592 not bound\n");
        return 0;
    }
    s_init_mode = CDVD_INIT_INIT;
    tkl_memset(&s_init_recv, 0, sizeof(s_init_recv));
    if (!ps2_sif_call_rmode(&s_init, 0,
                            &s_init_mode, 4,
                            &s_init_recv, (int)sizeof(s_init_recv),
                            1, 0, 0)) {
        uart_puts_raw("[CDVD] sceCdInit CALL failed\n");
        return 0;
    }
    r = (volatile struct cdvd_init_recv *)
        ((uintptr_t)&s_init_recv | CDVD_UNCACHED);
    s_cdvdfsv_ver = r->cdvdfsv_ver;
    snprintf(msg, sizeof(msg),
             "[CDVD] sceCdInit result=%d cdvdfsv=%d cdvdman=%d\n",
             (int)r->result, (int)r->cdvdfsv_ver, (int)r->cdvdman_ver);
    uart_puts_raw(msg);
    return r->result != 0;
}

static int rpc_diskready(void)
{
    volatile int32_t *r;
    char msg[64];
    int i;

    if (!ps2_sif_bind(CDVD_RPC_DISKREADY, &s_diskready)) {
        uart_puts_raw("[CDVD] DiskReady 0x8000059A not bound\n");
        return 0;
    }
    for (i = 0; i < 8; i++) {
        s_dr_mode = CDVD_READY_WAIT;
        tkl_memset(&s_dr_recv, 0, sizeof(s_dr_recv));
        if (!ps2_sif_call_rmode(&s_diskready, 0,
                                &s_dr_mode, 4, &s_dr_recv, 4, 1, 0, 0)) {
            uart_puts_raw("[CDVD] DiskReady CALL failed\n");
            return 0;
        }
        r = (volatile int32_t *)((uintptr_t)&s_dr_recv | CDVD_UNCACHED);
        snprintf(msg, sizeof(msg), "[CDVD] DiskReady=%d\n", (int)*r);
        uart_puts_raw(msg);
        if (*r == CDVD_COMPLETE) return 1;
        if (*r != CDVD_NOTREADY) return *r != 0;
        wait_us(250000u);
    }
    return 0;
}

static void search_canon(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    if (src[0] != '\\' && src[0] != '/') {
        if (i + 1 < n) dst[i++] = '\\';
    }
    while (*src && i + 1 < n) {
        char c = *src++;
        if (c == '/') c = '\\';
        dst[i++] = c;
    }
    dst[i] = 0;
}

int ps2_cdvd_search(const char *path, uint32_t *out_lsn, uint32_t *out_size)
{
    volatile struct cdvd_cdlfile *fp;
    volatile int32_t *ret;
    char msg[96];

    if (!path || !path[0] || !out_lsn || !out_size) return 0;
    if (!s_search_ok) return 0;

    tkl_memset(&s_spkt, 0, sizeof(s_spkt));
    tkl_memset(&s_srecv, 0, sizeof(s_srecv));
    search_canon(s_spkt.path, sizeof(s_spkt.path), path);
    s_spkt.eedest = &s_spkt;
    ps2_sif_flush_dcache();

    if (!ps2_sif_call_rmode(&s_search, 0,
                            &s_spkt, (int)sizeof(s_spkt),
                            &s_srecv, 4, 1, 0, 0)) {
        uart_puts_raw("[CDVD] SearchFile CALL failed\n");
        return 0;
    }

    fp  = (volatile struct cdvd_cdlfile *)((uintptr_t)&s_spkt | CDVD_UNCACHED);
    ret = (volatile int32_t *)((uintptr_t)&s_srecv | CDVD_UNCACHED);
    {
        char esc[48];
        int i, j = 0;
        for (i = 0; s_spkt.path[i] && j < 44; i++) {
            if (s_spkt.path[i] == '\\') {
                esc[j++] = '\\';
                if (j < 44) esc[j++] = '\\';
            } else {
                esc[j++] = s_spkt.path[i];
            }
        }
        esc[j] = 0;
        snprintf(msg, sizeof(msg),
                 "[CDVD] SearchFile '%s' ret=%d lsn=%u size=%u\n",
                 esc, (int)*ret,
                 (unsigned)fp->lsn, (unsigned)fp->size);
        uart_puts_raw(msg);
    }
    if (*ret == 0 || fp->size == 0) return 0;
    *out_lsn  = fp->lsn;
    *out_size = fp->size;
    return 1;
}

static int rpc_probe_pvd(void)
{
    uint8_t pvd[PS2_ISO_SECTOR];
    int i;

    for (i = 0; i < 2; i++) {
        tkl_memset(pvd, 0, sizeof(pvd));
        if (!rpc_read_sectors(16, 1, pvd)) {
            uart_puts_raw("[CDVD] CDVDFSV PVD read failed\n");
            if (i == 0) wait_us(200000u);
            continue;
        }
        if (pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0) {
            uart_puts_raw("[CDVD] CDVDFSV PVD ok (RPC 0x80000595)\n");
            return 1;
        }
        uart_puts_raw("[CDVD] CDVDFSV PVD is not ISO9660\n");
        hex16("[CDVD] PVD", pvd);
        if (i == 0) wait_us(200000u);
    }
    return 0;
}

static int rpc_handshake(void)
{
    int media;

    if (!ps2_sif_init()) return 0;
    /* sceCdInit / DiskReady are best-effort: BOOT2 already spun the
     * drive.  A failed CALL must not skip NCMD bind.  sceCdRead0
     * returns 0 until sceCdMmode(CD); default mmode is 0.  host: ELF
     * in PCSX2 has an empty tray (DiskType=0) — skip PVD on that. */
    (void)rpc_cdinit();
    (void)rpc_diskready();
    (void)rpc_disktype_wait();
    media = (s_disk_type == CDVD_TYPE_PS2_DVD) ? CDVD_MMODE_DVD
                                               : CDVD_MMODE_CD;
    (void)rpc_mmode(media);
    if (!disk_is_playstation(s_disk_type)) {
        uart_puts_raw(
            "[CDVD] GATE1 tray empty. Boot btron-ps2.iso, not host: ELF.\n");
        return 1;
    }

    if (ps2_sif_bind(CDVD_RPC_SEARCH, &s_search)) {
        uint32_t lsn = 0, sz = 0;
        s_search_ok = 1;
        uart_puts_raw("[CDVD] SearchFile 0x80000597 bound\n");
        (void)ps2_cdvd_search("\\SYSTEM.CNF;1", &lsn, &sz);
    } else {
        uart_puts_raw("[CDVD] SearchFile 0x80000597 not bound\n");
    }

    if (!ps2_sif_bind(CDVD_RPC_NCMD, &s_ncmd)) {
        uart_puts_raw("[CDVD] CDVDFSV 0x80000595 not bound\n");
        return 0;
    }
    s_rpc = 1;
    {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "[CDVD] NCMD 0x80000595 bound buf=%p\n", s_ncmd.buf);
        uart_puts_raw(msg);
    }
    (void)rpc_ncmd_diskready_wait();
    return 1;
}

int ps2_cdvd_init(void)
{
    char msg[80];

    if (s_inited) {
        if (s_ready) return 1;
        return s_search_ok || s_rpc;
    }
    s_inited = 1;

    if (rpc_handshake()) {
        if (!disk_is_playstation(s_disk_type)) {
            s_ready = 0;
            return 0;
        }
        if (rpc_probe_pvd()) {
            s_ready = 1;
            return 1;
        }
        uart_puts_raw("[CDVD] PVD miss; NCMD READ never landed CD001\n");
        s_ready = 0;
        return s_search_ok || s_rpc;
    }

    if (ps2_sif_ready()) {
        uart_puts_raw("[CDVD] SIF up, skipping EE MMIO 0xBF402000\n");
        s_ready = 0;
        return 0;
    }

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
        if (s_rpc) {
            if (!rpc_read_sectors(lba, n, out)) return 0;
        } else {
            if (!read_chunk_to_iop(lba, n)) return 0;
            copy_iop(out, 0, n * PS2_ISO_SECTOR);
        }
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
        if (s_rpc) {
            if (!rpc_fill_xfer(lba, nsec)) return 0;
            copy = nsec * PS2_ISO_SECTOR - skip;
            if (copy > remaining) copy = remaining;
            rpc_copy_from_xfer(out, skip, copy);
        } else {
            if (!read_chunk_to_iop(lba, nsec)) return 0;
            copy = nsec * PS2_ISO_SECTOR - skip;
            if (copy > remaining) copy = remaining;
            copy_iop(out, skip, copy);
        }
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
