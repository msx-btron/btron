/*
 * eegs_geom.c — shared EEGS clip / transform / 2D submit.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */
#include "eegs_geom.h"

#include <string.h>
#include <math.h>

#include "../gles/grid_map.h"

#if defined(BTRON_PS2_TARGET)
#include "../../../drivers/playstation2/ps2_vu0_kernel.h"
#endif

static int   s_vp_w, s_vp_h;
static float s_sx, s_sy, s_tx, s_ty;

static tgl_grid_pt_t s_grid_pts[EEGS_GEOM_GRID_CAP];
static tgl_tex_pt_t  s_tex_pts[EEGS_GEOM_QUAD_CAP * 4];

void eegs_mat_ident(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void eegs_mat_mul(float *out, const float *a, const float *b)
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

void eegs_mat_frustum(float *m, float l, float r, float b, float t, float n, float f)
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

void eegs_mat_rotate(float *m, float deg, float x, float y, float z)
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
    eegs_mat_ident(R);
    R[0] = xx * c1 + c;   R[4] = xy * c1 - zs;  R[8]  = xz * c1 + ys;
    R[1] = xy * c1 + zs;  R[5] = yy * c1 + c;   R[9]  = yz * c1 - xs;
    R[2] = xz * c1 - ys;  R[6] = yz * c1 + xs;  R[10] = zz * c1 + c;
    eegs_mat_mul(m, m, R);
}

void eegs_mat_translate(float *m, float x, float y, float z)
{
    float t[16];
    eegs_mat_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    eegs_mat_mul(m, m, t);
}

uint32_t eegs_geom_rgba(float r, float g, float b, float a)
{
    unsigned ri = (unsigned)(r * 255.0f); if (ri > 255u) ri = 255u;
    unsigned gi = (unsigned)(g * 255.0f); if (gi > 255u) gi = 255u;
    unsigned bi = (unsigned)(b * 255.0f); if (bi > 255u) bi = 255u;
    unsigned ai = (unsigned)(a * 255.0f); if (ai > 255u) ai = 255u;
    return ri | (gi << 8) | (bi << 16) | (ai << 24);
}

void eegs_geom_viewport(int width, int height)
{
    s_vp_w = width;
    s_vp_h = height;
    s_sx = ((float)width - 0.5f) * 0.5f;
    s_sy = -((float)height - 0.5f) * 0.5f;
    s_tx = ((float)width - 0.5f) * 0.5f;
    s_ty = ((float)height - 0.5f) * 0.5f;
}

static float plane_dist(const eegs_clip_vert_t *v, int plane)
{
    switch (plane) {
    case 0: return v->w + v->x;
    case 1: return v->w - v->x;
    case 2: return v->w + v->y;
    case 3: return v->w - v->y;
    case 4: return v->w + v->z;
    case 5: return v->w - v->z;
    default: return 0.0f;
    }
}

static void clip_intersect(eegs_clip_vert_t *o, const eegs_clip_vert_t *a,
                           const eegs_clip_vert_t *b, float t, int plane)
{
    o->x = a->x + t * (b->x - a->x);
    o->y = a->y + t * (b->y - a->y);
    o->z = a->z + t * (b->z - a->z);
    o->w = a->w + t * (b->w - a->w);
    o->s = a->s + t * (b->s - a->s);
    o->t = a->t + t * (b->t - a->t);
    switch (plane) {
    case 0: o->x = -o->w; break;
    case 1: o->x =  o->w; break;
    case 2: o->y = -o->w; break;
    case 3: o->y =  o->w; break;
    case 4: o->z = -o->w; break;
    case 5: o->z =  o->w; break;
    }
}

int eegs_clip_poly(eegs_clip_vert_t *verts, int n)
{
    eegs_clip_vert_t tmp[EEGS_GEOM_CLIP_MAX];
    int plane;
    for (plane = 0; plane < 6; plane++) {
        int i, outn = 0;
        if (n < 3) return 0;
        for (i = 0; i < n; i++) {
            const eegs_clip_vert_t *a = &verts[i];
            const eegs_clip_vert_t *b = &verts[(i + 1) % n];
            float da = plane_dist(a, plane);
            float db = plane_dist(b, plane);
            int ina = da >= 0.0f;
            int inb = db >= 0.0f;
            if (ina) {
                if (outn >= EEGS_GEOM_CLIP_MAX) return 0;
                tmp[outn++] = *a;
            }
            if (ina != inb) {
                float den = da - db;
                float t = (den == 0.0f) ? 0.0f : (da / den);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                if (outn >= EEGS_GEOM_CLIP_MAX) return 0;
                clip_intersect(&tmp[outn], a, b, t, plane);
                outn++;
            }
        }
        memcpy(verts, tmp, (size_t)outn * sizeof(eegs_clip_vert_t));
        n = outn;
    }
    return n;
}

int eegs_geom_map(const eegs_clip_vert_t *pc, uint32_t rgba, eegs_vertex_t *out)
{
    float w, winv;
    if (!pc || !out) return -1;
    w = pc->w;
    if (w <= 1e-4f) return -1;
    winv = 1.0f / w;
    out->x_px = (int32_t)(pc->x * winv * s_sx + s_tx);
    out->y_px = (int32_t)(pc->y * winv * s_sy + s_ty);
    out->z = 0u;
    out->rgba = rgba;
    out->s = pc->s;
    out->t = pc->t;
    if (out->x_px < -2047 || out->x_px > 2047) return -1;
    if (out->y_px < -2047 || out->y_px > 2047) return -1;
    return 0;
}

static void emit_tri(const eegs_vertex_t *a, const eegs_vertex_t *b,
                     const eegs_vertex_t *c, eegs_tri_state_t st)
{
    eegs_vertex_t v[3];
    v[0] = *a; v[1] = *b; v[2] = *c;
    eegs_triangle(v, st);
}

void eegs_geom_submit_poly(const eegs_clip_vert_t *pc, int n, uint32_t rgba,
                           const gs_tex_t *tex, eegs_tri_state_t st)
{
    eegs_clip_vert_t cv[EEGS_GEOM_CLIP_MAX];
    eegs_vertex_t vtx[EEGS_GEOM_CLIP_MAX];
    int i;
    if (!pc || n < 3) return;
    if (n > EEGS_GEOM_MAX_VERTS) n = EEGS_GEOM_MAX_VERTS;
    memcpy(cv, pc, (size_t)n * sizeof(eegs_clip_vert_t));
    n = eegs_clip_poly(cv, n);
    if (n < 3) return;
    st.textured = tex ? 1u : 0u;
    eegs_texture(tex);
    for (i = 0; i < n; i++) {
        if (eegs_geom_map(&cv[i], rgba, &vtx[i]) < 0) return;
    }
    for (i = 1; i < n - 1; i++) {
        int32_t ax = vtx[i].x_px - vtx[0].x_px;
        int32_t ay = vtx[i].y_px - vtx[0].y_px;
        int32_t bx = vtx[i + 1].x_px - vtx[0].x_px;
        int32_t by = vtx[i + 1].y_px - vtx[0].y_px;
        if (ax * by - bx * ay == 0) continue;
        emit_tri(&vtx[0], &vtx[i], &vtx[i + 1], st);
    }
}

static void xform_batch(const float mvp[16], const float *xyz, int n,
                        eegs_clip_vert_t *pc, const float *uv)
{
    int i;
#if defined(BTRON_PS2_TARGET)
    vu0_vec4_t in[EEGS_GEOM_MAX_VERTS] __attribute__((aligned(16)));
    vu0_vec4_t out[EEGS_GEOM_MAX_VERTS] __attribute__((aligned(16)));
    for (i = 0; i < n; i++) {
        in[i].x = xyz[i * 3 + 0];
        in[i].y = xyz[i * 3 + 1];
        in[i].z = xyz[i * 3 + 2];
        in[i].w = 1.0f;
    }
    ps2_vu0_transform_batch(mvp, in, (size_t)n, out);
    for (i = 0; i < n; i++) {
        pc[i].x = out[i].x;
        pc[i].y = out[i].y;
        pc[i].z = out[i].z;
        pc[i].w = out[i].w;
        pc[i].s = uv ? uv[i * 2 + 0] : 0.0f;
        pc[i].t = uv ? uv[i * 2 + 1] : 0.0f;
    }
#else
    for (i = 0; i < n; i++) {
        float x = xyz[i * 3 + 0], y = xyz[i * 3 + 1], z = xyz[i * 3 + 2];
        pc[i].x = mvp[0] * x + mvp[4] * y + mvp[8]  * z + mvp[12];
        pc[i].y = mvp[1] * x + mvp[5] * y + mvp[9]  * z + mvp[13];
        pc[i].z = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
        pc[i].w = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
        pc[i].s = uv ? uv[i * 2 + 0] : 0.0f;
        pc[i].t = uv ? uv[i * 2 + 1] : 0.0f;
    }
#endif
}

void eegs_geom_world_poly(const float mvp[16], const float *xyz, const float *uv,
                          int nverts, uint32_t rgba, const gs_tex_t *tex,
                          eegs_tri_state_t tri)
{
    eegs_clip_vert_t pc[EEGS_GEOM_MAX_VERTS];
    if (!mvp || !xyz || nverts < 3) return;
    if (nverts > EEGS_GEOM_MAX_VERTS) nverts = EEGS_GEOM_MAX_VERTS;
    xform_batch(mvp, xyz, nverts, pc, uv);
    eegs_geom_submit_poly(pc, nverts, rgba, tex, tri);
}

void eegs_geom_world_tris(const float mvp[16], const float *xyz, const float *uv,
                          int ntris, uint32_t rgba, const gs_tex_t *tex,
                          eegs_tri_state_t tri)
{
    eegs_clip_vert_t pc[EEGS_GEOM_MAX_VERTS];
    int i, n, nv;
    if (!mvp || !xyz || ntris <= 0) return;
    n = ntris * 3;
    i = 0;
    while (i < n) {
        int k;
        nv = n - i;
        if (nv > EEGS_GEOM_MAX_VERTS) nv = EEGS_GEOM_MAX_VERTS - (EEGS_GEOM_MAX_VERTS % 3);
        xform_batch(mvp, xyz + i * 3, nv, pc, uv ? uv + i * 2 : NULL);
        for (k = 0; k + 2 < nv; k += 3)
            eegs_geom_submit_poly(&pc[k], 3, rgba, tex, tri);
        i += nv;
    }
}

void eegs_geom_fill_rect(int x, int y, int w, int h, uint32_t rgba)
{
    eegs_vertex_t v[4];
    eegs_tri_state_t st;
    int i;
    if (w <= 0 || h <= 0) return;
    memset(&st, 0, sizeof st);
    eegs_texture(NULL);
    for (i = 0; i < 4; i++) {
        v[i].z = 0u;
        v[i].s = 0.0f;
        v[i].t = 0.0f;
        v[i].rgba = rgba;
    }
    v[0].x_px = x;     v[0].y_px = y;
    v[1].x_px = x + w; v[1].y_px = y;
    v[2].x_px = x + w; v[2].y_px = y + h;
    v[3].x_px = x;     v[3].y_px = y + h;
    eegs_geom_fill_quad(v, st);
}

void eegs_geom_fill_quad(const eegs_vertex_t v[4], eegs_tri_state_t st)
{
    eegs_vertex_t t[3];
    if (!v) return;
    t[0] = v[0]; t[1] = v[1]; t[2] = v[2];
    eegs_triangle(t, st);
    t[1] = v[2]; t[2] = v[3];
    eegs_triangle(t, st);
}

void eegs_geom_line_loop(const eegs_vertex_t *v, int n, eegs_tri_state_t st)
{
    int i;
    if (!v || n < 2) return;
    for (i = 0; i < n; i++) {
        eegs_vertex_t e[2];
        e[0] = v[i];
        e[1] = v[(i + 1) % n];
        eegs_line(e, st);
    }
}

static void map_ok(tgl_grid_map_t *g, int vp_w, int vp_h, int blended, int brighten, int smooth)
{
    memset(g, 0, sizeof *g);
    g->ok = 1u;
    g->vp_x0 = 0;
    g->vp_y0 = 0;
    g->vp_x1 = vp_w > 0 ? vp_w - 1 : 0;
    g->vp_y1 = vp_h > 0 ? vp_h - 1 : 0;
    g->blended = blended ? 1u : 0u;
    g->brighten = brighten ? 1u : 0u;
    g->smooth = smooth ? 1u : 0u;
    g->a_byte = 255u;
}

int eegs_geom_grid_px(const float *xs, const float *ys, const float *bri,
                      int rows, int cols, int vp_w, int vp_h, int brighten)
{
    tgl_grid_map_t gm;
    int n, i;
    if (!xs || !ys || !bri || rows < 2 || cols < 1) return -1;
    n = rows * cols;
    if (n > EEGS_GEOM_GRID_CAP) return -1;
    map_ok(&gm, vp_w, vp_h, 1, brighten, 1);
    for (i = 0; i < n; i++) {
        unsigned g;
        float b = bri[i];
        if (b < 0.0f) b = 0.0f;
        if (b > 1.0f) b = 1.0f;
        g = (unsigned)(b * 255.0f);
        if (g > 255u) g = 255u;
        s_grid_pts[i].x_px = (int32_t)xs[i];
        s_grid_pts[i].y_px = (int32_t)ys[i];
        s_grid_pts[i].rgba = g | (g << 8) | (g << 16) | (255u << 24);
    }
    eegs_texture(NULL);
    return eegs_grid_strip(&gm, s_grid_pts, rows, cols);
}

int eegs_geom_tex_quads_px(const gs_tex_t *tex,
                           const float *xs, const float *ys,
                           const float *us, const float *vs,
                           const float *cols, int nquads,
                           int vp_w, int vp_h, int blended, int brighten)
{
    tgl_tex_map_t tm;
    int n, i;
    if (!tex || !tex->plane || !xs || !ys || !us || !vs || !cols || nquads < 1)
        return -1;
    if (nquads > EEGS_GEOM_QUAD_CAP) return -1;
    n = nquads * 4;
    memset(&tm, 0, sizeof tm);
    map_ok(&tm.g, vp_w, vp_h, blended, brighten, 0);
    tm.pixmap = tex->plane;
    for (i = 0; i < n; i++) {
        s_tex_pts[i].x_px = (int32_t)xs[i];
        s_tex_pts[i].y_px = (int32_t)ys[i];
        s_tex_pts[i].s = eegs_geom_uv_to_st(us[i], tex->px_w);
        s_tex_pts[i].t = eegs_geom_uv_to_st(vs[i], tex->px_h);
        s_tex_pts[i].rgba = eegs_geom_rgba(cols[i * 4 + 0], cols[i * 4 + 1],
                                           cols[i * 4 + 2], cols[i * 4 + 3]);
    }
    return eegs_tex_quads(&tm, s_tex_pts, nquads);
}
