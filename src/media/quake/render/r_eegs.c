/*
 * r_eegs.c — EEGS-native Quake raster (VU0 transform + GS submit).
 *
 * Draw-time does not call TinyGL glBegin/glVertex/glFrustum.  Texture
 * planes still come from the load-time glTexImage2D registry.
 */
#include "../include/r_eegs.h"
#include "../include/texture.h"
#include "gl_dispatch.h"

#include <string.h>
#include <math.h>

#if defined(BTRON_PS2_TARGET)
#include "../../../desktop/graphics/eegs/backend_eegs.h"
#include "../../../drivers/playstation2/ps2_vu0_kernel.h"
#include "../../../drivers/playstation2/ps2_vu1_geom.h"
#include "../../../drivers/playstation2/ps2_gs_tex.h"
#endif

int R_EEGS_Live(void)
{
#if defined(BTRON_PS2_TARGET)
    return gl_active_backend() == GL_BACKEND_EEGS;
#else
    return 0;
#endif
}

#if !defined(BTRON_PS2_TARGET)

void R_EEGS_BeginFrame(int width, int height) { (void)width; (void)height; }
void R_EEGS_SetCamera(const float org[3], const float ang[3], int width, int height)
{
    (void)org; (void)ang; (void)width; (void)height;
}
void R_EEGS_Poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int gl_tex_idx)
{
    (void)xyz; (void)st; (void)nverts; (void)rgba; (void)gl_tex_idx;
}
void R_EEGS_Fill(int x, int y, int w, int h, uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void R_EEGS_String(int x, int y, const char *str, const uint8_t font[96][8])
{
    (void)x; (void)y; (void)str; (void)font;
}
void R_EEGS_Tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int gl_tex_idx)
{
    (void)xyz; (void)st; (void)ntris; (void)rgba; (void)gl_tex_idx;
}

#else /* BTRON_PS2_TARGET */

#define EEGS_MAX_VERTS 32
#define EEGS_CLIP_MAX  40

static float s_mvp[16] __attribute__((aligned(16)));
static int   s_vp_w, s_vp_h;
static float s_sx, s_sy, s_tx, s_ty;
static int   s_inited;
static int   s_logged;

/* Homogeneous clip-space vertex plus texels.  Unclipped BSP faces that
 * straddle the near plane project a vertex to infinity (GS 12.4 wrap)
 * and stretch walls/beams across the window. */
typedef struct {
    float x, y, z, w;
    float s, t;
} clip_vert_t;

static void m_ident(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void m_mul(float *out, const float *a, const float *b)
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

static void m_frustum(float *m, float l, float r, float b, float t, float n, float f)
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

static void m_rotate(float *m, float deg, float x, float y, float z)
{
    float r[16], c, s, rad, len, xx, yy, zz, xy, xz, yz, xs, ys, zs, c1;
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
    m_ident(r);
    r[0] = xx * c1 + c;   r[4] = xy * c1 - zs;  r[8]  = xz * c1 + ys;
    r[1] = xy * c1 + zs;  r[5] = yy * c1 + c;   r[9]  = yz * c1 - xs;
    r[2] = xz * c1 - ys;  r[6] = yz * c1 + xs;  r[10] = zz * c1 + c;
    m_mul(m, m, r);
}

static void m_translate(float *m, float x, float y, float z)
{
    float t[16];
    m_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    m_mul(m, m, t);
}

static uint32_t rgb_to_rgba(uint32_t color)
{
    unsigned r = (color >> 16) & 0xFFu;
    unsigned g = (color >> 8) & 0xFFu;
    unsigned b = color & 0xFFu;
    return r | (g << 8) | (b << 16) | (0xFFu << 24);
}

static uint32_t f_to_rgba(float r, float g, float b)
{
    unsigned ri = (unsigned)(r * 255.0f); if (ri > 255u) ri = 255u;
    unsigned gi = (unsigned)(g * 255.0f); if (gi > 255u) gi = 255u;
    unsigned bi = (unsigned)(b * 255.0f); if (bi > 255u) bi = 255u;
    return ri | (gi << 8) | (bi << 16) | (0xFFu << 24);
}

static const gs_tex_t *tex_of(int gl_idx)
{
    const void *plane;
    if (gl_idx < 0 || gl_idx >= g_num_gl_textures) return NULL;
    if (!g_gl_textures[gl_idx].is_uploaded) return NULL;
    plane = g_gl_textures[gl_idx].gs_plane;
    if (!plane) return NULL;
    return gs_tex_find(plane);
}

static float plane_dist(const clip_vert_t *v, int plane)
{
    switch (plane) {
    case 0: return v->w + v->x; /* x >= -w */
    case 1: return v->w - v->x;
    case 2: return v->w + v->y;
    case 3: return v->w - v->y;
    case 4: return v->w + v->z; /* near */
    case 5: return v->w - v->z; /* far */
    default: return 0.0f;
    }
}

static void clip_intersect(clip_vert_t *o, const clip_vert_t *a,
                           const clip_vert_t *b, float t, int plane)
{
    o->x = a->x + t * (b->x - a->x);
    o->y = a->y + t * (b->y - a->y);
    o->z = a->z + t * (b->z - a->z);
    o->w = a->w + t * (b->w - a->w);
    o->s = a->s + t * (b->s - a->s);
    o->t = a->t + t * (b->t - a->t);
    /* Snap the clipped coordinate onto the plane so w-divide cannot
     * send a near-plane vertex to infinity. */
    switch (plane) {
    case 0: o->x = -o->w; break;
    case 1: o->x =  o->w; break;
    case 2: o->y = -o->w; break;
    case 3: o->y =  o->w; break;
    case 4: o->z = -o->w; break;
    case 5: o->z =  o->w; break;
    }
}

/* Sutherland-Hodgman against the six homogeneous clip planes.
 * Returns vertex count, 0 if the polygon is gone. */
static int clip_poly(clip_vert_t *verts, int n)
{
    clip_vert_t tmp[EEGS_CLIP_MAX];
    int plane;
    for (plane = 0; plane < 6; plane++) {
        int i, outn = 0;
        if (n < 3) return 0;
        for (i = 0; i < n; i++) {
            const clip_vert_t *a = &verts[i];
            const clip_vert_t *b = &verts[(i + 1) % n];
            float da = plane_dist(a, plane);
            float db = plane_dist(b, plane);
            int ina = da >= 0.0f;
            int inb = db >= 0.0f;
            if (ina) {
                if (outn >= EEGS_CLIP_MAX) return 0;
                tmp[outn++] = *a;
            }
            if (ina != inb) {
                float den = da - db;
                float t = (den == 0.0f) ? 0.0f : (da / den);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                if (outn >= EEGS_CLIP_MAX) return 0;
                clip_intersect(&tmp[outn], a, b, t, plane);
                outn++;
            }
        }
        memcpy(verts, tmp, (size_t)outn * sizeof(clip_vert_t));
        n = outn;
    }
    return n;
}

/* eegs_vertex.s/t are texels; emit_vertex() later divides by the live
 * plane's power-of-two size.  Returns 0, or -1 if w/coords are unusable. */
static int map_clip_vert(const clip_vert_t *pc, uint32_t rgba, eegs_vertex_t *out)
{
    float w = pc->w;
    float winv;
    if (w <= 1e-4f) return -1;
    winv = 1.0f / w;
    out->x_px = (int32_t)(pc->x * winv * s_sx + s_tx);
    out->y_px = (int32_t)(pc->y * winv * s_sy + s_ty);
    out->z = 0u;
    out->rgba = rgba;
    out->s = pc->s;
    out->t = pc->t;
    /* GS XYZ2 is signed 12.4; pixels outside this wrap across the screen. */
    if (out->x_px < -2047 || out->x_px > 2047) return -1;
    if (out->y_px < -2047 || out->y_px > 2047) return -1;
    return 0;
}

static void emit_tri(const eegs_vertex_t *a, const eegs_vertex_t *b,
                     const eegs_vertex_t *c, int textured)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;
    v[0] = *a; v[1] = *b; v[2] = *c;
    st.smooth = 0u;
    st.textured = textured ? 1u : 0u;
    st.blended = 0u;
    st.brighten = 0u;
    st.depth_test = 0u;
    eegs_triangle(v, st);
}

static void emit_rect(int x0, int y0, int x1, int y1, uint32_t rgba)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;
    st.smooth = 0u; st.textured = 0u; st.blended = 0u; st.brighten = 0u; st.depth_test = 0u;
    v[0].z = v[1].z = v[2].z = 0u;
    v[0].s = v[1].s = v[2].s = 0.0f;
    v[0].t = v[1].t = v[2].t = 0.0f;
    v[0].rgba = v[1].rgba = v[2].rgba = rgba;
    v[0].x_px = x0; v[0].y_px = y0;
    v[1].x_px = x1; v[1].y_px = y0;
    v[2].x_px = x1; v[2].y_px = y1;
    eegs_triangle(v, st);
    v[1] = v[2];
    v[2].x_px = x0; v[2].y_px = y1;
    eegs_triangle(v, st);
}

void R_EEGS_BeginFrame(int width, int height)
{
    if (!s_inited) {
        ps2_vu1_geom_init();
        s_inited = 1;
        if (!s_logged) {
            extern void uart_puts_raw(const char *s);
            uart_puts_raw("[QUAKE] EEGS native VU0+GS (no TinyGL immediate mode)\n");
            s_logged = 1;
        }
    }
    s_vp_w = width;
    s_vp_h = height;
    s_sx = ((float)width - 0.5f) * 0.5f;
    s_sy = -((float)height - 0.5f) * 0.5f;
    s_tx = ((float)width - 0.5f) * 0.5f;
    s_ty = ((float)height - 0.5f) * 0.5f;
    if (width > 0 && height > 0)
        eegs_clear(0, 0, width, height, f_to_rgba(0.08f, 0.06f, 0.05f));
}

void R_EEGS_SetCamera(const float org[3], const float ang[3], int width, int height)
{
    float aspect, proj[16];
    (void)width; (void)height;
    if (s_vp_h <= 0) return;
    aspect = (float)s_vp_w / (float)s_vp_h;
    m_frustum(proj, -0.1f * aspect, 0.1f * aspect, -0.1f, 0.1f, 0.1f, 1000.0f);
    memcpy(s_mvp, proj, sizeof s_mvp);
    /* Match R_RenderView's TinyGL camera: Quake X-forward Y-left Z-up. */
    m_rotate(s_mvp, -90.0f, 1.0f, 0.0f, 0.0f);
    m_rotate(s_mvp,  90.0f, 0.0f, 0.0f, 1.0f);
    m_rotate(s_mvp, -ang[2], 1.0f, 0.0f, 0.0f);
    m_rotate(s_mvp, -ang[0], 0.0f, 1.0f, 0.0f);
    m_rotate(s_mvp, -ang[1], 0.0f, 0.0f, 1.0f);
    m_translate(s_mvp, -org[0], -org[1], -org[2]);
    ps2_vu1_geom_set_mvp(s_mvp);
}

static void submit_poly(const vu0_vec4_t *pc, const float *st, int n,
                        uint32_t rgba, const gs_tex_t *tex)
{
    clip_vert_t cv[EEGS_CLIP_MAX];
    eegs_vertex_t vtx[EEGS_CLIP_MAX];
    int i, textured;
    if (n < 3) return;
    if (n > EEGS_MAX_VERTS) n = EEGS_MAX_VERTS;
    textured = (tex != NULL);
    eegs_texture(tex);
    for (i = 0; i < n; i++) {
        cv[i].x = pc[i].x;
        cv[i].y = pc[i].y;
        cv[i].z = pc[i].z;
        cv[i].w = pc[i].w;
        cv[i].s = st ? st[i * 2 + 0] : 0.0f;
        cv[i].t = st ? st[i * 2 + 1] : 0.0f;
    }
    n = clip_poly(cv, n);
    if (n < 3) return;
    for (i = 0; i < n; i++) {
        if (map_clip_vert(&cv[i], rgba, &vtx[i]) < 0) return;
    }
    for (i = 1; i < n - 1; i++) {
        int32_t ax = vtx[i].x_px - vtx[0].x_px;
        int32_t ay = vtx[i].y_px - vtx[0].y_px;
        int32_t bx = vtx[i + 1].x_px - vtx[0].x_px;
        int32_t by = vtx[i + 1].y_px - vtx[0].y_px;
        if (ax * by - bx * ay == 0) continue;
        emit_tri(&vtx[0], &vtx[i], &vtx[i + 1], textured);
    }
}

void R_EEGS_Poly(const float *xyz, const float *st, int nverts,
                 uint32_t rgba, int gl_tex_idx)
{
    vu0_vec4_t in[EEGS_MAX_VERTS] __attribute__((aligned(16)));
    vu0_vec4_t out[EEGS_MAX_VERTS] __attribute__((aligned(16)));
    int i;
    if (!xyz || nverts < 3) return;
    if (nverts > EEGS_MAX_VERTS) nverts = EEGS_MAX_VERTS;
    for (i = 0; i < nverts; i++) {
        in[i].x = xyz[i * 3 + 0];
        in[i].y = xyz[i * 3 + 1];
        in[i].z = xyz[i * 3 + 2];
        in[i].w = 1.0f;
    }
    ps2_vu0_transform_batch(s_mvp, in, (size_t)nverts, out);
    submit_poly(out, st, nverts, rgba, tex_of(gl_tex_idx));
}

void R_EEGS_Tris(const float *xyz, const float *st, int ntris,
                 uint32_t rgba, int gl_tex_idx)
{
    vu0_vec4_t in[EEGS_MAX_VERTS] __attribute__((aligned(16)));
    vu0_vec4_t out[EEGS_MAX_VERTS] __attribute__((aligned(16)));
    const gs_tex_t *tex;
    int t, n, i;
    if (!xyz || ntris <= 0) return;
    tex = tex_of(gl_tex_idx);
    eegs_texture(tex);
    n = ntris * 3;
    /* Batch in chunks that fit the aligned scratch. */
    i = 0;
    while (i < n) {
        int chunk = n - i;
        int nv, k;
        if (chunk > EEGS_MAX_VERTS) chunk = EEGS_MAX_VERTS - (EEGS_MAX_VERTS % 3);
        nv = chunk;
        for (k = 0; k < nv; k++) {
            in[k].x = xyz[(i + k) * 3 + 0];
            in[k].y = xyz[(i + k) * 3 + 1];
            in[k].z = xyz[(i + k) * 3 + 2];
            in[k].w = 1.0f;
        }
        ps2_vu0_transform_batch(s_mvp, in, (size_t)nv, out);
        for (t = 0; t + 2 < nv; t += 3) {
            float tri_st[6];
            const float *stp = NULL;
            if (st) {
                for (k = 0; k < 3; k++) {
                    tri_st[k * 2 + 0] = st[(i + t + k) * 2 + 0];
                    tri_st[k * 2 + 1] = st[(i + t + k) * 2 + 1];
                }
                stp = tri_st;
            }
            submit_poly(&out[t], stp, 3, rgba, tex);
        }
        i += nv;
    }
}

void R_EEGS_Fill(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    eegs_texture(NULL);
    emit_rect(x, y, x + w, y + h, rgb_to_rgba(color));
}

void R_EEGS_String(int x, int y, const char *str, const uint8_t font[96][8])
{
    uint32_t rgba = f_to_rgba(0.85f, 0.70f, 0.15f);
    int cur_x;
    if (!str || !font) return;
    eegs_texture(NULL);
    cur_x = x;
    while (*str) {
        int num = (unsigned char)*str;
        int glyph, r;
        if (num < 32 || num > 127) num = '?';
        glyph = num - 32;
        for (r = 0; r < 8; r++) {
            uint8_t row = font[glyph][r];
            int c = 0;
            while (c < 8) {
                int run;
                if ((row & (0x80 >> c)) == 0) { c++; continue; }
                run = c;
                while (c < 8 && (row & (0x80 >> c))) c++;
                emit_rect(cur_x + run, y + r, cur_x + c, y + r + 1, rgba);
            }
        }
        cur_x += 8;
        str++;
    }
}

#endif /* BTRON_PS2_TARGET */
