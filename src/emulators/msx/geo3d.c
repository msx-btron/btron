/*
 * geo3d.c — MSX Geo3D Geometry Coprocessor Implementation.
 *
 * Alexander Moncks / HRA! Geo3D hardware geometry pipeline emulator v2.0.
 * Synthesized for MSX FPGA configurations (e.g. V9968 Cartridge on Tang Nano 20K).
 */

#include "geo3d.h"
#include <string.h>
#include <stdlib.h>

static inline int64_t floor_div(int64_t a, int64_t b)
{
    if (b == 0) return 0;
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

static inline int64_t sat(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline int64_t sat18(int64_t v)
{
    return sat(v, -131071, 131071);
}

static inline int64_t signed16(uint32_t x)
{
    x &= 0xFFFFu;
    return (x & 0x8000u) ? (int64_t)(int32_t)(x - 0x10000u) : (int64_t)x;
}

static inline uint16_t bump(const geo3d_t *g, uint16_t p)
{
    return g->ext ? (uint16_t)((p + 1u) & GEO3D_MASK)
                  : (uint16_t)((p & 0x300u) | ((p + 1u) & 0xFFu));
}

void geo3d_init(geo3d_t *g)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    geo3d_reset(g);
}

void geo3d_reset(geo3d_t *g)
{
    if (!g) return;
    v9938_t *vdp_save = g->vdp;
    memset(g, 0, sizeof(*g));
    g->vdp = vdp_save;

    g->cfg[0] = 0; g->cfg[1] = 0; g->cfg[2] = 0;
    g->cfg[3] = 0; g->cfg[4] = 0; g->cfg[5] = 0;
    g->cfg[6] = 0; g->cfg[7] = 0; g->cfg[8] = 0;
    g->cfg[9] = 0; g->cfg[10] = 0; g->cfg[11] = 0;
    g->cfg[12] = 256; /* F */
    g->cfg[13] = 128; /* CX */
    g->cfg[14] = 106; /* CY */
    g->cfg[15] = 16;  /* ZNEAR */
    g->cfg[16] = 256; /* W */
    g->cfg[17] = 212; /* H */

    g->rptr = 0x30;
    g->color = 15;
    g->texy = 512;
    g->light[0] = 0;
    g->light[1] = 0;
    g->light[2] = -16384;
}

void geo3d_attach_vdp(geo3d_t *g, v9938_t *v)
{
    if (g) g->vdp = v;
}

/* ---- Vertex Projection -------------------------------------------------- */

static geo3d_proj_t geo3d_project(const int64_t *c, const int64_t *v)
{
    int64_t xyz[3];
    for (int r = 0; r < 3; ++r) {
        int64_t acc = c[3 * r] * v[0] + c[3 * r + 1] * v[1] + c[3 * r + 2] * v[2];
        xyz[r] = sat((acc >> 14) + c[9 + r], -131071, 131071);
    }
    int64_t f = c[12], cx = c[13], cy = c[14], znear = c[15], w = c[16], h = c[17];
    int64_t zc = xyz[2];
    int64_t zout = sat(zc, -32768, 32767);
    if (zc < znear || zc <= 0) {
        geo3d_proj_t p = {0, 0, zout, 0x01u};
        return p;
    }
    uint32_t flags = 0;
    int64_t q[2];
    for (int axis = 0; axis < 2; ++axis) {
        int64_t cc = xyz[axis];
        int64_t a = (cc < 0) ? -cc : cc;
        int64_t num = a * f;
        int64_t qq;
        if (num >= (zc << 15)) {
            qq = 32767;
            flags |= 1u << (1 + axis);
        } else {
            qq = num / zc;
        }
        q[axis] = (cc < 0) ? -qq : qq;
    }
    int64_t sx = sat(cx + q[0], -32768, 32767);
    int64_t sy = sat(cy - q[1], -32768, 32767);
    uint32_t outc = (sx < 0) | ((sx >= w) << 1) | ((sy < 0) << 2) | ((sy >= h) << 3);
    flags |= outc << 4;
    geo3d_proj_t p = {sx, sy, zout, flags};
    return p;
}

/* ---- VDP Command Emission ----------------------------------------------- */

static void emit_cmd(geo3d_t *g, const uint8_t *bytes, unsigned size)
{
    if (!g->vdp) return;
    for (unsigned k = 0; k < size; k++) {
        unsigned reg;
        if (size != 19) {
            reg = 36 + k;
        } else {
            reg = (k < 14) ? (32 + k) : (k < 18) ? (33 + k) : 46;
        }
        v9938_write_reg(g->vdp, (uint8_t)reg, bytes[k]);
    }
}

/* ---- Wireframe Edge Rendering ------------------------------------------- */

typedef struct { int64_t x, y; } geo3d_pt_t;

static inline uint32_t ocode(geo3d_pt_t p, int64_t w, int64_t h)
{
    return (p.x < 0) | ((p.x >= w) << 1) | ((p.y < 0) << 2) | ((p.y >= h) << 3);
}

static inline geo3d_pt_t pt_mid(geo3d_pt_t a, geo3d_pt_t b)
{
    geo3d_pt_t m = {(a.x + b.x) >> 1, (a.y + b.y) >> 1};
    return m;
}

static geo3d_pt_t bisect_pt(geo3d_pt_t inside, geo3d_pt_t outside, int64_t w, int64_t h)
{
    geo3d_pt_t lo = inside, hi = outside;
    for (int it = 0; it < 16; ++it) {
        geo3d_pt_t m = pt_mid(lo, hi);
        if (ocode(m, w, h) == 0) lo = m; else hi = m;
    }
    return lo;
}

enum { CLIP_SKIP, CLIP_CULL, CLIP_DRAW };

static int clip_edge(geo3d_pt_t A, geo3d_pt_t B, int64_t w, int64_t h, geo3d_pt_t *e1, geo3d_pt_t *e2)
{
    uint32_t ca = ocode(A, w, h), cb = ocode(B, w, h);
    if (ca & cb) return CLIP_CULL;
    geo3d_pt_t p = {0, 0};
    if (ca == 0) {
        p = A;
    } else if (cb == 0) {
        p = B;
    } else {
        geo3d_pt_t lo = A, hi = B;
        int found = 0;
        for (int it = 0; it < 16; ++it) {
            geo3d_pt_t m = pt_mid(lo, hi);
            uint32_t cm = ocode(m, w, h);
            if (cm == 0) { p = m; found = 1; break; }
            if (ocode(lo, w, h) & cm) {
                lo = m;
            } else if (cm & ocode(hi, w, h)) {
                hi = m;
            } else {
                return CLIP_SKIP;
            }
        }
        if (!found) return CLIP_SKIP;
    }
    *e1 = (ca == 0) ? A : bisect_pt(p, A, w, h);
    *e2 = (cb == 0) ? B : bisect_pt(p, B, w, h);
    return CLIP_DRAW;
}

static void geo3d_render_edges(geo3d_t *g)
{
    int64_t w = g->cfg[16], h = g->cfg[17];
    for (uint32_t k = 0; k < g->nedge; ++k) {
        uint32_t e = (g->efirst + k) & GEO3D_MASK;
        const geo3d_proj_t *pa = &g->pmem[g->emem[e][0]];
        const geo3d_proj_t *pb = &g->pmem[g->emem[e][1]];
        if ((pa->flags & 7u) || (pb->flags & 7u)) {
            g->cntSkip++;
            g->cnt_skipped++;
            continue;
        }
        geo3d_pt_t pa_pt = {pa->sx, pa->sy};
        geo3d_pt_t pb_pt = {pb->sx, pb->sy};
        geo3d_pt_t e1, e2;
        int res = clip_edge(pa_pt, pb_pt, w, h, &e1, &e2);
        if (res == CLIP_SKIP) {
            g->cntSkip++;
            g->cnt_skipped++;
        } else if (res == CLIP_CULL) {
            g->cntCull++;
            g->cnt_culled++;
        } else {
            int64_t dx = e2.x - e1.x, dy = e2.y - e1.y;
            int64_t ax = (dx < 0) ? -dx : dx;
            int64_t ay = (dy < 0) ? -dy : dy;
            int maj = ay > ax;
            int64_t nx = maj ? ay : ax;
            int64_t ny = maj ? ax : ay;
            uint8_t arg = (uint8_t)(((dy < 0) << 3) | ((dx < 0) << 2) | maj);
            int64_t dyf = ((e1.y & 0x7FF) + g->ypage) & 0x7FF;
            uint8_t c_bytes[11] = {
                (uint8_t)(e1.x & 0xFF), (uint8_t)((e1.x >> 8) & 1),
                (uint8_t)(dyf & 0xFF), (uint8_t)(dyf >> 8),
                (uint8_t)(nx & 0xFF), (uint8_t)((nx >> 8) & 7),
                (uint8_t)(ny & 0xFF), (uint8_t)((ny >> 8) & 7),
                g->color, arg, (uint8_t)(0x70 | g->lop)
            };
            emit_cmd(g, c_bytes, 11);
            g->cntDraw++;
            g->cnt_drawn++;
        }
    }
}

/* ---- Face Rendering ----------------------------------------------------- */

typedef struct { int64_t x, u, v; } geo3d_cand_t;

static int vis_cmp(const void *pa, const void *pb)
{
    const geo3d_vis_t *a = (const geo3d_vis_t *)pa;
    const geo3d_vis_t *b = (const geo3d_vis_t *)pb;
    if (a->key != b->key) {
        return (a->key > b->key) ? -1 : 1;
    }
    return (a->fidx < b->fidx) ? -1 : (a->fidx > b->fidx) ? 1 : 0;
}

static void geo3d_render_faces(geo3d_t *g, int texOn)
{
    int64_t w = g->cfg[16], h = g->cfg[17];
    int64_t lm[3];
    for (int j = 0; j < 3; ++j) {
        int64_t s = 0;
        for (int i = 0; i < 3; ++i) s += g->cfg[3 * i + j] * g->light[i];
        lm[j] = sat18(s >> 14);
    }

    uint32_t vis_count = 0;
    for (uint32_t k = 0; k < g->nface; ++k) {
        uint32_t fidx = (g->ffirst + k) & GEO3D_MASK;
        const geo3d_face_t *f = &g->fmem[fidx];
        const geo3d_proj_t *pv[4];
        int near = 0;
        for (int m = 0; m < 4; ++m) {
            pv[m] = &g->pmem[f->i[m]];
            if (pv[m]->flags & 7u) near = 1;
        }
        if (near) {
            g->cntSkip++;
            g->cnt_skipped++;
            continue;
        }
        int64_t x0 = pv[0]->sx, y0 = pv[0]->sy;
        int64_t x1 = pv[1]->sx, y1 = pv[1]->sy;
        int64_t x2 = pv[2]->sx, y2 = pv[2]->sy;
        int64_t area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (area <= 0) {
            g->cntCull++;
            g->cnt_culled++;
            continue;
        }
        int64_t shade = (lm[0] * f->n[0] + lm[1] * f->n[1] + lm[2] * f->n[2]) >> 14;
        int64_t lvl = (shade <= 0) ? 0 : ((shade * 7) >> 14);
        if (lvl > 6) lvl = 6;
        int64_t key = pv[0]->z + pv[1]->z + pv[2]->z + pv[3]->z;
        g->vis[vis_count].key = key;
        g->vis[vis_count].fidx = (uint16_t)fidx;
        g->vis[vis_count].col = (uint8_t)((f->base + lvl) & 0xFFu);
        g->vis[vis_count].lvl = lvl;
        vis_count++;
    }

    qsort(g->vis, vis_count, sizeof(g->vis[0]), vis_cmp);
    g->cnt_drawn = g->cntDraw = (uint16_t)vis_count;

    for (uint32_t vi = 0; vi < vis_count; ++vi) {
        const geo3d_vis_t *vf = &g->vis[vi];
        const geo3d_face_t *f = &g->fmem[vf->fidx];
        int textured = texOn && (f->base & 0x80u);
        const uint8_t *uv = textured ? g->tmem[vf->fidx] : NULL;
        int64_t xs[4], ys[4], us[4], vs[4];
        for (int k = 0; k < 4; ++k) {
            xs[k] = g->pmem[f->i[k]].sx;
            ys[k] = g->pmem[f->i[k]].sy;
            us[k] = uv ? (int64_t)uv[2 * k] : 0;
            vs[k] = uv ? (int64_t)uv[2 * k + 1] : 0;
        }
        int64_t ymin = ys[0], ymax = ys[0];
        for (int k = 1; k < 4; ++k) {
            if (ys[k] < ymin) ymin = ys[k];
            if (ys[k] > ymax) ymax = ys[k];
        }
        if (ymin < 0) ymin = 0;
        if (ymax > h - 1) ymax = h - 1;

        for (int64_t y = ymin; y <= ymax; ++y) {
            int have = 0;
            geo3d_cand_t L = {0, 0, 0}, R = {0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                int a = k, b = (k + 1) & 3;
                int64_t xa = xs[a], ya = ys[a], xb = xs[b], yb = ys[b];
                if (ya == yb) {
                    if (y == ya) {
                        geo3d_cand_t c1 = {xa, us[a] * 256, vs[a] * 256};
                        if (!have || c1.x < L.x) L = c1;
                        if (!have || c1.x > R.x) R = c1;
                        have = 1;
                        geo3d_cand_t c2 = {xb, us[b] * 256, vs[b] * 256};
                        if (c2.x < L.x) L = c2;
                        if (c2.x > R.x) R = c2;
                    }
                } else {
                    int64_t lo_y = (ya < yb) ? ya : yb;
                    int64_t hi_y = (ya > yb) ? ya : yb;
                    if (lo_y <= y && y <= hi_y) {
                        int64_t dy_ = y - ya, den = yb - ya;
                        int64_t x = xa + floor_div(dy_ * (xb - xa), den);
                        int64_t u = 0, v = 0;
                        if (textured) {
                            u = us[a] * 256 + floor_div(dy_ * (us[b] - us[a]) * 256, den);
                            v = vs[a] * 256 + floor_div(dy_ * (vs[b] - vs[a]) * 256, den);
                        }
                        geo3d_cand_t c = {x, u, v};
                        if (!have || c.x < L.x) L = c;
                        if (!have || c.x > R.x) R = c;
                        have = 1;
                    }
                }
            }
            if (!have) continue;
            int64_t xl = L.x, xr = R.x;
            int64_t cl = (xl > 0) ? xl : 0;
            int64_t cr = (xr < w - 1) ? xr : (w - 1);
            if (cl > cr) continue;
            int64_t dy = (y + g->ypage) & 0x7FF;
            if (!textured) {
                int64_t nx = cr - cl;
                uint8_t c_bytes[11] = {
                    (uint8_t)(cl & 0xFF), (uint8_t)((cl >> 8) & 1),
                    (uint8_t)(dy & 0xFF), (uint8_t)(dy >> 8),
                    (uint8_t)(nx & 0xFF), (uint8_t)((nx >> 8) & 7), 0, 0,
                    vf->col, 0, (uint8_t)(0x70 | g->lop)
                };
                emit_cmd(g, c_bytes, 11);
                continue;
            }
            int64_t du, dv;
            if (xr == xl) {
                du = dv = 0;
            } else {
                int64_t d = sat18(xr - xl);
                du = sat(floor_div(sat18(R.u - L.u), d), -32767, 32767);
                dv = sat(floor_div(sat18(R.v - L.v), d), -32767, 32767);
            }
            int64_t off = sat18(cl - xl);
            int64_t su = L.u + off * du;
            int64_t sv = L.v + off * dv;
            int64_t sx = ((int64_t)g->texx + (su >> 8)) & 0xFFF;
            int64_t sy = ((int64_t)g->texy + vf->lvl * g->tstride + (sv >> 8)) & 0x1FFF;
            int64_t nxp = cr - cl + 1;
            uint8_t c_bytes[19] = {
                (uint8_t)(sx & 0xFF), (uint8_t)(sx >> 8),
                (uint8_t)(sy & 0xFF), (uint8_t)(sy >> 8),
                (uint8_t)(cl & 0xFF), (uint8_t)((cl >> 8) & 1),
                (uint8_t)(dy & 0xFF), (uint8_t)(dy >> 8),
                (uint8_t)(nxp & 0xFF), (uint8_t)((nxp >> 8) & 7), 1, 0,
                vf->col, 0,
                (uint8_t)(du & 0xFF), (uint8_t)((du >> 8) & 0xFF),
                (uint8_t)(dv & 0xFF), (uint8_t)((dv >> 8) & 0xFF),
                (uint8_t)(0x30 | g->lop)
            };
            emit_cmd(g, c_bytes, 19);
        }
    }
}

/* ---- Autonomous Run Phase ----------------------------------------------- */

static void geo3d_start_run(geo3d_t *g, uint8_t ctrl)
{
    g->cnt_skipped = g->cntSkip = 0;
    g->cnt_drawn = g->cntDraw = 0;
    g->cnt_culled = g->cntCull = 0;
    g->running = 1;

    for (uint32_t k = 0; k < g->nvert; ++k) {
        uint32_t vi = (g->vfirst + k) & GEO3D_MASK;
        g->pmem[vi] = geo3d_project(g->cfg, g->vmem[vi]);
    }
    if (g->nvert != 0) {
        uint32_t last = (g->vfirst + g->nvert - 1u) & GEO3D_MASK;
        g->ivtx[0] = g->vmem[last][0];
        g->ivtx[1] = g->vmem[last][1];
        g->ivtx[2] = g->vmem[last][2];
        g->imm = g->pmem[last];
    }

    if (ctrl & 2) {
        geo3d_render_faces(g, (ctrl & 4) != 0);
    } else {
        geo3d_render_edges(g);
    }

    g->running = 0;
}

/* ---- Register Read / Write ---------------------------------------------- */

static uint8_t geo3d_read_status(const geo3d_t *g)
{
    uint8_t edge = (g->running) ? 0x04u : 0x00u;
    return (uint8_t)((g->imm.flags & 0xF0u) | edge | (g->running ? 0x01u : 0x00u));
}

static uint8_t geo3d_read_reg(geo3d_t *g, uint8_t idx)
{
    switch (idx) {
    case 0x30: return (uint8_t)g->imm.sx;
    case 0x31: return (uint8_t)(g->imm.sx >> 8);
    case 0x32: return (uint8_t)g->imm.sy;
    case 0x33: return (uint8_t)(g->imm.sy >> 8);
    case 0x34: return (uint8_t)g->imm.z;
    case 0x35: return (uint8_t)(g->imm.z >> 8);
    case 0x36: return (uint8_t)g->imm.flags;
    case 0x40: return (uint8_t)g->vaddr;
    case 0x41: return (uint8_t)g->eaddr;
    case 0x42: return (uint8_t)g->nvert;
    case 0x43: return (uint8_t)g->nedge;
    case 0x44: return g->color;
    case 0x45: return g->lop;
    case 0x46: return (uint8_t)g->ypage;
    case 0x47: return (uint8_t)(g->ypage >> 8);
    case 0x48: return geo3d_read_status(g);
    case 0x49: return GEO3D_ID;
    case 0x4A: return (uint8_t)(g->cntSkip & 0xFFu);
    case 0x4B: return (uint8_t)((g->cntSkip >> 8) & 0xFFu);
    case 0x4C: return (uint8_t)(g->cntDraw & 0xFFu);
    case 0x4D: return (uint8_t)((g->cntDraw >> 8) & 0xFFu);
    case 0x4E: return (uint8_t)(g->cntCull & 0xFFu);
    case 0x4F: return (uint8_t)((g->cntCull >> 8) & 0xFFu);
    case 0x54: return (uint8_t)g->vfirst;
    case 0x55: return (uint8_t)(g->vfirst >> 8);
    case 0x56: return (uint8_t)g->ffirst;
    case 0x57: return (uint8_t)(g->ffirst >> 8);
    case 0x58: return (uint8_t)g->faddr;
    case 0x59: return (uint8_t)g->nface;
    case 0x65: return (uint8_t)g->taddr;
    case 0x66: return (uint8_t)g->efirst;
    case 0x67: return (uint8_t)(g->efirst >> 8);
    case 0x68: return (uint8_t)(g->vaddr >> 8);
    case 0x69: return (uint8_t)(g->eaddr >> 8);
    case 0x6A: return (uint8_t)(g->nvert >> 8);
    case 0x6B: return (uint8_t)(g->nedge >> 8);
    case 0x6C: return (uint8_t)(g->faddr >> 8);
    case 0x6D: return (uint8_t)(g->nface >> 8);
    case 0x6E: return (uint8_t)(g->taddr >> 8);
    case 0x6F: return (uint8_t)((GEO3D_AW << 4) | (g->ext ? 1u : 0u));
    default:   return 0xFFu;
    }
}

static uint8_t geo3d_read_data(geo3d_t *g)
{
    uint8_t v = geo3d_read_reg(g, g->rptr);
    if (g->rptr == 0x36) {
        g->rptr = 0x30;
    } else if ((g->rptr >> 4) == 4) {
        g->rptr = 0x40 | ((g->rptr + 1) & 0x0F);
    } else if (g->rptr >= 0x30 && g->rptr < 0x36) {
        ++g->rptr;
    }
    return v;
}

uint8_t geo3d_read(geo3d_t *g, uint8_t port)
{
    if (!g) return 0xFFu;
    if ((port & 2u) == 0u) {
        /* Port 8Dh / 9Dh: Status */
        return geo3d_read_status(g);
    }
    /* Port 8Fh / 9Fh: Data */
    return geo3d_read_data(g);
}

void geo3d_write(geo3d_t *g, uint8_t port, uint8_t val)
{
    if (!g) return;
    if ((port & 2u) == 0u) {
        /* Port 8Dh / 9Dh: Index */
        g->widx = val;
        g->rptr = val;
        g->vbuf_len = 0;
        g->ebuf_len = 0;
        g->fbuf_len = 0;
        g->tbuf_len = 0;
        return;
    }

    /* Port 8Fh / 9Fh: Data */
    uint8_t i = g->widx;
    uint8_t b = val;

    if (i < 0x2A) {
        if ((i & 1) == 0) {
            g->lo = b;
        } else {
            uint32_t word = ((uint32_t)b << 8) | g->lo;
            uint32_t wi = i >> 1;
            if (wi < 18) {
                g->cfg[wi] = (wi == 12) ? (int64_t)word : signed16(word);
            } else {
                g->ivtx[wi - 18] = signed16(word);
                if (wi == 20) {
                    g->imm = geo3d_project(g->cfg, g->ivtx);
                    g->rptr = 0x30;
                }
            }
        }
    } else {
        #define LO8(r) do { (r) = (uint16_t)(((r) & 0x300u) | b); } while (0)
        #define HI2(r) do { (r) = (uint16_t)(((r) & 0x0FFu) | (((uint16_t)(b & 3u)) << 8)); } while (0)

        switch (i) {
        case 0x40: LO8(g->vaddr); break;
        case 0x41: LO8(g->eaddr); break;
        case 0x42: LO8(g->nvert); break;
        case 0x43: LO8(g->nedge); break;
        case 0x44: g->color = b; break;
        case 0x45: g->lop = b & 0x0F; break;
        case 0x46: g->ypage = (g->ypage & 0x700u) | b; break;
        case 0x47: g->ypage = (g->ypage & 0x0FFu) | (((uint32_t)(b & 7u)) << 8); break;
        case 0x48:
            g->ctrlFace = (b & 2) != 0;
            g->ctrlTex  = (b & 4) != 0;
            if ((b & 1) && !g->running) geo3d_start_run(g, b);
            break;
        case 0x50:
            g->vbuf[g->vbuf_len++] = b;
            if (g->vbuf_len == 6) {
                for (int k = 0; k < 3; ++k) {
                    uint32_t w = (uint32_t)g->vbuf[2 * k] | ((uint32_t)g->vbuf[2 * k + 1] << 8);
                    g->vmem[g->vaddr][k] = signed16(w);
                }
                g->vaddr = bump(g, g->vaddr);
                g->vbuf_len = 0;
            }
            break;
        case 0x51:
            g->ebuf[g->ebuf_len++] = b;
            if (g->ebuf_len == (g->ext ? 3u : 2u)) {
                uint32_t hi = g->ext ? g->ebuf[2] : 0;
                g->emem[g->eaddr][0] = (uint16_t)(g->ebuf[0] | ((hi & 3u) << 8));
                g->emem[g->eaddr][1] = (uint16_t)(g->ebuf[1] | (((hi >> 2) & 3u) << 8));
                g->eaddr = bump(g, g->eaddr);
                g->ebuf_len = 0;
            }
            break;
        case 0x52:
            g->fbuf[g->fbuf_len++] = b;
            if (g->fbuf_len == (g->ext ? 12u : 11u)) {
                geo3d_face_t *fc = &g->fmem[g->faddr];
                uint32_t hi = g->ext ? g->fbuf[4] : 0;
                uint32_t o = g->ext ? 5 : 4;
                for (int k = 0; k < 4; ++k) {
                    fc->i[k] = (uint16_t)(g->fbuf[k] | (((hi >> (2 * k)) & 3u) << 8));
                }
                for (int k = 0; k < 3; ++k) {
                    uint32_t nw = (uint32_t)g->fbuf[o + 2 * k] | ((uint32_t)g->fbuf[o + 1 + 2 * k] << 8);
                    fc->n[k] = signed16(nw);
                }
                fc->base = g->fbuf[o + 6];
                g->faddr = bump(g, g->faddr);
                g->fbuf_len = 0;
            }
            break;
        case 0x53:
            g->tbuf[g->tbuf_len++] = b;
            if (g->tbuf_len == 8) {
                memcpy(g->tmem[g->taddr], g->tbuf, 8);
                g->taddr = bump(g, g->taddr);
                g->tbuf_len = 0;
            }
            break;
        case 0x54: LO8(g->vfirst); break;
        case 0x55: HI2(g->vfirst); break;
        case 0x56: LO8(g->ffirst); break;
        case 0x57: HI2(g->ffirst); break;
        case 0x58: LO8(g->faddr); break;
        case 0x59: LO8(g->nface); break;
        case 0x5A: case 0x5C: case 0x5E: g->lo = b; break;
        case 0x5B: case 0x5D: case 0x5F:
            g->light[(i - 0x5B) / 2] = signed16(((uint32_t)b << 8) | g->lo);
            break;
        case 0x60: g->texx = (g->texx & 0x100u) | b; break;
        case 0x61: g->texx = (g->texx & 0x0FFu) | (((uint32_t)(b & 1u)) << 8); break;
        case 0x62: g->texy = (g->texy & 0x1F00u) | b; break;
        case 0x63: g->texy = (g->texy & 0x00FFu) | (((uint32_t)(b & 0x1Fu)) << 8); break;
        case 0x64: g->tstride = b; break;
        case 0x65: LO8(g->taddr); break;
        case 0x66: LO8(g->efirst); break;
        case 0x67: HI2(g->efirst); break;
        case 0x68: HI2(g->vaddr); break;
        case 0x69: HI2(g->eaddr); break;
        case 0x6A: HI2(g->nvert); break;
        case 0x6B: HI2(g->nedge); break;
        case 0x6C: HI2(g->faddr); break;
        case 0x6D: HI2(g->nface); break;
        case 0x6E: HI2(g->taddr); break;
        case 0x6F: g->ext = (b & 1) != 0; break;
        default: break;
        }

        #undef LO8
        #undef HI2
    }

    /* Index advance matching RTL next_idx */
    if ((i >> 4) == 4) {
        g->widx = 0x40 | ((i + 1) & 0x0F);
    } else if ((i >> 2) == 0x15) {
        g->widx = 0x54 | ((i + 1) & 0x03);
    } else if ((i >> 3) == 0x0B) {
        g->widx = 0x58 | ((i + 1) & 0x07);
    } else if ((i >> 3) == 0x0C) {
        g->widx = 0x60 | ((i + 1) & 0x07);
    } else if ((i >> 3) == 0x0D) {
        g->widx = 0x68 | ((i + 1) & 0x07);
    } else if (i == 0x29) {
        g->widx = 0x24;
    } else if (i < 0x40) {
        g->widx = i + 1;
    }
}
