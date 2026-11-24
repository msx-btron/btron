/*
 * matrix_atlas.c — Zero-FPU libschrift Glyph Atlas & Palette LUT Builder
 *
 * Uses sft_init_mem() + sft_render_q16() (zero heap, zero float/double).
 * Pre-bakes 3x3 glow halo on desktop and 4-bit packed coverage on PicoCalc.
 *
 * Credits: Rezmason/matrix (MIT) & tomolt/libschrift (ISC).
 */

#include "matrix_internal.h"
#include "matrix_modes.h"
#include "matrix_ttf_embed.inc"
#include <string.h>

static int m_ci_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') { ca = (char)(ca + ('a' - 'A')); }
        if (cb >= 'A' && cb <= 'Z') { cb = (char)(cb + ('a' - 'A')); }
        if (ca != cb) { return 0; }
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

int matrix_parse_mode(const char *arg)
{
    if ((arg == NULL) || (arg[0] == '\0')) {
        return (int)MATRIX_MODE_CLASSIC;
    }
    while (*arg == ' ') { arg++; }
    if (*arg == '\0') {
        return (int)MATRIX_MODE_CLASSIC;
    }
    if (m_ci_eq(arg, "trinity")       || m_ci_eq(arg, "1")) { return (int)MATRIX_MODE_TRINITY; }
    if (m_ci_eq(arg, "classic")       || m_ci_eq(arg, "2")) { return (int)MATRIX_MODE_CLASSIC; }
    if (m_ci_eq(arg, "resurrections") || m_ci_eq(arg, "3")) { return (int)MATRIX_MODE_RESURRECTIONS; }
    if (m_ci_eq(arg, "operator")      || m_ci_eq(arg, "4")) { return (int)MATRIX_MODE_OPERATOR; }
    if (m_ci_eq(arg, "megacity")      || m_ci_eq(arg, "5")) { return (int)MATRIX_MODE_MEGACITY; }
    if (m_ci_eq(arg, "pride")         || m_ci_eq(arg, "6")) { return (int)MATRIX_MODE_PRIDE; }
    return -1;
}

const char *matrix_mode_name(int mode)
{
    if ((mode < 0) || (mode >= (int)MATRIX_MODE_COUNT)) {
        return "Classic";
    }
    return g_matrix_mode_specs[mode].label;
}

const MatrixModeSpec *matrix_get_mode_spec(MatrixMode mode)
{
    uint32_t idx = (uint32_t)mode;
    if (idx >= (uint32_t)MATRIX_MODE_COUNT) {
        idx = (uint32_t)MATRIX_MODE_CLASSIC;
    }
    return &g_matrix_mode_specs[idx];
}

const uint32_t *matrix_get_codepoints(uint8_t font_id, uint16_t *out_count)
{
    if (font_id == MATRIX_FONT_RESURRECTED) {
        *out_count = (uint16_t)(sizeof(g_matrix_res_cps) / sizeof(g_matrix_res_cps[0]));
        return g_matrix_res_cps;
    }
    *out_count = (uint16_t)(sizeof(g_matrix_code_cps) / sizeof(g_matrix_code_cps[0]));
    return g_matrix_code_cps;
}

static uint32_t pack_argb32(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)((((uint16_t)r & 0xF8u) << 8)
                          | (((uint16_t)g & 0xFCu) << 3)
                          | ((uint16_t)b >> 3));
#if defined(PICO_REAL_HW)
    /* ST7365P SPI DMA expects big-endian RGB565 directly in dp_pico_band_row() */
    return (uint16_t)((c >> 8) | (c << 8));
#else
    return c;
#endif
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, uint32_t t_q8)
{
    int32_t diff = (int32_t)b - (int32_t)a;
    int32_t val = (int32_t)a + ((diff * (int32_t)t_q8) >> 8);
    if (val < 0)   { return 0u; }
    if (val > 255) { return 255u; }
    return (uint8_t)val;
}

static void build_ramp_lut(MatrixRGB c0, MatrixRGB c1, MatrixRGB c2, MatrixRGB c3,
                           uint32_t *out32, uint16_t *out565)
{
    uint32_t i;
    for (i = 0u; i < 256u; i++) {
        uint8_t r, g, b;
        if (i < 64u) {
            uint32_t t = i << 2;
            r = lerp_u8(c0.r, c1.r, t);
            g = lerp_u8(c0.g, c1.g, t);
            b = lerp_u8(c0.b, c1.b, t);
        } else if (i < 192u) {
            uint32_t t = (i - 64u) << 1;
            r = lerp_u8(c1.r, c2.r, t);
            g = lerp_u8(c1.g, c2.g, t);
            b = lerp_u8(c1.b, c2.b, t);
        } else {
            uint32_t t = (i - 192u) << 2;
            r = lerp_u8(c2.r, c3.r, t);
            g = lerp_u8(c2.g, c3.g, t);
            b = lerp_u8(c2.b, c3.b, t);
        }
        if (out32 != NULL) {
            out32[i] = pack_argb32(r, g, b);
        }
        out565[i >> MATRIX_LUT565_SHIFT] = pack_rgb565(r, g, b);
    }
}

void matrix_atlas_build_luts(MatrixAtlas *atlas, MatrixMode mode)
{
    const MatrixModeSpec *spec = matrix_get_mode_spec(mode);
    MatrixRGB black = { 0u, 0u, 0u };
    MatrixRGB white = { 255u, 255u, 255u };
    uint32_t s;

#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    build_ramp_lut(spec->pal_bg, spec->pal_dim, spec->pal_mid, spec->pal_bright,
                   NULL, atlas->lut565);
    build_ramp_lut(black, spec->pal_mid, spec->cursor_rgb, white,
                   NULL, atlas->cursor_lut565);
    build_ramp_lut(black, spec->glint_rgb, spec->glint_rgb, white,
                   NULL, atlas->glint_lut565);
#else
    build_ramp_lut(spec->pal_bg, spec->pal_dim, spec->pal_mid, spec->pal_bright,
                   atlas->lut32, atlas->lut565);
    build_ramp_lut(black, spec->pal_mid, spec->cursor_rgb, white,
                   atlas->cursor_lut32, atlas->cursor_lut565);
    build_ramp_lut(black, spec->glint_rgb, spec->glint_rgb, white,
                   atlas->glint_lut32, atlas->glint_lut565);
#endif

    for (s = 0u; s < 6u; s++) {
        MatrixRGB base = g_pride_stripes[s];
        MatrixRGB dim;
        MatrixRGB bright;
        dim.r = (uint8_t)(base.r >> 2);
        dim.g = (uint8_t)(base.g >> 2);
        dim.b = (uint8_t)(base.b >> 2);
        bright.r = (uint8_t)((base.r + 255u) >> 1);
        bright.g = (uint8_t)((base.g + 255u) >> 1);
        bright.b = (uint8_t)((base.b + 255u) >> 1);
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
        build_ramp_lut(black, dim, base, bright, NULL, atlas->pride_lut565[s]);
#else
        build_ramp_lut(black, dim, base, bright,
                       atlas->pride_lut32[s], atlas->pride_lut565[s]);
#endif
    }
}

int matrix_atlas_init_fonts(MatrixAtlas *atlas)
{
    if (atlas == NULL) {
        return -1;
    }
    if (atlas->fonts_ready != 0u) {
        return 0;
    }
    if (sft_init_mem(&atlas->font_code, g_matrix_code_ttf, sizeof(g_matrix_code_ttf)) < 0) {
        return -1;
    }
    if (sft_init_mem(&atlas->font_res, g_matrix_resurrected_ttf, sizeof(g_matrix_resurrected_ttf)) < 0) {
        return -1;
    }
    atlas->fonts_ready = 1u;
    atlas->loaded_font_id = 0xFFu;
    return 0;
}

#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)
static void bake_glow_3x3(const uint8_t *src, uint8_t *dst, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            uint32_t sum = 0u;
            int dy;
            for (dy = -1; dy <= 1; dy++) {
                int sy = y + dy;
                if (sy < 0 || sy >= h) { continue; }
                {
                    const uint8_t *row = &src[sy * w];
                    int dx;
                    for (dx = -1; dx <= 1; dx++) {
                        int sx = x + dx;
                        if (sx >= 0 && sx < w) {
                            sum += (uint32_t)row[sx];
                        }
                    }
                }
            }
            /* Approximate sum / 8 clamped to 255 (fast shift, no division) */
            sum = (sum * 28u) >> 8;
            if (sum > 255u) { sum = 255u; }
            dst[(y * w) + x] = (uint8_t)sum;
        }
    }
}
#endif

int matrix_atlas_build(MatrixAtlas *atlas, MatrixMode mode, int cell_w, int cell_h)
{
    const MatrixModeSpec *spec = matrix_get_mode_spec(mode);
    const SFT_Font *font;
    const uint32_t *cps;
    uint16_t count = 0u;
    uint16_t i;
    int32_t x_scale_q16;
    int32_t y_scale_q16;
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    static uint8_t tmp_bmp[16 * 16];
    static uint8_t cell_buf[16 * 16];
    const int max_dim = 16;
#else
    static uint8_t tmp_bmp[32 * 32];
    static uint8_t cell_buf[32 * 32];
    const int max_dim = 32;
#endif

    if (matrix_atlas_init_fonts(atlas) < 0) {
        return -1;
    }
    matrix_atlas_build_luts(atlas, mode);

#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    cell_w = (int)MATRIX_PICO_CELL_W;
    cell_h = (int)MATRIX_PICO_CELL_H;
#else
    if (cell_w < 6)  { cell_w = 6; }
    if (cell_h < 8)  { cell_h = 8; }
    if (cell_w > (int)MATRIX_MAX_CELL_W) { cell_w = (int)MATRIX_MAX_CELL_W; }
    if (cell_h > (int)MATRIX_MAX_CELL_H) { cell_h = (int)MATRIX_MAX_CELL_H; }
#endif

    /* Skip re-rasterization if font & cell size already match */
    if ((atlas->loaded_font_id == spec->font_id) &&
        ((int)atlas->cell_w == cell_w) &&
        ((int)atlas->cell_h == cell_h)) {
        return 0;
    }

    font = (spec->font_id == MATRIX_FONT_RESURRECTED) ? &atlas->font_res : &atlas->font_code;
    cps  = matrix_get_codepoints(spec->font_id, &count);
    if (count > MATRIX_MAX_GLYPHS) {
        count = (uint16_t)MATRIX_MAX_GLYPHS;
    }

    /* Leave 1px margin inside cell for crisp glow & spacing */
    x_scale_q16 = (int32_t)((cell_w - 1) << 16);
    y_scale_q16 = (int32_t)((cell_h - 2) << 16);
    if (x_scale_q16 < (4 << 16)) { x_scale_q16 = 4 << 16; }
    if (y_scale_q16 < (6 << 16)) { y_scale_q16 = 6 << 16; }

    atlas->loaded_font_id = spec->font_id;
    atlas->glyph_count    = count;
    atlas->cell_w         = (uint8_t)cell_w;
    atlas->cell_h         = (uint8_t)cell_h;

    for (i = 0u; i < count; i++) {
        SFT_Glyph gid = 0u;
        SFT_GMetricsQ16 gm;
        int gw;
        int gh;
        int ox;
        int oy;
        int y;

        memset(cell_buf, 0, (size_t)(cell_w * cell_h));
        if (sft_lookup_font(font, cps[i], &gid) == 0 && gid != 0u &&
            sft_gmetrics_q16(font, gid, x_scale_q16, y_scale_q16, SFT_DOWNWARD_Y, &gm) == 0 &&
            gm.minWidth > 0 && gm.minHeight > 0) {
            SFT_Image img;
            gw = (int)gm.minWidth;
            gh = (int)gm.minHeight;
            if (gw > cell_w) { gw = cell_w; }
            if (gh > cell_h) { gh = cell_h; }
            if (gw > max_dim) { gw = max_dim; }
            if (gh > max_dim) { gh = max_dim; }

            img.pixels = tmp_bmp;
            img.width  = gw;
            img.height = gh;
            if (sft_render_q16(font, gid, x_scale_q16, y_scale_q16,
                               0, 0, SFT_DOWNWARD_Y, img, &atlas->scratch) == 0) {
                ox = (cell_w - gw) >> 1;
                oy = (cell_h - gh) >> 1;
                for (y = 0; y < gh; y++) {
                    memcpy(&cell_buf[((oy + y) * cell_w) + ox], &tmp_bmp[y * gw], (size_t)gw);
                }
            }
        }

#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
        {
            uint32_t stride_bytes = ((uint32_t)cell_w * (uint32_t)cell_h) >> 1;
            uint8_t *dst = &atlas->packed4[(uint32_t)i * stride_bytes];
            uint32_t p;
            uint32_t npix = (uint32_t)cell_w * (uint32_t)cell_h;
            for (p = 0u; p < npix; p += 2u) {
                uint8_t lo = (uint8_t)(cell_buf[p] >> 4);
                uint8_t hi = (uint8_t)(cell_buf[p + 1u] >> 4);
                dst[p >> 1] = (uint8_t)(lo | (hi << 4));
            }
        }
#else
        {
            uint8_t *dst_core = &atlas->core[(uint32_t)i * MATRIX_ATLAS_STRIDE];
            uint8_t *dst_glow = &atlas->glow[(uint32_t)i * MATRIX_ATLAS_STRIDE];
            memcpy(dst_core, cell_buf, (size_t)(cell_w * cell_h));
            bake_glow_3x3(dst_core, dst_glow, cell_w, cell_h);
        }
#endif
    }
    return 0;
}
