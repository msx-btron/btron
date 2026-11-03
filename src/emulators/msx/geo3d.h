/*
 * geo3d.h — MSX Geo3D Geometry Coprocessor (V9968 Cartridge extension).
 *
 * Clean-room C99 implementation of Alexander Moncks / HRA! Geo3D coprocessor v2.0.
 * Handles 3x3 fixed-point matrix transforms (Q2.14), perspective projection,
 * edge clipping, face shading, depth sorting, and direct LINE/LRMM polygon rendering.
 */

#ifndef _BTRON_EMULATOR_MSX_GEO3D_H_
#define _BTRON_EMULATOR_MSX_GEO3D_H_

#include <stdint.h>
#include "v9938.h"

#define GEO3D_AW    10u
#define GEO3D_NMEM  (1u << GEO3D_AW) /* 1024 */
#define GEO3D_MASK  (GEO3D_NMEM - 1u) /* 1023 */
#define GEO3D_ID    0x20u             /* Geo3D v2.0 signature */

typedef struct {
    int64_t  sx, sy, z;
    uint32_t flags;
} geo3d_proj_t;

typedef struct {
    uint16_t i[4];
    int64_t  n[3];
    uint8_t  base;
} geo3d_face_t;

typedef struct {
    int64_t  key;
    uint16_t fidx;
    uint8_t  col;
    int64_t  lvl;
} geo3d_vis_t;

typedef struct {
    /* Configuration registers (M00..M22, TX, TY, TZ, F, CX, CY, ZNEAR, W, H) */
    int64_t         cfg[18];
    int64_t         ivtx[3];
    geo3d_proj_t    imm;

    /* Model RAMs (1024 entries) */
    int64_t         vmem[GEO3D_NMEM][3];
    uint16_t        emem[GEO3D_NMEM][2];
    geo3d_face_t    fmem[GEO3D_NMEM];
    uint8_t         tmem[GEO3D_NMEM][8];
    geo3d_proj_t    pmem[GEO3D_NMEM];
    geo3d_vis_t     vis[GEO3D_NMEM];

    /* Control & Address Registers */
    uint8_t         widx;
    uint8_t         rptr;
    uint8_t         lo;
    uint16_t        vaddr, eaddr, nvert, nedge; /* 10-bit */
    uint16_t        faddr, nface, taddr;        /* 10-bit */
    uint16_t        vfirst, efirst, ffirst;     /* 10-bit */
    uint8_t         ext;                        /* Extended 10-bit indexing mode */
    uint8_t         color, lop;
    uint32_t        ypage;                      /* 11-bit */
    uint8_t         tstride;
    uint32_t        texx;                       /* 9-bit */
    uint32_t        texy;                       /* 13-bit */
    int64_t         light[3];
    uint8_t         ctrlFace, ctrlTex;

    /* Streaming buffers */
    uint8_t         vbuf[6];
    uint8_t         vbuf_len;
    uint8_t         ebuf[3];
    uint8_t         ebuf_len;
    uint8_t         fbuf[12];
    uint8_t         fbuf_len;
    uint8_t         tbuf[8];
    uint8_t         tbuf_len;

    /* Status & Counters */
    uint8_t         running;
    uint16_t        cntSkip, cntDraw, cntCull;
    uint16_t        cnt_skipped, cnt_drawn, cnt_culled;

    /* Attached VDP */
    v9938_t        *vdp;
} geo3d_t;

void    geo3d_init(geo3d_t *g);
void    geo3d_reset(geo3d_t *g);
void    geo3d_attach_vdp(geo3d_t *g, v9938_t *v);

uint8_t geo3d_read(geo3d_t *g, uint8_t port);
void    geo3d_write(geo3d_t *g, uint8_t port, uint8_t val);

#endif /* _BTRON_EMULATOR_MSX_GEO3D_H_ */
