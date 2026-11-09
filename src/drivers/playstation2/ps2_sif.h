/*
 * ps2_sif.h — Emotion Engine SIF CMD/RPC client (poll mode)
 *
 * EE talks to IOP FILEIO / CDVDMAN over SIF, the same path BOOT2 used
 * to load BTRON.ELF.  The CDVD block at IOP 0x1F402000 is not on the
 * EE's I/O window; SIF RPC is.
 *
 * Polls DMAC SIF0 completion instead of AddDmacHandler.  Layouts match
 * the public SIF CMD/RPC protocol (sifcmd-common / sifrpc-common /
 * sifdma).  No vendor C is imported.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_SIF_H
#define PS2_SIF_H

#include <stdint.h>

typedef struct ps2_sif_rpc {
    void            *server;    /* IOP server cookie from BIND */
    void            *buf;       /* IOP send buffer (RPC extra dest) */
    void            *cbuf;
    volatile int     done;
    void           (*end)(void *arg);
    void            *end_arg;
} ps2_sif_rpc_t;

int  ps2_sif_init(void);
int  ps2_sif_ready(void);

/* Bind RPC service `sid` (FILEIO is 0x80000001).  Fills cli->server. */
int  ps2_sif_bind(uint32_t sid, ps2_sif_rpc_t *cli);

/* Blocking RPC.  send/recv may be NULL when the matching size is 0.
 * rmode is ignored: IOP rmode 0 DMAs RPC_END to pkt_addr, which this
 * poll client never sees.  Calls always use rmode 1 (CMD-chain END). */
int  ps2_sif_call_rmode(ps2_sif_rpc_t *cli, int fno,
                        void *send, int send_size,
                        void *recv, int recv_size, int rmode,
                        void (*end)(void *), void *end_arg);

int  ps2_sif_call(ps2_sif_rpc_t *cli, int fno,
                  void *send, int send_size,
                  void *recv, int recv_size,
                  void (*end)(void *), void *end_arg);

void ps2_sif_flush_dcache(void);
void ps2_sif_invalidate_dcache(void);

#endif /* PS2_SIF_H */
