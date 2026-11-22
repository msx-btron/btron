/*
 * dp_pico.c — Banded 320x16 RGB565 Display Primitives (GDEV) for PicoCalc
 *
 * Uses double-buffered 320x16 big-endian RGB565 strips (20 KiB total SRAM)
 * overlapped with SPI1 DMA Channel 0 transfers (st7365p_flush_band_dma).
 * All multilingual text (Japanese JIS, Tibetan, Cyrillic, Sanskrit, ASCII)
 * is rendered by the real B-System /fonts/ engine (troncode.c + jis_fonts.c
 * + tibetan_fonts.c) via dp_pico_put_pixel().
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/dp.h>
#include <btron/troncode.h>

extern void st7365p_flush_band_dma(uint16_t y0, uint16_t h, const uint16_t *rgb565_be);
extern void st7365p_dma_wait(void);

#define DP_W            320
#define DP_H            320
#define DP_BAND_H       16
#define DP_BAND_COUNT   (DP_H / DP_BAND_H)

/* Double-buffered 320x16 RGB565 (big-endian) strips = 20,480 bytes SRAM */
static uint16_t s_strip[2][DP_W * DP_BAND_H] __attribute__((aligned(4)));
static int      s_active_buf = 0;
static int      s_band_y0    = 0;
static uint32_t s_present_gen = 1;
/* Runs after the last band releases CS. The gears frame uses it to join
 * the back buffer. It must not start another present. */
static void (*s_frame_tail)(void);

void dp_pico_set_frame_tail(void (*fn)(void))
{
    s_frame_tail = fn;
}

uint32_t dp_pico_present_gen(void)
{
    return s_present_gen;
}

/* Convert 0xAARRGGBB -> big-endian RGB565 (byte-swapped for 8-bit SPI1 DMA) */
uint16_t dp_pico_color565(COLOR c)
{
    uint16_t r = (uint16_t)((c >> 19) & 0x1Fu);
    uint16_t g = (uint16_t)((c >> 10) & 0x3Fu);
    uint16_t b = (uint16_t)((c >>  3) & 0x1Fu);
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((v >> 8) | (v << 8));
}

/* Current DMA strip. Callers write big-endian RGB565. */
uint16_t *dp_pico_band_ptr(int *y0, int *h, int *stride)
{
    if (y0) *y0 = s_band_y0;
    if (h) *h = DP_BAND_H;
    if (stride) *stride = DP_W;
    return s_strip[s_active_buf];
}

/* Current band row, big-endian RGB565, or NULL when y is outside the band. */
uint16_t *dp_pico_band_row(int y)
{
    int ry = y - s_band_y0;
    if (ry < 0 || ry >= DP_BAND_H) return NULL;
    return s_strip[s_active_buf] + ry * DP_W;
}

void dp_pico_put_pixel(H x, H y, uint16_t rgb565_be)
{
    int ry = (int)y - s_band_y0;
    if (x >= 0 && x < DP_W && ry >= 0 && ry < DP_BAND_H) {
        s_strip[s_active_buf][ry * DP_W + x] = rgb565_be;
    }
}

ER fill_rec(GDEV *dev, const RECT *r, COLOR col)
{
    int x0, y0, x1, y1, y, x;
    uint16_t px;
    uint16_t *buf;
    if (!dev || !r) return -1;

    x0 = r->left;   y0 = r->top;
    x1 = r->right;  y1 = r->bottom;
    if (x0 < dev->clip.left)   x0 = dev->clip.left;
    if (y0 < dev->clip.top)    y0 = dev->clip.top;
    if (x1 > dev->clip.right)  x1 = dev->clip.right;
    if (y1 > dev->clip.bottom) y1 = dev->clip.bottom;
    if (x0 >= x1 || y0 >= y1) return 0;

    px  = dp_pico_color565(col);
    buf = s_strip[s_active_buf];
    for (y = y0; y < y1; y++) {
        uint16_t *row = buf + (y - s_band_y0) * DP_W;
        for (x = x0; x < x1; x++) {
            row[x] = px;
        }
    }
    return 0;
}

void dp_pico_init_gdev(GDEV *dev)
{
    dev->width   = DP_W;
    dev->height  = DP_H;
    dev->is_vram = 1;
    dev->pixels  = NULL;
    dev->cap_w   = DP_W;
    dev->cap_h   = DP_H;
    dev->fg_fb   = COLOR_BLACK;
    dev->clip    = (RECT){ 0, 0, DP_W, DP_H };
}

void dp_pico_blit_rgba_2x(const RECT *dst_rect, const uint32_t *src, int src_w, int src_h)
{
    int x0, y0, x1, y1, by0, by1, y, x, dst_w, dst_h;
    uint32_t step_x, step_y, sy_fp, sx_start;
    uint16_t *strip;
    if (!dst_rect || !src || src_w <= 0 || src_h <= 0) return;

    x0 = dst_rect->left;  if (x0 < 0) x0 = 0;
    x1 = dst_rect->right; if (x1 > DP_W) x1 = DP_W;
    y0 = dst_rect->top;   if (y0 < 0) y0 = 0;
    y1 = dst_rect->bottom;if (y1 > DP_H) y1 = DP_H;
    if (x0 >= x1 || y0 >= y1) return;

    by0 = y0 > s_band_y0 ? y0 : s_band_y0;
    by1 = y1 < (s_band_y0 + DP_BAND_H) ? y1 : (s_band_y0 + DP_BAND_H);
    if (by0 >= by1) return;

    dst_w = dst_rect->right - dst_rect->left;
    dst_h = dst_rect->bottom - dst_rect->top;
    if (dst_w <= 0 || dst_h <= 0) return;

    /* Q16.16 fixed-point DDA: ZERO divisions in the inner pixel loop! */
    step_x   = ((uint32_t)src_w << 16) / (uint32_t)dst_w;
    step_y   = ((uint32_t)src_h << 16) / (uint32_t)dst_h;
    sx_start = (uint32_t)(x0 - dst_rect->left) * step_x;
    sy_fp    = (uint32_t)(by0 - dst_rect->top) * step_y;

    strip = s_strip[s_active_buf];
    for (y = by0; y < by1; y++) {
        const uint32_t *srow = src + (sy_fp >> 16) * src_w;
        uint16_t *drow = strip + (y - s_band_y0) * DP_W;
        uint32_t sx_fp = sx_start;
        for (x = x0; x < x1; x++) {
            drow[x] = dp_pico_color565((COLOR)srow[sx_fp >> 16]);
            sx_fp += step_x;
        }
        sy_fp += step_y;
    }
}

/* Native RGB565 (r<<11|g<<5|b) into the strip. The panel DMA is byte-swapped.
 * The DDA matches dp_pico_blit_rgba_2x. */
void dp_pico_blit_rgb565(const RECT *dst_rect, const uint16_t *src, int src_w, int src_h)
{
    int x0, y0, x1, y1, by0, by1, y, x, dst_w, dst_h;
    uint32_t step_x, step_y, sy_fp, sx_start;
    uint16_t *strip;
    if (!dst_rect || !src || src_w <= 0 || src_h <= 0) return;

    x0 = dst_rect->left;  if (x0 < 0) x0 = 0;
    x1 = dst_rect->right; if (x1 > DP_W) x1 = DP_W;
    y0 = dst_rect->top;   if (y0 < 0) y0 = 0;
    y1 = dst_rect->bottom;if (y1 > DP_H) y1 = DP_H;
    if (x0 >= x1 || y0 >= y1) return;

    by0 = y0 > s_band_y0 ? y0 : s_band_y0;
    by1 = y1 < (s_band_y0 + DP_BAND_H) ? y1 : (s_band_y0 + DP_BAND_H);
    if (by0 >= by1) return;

    dst_w = dst_rect->right - dst_rect->left;
    dst_h = dst_rect->bottom - dst_rect->top;
    if (dst_w <= 0 || dst_h <= 0) return;

    step_x   = ((uint32_t)src_w << 16) / (uint32_t)dst_w;
    step_y   = ((uint32_t)src_h << 16) / (uint32_t)dst_h;
    sx_start = (uint32_t)(x0 - dst_rect->left) * step_x;
    sy_fp    = (uint32_t)(by0 - dst_rect->top) * step_y;

    strip = s_strip[s_active_buf];
    for (y = by0; y < by1; y++) {
        const uint16_t *srow = src + (sy_fp >> 16) * src_w;
        uint16_t *drow = strip + (y - s_band_y0) * DP_W;
        uint32_t sx_fp = sx_start;
        for (x = x0; x < x1; x++) {
            uint16_t c = srow[sx_fp >> 16];
            drow[x] = (uint16_t)((c >> 8) | (c << 8));
            sx_fp += step_x;
        }
        sy_fp += step_y;
    }
}

/* Render dirty 16-line bands via ping-pong SPI1 DMA (0 SPI traffic if mask==0) */
void dp_pico_present(GDEV *dev, uint32_t dirty_mask, void (*paint_fn)(GDEV *))
{
    if (!dev || !paint_fn || dirty_mask == 0u) return;

    s_present_gen++;
    if (s_present_gen == 0u) s_present_gen = 1u;

    for (int b = 0; b < DP_BAND_COUNT; b++) {
        if (!(dirty_mask & (1UL << b))) continue;
        s_active_buf = b & 1;
        s_band_y0    = b * DP_BAND_H;
        dev->clip    = (RECT){ 0, (H)s_band_y0, DP_W, (H)(s_band_y0 + DP_BAND_H) };
        paint_fn(dev);
        st7365p_flush_band_dma((uint16_t)s_band_y0, DP_BAND_H, s_strip[s_active_buf]);
    }
    st7365p_dma_wait();
    dev->clip = (RECT){ 0, 0, DP_W, DP_H };
    if (s_frame_tail) s_frame_tail();
}
