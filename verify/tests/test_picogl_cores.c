/*
 * Both cores for the three PicoCalc scenes.
 *
 * QEMU mps2-an385 has one CPU. QEMU mps2-an521 leaves CPU1 powered off
 * (SSE-200 has no guest power control), so `make test-pico` still prints
 * "1 OFF". This process is the stand-in: core 0 submits, the pthread
 * drains the same span ring. A scene fails when the ring head does not
 * move with the triangle count, the consumer does not finish, or core 1
 * stays down.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <graphics/gl.h>
#include "picogl.h"
#include "start_sim.h"

static int g_fail;

static int lit_n(const uint16_t *p, int n)
{
    int i, c = 0;
    for (i = 0; i < n; i++) if (p[i]) c++;
    return c;
}

static void expect(const char *name, uint32_t posted0, unsigned refused0,
                   int min_tri, int min_span, int min_lit, int lit)
{
    uint32_t posted = picogl_span_posted() - posted0;
    unsigned refused = pico_core1_refused() - refused0;
    int tris = picogl_tri_n();
    int spans = picogl_span_n();
    int core = pico_core1_state();
    int done = picogl_span_finished() == picogl_span_posted();
    printf("cores %s tri=%d posted=%u span=%d lit=%d core=%d refused=%u done=%d\n",
           name, tris, posted, spans, lit, core, refused, done);
    if (core == 0 || refused != 0 || !done || tris < min_tri ||
        posted != (uint32_t)tris || spans < min_span || lit < min_lit) {
        printf("cores %s FAIL\n", name);
        g_fail = 1;
    }
}

static void tooth_gear(float radius, int teeth)
{
    int i;
    picogl_gl_begin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        float a0 = (float)i / (float)teeth * 6.2831853f;
        float a1 = (float)(i + 1) / (float)teeth * 6.2831853f;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        float r0 = radius * ((i & 1) ? 1.0f : 0.72f);
        float r1 = radius * ((i & 1) ? 0.72f : 1.0f);
        picogl_gl_vertex3f(r0 * c0, r0 * s0,  0.45f);
        picogl_gl_vertex3f(r1 * c1, r1 * s1,  0.45f);
        picogl_gl_vertex3f(r1 * c1, r1 * s1, -0.45f);
        picogl_gl_vertex3f(r0 * c0, r0 * s0, -0.45f);
    }
    picogl_gl_end();
}

static void gears(uint16_t *rgb, int32_t *iz, int w, int h, float angle)
{
    float aspect;
    uint32_t posted0;
    unsigned refused0;
    picogl_bind_target(rgb, iz, w, h, w);
    picogl_gl_viewport(0, 0, w, h);
    posted0 = picogl_span_posted();
    refused0 = pico_core1_refused();
    picogl_gl_matrix_mode(GL_PROJECTION);
    picogl_gl_load_identity();
    aspect = (float)w / (float)h;
    picogl_gl_frustum(-aspect, aspect, -1.0, 1.0, 5.0, 60.0);
    picogl_gl_matrix_mode(GL_MODELVIEW);
    picogl_gl_load_identity();
    picogl_gl_enable(GL_DEPTH_TEST);
    picogl_gl_disable(GL_CULL_FACE);
    picogl_gl_disable(GL_LIGHTING);
    picogl_gl_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    picogl_gl_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    picogl_gl_translate_f(0.0f, 0.0f, -18.0f);
    picogl_gl_rotate_f(20.0f, 1.0f, 0.0f, 0.0f);
    picogl_gl_rotate_f(angle, 0.0f, 0.0f, 1.0f);
    picogl_gl_color3f(0.85f, 0.15f, 0.15f);
    picogl_gl_push_matrix();
    picogl_gl_translate_f(-3.0f, -1.5f, 0.0f);
    tooth_gear(3.2f, 20);
    picogl_gl_pop_matrix();
    picogl_gl_color3f(0.15f, 0.75f, 0.20f);
    picogl_gl_push_matrix();
    picogl_gl_translate_f(3.2f, -1.2f, 0.0f);
    picogl_gl_rotate_f(-2.0f * angle, 0.0f, 0.0f, 1.0f);
    tooth_gear(2.0f, 10);
    picogl_gl_pop_matrix();
    picogl_gl_color3f(0.20f, 0.35f, 0.85f);
    picogl_gl_push_matrix();
    picogl_gl_translate_f(-2.4f, 3.4f, 0.0f);
    picogl_gl_rotate_f(-2.0f * angle, 0.0f, 0.0f, 1.0f);
    tooth_gear(1.6f, 10);
    picogl_gl_pop_matrix();
    picogl_flush_target();
    expect("gears", posted0, refused0, 20, 1, 100, lit_n(rgb, w * h));
}

/* Same 6x4 sheet as xmb_pico.c. One full target, not a 16-row band. */
#define WAVE_NX 6
#define WAVE_NY 4
static const int s_wave_sin[16] = {
    0, 5, 9, 12, 13, 12, 9, 5, 0, -5, -9, -12, -13, -12, -9, -5
};

static void wave_point(int i, int j, int cx, int horizon, int depth,
                       int *ox, int *oy)
{
    int persp = j + 1;
    int wob = s_wave_sin[(i * 3) & 15];
    int y = horizon + (j * j * depth) / (WAVE_NY * WAVE_NY);
    int x = cx + (i - WAVE_NX / 2) * (8 + persp * 10);
    y += (wob * persp) / 6;
    *ox = x;
    *oy = y;
}

static void xmb(uint16_t *rgb, int w, int h)
{
    uint32_t posted0;
    unsigned refused0;
    int j, i;
    int cx = w / 2;
    int horizon = 24;
    int depth = h - horizon - 8;
    memset(rgb, 0, (size_t)w * (size_t)h * sizeof(uint16_t));
    picogl_bind_target(rgb, 0, w, h, w);
    picogl_set_depth(0);
    picogl_reset_stats();
    posted0 = picogl_span_posted();
    refused0 = pico_core1_refused();
    for (j = 0; j < WAVE_NY; j++) {
        uint16_t color = (uint16_t)(0x0410u * (unsigned)(j + 1));
        for (i = 0; i < WAVE_NX; i++) {
            int xa, ya, xb, yb, xc, yc, xd, yd;
            wave_point(i, j, cx, horizon, depth, &xa, &ya);
            wave_point(i + 1, j, cx, horizon, depth, &xb, &yb);
            wave_point(i + 1, j + 1, cx, horizon, depth, &xc, &yc);
            wave_point(i, j + 1, cx, horizon, depth, &xd, &yd);
            picogl_screen_tri(xa, ya, xb, yb, xc, yc, j + 1, color);
            picogl_screen_tri(xa, ya, xc, yc, xd, yd, j + 1, color);
        }
    }
    picogl_flush_target();
    expect("xmb", posted0, refused0, 16, 1, 50, lit_n(rgb, w * h));
}

int main(void)
{
    enum { GW = 112, GH = 96, XW = 320, XH = 160 };
    uint16_t *grgb = (uint16_t *)calloc((size_t)GW * GH, sizeof(uint16_t));
    int32_t *giz = (int32_t *)calloc((size_t)GW * GH, sizeof(int32_t));
    uint16_t *xrgb = (uint16_t *)calloc((size_t)XW * XH, sizeof(uint16_t));
    uint32_t posted0;
    unsigned refused0;
    if (!grgb || !giz || !xrgb) return 1;

    printf("qemu pico core 1 stays OFF: an385 is one CPU and an521 "
           "never powers CPU1. This pthread is the two-core check.\n");

    gears(grgb, giz, GW, GH, 12.0f);
    gears(grgb, giz, GW, GH, 18.0f);
    xmb(xrgb, XW, XH);

    start_sim_init();
    start_sim_render();
    picogl_reset_stats();
    posted0 = picogl_span_posted();
    refused0 = pico_core1_refused();
    start_sim_render();
    expect("quake", posted0, refused0, 40, 1, 1000, start_sim_lit());

    free(grgb);
    free(giz);
    free(xrgb);
    if (g_fail) {
        printf("cores FAIL\n");
        return 1;
    }
    printf("cores ok\n");
    return 0;
}
