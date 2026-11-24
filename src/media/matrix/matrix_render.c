/*
 * matrix_render.c — Zero-FPU Integer BitBlit Renderer (ARGB32 + RGB565)
 *
 * Features:
 *   - Dirty-cell culling on 2D grid modes (skips unchanged & black cells)
 *   - Branchless 256-entry palette LUT lookup (zero float, zero inner division)
 *   - Pre-baked 3x3 glow halo modulation on desktop
 *   - Volumetric 3-plane parallax renderer for Trinity mode
 *   - Row-band RGB565 streaming for PicoCalc ST7365P LCD
 */

#include "matrix_internal.h"
#include <string.h>

static uint32_t m_pride_band(int row, int total_rows)
{
    uint32_t b;
    if (total_rows <= 0) { return 0u; }
    b = (uint32_t)((row * 6) / total_rows);
    return (b < 6u) ? b : 5u;
}

#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)

static void blit_cell_argb32(const MatrixAtlas *atlas, const MatrixCell *cell,
                             const uint32_t *pal_lut, uint8_t bloom_on,
                             uint32_t *dst_base, int dst_stride)
{
    int cw = (int)atlas->cell_w;
    int ch = (int)atlas->cell_h;
    uint32_t g_off = (uint32_t)cell->glyph * MATRIX_ATLAS_STRIDE;
    const uint8_t *core = &atlas->core[g_off];
    const uint8_t *glow = &atlas->glow[g_off];
    uint32_t bright = (uint32_t)cell->bright;
    uint32_t glow_amt = (bloom_on != 0u) ? ((bright * 110u) >> 8) : 0u;
    const uint32_t *lut = pal_lut;
    int y;

    if (cell->cursor > 128u) {
        lut = atlas->cursor_lut32;
        bright = 255u;
        glow_amt = (bloom_on != 0u) ? 180u : 0u;
    } else if (cell->glint > 120u) {
        lut = atlas->glint_lut32;
        bright = 240u;
    }

    for (y = 0; y < ch; y++) {
        uint32_t *dst = &dst_base[y * dst_stride];
        const uint8_t *c_row = &core[y * cw];
        const uint8_t *g_row = &glow[y * cw];
        int x;
        for (x = 0; x < cw; x++) {
            uint32_t idx = (((uint32_t)c_row[x] * bright) + ((uint32_t)g_row[x] * glow_amt)) >> 8;
            if (idx > 255u) { idx = 255u; }
            dst[x] = lut[idx];
        }
    }
}

static void render_volumetric_argb32(const MatrixState *st, uint32_t *pixels,
                                     int width, int height)
{
    const MatrixAtlas *atlas = &st->atlas;
    int cw = (int)atlas->cell_w;
    int ch = (int)atlas->cell_h;
    uint32_t plane;
    uint32_t i;
    uint32_t npix = (uint32_t)width * (uint32_t)height;

    /* Clear to black (0xFF000000) */
    for (i = 0u; i < npix; i++) {
        pixels[i] = 0xFF000000u;
    }

    /* Draw back-to-front across 3 depth planes (far -> mid -> near) */
    for (plane = 0u; plane < MATRIX_VOL_PLANES; plane++) {
        int scale_num = (plane == 0u) ? 2 : ((plane == 1u) ? 3 : 4); /* /4 scale */
        int dw = (cw * scale_num) >> 2;
        int dh = (ch * scale_num) >> 2;
        uint32_t plane_bright = (plane == 0u) ? 110u : ((plane == 1u) ? 185u : 255u);
        if (dw < 4) { dw = 4; }
        if (dh < 6) { dh = 6; }

        for (i = 0u; i < MATRIX_VOL_DROPS; i++) {
            const MatrixVolDrop *d = &st->vol_drops[i];
            int gx;
            int head_y;
            int k;
            if (d->plane != plane) { continue; }
            gx = d->x_q16 >> 16;
            head_y = d->y_q16 >> 16;

            for (k = 0; k < (int)d->length; k++) {
                int gy = head_y - (k * dh);
                uint32_t tail_fade;
                uint32_t b;
                uint32_t g_idx;
                const uint8_t *core;
                const uint32_t *lut;
                int y;

                if (gy >= height || (gy + dh) <= 0 || gx >= width || (gx + dw) <= 0) {
                    continue;
                }
                tail_fade = (uint32_t)((((int)d->length - k) * 255) / (int)d->length);
                b = (plane_bright * tail_fade) >> 8;
                g_idx = ((uint32_t)d->seed + (uint32_t)(k * 13) + ((st->frame_count >> 3) & 3u)) % atlas->glyph_count;
                core = &atlas->core[g_idx * MATRIX_ATLAS_STRIDE];

                if (k == 0) {
                    lut = atlas->cursor_lut32;
                    b = plane_bright;
                } else if (((d->seed + (uint32_t)k) & 15u) == 0u && plane == 2u) {
                    lut = atlas->glint_lut32;
                } else {
                    lut = atlas->lut32;
                }

                for (y = 0; y < dh; y++) {
                    int py = gy + y;
                    int sy;
                    uint32_t *dst;
                    const uint8_t *src;
                    int x;
                    if (py < 0 || py >= height) { continue; }
                    sy = (y * ch) / dh;
                    dst = &pixels[py * width];
                    src = &core[sy * cw];
                    for (x = 0; x < dw; x++) {
                        int px = gx + x;
                        int sx;
                        uint32_t idx;
                        if (px < 0 || px >= width) { continue; }
                        sx = (x * cw) / dw;
                        idx = ((uint32_t)src[sx] * b) >> 8;
                        if (idx > 8u) {
                            dst[px] = lut[idx];
                        }
                    }
                }
            }
        }
    }
}

void matrix_render_argb32(MatrixState *st, uint32_t *pixels, int width, int height)
{
    const MatrixModeSpec *spec;
    int cw;
    int ch;
    uint16_t r;
    uint16_t c;

    if ((st == NULL) || (pixels == NULL) || (width <= 0) || (height <= 0)) {
        return;
    }
    spec = matrix_get_mode_spec(st->mode);
    if ((spec->flags & MAT_FLAG_VOLUMETRIC) != 0u) {
        render_volumetric_argb32(st, pixels, width, height);
        st->full_redraw = 0u;
        return;
    }

    if (st->full_redraw != 0u) {
        uint32_t i;
        uint32_t npix = (uint32_t)width * (uint32_t)height;
        for (i = 0u; i < npix; i++) {
            pixels[i] = 0xFF000000u;
        }
    }

    cw = (int)st->atlas.cell_w;
    ch = (int)st->atlas.cell_h;

    for (r = 0u; r < st->rows; r++) {
        const uint32_t *row_lut = ((spec->flags & MAT_FLAG_PRIDE) != 0u)
            ? st->atlas.pride_lut32[m_pride_band((int)r, (int)st->rows)]
            : st->atlas.lut32;
        MatrixCell *row_cells = &st->cells[(uint32_t)r * st->cols];
        int py = (int)r * ch;
        if ((py + ch) > height) { break; }

        for (c = 0u; c < st->cols; c++) {
            MatrixCell *cell = &row_cells[c];
            int px = (int)c * cw;
            /* Exact 32-bit state key (4 x uint8_t) for bit-exact dirty-cell culling */
            uint32_t key = ((uint32_t)cell->glyph  << 24)
                         | ((uint32_t)cell->bright << 16)
                         | ((uint32_t)cell->cursor << 8)
                         |  (uint32_t)cell->glint;
            if ((px + cw) > width) { break; }
            if ((st->full_redraw == 0u) && (key == cell->prev_key)) {
                continue;
            }
            cell->prev_key = key;
            blit_cell_argb32(&st->atlas, cell, row_lut, st->bloom_on,
                             &pixels[(py * width) + px], width);
        }
    }
    st->full_redraw = 0u;
}

#endif /* !PICO_REAL_HW && !MATRIX_PICO_PROFILE */

/* ── Zero-FPU Single-Scanline RGB565 Renderer (Direct dp_pico_band_row) ─── */

void matrix_render_rgb565_scanline(const MatrixState *st, int rel_y,
                                   uint16_t *dst_x0, int width)
{
    const MatrixModeSpec *spec;
    const MatrixAtlas *atlas;
    int cw;
    int ch;
    int cell_row;
    int glyph_y;
    const uint16_t *row_lut;
    const MatrixCell *row_cells;
    uint16_t c;

    if ((st == NULL) || (dst_x0 == NULL) || (width <= 0) || (rel_y < 0)) {
        return;
    }
    /* Fast zero-fill for the active scanline (bg = black) */
    memset(dst_x0, 0, (size_t)width * sizeof(uint16_t));

    atlas = &st->atlas;
    cw = (int)atlas->cell_w;
    ch = (int)atlas->cell_h;
    if (cw <= 0 || ch <= 0) {
        return;
    }

    /* Fast integer row/subrow lookup without runtime division on 12px cells */
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    /* ch == 12: reciprocal multiply (rel_y * 2731) >> 15 == rel_y / 12 for rel_y < 384 */
    cell_row = (rel_y * 2731) >> 15;
    glyph_y  = rel_y - (cell_row * 12);
#else
    cell_row = rel_y / ch;
    glyph_y  = rel_y - (cell_row * ch);
#endif
    if (cell_row < 0 || cell_row >= (int)st->rows) {
        return;
    }

    spec = matrix_get_mode_spec(st->mode);
    row_lut = ((spec->flags & MAT_FLAG_PRIDE) != 0u)
        ? atlas->pride_lut565[m_pride_band(cell_row, (int)st->rows)]
        : atlas->lut565;
    row_cells = &st->cells[(uint32_t)cell_row * st->cols];

    for (c = 0u; c < st->cols; c++) {
        const MatrixCell *cell = &row_cells[c];
        int px = (int)c * cw;
        uint32_t bright = (uint32_t)cell->bright;
        const uint16_t *lut = row_lut;
        uint16_t *dst;

        if ((px + cw) > width) { break; }
        if (bright == 0u && cell->cursor == 0u) { continue; }
        if (cell->cursor > 128u) {
            lut = atlas->cursor_lut565;
            bright = 255u;
        } else if (cell->glint > 120u) {
            lut = atlas->glint_lut565;
            bright = 240u;
        }
        dst = &dst_x0[px];

#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
        /* cw == 8 (4 packed bytes/row), hoisted b_scale, zero-skip 2px at a time */
        {
            uint32_t b_scale = (bright * 17u) >> MATRIX_LUT565_SHIFT;
            const uint8_t *gptr = &atlas->packed4[((uint32_t)cell->glyph * 48u) + ((uint32_t)glyph_y << 2)];
            uint32_t b;
            for (b = 0u; b < 4u; b++) {
                uint32_t p = (uint32_t)gptr[b];
                if (p != 0u) {
                    uint32_t p0 = p & 0x0Fu;
                    uint32_t p1 = p >> 4;
                    if (p0 != 0u) { dst[b << 1]        = lut[(p0 * b_scale) >> 8]; }
                    if (p1 != 0u) { dst[(b << 1) + 1u] = lut[(p1 * b_scale) >> 8]; }
                }
            }
        }
#else
        {
            const uint8_t *c_row = &atlas->core[((uint32_t)cell->glyph * MATRIX_ATLAS_STRIDE)
                                                + (uint32_t)(glyph_y * cw)];
            int x;
            for (x = 0; x < cw; x++) {
                uint32_t a = (uint32_t)c_row[x];
                if (a != 0u) {
                    dst[x] = lut[((a * bright) >> 8) >> MATRIX_LUT565_SHIFT];
                }
            }
        }
#endif
    }
}
