/*
 * matrix_internal.h — Bounded NASA JPL Rule 3 data structures for Matrix
 *
 * 100% integer / Q16.16 fixed-point simulation & blitting (zero FPU, zero heap).
 */

#ifndef BTRON_MATRIX_INTERNAL_H
#define BTRON_MATRIX_INTERNAL_H

#include "matrix.h"
#include <btron/schrift.h>
#include <stddef.h>
#include <stdint.h>

#define MATRIX_FONT_CODE         0u
#define MATRIX_FONT_RESURRECTED  1u

#define MAT_FLAG_VOLUMETRIC      (1u << 0)
#define MAT_FLAG_BOX_RIPPLE      (1u << 1)
#define MAT_FLAG_PRIDE           (1u << 2)
#define MAT_FLAG_GLINT           (1u << 3)
#define MAT_FLAG_BLOOM           (1u << 4)

#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
#define MATRIX_MAX_COLS          40u     /* 40 * 8 = 320 px full width */
#define MATRIX_MAX_ROWS          27u     /* 27 * 12 = 324 px full height */
#define MATRIX_MAX_GLYPHS        64u
#define MATRIX_PICO_CELL_W       8u
#define MATRIX_PICO_CELL_H       12u
#define MATRIX_ATLAS_BYTES       (MATRIX_MAX_GLYPHS * ((MATRIX_PICO_CELL_W * MATRIX_PICO_CELL_H) / 2u))
#define MATRIX_VOL_PLANES        2u
#define MATRIX_VOL_DROPS         16u
#define MATRIX_LUT565_LEN        32u
#define MATRIX_LUT565_SHIFT      3u
#else
#define MATRIX_MAX_COLS          128u
#define MATRIX_MAX_ROWS          72u
#define MATRIX_MAX_GLYPHS        138u
#define MATRIX_MAX_CELL_W        24u
#define MATRIX_MAX_CELL_H        32u
#define MATRIX_ATLAS_STRIDE      (MATRIX_MAX_CELL_W * MATRIX_MAX_CELL_H)
#define MATRIX_VOL_PLANES        3u
#define MATRIX_VOL_DROPS         96u
#define MATRIX_LUT565_LEN        256u
#define MATRIX_LUT565_SHIFT      0u
#endif

typedef struct MatrixRGB
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} MatrixRGB;

typedef struct MatrixModeSpec
{
    const char *id;
    const char *label;
    uint8_t     font_id;
    uint8_t     num_cols_ref;
    uint8_t     flags;
    uint8_t     base_bright;      /* 0..255 ambient floor (operator = 38) */
    uint16_t    fall_speed_q8;    /* rows/sec in Q8 */
    uint16_t    cycle_speed_q8;   /* mutations/sec scale */
    uint16_t    decay_rate_q8;    /* brightness decay/sec in Q8 */
    uint8_t     drop_len_min;
    uint8_t     drop_len_max;
    MatrixRGB   pal_bg;
    MatrixRGB   pal_dim;
    MatrixRGB   pal_mid;
    MatrixRGB   pal_bright;
    MatrixRGB   cursor_rgb;
    MatrixRGB   glint_rgb;
} MatrixModeSpec;

typedef struct MatrixCell
{
    uint8_t  glyph;
    uint8_t  bright;
    uint8_t  cursor;
    uint8_t  glint;
#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)
    uint32_t prev_key;
#endif
} MatrixCell;

typedef struct MatrixCol
{
    int32_t  head_y_q16;
    int32_t  speed_q16;
    int16_t  delay_ms;
    uint8_t  length;
    uint8_t  active;
} MatrixCol;

typedef struct MatrixVolDrop
{
    int32_t  x_q16;
    int32_t  y_q16;
    int32_t  vy_q16;
    uint8_t  plane;     /* 0 = far, 1 = mid, 2 = near */
    uint8_t  length;
    uint8_t  seed;
    uint8_t  active;
} MatrixVolDrop;

typedef struct MatrixAtlas
{
    SFT_Font font_code;
    SFT_Font font_res;
    uint8_t  fonts_ready;
    uint8_t  loaded_font_id;
    uint16_t glyph_count;
    uint8_t  cell_w;
    uint8_t  cell_h;
    uint8_t  bloom_enabled;
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    /* 4-bit packed coverage (2 px/byte) -> 3.0 KiB SRAM */
    uint8_t  packed4[MATRIX_ATLAS_BYTES];
    uint16_t lut565[MATRIX_LUT565_LEN];
    uint16_t cursor_lut565[MATRIX_LUT565_LEN];
    uint16_t glint_lut565[MATRIX_LUT565_LEN];
    uint16_t pride_lut565[6][MATRIX_LUT565_LEN];
#else
    /* 8-bit core + 8-bit pre-blurred 3x3 glow halo */
    uint8_t  core[MATRIX_MAX_GLYPHS * MATRIX_ATLAS_STRIDE];
    uint8_t  glow[MATRIX_MAX_GLYPHS * MATRIX_ATLAS_STRIDE];
    uint32_t lut32[256];
    uint32_t cursor_lut32[256];
    uint32_t glint_lut32[256];
    uint32_t pride_lut32[6][256];
    uint16_t lut565[256];
    uint16_t cursor_lut565[256];
    uint16_t glint_lut565[256];
    uint16_t pride_lut565[6][256];
#endif
    SFT_Scratch scratch;
} MatrixAtlas;

typedef struct MatrixState
{
    MatrixMode    mode;
    uint32_t      rng;
    uint32_t      frame_count;
    uint32_t      elapsed_ms;
    int           view_w;
    int           view_h;
    int           font_px_override;
    uint16_t      cols;
    uint16_t      rows;
    uint8_t       paused;
    uint8_t       bloom_on;
    uint8_t       full_redraw;
    /* Chebyshev square ripple state (Operator mode) */
    int16_t       ripple_cx;
    int16_t       ripple_cy;
    int32_t       ripple_rad_q16;
    uint8_t       ripple_active;
    /* Grid & columns */
    MatrixCol     col_state[MATRIX_MAX_COLS];
    MatrixCell    cells[MATRIX_MAX_ROWS * MATRIX_MAX_COLS];
    /* Volumetric drops (Trinity mode) */
    MatrixVolDrop vol_drops[MATRIX_VOL_DROPS];
    /* Pre-rasterized glyph atlas & LUTs */
    MatrixAtlas   atlas;
} MatrixState;

/* Mode specs (matrix_modes.h) */
const MatrixModeSpec *matrix_get_mode_spec(MatrixMode mode);
const uint32_t       *matrix_get_codepoints(uint8_t font_id, uint16_t *out_count);

/* Atlas & LUT builder (matrix_atlas.c) */
int  matrix_atlas_init_fonts(MatrixAtlas *atlas);
int  matrix_atlas_build(MatrixAtlas *atlas, MatrixMode mode, int cell_w, int cell_h);
void matrix_atlas_build_luts(MatrixAtlas *atlas, MatrixMode mode);

/* Fixed-point simulation (matrix_sim.c) */
void matrix_sim_init(MatrixState *st, MatrixMode mode, int view_w, int view_h);
void matrix_sim_resize(MatrixState *st, int view_w, int view_h);
void matrix_sim_set_mode(MatrixState *st, MatrixMode mode);
void matrix_sim_trigger_ripple(MatrixState *st, int px_x, int px_y);
void matrix_sim_step(MatrixState *st, uint32_t dt_ms);

/* Integer pixel blitter (matrix_render.c) */
void matrix_render_argb32(MatrixState *st, uint32_t *pixels, int width, int height);
void matrix_render_rgb565_scanline(const MatrixState *st, int rel_y,
                                   uint16_t *dst_x0, int width);

#endif /* BTRON_MATRIX_INTERNAL_H */
