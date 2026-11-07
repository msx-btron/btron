/*
 * src/gl/backend_v3d.c — VideoCore VI (V3D 4.2) Hardware Rasterizer Backend
 *
 * Broadcom BCM2711 / Raspberry Pi 4 & Pi 400
 *
 * Implements hardware-accelerated OpenGL ES 1.1 / 2.0 subset:
 * 1. Control List Executor (CLE) Tile Binning & Rendering
 * 2. Hardware Clear (V3D Tile Clear via Thread 0 & Thread 1)
 * 3. Fixed-Function Graphical Command Pipeline:
 *    - ModelView & Projection 4x4 Matrix Stacks
 *    - Primitive Assembly & Decomposition (Triangles, Quads, Strips, Fans, Lines)
 *    - QPU Shader Selection (Basic Color, Texture Modulate, Directional Lighting)
 *    - Direct Streaming to V3D CLE Binner & Tile Render Lists
 *
 * Conforms to NASA JPL Rule 1 (bounded loops) and Rule 3 (no dynamic allocations).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/btron.h>
#include <btron/wnd.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#include "backend_v3d.h"
#include "backend_tinygl.h"

#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
#include <arch/bcm283x/v3d_regs.h>
#include <arch/bcm283x/v3d.h>
#include <arch/bcm283x/v3d_qpu.h>
#else
static inline int v3d_is_available(void) { return 0; }
static inline int v3d_clear(uint32_t c, uint32_t w, uint32_t h) { (void)c; (void)w; (void)h; return 0; }
static inline int v3d_set_viewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; return 0; }
static inline int v3d_set_render_target(void *p) { (void)p; return 0; }
static inline int v3d_upload_texture(uint32_t id, const void *p, uint32_t w, uint32_t h) { (void)id; (void)p; (void)w; (void)h; return 0; }
static inline int v3d_flush(void) { return 0; }
typedef struct {
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float pad;
} v3d_vertex_t;
#define V3D_PRIM_TRIANGLES              0x04
#define V3D_PRIM_LINES                  0x01
#define V3D_SHADER_BASIC_COLOR          0
#define V3D_SHADER_TEXTURE_MODULATE     1
#define V3D_SHADER_LIGHTING             2
static inline int v3d_emit_primitive_batch(int p, int s, const v3d_vertex_t *v, uint32_t c) {
    (void)p; (void)s; (void)v; (void)c; return 0;
}
#endif

extern void uart_puts_raw(const char *s);

/* ── 4x4 Matrix Mathematics ───────────────────────────────────────── */

typedef struct {
    float m[4][4];
} v3d_mat4_t;

static inline void v3d_mat4_identity(v3d_mat4_t *dst) {
    memset(dst, 0, sizeof(v3d_mat4_t));
    dst->m[0][0] = 1.0f;
    dst->m[1][1] = 1.0f;
    dst->m[2][2] = 1.0f;
    dst->m[3][3] = 1.0f;
}

static inline void v3d_mat4_multiply(v3d_mat4_t *dst, const v3d_mat4_t *a, const v3d_mat4_t *b) {
    v3d_mat4_t res;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            res.m[r][c] = a->m[r][0] * b->m[0][c] +
                          a->m[r][1] * b->m[1][c] +
                          a->m[r][2] * b->m[2][c] +
                          a->m[r][3] * b->m[3][c];
        }
    }
    *dst = res;
}

static inline void v3d_mat4_translate(v3d_mat4_t *m, float x, float y, float z) {
    v3d_mat4_t t;
    v3d_mat4_identity(&t);
    t.m[0][3] = x;
    t.m[1][3] = y;
    t.m[2][3] = z;
    v3d_mat4_multiply(m, m, &t);
}

static inline void v3d_mat4_scale(v3d_mat4_t *m, float x, float y, float z) {
    v3d_mat4_t s;
    v3d_mat4_identity(&s);
    s.m[0][0] = x;
    s.m[1][1] = y;
    s.m[2][2] = z;
    v3d_mat4_multiply(m, m, &s);
}

static inline void v3d_mat4_rotate(v3d_mat4_t *m, float angle_deg, float x, float y, float z) {
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = (float)cos(rad);
    float s = (float)sin(rad);
    float len = (float)sqrt(x * x + y * y + z * z);
    if (len > 0.00001f) {
        x /= len; y /= len; z /= len;
    }

    v3d_mat4_t r;
    v3d_mat4_identity(&r);
    r.m[0][0] = x * x * (1.0f - c) + c;
    r.m[0][1] = x * y * (1.0f - c) - z * s;
    r.m[0][2] = x * z * (1.0f - c) + y * s;

    r.m[1][0] = y * x * (1.0f - c) + z * s;
    r.m[1][1] = y * y * (1.0f - c) + c;
    r.m[1][2] = y * z * (1.0f - c) - x * s;

    r.m[2][0] = z * x * (1.0f - c) - y * s;
    r.m[2][1] = z * y * (1.0f - c) + x * s;
    r.m[2][2] = z * z * (1.0f - c) + c;

    v3d_mat4_multiply(m, m, &r);
}

static inline void v3d_mat4_frustum(v3d_mat4_t *m, double l, double r, double b,
                                    double t, double n, double f) {
    v3d_mat4_t p;
    memset(&p, 0, sizeof(v3d_mat4_t));
    p.m[0][0] = (float)(2.0 * n / (r - l));
    p.m[0][2] = (float)((r + l) / (r - l));
    p.m[1][1] = (float)(2.0 * n / (t - b));
    p.m[1][2] = (float)((t + b) / (t - b));
    p.m[2][2] = (float)(-(f + n) / (f - n));
    p.m[2][3] = (float)(-(2.0 * f * n) / (f - n));
    p.m[3][2] = -1.0f;

    v3d_mat4_multiply(m, m, &p);
}

static inline void v3d_mat4_ortho(v3d_mat4_t *m, double l, double r, double b,
                                  double t, double n, double f) {
    if (r - l == 0.0 || t - b == 0.0 || f - n == 0.0) return;

    v3d_mat4_t p;
    memset(&p, 0, sizeof(v3d_mat4_t));
    p.m[0][0] = (float)(2.0 / (r - l));
    p.m[1][1] = (float)(2.0 / (t - b));
    p.m[2][2] = (float)(-2.0 / (f - n));
    p.m[0][3] = (float)(-(r + l) / (r - l));
    p.m[1][3] = (float)(-(t + b) / (t - b));
    p.m[2][3] = (float)(-(f + n) / (f - n));
    p.m[3][3] = 1.0f;

    v3d_mat4_multiply(m, m, &p);
}

static inline void v3d_mat4_transform_vec4(const v3d_mat4_t *m, float x, float y, float z,
                                          float *ox, float *oy, float *oz, float *ow) {
    *ox = m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z + m->m[0][3];
    *oy = m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z + m->m[1][3];
    *oz = m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z + m->m[2][3];
    *ow = m->m[3][0] * x + m->m[3][1] * y + m->m[3][2] * z + m->m[3][3];
}

static inline void v3d_mat4_transform_normal(const v3d_mat4_t *m, float nx, float ny, float nz,
                                             float *onx, float *ony, float *onz) {
    float tx = m->m[0][0] * nx + m->m[0][1] * ny + m->m[0][2] * nz;
    float ty = m->m[1][0] * nx + m->m[1][1] * ny + m->m[1][2] * nz;
    float tz = m->m[2][0] * nx + m->m[2][1] * ny + m->m[2][2] * nz;
    float len = (float)sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 0.00001f) {
        *onx = tx / len;
        *ony = ty / len;
        *onz = tz / len;
    } else {
        *onx = 0.0f; *ony = 0.0f; *onz = 1.0f;
    }
}

/* ── Hardware Backend State ────────────────────────────────────────── */

#define V3D_MATRIX_STACK_MAX    16
#define V3D_BATCH_MAX_VERTS     1024

static int   s_v3d_w = 0;
static int   s_v3d_h = 0;
static void *s_v3d_pixel_buf = NULL;
static uint32_t s_v3d_clear_col = 0xFF000000u;

/* Viewport */
static int s_vp_x = 0;
static int s_vp_y = 0;
static int s_vp_w = 1024;
static int s_vp_h = 768;

/* Matrix Stacks */
static v3d_mat4_t s_mv_stack[V3D_MATRIX_STACK_MAX];
static int        s_mv_top = 0;

static v3d_mat4_t s_proj_stack[V3D_MATRIX_STACK_MAX];
static int        s_proj_top = 0;

static GLenum     s_matrix_mode = GL_MODELVIEW;

/* Current Vertex Attributes */
static float s_cur_nx = 0.0f, s_cur_ny = 0.0f, s_cur_nz = 1.0f;
static float s_cur_r  = 1.0f, s_cur_g  = 1.0f, s_cur_b  = 1.0f;
static float s_cur_a  = 1.0f;
static float s_cur_u  = 0.0f, s_cur_v  = 0.0f;

/* Material Attributes */
static float s_mat_diffuse[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
static float s_mat_ambient[4] = { 0.2f, 0.2f, 0.2f, 1.0f };

/* Light 0 */
static float s_light0_pos[4]     = { 0.0f, 0.0f, 1.0f, 0.0f };
static float s_light0_diffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static float s_light0_ambient[4] = { 0.2f, 0.2f, 0.2f, 1.0f };

/* State Caps */
static int s_lighting_enabled = 0;
static int s_light0_enabled = 0;
static int s_color_material_enabled = 1;
static int s_depth_test_enabled = 1;
static int s_cull_face_enabled = 0;
static int s_blend_enabled = 0;
static int s_tex_enabled = 0;
static GLenum s_blend_src = GL_ONE;
static GLenum s_blend_dst = GL_ZERO;

/* Primitive Assembly Window */
typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
} v3d_in_vert_t;

static GLenum        s_prim_mode = GL_TRIANGLES;
static int           s_in_begin = 0;
static v3d_in_vert_t s_win[4];
static int           s_win_n = 0;
static int           s_win_i = 0;
static v3d_in_vert_t s_fan_base;
static v3d_in_vert_t s_loop_first;

/* Vertex Batch Buffer for V3D Hardware Control List */
static v3d_vertex_t  s_batch_verts[V3D_BATCH_MAX_VERTS];
static uint32_t      s_batch_count = 0;

/* ── Forward Declarations ─────────────────────────────────────────── */
gl_ops_t g_v3d_ops;

/* ── Vertex Batch Emission to V3D CLE ─────────────────────────────── */

static void v3d_flush_batch(void) {
    if (s_batch_count == 0) return;
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        int shader = s_tex_enabled ? V3D_SHADER_TEXTURE_MODULATE :
                     (s_lighting_enabled ? V3D_SHADER_LIGHTING : V3D_SHADER_BASIC_COLOR);
        v3d_emit_primitive_batch(V3D_PRIM_TRIANGLES, shader, s_batch_verts, s_batch_count);
    }
#endif
    s_batch_count = 0;
}

static void v3d_append_vert(const v3d_in_vert_t *v) {
    if (s_batch_count >= V3D_BATCH_MAX_VERTS - 4) {
        v3d_flush_batch();
    }

    const v3d_mat4_t *mv = &s_mv_stack[s_mv_top];
    const v3d_mat4_t *proj = &s_proj_stack[s_proj_top];

    /* 1. Transform to Eye Space */
    float ex, ey, ez, ew;
    v3d_mat4_transform_vec4(mv, v->x, v->y, v->z, &ex, &ey, &ez, &ew);

    /* 2. Directional Lighting */
    float lr = v->r;
    float lg = v->g;
    float lb = v->b;
    float la = v->a;

    if (s_lighting_enabled && s_light0_enabled) {
        float tnx, tny, tnz;
        v3d_mat4_transform_normal(mv, v->nx, v->ny, v->nz, &tnx, &tny, &tnz);

        float lx = s_light0_pos[0];
        float ly = s_light0_pos[1];
        float lz = s_light0_pos[2];
        float llen = (float)sqrt(lx * lx + ly * ly + lz * lz);
        if (llen > 0.0001f) {
            lx /= llen; ly /= llen; lz /= llen;
        } else {
            lz = 1.0f;
        }

        float n_dot_l = tnx * lx + tny * ly + tnz * lz;
        if (n_dot_l < 0.0f) n_dot_l = 0.0f;

        float mat_r = s_color_material_enabled ? v->r : s_mat_diffuse[0];
        float mat_g = s_color_material_enabled ? v->g : s_mat_diffuse[1];
        float mat_b = s_color_material_enabled ? v->b : s_mat_diffuse[2];

        float amb_r = (s_color_material_enabled ? v->r : s_mat_ambient[0]) * s_light0_ambient[0];
        float amb_g = (s_color_material_enabled ? v->g : s_mat_ambient[1]) * s_light0_ambient[1];
        float amb_b = (s_color_material_enabled ? v->b : s_mat_ambient[2]) * s_light0_ambient[2];

        lr = amb_r + mat_r * s_light0_diffuse[0] * n_dot_l;
        lg = amb_g + mat_g * s_light0_diffuse[1] * n_dot_l;
        lb = amb_b + mat_b * s_light0_diffuse[2] * n_dot_l;

        if (lr > 1.0f) lr = 1.0f;
        if (lg > 1.0f) lg = 1.0f;
        if (lb > 1.0f) lb = 1.0f;
    }

    /* 3. Transform to Clip Space */
    float cx, cy, cz, cw;
    v3d_mat4_transform_vec4(proj, ex, ey, ez, &cx, &cy, &cz, &cw);

    /* 4. Pack into V3D Vertex Record */
    v3d_vertex_t *out = &s_batch_verts[s_batch_count++];
    out->x = cx;
    out->y = cy;
    out->z = cz;
    out->w = cw;
    out->r = lr;
    out->g = lg;
    out->b = lb;
    out->a = la;
    out->u = v->u;
    out->v = v->v;
    out->nx = v->nx;
    out->ny = v->ny;
    out->nz = v->nz;
    out->pad = 0.0f;
}

static void v3d_emit_tri(const v3d_in_vert_t *v0, const v3d_in_vert_t *v1, const v3d_in_vert_t *v2) {
    v3d_append_vert(v0);
    v3d_append_vert(v1);
    v3d_append_vert(v2);
}

static void v3d_prim_vertex(const v3d_in_vert_t *v) {
    switch (s_prim_mode) {
    case GL_TRIANGLES:
        s_win[s_win_n++] = *v;
        if (s_win_n == 3) {
            v3d_emit_tri(&s_win[0], &s_win[1], &s_win[2]);
            s_win_n = 0;
        }
        break;
    case GL_QUADS:
        s_win[s_win_n++] = *v;
        if (s_win_n == 4) {
            v3d_emit_tri(&s_win[0], &s_win[1], &s_win[2]);
            v3d_emit_tri(&s_win[0], &s_win[2], &s_win[3]);
            s_win_n = 0;
        }
        break;
    case GL_QUAD_STRIP:
        s_win[s_win_n++] = *v;
        if (s_win_n == 4) {
            v3d_emit_tri(&s_win[0], &s_win[1], &s_win[3]);
            v3d_emit_tri(&s_win[0], &s_win[3], &s_win[2]);
            s_win[0] = s_win[2];
            s_win[1] = s_win[3];
            s_win_n = 2;
        }
        break;
    case GL_TRIANGLE_STRIP:
        s_win[s_win_n++] = *v;
        if (s_win_n == 3) {
            if ((s_win_i - 2) & 1) v3d_emit_tri(&s_win[1], &s_win[0], &s_win[2]);
            else                   v3d_emit_tri(&s_win[0], &s_win[1], &s_win[2]);
            s_win[0] = s_win[1];
            s_win[1] = s_win[2];
            s_win_n = 2;
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (s_win_i == 0) {
            s_fan_base = *v;
        } else if (s_win_i >= 2) {
            v3d_emit_tri(&s_fan_base, &s_win[0], v);
        }
        s_win[0] = *v;
        s_win_n = 1;
        break;
    default:
        break;
    }
    s_win_i++;
}

/* ── gl_ops_t Implementations ─────────────────────────────────────── */

static void v3d_gl_begin(GLenum mode) {
    s_prim_mode = mode;
    s_win_n = 0;
    s_win_i = 0;
    s_in_begin = 1;
    if (g_tinygl_ops.gl_begin) {
        g_tinygl_ops.gl_begin(mode);
    }
}

static void v3d_gl_end(void) {
    if (s_in_begin) {
        v3d_flush_batch();
        s_win_n = 0;
        s_win_i = 0;
        s_in_begin = 0;
    }
    if (g_tinygl_ops.gl_end) {
        g_tinygl_ops.gl_end();
    }
}

static void v3d_gl_vertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (s_in_begin) {
        v3d_in_vert_t v;
        v.x = x; v.y = y; v.z = z;
        v.nx = s_cur_nx; v.ny = s_cur_ny; v.nz = s_cur_nz;
        v.r = s_cur_r; v.g = s_cur_g; v.b = s_cur_b;
        v.a = s_cur_a;
        v.u = s_cur_u; v.v = s_cur_v;
        v3d_prim_vertex(&v);
    }
    if (g_tinygl_ops.gl_vertex3f) {
        g_tinygl_ops.gl_vertex3f(x, y, z);
    }
}

static void v3d_gl_vertex2f(GLfloat x, GLfloat y) {
    v3d_gl_vertex3f(x, y, 0.0f);
}

static void v3d_gl_normal3f(GLfloat x, GLfloat y, GLfloat z) {
    s_cur_nx = x; s_cur_ny = y; s_cur_nz = z;
    if (g_tinygl_ops.gl_normal3f) {
        g_tinygl_ops.gl_normal3f(x, y, z);
    }
}

static void v3d_gl_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    s_cur_r = r; s_cur_g = g; s_cur_b = b; s_cur_a = a;
    if (g_tinygl_ops.gl_color4f) {
        g_tinygl_ops.gl_color4f(r, g, b, a);
    }
}

static void v3d_gl_color3f(GLfloat r, GLfloat g, GLfloat b) {
    v3d_gl_color4f(r, g, b, 1.0f);
}

static void v3d_gl_color3fv(const GLfloat *v) {
    if (v) v3d_gl_color4f(v[0], v[1], v[2], 1.0f);
}

static void v3d_gl_tex_coord2f(GLfloat s, GLfloat t) {
    s_cur_u = s; s_cur_v = t;
    if (g_tinygl_ops.gl_tex_coord2f) {
        g_tinygl_ops.gl_tex_coord2f(s, t);
    }
}

static void v3d_gl_tex_coord2fv(const GLfloat *v) {
    if (v) v3d_gl_tex_coord2f(v[0], v[1]);
}

/* ── Matrix Operations ────────────────────────────────────────────── */

static void v3d_gl_matrix_mode(GLenum mode) {
    s_matrix_mode = mode;
    if (g_tinygl_ops.gl_matrix_mode) {
        g_tinygl_ops.gl_matrix_mode(mode);
    }
}

static void v3d_gl_load_identity(void) {
    if (s_matrix_mode == GL_PROJECTION) {
        v3d_mat4_identity(&s_proj_stack[s_proj_top]);
    } else {
        v3d_mat4_identity(&s_mv_stack[s_mv_top]);
    }
    if (g_tinygl_ops.gl_load_identity) {
        g_tinygl_ops.gl_load_identity();
    }
}

static void v3d_gl_push_matrix(void) {
    if (s_matrix_mode == GL_PROJECTION) {
        if (s_proj_top < V3D_MATRIX_STACK_MAX - 1) {
            s_proj_stack[s_proj_top + 1] = s_proj_stack[s_proj_top];
            s_proj_top++;
        }
    } else {
        if (s_mv_top < V3D_MATRIX_STACK_MAX - 1) {
            s_mv_stack[s_mv_top + 1] = s_mv_stack[s_mv_top];
            s_mv_top++;
        }
    }
    if (g_tinygl_ops.gl_push_matrix) {
        g_tinygl_ops.gl_push_matrix();
    }
}

static void v3d_gl_pop_matrix(void) {
    if (s_matrix_mode == GL_PROJECTION) {
        if (s_proj_top > 0) s_proj_top--;
    } else {
        if (s_mv_top > 0) s_mv_top--;
    }
    if (g_tinygl_ops.gl_pop_matrix) {
        g_tinygl_ops.gl_pop_matrix();
    }
}

static void v3d_gl_rotate_f(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    if (s_matrix_mode == GL_PROJECTION) {
        v3d_mat4_rotate(&s_proj_stack[s_proj_top], angle, x, y, z);
    } else {
        v3d_mat4_rotate(&s_mv_stack[s_mv_top], angle, x, y, z);
    }
    if (g_tinygl_ops.gl_rotate_f) {
        g_tinygl_ops.gl_rotate_f(angle, x, y, z);
    }
}

static void v3d_gl_translate_f(GLfloat x, GLfloat y, GLfloat z) {
    if (s_matrix_mode == GL_PROJECTION) {
        v3d_mat4_translate(&s_proj_stack[s_proj_top], x, y, z);
    } else {
        v3d_mat4_translate(&s_mv_stack[s_mv_top], x, y, z);
    }
    if (g_tinygl_ops.gl_translate_f) {
        g_tinygl_ops.gl_translate_f(x, y, z);
    }
}

static void v3d_gl_scale_f(GLfloat x, GLfloat y, GLfloat z) {
    if (s_matrix_mode == GL_PROJECTION) {
        v3d_mat4_scale(&s_proj_stack[s_proj_top], x, y, z);
    } else {
        v3d_mat4_scale(&s_mv_stack[s_mv_top], x, y, z);
    }
    if (g_tinygl_ops.gl_scale_f) {
        g_tinygl_ops.gl_scale_f(x, y, z);
    }
}

static void v3d_gl_frustum(double l, double r, double b, double t, double n, double f) {
    if (s_matrix_mode == GL_PROJECTION) {
        v3d_mat4_frustum(&s_proj_stack[s_proj_top], l, r, b, t, n, f);
    } else {
        v3d_mat4_frustum(&s_mv_stack[s_mv_top], l, r, b, t, n, f);
    }
    if (g_tinygl_ops.gl_frustum) {
        g_tinygl_ops.gl_frustum(l, r, b, t, n, f);
    }
}

static void v3d_gl_ortho(double l, double r, double b, double t, double n, double f) {
    if (s_matrix_mode == GL_PROJECTION) {
        v3d_mat4_ortho(&s_proj_stack[s_proj_top], l, r, b, t, n, f);
    } else {
        v3d_mat4_ortho(&s_mv_stack[s_mv_top], l, r, b, t, n, f);
    }
    if (g_tinygl_ops.gl_ortho) {
        g_tinygl_ops.gl_ortho(l, r, b, t, n, f);
    }
}

/* ── State & Pipeline Operations ──────────────────────────────────── */

static void v3d_gl_enable(GLenum cap) {
    switch (cap) {
    case GL_LIGHTING:           s_lighting_enabled = 1; break;
    case GL_LIGHT0:             s_light0_enabled = 1; break;
    case GL_COLOR_MATERIAL:     s_color_material_enabled = 1; break;
    case GL_DEPTH_TEST:         s_depth_test_enabled = 1; break;
    case GL_CULL_FACE:          s_cull_face_enabled = 1; break;
    case GL_BLEND:              s_blend_enabled = 1; break;
    case GL_TEXTURE_2D:         s_tex_enabled = 1; break;
    default: break;
    }
    if (g_tinygl_ops.gl_enable) {
        g_tinygl_ops.gl_enable(cap);
    }
}

static void v3d_gl_disable(GLenum cap) {
    switch (cap) {
    case GL_LIGHTING:           s_lighting_enabled = 0; break;
    case GL_LIGHT0:             s_light0_enabled = 0; break;
    case GL_COLOR_MATERIAL:     s_color_material_enabled = 0; break;
    case GL_DEPTH_TEST:         s_depth_test_enabled = 0; break;
    case GL_CULL_FACE:          s_cull_face_enabled = 0; break;
    case GL_BLEND:              s_blend_enabled = 0; break;
    case GL_TEXTURE_2D:         s_tex_enabled = 0; break;
    default: break;
    }
    if (g_tinygl_ops.gl_disable) {
        g_tinygl_ops.gl_disable(cap);
    }
}

static void v3d_gl_shade_model(GLenum mode) {
    if (g_tinygl_ops.gl_shade_model) {
        g_tinygl_ops.gl_shade_model(mode);
    }
}

static void v3d_gl_blend_func(GLenum src, GLenum dst) {
    s_blend_src = src;
    s_blend_dst = dst;
    if (g_tinygl_ops.gl_blend_func) {
        g_tinygl_ops.gl_blend_func(src, dst);
    }
}

static void v3d_gl_light_fv(GLenum light, GLenum pname, const GLfloat *params) {
    if (light == GL_LIGHT0 && params) {
        if (pname == GL_POSITION) {
            memcpy(s_light0_pos, params, 4 * sizeof(float));
        } else if (pname == GL_DIFFUSE) {
            memcpy(s_light0_diffuse, params, 4 * sizeof(float));
        } else if (pname == GL_AMBIENT) {
            memcpy(s_light0_ambient, params, 4 * sizeof(float));
        }
    }
    if (g_tinygl_ops.gl_light_fv) {
        g_tinygl_ops.gl_light_fv(light, pname, params);
    }
}

static void v3d_gl_material_fv(GLenum face, GLenum pname, const GLfloat *params) {
    if (params) {
        if (pname == GL_DIFFUSE) {
            memcpy(s_mat_diffuse, params, 4 * sizeof(float));
        } else if (pname == GL_AMBIENT) {
            memcpy(s_mat_ambient, params, 4 * sizeof(float));
        }
    }
    if (g_tinygl_ops.gl_material_fv) {
        g_tinygl_ops.gl_material_fv(face, pname, params);
    }
}

static void v3d_gl_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    uint8_t ur = (uint8_t)(r * 255.0f);
    uint8_t ug = (uint8_t)(g * 255.0f);
    uint8_t ub = (uint8_t)(b * 255.0f);
    uint8_t ua = (uint8_t)(a * 255.0f);
    s_v3d_clear_col = ((uint32_t)ua << 24) | ((uint32_t)ur << 16) |
                      ((uint32_t)ug << 8)  | (uint32_t)ub;
    if (g_tinygl_ops.gl_clear_color) {
        g_tinygl_ops.gl_clear_color(r, g, b, a);
    }
}

static void v3d_gl_clear(GLbitfield mask) {
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available() && (mask & GL_COLOR_BUFFER_BIT)) {
        v3d_clear(s_v3d_clear_col, (uint32_t)s_v3d_w, (uint32_t)s_v3d_h);
    }
#endif
    if (g_tinygl_ops.gl_clear) {
        g_tinygl_ops.gl_clear(mask);
    }
}

static void v3d_gl_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    s_v3d_w = w;
    s_v3d_h = h;
    s_vp_x = x;
    s_vp_y = y;
    s_vp_w = w;
    s_vp_h = h;
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_set_viewport((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
    }
#endif
    if (g_tinygl_ops.gl_viewport) {
        g_tinygl_ops.gl_viewport(x, y, w, h);
    }
}

static GLuint s_cur_tex_id = 0;

static void v3d_gl_bind_texture(GLenum target, GLuint texture) {
    s_cur_tex_id = texture;
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_upload_texture(texture, NULL, 0, 0);
    }
#endif
    if (g_tinygl_ops.gl_bind_texture) {
        g_tinygl_ops.gl_bind_texture(target, texture);
    }
}

static void v3d_gl_tex_image_2d(GLenum target, GLint level, GLint components,
                                GLsizei width, GLsizei height, GLint border,
                                GLenum format, GLenum type, const void *pixels) {
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_upload_texture(s_cur_tex_id, pixels, (uint32_t)width, (uint32_t)height);
    }
#endif
    if (g_tinygl_ops.gl_tex_image_2d) {
        g_tinygl_ops.gl_tex_image_2d(target, level, components, width, height,
                                    border, format, type, pixels);
    }
}

static void v3d_gl_swap_buffers(void) {
    if (s_in_begin) {
        v3d_flush_batch();
    }
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_flush();
    }
#endif
    if (g_tinygl_ops.swap_buffers) {
        g_tinygl_ops.swap_buffers();
    }
}

void v3d_backend_init(int width, int height, void *pixel_buf) {
    s_v3d_w = width;
    s_v3d_h = height;
    s_v3d_pixel_buf = pixel_buf;
    s_vp_x = 0;
    s_vp_y = 0;
    s_vp_w = width;
    s_vp_h = height;

    /* Initialize matrix stacks with identity */
    s_mv_top = 0;
    v3d_mat4_identity(&s_mv_stack[0]);
    s_proj_top = 0;
    v3d_mat4_identity(&s_proj_stack[0]);
    s_matrix_mode = GL_MODELVIEW;

    s_batch_count = 0;
    s_win_n = 0;
    s_win_i = 0;
    s_in_begin = 0;

    tinygl_backend_init(width, height, pixel_buf);

    /* Inherit base dispatch table from TinyGL */
    g_v3d_ops = g_tinygl_ops;

    /* Override with hardware accelerated operations */
    g_v3d_ops.gl_begin        = v3d_gl_begin;
    g_v3d_ops.gl_end          = v3d_gl_end;
    g_v3d_ops.gl_vertex3f     = v3d_gl_vertex3f;
    g_v3d_ops.gl_vertex2f     = v3d_gl_vertex2f;
    g_v3d_ops.gl_normal3f     = v3d_gl_normal3f;
    g_v3d_ops.gl_color4f      = v3d_gl_color4f;
    g_v3d_ops.gl_color3f      = v3d_gl_color3f;
    g_v3d_ops.gl_color3fv     = v3d_gl_color3fv;
    g_v3d_ops.gl_tex_coord2f  = v3d_gl_tex_coord2f;
    g_v3d_ops.gl_tex_coord2fv = v3d_gl_tex_coord2fv;

    g_v3d_ops.gl_matrix_mode  = v3d_gl_matrix_mode;
    g_v3d_ops.gl_load_identity= v3d_gl_load_identity;
    g_v3d_ops.gl_push_matrix  = v3d_gl_push_matrix;
    g_v3d_ops.gl_pop_matrix   = v3d_gl_pop_matrix;
    g_v3d_ops.gl_rotate_f     = v3d_gl_rotate_f;
    g_v3d_ops.gl_translate_f  = v3d_gl_translate_f;
    g_v3d_ops.gl_scale_f      = v3d_gl_scale_f;
    g_v3d_ops.gl_frustum      = v3d_gl_frustum;
    g_v3d_ops.gl_ortho        = v3d_gl_ortho;

    g_v3d_ops.gl_enable       = v3d_gl_enable;
    g_v3d_ops.gl_disable      = v3d_gl_disable;
    g_v3d_ops.gl_shade_model  = v3d_gl_shade_model;
    g_v3d_ops.gl_blend_func   = v3d_gl_blend_func;
    g_v3d_ops.gl_light_fv     = v3d_gl_light_fv;
    g_v3d_ops.gl_material_fv  = v3d_gl_material_fv;

    g_v3d_ops.gl_clear_color  = v3d_gl_clear_color;
    g_v3d_ops.gl_clear        = v3d_gl_clear;
    g_v3d_ops.gl_viewport     = v3d_gl_viewport;
    g_v3d_ops.gl_bind_texture = v3d_gl_bind_texture;
    g_v3d_ops.gl_tex_image_2d = v3d_gl_tex_image_2d;
    g_v3d_ops.swap_buffers    = v3d_gl_swap_buffers;

#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_set_viewport(0, 0, (uint32_t)width, (uint32_t)height);
        v3d_set_render_target(pixel_buf);
    }
#endif

    uart_puts_raw("[GL] v3d: VideoCore VI hardware acceleration pipeline ready\n");
}

void v3d_backend_resize(int width, int height, void *pixel_buf) {
    s_v3d_w = width;
    s_v3d_h = height;
    s_v3d_pixel_buf = pixel_buf;
    s_vp_w = width;
    s_vp_h = height;
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_set_viewport((uint32_t)s_vp_x, (uint32_t)s_vp_y, (uint32_t)width, (uint32_t)height);
        v3d_set_render_target(pixel_buf);
    }
#endif
    tinygl_backend_resize(width, height, pixel_buf);
}

void v3d_backend_make_current(int width, int height, void *pixel_buf) {
    s_v3d_w = width;
    s_v3d_h = height;
    s_v3d_pixel_buf = pixel_buf;
    s_vp_x = 0;
    s_vp_y = 0;
    s_vp_w = width;
    s_vp_h = height;
#if defined(_RPI_BCM283x_) || defined(BTRON_YOKOYAMA_AARCH64)
    if (v3d_is_available()) {
        v3d_set_viewport(0, 0, (uint32_t)width, (uint32_t)height);
        v3d_set_render_target(pixel_buf);
    }
#endif
}
