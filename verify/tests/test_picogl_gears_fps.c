/*
 * Host timing of one GLGears-shaped frame on the PicoGL door.
 * The number is this machine, not the RP2040. Device glEnd does not
 * join; this host build joins there so the pixels are finished.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <graphics/gl.h>
#include "../../src/desktop/graphics/picogl/picogl.h"

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static int lit(const uint16_t *p, int n)
{
    int i, c = 0;
    for (i = 0; i < n; i++) if (p[i]) c++;
    return c;
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

static void frame(uint16_t *rgb, int32_t *iz, int w, int h, float angle)
{
    float aspect;
    picogl_bind_target(rgb, iz, w, h, w);
    picogl_gl_viewport(0, 0, w, h);
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
}

int main(void)
{
    enum { W = 112, H = 96 };
    uint16_t *rgb = (uint16_t *)calloc((size_t)W * H, sizeof(uint16_t));
    int32_t *iz = (int32_t *)calloc((size_t)W * H, sizeof(int32_t));
    uint64_t t0, t1;
    int pixels, tris, spans;
    if (!rgb || !iz) return 1;
    frame(rgb, iz, W, H, 12.0f);
    picogl_reset_stats();
    t0 = now_us();
    frame(rgb, iz, W, H, 18.0f);
    t1 = now_us();
    pixels = lit(rgb, W * H);
    tris = picogl_tri_n();
    spans = picogl_span_n();
    printf("gears host us=%llu tri=%d span=%d lit=%d core=%d refused=%u\n",
           (unsigned long long)(t1 - t0), tris, spans, pixels,
           pico_core1_state(), pico_core1_refused());
    printf("gears host is not an RP2040 frame time\n");
    if (tris < 20 || pixels < 100 || pixels >= W * H || pico_core1_state() == 0)
        return 1;
    return 0;
}
