/*
 * picogl_span.c — Q10 camera, clip, band bins, and the span fill.
 *
 * The fill walks pixel centers with an integer edge test. A larger iz wins.
 * Core 1 runs the same fill when a target is bound. The host test calls
 * picogl_raster_band on this core.
 */
#include "picogl.h"

#include <math.h>
#include <string.h>

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
static picogl_tri_t s_queue[PICOGL_BAND_CAP];
static volatile int s_queue_n;

__attribute__((weak)) void pico_core1_launch(void) {}
__attribute__((weak)) int pico_core1_run_flush(void) { return 0; }

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

/* Pixel-center edge test. The same formula is copied in test_picogl_raster.c.
 * Inlined so the scratch copy of paint_row does not call back into flash. */
static inline __attribute__((always_inline)) int edge_cross(int ax, int ay, int bx, int by, int px, int py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int tri_area2(const picogl_tri_t *t)
{
    return edge_cross(t->x0 * 2, t->y0 * 2, t->x1 * 2, t->y1 * 2, t->x2 * 2, t->y2 * 2);
}

static inline __attribute__((always_inline)) int pixel_inside(const picogl_tri_t *t, int x, int y)
{
    int px = x * 2 + 1;
    int py = y * 2 + 1;
    int c0 = edge_cross(t->x0 * 2, t->y0 * 2, t->x1 * 2, t->y1 * 2, px, py);
    int c1 = edge_cross(t->x1 * 2, t->y1 * 2, t->x2 * 2, t->y2 * 2, px, py);
    int c2 = edge_cross(t->x2 * 2, t->y2 * 2, t->x0 * 2, t->y0 * 2, px, py);
    int pos = (c0 > 0) + (c1 > 0) + (c2 > 0);
    int neg = (c0 < 0) + (c1 < 0) + (c2 < 0);
    if (pos == 0 && neg == 0) return 0;
    return pos == 0 || neg == 0;
}

#if defined(PICO_REAL_HW)
__attribute__((section(".scratch_x"), noinline))
#endif
static void paint_row(const picogl_tri_t *t, int y, uint16_t *row, int32_t *iz, int width)
{
    int x, x0, x1, wrote = 0;
    int ymin, ymax;

    ymin = t->y0;
    if (t->y1 < ymin) ymin = t->y1;
    if (t->y2 < ymin) ymin = t->y2;
    ymax = t->y0;
    if (t->y1 > ymax) ymax = t->y1;
    if (t->y2 > ymax) ymax = t->y2;
    if (y < ymin || y > ymax) return;

    x0 = t->x0;
    if (t->x1 < x0) x0 = t->x1;
    if (t->x2 < x0) x0 = t->x2;
    x1 = t->x0;
    if (t->x1 > x1) x1 = t->x1;
    if (t->x2 > x1) x1 = t->x2;
    if (x0 < 0) x0 = 0;
    if (x1 >= width) x1 = width - 1;
    for (x = x0; x <= x1; x++) {
        if (!pixel_inside(t, x, y)) continue;
        if (s_depth && !(t->iz > iz[x])) continue;
        if (s_depth) iz[x] = t->iz;
        row[x] = t->rgb;
        wrote = 1;
    }
    if (wrote) s_spans++;
}

static void paint_tri_target(const picogl_tri_t *t)
{
    int y, ymin, ymax;
    if (!s_tgt_rgb || !s_tgt_iz || s_tgt_w <= 0 || s_tgt_h <= 0) return;
    ymin = t->y0;
    if (t->y1 < ymin) ymin = t->y1;
    if (t->y2 < ymin) ymin = t->y2;
    ymax = t->y0;
    if (t->y1 > ymax) ymax = t->y1;
    if (t->y2 > ymax) ymax = t->y2;
    if (ymin < 0) ymin = 0;
    if (ymax >= s_tgt_h) ymax = s_tgt_h - 1;
    for (y = ymin; y <= ymax; y++) {
        paint_row(t, y,
                  s_tgt_rgb + y * s_tgt_stride,
                  s_tgt_iz + y * s_tgt_stride,
                  s_tgt_w);
    }
}

void picogl_flush_target_now(void)
{
    int i, n;
    n = s_queue_n;
    for (i = 0; i < n; i++)
        paint_tri_target(&s_queue[i]);
    s_queue_n = 0;
#if defined(PICO_REAL_HW)
    __asm__ volatile("dmb sy" ::: "memory");
#endif
}

void picogl_flush_target(void)
{
    if (s_queue_n <= 0) return;
    if (!s_tgt_rgb) {
        s_queue_n = 0;
        return;
    }
    if (pico_core1_run_flush()) return;
    picogl_flush_target_now();
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
        sx[i] = (p[i]->x / w * 0.5f + 0.5f) * (float)s_vp_w;
        sy[i] = (0.5f - p[i]->y / w * 0.5f) * (float)s_vp_h;
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
        if (s_queue_n >= PICOGL_BAND_CAP)
            picogl_flush_target();
        if (s_queue_n < PICOGL_BAND_CAP)
            s_queue[s_queue_n++] = tri;
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
    memset(s_band_n, 0, sizeof s_band_n);
    s_queue_n = 0;
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
            paint_row(&s_band[band][i], y0 + y, row, iz_row, width);
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
