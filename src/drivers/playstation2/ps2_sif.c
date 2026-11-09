/*
 * ps2_sif.c — Cleanroom poll-mode SIF CMD/RPC client
 *
 * Syscalls: FlushCache 0x64, SifDmaStat 0x76, SifSetDma 0x77,
 * SifSetDChain 0x78, SifSetReg 0x79, SifGetReg 0x7a, DisableDmac 0x17.
 * SIF0 completion is polled on D_STAT; leftover BOOT2 handlers are
 * masked so they cannot consume packets destined for our pktbuf.
 *
 * Protocol from public headers only.  No vendor SDK C.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_sif.h"
#include "ps2_irq.h"
#include "ps2_console.h"
#include "ps2_usb.h"

#include <libstr.h>
#include <stdio.h>

extern int32_t  ps2_ee_syscall3(int32_t nr, int32_t a0, int32_t a1, int32_t a2);
extern uint32_t ps2_count_read(void);

#define EE_SYS_FLUSH_CACHE    0x64
#define EE_SYS_SIF_DMA_STAT   0x76
#define EE_SYS_SIF_SET_DMA    0x77
#define EE_SYS_SIF_SET_DCHAIN 0x78
#define EE_SYS_SIF_SET_REG    0x79
#define EE_SYS_SIF_GET_REG    0x7a
#define EE_SYS_DISABLE_DMAC   0x17

#define DMAC_SIF0             5
#define SIF0_CHCR             (*(volatile uint32_t *)0xB000c000u)
#define DMAC_STAT             (*(volatile uint32_t *)0xB000e010u)
#define CHCR_STR              0x100u
#define STAT_SIF0             0x20u

#define SIF_UNCACHED          0x20000000u
#define SIF_DMA_INT_O         0x4
#define SIF_DMA_ERT           0x40

#define SIF_REG_SUBADDR       2
#define SIF_REG_SMFLAG        4
#define SIF_SYSREG_SUBADDR    0x80000000u
#define SIF_SYSREG_MAINADDR   0x80000001u
#define SIF_SYSREG_RPCINIT    0x80000002u
#define SIF_STAT_CMDINIT      0x20000u

#define SIF_CMD_CHANGE_SADDR  0x80000000u
#define SIF_CMD_SET_SREG      0x80000001u
#define SIF_CMD_INIT_CMD      0x80000002u
#define SIF_CMD_RPC_END       0x80000008u
#define SIF_CMD_RPC_BIND      0x80000009u
#define SIF_CMD_RPC_CALL      0x8000000au

#define SIF_PKT_MAX           64
#define SIF_TIMEOUT_US        2000000u
#define SIF_CALL_US           8000000u

typedef struct {
    uint32_t size_word;     /* psize:8 | dsize:24, little-endian */
    void    *dest;
    int32_t  cid;
    uint32_t opt;
} ps2_sif_hdr_t;

typedef struct {
    void    *src;
    void    *dest;
    int32_t  size;
    int32_t  attr;
} ps2_sif_dma_t;

typedef struct {
    ps2_sif_hdr_t hdr;
    int32_t       rec_id;
    void         *pkt_addr;
    int32_t       rpc_id;
    void         *cd;
    int32_t       sid;
    uint8_t       pad[SIF_PKT_MAX - 36];
} ps2_sif_bind_pkt_t;

typedef struct {
    ps2_sif_hdr_t hdr;
    int32_t       rec_id;
    void         *pkt_addr;
    int32_t       rpc_id;
    void         *cd;
    int32_t       rpc_number;
    int32_t       send_size;
    void         *recvbuf;
    int32_t       recv_size;
    int32_t       rmode;
    void         *sd;
    uint8_t       pad[SIF_PKT_MAX - 56];
} ps2_sif_call_pkt_t;

typedef struct {
    ps2_sif_hdr_t hdr;
    int32_t       rec_id;
    void         *pkt_addr;
    int32_t       rpc_id;
    void         *cd;
    uint32_t      cid;
    void         *sd;
    void         *buf;
    void         *cbuf;
} ps2_sif_rend_pkt_t;

typedef char ps2_sif_hdr_sz[(sizeof(ps2_sif_hdr_t) == 16) ? 1 : -1];
typedef char ps2_sif_bind_sz[(sizeof(ps2_sif_bind_pkt_t) == SIF_PKT_MAX) ? 1 : -1];
typedef char ps2_sif_call_sz[(sizeof(ps2_sif_call_pkt_t) == SIF_PKT_MAX) ? 1 : -1];

static uint8_t s_pktbuf[SIF_PKT_MAX] __attribute__((aligned(64)));
static uint8_t s_cmdpkt[SIF_PKT_MAX] __attribute__((aligned(64)));
static volatile ps2_sif_hdr_t *s_pkt_uc;
static void    *s_iopbuf;
static int      s_ready;
static int      s_rpc_id = 1;
static int      s_count_runs = -1;
static ps2_sif_rpc_t *s_wait_cli;

static int count_running(void)
{
    if (s_count_runs < 0) {
        uint32_t a = ps2_count_read();
        {
            volatile int i;
            for (i = 0; i < 8000; i++) { }
        }
        s_count_runs = (ps2_count_read() != a) ? 1 : 0;
    }
    return s_count_runs;
}

static int wait_budget(uint32_t start, uint32_t timeout_us, uint32_t spins)
{
    if (!count_running())
        return spins > timeout_us * 40u;
    return (uint32_t)(ps2_count_read() - start) >=
           timeout_us * btron_ee_ticks_per_us();
}

void ps2_sif_flush_dcache(void)
{
    (void)ps2_ee_syscall3(EE_SYS_FLUSH_CACHE, 0, 0, 0);
}

void ps2_sif_invalidate_dcache(void)
{
    (void)ps2_ee_syscall3(EE_SYS_FLUSH_CACHE, 1, 0, 0);
}

static int32_t sif_get_reg(uint32_t reg)
{
    return ps2_ee_syscall3(EE_SYS_SIF_GET_REG, (int32_t)reg, 0, 0);
}

static void sif_set_reg(uint32_t reg, uint32_t val)
{
    (void)ps2_ee_syscall3(EE_SYS_SIF_SET_REG, (int32_t)reg, (int32_t)val, 0);
}

static void sif_set_dchain(void)
{
    (void)ps2_ee_syscall3(EE_SYS_SIF_SET_DCHAIN, 0, 0, 0);
}

static int dma_wait(int32_t id, uint32_t timeout_us)
{
    uint32_t start = ps2_count_read();
    uint32_t spins = 0;

    if (id == 0) return 0;
    for (;;) {
        int32_t st = ps2_ee_syscall3(EE_SYS_SIF_DMA_STAT, id, 0, 0);
        if (st < 0) return 1;
        if ((spins & 2047u) == 0u) ps2_usb_poll();
        if (wait_budget(start, timeout_us, ++spins)) return 0;
    }
}

static uint32_t sif_send(int cid, void *pkt, int pktsize,
                         void *extra_src, void *extra_dst, int extra_size)
{
    ps2_sif_dma_t dmat[2];
    ps2_sif_hdr_t *h = (ps2_sif_hdr_t *)pkt;
    int n = 0;
    int32_t id;

    if (!s_iopbuf || pktsize < 16 || pktsize > 112) return 0;

    h->size_word = ((uint32_t)pktsize & 0xffu) |
                   ((uint32_t)(extra_size > 0 ? extra_size : 0) << 8);
    h->dest = (extra_size > 0) ? extra_dst : 0;
    h->cid  = cid;

    if (extra_size > 0 && extra_src && extra_dst) {
        dmat[n].src  = extra_src;
        dmat[n].dest = extra_dst;
        dmat[n].size = extra_size;
        dmat[n].attr = 0;
        n++;
    }
    dmat[n].src  = pkt;
    dmat[n].dest = s_iopbuf;
    dmat[n].size = pktsize;
    dmat[n].attr = SIF_DMA_ERT | SIF_DMA_INT_O;
    n++;

    ps2_sif_flush_dcache();
    id = ps2_ee_syscall3(EE_SYS_SIF_SET_DMA, (int32_t)(uintptr_t)dmat, n, 0);
    if (!dma_wait(id, 1000000u)) return 0;
    return (uint32_t)id;
}

static void sif_dispatch(void *packet)
{
    ps2_sif_hdr_t *h = (ps2_sif_hdr_t *)packet;
    uint32_t cid = (uint32_t)h->cid;

    if (cid == SIF_CMD_CHANGE_SADDR) {
        struct { ps2_sif_hdr_t hdr; void *buf; } *p = packet;
        if (p->buf) s_iopbuf = p->buf;
        return;
    }
    if (cid == SIF_CMD_SET_SREG) {
        struct { ps2_sif_hdr_t hdr; int32_t index; uint32_t value; } *p = packet;
        if (p->index == 0 && p->value)
            sif_set_reg(SIF_SYSREG_RPCINIT, 1);
        return;
    }
    if (cid == SIF_CMD_RPC_END) {
        ps2_sif_rend_pkt_t *r = (ps2_sif_rend_pkt_t *)packet;
        ps2_sif_rpc_t *cli = (ps2_sif_rpc_t *)r->cd;
        if (!cli || cli != s_wait_cli) return;
        if (r->cid == SIF_CMD_RPC_BIND) {
            cli->server = r->sd;
            cli->buf    = r->buf;
            cli->cbuf   = r->cbuf;
        } else if (r->cid == SIF_CMD_RPC_CALL) {
            if (cli->end) cli->end(cli->end_arg);
        }
        cli->done = 1;
        s_wait_cli = 0;
    }
}

static int sif_pump(uint32_t timeout_us)
{
    uint32_t start = ps2_count_read();
    uint32_t spins = 0;
    uint8_t  copy[SIF_PKT_MAX];
    uint32_t psize;
    uint32_t i;

    for (;;) {
        psize = s_pkt_uc->size_word & 0xffu;
        if ((DMAC_STAT & STAT_SIF0) || psize) {
            if (DMAC_STAT & STAT_SIF0)
                DMAC_STAT = STAT_SIF0;
            psize = s_pkt_uc->size_word & 0xffu;
            if (psize) {
                if (psize > SIF_PKT_MAX) psize = SIF_PKT_MAX;
                for (i = 0; i < psize; i++)
                    copy[i] = ((volatile uint8_t *)s_pkt_uc)[i];
                s_pkt_uc->size_word = 0;
                sif_set_dchain();
                sif_dispatch(copy);
                return 1;
            }
            sif_set_dchain();
        }
        if ((spins & 2047u) == 0u) ps2_usb_poll();
        if (wait_budget(start, timeout_us, ++spins)) return 0;
    }
}

int ps2_sif_init(void)
{
    struct {
        ps2_sif_hdr_t hdr;
        void         *buf;
        uint8_t       pad[SIF_PKT_MAX - 20];
    } *ca = (void *)s_cmdpkt;
    int32_t iop;
    uint32_t start;
    uint32_t spins = 0;
    char msg[80];

    if (s_ready) return 1;

    tkl_memset(s_pktbuf, 0, sizeof(s_pktbuf));
    tkl_memset(s_cmdpkt, 0, sizeof(s_cmdpkt));
    ps2_sif_flush_dcache();
    s_pkt_uc = (volatile ps2_sif_hdr_t *)((uintptr_t)s_pktbuf | SIF_UNCACHED);

    /* BOOT2 left a SIF0 DMAC handler on a pktbuf we overwrote.  Mask it. */
    (void)ps2_ee_syscall3(EE_SYS_DISABLE_DMAC, DMAC_SIF0, 0, 0);
    if (DMAC_STAT & STAT_SIF0)
        DMAC_STAT = STAT_SIF0;
    if (!(SIF0_CHCR & CHCR_STR))
        sif_set_dchain();

    iop = sif_get_reg(SIF_SYSREG_SUBADDR);
    if (iop == 0)
        iop = sif_get_reg(SIF_REG_SUBADDR);

    if (iop) {
        s_iopbuf = (void *)(uintptr_t)(uint32_t)iop;
        tkl_memset(s_cmdpkt, 0, sizeof(s_cmdpkt));
        ca->buf = (void *)s_pkt_uc;
        if (!sif_send(SIF_CMD_CHANGE_SADDR, s_cmdpkt, 20, 0, 0, 0)) {
            uart_puts_raw("[SIF] CHANGE_SADDR DMA failed\n");
            return 0;
        }
        if (!(SIF0_CHCR & CHCR_STR))
            sif_set_dchain();
    } else {
        start = ps2_count_read();
        while (!(sif_get_reg(SIF_REG_SMFLAG) & SIF_STAT_CMDINIT)) {
            if (wait_budget(start, SIF_TIMEOUT_US, ++spins)) {
                uart_puts_raw("[SIF] IOP CMDINIT timeout\n");
                return 0;
            }
        }
        iop = sif_get_reg(SIF_REG_SUBADDR);
        if (!iop) {
            uart_puts_raw("[SIF] no IOP cmd buffer\n");
            return 0;
        }
        s_iopbuf = (void *)(uintptr_t)(uint32_t)iop;
        sif_set_reg(SIF_SYSREG_SUBADDR, (uint32_t)iop);
        tkl_memset(s_cmdpkt, 0, sizeof(s_cmdpkt));
        ca->hdr.opt = 0;
        ca->buf = (void *)s_pkt_uc;
        if (!sif_send(SIF_CMD_INIT_CMD, s_cmdpkt, 20, 0, 0, 0)) {
            uart_puts_raw("[SIF] INIT_CMD DMA failed\n");
            return 0;
        }
    }

    if (!sif_get_reg(SIF_SYSREG_RPCINIT)) {
        tkl_memset(s_cmdpkt, 0, sizeof(s_cmdpkt));
        ((ps2_sif_hdr_t *)s_cmdpkt)->opt = 1;
        if (!sif_send(SIF_CMD_INIT_CMD, s_cmdpkt, 16, 0, 0, 0)) {
            uart_puts_raw("[SIF] RPC INIT_CMD failed\n");
            return 0;
        }
        start = ps2_count_read();
        spins = 0;
        while (!sif_get_reg(SIF_SYSREG_RPCINIT)) {
            (void)sif_pump(10000u);
            if (wait_budget(start, SIF_TIMEOUT_US, ++spins)) {
                uart_puts_raw("[SIF] RPCINIT timeout\n");
                return 0;
            }
        }
    }

    s_ready = 1;
    snprintf(msg, sizeof(msg), "[SIF] ready iopbuf=%p\n", s_iopbuf);
    uart_puts_raw(msg);
    return 1;
}

int ps2_sif_ready(void)
{
    return s_ready;
}

int ps2_sif_bind(uint32_t sid, ps2_sif_rpc_t *cli)
{
    ps2_sif_bind_pkt_t *b = (ps2_sif_bind_pkt_t *)s_cmdpkt;
    uint32_t start;
    uint32_t spins = 0;

    if (!cli) return 0;
    if (!s_ready && !ps2_sif_init()) return 0;

    tkl_memset(cli, 0, sizeof(*cli));
    tkl_memset(s_cmdpkt, 0, sizeof(s_cmdpkt));
    b->rec_id   = 0x05;
    b->pkt_addr = s_cmdpkt;
    b->rpc_id   = s_rpc_id++;
    b->cd       = cli;
    b->sid      = (int32_t)sid;
    cli->done   = 0;
    s_wait_cli  = cli;

    if (!sif_send(SIF_CMD_RPC_BIND, s_cmdpkt, SIF_PKT_MAX, 0, 0, 0)) {
        s_wait_cli = 0;
        uart_puts_raw("[SIF] BIND send failed\n");
        return 0;
    }

    start = ps2_count_read();
    while (!cli->done) {
        if (!sif_pump(50000u) && wait_budget(start, SIF_TIMEOUT_US, ++spins)) {
            char msg[64];
            s_wait_cli = 0;
            snprintf(msg, sizeof(msg), "[SIF] BIND timeout sid=0x%08x\n",
                     (unsigned)sid);
            uart_puts_raw(msg);
            return 0;
        }
        spins++;
    }
    if (!cli->server) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[SIF] BIND: no server sid=0x%08x\n",
                 (unsigned)sid);
        uart_puts_raw(msg);
        return 0;
    }
    return 1;
}

int ps2_sif_call_rmode(ps2_sif_rpc_t *cli, int fno,
                       void *send, int send_size,
                       void *recv, int recv_size, int rmode,
                       void (*end)(void *), void *end_arg)
{
    ps2_sif_call_pkt_t *c = (ps2_sif_call_pkt_t *)s_cmdpkt;
    uint32_t start;
    uint32_t spins = 0;

    if (!cli || !cli->server) return 0;
    if (!s_ready) return 0;

    tkl_memset(s_cmdpkt, 0, sizeof(s_cmdpkt));
    c->rec_id     = 0x05;
    c->pkt_addr   = s_cmdpkt;
    c->rpc_id     = s_rpc_id++;
    c->cd         = cli;
    c->rpc_number = fno;
    c->send_size  = send_size;
    c->recvbuf    = recv;
    c->recv_size  = recv_size;
    /* rmode 0 makes IOP DMA RPC_END to pkt_addr; our poll client only
     * sees SIF_CMD_RPC_END on the CHANGE_SADDR pktbuf.  Always 1. */
    (void)rmode;
    c->rmode      = 1;
    c->sd         = cli->server;
    cli->end      = end;
    cli->end_arg  = end_arg;
    cli->done     = 0;
    s_wait_cli    = cli;

    if (send_size > 0 && !cli->buf) {
        uart_puts_raw("[SIF] CALL extra dest is NULL (BIND buf missed)\n");
        return 0;
    }
    if (recv && recv_size > 0)
        ps2_sif_flush_dcache();

    if (!sif_send(SIF_CMD_RPC_CALL, s_cmdpkt, SIF_PKT_MAX,
                  send, cli->buf, send_size)) {
        s_wait_cli = 0;
        return 0;
    }

    start = ps2_count_read();
    while (!cli->done) {
        if (!sif_pump(50000u) && wait_budget(start, SIF_CALL_US, ++spins)) {
            s_wait_cli = 0;
            uart_puts_raw("[SIF] CALL timeout\n");
            return 0;
        }
        spins++;
    }
    if (recv && recv_size > 0)
        ps2_sif_invalidate_dcache();
    return 1;
}

int ps2_sif_call(ps2_sif_rpc_t *cli, int fno,
                 void *send, int send_size,
                 void *recv, int recv_size,
                 void (*end)(void *), void *end_arg)
{
    return ps2_sif_call_rmode(cli, fno, send, send_size,
                              recv, recv_size, 1, end, end_arg);
}
