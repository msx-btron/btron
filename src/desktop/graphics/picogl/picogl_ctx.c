/*
 * picogl_ctx.c — GLES door onto the PicoGL triangles.
 *
 * GLGears is this door. A glEnd emits the same triangles as R_EEGS_Tris.
 * Flat shade uses the provoking vertex. One directional light. Back faces
 * are culled in screen space after the Y flip.
 */
#ifndef __STDC_IEC_559__
#define __STDC_IEC_559__ 1
#endif
#define BTRON_GL_NO_MACRO_SHIMS
#include "../gles/gl_dispatch.h"
#include "picogl.h"
#if defined(PICO_REAL_HW)
#include "pico_hal.h"
#endif

#include <math.h>
#include <string.h>

#define PICOGL_STACK 8
#define PICOGL_VMAX  4

typedef struct {
    float x, y, z;
    uint32_t rgba;
} pgv_t;

static float s_mv[PICOGL_STACK][16];
static float s_proj[PICOGL_STACK][16];
static int   s_mv_sp, s_proj_sp;
static int   s_mode;
static int   s_inited;
static int   s_in_begin;
static int   s_prim;
static int   s_vn;
static int   s_vcount;
static pgv_t s_gv[PICOGL_VMAX];

static int   s_cull_face;
static int   s_lighting;
static int   s_light0;
static int   s_depth;
static float s_nx, s_ny, s_nz;
static float s_cr, s_cg, s_cb;
static float s_mat_amb[3];
static float s_mat_diff[3];
static float s_l_amb[3];
static float s_l_diff[3];
static float s_l_dir[3];
static uint16_t s_clear_rgb;

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
    if (rl == 0.0f || tb == 0.0f || fn == 0.0f || n == 0.0f) return;
    m[0]  = (2.0f * n) / rl;
    m[5]  = (2.0f * n) / tb;
    m[8]  = (r + l) / rl;
    m[9]  = (t + b) / tb;
    m[10] = -(f + n) / fn;
    m[11] = -1.0f;
    m[14] = -(2.0f * f * n) / fn;
}

static void mat_rotate_into(float *m, float deg, float x, float y, float z)
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

static void mat_translate_into(float *m, float x, float y, float z)
{
    float t[16];
    mat_ident(t);
    t[12] = x; t[13] = y; t[14] = z;
    mat_mul(m, m, t);
}

static void ensure(void)
{
    if (s_inited) return;
    s_inited = 1;
    s_mode = GL_MODELVIEW;
    mat_ident(s_mv[0]);
    mat_ident(s_proj[0]);
    s_cr = s_cg = s_cb = 1.0f;
    s_nx = 0.0f; s_ny = 0.0f; s_nz = 1.0f;
    s_mat_amb[0] = s_mat_amb[1] = s_mat_amb[2] = 0.2f;
    s_mat_diff[0] = s_mat_diff[1] = s_mat_diff[2] = 0.8f;
    s_l_amb[0] = s_l_amb[1] = s_l_amb[2] = 0.2f;
    s_l_diff[0] = s_l_diff[1] = s_l_diff[2] = 1.0f;
    s_l_dir[0] = 0.0f; s_l_dir[1] = 0.0f; s_l_dir[2] = 1.0f;
    s_light0 = 1;
    s_depth = 1;
    s_clear_rgb = 0;
}

static float *cur_mat(void)
{
    ensure();
    if (s_mode == GL_PROJECTION) return s_proj[s_proj_sp];
    return s_mv[s_mv_sp];
}

static unsigned u8f(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    return (unsigned)(c * 255.0f + 0.5f);
}

static uint32_t pack_rgb(float r, float g, float b)
{
    return (uint32_t)u8f(r) | ((uint32_t)u8f(g) << 8) | ((uint32_t)u8f(b) << 16) | 0xFF000000u;
}

static uint32_t shade_vertex(void)
{
    const float *m;
    float ex, ey, ez, len, ndot, r, g, b;
    if (!s_lighting) return pack_rgb(s_cr, s_cg, s_cb);
    m = s_mv[s_mv_sp];
    ex = m[0] * s_nx + m[4] * s_ny + m[8]  * s_nz;
    ey = m[1] * s_nx + m[5] * s_ny + m[9]  * s_nz;
    ez = m[2] * s_nx + m[6] * s_ny + m[10] * s_nz;
    len = sqrtf(ex * ex + ey * ey + ez * ez);
    if (len > 1e-8f) { ex /= len; ey /= len; ez /= len; }
    ndot = ex * s_l_dir[0] + ey * s_l_dir[1] + ez * s_l_dir[2];
    if (!s_light0 || ndot < 0.0f) ndot = 0.0f;
    r = s_mat_amb[0] * s_l_amb[0] + ndot * s_mat_diff[0] * s_l_diff[0];
    g = s_mat_amb[1] * s_l_amb[1] + ndot * s_mat_diff[1] * s_l_diff[1];
    b = s_mat_amb[2] * s_l_amb[2] + ndot * s_mat_diff[2] * s_l_diff[2];
    return pack_rgb(r, g, b);
}

static void emit_tri(int i0, int i1, int i2)
{
    float xyz[9];
    xyz[0] = s_gv[i0].x; xyz[1] = s_gv[i0].y; xyz[2] = s_gv[i0].z;
    xyz[3] = s_gv[i1].x; xyz[4] = s_gv[i1].y; xyz[5] = s_gv[i1].z;
    xyz[6] = s_gv[i2].x; xyz[7] = s_gv[i2].y; xyz[8] = s_gv[i2].z;
    picogl_poly(xyz, NULL, 3, s_gv[i2].rgba, -1);
}

static void emit_ready(void)
{
    if (s_prim == GL_TRIANGLES && s_vn == 3) {
        emit_tri(0, 1, 2);
        s_vn = 0;
    } else if (s_prim == GL_TRIANGLE_FAN && s_vn == 3) {
        emit_tri(0, 1, 2);
        s_gv[1] = s_gv[2];
        s_vn = 2;
    } else if (s_prim == GL_TRIANGLE_STRIP && s_vn == 3) {
        if (s_vcount & 1) emit_tri(0, 1, 2);
        else emit_tri(2, 1, 0);
        s_gv[0] = s_gv[1];
        s_gv[1] = s_gv[2];
        s_vn = 2;
    } else if (s_prim == GL_QUADS && s_vn == 4) {
        emit_tri(0, 1, 2);
        emit_tri(0, 2, 3);
        s_vn = 0;
    } else if (s_prim == GL_QUAD_STRIP && s_vn == 4) {
        emit_tri(0, 1, 2);
        emit_tri(1, 3, 2);
        s_gv[0] = s_gv[2];
        s_gv[1] = s_gv[3];
        s_vn = 2;
    }
}

void picogl_gl_begin(GLenum mode)
{
    float mvp[16];
    ensure();
    s_prim = (int)mode;
    s_vn = 0;
    s_vcount = 0;
    s_in_begin = 1;
    mat_mul(mvp, s_proj[s_proj_sp], s_mv[s_mv_sp]);
    picogl_set_mvp(mvp);
    picogl_set_cull(s_cull_face);
    picogl_set_depth(s_depth);
}

void picogl_gl_end(void)
{
    s_in_begin = 0;
    s_vn = 0;
    /* On the device the ring stays queued so the three gears overlap
     * core 1. The join is the flush before the panel read. Host tests
     * read the target at glEnd, so they join here. */
#if !defined(PICO_REAL_HW)
    picogl_flush_target();
#endif
}

void picogl_gl_vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (!s_in_begin || s_vn >= PICOGL_VMAX) return;
    s_gv[s_vn].x = x;
    s_gv[s_vn].y = y;
    s_gv[s_vn].z = z;
    s_gv[s_vn].rgba = shade_vertex();
    s_vn++;
    s_vcount++;
    emit_ready();
}

void picogl_gl_normal3f(GLfloat x, GLfloat y, GLfloat z)
{
    s_nx = x; s_ny = y; s_nz = z;
}

void picogl_gl_color3f(GLfloat r, GLfloat g, GLfloat b)
{
    s_cr = r; s_cg = g; s_cb = b;
}

void picogl_gl_color3fv(const GLfloat *v)
{
    if (!v) return;
    picogl_gl_color3f(v[0], v[1], v[2]);
}

void picogl_gl_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    (void)a;
    picogl_gl_color3f(r, g, b);
}

void picogl_gl_vertex2f(GLfloat x, GLfloat y)
{
    picogl_gl_vertex3f(x, y, 0.0f);
}

void picogl_gl_matrix_mode(GLenum mode)
{
    ensure();
    if (mode == GL_PROJECTION || mode == GL_MODELVIEW) s_mode = (int)mode;
}

void picogl_gl_load_identity(void)
{
    mat_ident(cur_mat());
}

void picogl_gl_push_matrix(void)
{
    ensure();
    if (s_mode == GL_PROJECTION) {
        if (s_proj_sp + 1 >= PICOGL_STACK) return;
        memcpy(s_proj[s_proj_sp + 1], s_proj[s_proj_sp], sizeof s_proj[0]);
        s_proj_sp++;
    } else {
        if (s_mv_sp + 1 >= PICOGL_STACK) return;
        memcpy(s_mv[s_mv_sp + 1], s_mv[s_mv_sp], sizeof s_mv[0]);
        s_mv_sp++;
    }
}

void picogl_gl_pop_matrix(void)
{
    ensure();
    if (s_mode == GL_PROJECTION) {
        if (s_proj_sp > 0) s_proj_sp--;
    } else if (s_mv_sp > 0) {
        s_mv_sp--;
    }
}

void picogl_gl_rotate_f(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    mat_rotate_into(cur_mat(), angle, x, y, z);
}

void picogl_gl_translate_f(GLfloat x, GLfloat y, GLfloat z)
{
    mat_translate_into(cur_mat(), x, y, z);
}

void picogl_gl_scale_f(GLfloat x, GLfloat y, GLfloat z)
{
    float *m = cur_mat();
    m[0] *= x; m[1] *= x; m[2] *= x; m[3] *= x;
    m[4] *= y; m[5] *= y; m[6] *= y; m[7] *= y;
    m[8] *= z; m[9] *= z; m[10] *= z; m[11] *= z;
}

void picogl_gl_frustum(double l, double r, double b, double t, double n, double f)
{
    float F[16];
    mat_frustum(F, (float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
    mat_mul(cur_mat(), cur_mat(), F);
}

void picogl_gl_ortho(double l, double r, double b, double t, double n, double f)
{
    float O[16];
    float rl = (float)(r - l), tb = (float)(t - b), fn = (float)(f - n);
    memset(O, 0, sizeof O);
    if (rl == 0.0f || tb == 0.0f || fn == 0.0f) return;
    O[0] = 2.0f / rl;
    O[5] = 2.0f / tb;
    O[10] = -2.0f / fn;
    O[12] = -(float)(r + l) / rl;
    O[13] = -(float)(t + b) / tb;
    O[14] = -(float)(f + n) / fn;
    O[15] = 1.0f;
    mat_mul(cur_mat(), cur_mat(), O);
}

void picogl_gl_viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    (void)x; (void)y;
    ensure();
    if (w > 0 && h > 0) picogl_begin_frame(w, h);
}

void picogl_gl_clear(GLbitfield mask)
{
    int color = (mask & GL_COLOR_BUFFER_BIT) != 0;
    int depth = (mask & GL_DEPTH_BUFFER_BIT) != 0;
    picogl_clear_target(color, depth, s_clear_rgb);
}

void picogl_gl_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    (void)a;
    s_clear_rgb = (uint16_t)(((u8f(r) >> 3) << 11) | ((u8f(g) >> 2) << 5) | (u8f(b) >> 3));
}

void picogl_gl_enable(GLenum cap)
{
    ensure();
    if (cap == GL_CULL_FACE) s_cull_face = 1;
    else if (cap == GL_LIGHTING) s_lighting = 1;
    else if (cap == GL_LIGHT0) s_light0 = 1;
    else if (cap == GL_DEPTH_TEST) s_depth = 1;
    picogl_set_cull(s_cull_face);
    picogl_set_depth(s_depth);
}

void picogl_gl_disable(GLenum cap)
{
    ensure();
    if (cap == GL_CULL_FACE) s_cull_face = 0;
    else if (cap == GL_LIGHTING) s_lighting = 0;
    else if (cap == GL_LIGHT0) s_light0 = 0;
    else if (cap == GL_DEPTH_TEST) s_depth = 0;
    picogl_set_cull(s_cull_face);
    picogl_set_depth(s_depth);
}

void picogl_gl_shade_model(GLenum mode)
{
    (void)mode; /* Flat. The provoking vertex supplies the triangle color. */
}

void picogl_gl_light_fv(GLenum light, GLenum pname, const GLfloat *params)
{
    const float *m;
    float x, y, z, w, tx, ty, tz, tw, len;
    ensure();
    if (light != GL_LIGHT0 || !params) return;
    if (pname == GL_AMBIENT) {
        s_l_amb[0] = params[0]; s_l_amb[1] = params[1]; s_l_amb[2] = params[2];
    } else if (pname == GL_DIFFUSE || pname == GL_SPECULAR) {
        if (pname == GL_DIFFUSE) {
            s_l_diff[0] = params[0]; s_l_diff[1] = params[1]; s_l_diff[2] = params[2];
        }
    } else if (pname == GL_POSITION) {
        m = s_mv[s_mv_sp];
        x = params[0]; y = params[1]; z = params[2]; w = params[3];
        tx = m[0] * x + m[4] * y + m[8]  * z + m[12] * w;
        ty = m[1] * x + m[5] * y + m[9]  * z + m[13] * w;
        tz = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
        tw = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
        if (tw != 0.0f) { tx /= tw; ty /= tw; tz /= tw; }
        len = sqrtf(tx * tx + ty * ty + tz * tz);
        if (len > 1e-8f) { tx /= len; ty /= len; tz /= len; }
        s_l_dir[0] = tx; s_l_dir[1] = ty; s_l_dir[2] = tz;
    }
}

void picogl_gl_material_fv(GLenum face, GLenum pname, const GLfloat *params)
{
    if (!params) return;
    if (face != GL_FRONT && face != GL_FRONT_AND_BACK) return;
    if (pname == GL_AMBIENT || pname == GL_AMBIENT_AND_DIFFUSE) {
        s_mat_amb[0] = params[0]; s_mat_amb[1] = params[1]; s_mat_amb[2] = params[2];
    }
    if (pname == GL_DIFFUSE || pname == GL_AMBIENT_AND_DIFFUSE) {
        s_mat_diff[0] = params[0]; s_mat_diff[1] = params[1]; s_mat_diff[2] = params[2];
    }
}

static GLuint stub_gen_lists(GLsizei range) { (void)range; return 0; }
static void stub_void(void) {}
static void stub_new_list(GLuint list, GLenum mode) { (void)list; (void)mode; }
static void stub_call_list(GLuint list) { (void)list; }
static void stub_tex2(GLfloat s, GLfloat t) { (void)s; (void)t; }
static void stub_tex2v(const GLfloat *v) { (void)v; }
static void stub_gen_tex(GLsizei n, GLuint *t) { (void)n; (void)t; }
static void stub_bind(GLenum t, GLuint x) { (void)t; (void)x; }
static void stub_image(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f,
                      GLenum g, GLenum h, const void *p)
{
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)p;
}
static void stub_texp(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
static void stub_blend(GLenum s, GLenum d) { (void)s; (void)d; }

gl_ops_t g_picogl_ops = {
    picogl_gl_begin,
    picogl_gl_end,
    picogl_gl_vertex3f,
    picogl_gl_normal3f,
    picogl_gl_color3f,
    picogl_gl_color3fv,
    picogl_gl_matrix_mode,
    picogl_gl_load_identity,
    picogl_gl_push_matrix,
    picogl_gl_pop_matrix,
    picogl_gl_rotate_f,
    picogl_gl_translate_f,
    picogl_gl_frustum,
    picogl_gl_viewport,
    picogl_gl_clear,
    picogl_gl_clear_color,
    picogl_gl_enable,
    picogl_gl_disable,
    picogl_gl_shade_model,
    picogl_gl_light_fv,
    picogl_gl_material_fv,
    stub_gen_lists,
    stub_new_list,
    stub_void,
    stub_call_list,
    stub_tex2,
    stub_tex2v,
    stub_gen_tex,
    stub_bind,
    stub_image,
    stub_texp,
    stub_void,
    picogl_gl_scale_f,
    stub_blend,
    picogl_gl_color4f,
    picogl_gl_vertex2f,
    picogl_gl_ortho
};

void picogl_log_frame(const char *client, uint32_t ms, int bands)
{
#if defined(PICO_REAL_HW)
    char buf[96];
    char *p = buf;
    char *end = buf + (int)sizeof buf - 2;
    unsigned vals[4];
    const char *tags[4];
    const char *t;
    int i;

    t = "picogl ";
    while (*t && p < end) *p++ = *t++;
    t = (client && client[0]) ? client : "gears";
    while (*t && p < end) *p++ = *t++;
    if (p < end) *p++ = ' ';

    vals[0] = ms;
    tags[0] = "";
    vals[1] = (unsigned)picogl_tri_n();
    tags[1] = " tri=";
    vals[2] = (unsigned)picogl_span_n();
    tags[2] = " span=";
    vals[3] = (unsigned)picogl_drop();
    tags[3] = " drop=";
    for (i = 0; i < 4; i++) {
        char tmp[10];
        int n = 0;
        unsigned v = vals[i];
        t = tags[i];
        while (*t && p < end) *p++ = *t++;
        if (v == 0) tmp[n++] = '0';
        while (v && n < 10) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (n > 0 && p < end) *p++ = tmp[--n];
    }
    t = " bands=";
    while (*t && p < end) *p++ = *t++;
    {
        char tmp[10];
        int n = 0;
        unsigned v = (unsigned)bands;
        if (v == 0) tmp[n++] = '0';
        while (v && n < 10) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
        while (n > 0 && p < end) *p++ = tmp[--n];
    }
    *p++ = '\n';
    *p = '\0';
    pico_hal_uart_puts(buf);
#else
    (void)client;
    (void)ms;
    (void)bands;
#endif
}