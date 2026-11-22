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

static int screen_tri_matches(void)
{
    enum { W = 32, H = 24 };
    uint16_t *rgb = (uint16_t *)calloc((size_t)W * H, sizeof(uint16_t));
    int32_t *iz = (int32_t *)calloc((size_t)W * H, sizeof(int32_t));
    picogl_tri_t t;
    int x, y, mismatches = 0;
    if (!rgb || !iz) return 0;
    memset(&t, 0, sizeof t);
    t.x0 = 2; t.y0 = 2;
    t.x1 = 28; t.y1 = 4;
    t.x2 = 8; t.y2 = 20;
    t.iz = 10;
    t.rgb = 0xF800;
    picogl_bind_target(rgb, iz, W, H, W);
    picogl_set_cull(0);
    picogl_set_depth(1);
    picogl_clear_target(1, 1, 0);
    picogl_screen_tri(t.x0, t.y0, t.x1, t.y1, t.x2, t.y2, t.iz, t.rgb);
    picogl_flush_target();
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int inside = ref_inside(&t, x, y);
            uint16_t got = rgb[y * W + x];
            if ((inside && got != t.rgb) || (!inside && got == t.rgb)) {
                if (mismatches < 4)
                    printf("  screen mismatch %d,%d got %04x inside %d\n",
                           x, y, got, inside);
                mismatches++;
            }
        }
    }
    check("a posted screen triangle matches the center test", mismatches == 0);
    check("the host consumer ran that triangle", pico_core1_state() != 0);
    picogl_bind_target(NULL, NULL, 0, 0, 0);
    free(rgb);
    free(iz);
    return mismatches == 0 && pico_core1_state() != 0;
}

/* Two bands of the same list. Resume must match a fresh divide on
 * every row, including a sealed edge, a horizontal edge, and a view
 * that does not start on a band. */
static int resume_matches(void)
{
    picogl_tri_t tris[4];
    enum { W = 80, BANDS = 4, BH = 16, H = BANDS * BH };
    uint16_t *fresh;
    uint16_t *kept;
    int32_t *iz;
    int q[8 * 3];
    int r[8 * 3];
    int16_t ey[8];
    int cover, oy, band, i, bad;
    memset(tris, 0, sizeof tris);
    tris[0].x0 = 2;  tris[0].y0 = 0;
    tris[0].x1 = 70; tris[0].y1 = 0;
    tris[0].x2 = 20; tris[0].y2 = 50;
    tris[0].iz = 40; tris[0].rgb = 0xF800; tris[0].flags = (uint8_t)PICOGL_SEAL;
    tris[1].x0 = 8;  tris[1].y0 = 18;
    tris[1].x1 = 40; tris[1].y1 = 22;
    tris[1].x2 = 12; tris[1].y2 = 48;
    tris[1].iz = 20; tris[1].rgb = 0x07E0; tris[1].flags = (uint8_t)PICOGL_FLAT;
    tris[2].x0 = 30; tris[2].y0 = 4;
    tris[2].x1 = 60; tris[2].y1 = 30;
    tris[2].x2 = 28; tris[2].y2 = 36;
    tris[2].iz = 10; tris[2].rgb = 0x001F; tris[2].flags = (uint8_t)PICOGL_SEAL;
    /* One pixel of height across the view. step/d is past the old 48-step
     * repair, so the precomputed row delta has to match a fresh divide. */
    tris[3].x0 = 0;  tris[3].y0 = 6;
    tris[3].x1 = 79; tris[3].y1 = 7;
    tris[3].x2 = 10; tris[3].y2 = 58;
    tris[3].iz = 5; tris[3].rgb = 0xFFE0; tris[3].flags = (uint8_t)PICOGL_FLAT;
    fresh = (uint16_t *)calloc((size_t)H * W, sizeof(uint16_t));
    kept = (uint16_t *)calloc((size_t)H * W, sizeof(uint16_t));
    iz = (int32_t *)calloc((size_t)BH * W, sizeof(int32_t));
    if (!fresh || !kept || !iz) return 0;
    bad = 0;
    picogl_set_depth(1);
    picogl_set_edge_step(0, 0);
    for (cover = 0; cover < 2; cover++) {
        for (oy = 0; oy <= 8; oy += 8) {
            picogl_set_cover(cover);
            picogl_set_edge_resume(0, 0, 0, 0);
            for (band = 0; band < BANDS; band++) {
                memset(iz, 0, (size_t)BH * W * sizeof(int32_t));
                picogl_paint_band(tris, 4, fresh + band * BH * W, W,
                                  band * BH, BH, 0, oy, W, H, iz, 0x0000);
            }
            memset(ey, 0xff, sizeof ey);
            picogl_set_edge_resume(q, r, ey, 8);
            for (band = 0; band < BANDS; band++) {
                memset(iz, 0, (size_t)BH * W * sizeof(int32_t));
                picogl_paint_band(tris, 4, kept + band * BH * W, W,
                                  band * BH, BH, 0, oy, W, H, iz, 0x0000);
            }
            for (i = 0; i < H * W; i++) {
                if (fresh[i] != kept[i]) bad++;
            }
        }
    }
    picogl_set_edge_resume(0, 0, 0, 0);
    picogl_set_edge_step(0, 0);
    picogl_set_cover(0);
    free(fresh);
    free(kept);
    free(iz);
    check("a resumed edge matches a fresh divide", bad == 0);
    return bad == 0;
}

/* Left pair covers x 0..49. The far triangle owns the right side.
 * Stopping when the left bbox is full would drop that hole. */
static int cover_keeps_a_hole(void)
{
    picogl_tri_t tris[3];
    uint16_t *rgb;
    int32_t *iz;
    int wall, hole;
    uint16_t near_rgb, far_rgb;
    memset(tris, 0, sizeof tris);
    tris[0].x0 = 0;  tris[0].y0 = 0;
    tris[0].x1 = 49; tris[0].y1 = 0;
    tris[0].x2 = 0;  tris[0].y2 = 15;
    tris[0].iz = 40; tris[0].rgb = 0xF800; tris[0].flags = (uint8_t)PICOGL_FLAT;
    tris[1].x0 = 49; tris[1].y0 = 0;
    tris[1].x1 = 49; tris[1].y1 = 15;
    tris[1].x2 = 0;  tris[1].y2 = 15;
    tris[1].iz = 40; tris[1].rgb = 0xF800; tris[1].flags = (uint8_t)PICOGL_FLAT;
    tris[2].x0 = 40; tris[2].y0 = 0;
    tris[2].x1 = 79; tris[2].y1 = 0;
    tris[2].x2 = 79; tris[2].y2 = 15;
    tris[2].iz = 10; tris[2].rgb = 0x001F; tris[2].flags = (uint8_t)PICOGL_FLAT;
    rgb = (uint16_t *)calloc(16u * 80u, sizeof(uint16_t));
    iz = (int32_t *)calloc(16u * 80u, sizeof(int32_t));
    if (!rgb || !iz) return 0;
    picogl_set_cover(1);
    picogl_set_edge_resume(0, 0, 0, 0);
    picogl_paint_band(tris, 3, rgb, 80, 0, 16, 0, 0, 80, 16, iz, 0x0000);
    /* The band stores panel byte order. */
    near_rgb = (uint16_t)((0xF800 >> 8) | (0xF800 << 8));
    far_rgb = (uint16_t)((0x001F >> 8) | (0x001F << 8));
    wall = rgb[8 * 80 + 10] == near_rgb;
    hole = rgb[8 * 80 + 70] == far_rgb;
    if (!wall || !hole)
        printf("  cover hole wall %04x hole %04x\n",
               rgb[8 * 80 + 10], rgb[8 * 80 + 70]);
    picogl_set_cover(0);
    free(rgb);
    free(iz);
    check("a filled span leaves the uncovered side", wall && hole);
    return wall && hole;
}

static int span_box(const picogl_tri_t *t, int n, int *w, int *h)
{
    int i, minx, maxx, miny, maxy;
    if (n < 1) return 0;
    minx = maxx = t[0].x0;
    miny = maxy = t[0].y0;
    for (i = 0; i < n; i++) {
        int xs[3], ys[3], k;
        xs[0] = t[i].x0; xs[1] = t[i].x1; xs[2] = t[i].x2;
        ys[0] = t[i].y0; ys[1] = t[i].y1; ys[2] = t[i].y2;
        for (k = 0; k < 3; k++) {
            if (xs[k] < minx) minx = xs[k];
            if (xs[k] > maxx) maxx = xs[k];
            if (ys[k] < miny) miny = ys[k];
            if (ys[k] > maxy) maxy = ys[k];
        }
    }
    *w = maxx - minx;
    *h = maxy - miny;
    return *w > 0 && *h > 0;
}

static int count_diff(const uint16_t *a, const uint16_t *b, int n, int w,
                      const char *tag)
{
    int i, bad = 0;
    for (i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        if (bad < 4)
            printf("  %s %d,%d %04x %04x\n", tag, i % w, i / w, a[i], b[i]);
        bad++;
    }
    return bad;
}

static void fill_resume_tris(picogl_tri_t *tris)
{
    memset(tris, 0, 4 * sizeof *tris);
    tris[0].x0 = 2;  tris[0].y0 = 0;
    tris[0].x1 = 70; tris[0].y1 = 0;
    tris[0].x2 = 20; tris[0].y2 = 50;
    tris[0].iz = 40; tris[0].rgb = 0xF800; tris[0].flags = (uint8_t)PICOGL_SEAL;
    tris[1].x0 = 8;  tris[1].y0 = 18;
    tris[1].x1 = 40; tris[1].y1 = 22;
    tris[1].x2 = 12; tris[1].y2 = 48;
    tris[1].iz = 20; tris[1].rgb = 0x07E0; tris[1].flags = (uint8_t)PICOGL_FLAT;
    tris[2].x0 = 30; tris[2].y0 = 4;
    tris[2].x1 = 60; tris[2].y1 = 30;
    tris[2].x2 = 28; tris[2].y2 = 36;
    tris[2].iz = 10; tris[2].rgb = 0x001F; tris[2].flags = (uint8_t)PICOGL_SEAL;
    tris[3].x0 = 0;  tris[3].y0 = 6;
    tris[3].x1 = 79; tris[3].y1 = 7;
    tris[3].x2 = 10; tris[3].y2 = 58;
    tris[3].iz = 5; tris[3].rgb = 0xFFE0; tris[3].flags = (uint8_t)PICOGL_FLAT;
}

/* The later half paints another band, then holes take that pixel or
 * sky. On the host both halves run here. The picture has to match one
 * paint_band, including a saved row step. */
static int split_matches(void)
{
    picogl_tri_t tris[4];
    enum { W = 80, BANDS = 4, BH = 16, H = BANDS * BH, N = 8 };
    uint16_t *one;
    uint16_t *two;
    uint16_t *far;
    int32_t *iz;
    int q[N * 3];
    int r[N * 3];
    int16_t ey[N];
    int16_t dq[N * 3];
    uint16_t dr[N * 3];
    int oy, band, bad_step, bad_split, bad_hole, wall, hole;
    uint16_t near_rgb, far_rgb;
    picogl_core1_job();
    check("an idle far-half job stays idle", picogl_core1_busy() == 0);
    fill_resume_tris(tris);
    one = (uint16_t *)calloc((size_t)H * W, sizeof(uint16_t));
    two = (uint16_t *)calloc((size_t)H * W, sizeof(uint16_t));
    far = (uint16_t *)calloc((size_t)BH * W, sizeof(uint16_t));
    iz = (int32_t *)calloc((size_t)BH * W, sizeof(int32_t));
    if (!one || !two || !far || !iz) return 0;
    bad_step = 0;
    bad_split = 0;
    picogl_set_depth(1);
    picogl_set_cover(1);
    for (oy = 0; oy <= 8; oy += 8) {
        picogl_set_edge_resume(0, 0, 0, 0);
        picogl_set_edge_step(0, 0);
        for (band = 0; band < BANDS; band++) {
            memset(iz, 0, (size_t)BH * W * sizeof(int32_t));
            picogl_paint_band(tris, 4, one + band * BH * W, W,
                              band * BH, BH, 0, oy, W, H, iz, 0x0000);
        }
        memset(ey, 0xff, sizeof ey);
        memset(dq, 0, sizeof dq);
        memset(dr, 0, sizeof dr);
        picogl_set_edge_resume(q, r, ey, N);
        picogl_set_edge_step(dq, dr);
        for (band = 0; band < BANDS; band++) {
            memset(iz, 0, (size_t)BH * W * sizeof(int32_t));
            picogl_paint_band(tris, 4, two + band * BH * W, W,
                              band * BH, BH, 0, oy, W, H, iz, 0x0000);
        }
        bad_step += count_diff(one, two, H * W, W, "step");
        memset(ey, 0xff, sizeof ey);
        memset(dq, 0, sizeof dq);
        memset(dr, 0, sizeof dr);
        for (band = 0; band < BANDS; band++) {
            memset(iz, 0, (size_t)BH * W * sizeof(int32_t));
            memset(far, 0xA5, (size_t)BH * W * sizeof(uint16_t));
            picogl_paint_band_split(tris, 4, two + band * BH * W, far, W,
                                    band * BH, BH, 0, oy, W, H, iz, 0x0000);
        }
        bad_split += count_diff(one, two, H * W, W, "split");
    }
    /* Three triangles, no resume. The cut is 1, so the right side is
     * the far half. A poisoned far band must not leak into a hole. */
    memset(tris, 0, sizeof tris);
    tris[0].x0 = 0;  tris[0].y0 = 0;
    tris[0].x1 = 49; tris[0].y1 = 0;
    tris[0].x2 = 0;  tris[0].y2 = 15;
    tris[0].iz = 40; tris[0].rgb = 0xF800; tris[0].flags = (uint8_t)PICOGL_FLAT;
    tris[1].x0 = 49; tris[1].y0 = 0;
    tris[1].x1 = 49; tris[1].y1 = 15;
    tris[1].x2 = 0;  tris[1].y2 = 15;
    tris[1].iz = 40; tris[1].rgb = 0xF800; tris[1].flags = (uint8_t)PICOGL_FLAT;
    tris[2].x0 = 40; tris[2].y0 = 0;
    tris[2].x1 = 79; tris[2].y1 = 0;
    tris[2].x2 = 79; tris[2].y2 = 15;
    tris[2].iz = 10; tris[2].rgb = 0x001F; tris[2].flags = (uint8_t)PICOGL_FLAT;
    picogl_set_edge_resume(0, 0, 0, 0);
    picogl_set_edge_step(0, 0);
    picogl_set_cover(1);
    memset(one, 0, (size_t)BH * W * sizeof(uint16_t));
    memset(two, 0, (size_t)BH * W * sizeof(uint16_t));
    memset(far, 0xA5, (size_t)BH * W * sizeof(uint16_t));
    memset(iz, 0, (size_t)BH * W * sizeof(int32_t));
    picogl_paint_band(tris, 3, one, W, 0, BH, 0, 0, W, BH, iz, 0x0000);
    picogl_paint_band_split(tris, 3, two, far, W, 0, BH, 0, 0, W, BH, iz, 0x0000);
    bad_hole = count_diff(one, two, BH * W, W, "hole");
    near_rgb = (uint16_t)((0xF800 >> 8) | (0xF800 << 8));
    far_rgb = (uint16_t)((0x001F >> 8) | (0x001F << 8));
    wall = two[8 * W + 10] == near_rgb;
    hole = two[8 * W + 70] == far_rgb;
    if (!wall || !hole)
        printf("  split hole wall %04x hole %04x\n",
               two[8 * W + 10], two[8 * W + 70]);
    picogl_set_edge_resume(0, 0, 0, 0);
    picogl_set_edge_step(0, 0);
    picogl_set_cover(0);
    free(one);
    free(two);
    free(far);
    free(iz);
    check("a saved row step matches a fresh divide", bad_step == 0);
    check("a split band matches one core", bad_split == 0 && bad_hole == 0);
    check("a split band keeps the far side of a hole", wall && hole);
    return bad_step == 0 && bad_split == 0 && bad_hole == 0 && wall && hole;
}

/* 160x320 render stretched onto a square. The world square must be
 * about twice as tall as it is wide in the render buffer. Viewport
 * aspect leaves it square, which the stretch then widens. */
static int display_aspect(void)
{
    picogl_tri_t cap[8];
    float org[3] = {0.0f, 0.0f, 0.0f};
    float ang[3] = {0.0f, 0.0f, 0.0f};
    int n, w = 0, h = 0, ok_dest, ok_view;
    picogl_bind_target(0, 0, 0, 0, 0);
    picogl_set_cull(0);
    picogl_capture(cap, 8);
    picogl_begin_frame(160, 320);
    picogl_set_display(320, 320);
    picogl_set_camera(org, ang, 160, 320);
    picogl_world_tri(64, -8, -8, 64, 8, -8, 64, 8, 8, 0xFF0000FFu);
    picogl_world_tri(64, -8, -8, 64, 8, 8, 64, -8, 8, 0xFF0000FFu);
    n = picogl_captured();
    ok_dest = span_box(cap, n, &w, &h) && h > w + w / 2;
    if (!ok_dest)
        printf("  dest aspect n=%d w=%d h=%d\n", n, w, h);
    picogl_begin_frame(160, 320);
    picogl_set_display(0, 0);
    picogl_set_camera(org, ang, 160, 320);
    picogl_world_tri(64, -8, -8, 64, 8, -8, 64, 8, 8, 0xFF0000FFu);
    picogl_world_tri(64, -8, -8, 64, 8, 8, 64, -8, 8, 0xFF0000FFu);
    n = picogl_captured();
    ok_view = span_box(cap, n, &w, &h) && h * 2 > w && w * 2 > h;
    if (!ok_view)
        printf("  view aspect n=%d w=%d h=%d\n", n, w, h);
    picogl_capture(0, 0);
    picogl_set_display(0, 0);
    check("destination aspect keeps a stretched square", ok_dest && ok_view);
    return ok_dest && ok_view;
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PicoGL phase 2: one band, nearer triangle wins\n");
    printf("==========================================================\n");
    depth_band();
    door_cull_and_depth();
    queue_does_not_drop();
    screen_tri_matches();
    resume_matches();
    cover_keeps_a_hole();
    split_matches();
    display_aspect();
    printf("  checks=%d failures=%d\n", s_checks, s_fail);
    if (s_fail == 0) {
        printf("[CI-TEST] SUCCESS - the depth winner matches the software band\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the raster and the software band disagree\n");
    return 1;
}
