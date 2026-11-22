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

static void gear_project(int x, int y, int z, int spin, int yaw, int pitch,
                         int tx, int ty, int vw, int vh,
                         int *sx, int *sy, int *iz)
{
    int cs = icos_deg(spin), ss = isin_deg(spin);
    int cy = icos_deg(yaw), syaw = isin_deg(yaw);
    int cx = icos_deg(pitch), sp = isin_deg(pitch);
    int x1 = (int)(((int)x * cs - (int)y * ss) >> 14);
    int y1 = (int)(((int)x * ss + (int)y * cs) >> 14);
    int z1 = z;
    int x2, z2, y3, z3, denom, scale;
    x1 += tx;
    y1 += ty;
    x2 = (int)(((int)x1 * cy + (int)z1 * syaw) >> 14);
    z2 = (int)(((int)z1 * cy - (int)x1 * syaw) >> 14);
    y3 = (int)(((int)y1 * cx - (int)z2 * sp) >> 14);
    z3 = (int)(((int)y1 * sp + (int)z2 * cx) >> 14) - 40 * 256;
    denom = -z3;
    if (denom < 64) denom = 64;
    scale = (5 * vh) / 2;
    *sx = vw / 2 + (x2 * scale) / denom;
    *sy = vh / 2 - (y3 * scale) / denom;
    *iz = 30000 - (denom >> 4);
    if (*iz < 1) *iz = 1;
}

static void gear_quad(int x0, int y0, int z0, int x1, int y1, int z1,
                      int x2, int y2, int z2, int x3, int y3, int z3,
                      int spin, int yaw, int pitch, int tx, int ty,
                      int vw, int vh, uint16_t rgb)
{
    int s0x, s0y, i0, s1x, s1y, i1, s2x, s2y, i2, s3x, s3y, i3, iz;
    gear_project(x0, y0, z0, spin, yaw, pitch, tx, ty, vw, vh, &s0x, &s0y, &i0);
    gear_project(x1, y1, z1, spin, yaw, pitch, tx, ty, vw, vh, &s1x, &s1y, &i1);
    gear_project(x2, y2, z2, spin, yaw, pitch, tx, ty, vw, vh, &s2x, &s2y, &i2);
    gear_project(x3, y3, z3, spin, yaw, pitch, tx, ty, vw, vh, &s3x, &s3y, &i3);
    iz = i0;
    if (i1 > iz) iz = i1;
    if (i2 > iz) iz = i2;
    picogl_screen_tri(s0x, s0y, s1x, s1y, s2x, s2y, iz, rgb);
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

static void draw_gear_int(const PRECOMP_GEAR *g, int spin, int yaw, int pitch,
                          int tx, int ty, int vw, int vh,
                          uint16_t front, uint16_t side)
{
    int i, teeth = g->teeth;
    int hw = g->ihw;
    for (i = 0; i < teeth; i++) {
        int ax, ay, bx, by, cx, cy, dx, dy;
        int ex, ey, fx, fy;
        gear_xy(g->ir1, g->ic0[i], g->is0[i], &ax, &ay);
        gear_xy(g->ir2, g->ic1[i], g->is1[i], &bx, &by);
        gear_xy(g->ir2, g->ic2[i], g->is2[i], &cx, &cy);
        gear_xy(g->ir1, g->ic3[i], g->is3[i], &dx, &dy);
        gear_xy(g->ir0, g->ic0[i], g->is0[i], &ex, &ey);
        gear_xy(g->ir0, g->ic3[i], g->is3[i], &fx, &fy);
        gear_quad(ex, ey, hw, ax, ay, hw, dx, dy, hw, fx, fy, hw,
                  spin, yaw, pitch, tx, ty, vw, vh, front);
        gear_quad(ax, ay, hw, bx, by, hw, cx, cy, hw, dx, dy, hw,
                  spin, yaw, pitch, tx, ty, vw, vh, front);
        gear_quad(bx, by, hw, cx, cy, hw, cx, cy, -hw, bx, by, -hw,
                  spin, yaw, pitch, tx, ty, vw, vh, side);
    }
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
    /* 112x96 RGB565 plus iz, static. PicoGL draws it. The center tile stays
     * GLGears. TinyGL's glBegin is not called. */
    static uint16_t     s_rgb[112 * 96];
    static int32_t      s_iz[112 * 96];
    static PRECOMP_GEAR  s_g1, s_g2, s_g3;
    static int           s_inited = 0;
    static int           s_valid = 0;
    static uint32_t      s_frame_gen;
    static float         s_last_angle = -9999.0f;
    static float         s_last_rx    = -9999.0f;
    static float         s_last_ry    = -9999.0f;
    extern uint32_t dp_pico_present_gen(void);
    uint32_t gen;
    (void)dev;
    if (!bounds) return;
    gen = dp_pico_present_gen();
    w = 112;
    h = 96;
    if (!s_inited) {
        s_inited = 1;
        picogl_bind_target(s_rgb, s_iz, w, h, w);
        pico_core1_launch();
        precomp_gear_init(&s_g1, 1.0f, 4.0f, 1.0f, 20, 0.7f);
        precomp_gear_init(&s_g2, 0.5f, 2.0f, 2.0f, 10, 0.7f);
        precomp_gear_init(&s_g3, 1.3f, 2.0f, 0.5f, 10, 0.7f);
    }
    /* present() calls this once per 16px band. One generation is one frame.
     * A repeated angle between presents only blits as well. */
    if (s_valid && (gen == s_frame_gen ||
        (angle_deg == s_last_angle && view_rotx == s_last_rx &&
         view_roty == s_last_ry))) {
        dp_pico_blit_rgb565(bounds, s_rgb, w, h);
        return;
    }
    s_frame_gen = gen;
    s_last_angle = angle_deg;
    s_last_rx    = view_rotx;
    s_last_ry    = view_roty;
    s_valid      = 1;

    /* Integer rotate and a fixed frustum. Core 1 fills the screen
     * triangles. The float GLES door stays on the host build. */
    picogl_begin_frame(w, h);
    picogl_set_depth(1);
    picogl_clear_target(1, 1, 0x1083u);
    draw_gear_int(&s_g1, (int)angle_deg, (int)view_roty, (int)view_rotx,
                  -3 * 256, -2 * 256, w, h, 0xF800u, 0xA000u);
    draw_gear_int(&s_g2, (int)(-2.0f * angle_deg - 9.0f),
                  (int)view_roty, (int)view_rotx,
                  794, -2 * 256, w, h, 0x07E0u, 0x04A0u);
    draw_gear_int(&s_g3, (int)(-2.0f * angle_deg - 25.0f),
                  (int)view_roty, (int)view_rotx,
                  -794, 1075, w, h, 0x001Fu, 0x0010u);
    /* glEnd leaves the ring queued. This join is the one before the panel read. */
    picogl_flush_target();
    dp_pico_blit_rgb565(bounds, s_rgb, w, h);
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
