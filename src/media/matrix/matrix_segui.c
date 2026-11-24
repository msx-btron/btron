/*
 * matrix_segui.c — PicoCalc / SegUI SDK Demo Adapter for Matrix Screensaver
 *
 * Non-FPU optimal (100% integer / Q16.16 fixed-point, zero soft-float).
 * Writes big-endian RGB565 directly into dp_pico_band_row(y) ONLY for the
 * 16 scanlines belonging to the active DMA strip — zero intermediate band
 * buffer, zero redundant per-band re-rendering, full 320px width (40 cols).
 *
 * Credits: Rezmason/matrix (MIT) & tomolt/libschrift (ISC).
 */

#include "matrix_internal.h"
#include <btron/dp.h>
#include <string.h>

#if defined(PICO_REAL_HW)
extern uint16_t *dp_pico_band_row(int y);
#else
static uint16_t s_host_scanline[480];
#endif

static MatrixState s_segui_st;
static uint8_t     s_segui_inited    = 0u;
static MatrixMode  s_segui_want_mode = MATRIX_MODE_CLASSIC;

static void segui_ensure_init(int w, int h)
{
    if (w <= 0) { w = 320; }
    if (h <= 0) { h = 260; }
    if (s_segui_inited == 0u) {
        matrix_sim_init(&s_segui_st, s_segui_want_mode, w, h);
        s_segui_inited = 1u;
    } else if (s_segui_st.view_w != w || s_segui_st.view_h != h) {
        matrix_sim_resize(&s_segui_st, w, h);
    }
    if (s_segui_st.mode != s_segui_want_mode) {
        matrix_sim_set_mode(&s_segui_st, s_segui_want_mode);
    }
}

void matrix_segui_set_mode(int mode)
{
    if (mode >= 0 && mode < (int)MATRIX_MODE_COUNT) {
        s_segui_want_mode = (MatrixMode)mode;
        if (s_segui_inited != 0u) {
            matrix_sim_set_mode(&s_segui_st, s_segui_want_mode);
        }
    }
}

int matrix_segui_get_mode(void)
{
    return (int)s_segui_want_mode;
}

void matrix_segui_tick(void)
{
    if (s_segui_inited == 0u) {
        return;
    }
    matrix_sim_step(&s_segui_st, 33u);
}

int matrix_segui_key(int key)
{
    if (key >= '1' && key <= '6') {
        matrix_segui_set_mode(key - '1');
        return 1;
    }
    if (key == ' ') {
        if (s_segui_inited != 0u) {
            s_segui_st.paused = (s_segui_st.paused == 0u) ? 1u : 0u;
        }
        return 1;
    }
    /* Palm / PicoCalc arrow keycodes: PALM_KEY_LEFT=0xB4, PALM_KEY_RIGHT=0xB7 */
    if (key == 0xB4 || key == 0x1102 || key == 'h' || key == 'H') {
        int prev = ((int)s_segui_want_mode + (int)MATRIX_MODE_COUNT - 1) % (int)MATRIX_MODE_COUNT;
        matrix_segui_set_mode(prev);
        return 1;
    }
    if (key == 0xB7 || key == 0x1103 || key == 'l' || key == 'L' || key == '\t') {
        int next = ((int)s_segui_want_mode + 1) % (int)MATRIX_MODE_COUNT;
        matrix_segui_set_mode(next);
        return 1;
    }
    if (key == '\r' || key == '\n') {
        if (s_segui_inited != 0u) {
            matrix_sim_trigger_ripple(&s_segui_st, s_segui_st.view_w >> 1, s_segui_st.view_h >> 1);
        }
        return 1;
    }
    return 0;
}

#if !defined(PICO_REAL_HW)
static uint32_t rgb565_to_argb32(uint16_t c)
{
    uint32_t r = ((uint32_t)(c >> 11) & 0x1Fu);
    uint32_t g = ((uint32_t)(c >> 5)  & 0x3Fu);
    uint32_t b = ((uint32_t)c         & 0x1Fu);
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
#endif

void matrix_segui_paint_rect(void *gdev_ptr, int left, int top, int right, int bottom)
{
    int w = right - left;
    int h = bottom - top;
    int y;

    if (w <= 0 || h <= 0) {
        return;
    }
    segui_ensure_init(w, h);

#if defined(PICO_REAL_HW)
    (void)gdev_ptr;
    if (left < 0) { left = 0; }
    if (right > 320) { right = 320; }
    w = right - left;
    if (w <= 0) { return; }

    /* Render ONLY scanlines inside the active 16px DMA strip (zero redundant work!) */
    for (y = top; y < bottom; y++) {
        uint16_t *row = dp_pico_band_row(y);
        if (row == NULL) {
            continue;
        }
        matrix_render_rgb565_scanline(&s_segui_st, y - top, &row[left], w);
    }
#else
    {
        GDEV *dev = (GDEV *)gdev_ptr;
        if (dev == NULL || dev->pixels == NULL) {
            return;
        }
        if (w > 480) { w = 480; }
        for (y = top; y < bottom; y++) {
            int x;
            if (y < 0 || y >= (int)dev->height) { continue; }
            matrix_render_rgb565_scanline(&s_segui_st, y - top, s_host_scanline, w);
            for (x = 0; x < w; x++) {
                int px = left + x;
                if (px >= 0 && px < (int)dev->width) {
                    dev->pixels[(y * (int)dev->width) + px] = rgb565_to_argb32(s_host_scanline[x]);
                }
            }
        }
    }
#endif
}
