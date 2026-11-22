/*
 * test_picogl_bin.c — Phase 1 host gate. One fan is transformed and binned.
 * The pixels are checked by test_picogl_raster. This file still checks the bins.
 */
#include <stdio.h>
#include <string.h>

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

static int bands_used(void)
{
    int b, n = 0;
    for (b = 0; b < PICOGL_BANDS; b++)
        if (picogl_band_n(b) > 0) n++;
    return n;
}

static int axis_min(int a, int b, int c)
{
    int m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

static int axis_max(int a, int b, int c)
{
    int m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

static int tri_covers_center(void)
{
    int b;
    for (b = 0; b < PICOGL_BANDS; b++) {
        const picogl_tri_t *t;
        int xmin, xmax, ymin, ymax;
        if (picogl_band_n(b) < 1) continue;
        t = picogl_band_tri(b, 0);
        if (!t) return 0;
        /* Screen center of a 320 viewport is (160, 160). The fan sits on +X
         * in front of a yaw-0 camera, so its box has to contain that point. */
        xmin = axis_min(t->x0, t->x1, t->x2);
        xmax = axis_max(t->x0, t->x1, t->x2);
        ymin = axis_min(t->y0, t->y1, t->y2);
        ymax = axis_max(t->y0, t->y1, t->y2);
        if (ymin < 160 && ymax > 160 && xmin < 160 && xmax > 160)
            return 1;
    }
    return 0;
}

int main(void)
{
    float org[3] = {0.0f, 0.0f, 0.0f};
    float ang[3] = {0.0f, 0.0f, 0.0f};
    float front[9] = {
        64.0f, -8.0f, -8.0f,
        64.0f,  8.0f, -8.0f,
        64.0f,  0.0f,  8.0f
    };
    float st[6] = {4.0f, 6.0f, 16.0f, 0.0f, 8.0f, 16.0f};
    float behind[9] = {
        -32.0f, -4.0f, -4.0f,
        -32.0f,  4.0f, -4.0f,
        -32.0f,  0.0f,  4.0f
    };
    const uint8_t font[96][8] = {{0}};
    const picogl_tri_t *t;
    int i, b, full;

    printf("==========================================================\n");
    printf(" PicoGL phase 1: one fan into the band bins\n");
    printf("==========================================================\n");

    picogl_begin_frame(320, 320);
    picogl_set_camera(org, ang, 320, 320);
    picogl_poly(front, st, 3, 0xFF0000FFu, 3);

    check("the fan is not dropped", picogl_drop() == 0);
    check("the fan lands in at least one band", bands_used() > 0);
    check("the fan covers the screen center", tri_covers_center());

    t = NULL;
    for (b = 0; b < PICOGL_BANDS && !t; b++)
        if (picogl_band_n(b) > 0) t = picogl_band_tri(b, 0);
    check("the binned triangle keeps the red shade", t && t->rgb == 0xF800);
    check("the binned triangle keeps the texture index", t && t->tex == 3 && (t->flags & PICOGL_TEX));
    check("the binned triangle keeps the first texel", t && t->u == 4 && t->v == 6);

    picogl_begin_frame(320, 320);
    picogl_set_camera(org, ang, 320, 320);
    picogl_poly(behind, NULL, 3, 0xFF00FF00u, -1);
    check("a face behind the camera is not binned", bands_used() == 0 && picogl_drop() == 0);

    picogl_begin_frame(320, 320);
    picogl_set_camera(org, ang, 320, 320);
    for (i = 0; i < PICOGL_BAND_CAP + 1; i++)
        picogl_poly(front, NULL, 3, 0xFFFFFFFFu, -1);
    full = 1;
    for (b = 0; b < PICOGL_BANDS; b++) {
        int n = picogl_band_n(b);
        if (n != 0 && n != PICOGL_BAND_CAP) full = 0;
    }
    check("a band stops at the cap and the rest count as drops",
          picogl_drop() > 0 && full);

    picogl_fill(8, 8, 40, 12, 0xFF00FF00u);
    picogl_fill(0, 0, 0, 10, 0);
    picogl_string(4, 4, "Introduction", font);
    check("HUD fill and string are recorded and do not become triangles",
          picogl_fill_n() == 1 && picogl_glyph_n() == 12);

    printf("  checks=%d failures=%d drop=%d\n", s_checks, s_fail, picogl_drop());
    if (s_fail == 0) {
        printf("[CI-TEST] SUCCESS - the fan matches the bins\n");
        return 0;
    }
    if (t) printf("  tri (%d,%d) (%d,%d) (%d,%d) rgb=%04x\n",
                  t->x0, t->y0, t->x1, t->y1, t->x2, t->y2, t->rgb);
    printf("[CI-TEST] FAIL - the fan and the bins disagree\n");
    return 1;
}
