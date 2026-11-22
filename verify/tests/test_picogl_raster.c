/*
 * test_picogl_raster.c — Phase 2 host gate.
 *
 * One band is filled on this core. The depth winner has to match an
 * inside test written here, not the one in picogl_span.c. The nearer
 * triangle is submitted first, so a painter that ignores iz fails.
 */
#ifndef __STDC_IEC_559__
#define __STDC_IEC_559__ 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <graphics/gl.h>
#include "../../src/desktop/graphics/picogl/picogl.h"

static int s_checks, s_fail;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_fail++;
        printf("  [FAIL] %s\n", what);
    } else {
        printf("  [ok]   %s\n", what);
    }
}

/* Independent of picogl_span.c. Pixel centers, Y down. A zero edge counts
 * as inside. A degenerate triangle does not. */
static int ref_inside(const picogl_tri_t *t, int x, int y)
{
    double px = (double)x + 0.5;
    double py = (double)y + 0.5;
    double c0 = ((double)t->x1 - t->x0) * (py - t->y0) - ((double)t->y1 - t->y0) * (px - t->x0);
    double c1 = ((double)t->x2 - t->x1) * (py - t->y1) - ((double)t->y2 - t->y1) * (px - t->x1);
    double c2 = ((double)t->x0 - t->x2) * (py - t->y2) - ((double)t->y0 - t->y2) * (px - t->x2);
    int pos = (c0 > 0.0) + (c1 > 0.0) + (c2 > 0.0);
    int neg = (c0 < 0.0) + (c1 < 0.0) + (c2 < 0.0);
    if (pos == 0 && neg == 0) return 0;
    return pos == 0 || neg == 0;
}

static void ref_row(const picogl_tri_t *tris, int n, int y, uint16_t *row, int width,
                    int *both, int *only_far, uint16_t near_rgb, uint16_t far_rgb)
{
    int x, i;
    for (x = 0; x < width; x++) {
        int32_t best = 0;
        uint16_t color = 0;
        int hit = 0;
        int seen_near = 0, seen_far = 0;
        for (i = 0; i < n; i++) {
            if (!ref_inside(&tris[i], x, y)) continue;
            if (tris[i].rgb == near_rgb) seen_near = 1;
            if (tris[i].rgb == far_rgb) seen_far = 1;
            if (!hit || tris[i].iz > best) {
                best = tris[i].iz;
                color = tris[i].rgb;
                hit = 1;
            }
        }
        row[x] = hit ? color : 0;
        if (both && seen_near && seen_far && color == near_rgb) (*both)++;
        if (only_far && seen_far && !seen_near && color == far_rgb) (*only_far)++;
    }
}

static int depth_band(void)
{
    float org[3] = {0.0f, 0.0f, 0.0f};
    float ang[3] = {0.0f, 0.0f, 0.0f};
    /* Closer triangle first, and smaller on screen. The farther one is
     * submitted second and sticks out past it. */
    float near_xyz[9] = {
        32.0f, -4.0f, -4.0f,
        32.0f,  4.0f, -4.0f,
        32.0f,  0.0f,  4.0f
    };
    float far_xyz[9] = {
        64.0f, -20.0f, -20.0f,
        64.0f,  20.0f, -20.0f,
        64.0f,   0.0f,  20.0f
    };
    int32_t *iz;
    int b, mismatches = 0;
    int both = 0, only_far = 0;
    const uint16_t near_rgb = 0xF800;
    const uint16_t far_rgb = 0x001F;

    picogl_bind_target(NULL, NULL, 0, 0, 0);
    picogl_set_cull(0);
    picogl_set_depth(1);
    picogl_begin_frame(320, 320);
    picogl_set_camera(org, ang, 320, 320);
    picogl_poly(near_xyz, NULL, 3, 0xFF0000FFu, -1);
    picogl_poly(far_xyz, NULL, 3, 0xFFFF0000u, -1);

    iz = (int32_t *)calloc(320u, sizeof(int32_t));
    if (!iz) return 0;

    for (b = 0; b < PICOGL_BANDS; b++) {
        int n = picogl_band_n(b);
        int y, i;
        uint16_t *band;
        uint16_t *want;
        int y0;
        if (n <= 0) continue;
        band = (uint16_t *)calloc((size_t)PICOGL_BAND_H * 320u, sizeof(uint16_t));
        want = (uint16_t *)calloc((size_t)PICOGL_BAND_H * 320u, sizeof(uint16_t));
        if (!band || !want) return 0;
        y0 = b * PICOGL_BAND_H;
        picogl_raster_band(b, band, 320, iz, 320);
        for (y = 0; y < PICOGL_BAND_H; y++) {
            picogl_tri_t tris[PICOGL_BAND_CAP];
            int tn = 0;
            for (i = 0; i < n && tn < PICOGL_BAND_CAP; i++) {
                const picogl_tri_t *t = picogl_band_tri(b, i);
                if (t) tris[tn++] = *t;
            }
            ref_row(tris, tn, y0 + y, want + y * 320, 320,
                    &both, &only_far, near_rgb, far_rgb);
        }
        for (i = 0; i < PICOGL_BAND_H * 320; i++) {
            if (band[i] != want[i]) {
                if (mismatches < 4) {
                    printf("  mismatch band %d pix %d got %04x want %04x\n",
                           b, i, band[i], want[i]);
                }
                mismatches++;
            }
        }
        free(band);
        free(want);
    }
    free(iz);
    check("the raster matches the software band", mismatches == 0);
    check("the nearer triangle wins where they overlap", both > 0);
    check("the farther triangle is still visible beside it", only_far > 0);
    if (mismatches || both == 0 || only_far == 0) {
        printf("  mismatches=%d overlap=%d far_only=%d drop=%d\n",
               mismatches, both, only_far, picogl_drop());
    }
    return mismatches == 0 && both > 0 && only_far > 0;
}

static int count_rgb(const uint16_t *p, int n, uint16_t rgb)
{
    int i, c = 0;
    for (i = 0; i < n; i++) if (p[i] == rgb) c++;
    return c;
}

static int door_cull_and_depth(void)
{
    enum { W = 64, H = 64 };
    uint16_t *rgb = (uint16_t *)calloc((size_t)W * H, sizeof(uint16_t));
    int32_t *iz = (int32_t *)calloc((size_t)W * H, sizeof(int32_t));
    int red, blue, green;
    if (!rgb || !iz) return 0;

    picogl_bind_target(rgb, iz, W, H, W);
    picogl_gl_viewport(0, 0, W, H);
    picogl_gl_matrix_mode(GL_PROJECTION);
    picogl_gl_load_identity();
    picogl_gl_frustum(-1.0, 1.0, -1.0, 1.0, 5.0, 60.0);
    picogl_gl_matrix_mode(GL_MODELVIEW);
    picogl_gl_load_identity();
    picogl_gl_disable(GL_LIGHTING);
    picogl_gl_enable(GL_DEPTH_TEST);
    picogl_gl_enable(GL_CULL_FACE);
    picogl_gl_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    picogl_gl_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* CCW when looking down -Z: lower-left, lower-right, top. */
    picogl_gl_color3f(1.0f, 0.0f, 0.0f);
    picogl_gl_begin(GL_TRIANGLES);
    picogl_gl_vertex3f(-0.6f, -0.6f, -12.0f);
    picogl_gl_vertex3f( 0.6f, -0.6f, -12.0f);
    picogl_gl_vertex3f( 0.0f,  0.6f, -12.0f);
    picogl_gl_end();
    red = count_rgb(rgb, W * H, 0xF800);
    check("a front face reaches the target", red > 0);

    picogl_gl_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    picogl_gl_color3f(0.0f, 1.0f, 0.0f);
    picogl_gl_begin(GL_TRIANGLES);
    picogl_gl_vertex3f( 0.6f, -0.6f, -12.0f);
    picogl_gl_vertex3f(-0.6f, -0.6f, -12.0f);
    picogl_gl_vertex3f( 0.0f,  0.6f, -12.0f);
    picogl_gl_end();
    green = count_rgb(rgb, W * H, 0x07E0);
    check("a back face is culled", green == 0);

    picogl_gl_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    picogl_gl_color3f(0.0f, 0.0f, 1.0f);
    picogl_gl_begin(GL_TRIANGLES);
    picogl_gl_vertex3f(-2.0f, -2.0f, -30.0f);
    picogl_gl_vertex3f( 2.0f, -2.0f, -30.0f);
    picogl_gl_vertex3f( 0.0f,  2.0f, -30.0f);
    picogl_gl_end();
    picogl_gl_color3f(1.0f, 0.0f, 0.0f);
    picogl_gl_begin(GL_TRIANGLES);
    picogl_gl_vertex3f(-0.35f, -0.35f, -10.0f);
    picogl_gl_vertex3f( 0.35f, -0.35f, -10.0f);
    picogl_gl_vertex3f( 0.0f,   0.35f, -10.0f);
    picogl_gl_end();
    red = count_rgb(rgb, W * H, 0xF800);
    blue = count_rgb(rgb, W * H, 0x001F);
    check("the door keeps the nearer color", rgb[(H / 2) * W + (W / 2)] == 0xF800);
    check("the door keeps the farther color outside it", blue > 0 && red > 0);
    if (red == 0 || blue == 0 || rgb[(H / 2) * W + (W / 2)] != 0xF800) {
        printf("  door red=%d blue=%d center=%04x\n",
               red, blue, rgb[(H / 2) * W + (W / 2)]);
    }

    picogl_bind_target(NULL, NULL, 0, 0, 0);
    picogl_set_cull(0);
    free(rgb);
    free(iz);
    return 1;
}

static int queue_does_not_drop(void)
{
    float org[3] = {0.0f, 0.0f, 0.0f};
    float ang[3] = {0.0f, 0.0f, 0.0f};
    float front[9] = {
        64.0f, -8.0f, -8.0f,
        64.0f,  8.0f, -8.0f,
        64.0f,  0.0f,  8.0f
    };
    uint16_t rgb[8 * 8];
    int32_t iz[8 * 8];
    int i;

    memset(rgb, 0, sizeof rgb);
    memset(iz, 0, sizeof iz);
    picogl_bind_target(rgb, iz, 8, 8, 8);
    picogl_begin_frame(320, 320);
    picogl_set_camera(org, ang, 320, 320);
    picogl_set_cull(0);
    for (i = 0; i < PICOGL_BAND_CAP + 4; i++)
        picogl_poly(front, NULL, 3, 0xFF0000FFu, -1);
    picogl_flush_target();
    check("a bound target draws past the band cap without a drop",
          picogl_drop() == 0 && picogl_tri_n() == PICOGL_BAND_CAP + 4);
    picogl_bind_target(NULL, NULL, 0, 0, 0);
    return picogl_drop() == 0;
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PicoGL phase 2: one band, nearer triangle wins\n");
    printf("==========================================================\n");
    depth_band();
    door_cull_and_depth();
    queue_does_not_drop();
    printf("  checks=%d failures=%d\n", s_checks, s_fail);
    if (s_fail == 0) {
        printf("[CI-TEST] SUCCESS - the depth winner matches the software band\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the raster and the software band disagree\n");
    return 1;
}
