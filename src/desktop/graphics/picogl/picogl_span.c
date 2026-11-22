/*
 * picogl_span.c — Q10 camera, clip, band bins, and the span fill.
 *
 * The fill walks pixel centers with an integer edge test. A larger iz wins.
 * Core 1 runs the same fill when a target is bound. The host test calls
 * picogl_raster_band on this core. The edge divide stays in software.
 * A captured list can keep each edge's remainder, so a later band steps
 * it instead of dividing again. Each later row adds a quotient computed
 * once per edge, and does not divide. The SIO divider result is undefined
 * until 8 cycles after the divisor write, and reading it early tore
 * the spans.
 */
#include "picogl.h"

#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_MAIN__)
#define NEXUS_BARRIER() __asm__ volatile("dmb sy" ::: "memory")
#endif
#include <btron/nexus_ring.h>

#include <math.h>
#include <string.h>
#if !defined(PICO_REAL_HW)
#include <sched.h>
#endif
#if defined(PICO_REAL_HW)
#include "pico_regs.h"
#endif

/* 128 slots cover one Quake hall submit (~116 tris) and a burst of a
 * gear strip. It is not a second bucket set: the bins stay one 7,680-byte
 * table. 128 * 24 = 3,072 bytes. snd still refuses instead of spinning.
 * The producer waits outside snd until one slot frees. */
#define PICOGL_SPAN_SLOTS 128
NEXUS_RING_DEFINE(picogl_span_ring_t, picogl_tri_t, PICOGL_SPAN_SLOTS)



#define PICOGL_VERTS 40

typedef char picogl_tri_must_be_24[(sizeof(picogl_tri_t) == 24) ? 1 : -1];

typedef struct {
    float x, y, z, w;
    float u, v;
} pv_t;

static float s_mvp[16];
static int32_t s_mq[16];
static int   s_mq_ok;
static int   s_vp_w, s_vp_h;
/* Destination of the stretch. 0 means the frustum uses the viewport. */
static int   s_disp_w, s_disp_h;
static int   s_cull;
static int   s_depth = 1;
static volatile int s_drop;
static int   s_fills;
static int   s_glyphs;
static volatile int s_tris;
static volatile int s_spans;
static int   s_band_n[PICOGL_BANDS];
static picogl_tri_t s_band[PICOGL_BANDS][PICOGL_BAND_CAP];

static uint16_t *s_tgt_rgb;
static int32_t  *s_tgt_iz;
static int       s_tgt_w, s_tgt_h, s_tgt_stride;
static picogl_tri_t *s_cap;
static int       s_cap_n, s_cap_max;
static int       s_from_world;
static uint16_t *s_push_rgb;
static int32_t  *s_push_iz;
static int       s_push_w, s_push_h, s_push_stride;
static int       s_push_vp_w, s_push_vp_h, s_push_depth, s_pushed;
static picogl_span_ring_t s_span_ring;
static volatile uint32_t s_span_done;
/* One band of coverage, 320 bits by 16 rows. Armed only while s_cover
 * is set, and only for a band no wider than 320. */
static int s_cover;
static uint32_t s_cov[16][10];
/* Far half of a Quake band. Core 1 writes this. Core 0 keeps s_cov. */
static uint32_t s_cov_far[16][10];
/* Quake keeps these. Gears leave them unset, so a gear triangle still
 * divides at the row it starts on. y < 0 means that slot has no remainder.
 * dq,dr are the row step. They depend on the edge, not the row, so a
 * later band installs them and does not divide. */
static int *s_edge_q;
static int *s_edge_r;
static int16_t *s_edge_y;
static int16_t *s_edge_dq;
static uint16_t *s_edge_dr;
static int s_edge_n;

/* One band paint. Two of these run together: core 0 writes the near
 * half into the DMA strip, core 1 writes the far half into another
 * band. Coverage and the pixel buffer are not shared. */
typedef struct span_px {
    uint16_t *rgb;
    int32_t *iz;
    int stride;
    int dest_w, dest_h;
    int xoff, yoff;
    int vw, vh;
    int swap;
    int cover;
    uint32_t (*cov)[10];
    int cov_left;
    volatile int *span_ctr;
} span_px;

static void mat_ident(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_mul(float *out, const float *a, const float *b)
{
    float t[16];
    int c, r;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            t[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    memcpy(out, t, sizeof t);
}

static void mat_frustum(float *m, float l, float r, float b, float t, float n, float f)
{
    float rl = r - l, tb = t - b, fn = f - n;
    memset(m, 0, 16 * sizeof(float));
    m[0]  = (2.0f * n) / rl;
    m[5]  = (2.0f * n) / tb;
    m[8]  = (r + l) / rl;
    m[9]  = (t + b) / tb;
    m[10] = -(f + n) / fn;
    m[11] = -1.0f;
    m[14] = -(2.0f * f * n) / fn;
}

static void mat_rotate(float *m, float deg, float x, float y, float z)
{
    float R[16], c, s, rad, len, xx, yy, zz, xy, xz, yz, xs, ys, zs, c1;
    rad = deg * (3.14159265358979323846f / 180.0f);
    c = cosf(rad);
    s = sinf(rad);
    len = sqrtf(x * x + y * y + z * z);
    if (len < 1e-8f) return;
    x /= len; y /= len; z /= len;
    c1 = 1.0f - c;
    xx = x * x; yy = y * y; zz = z * z;
    xy = x * y; xz = x * z; yz = y * z;
    xs = x * s; ys = y * s; zs = z * s;
    mat_ident(R);
    R[0] = xx * c1 + c;  R[4] = xy * c1 - zs; R[8]  = xz * c1 + ys;
    R[1] = xy * c1 + zs; R[5] = yy * c1 + c;  R[9]  = yz * c1 - xs;
    R[2] = xz * c1 - ys; R[6] = yz * c1 + xs; R[10] = zz * c1 + c;
    mat_mul(m, m, R);
}

static void mat_translate(float *m, float x, float y, float z)
{
    float t[16];
    mat_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    mat_mul(m, m, t);
}

static void xform(pv_t *o, float x, float y, float z, float u, float v)
{
    const float *m = s_mvp;
    o->x = m[0] * x + m[4] * y + m[8]  * z + m[12];
    o->y = m[1] * x + m[5] * y + m[9]  * z + m[13];
    o->z = m[2] * x + m[6] * y + m[10] * z + m[14];
    o->w = m[3] * x + m[7] * y + m[11] * z + m[15];
    o->u = u;
    o->v = v;
}

static float plane_dist(const pv_t *v, int plane)
{
    switch (plane) {
    case 0: return v->w + v->x;
    case 1: return v->w - v->x;
    case 2: return v->w + v->y;
    case 3: return v->w - v->y;
    case 4: return v->w + v->z;
    default: return v->w - v->z;
    }
}

static void lerp_vert(pv_t *o, const pv_t *a, const pv_t *b, float t)
{
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
    o->u = a->u + (b->u - a->u) * t;
    o->v = a->v + (b->v - a->v) * t;
}

static int clip_plane(pv_t *v, int n, int plane)
{
    pv_t out[PICOGL_VERTS];
    int i, m = 0;
    for (i = 0; i < n; i++) {
        const pv_t *a = &v[i];
        const pv_t *b = &v[(i + 1) % n];
        float da = plane_dist(a, plane);
        float db = plane_dist(b, plane);
        int ina = da >= 0.0f;
        int inb = db >= 0.0f;
        if (ina && inb) {
            if (m < PICOGL_VERTS) out[m++] = *b;
        } else if (ina && !inb) {
            float t = da / (da - db);
            if (m < PICOGL_VERTS) lerp_vert(&out[m++], a, b, t);
        } else if (!ina && inb) {
            float t = da / (da - db);
            if (m < PICOGL_VERTS) lerp_vert(&out[m++], a, b, t);
            if (m < PICOGL_VERTS) out[m++] = *b;
        }
    }
    if (m > 0) memcpy(v, out, (size_t)m * sizeof(pv_t));
    return m;
}

static int clip_poly(pv_t *v, int n)
{
    int p;
    for (p = 0; p < 6; p++) {
        n = clip_plane(v, n, p);
        if (n < 3) return 0;
    }
    return n;
}

static uint16_t rgb565(uint32_t rgba)
{
    /* rgba is R | G<<8 | B<<16 | A<<24, the packing r_brush.c already uses. */
    unsigned r = (rgba & 0xFFu) >> 3;
    unsigned g = ((rgba >> 8) & 0xFFu) >> 2;
    unsigned b = ((rgba >> 16) & 0xFFu) >> 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int qround(float v)
{
    if (v >= 0.0f) return (int)(v + 0.5f);
    return (int)(v - 0.5f);
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Pixel-center half-plane. A zero edge stays inside. The row span is the
 * intersection of the three half-planes, so the fill does not test the
 * bbox one pixel at a time. test_picogl_raster.c checks the same centers. */
static inline __attribute__((always_inline)) int edge_cross(int ax, int ay, int bx, int by, int px, int py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int tri_area2(const picogl_tri_t *t)
{
    return edge_cross(t->x0 * 2, t->y0 * 2, t->x1 * 2, t->y1 * 2, t->x2 * 2, t->y2 * 2);
}

static inline __attribute__((always_inline)) int floor_div_pos(int n, int d)
{
    if (n >= 0) return n / d;
    return -((d - 1 - n) / d);
}

/* c = dx*(py-ay) - dy*((2*x+1)-ax) >= 0, with doubled vertex coordinates.
 * The first row divides once per edge. Later rows add dq, and dr which
 * stays in [0, d), so one compare puts the remainder back. */
typedef struct {
    int kind; /* 1 = x max, 2 = x min, 3 = horizontal */
    int q, r, d, step;
    int dq, dr;
    int ky, dx;
} span_edge_t;

/* dq*d + dr == step, and 0 <= dr < d. A row is then one add. */
static void edge_prep(span_edge_t *e)
{
    int step = e->step;
    int d = e->d;
    int dq;
    if (d <= 0) {
        e->dq = 0;
        e->dr = 0;
        return;
    }
    if (step >= 0 && step < d) {
        e->dq = 0;
        e->dr = step;
        return;
    }
    if (step < 0 && step > -d) {
        e->dq = -1;
        e->dr = step + d;
        return;
    }
    dq = floor_div_pos(step, d);
    e->dq = dq;
    e->dr = step - dq * d;
}

/* have = 0 divides. have = 1 installs the remainder saved at this py.
 * That remainder is what edge_init would produce, so the half-planes
 * do not move. A horizontal edge recomputes ky and ignores q, r.
 * A resumed non-horizontal edge does not recompute ky. */
/* have_step installs dq,dr from the previous band. 0xFFFF in sdr means
 * the step did not fit in the stored pair and this band divides. */
static void edge_at(span_edge_t *e, int ax, int ay, int bx, int by, int py,
                    int have, int sq, int sr, int have_step, int sdq, int sdr)
{
    int dx = bx - ax;
    int dy = by - ay;
    e->kind = 0;
    e->q = e->r = e->d = e->step = 0;
    e->dq = e->dr = 0;
    e->ky = 0;
    e->dx = dx;
    if (dy == 0) {
        e->ky = dx * (py - ay);
        e->kind = 3;
        return;
    }
    if (dy > 0) {
        int d = dy << 1;
        e->kind = 1;
        e->d = d;
        e->step = dx << 1;
        if (have) {
            e->q = sq;
            e->r = sr;
        } else {
            int ky = dx * (py - ay) - dy * (1 - ax);
            int q = floor_div_pos(ky, d);
            e->ky = ky;
            e->q = q;
            e->r = ky - q * d;
        }
    } else {
        int d = (-dy) << 1;
        e->kind = 2;
        e->d = d;
        e->step = -(dx << 1);
        if (have) {
            e->q = sq;
            e->r = sr;
        } else {
            int ky = dx * (py - ay) - dy * (1 - ax);
            int n = -ky;
            int m = n + d - 1;
            int q = floor_div_pos(m, d);
            e->ky = ky;
            e->q = q;
            e->r = m - q * d;
        }
    }
    if (have && have_step && sdr != 0xFFFF) {
        e->dq = sdq;
        e->dr = sdr;
        return;
    }
    edge_prep(e);
}

static void edge_init(span_edge_t *e, int ax, int ay, int bx, int by, int py)
{
    edge_at(e, ax, ay, bx, by, py, 0, 0, 0, 0, 0, 0);
}

static void edge_step_save(int slot, const span_edge_t *e0,
                           const span_edge_t *e1, const span_edge_t *e2)
{
    const span_edge_t *e[3];
    int k;
    if (!s_edge_dq || !s_edge_dr || slot < 0) return;
    e[0] = e0;
    e[1] = e1;
    e[2] = e2;
    for (k = 0; k < 3; k++) {
        int i = slot * 3 + k;
        int dq = e[k]->dq;
        int dr = e[k]->dr;
        if (dq != (int)(int16_t)dq || dr < 0 || dr > 65534) {
            s_edge_dq[i] = 0;
            s_edge_dr[i] = 0xFFFFu;
        } else {
            s_edge_dq[i] = (int16_t)dq;
            s_edge_dr[i] = (uint16_t)dr;
        }
    }
}

static void edge_advance(span_edge_t *e)
{
    if (e->kind == 3) {
        e->ky += e->dx << 1;
        return;
    }
    if (e->kind == 0 || e->d <= 0) return;
    e->q += e->dq;
    e->r += e->dr;
    if (e->r >= e->d) {
        e->r -= e->d;
        e->q++;
    }
}

static void edge_apply(const span_edge_t *e, int *lo, int *hi)
{
    if (e->kind == 1) {
        if (e->q < *hi) *hi = e->q;
    } else if (e->kind == 2) {
        if (e->q > *lo) *lo = e->q;
    } else if (e->kind == 3) {
        if (e->ky < 0) *hi = *lo - 1;
    }
}

static int cover_live(const span_px *px)
{
    return px && px->cover && px->rgb && px->cov
        && px->dest_h > 0 && px->dest_h <= 16
        && px->dest_w > 0 && px->dest_w <= 320;
}

/* 1 when every bit in [lo, hi] on this band row is already written. */
static int span_full(const span_px *px, int dy, int lo, int hi)
{
    uint32_t *bits;
    int x;
    if (!px || !px->cov || (unsigned)dy >= 16u) return 0;
    if (lo < 0) lo = 0;
    if (hi > 319) hi = 319;
    if (lo > hi) return 1;
    bits = px->cov[dy];
    x = lo;
    while (x <= hi) {
        int bit = x & 31;
        int n = 32 - bit;
        uint32_t mask;
        if (x + n - 1 > hi) n = hi - x + 1;
        mask = (n == 32) ? 0xffffffffu : (((1u << n) - 1u) << bit);
        if ((bits[x >> 5] & mask) != mask) return 0;
        x += n;
    }
    return 1;
}

static int cover_full(const span_px *px, int x0, int x1, int y0, int y1)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 319) x1 = 319;
    if (y1 > 15) y1 = 15;
    if (x0 > x1 || y0 > y1) return 1;
    for (y = y0; y <= y1; y++) {
        if (!span_full(px, y, x0, x1)) return 0;
    }
    return 1;
}

#if defined(PICO_REAL_HW)
__attribute__((section(".scratch_x"), noinline))
#endif
static void paint_span_write(uint16_t *row, int32_t *iz, int lo, int hi,
                             int32_t izv, uint16_t rgb, int swap)
{
    int x, wrote = 0;
    uint16_t pix = swap ? (uint16_t)((rgb >> 8) | (rgb << 8)) : rgb;
    for (x = lo; x <= hi; x++) {
        if (s_depth) {
            if (!iz || !(izv > iz[x])) continue;
            iz[x] = izv;
        }
        row[x] = pix;
        wrote = 1;
    }
    if (wrote) s_spans++;
}

#if defined(PICO_REAL_HW)
__attribute__((section(".scratch_x"), noinline))
#endif
/* Front-to-back. The caller sorted by descending flat iz and kept
 * earlier triangles on a tie, which is the same rule as a strict iz
 * test (a later triangle cannot win a pixel an earlier one wrote).
 * A full covered word is one compare. A fully free word is halfword
 * stores, not a uint32 alias of the row. A mixed word walks bits.
 * The bit is set only for a pixel this call writes. LIFT does not
 * come here. iz is not loaded or stored. */
static void paint_span_cover(span_px *px, int lo, int hi,
                             int32_t izv, uint16_t rgb, int dy)
{
    int x, wrote = 0;
    uint16_t *row;
    uint16_t pix;
    uint32_t *bits;
    (void)izv;
    if (!px || !px->rgb || !px->cov || (unsigned)dy >= 16u || lo > hi) return;
    row = px->rgb + dy * px->stride;
    pix = px->swap ? (uint16_t)((rgb >> 8) | (rgb << 8)) : rgb;
    bits = px->cov[dy];
    x = lo;
    while (x <= hi) {
        int bit = x & 31;
        int n = 32 - bit;
        uint32_t *wordp;
        uint32_t word, mask;
        if (x + n - 1 > hi) n = hi - x + 1;
        wordp = bits + (x >> 5);
        word = *wordp;
        mask = (n == 32) ? 0xffffffffu : (((1u << n) - 1u) << bit);
        if ((word & mask) == mask) {
            x += n;
            continue;
        }
        if ((word & mask) == 0) {
            uint16_t *p = row + x;
            int i = n;
            if (n == 32) {
                do {
                    p[0] = pix; p[1] = pix; p[2] = pix; p[3] = pix;
                    p += 4;
                    i -= 4;
                } while (i);
            } else {
                do { *p++ = pix; } while (--i);
            }
            *wordp = word | mask;
            wrote = 1;
            px->cov_left -= n;
            x += n;
            continue;
        }
        {
            int i, nwrite = 0;
            uint32_t dirty = 0;
            for (i = 0; i < n; i++) {
                uint32_t m = 1u << (bit + i);
                if (word & m) continue;
                row[x + i] = pix;
                dirty |= m;
                nwrite++;
            }
            if (dirty) {
                *wordp = word | dirty;
                wrote = 1;
                px->cov_left -= nwrite;
            }
            x += n;
        }
    }
    if (wrote && px->span_ctr) (*px->span_ctr)++;
}

#if defined(PICO_REAL_HW)
__attribute__((section(".scratch_x"), noinline))
#endif
static void paint_span_lift(uint16_t *row, int lo, int hi, uint16_t lift)
{
    unsigned sr = (lift >> 11) & 31u;
    unsigned sg = (lift >> 5) & 63u;
    unsigned sb = lift & 31u;
    int x, wrote = 0;
    /* The XMB strip is panel order, byte-swapped RGB565. lift is native. */
    for (x = lo; x <= hi; x++) {
        uint16_t d = (uint16_t)((row[x] >> 8) | (row[x] << 8));
        unsigned dr = (d >> 11) & 31u;
        unsigned dg = (d >> 5) & 63u;
        unsigned db = d & 31u;
        unsigned out;
        dr += (dr * sr) >> 4;
        dg += (dg * sg) >> 5;
        db += (db * sb) >> 4;
        if (dr > 31u) dr = 31u;
        if (dg > 63u) dg = 63u;
        if (db > 31u) db = 31u;
        out = (dr << 11) | (dg << 5) | db;
        row[x] = (uint16_t)((out >> 8) | (out << 8));
        wrote = 1;
    }
    if (wrote) s_spans++;
}

static void paint_tri_target(const picogl_tri_t *t, int slot, span_px *px)
{
    int y, ymin, ymax, py, area, have, have_step;
    int x0, y0, x1, y1, x2, y2;
    int view_w, view_h, xoff, yoff, dest_w, dest_h, stride, swap;
    int sdq0, sdr0, sdq1, sdr1, sdq2, sdr2;
    uint16_t *rgb;
    int32_t *izbase;
    span_edge_t e0, e1, e2;
    if (!px || !px->rgb) return;
    rgb = px->rgb;
    izbase = px->iz;
    stride = px->stride;
    dest_w = px->dest_w;
    dest_h = px->dest_h;
    view_w = px->vw;
    view_h = px->vh;
    xoff = px->xoff;
    yoff = px->yoff;
    swap = px->swap;
    if (s_depth && !izbase && !px->cover) return;
    ymin = t->y0;
    if (t->y1 < ymin) ymin = t->y1;
    if (t->y2 < ymin) ymin = t->y2;
    ymax = t->y0;
    if (t->y1 > ymax) ymax = t->y1;
    if (t->y2 > ymax) ymax = t->y2;
    if (ymin < 0) ymin = 0;
    if (ymax >= view_h) ymax = view_h - 1;
    if (ymin > ymax) return;
    /* A band is dest_h rows of the view, starting at -yoff. edge_init at
     * that row is the same edge state as walking every row above it, so
     * a 320-tall wall is filled once per band instead of once per pixel
     * of its whole height. A full target has yoff 0 and dest_h == view_h,
     * and this clamp does not move. */
    {
        int top = -yoff;
        int bot = dest_h - yoff;
        if (ymin < top) ymin = top;
        if (ymax >= bot) ymax = bot - 1;
        if (ymin > ymax) return;
    }
    /* A nearer triangle already owns this sealed box. Skip the divides. */
    if (cover_live(px) && (t->flags & PICOGL_LIFT) == 0) {
        int bx0 = t->x0;
        int bx1 = t->x0;
        if (t->x1 < bx0) bx0 = t->x1;
        if (t->x2 < bx0) bx0 = t->x2;
        if (t->x1 > bx1) bx1 = t->x1;
        if (t->x2 > bx1) bx1 = t->x2;
        if (t->flags & PICOGL_SEAL) {
            if (bx0 > 0) bx0--;
            if (bx1 + 1 < view_w) bx1++;
        }
        if (bx0 < 0) bx0 = 0;
        if (bx1 >= view_w) bx1 = view_w - 1;
        if (cover_full(px, bx0 + xoff, bx1 + xoff, ymin + yoff, ymax + yoff))
            return;
    }
    x0 = t->x0 * 2; y0 = t->y0 * 2;
    x1 = t->x1 * 2; y1 = t->y1 * 2;
    x2 = t->x2 * 2; y2 = t->y2 * 2;
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) return;
    py = ymin * 2 + 1;
    /* The saved remainder is the state edge_init would compute at ymin.
     * Bands run downward, so the next band of this triangle steps it. */
    have = 0;
    have_step = 0;
    sdq0 = sdr0 = sdq1 = sdr1 = sdq2 = sdr2 = 0;
    if (slot >= 0 && s_edge_y && s_edge_y[slot] == (int16_t)ymin)
        have = 1;
    if (have && s_edge_dq && s_edge_dr && slot < s_edge_n) {
        int b = slot * 3;
        have_step = 1;
        sdq0 = s_edge_dq[b];
        sdr0 = s_edge_dr[b];
        sdq1 = s_edge_dq[b + 1];
        sdr1 = s_edge_dr[b + 1];
        sdq2 = s_edge_dq[b + 2];
        sdr2 = s_edge_dr[b + 2];
    }
    if (area > 0) {
        edge_at(&e0, x0, y0, x1, y1, py, have,
                have ? s_edge_q[slot * 3] : 0, have ? s_edge_r[slot * 3] : 0,
                have_step, sdq0, sdr0);
        edge_at(&e1, x1, y1, x2, y2, py, have,
                have ? s_edge_q[slot * 3 + 1] : 0, have ? s_edge_r[slot * 3 + 1] : 0,
                have_step, sdq1, sdr1);
        edge_at(&e2, x2, y2, x0, y0, py, have,
                have ? s_edge_q[slot * 3 + 2] : 0, have ? s_edge_r[slot * 3 + 2] : 0,
                have_step, sdq2, sdr2);
    } else {
        edge_at(&e0, x1, y1, x0, y0, py, have,
                have ? s_edge_q[slot * 3] : 0, have ? s_edge_r[slot * 3] : 0,
                have_step, sdq0, sdr0);
        edge_at(&e1, x2, y2, x1, y1, py, have,
                have ? s_edge_q[slot * 3 + 1] : 0, have ? s_edge_r[slot * 3 + 1] : 0,
                have_step, sdq1, sdr1);
        edge_at(&e2, x0, y0, x2, y2, py, have,
                have ? s_edge_q[slot * 3 + 2] : 0, have ? s_edge_r[slot * 3 + 2] : 0,
                have_step, sdq2, sdr2);
    }
    {
        int box_lo = t->x0;
        int box_hi = t->x0;
        if (t->x1 < box_lo) box_lo = t->x1;
        if (t->x2 < box_lo) box_lo = t->x2;
        if (t->x1 > box_hi) box_hi = t->x1;
        if (t->x2 > box_hi) box_hi = t->x2;
        if (box_lo < 0) box_lo = 0;
        if (box_hi >= view_w) box_hi = view_w - 1;
    for (y = ymin; y <= ymax; y++) {
        int lo = box_lo;
        int hi = box_hi;
        int dy = y + yoff;
        int draw = 1;
        edge_apply(&e0, &lo, &hi);
        edge_apply(&e1, &lo, &hi);
        edge_apply(&e2, &lo, &hi);
        /* The integer projector rounds a shared edge so the pixel
         * center falls outside both faces. Cover one more pixel. */
        if ((t->flags & PICOGL_SEAL) && lo <= hi) {
            if (lo > 0) lo--;
            if (hi + 1 < view_w) hi++;
        }
        if (dy < 0 || dy >= dest_h) draw = 0;
        lo += xoff;
        hi += xoff;
        if (lo < 0) lo = 0;
        if (hi >= dest_w) hi = dest_w - 1;
        if (draw && lo <= hi) {
            uint16_t *row = rgb + dy * stride;
            int32_t *iz = izbase ? izbase + dy * stride : 0;
            if (t->flags & PICOGL_LIFT)
                paint_span_lift(row, lo, hi, t->rgb);
            else if (!cover_live(px))
                paint_span_write(row, iz, lo, hi, t->iz, t->rgb, swap);
            else
                paint_span_cover(px, lo, hi, t->iz, t->rgb, dy);
        }
        edge_advance(&e0);
        edge_advance(&e1);
        edge_advance(&e2);
    }
    }
    if (slot >= 0 && s_edge_q && s_edge_r && s_edge_y) {
        s_edge_q[slot * 3] = e0.q;
        s_edge_r[slot * 3] = e0.r;
        s_edge_q[slot * 3 + 1] = e1.q;
        s_edge_r[slot * 3 + 1] = e1.r;
        s_edge_q[slot * 3 + 2] = e2.q;
        s_edge_r[slot * 3 + 2] = e2.r;
        s_edge_y[slot] = (int16_t)(ymax + 1);
        edge_step_save(slot, &e0, &e1, &e2);
    }
}

static void px_target(span_px *px)
{
    memset(px, 0, sizeof *px);
    px->rgb = s_tgt_rgb;
    px->iz = s_tgt_iz;
    px->stride = s_tgt_stride;
    px->dest_w = s_tgt_w;
    px->dest_h = s_tgt_h;
    px->vw = s_tgt_w;
    px->vh = s_tgt_h;
    px->span_ctr = &s_spans;
}

void picogl_span_drain(void)
{
    /* The slot stays owned by the consumer until the paint finishes.
     * Tail moves after the write, so the producer cannot reuse it and
     * the triangle is not copied out of the ring. */
    span_px px;
    px_target(&px);
    for (;;) {
        uint32_t t = s_span_ring.tail;
        uint32_t h = s_span_ring.head;
        const picogl_tri_t *tri;
        if (t == h) return;
        NEXUS_BARRIER();
        tri = &s_span_ring.slot[t & (PICOGL_SPAN_SLOTS - 1)];
        paint_tri_target(tri, -1, &px);
        NEXUS_BARRIER();
        s_span_ring.tail = t + 1u;
        s_span_done = t + 1u;
        NEXUS_BARRIER();
    }
}

uint32_t picogl_span_posted(void) { return s_span_ring.head; }
uint32_t picogl_span_finished(void) { return s_span_done; }
int picogl_span_queued(void)
{
    uint32_t in_ring = picogl_span_ring_t_len(&s_span_ring);
    uint32_t behind = s_span_ring.head - s_span_done;
    if (behind < in_ring) behind = in_ring;
    return (int)behind;
}
unsigned picogl_span_refused(void)
{
    /* Paint reads the slot in place. rcv would publish tail early. */
    if (0) {
        picogl_tri_t dump;
        (void)picogl_span_ring_t_rcv(&s_span_ring, &dump);
    }
    return s_span_ring.refused;
}

static void post_target(const picogl_tri_t *tri)
{
    int st, spins;
    if (s_cap) {
        if (s_cap_n < s_cap_max) s_cap[s_cap_n++] = *tri;
        else s_drop++;
        return;
    }
    pico_core1_launch();
    st = pico_core1_state();
    if (st == 1 || st == 2) {
        for (spins = 0; spins < 200000; spins++) {
            uint32_t h = s_span_ring.head;
            uint32_t t = s_span_ring.tail;
            if ((uint32_t)(h - t) < PICOGL_SPAN_SLOTS &&
                picogl_span_ring_t_snd(&s_span_ring, tri) == 0) {
                pico_core1_sev();
                return;
            }
            /* One slot, not the whole ring. Core 1 keeps the tris it
             * already has. Core 0 resumes the next vertex when one
             * slot frees. A full reap here would serialize the cores. */
            pico_core1_sev();
#if !defined(PICO_REAL_HW)
            sched_yield();
#endif
        }
        pico_core1_reap();
        st = pico_core1_state();
        if ((st == 1 || st == 2) &&
            picogl_span_ring_t_snd(&s_span_ring, tri) == 0) {
            pico_core1_sev();
            return;
        }
    }
    {
        span_px px;
        px_target(&px);
        paint_tri_target(tri, -1, &px);
    }
}

void picogl_flush_target_now(void)
{
    picogl_span_drain();
}

void picogl_flush_target(void)
{
    pico_core1_reap();
}

static void push_tri(const pv_t *a, const pv_t *b, const pv_t *c,
                     uint32_t rgba, uint8_t flags, int tex)
{
    float sx[3], sy[3], izf;
    const pv_t *p[3];
    int y0, y1, band0, band1, band;
    int xs[3], ys[3];
    picogl_tri_t tri;
    int i;

    if (s_vp_w <= 0 || s_vp_h <= 0) return;
    p[0] = a; p[1] = b; p[2] = c;
    izf = 0.0f;
    for (i = 0; i < 3; i++) {
        float w = p[i]->w;
        if (!(w >= 1.0e-4f)) return;
        {
            float nx = p[i]->x / w;
            float ny = p[i]->y / w;
            /* A vertex the clipper should have cut. Clamping it to the
             * int16 box turns the triangle into a screen-edge spike. */
            if (nx < -1.25f || nx > 1.25f || ny < -1.25f || ny > 1.25f)
                return;
            sx[i] = (nx * 0.5f + 0.5f) * (float)s_vp_w;
            sy[i] = (0.5f - ny * 0.5f) * (float)s_vp_h;
        }
        if (sx[i] != sx[i] || sy[i] != sy[i]) return;
        if ((1.0f / w) > izf) izf = 1.0f / w;
    }
    /* Screen Y grows down, so a GL front face (CCW, Y up) has negative area. */
    if (s_cull) {
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
        if (area >= 0.0f) return;
    }
    for (i = 0; i < 3; i++) {
        xs[i] = clamp_i(qround(sx[i]), -4096, 4096);
        ys[i] = clamp_i(qround(sy[i]), -4096, 4096);
    }
    y0 = ys[0];
    if (ys[1] < y0) y0 = ys[1];
    if (ys[2] < y0) y0 = ys[2];
    y1 = ys[0];
    if (ys[1] > y1) y1 = ys[1];
    if (ys[2] > y1) y1 = ys[2];
    if (y1 < y0) return;
    if (y0 >= s_vp_h || y1 < 0) return;

    memset(&tri, 0, sizeof tri);
    tri.x0 = (int16_t)xs[0]; tri.y0 = (int16_t)ys[0];
    tri.x1 = (int16_t)xs[1]; tri.y1 = (int16_t)ys[1];
    tri.x2 = (int16_t)xs[2]; tri.y2 = (int16_t)ys[2];
    if (tri_area2(&tri) == 0) return;
    tri.iz = (int32_t)(izf * (float)PICOGL_Q10);
    if (tri.iz < 1) tri.iz = 1;
    tri.u = (a->u > 0.0f && a->u < 65535.0f) ? (uint16_t)a->u : 0;
    tri.v = (a->v > 0.0f && a->v < 65535.0f) ? (uint16_t)a->v : 0;
    tri.rgb = rgb565(rgba);
    tri.flags = flags;
    tri.tex = (tex >= 0 && tex < 256) ? (uint8_t)tex : 0;
    s_tris++;

    if (s_tgt_rgb) {
        post_target(&tri);
        return;
    }

    if (y0 < 0) y0 = 0;
    if (y1 >= s_vp_h) y1 = s_vp_h - 1;
    band0 = y0 / PICOGL_BAND_H;
    band1 = y1 / PICOGL_BAND_H;
    if (band0 < 0) band0 = 0;
    if (band1 >= PICOGL_BANDS) band1 = PICOGL_BANDS - 1;
    for (band = band0; band <= band1; band++) {
        if (s_band_n[band] >= PICOGL_BAND_CAP) {
            s_drop++;
            continue;
        }
        s_band[band][s_band_n[band]++] = tri;
    }
}

static void submit_poly(const float *xyz, const float *st, int nverts,
                        uint32_t rgba, int tex)
{
    pv_t v[PICOGL_VERTS];
    int i, n, t;
    uint8_t flags;

    if (!xyz || nverts < 3 || s_vp_w <= 0) return;
    if (nverts > 32) nverts = 32;
    for (i = 0; i < nverts; i++) {
        float u = 0.0f, vv = 0.0f;
        if (st) { u = st[i * 2]; vv = st[i * 2 + 1]; }
        xform(&v[i], xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2], u, vv);
    }
    n = clip_poly(v, nverts);
    if (n < 3) return;
    flags = (tex >= 0) ? (uint8_t)PICOGL_TEX : (uint8_t)PICOGL_FLAT;
    for (t = 1; t < n - 1; t++)
        push_tri(&v[0], &v[t], &v[t + 1], rgba, flags, tex);
}

void picogl_begin_frame(int width, int height)
{
    pico_core1_reap();
    memset(s_band_n, 0, sizeof s_band_n);
    s_drop = 0;
    s_fills = 0;
    s_glyphs = 0;
    s_tris = 0;
    s_spans = 0;
    s_cap_n = 0;
    if (width > 0) s_vp_w = width;
    if (height > 0) s_vp_h = height;
    if (s_vp_h > PICOGL_BANDS * PICOGL_BAND_H)
        s_vp_h = PICOGL_BANDS * PICOGL_BAND_H;
}

void picogl_begin_capture(int width, int height)
{
    s_tris = 0;
    s_cap_n = 0;
    if (width > 0) s_vp_w = width;
    if (height > 0) s_vp_h = height;
    if (s_vp_h > PICOGL_BANDS * PICOGL_BAND_H)
        s_vp_h = PICOGL_BANDS * PICOGL_BAND_H;
}

static void mq_capture(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        float q = s_mvp[i] * 4096.0f;
        if (q >= 0.0f) s_mq[i] = (int32_t)(q + 0.5f);
        else s_mq[i] = (int32_t)(q - 0.5f);
    }
    s_mq_ok = 1;
}

void picogl_set_mvp(const float m[16])
{
    if (!m) return;
    memcpy(s_mvp, m, sizeof s_mvp);
    mq_capture();
}

void picogl_set_cull(int enable)
{
    s_cull = enable ? 1 : 0;
}

void picogl_set_depth(int enable)
{
    s_depth = enable ? 1 : 0;
}

static void mq_capture(void);

void picogl_set_display(int w, int h)
{
    s_disp_w = w > 0 ? w : 0;
    s_disp_h = h > 0 ? h : 0;
}

void picogl_set_camera(const float org[3], const float ang[3], int width, int height)
{
    float aspect, ox, oy, oz, ax, ay, az;
    if (width > 0) s_vp_w = width;
    if (height > 0) s_vp_h = height;
    if (s_vp_h > PICOGL_BANDS * PICOGL_BAND_H)
        s_vp_h = PICOGL_BANDS * PICOGL_BAND_H;
    if (s_vp_w <= 0 || s_vp_h <= 0) return;
    /* A non-square render stretched onto a different rectangle uses the
     * destination aspect. The viewport stays the render size, so the
     * projector still writes render-buffer pixels. */
    if (s_disp_w > 0 && s_disp_h > 0)
        aspect = (float)s_disp_w / (float)s_disp_h;
    else
        aspect = (float)s_vp_w / (float)s_vp_h;
    mat_frustum(s_mvp, -PICOGL_NEAR * aspect, PICOGL_NEAR * aspect,
                -PICOGL_NEAR, PICOGL_NEAR, PICOGL_NEAR, PICOGL_FAR);
    /* Quake camera: X forward, Y left, Z up. Same turns as R_EEGS_SetCamera. */
    mat_rotate(s_mvp, -90.0f, 1.0f, 0.0f, 0.0f);
    mat_rotate(s_mvp,  90.0f, 0.0f, 0.0f, 1.0f);
    ax = ang ? ang[0] : 0.0f;
    ay = ang ? ang[1] : 0.0f;
    az = ang ? ang[2] : 0.0f;
    mat_rotate(s_mvp, -az, 1.0f, 0.0f, 0.0f);
    mat_rotate(s_mvp, -ax, 0.0f, 1.0f, 0.0f);
    mat_rotate(s_mvp, -ay, 0.0f, 0.0f, 1.0f);
    ox = org ? org[0] : 0.0f;
    oy = org ? org[1] : 0.0f;
    oz = org ? org[2] : 0.0f;
    mat_translate(s_mvp, -ox, -oy, -oz);
    mq_capture();
}

void picogl_poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int tex)
{
    submit_poly(xyz, st, nverts, rgba, tex);
}

void picogl_tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int tex)
{
    int i;
    if (!xyz || ntris <= 0) return;
    for (i = 0; i < ntris; i++) {
        const float *p = xyz + i * 9;
        const float *s = st ? st + i * 6 : NULL;
        submit_poly(p, s, 3, rgba, tex);
    }
}

void picogl_fill(int x, int y, int w, int h, uint32_t color)
{
    (void)x; (void)y; (void)color;
    if (w <= 0 || h <= 0) return;
    s_fills++;
}

void picogl_string(int x, int y, const char *str, const uint8_t font[96][8])
{
    (void)x; (void)y;
    if (!str || !font) return;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        if (c >= 32 && c <= 127) s_glyphs++;
    }
}

void picogl_reset_stats(void)
{
    s_drop = 0;
    s_fills = 0;
    s_glyphs = 0;
    s_tris = 0;
    s_spans = 0;
}

void picogl_target_push(void)
{
    s_push_rgb = s_tgt_rgb;
    s_push_iz = s_tgt_iz;
    s_push_w = s_tgt_w;
    s_push_h = s_tgt_h;
    s_push_stride = s_tgt_stride;
    s_push_vp_w = s_vp_w;
    s_push_vp_h = s_vp_h;
    s_push_depth = s_depth;
    s_pushed = 1;
}

void picogl_target_pop(void)
{
    if (!s_pushed) return;
    s_tgt_rgb = s_push_rgb;
    s_tgt_iz = s_push_iz;
    s_tgt_w = s_push_w;
    s_tgt_h = s_push_h;
    s_tgt_stride = s_push_stride;
    s_vp_w = s_push_vp_w;
    s_vp_h = s_push_vp_h;
    s_depth = s_push_depth;
    s_pushed = 0;
}

typedef struct { int x, y, z, w; } cw_t;

/* Signed 32x32 to 64. Same bits as a widening multiply. The device
 * has no 64-bit multiply, and a call per product dominated this dot. */
static int64_t mul_i32(int a, int b)
{
#if defined(PICO_REAL_HW)
    uint32_t a0 = (uint32_t)a & 0xffffu;
    uint32_t a1 = (uint32_t)a >> 16;
    uint32_t b0 = (uint32_t)b & 0xffffu;
    uint32_t b1 = (uint32_t)b >> 16;
    uint32_t p00 = a0 * b0;
    uint32_t p01 = a0 * b1;
    uint32_t p10 = a1 * b0;
    uint32_t p11 = a1 * b1;
    uint32_t mid = (p00 >> 16) + (p01 & 0xffffu) + (p10 & 0xffffu);
    uint32_t lo = (p00 & 0xffffu) | (mid << 16);
    uint32_t hi = p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16);
    if (a < 0) hi -= (uint32_t)b;
    if (b < 0) hi -= (uint32_t)a;
    return (int64_t)(((uint64_t)hi << 32) | lo);
#else
    return (int64_t)a * (int64_t)b;
#endif
}

static void cw_point(int x, int y, int z, cw_t *o)
{
    int r;
    for (r = 0; r < 4; r++) {
        int64_t s = mul_i32(s_mq[r], x)
                  + mul_i32(s_mq[r + 4], y)
                  + mul_i32(s_mq[r + 8], z)
                  + (int64_t)s_mq[r + 12];
        int c = (int)(s >> 12);
        if (r == 0) o->x = c;
        else if (r == 1) o->y = c;
        else if (r == 2) o->z = c;
        else o->w = c;
    }
}

static int cw_dist(const cw_t *v, int plane)
{
    switch (plane) {
    case 0: return v->w + v->x;
    case 1: return v->w - v->x;
    case 2: return v->w + v->y;
    case 3: return v->w - v->y;
    case 4: return v->w + v->z;
    default: return v->w - v->z;
    }
}

static int cw_inside(const cw_t *v)
{
    int w = v->w;
    if (w < 1) return 0;
    if (v->x < -w || v->x > w) return 0;
    if (v->y < -w || v->y > w) return 0;
    if (v->z < -w || v->z > w) return 0;
    return 1;
}

/* Toward-zero truncation put a clipped vertex a pixel off the edge the
 * neighbour still owns, which is the black gap between faces. */
static int cw_round_div(int64_t num, int den)
{
    if (den < 0) {
        num = -num;
        den = -den;
    }
    if (den == 0) return 0;
    if (num >= 0) num += (int64_t)den >> 1;
    else num -= (int64_t)den >> 1;
    /* Same nearest quotient. A numerator that fits in int32 takes the
     * 32-bit divide. The rounding above is unchanged. */
    if (num <= 2147483647LL && num >= (-2147483647LL - 1))
        return (int)((int32_t)num / den);
    return (int)(num / den);
}

static void cw_lerp(cw_t *o, const cw_t *a, const cw_t *b, int da, int db)
{
    int den = da - db;
    if (den == 0) { *o = *b; return; }
    o->x = a->x + cw_round_div((int64_t)(b->x - a->x) * da, den);
    o->y = a->y + cw_round_div((int64_t)(b->y - a->y) * da, den);
    o->z = a->z + cw_round_div((int64_t)(b->z - a->z) * da, den);
    o->w = a->w + cw_round_div((int64_t)(b->w - a->w) * da, den);
}

static int cw_clip(cw_t *v, int n, int plane)
{
    cw_t out[12];
    int i, m = 0;
    if (n > 12) n = 12;
    for (i = 0; i < n; i++) {
        const cw_t *a = &v[i];
        const cw_t *b = &v[(i + 1) % n];
        int da = cw_dist(a, plane);
        int db = cw_dist(b, plane);
        int ina = da >= 0;
        int inb = db >= 0;
        if (ina && inb) {
            if (m < 12) out[m++] = *b;
        } else if (ina && !inb) {
            if (m < 12) cw_lerp(&out[m++], a, b, da, db);
        } else if (!ina && inb) {
            if (m < 12) cw_lerp(&out[m++], a, b, da, db);
            if (m < 12) out[m++] = *b;
        }
    }
    for (i = 0; i < m; i++) v[i] = out[i];
    return m;
}

static int cw_project(const cw_t *v, int *sx, int *sy, int *iz)
{
    int w = v->w;
    int guard = w + (w >> 2);
    int64_t den, nx, ny;
    if (w < 1) return 0;
    if (v->x < -guard || v->x > guard || v->y < -guard || v->y > guard)
        return 0;
    den = (int64_t)w << 1;
    nx = ((int64_t)v->x + w) * s_vp_w;
    ny = ((int64_t)w - v->y) * s_vp_h;
    if (nx >= 0) nx += den >> 1; else nx -= den >> 1;
    if (ny >= 0) ny += den >> 1; else ny -= den >> 1;
    /* Same nearest quotient. A numerator that fits in int32 takes the
     * 32-bit divide. cw_lerp is a different rounding site and stays. */
    if (den <= 2147483647LL &&
        nx <= 2147483647LL && nx >= (-2147483647LL - 1) &&
        ny <= 2147483647LL && ny >= (-2147483647LL - 1)) {
        *sx = (int)((int32_t)nx / (int32_t)den);
        *sy = (int)((int32_t)ny / (int32_t)den);
    } else {
        *sx = (int)(nx / den);
        *sy = (int)(ny / den);
    }
    if (w >= PICOGL_Q10) *iz = 1;
    else *iz = PICOGL_Q10 / w;
    if (*iz < 1) *iz = 1;
    return 1;
}

static void cw_post(const cw_t *a, const cw_t *b, const cw_t *c, uint16_t rgb)
{
    int sx0, sy0, iz0, sx1, sy1, iz1, sx2, sy2, iz2, iz;
    if (!cw_project(a, &sx0, &sy0, &iz0)) return;
    if (!cw_project(b, &sx1, &sy1, &iz1)) return;
    if (!cw_project(c, &sx2, &sy2, &iz2)) return;
    iz = iz0;
    if (iz1 > iz) iz = iz1;
    if (iz2 > iz) iz = iz2;
    picogl_screen_tri(sx0, sy0, sx1, sy1, sx2, sy2, iz, rgb);
}

void picogl_world_tri(int x0, int y0, int z0, int x1, int y1, int z1,
                      int x2, int y2, int z2, uint32_t rgba)
{
    cw_t v[12];
    int n, p, i;
    uint16_t rgb;
    if (!s_mq_ok || s_vp_w <= 0 || s_vp_h <= 0) return;
    if (!s_tgt_rgb && !s_cap) return;
    cw_point(x0, y0, z0, &v[0]);
    cw_point(x1, y1, z1, &v[1]);
    cw_point(x2, y2, z2, &v[2]);
    rgb = rgb565(rgba);
    s_from_world = 1;
    if (cw_inside(&v[0]) && cw_inside(&v[1]) && cw_inside(&v[2])) {
        cw_post(&v[0], &v[1], &v[2], rgb);
        s_from_world = 0;
        return;
    }
    n = 3;
    for (p = 0; p < 6; p++) {
        n = cw_clip(v, n, p);
        if (n < 3) {
            s_from_world = 0;
            return;
        }
    }
    for (i = 1; i < n - 1; i++)
        cw_post(&v[0], &v[i], &v[i + 1], rgb);
    s_from_world = 0;
}

void picogl_screen_tri(int x0, int y0, int x1, int y1, int x2, int y2,
                       int32_t iz, uint16_t rgb)
{
    picogl_tri_t tri;
    if (!s_tgt_rgb && !s_cap) return;
    memset(&tri, 0, sizeof tri);
    tri.x0 = (int16_t)clamp_i(x0, -4096, 4096);
    tri.y0 = (int16_t)clamp_i(y0, -4096, 4096);
    tri.x1 = (int16_t)clamp_i(x1, -4096, 4096);
    tri.y1 = (int16_t)clamp_i(y1, -4096, 4096);
    tri.x2 = (int16_t)clamp_i(x2, -4096, 4096);
    tri.y2 = (int16_t)clamp_i(y2, -4096, 4096);
    if (tri_area2(&tri) == 0) return;
    tri.iz = iz > 0 ? iz : 1;
    tri.rgb = rgb;
    tri.flags = (uint8_t)(s_from_world ? (PICOGL_FLAT | PICOGL_SEAL) : PICOGL_FLAT);
    s_tris++;
    post_target(&tri);
}

void picogl_screen_lift(int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t lift)
{
    picogl_tri_t tri;
    if (!s_tgt_rgb) return;
    memset(&tri, 0, sizeof tri);
    tri.x0 = (int16_t)clamp_i(x0, -4096, 4096);
    tri.y0 = (int16_t)clamp_i(y0, -4096, 4096);
    tri.x1 = (int16_t)clamp_i(x1, -4096, 4096);
    tri.y1 = (int16_t)clamp_i(y1, -4096, 4096);
    tri.x2 = (int16_t)clamp_i(x2, -4096, 4096);
    tri.y2 = (int16_t)clamp_i(y2, -4096, 4096);
    if (tri_area2(&tri) == 0) return;
    tri.iz = 1;
    tri.rgb = lift;
    tri.flags = (uint8_t)PICOGL_LIFT;
    s_tris++;
    post_target(&tri);
}

void picogl_capture(picogl_tri_t *dst, int cap)
{
    s_cap = dst;
    s_cap_n = 0;
    s_cap_max = (dst && cap > 0) ? cap : 0;
}

int picogl_captured(void)
{
    return s_cap_n;
}

void picogl_set_cover(int on)
{
    s_cover = on ? 1 : 0;
}

void picogl_set_edge_resume(int *q, int *r, int16_t *y, int n)
{
    s_edge_q = (q && r && y && n > 0) ? q : 0;
    s_edge_r = s_edge_q ? r : 0;
    s_edge_y = s_edge_q ? y : 0;
    s_edge_n = s_edge_q ? n : 0;
}

void picogl_set_edge_step(int16_t *dq, uint16_t *dr)
{
    s_edge_dq = (dq && dr) ? dq : 0;
    s_edge_dr = s_edge_dq ? dr : 0;
}

static void px_bind(span_px *px, uint16_t *be, int32_t *iz, int stride,
                    int band_h, int ox, int oy, int vw, int vh,
                    uint32_t (*cov)[10], volatile int *span_ctr, int cover)
{
    memset(px, 0, sizeof *px);
    px->rgb = be;
    px->iz = iz;
    px->stride = stride;
    px->dest_w = stride;
    px->dest_h = band_h;
    px->xoff = ox;
    px->yoff = oy;
    px->vw = vw;
    px->vh = vh;
    px->swap = 1;
    px->cover = cover;
    px->cov = cov;
    px->span_ctr = span_ctr;
}

/* Count the view pixels, then walk [i0, i1). A full view stops this
 * half. The other half has its own bits, so the stop does not drop a
 * pixel that this half never covered. */
static void paint_range(span_px *px, const picogl_tri_t *tris, int i0, int i1,
                        int band_y, int oy)
{
    int y, i;
    if (!px || !px->rgb) return;
    if (px->cover && px->cov && px->dest_h > 0 && px->dest_h <= 16) {
        memset(px->cov, 0, (size_t)px->dest_h * sizeof px->cov[0]);
        px->cov_left = 0;
        for (y = 0; y < px->dest_h; y++) {
            int sy = band_y + y;
            int lo, hi;
            if (sy < oy || sy >= oy + px->vh) continue;
            lo = px->xoff;
            hi = px->xoff + px->vw - 1;
            if (lo < 0) lo = 0;
            if (hi >= px->stride) hi = px->stride - 1;
            if (lo <= hi) px->cov_left += hi - lo + 1;
        }
    }
    if (!tris || i0 >= i1) return;
    /* yoff was the origin. The band walk wants it relative to band_y. */
    px->yoff = oy - band_y;
    for (i = i0; i < i1; i++) {
        int ymin = tris[i].y0;
        int ymax = tris[i].y0;
        if (tris[i].y1 < ymin) ymin = tris[i].y1;
        if (tris[i].y2 < ymin) ymin = tris[i].y2;
        if (tris[i].y1 > ymax) ymax = tris[i].y1;
        if (tris[i].y2 > ymax) ymax = tris[i].y2;
        if (ymax < 0 || ymin >= px->vh) continue;
        if (oy + ymax < band_y || oy + ymin >= band_y + px->dest_h) continue;
        if (px->cover && px->cov_left <= 0) break;
        paint_tri_target(&tris[i],
                         (s_edge_q && s_edge_y && i < s_edge_n) ? i : -1, px);
    }
}

/* Holes in the near coverage take the far pixel, or sky. Near wins.
 * The far list is the later half of the same front-to-back order. */
static void composite_far(uint16_t *dst, const uint16_t *far,
                          const uint32_t (*nc)[10], const uint32_t (*fc)[10],
                          int stride, int band_h,
                          int ox, int oy, int vw, int vh, int band_y,
                          uint16_t clear_rgb, int use_far)
{
    uint16_t sky = (uint16_t)((clear_rgb >> 8) | (clear_rgb << 8));
    int y;
    for (y = 0; y < band_h; y++) {
        int sy = band_y + y;
        int lo, hi, x;
        uint16_t *row;
        const uint16_t *frow;
        const uint32_t *nb;
        const uint32_t *fb;
        if (sy < oy || sy >= oy + vh) continue;
        lo = ox;
        hi = ox + vw - 1;
        if (lo < 0) lo = 0;
        if (hi >= stride) hi = stride - 1;
        if (lo > hi) continue;
        row = dst + y * stride;
        frow = far ? far + y * stride : 0;
        nb = nc ? nc[y] : 0;
        fb = (use_far && fc) ? fc[y] : 0;
        x = lo;
        while (x <= hi) {
            int bit = x & 31;
            int n = 32 - bit;
            int wi = x >> 5;
            uint32_t care, nmask, fmask;
            if (x + n - 1 > hi) n = hi - x + 1;
            care = (n == 32) ? 0xffffffffu : (((1u << n) - 1u) << bit);
            nmask = nb ? (nb[wi] & care) : 0;
            if (nmask == care) {
                x += n;
                continue;
            }
            fmask = fb ? (fb[wi] & care) : 0;
            if (nmask == 0 && fmask == care && frow) {
                uint16_t *d = row + x;
                const uint16_t *s = frow + x;
                int i = n;
                while (i--) *d++ = *s++;
                x += n;
                continue;
            }
            if (nmask == 0 && fmask == 0) {
                uint16_t *d = row + x;
                int i = n;
                while (i--) *d++ = sky;
                x += n;
                continue;
            }
            {
                int i;
                for (i = 0; i < n; i++) {
                    uint32_t m = 1u << (bit + i);
                    if (nmask & m) continue;
                    if ((fmask & m) && frow) row[x + i] = frow[x + i];
                    else row[x + i] = sky;
                }
                x += n;
            }
        }
    }
}

static struct {
    const picogl_tri_t *tris;
    int i0, i1;
    int band_y, oy;
    volatile int spans;
    span_px px;
} s_far_job;

static volatile int s_far_go;
static volatile int s_far_done;

/* The quake image overrides these. A direct call stays, so the linker
 * can retarget it. Inlining the empty body would ignore that override. */
__attribute__((weak, noinline)) void picogl_core1_extra(void)
{
}

__attribute__((weak, noinline)) int picogl_core1_extra_busy(void)
{
    return 0;
}

int picogl_core1_busy(void)
{
    if (s_far_go) return 1;
    if (picogl_core1_extra_busy()) return 1;
    return 0;
}

void picogl_core1_job(void)
{
    picogl_core1_extra();
    if (!s_far_go) return;
    NEXUS_BARRIER();
    paint_range(&s_far_job.px, s_far_job.tris, s_far_job.i0, s_far_job.i1,
                s_far_job.band_y, s_far_job.oy);
    NEXUS_BARRIER();
    s_far_done = 1;
    NEXUS_BARRIER();
    s_far_go = 0;
}

static int far_kick(void)
{
#if defined(PICO_REAL_HW)
    int st;
    if (s_far_go) return 0;
    st = pico_core1_state();
    if (st != 1 && st != 2) return 0;
    s_far_done = 0;
    NEXUS_BARRIER();
    s_far_go = 1;
    NEXUS_BARRIER();
    pico_core1_sev();
    return 1;
#else
    return 0;
#endif
}

static int far_join(void)
{
#if defined(PICO_REAL_HW)
    uint32_t t0 = TIMER0_TIMERAWL;
    while (!s_far_done) {
        if ((TIMER0_TIMERAWL - t0) > 200000u) return 0;
        pico_core1_sev();
    }
    NEXUS_BARRIER();
    return 1;
#else
    return 0;
#endif
}

void picogl_paint_band(const picogl_tri_t *tris, int ntri,
                       uint16_t *be, int stride,
                       int band_y, int band_h,
                       int ox, int oy, int vw, int vh,
                       int32_t *iz, uint16_t clear_rgb)
{
    int y, x;
    int cover_on;
    span_px px;
    if (!be || stride <= 0 || band_h <= 0 || vw <= 0 || vh <= 0) return;
    if (!iz && !s_cover) return;
    /* Coverage is the depth test. Skip the sky and iz clear: each covered
     * pixel is written once by its nearest triangle, and the pass below
     * fills the holes. The z-test path still clears both. */
    cover_on = s_cover && band_h <= 16 && stride <= 320;
    if (!cover_on) {
        uint16_t sky = (uint16_t)((clear_rgb >> 8) | (clear_rgb << 8));
        for (y = 0; y < band_h; y++) {
            int sy = band_y + y;
            uint16_t *row;
            if (sy < oy || sy >= oy + vh) continue;
            row = be + y * stride;
            for (x = 0; x < vw; x++) {
                int sx = ox + x;
                if ((unsigned)sx >= (unsigned)stride) continue;
                row[sx] = sky;
                if (iz) iz[y * stride + sx] = 0;
            }
        }
    }
    px_bind(&px, be, iz, stride, band_h, ox, oy, vw, vh,
            cover_on ? s_cov : 0, &s_spans, cover_on);
    paint_range(&px, tris, 0, ntri, band_y, oy);
    if (cover_on && px.cov_left > 0) {
        for (y = 0; y < band_h; y++) {
            int sy = band_y + y;
            int lo, hi;
            if (sy < oy || sy >= oy + vh) continue;
            lo = ox;
            hi = ox + vw - 1;
            if (lo < 0) lo = 0;
            if (hi >= stride) hi = stride - 1;
            if (lo <= hi)
                paint_span_cover(&px, lo, hi, 1, clear_rgb, y);
        }
    }
}

void picogl_paint_band_split(const picogl_tri_t *tris, int ntri,
                             uint16_t *be, uint16_t *far_be, int stride,
                             int band_y, int band_h,
                             int ox, int oy, int vw, int vh,
                             int32_t *iz, uint16_t clear_rgb)
{
    span_px near;
    int mid, kicked, use_far, cover_on;
    if (!be || !s_cover || !far_be || !tris || ntri < 2 ||
        band_h > 16 || stride > 320 || stride <= 0) {
        picogl_paint_band(tris, ntri, be, stride, band_y, band_h,
                          ox, oy, vw, vh, iz, clear_rgb);
        return;
    }
    cover_on = 1;
    mid = ntri >> 1;
    /* Adjacent int16 edge slots share a word. An even cut keeps each
     * core on its own words. */
    if (s_edge_y && (mid & 1)) mid--;
    if (mid < 1) {
        picogl_paint_band(tris, ntri, be, stride, band_y, band_h,
                          ox, oy, vw, vh, iz, clear_rgb);
        return;
    }
    px_bind(&near, be, iz, stride, band_h, ox, oy, vw, vh,
            s_cov, &s_spans, cover_on);
    kicked = 0;
    use_far = 0;
    /* A late core 1 still owns s_far_job. Do not rewrite it. */
    if (!s_far_go) {
        s_far_job.spans = 0;
        px_bind(&s_far_job.px, far_be, 0, stride, band_h, ox, oy, vw, vh,
                s_cov_far, &s_far_job.spans, cover_on);
        s_far_job.tris = tris;
        s_far_job.i0 = mid;
        s_far_job.i1 = ntri;
        s_far_job.band_y = band_y;
        s_far_job.oy = oy;
        kicked = far_kick();
    }
    paint_range(&near, tris, 0, mid, band_y, oy);
    if (kicked) {
        use_far = far_join();
        if (use_far) s_spans += s_far_job.spans;
    } else if (!s_far_go) {
        paint_range(&s_far_job.px, tris, mid, ntri, band_y, oy);
        s_spans += s_far_job.spans;
        use_far = 1;
    } else {
        use_far = 0;
    }
    composite_far(be, far_be, s_cov, s_cov_far, stride, band_h,
                  ox, oy, vw, vh, band_y, clear_rgb, use_far);
}

void picogl_bind_target(uint16_t *rgb, int32_t *iz, int w, int h, int stride)
{
    s_tgt_rgb = rgb;
    s_tgt_iz = iz;
    if (!rgb || w <= 0 || h <= 0) {
        s_tgt_w = 0;
        s_tgt_h = 0;
        s_tgt_stride = 0;
        return;
    }
    s_tgt_w = w;
    s_tgt_h = h;
    s_tgt_stride = (stride > 0) ? stride : w;
    if (w > 0) s_vp_w = w;
    if (h > 0 && h <= PICOGL_BANDS * PICOGL_BAND_H) s_vp_h = h;
}

void picogl_clear_target(int color, int depth, uint16_t rgb)
{
    int y, x;
    pico_core1_reap();
    if (!s_tgt_rgb || s_tgt_w <= 0 || s_tgt_h <= 0) return;
    for (y = 0; y < s_tgt_h; y++) {
        uint16_t *row = s_tgt_rgb + y * s_tgt_stride;
        int32_t *iz = s_tgt_iz ? s_tgt_iz + y * s_tgt_stride : NULL;
        for (x = 0; x < s_tgt_w; x++) {
            if (color) row[x] = rgb;
            if (depth && iz) iz[x] = 0;
        }
    }
}

static void paint_one_row(const picogl_tri_t *t, int y, uint16_t *row,
                          int32_t *iz, int width)
{
    int ymin, ymax, lo, hi, py, area;
    int x0, y0, x1, y1, x2, y2;
    span_edge_t e0, e1, e2;
    ymin = t->y0;
    if (t->y1 < ymin) ymin = t->y1;
    if (t->y2 < ymin) ymin = t->y2;
    ymax = t->y0;
    if (t->y1 > ymax) ymax = t->y1;
    if (t->y2 > ymax) ymax = t->y2;
    if (y < ymin || y > ymax) return;
    lo = t->x0;
    hi = t->x0;
    if (t->x1 < lo) lo = t->x1;
    if (t->x2 < lo) lo = t->x2;
    if (t->x1 > hi) hi = t->x1;
    if (t->x2 > hi) hi = t->x2;
    if (lo < 0) lo = 0;
    if (hi >= width) hi = width - 1;
    if (lo > hi) return;
    x0 = t->x0 * 2; y0 = t->y0 * 2;
    x1 = t->x1 * 2; y1 = t->y1 * 2;
    x2 = t->x2 * 2; y2 = t->y2 * 2;
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) return;
    py = y * 2 + 1;
    if (area > 0) {
        edge_init(&e0, x0, y0, x1, y1, py);
        edge_init(&e1, x1, y1, x2, y2, py);
        edge_init(&e2, x2, y2, x0, y0, py);
    } else {
        edge_init(&e0, x1, y1, x0, y0, py);
        edge_init(&e1, x2, y2, x1, y1, py);
        edge_init(&e2, x0, y0, x2, y2, py);
    }
    edge_apply(&e0, &lo, &hi);
    edge_apply(&e1, &lo, &hi);
    edge_apply(&e2, &lo, &hi);
    if (lo <= hi)
        paint_span_write(row, iz, lo, hi, t->iz, t->rgb, 0);
}

void picogl_raster_band(int band, uint16_t *rgb, int stride, int32_t *iz_row, int width)
{
    int y, i, n, y0;
    if (!rgb || !iz_row || width <= 0) return;
    if (band < 0 || band >= PICOGL_BANDS || stride <= 0) return;
    y0 = band * PICOGL_BAND_H;
    n = s_band_n[band];
    for (y = 0; y < PICOGL_BAND_H; y++) {
        uint16_t *row = rgb + y * stride;
        for (i = 0; i < width; i++) iz_row[i] = 0;
        for (i = 0; i < n; i++)
            paint_one_row(&s_band[band][i], y0 + y, row, iz_row, width);
    }
}

int picogl_drop(void) { return s_drop; }

int picogl_band_n(int band)
{
    if (band < 0 || band >= PICOGL_BANDS) return 0;
    return s_band_n[band];
}

const picogl_tri_t *picogl_band_tri(int band, int i)
{
    if (band < 0 || band >= PICOGL_BANDS) return NULL;
    if (i < 0 || i >= s_band_n[band]) return NULL;
    return &s_band[band][i];
}

int picogl_fill_n(void) { return s_fills; }
int picogl_glyph_n(void) { return s_glyphs; }
int picogl_tri_n(void) { return s_tris; }
int picogl_span_n(void) { return s_spans; }
