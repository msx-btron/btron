/*
 * picogl_span.c — Q10 camera, clip, band bins, and the span fill.
 *
 * The fill walks pixel centers with an integer edge test. A larger iz wins.
 * Core 1 runs the same fill when a target is bound. The host test calls
 * picogl_raster_band on this core.
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
static int   s_vp_w, s_vp_h;
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
static uint16_t *s_push_rgb;
static int32_t  *s_push_iz;
static int       s_push_w, s_push_h, s_push_stride;
static int       s_push_vp_w, s_push_vp_h, s_push_depth, s_pushed;
static picogl_span_ring_t s_span_ring;
static volatile uint32_t s_span_done;

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
 * The first row divides once per edge. Later rows add 2*dx to the numerator. */
typedef struct {
    int kind; /* 1 = x max, 2 = x min, 3 = horizontal */
    int q, r, d, step;
    int ky, dx;
} span_edge_t;

static void edge_init(span_edge_t *e, int ax, int ay, int bx, int by, int py)
{
    int dx = bx - ax;
    int dy = by - ay;
    int ky = dx * (py - ay) - dy * (1 - ax);
    e->kind = 0;
    e->q = e->r = e->d = e->step = 0;
    e->ky = ky;
    e->dx = dx;
    if (dy == 0) {
        e->kind = 3;
        return;
    }
    if (dy > 0) {
        int d = dy << 1;
        int q = floor_div_pos(ky, d);
        e->kind = 1;
        e->q = q;
        e->r = ky - q * d;
        e->d = d;
        e->step = dx << 1;
    } else {
        int n = -ky;
        int d = (-dy) << 1;
        int m = n + d - 1;
        int q = floor_div_pos(m, d);
        e->kind = 2;
        e->q = q;
        e->r = m - q * d;
        e->d = d;
        e->step = -(dx << 1);
    }
}

static void edge_advance(span_edge_t *e)
{
    int guard;
    if (e->kind == 3) {
        e->ky += e->dx << 1;
        return;
    }
    if (e->kind == 0 || e->d <= 0) return;
    e->r += e->step;
    guard = 0;
    while (e->r >= e->d && guard < 48) {
        e->r -= e->d;
        e->q++;
        guard++;
    }
    while (e->r < 0 && guard < 48) {
        e->r += e->d;
        e->q--;
        guard++;
    }
    if (e->r >= e->d || e->r < 0) {
        int n = e->q * e->d + e->r;
        int q = floor_div_pos(n, e->d);
        e->q = q;
        e->r = n - q * e->d;
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

#if defined(PICO_REAL_HW)
__attribute__((section(".scratch_x"), noinline))
#endif
static void paint_span_write(uint16_t *row, int32_t *iz, int lo, int hi,
                             int32_t izv, uint16_t rgb)
{
    int x, wrote = 0;
    for (x = lo; x <= hi; x++) {
        if (s_depth) {
            if (!iz || !(izv > iz[x])) continue;
            iz[x] = izv;
        }
        row[x] = rgb;
        wrote = 1;
    }
    if (wrote) s_spans++;
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

static void paint_tri_target(const picogl_tri_t *t)
{
    int y, ymin, ymax, py, area;
    int x0, y0, x1, y1, x2, y2;
    span_edge_t e0, e1, e2;
    if (!s_tgt_rgb || s_tgt_w <= 0 || s_tgt_h <= 0) return;
    if (s_depth && !s_tgt_iz) return;
    ymin = t->y0;
    if (t->y1 < ymin) ymin = t->y1;
    if (t->y2 < ymin) ymin = t->y2;
    ymax = t->y0;
    if (t->y1 > ymax) ymax = t->y1;
    if (t->y2 > ymax) ymax = t->y2;
    if (ymin < 0) ymin = 0;
    if (ymax >= s_tgt_h) ymax = s_tgt_h - 1;
    if (ymin > ymax) return;
    x0 = t->x0 * 2; y0 = t->y0 * 2;
    x1 = t->x1 * 2; y1 = t->y1 * 2;
    x2 = t->x2 * 2; y2 = t->y2 * 2;
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) return;
    py = ymin * 2 + 1;
    if (area > 0) {
        edge_init(&e0, x0, y0, x1, y1, py);
        edge_init(&e1, x1, y1, x2, y2, py);
        edge_init(&e2, x2, y2, x0, y0, py);
    } else {
        edge_init(&e0, x1, y1, x0, y0, py);
        edge_init(&e1, x2, y2, x1, y1, py);
        edge_init(&e2, x0, y0, x2, y2, py);
    }
    for (y = ymin; y <= ymax; y++) {
        int lo = t->x0;
        int hi = t->x0;
        if (t->x1 < lo) lo = t->x1;
        if (t->x2 < lo) lo = t->x2;
        if (t->x1 > hi) hi = t->x1;
        if (t->x2 > hi) hi = t->x2;
        if (lo < 0) lo = 0;
        if (hi >= s_tgt_w) hi = s_tgt_w - 1;
        edge_apply(&e0, &lo, &hi);
        edge_apply(&e1, &lo, &hi);
        edge_apply(&e2, &lo, &hi);
        if (lo <= hi) {
            uint16_t *row = s_tgt_rgb + y * s_tgt_stride;
            if (t->flags & PICOGL_LIFT)
                paint_span_lift(row, lo, hi, t->rgb);
            else
                paint_span_write(row, s_tgt_iz ? s_tgt_iz + y * s_tgt_stride : 0,
                                 lo, hi, t->iz, t->rgb);
        }
        edge_advance(&e0);
        edge_advance(&e1);
        edge_advance(&e2);
    }
}

void picogl_span_drain(void)
{
    /* The slot stays owned by the consumer until the paint finishes.
     * Tail moves after the write, so the producer cannot reuse it and
     * the triangle is not copied out of the ring. */
    for (;;) {
        uint32_t t = s_span_ring.tail;
        uint32_t h = s_span_ring.head;
        const picogl_tri_t *tri;
        if (t == h) return;
        NEXUS_BARRIER();
        tri = &s_span_ring.slot[t & (PICOGL_SPAN_SLOTS - 1)];
        paint_tri_target(tri);
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
    paint_tri_target(tri);
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
    if (width > 0) s_vp_w = width;
    if (height > 0) s_vp_h = height;
    if (s_vp_h > PICOGL_BANDS * PICOGL_BAND_H)
        s_vp_h = PICOGL_BANDS * PICOGL_BAND_H;
}

void picogl_set_mvp(const float m[16])
{
    if (!m) return;
    memcpy(s_mvp, m, sizeof s_mvp);
}

void picogl_set_cull(int enable)
{
    s_cull = enable ? 1 : 0;
}

void picogl_set_depth(int enable)
{
    s_depth = enable ? 1 : 0;
}

void picogl_set_camera(const float org[3], const float ang[3], int width, int height)
{
    float aspect, ox, oy, oz, ax, ay, az;
    if (width > 0) s_vp_w = width;
    if (height > 0) s_vp_h = height;
    if (s_vp_h > PICOGL_BANDS * PICOGL_BAND_H)
        s_vp_h = PICOGL_BANDS * PICOGL_BAND_H;
    if (s_vp_w <= 0 || s_vp_h <= 0) return;
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

void picogl_screen_tri(int x0, int y0, int x1, int y1, int x2, int y2,
                       int32_t iz, uint16_t rgb)
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
    tri.iz = iz > 0 ? iz : 1;
    tri.rgb = rgb;
    tri.flags = (uint8_t)PICOGL_FLAT;
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
        paint_span_write(row, iz, lo, hi, t->iz, t->rgb);
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
