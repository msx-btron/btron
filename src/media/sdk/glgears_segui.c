/*
 * glgears_segui.c — Real OpenGL ES 1.1 (TinyGL) GLGears Renderer for SegUI
 *
 * Uses the authentic 3D extruded involute gear geometry, GL_LIGHT0 smooth
 * shading, GL_DEPTH_TEST, and perspective projection from src/media/sdk/glgears.c,
 * rendering directly via TinyGL into a SegUI GDEV canvas RECT.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <btron/dp.h>
#include <graphics/gl.h>
#if !defined(PICO_REAL_HW)
#include "zbuffer.h"
#endif
#if defined(PICO_REAL_HW)
#include "picogl.h"
#define glBegin            picogl_gl_begin
#define glEnd              picogl_gl_end
#define glVertex3f         picogl_gl_vertex3f
#define glNormal3f         picogl_gl_normal3f
#define glViewport         picogl_gl_viewport
#define glEnable           picogl_gl_enable
#define glShadeModel       picogl_gl_shade_model
#define glLightfv          picogl_gl_light_fv
#define glMatrixMode       picogl_gl_matrix_mode
#define glLoadIdentity     picogl_gl_load_identity
#define glFrustum          picogl_gl_frustum
#define glTranslatef       picogl_gl_translate_f
#define glRotatef          picogl_gl_rotate_f
#define glClearColor       picogl_gl_clear_color
#define glClear            picogl_gl_clear
#define glPushMatrix       picogl_gl_push_matrix
#define glPopMatrix        picogl_gl_pop_matrix
#define glMaterialfv       picogl_gl_material_fv
extern void dp_pico_blit_rgb565(const RECT *dst, const uint16_t *src, int src_w, int src_h);
#endif

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

typedef struct {
    GLfloat r0, r1, r2, half_w;
    GLint   teeth;
    GLfloat c0[21], s0[21]; /* angle a          */
    GLfloat c1[20], s1[20]; /* angle a + 1*da   */
    GLfloat c2[20], s2[20]; /* angle a + 2*da   */
    GLfloat c3[21], s3[21]; /* angle a + 3*da   */
    GLfloat n1x[20], n1y[20]; /* tooth flank 1 normal */
    GLfloat n2x[20], n2y[20]; /* tooth flank 2 normal */
    int ir0, ir1, ir2, ihw;
    int16_t ic0[21], is0[21], ic1[20], is1[20];
    int16_t ic2[20], is2[20], ic3[21], is3[21];
} PRECOMP_GEAR;

static void precomp_gear_init(PRECOMP_GEAR *g, GLfloat inner_radius,
                              GLfloat outer_radius, GLfloat width,
                              GLint teeth, GLfloat tooth_depth)
{
    GLint i;
    GLfloat da = 2.0f * (GLfloat)M_PI / (GLfloat)teeth / 4.0f;
    g->r0     = inner_radius;
    g->r1     = outer_radius - tooth_depth * 0.5f;
    g->r2     = outer_radius + tooth_depth * 0.5f;
    g->half_w = width * 0.5f;
    g->teeth  = teeth;

    for (i = 0; i <= teeth; i++) {
        GLfloat a = (GLfloat)i * 2.0f * (GLfloat)M_PI / (GLfloat)teeth;
        g->c0[i] = cosf(a);
        g->s0[i] = sinf(a);
        g->c3[i] = cosf(a + 3.0f * da);
        g->s3[i] = sinf(a + 3.0f * da);
        if (i < teeth) {
            GLfloat u, v, len;
            g->c1[i] = cosf(a + da);
            g->s1[i] = sinf(a + da);
            g->c2[i] = cosf(a + 2.0f * da);
            g->s2[i] = sinf(a + 2.0f * da);

            u = g->r2 * g->c1[i] - g->r1 * g->c0[i];
            v = g->r2 * g->s1[i] - g->r1 * g->s0[i];
            len = sqrtf(u * u + v * v);
            if (len > 1e-6f) { u /= len; v /= len; }
            g->n1x[i] = v; g->n1y[i] = -u;

            u = g->r1 * g->c3[i] - g->r2 * g->c2[i];
            v = g->r1 * g->s3[i] - g->r2 * g->s2[i];
            len = sqrtf(u * u + v * v);
            if (len > 1e-6f) { u /= len; v /= len; }
            g->n2x[i] = v; g->n2y[i] = -u;
        }
    }
    g->ir0 = (int)(g->r0 * 256.0f);
    g->ir1 = (int)(g->r1 * 256.0f);
    g->ir2 = (int)(g->r2 * 256.0f);
    g->ihw = (int)(g->half_w * 256.0f);
    for (i = 0; i <= teeth; i++) {
        g->ic0[i] = (int16_t)(g->c0[i] * 16384.0f);
        g->is0[i] = (int16_t)(g->s0[i] * 16384.0f);
        g->ic3[i] = (int16_t)(g->c3[i] * 16384.0f);
        g->is3[i] = (int16_t)(g->s3[i] * 16384.0f);
        if (i < teeth) {
            g->ic1[i] = (int16_t)(g->c1[i] * 16384.0f);
            g->is1[i] = (int16_t)(g->s1[i] * 16384.0f);
            g->ic2[i] = (int16_t)(g->c2[i] * 16384.0f);
            g->is2[i] = (int16_t)(g->s2[i] * 16384.0f);
        }
    }
}

#if !defined(PICO_REAL_HW)
static void draw_precomp_gear(const PRECOMP_GEAR *g)
{
    GLint i;
    GLfloat r0 = g->r0, r1 = g->r1, r2 = g->r2, hw = g->half_w;
    GLint teeth = g->teeth;

    glNormal3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        glVertex3f(r0 * g->c0[i], r0 * g->s0[i], hw);
        glVertex3f(r1 * g->c0[i], r1 * g->s0[i], hw);
        glVertex3f(r0 * g->c0[i], r0 * g->s0[i], hw);
        glVertex3f(r1 * g->c3[i], r1 * g->s3[i], hw);
    }
    glEnd();

    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        glVertex3f(r1 * g->c0[i], r1 * g->s0[i], hw);
        glVertex3f(r2 * g->c1[i], r2 * g->s1[i], hw);
        glVertex3f(r2 * g->c2[i], r2 * g->s2[i], hw);
        glVertex3f(r1 * g->c3[i], r1 * g->s3[i], hw);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        glVertex3f(r1 * g->c0[i], r1 * g->s0[i], -hw);
        glVertex3f(r0 * g->c0[i], r0 * g->s0[i], -hw);
        glVertex3f(r1 * g->c3[i], r1 * g->s3[i], -hw);
        glVertex3f(r0 * g->c0[i], r0 * g->s0[i], -hw);
    }
    glEnd();

    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        glVertex3f(r1 * g->c3[i], r1 * g->s3[i], -hw);
        glVertex3f(r2 * g->c2[i], r2 * g->s2[i], -hw);
        glVertex3f(r2 * g->c1[i], r2 * g->s1[i], -hw);
        glVertex3f(r1 * g->c0[i], r1 * g->s0[i], -hw);
    }
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        glVertex3f(r1 * g->c0[i], r1 * g->s0[i],  hw);
        glVertex3f(r1 * g->c0[i], r1 * g->s0[i], -hw);
        glNormal3f(g->n1x[i], g->n1y[i], 0.0f);
        glVertex3f(r2 * g->c1[i], r2 * g->s1[i],  hw);
        glVertex3f(r2 * g->c1[i], r2 * g->s1[i], -hw);
        glNormal3f(g->c0[i], g->s0[i], 0.0f);
        glVertex3f(r2 * g->c2[i], r2 * g->s2[i],  hw);
        glVertex3f(r2 * g->c2[i], r2 * g->s2[i], -hw);
        glNormal3f(g->n2x[i], g->n2y[i], 0.0f);
        glVertex3f(r1 * g->c3[i], r1 * g->s3[i],  hw);
        glVertex3f(r1 * g->c3[i], r1 * g->s3[i], -hw);
        glNormal3f(g->c0[i], g->s0[i], 0.0f);
    }
    glVertex3f(r1, 0.0f,  hw);
    glVertex3f(r1, 0.0f, -hw);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        glNormal3f(-g->c0[i], -g->s0[i], 0.0f);
        glVertex3f(r0 * g->c0[i], r0 * g->s0[i], -hw);
        glVertex3f(r0 * g->c0[i], r0 * g->s0[i],  hw);
    }
    glEnd();
}

static void build_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                       GLint teeth, GLfloat tooth_depth)
{
    PRECOMP_GEAR g;
    precomp_gear_init(&g, inner_radius, outer_radius, width, teeth, tooth_depth);
    draw_precomp_gear(&g);
}
#endif /* !PICO_REAL_HW */

#if defined(PICO_REAL_HW)
/* Q14 sine, one degree. The device frame rotates with this table and posts
 * screen triangles. It does not call sinf, the float matrix, or per-vertex
 * lighting. */
static const int16_t s_sin90[91] = {
    0,286,572,857,1143,1428,1713,1997,2280,2563,2845,3126,3406,3686,3964,
    4240,4516,4790,5063,5334,5604,5872,6138,6402,6664,6924,7182,7438,7692,
    7943,8192,8438,8682,8923,9162,9397,9630,9860,10087,10311,10531,10749,
    10963,11174,11381,11585,11786,11982,12176,12365,12551,12733,12911,13085,
    13255,13421,13583,13741,13894,14044,14189,14330,14466,14598,14726,14849,
    14968,15082,15191,15296,15396,15491,15582,15668,15749,15826,15897,15964,
    16026,16083,16135,16182,16225,16262,16294,16322,16344,16362,16374,16382,
    16384
};

static int isin_deg(int deg)
{
    int a = deg % 360;
    if (a < 0) a += 360;
    if (a <= 90) return s_sin90[a];
    if (a <= 180) return s_sin90[180 - a];
    if (a <= 270) return -s_sin90[a - 180];
    return -s_sin90[360 - a];
}

static int icos_deg(int deg)
{
    return isin_deg(deg + 90);
}

typedef struct {
    int cs, ss, cy, syaw, cx, sp;
    int tx, ty;
    int half_w, half_h, scale;
} gear_xf_t;

/* Spin, yaw, pitch, and the viewport scale are constant for one gear. */
static void gear_xf_make(gear_xf_t *xf, int spin, int yaw, int pitch,
                         int tx, int ty, int vw, int vh)
{
    xf->cs = icos_deg(spin);
    xf->ss = isin_deg(spin);
    xf->cy = icos_deg(yaw);
    xf->syaw = isin_deg(yaw);
    xf->cx = icos_deg(pitch);
    xf->sp = isin_deg(pitch);
    xf->tx = tx;
    xf->ty = ty;
    xf->half_w = vw / 2;
    xf->half_h = vh / 2;
    xf->scale = (5 * vh) / 2;
}

static void gear_project(const gear_xf_t *xf, int x, int y, int z,
                         int *sx, int *sy, int *iz)
{
    int x1 = (int)(((int)x * xf->cs - (int)y * xf->ss) >> 14);
    int y1 = (int)(((int)x * xf->ss + (int)y * xf->cs) >> 14);
    int z1 = z;
    int x2, z2, y3, z3, denom;
    x1 += xf->tx;
    y1 += xf->ty;
    x2 = (int)(((int)x1 * xf->cy + (int)z1 * xf->syaw) >> 14);
    z2 = (int)(((int)z1 * xf->cy - (int)x1 * xf->syaw) >> 14);
    y3 = (int)(((int)y1 * xf->cx - (int)z2 * xf->sp) >> 14);
    z3 = (int)(((int)y1 * xf->sp + (int)z2 * xf->cx) >> 14) - 40 * 256;
    denom = -z3;
    if (denom < 64) denom = 64;
    *sx = xf->half_w + (x2 * xf->scale) / denom;
    *sy = xf->half_h - (y3 * xf->scale) / denom;
    *iz = 30000 - (denom >> 4);
    if (*iz < 1) *iz = 1;
}

/* Outward normal after the same RotX*RotY*RotZ the vertices use.
 * A face pointing away from the camera is the one GL_CULL_FACE drops.
 * nz > 0 is the front disk; walls and the back use the side color. */
static int gear_face(const gear_xf_t *xf,
                     int x0, int y0, int z0, int x1, int y1, int z1,
                     int x2, int y2, int z2,
                     uint16_t front, uint16_t side, uint16_t *rgb)
{
    int ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
    int vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
    int64_t nx = (int64_t)uy * vz - (int64_t)uz * vy;
    int64_t ny = (int64_t)uz * vx - (int64_t)ux * vz;
    int64_t nz = (int64_t)ux * vy - (int64_t)uy * vx;
    int64_t xr = (nx * xf->cs - ny * xf->ss) >> 14;
    int64_t yr = (nx * xf->ss + ny * xf->cs) >> 14;
    int64_t zr = nz;
    int64_t zr2 = (zr * xf->cy - xr * xf->syaw) >> 14;
    int64_t z3 = (yr * xf->sp + zr2 * xf->cx) >> 14;
    if (z3 <= 0) return 0;
    *rgb = (nz > 0) ? front : side;
    return 1;
}

static void gear_emit(const gear_xf_t *xf,
                      int x0, int y0, int z0, int x1, int y1, int z1,
                      int x2, int y2, int z2, int x3, int y3, int z3,
                      int n, uint16_t front, uint16_t side)
{
    int s0x, s0y, i0, s1x, s1y, i1, s2x, s2y, i2, s3x, s3y, i3, iz;
    uint16_t rgb;
    if (!gear_face(xf, x0, y0, z0, x1, y1, z1, x2, y2, z2, front, side, &rgb))
        return;
    gear_project(xf, x0, y0, z0, &s0x, &s0y, &i0);
    gear_project(xf, x1, y1, z1, &s1x, &s1y, &i1);
    gear_project(xf, x2, y2, z2, &s2x, &s2y, &i2);
    iz = i0;
    if (i1 > iz) iz = i1;
    if (i2 > iz) iz = i2;
    picogl_screen_tri(s0x, s0y, s1x, s1y, s2x, s2y, iz, rgb);
    if (n < 4) return;
    gear_project(xf, x3, y3, z3, &s3x, &s3y, &i3);
    iz = i0;
    if (i2 > iz) iz = i2;
    if (i3 > iz) iz = i3;
    picogl_screen_tri(s0x, s0y, s2x, s2y, s3x, s3y, iz, rgb);
}

static void gear_xy(int radius, int ic, int isin, int *ox, int *oy)
{
    *ox = (radius * ic) >> 14;
    *oy = (radius * isin) >> 14;
}

static void gear_xyk(const PRECOMP_GEAR *g, int kind, int i, int *ox, int *oy)
{
    int rad, ic, isn;
    if (kind == 0)      { rad = g->ir0; ic = g->ic0[i]; isn = g->is0[i]; }
    else if (kind == 1) { rad = g->ir1; ic = g->ic0[i]; isn = g->is0[i]; }
    else if (kind == 2) { rad = g->ir2; ic = g->ic1[i]; isn = g->is1[i]; }
    else if (kind == 3) { rad = g->ir2; ic = g->ic2[i]; isn = g->is2[i]; }
    else                { rad = g->ir1; ic = g->ic3[i]; isn = g->is3[i]; }
    gear_xy(rad, ic, isn, ox, oy);
}

/* The same faces as glgears.c gear(): front disk, tooth tops, back disk,
 * four walls around each tooth, and the inner cylinder. Hidden faces are
 * not posted. */
static void gear_one_tooth(const PRECOMP_GEAR *g, const gear_xf_t *xf, int i,
                           uint16_t front, uint16_t side)
{
    int ax, ay, bx, by, cx, cy, dx, dy, ex, ey, fx, fy, gx, gy;
    int i1 = i + 1;
    int hw = g->ihw;
    gear_xyk(g, 1, i, &ax, &ay);
    gear_xyk(g, 2, i, &bx, &by);
    gear_xyk(g, 3, i, &cx, &cy);
    gear_xyk(g, 4, i, &dx, &dy);
    gear_xyk(g, 0, i, &ex, &ey);
    gear_xyk(g, 0, i1, &fx, &fy);
    gear_xyk(g, 1, i1, &gx, &gy);
    gear_emit(xf, ex, ey, hw, ax, ay, hw, dx, dy, hw, 0, 0, 0, 3, front, side);
    gear_emit(xf, ex, ey, hw, dx, dy, hw, gx, gy, hw, fx, fy, hw, 4, front, side);
    gear_emit(xf, ax, ay, hw, bx, by, hw, cx, cy, hw, dx, dy, hw, 4, front, side);
    gear_emit(xf, ax, ay, -hw, ex, ey, -hw, dx, dy, -hw, 0, 0, 0, 3, front, side);
    gear_emit(xf, dx, dy, -hw, ex, ey, -hw, fx, fy, -hw, gx, gy, -hw, 4, front, side);
    gear_emit(xf, dx, dy, -hw, cx, cy, -hw, bx, by, -hw, ax, ay, -hw, 4, front, side);
    gear_emit(xf, ax, ay, hw, ax, ay, -hw, bx, by, -hw, bx, by, hw, 4, front, side);
    gear_emit(xf, bx, by, hw, bx, by, -hw, cx, cy, -hw, cx, cy, hw, 4, front, side);
    gear_emit(xf, cx, cy, hw, cx, cy, -hw, dx, dy, -hw, dx, dy, hw, 4, front, side);
    gear_emit(xf, dx, dy, hw, dx, dy, -hw, gx, gy, -hw, gx, gy, hw, 4, front, side);
    gear_emit(xf, ex, ey, -hw, ex, ey, hw, fx, fy, hw, fx, fy, -hw, 4, front, side);
}

static void draw_gear_int(const PRECOMP_GEAR *g, int spin, int yaw, int pitch,
                          int tx, int ty, int vw, int vh,
                          uint16_t front, uint16_t side)
{
    gear_xf_t xf;
    int i;
    gear_xf_make(&xf, spin, yaw, pitch, tx, ty, vw, vh);
    for (i = 0; i < g->teeth; i++)
        gear_one_tooth(g, &xf, i, front, side);
}

/* Two color buffers. Depth stays one buffer: the blit reads color only.
 * The back buffer is the next angle, posted while SPI sends the front.
 * The join is the present tail, after CS is high. Quake has no room
 * for a second color buffer and does not use this. */
#define GEAR_W 112
#define GEAR_H 96
#define GEAR_CLEAR 0x1083u
enum { AHEAD_EMPTY = 0, AHEAD_CLEAR, AHEAD_POST, AHEAD_QUEUED, AHEAD_READY };

static uint16_t    s_gear_rgb[2][GEAR_W * GEAR_H];
static int32_t     s_gear_iz[GEAR_W * GEAR_H];
static PRECOMP_GEAR s_g1, s_g2, s_g3;
static int         s_inited;
static int         s_front;
static int         s_ahead;
static int         s_ahead_ang, s_ahead_rx, s_ahead_ry;
static int         s_spin_v[3], s_tx[3], s_ty[3], s_yaw, s_pitch;
static uint16_t    s_fc[3], s_sc[3];
static gear_xf_t   s_xf[3];
static int         s_gear_i, s_tooth_i, s_clear_y;
static int         s_spin_q, s_paused, s_spin_set;
static uint32_t    s_hook_gen;

extern uint32_t dp_pico_present_gen(void);
extern void st7365p_set_wait_slice(void (*fn)(void));
extern void dp_pico_set_frame_tail(void (*fn)(void));

static void gear_bind_back(void)
{
    picogl_bind_target(s_gear_rgb[s_front ^ 1], s_gear_iz, GEAR_W, GEAR_H, GEAR_W);
}

static void gear_clear_step(void)
{
    int y, y1, x, i;
    uint16_t *rgb = s_gear_rgb[s_front ^ 1];
    int32_t *iz = s_gear_iz;
    if (s_clear_y == 0) {
        gear_bind_back();
        picogl_begin_frame(GEAR_W, GEAR_H);
        picogl_set_depth(1);
    }
    y1 = s_clear_y + 16;
    if (y1 > GEAR_H) y1 = GEAR_H;
    for (y = s_clear_y; y < y1; y++) {
        uint16_t *row = rgb + y * GEAR_W;
        int32_t *izr = iz + y * GEAR_W;
        for (x = 0; x < GEAR_W; x++) {
            row[x] = (uint16_t)GEAR_CLEAR;
            izr[x] = 0;
        }
    }
    s_clear_y = y1;
    if (s_clear_y < GEAR_H) return;
    for (i = 0; i < 3; i++)
        gear_xf_make(&s_xf[i], s_spin_v[i], s_yaw, s_pitch,
                     s_tx[i], s_ty[i], GEAR_W, GEAR_H);
    s_gear_i = 0;
    s_tooth_i = 0;
    s_ahead = AHEAD_POST;
}

/* One tooth, or 0 when this gear's teeth are done. The ring stays under
 * 128 so the post does not reap inside the SPI wait. */
static int gear_post_one(void)
{
    const PRECOMP_GEAR *g;
    if (s_gear_i >= 3) return 0;
    g = (s_gear_i == 0) ? &s_g1 : (s_gear_i == 1) ? &s_g2 : &s_g3;
    if (s_tooth_i >= g->teeth) {
        s_gear_i++;
        s_tooth_i = 0;
        return 1;
    }
    gear_bind_back();
    gear_one_tooth(g, &s_xf[s_gear_i], s_tooth_i, s_fc[s_gear_i], s_sc[s_gear_i]);
    s_tooth_i++;
    if (s_tooth_i >= g->teeth) {
        s_gear_i++;
        s_tooth_i = 0;
    }
    return 1;
}

static void gear_wait_slice(void)
{
    if (s_hook_gen != dp_pico_present_gen()) return;
    if (s_ahead == AHEAD_CLEAR) {
        if (s_clear_y == 0 && picogl_span_queued() != 0) return;
        gear_clear_step();
        return;
    }
    if (s_ahead != AHEAD_POST) return;
    if (picogl_span_queued() > 100) return;
    if (!gear_post_one()) {
        s_ahead = AHEAD_QUEUED;
        st7365p_set_wait_slice(0);
    }
}

static void gear_ahead_finish(void)
{
    if (s_ahead == AHEAD_EMPTY || s_ahead == AHEAD_READY) return;
    st7365p_set_wait_slice(0);
    while (s_ahead == AHEAD_CLEAR) gear_clear_step();
    while (s_ahead == AHEAD_POST) {
        if (!gear_post_one()) s_ahead = AHEAD_QUEUED;
    }
    if (s_ahead == AHEAD_QUEUED) {
        gear_bind_back();
        picogl_flush_target();
        s_ahead = AHEAD_READY;
    }
}

static void gear_frame_tail(void)
{
    /* CS is high. Finish the back buffer so the next present finds an
     * empty ring, including when the next paint is not gears. */
    gear_ahead_finish();
}

static void gear_ahead_drop(void)
{
    if (s_ahead == AHEAD_POST || s_ahead == AHEAD_QUEUED) {
        gear_bind_back();
        picogl_flush_target();
    }
    s_ahead = AHEAD_EMPTY;
    st7365p_set_wait_slice(0);
}

static void gear_ahead_arm(float angle_deg, float view_rotx, float view_roty)
{
    s_spin_v[0] = (int)angle_deg;
    s_spin_v[1] = (int)(-2.0f * angle_deg - 9.0f);
    s_spin_v[2] = (int)(-2.0f * angle_deg - 25.0f);
    s_yaw = (int)view_roty;
    s_pitch = (int)view_rotx;
    s_ahead_ang = s_spin_v[0];
    s_ahead_rx = s_pitch;
    s_ahead_ry = s_yaw;
    s_tx[0] = -3 * 256; s_ty[0] = -2 * 256; s_fc[0] = 0xF800u; s_sc[0] = 0xA000u;
    s_tx[1] = 794;      s_ty[1] = -2 * 256; s_fc[1] = 0x07E0u; s_sc[1] = 0x04A0u;
    s_tx[2] = -794;     s_ty[2] = 1075;     s_fc[2] = 0x001Fu; s_sc[2] = 0x0010u;
    s_clear_y = 0;
    s_ahead = AHEAD_CLEAR;
    s_hook_gen = dp_pico_present_gen();
    st7365p_set_wait_slice(gear_wait_slice);
    dp_pico_set_frame_tail(gear_frame_tail);
}

static int gear_ahead_match(float angle_deg, float view_rotx, float view_roty)
{
    return s_ahead != AHEAD_EMPTY &&
           s_ahead_ang == (int)angle_deg &&
           s_ahead_rx == (int)view_rotx &&
           s_ahead_ry == (int)view_roty;
}

static void gear_draw_sync(float angle_deg, float view_rotx, float view_roty)
{
    picogl_bind_target(s_gear_rgb[s_front], s_gear_iz, GEAR_W, GEAR_H, GEAR_W);
    picogl_begin_frame(GEAR_W, GEAR_H);
    picogl_set_depth(1);
    picogl_clear_target(1, 1, (uint16_t)GEAR_CLEAR);
    draw_gear_int(&s_g1, (int)angle_deg, (int)view_roty, (int)view_rotx,
                  -3 * 256, -2 * 256, GEAR_W, GEAR_H, 0xF800u, 0xA000u);
    draw_gear_int(&s_g2, (int)(-2.0f * angle_deg - 9.0f),
                  (int)view_roty, (int)view_rotx,
                  794, -2 * 256, GEAR_W, GEAR_H, 0x07E0u, 0x04A0u);
    draw_gear_int(&s_g3, (int)(-2.0f * angle_deg - 25.0f),
                  (int)view_roty, (int)view_rotx,
                  -794, 1075, GEAR_W, GEAR_H, 0x001Fu, 0x0010u);
    picogl_flush_target();
}

void glgears_segui_spin(int angle_q, int yaw_q, int paused)
{
    (void)yaw_q;
    s_spin_q = angle_q;
    s_paused = paused;
    s_spin_set = 1;
}
#endif

uint32_t gl_perf_us(void) { return 0; }

void glgears_segui_render(GDEV *dev, const RECT *bounds, float angle_deg,
                          float view_rotx, float view_roty)
{
    int w, h;
#if !defined(PICO_REAL_HW)
    ZBuffer *zb;
    static GLfloat pos[4]   = { 5.0f, 5.0f, 10.0f, 0.0f };
    static GLfloat red[4]   = { 0.85f, 0.15f, 0.15f, 1.0f };
    static GLfloat green[4] = { 0.15f, 0.75f, 0.20f, 1.0f };
    static GLfloat blue[4]  = { 0.20f, 0.35f, 0.85f, 1.0f };
    static GLfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static GLfloat amb[4]   = { 0.2f, 0.2f, 0.2f, 1.0f };
#endif

#if defined(PICO_REAL_HW)
    /* Two 112x96 color buffers. The front is blitted. The back is the next
     * angle, posted from the SPI wait and joined after CS rises. */
    static int      s_valid = 0;
    static uint32_t s_frame_gen;
    static float    s_show_angle = -9999.0f;
    static float    s_show_rx    = -9999.0f;
    static float    s_show_ry    = -9999.0f;
    uint32_t gen;
    (void)dev;
    if (!bounds) return;
    gen = dp_pico_present_gen();
    w = GEAR_W;
    h = GEAR_H;
    if (!s_inited) {
        s_inited = 1;
        pico_core1_launch();
        precomp_gear_init(&s_g1, 1.0f, 4.0f, 1.0f, 20, 0.7f);
        precomp_gear_init(&s_g2, 0.5f, 2.0f, 2.0f, 10, 0.7f);
        precomp_gear_init(&s_g3, 1.3f, 2.0f, 0.5f, 10, 0.7f);
    }
    /* Later bands of this present only blit the front. */
    if (s_valid && gen == s_frame_gen) {
        dp_pico_blit_rgb565(bounds, s_gear_rgb[s_front], w, h);
        return;
    }
    /* Pause, or the same angle again. Do not show the ahead frame. */
    if (s_valid && angle_deg == s_show_angle &&
        view_rotx == s_show_rx && view_roty == s_show_ry) {
        /* Pause holds this picture. A predicted next angle is dropped. */
        if (s_paused) gear_ahead_drop();
        s_frame_gen = gen;
        dp_pico_blit_rgb565(bounds, s_gear_rgb[s_front], w, h);
        return;
    }
    if (gear_ahead_match(angle_deg, view_rotx, view_roty)) {
        gear_ahead_finish();
        s_front ^= 1;
        s_ahead = AHEAD_EMPTY;
    } else {
        gear_ahead_drop();
        gear_draw_sync(angle_deg, view_rotx, view_roty);
    }
    s_frame_gen = gen;
    s_show_angle = angle_deg;
    s_show_rx    = view_rotx;
    s_show_ry    = view_roty;
    s_valid      = 1;
    if (!s_paused && s_spin_set) {
        int nq = (s_spin_q + 4) & 255;
        gear_ahead_arm((float)nq * 1.4f, view_rotx, view_roty);
    }
    dp_pico_blit_rgb565(bounds, s_gear_rgb[s_front], w, h);
    return;
#else
    int x, y;
    if (!dev || !dev->pixels || !bounds) return;
    w = (int)bounds->right - (int)bounds->left;
    h = (int)bounds->bottom - (int)bounds->top;
    if (w <= 0 || h <= 0) return;

    zb = ZB_open(w, h, ZB_MODE_RGBA, NULL);
    if (!zb) return;
    glInit(zb);

    glViewport(0, 0, w, h);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    {
        GLfloat aspect = (GLfloat)w / (GLfloat)h;
        glFrustum(-aspect, aspect, -1.0f, 1.0f, 5.0f, 60.0f);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glTranslatef(0.0f, 0.0f, -40.0f);

    glClearColor(0.05f, 0.07f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glRotatef(view_rotx, 1.0f, 0.0f, 0.0f);
    glRotatef(view_roty, 0.0f, 1.0f, 0.0f);

    /* Gear 1: Red (20 teeth) */
    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle_deg, 0.0f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    build_gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glPopMatrix();

    /* Gear 2: Green (10 teeth) */
    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle_deg - 9.0f, 0.0f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    build_gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glPopMatrix();

    /* Gear 3: Blue (10 teeth) */
    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle_deg - 25.0f, 0.0f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    build_gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glPopMatrix();

    glPopMatrix();

    /* Blit TinyGL 32-bit RGBA framebuffer into GDEV bounds */
    for (y = 0; y < h; y++) {
        int dy = (int)bounds->top + y;
        const uint32_t *src;
        COLOR *dst;
        if (dy < 0 || dy >= dev->height) continue;
        src = (const uint32_t *)zb->pbuf + y * w;
        dst = dev->pixels + dy * dev->width;
        for (x = 0; x < w; x++) {
            int dx = (int)bounds->left + x;
            if (dx >= 0 && dx < dev->width) {
                dst[dx] = (COLOR)(src[x] | 0xFF000000u);
            }
        }
    }

    glClose();
    ZB_close(zb);
#endif
}
