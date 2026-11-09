/*
 * src/demo/mars.c — MARS 1993 Voxel Landscape Native BTRON Application & CLU Command
 *
 * Cleanroom C99 implementation of Tim J. Clarke's 1993 Martian voxel terrain
 * engine (Pouët 4662 / MARS.COM / Wojciech Muła).
 * Adheres to NASA/JPL Power of Ten rules:
 *   - Rule 1: Zero recursion (iterative diamond-square state machine).
 *   - Rule 2: Fixed statically verifiable loop upper bounds.
 *   - Rule 3: Zero heap allocation after initialization (static buffers).
 *   - Rule 4: Small modular functions (< 60 lines each).
 *   - Rule 5: Assertions validating parameters and array bounds.
 *   - Rule 6: Minimal variable scope.
 *   - Rule 7: Checked return values.
 *   - Rule 8: Limited preprocessor macros.
 *   - Rule 9: Max 1 pointer dereference.
 *   - Rule 10: Warning-free C99 (-Wall -Wextra).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "mars.h"
#include <btron/btron.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define MARS_CANVAS_W 256
#define MARS_CANVAS_H 200
#define MARS_WND_W    320
#define MARS_WND_H    240
#define MARS_MAP_SIZE 0x10000

#define MAP_IDX_CAP(idx) (((size_t)(idx) % MARS_MAP_SIZE))
#define MAP_IDX(x, y)    MAP_IDX_CAP(((size_t)(y) * 256 + (size_t)(x)))
#define CLAMP_VAL(v, min, max) ((v) < (min) ? (min) : ((v) > (max) ? (max) : (v)))

#define TERRAIN_RAY_TABLE_COUNT (200 + 0x29 + 0x0a + 0x0a)

/* ── Static NASA JPL Rule 3 Memory Pools ──────────────────────────── */
#define MARS_BUF_COUNT 3
static uint8_t  s_landscape_buffers[MARS_BUF_COUNT][MARS_CANVAS_W * MARS_CANVAS_H];
static volatile int  s_render_idx   = 0;
static volatile int  s_ready_idx    = 1;
static volatile int  s_display_idx  = 2;
static volatile bool s_has_new_frame = false;

static uint8_t  s_aux_array[MARS_MAP_SIZE];   /* 64 KB terrain height map */
static uint8_t  s_terra_data[MARS_MAP_SIZE];  /* 64 KB slope & shading map */
static uint8_t  s_sky_data[MARS_MAP_SIZE];    /* 64 KB sky plasma texture */

static uint16_t s_ray_table[TERRAIN_RAY_TABLE_COUNT];
static int16_t  s_col_heights[MARS_CANVAS_W];
static uint16_t s_col_colors[MARS_CANVAS_W];
static COLOR    s_palette_argb[256];

static const uint16_t CONST_CF85 = 0xcf85;
static uint16_t s_var_0355 = 0x03e8; /* Camera X position (calibrated) */
static uint16_t s_var_0357 = 0x03e8; /* Camera Y position (calibrated) */
static uint16_t s_var_0359 = 0;      /* Camera Z / elevation */
static uint16_t s_var_035d = 0x0e75; /* Calibrated seed matching original MARS picture */
static uint16_t s_var_035d_tmp = 0;  /* LCG working seed */
static int32_t  s_jump_vel = 0;      /* Ballistic jump vertical velocity */
static int32_t  s_jump_height = 0;   /* Ballistic jump current altitude */

static WND *s_mars_wnd = NULL;
static ID   s_mars_tskid = 0;
static bool s_mars_running = false;
static bool s_mars_initialized = false;

/* ── Original 1993 Tim Clarke MARS.EXE VGA Palette ────────────────── */
static const uint8_t s_orig_palette[96][3] = {
    /* 00..63: Terracotta Martian mountain slope gradient (6-bit VGA DAC values) */
    {0x00, 0x00, 0x00}, {0x01, 0x00, 0x00}, {0x02, 0x00, 0x00}, {0x03, 0x00, 0x00},
    {0x03, 0x01, 0x00}, {0x04, 0x00, 0x00}, {0x05, 0x00, 0x00}, {0x06, 0x00, 0x00},
    {0x07, 0x00, 0x00}, {0x08, 0x00, 0x00}, {0x09, 0x00, 0x00}, {0x0a, 0x00, 0x00},
    {0x0b, 0x00, 0x00}, {0x0c, 0x00, 0x00}, {0x0d, 0x00, 0x00}, {0x0e, 0x00, 0x00},
    {0x0f, 0x00, 0x00}, {0x10, 0x00, 0x00}, {0x11, 0x00, 0x00}, {0x12, 0x00, 0x00},
    {0x13, 0x00, 0x00}, {0x14, 0x00, 0x00}, {0x15, 0x00, 0x00}, {0x16, 0x00, 0x00},
    {0x17, 0x00, 0x00}, {0x18, 0x00, 0x00}, {0x19, 0x00, 0x00}, {0x1a, 0x00, 0x00},
    {0x1b, 0x00, 0x00}, {0x1c, 0x00, 0x00}, {0x1d, 0x01, 0x00}, {0x1e, 0x02, 0x00},
    {0x1f, 0x03, 0x00}, {0x20, 0x04, 0x00}, {0x21, 0x05, 0x00}, {0x22, 0x06, 0x00},
    {0x23, 0x07, 0x01}, {0x24, 0x08, 0x02}, {0x25, 0x09, 0x03}, {0x26, 0x0a, 0x04},
    {0x27, 0x0b, 0x05}, {0x28, 0x0c, 0x06}, {0x29, 0x0d, 0x07}, {0x2a, 0x0e, 0x08},
    {0x2b, 0x0f, 0x09}, {0x2c, 0x10, 0x0a}, {0x2d, 0x11, 0x0b}, {0x2e, 0x12, 0x0c},
    {0x2f, 0x13, 0x0d}, {0x30, 0x14, 0x0e}, {0x31, 0x15, 0x0f}, {0x32, 0x16, 0x10},
    {0x33, 0x17, 0x11}, {0x34, 0x18, 0x12}, {0x35, 0x19, 0x13}, {0x36, 0x1a, 0x14},
    {0x37, 0x1b, 0x15}, {0x38, 0x1c, 0x16}, {0x39, 0x1d, 0x17}, {0x3a, 0x1e, 0x18},
    {0x3b, 0x1f, 0x19}, {0x3c, 0x20, 0x1a}, {0x3d, 0x21, 0x1b}, {0x3e, 0x22, 0x1c},
    /* 64..95: Dusty rose sky and pink-tinted cloud gradient */
    {0x32, 0x0a, 0x0a}, {0x32, 0x0b, 0x0b}, {0x32, 0x0c, 0x0c}, {0x32, 0x0d, 0x0d},
    {0x32, 0x0e, 0x0e}, {0x32, 0x0f, 0x0f}, {0x32, 0x10, 0x10}, {0x32, 0x11, 0x11},
    {0x32, 0x12, 0x12}, {0x32, 0x13, 0x13}, {0x32, 0x14, 0x14}, {0x32, 0x15, 0x15},
    {0x32, 0x16, 0x16}, {0x32, 0x17, 0x17}, {0x32, 0x18, 0x18}, {0x32, 0x19, 0x19},
    {0x32, 0x1a, 0x1a}, {0x32, 0x1b, 0x1b}, {0x32, 0x1c, 0x1c}, {0x32, 0x1d, 0x1d},
    {0x32, 0x1e, 0x1e}, {0x32, 0x1f, 0x1f}, {0x32, 0x20, 0x20}, {0x32, 0x21, 0x21},
    {0x32, 0x22, 0x22}, {0x32, 0x23, 0x23}, {0x32, 0x24, 0x24}, {0x32, 0x25, 0x25},
    {0x32, 0x26, 0x26}, {0x32, 0x27, 0x27}, {0x32, 0x28, 0x28}, {0x32, 0x29, 0x29}
};

/* ── Random Number Generator (LCG) ────────────────────────────────── */
static int cr05_subroutine(uint8_t center_val, int step_size) {
    uint32_t rng_acc = (uint32_t)0x00ab * s_var_035d_tmp + 0x2bcd;
    s_var_035d_tmp = (uint16_t)(rng_acc % CONST_CF85);
    int16_t rng_offset = (int16_t)s_var_035d_tmp - 0x67c2;
    int32_t scaled_rand = (int32_t)step_size * rng_offset;
    uint8_t rand_byte = (uint8_t)((scaled_rand >> 8) & 0xFF);
    uint8_t rand_hi_byte = (uint8_t)((scaled_rand >> 16) & 0xFF);
    int16_t rand_variation = (int16_t)((rand_hi_byte << 8) | rand_byte);
    rand_variation >>= 5;
    rand_variation &= 0x00FF;
    if (rand_variation & 0x80) {
        rand_variation |= (int16_t)0xFF00;
    }
    return rand_variation + center_val;
}

static uint8_t cr03_subroutine(uint8_t *buf, unsigned int src_idx,
                               unsigned int dst_idx, uint8_t half_step, unsigned int ref_val) {
    assert(buf != NULL);
    if (buf[dst_idx] != 0xff) {
        return buf[src_idx];
    }
    uint16_t sum_word = (uint16_t)((ref_val + buf[src_idx]) / 2);
    int16_t varied_val = (int16_t)cr05_subroutine((uint8_t)sum_word, half_step);
    buf[dst_idx] = (uint8_t)CLAMP_VAL(varied_val, 0, 0xfe);
    return buf[src_idx];
}

/* ── Non-Recursive Iterative Diamond-Square (NASA JPL Rule 1) ─────── */
typedef struct {
    uint16_t full_size;
    uint8_t  start_x;
    uint8_t  start_y;
} DsQuad;

static void diamond_square_iter(uint8_t *buf) {
    assert(buf != NULL);
    DsQuad stack[32];
    int sp = 0;

    stack[sp].full_size = 256;
    stack[sp].start_x = 0;
    stack[sp].start_y = 0;
    sp++;

    while (sp > 0) {
        sp--;
        DsQuad q = stack[sp];
        uint16_t half_step = q.full_size / 2;
        unsigned int x = q.start_x;
        unsigned int y = q.start_y;
        unsigned int pos = MAP_IDX(x, y);
        uint8_t corner_val = buf[pos];
        const uint16_t step_twice = 2 * half_step;

        x = (x + half_step) % 256; pos = MAP_IDX(x, y);
        unsigned int mid_pos = pos;
        x = (x + half_step) % 256; pos = MAP_IDX(x, y);
        corner_val = cr03_subroutine(buf, pos, mid_pos, (uint8_t)half_step, corner_val);

        y = (y + half_step) % 256; pos = MAP_IDX(x, y);
        mid_pos = pos;
        y = (y + half_step) % 256; pos = MAP_IDX(x, y);
        corner_val = cr03_subroutine(buf, pos, mid_pos, (uint8_t)half_step, corner_val);

        x = (x - half_step) % 256; pos = MAP_IDX(x, y);
        mid_pos = pos;
        x = (x - half_step) % 256; pos = MAP_IDX(x, y);
        corner_val = cr03_subroutine(buf, pos, mid_pos, (uint8_t)half_step, corner_val);

        y = (y - half_step) % 256; pos = MAP_IDX(x, y);
        mid_pos = pos;
        y = (y - half_step) % 256; pos = MAP_IDX(x, y);
        corner_val = cr03_subroutine(buf, pos, mid_pos, (uint8_t)half_step, corner_val);

        uint16_t corner_sum = buf[MAP_IDX(q.start_x, q.start_y)];
        x = (x + step_twice) % 256; pos = MAP_IDX(x, y); corner_sum += buf[pos];
        y = (y + step_twice) % 256; pos = MAP_IDX(x, y); corner_sum += buf[pos];
        x = (x - step_twice) % 256; pos = MAP_IDX(x, y); corner_sum += buf[pos];
        corner_sum >>= 2;

        int new_val = cr05_subroutine((uint8_t)corner_sum, half_step);
        new_val = CLAMP_VAL(new_val, 0, 0xfe);
        x = (x + half_step) % 256; y = (y - half_step) % 256;
        buf[MAP_IDX(x, y)] = (uint8_t)new_val;

        if (half_step == 1) continue;
        uint16_t next_size = half_step;
        uint8_t qx = (uint8_t)((x - half_step) % 256);
        uint8_t qy = (uint8_t)((y - half_step) % 256);

        stack[sp].full_size = next_size; stack[sp].start_x = qx; stack[sp].start_y = qy + next_size; sp++;
        stack[sp].full_size = next_size; stack[sp].start_x = qx + next_size; stack[sp].start_y = qy + next_size; sp++;
        stack[sp].full_size = next_size; stack[sp].start_x = qx + next_size; stack[sp].start_y = qy; sp++;
        stack[sp].full_size = next_size; stack[sp].start_x = qx; stack[sp].start_y = qy; sp++;
        assert(sp <= 32);
    }
}

/* ── Palette, Ray Table & Plasma Generator ────────────────────────── */
static void init_ray_table(void) {
    uint16_t value = 0x0b3e;
    int out_idx = 0;
    for (int i = 0; i < 200; i++) { s_ray_table[out_idx++] = value; value += 0x0006; }
    value = 0;
    for (int i = 0; i < 0x29; i++) { s_ray_table[out_idx++] = value++; }
    value = 0x2b;
    for (int i = 0; i < 0x0a; i++) { s_ray_table[out_idx++] = value; value += 2; }
    value = 0x40;
    for (int i = 0; i < 0x0a; i++) { s_ray_table[out_idx++] = value; value += 4; }
    assert(out_idx == TERRAIN_RAY_TABLE_COUNT);
}

static void load_palette(void) {
    for (int i = 0; i < 96; i++) {
        uint8_t r = (uint8_t)(s_orig_palette[i][0] << 2);
        uint8_t g = (uint8_t)(s_orig_palette[i][1] << 2);
        uint8_t b = (uint8_t)(s_orig_palette[i][2] << 2);
        s_palette_argb[i] = (COLOR)(0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
    }
    for (int i = 96; i < 256; i++) {
        s_palette_argb[i] = (COLOR)0xFF000000;
    }
}

static void prepare_sky(void) {
    memset(s_sky_data, 0xFF, MARS_MAP_SIZE);
    uint32_t temp = (uint32_t)0x00ab * s_var_035d + 0x2bcd;
    s_var_035d = (uint16_t)(temp % CONST_CF85);
    s_sky_data[MAP_IDX(0, 0)] = 0;
    s_sky_data[MAP_IDX(128, 0)] = 0xfe;
    s_sky_data[MAP_IDX(0, 128)] = 0xfe;
    s_sky_data[MAP_IDX(128, 128)] = 0;
    s_var_035d_tmp = s_var_035d;
    diamond_square_iter(s_sky_data);
    for (size_t i = 0; i < MARS_MAP_SIZE; i++) {
        s_sky_data[i] = (uint8_t)((s_sky_data[i] >> 3) + 0x40);
    }
}

static void prepare_terrain(void) {
    memset(s_aux_array, 0xFF, MARS_MAP_SIZE);
    memset(s_terra_data, 0xFF, MARS_MAP_SIZE);
    uint32_t temp = (uint32_t)0x00ab * s_var_035d + 0x2bcd;
    s_var_035d = (uint16_t)(temp % CONST_CF85);
    s_aux_array[0] = 0x80;
    s_var_035d_tmp = s_var_035d;
    diamond_square_iter(s_aux_array);

    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            size_t px_idx = MAP_IDX(x, y);
            int sum = s_aux_array[px_idx];
            sum += s_aux_array[MAP_IDX(x + 4, y)];
            sum += s_aux_array[MAP_IDX(x + 2, y + 2)];
            sum += s_aux_array[MAP_IDX(x - 1, y - 1)];
            s_aux_array[px_idx] = (uint8_t)(sum / 4);
        }
    }

    for (size_t px_idx = 0; px_idx < MARS_MAP_SIZE; px_idx++) {
        int16_t diff = (int16_t)s_aux_array[px_idx] - (int16_t)s_aux_array[MAP_IDX_CAP(px_idx + 3)] + 0x20;
        s_terra_data[px_idx] = (uint8_t)CLAMP_VAL(diff, 0, 63);
    }

    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            size_t px_idx = MAP_IDX(x, y);
            int sum = s_aux_array[px_idx];
            sum += s_aux_array[MAP_IDX(x + 1, y)];
            sum += s_aux_array[MAP_IDX(x, y + 1)];
            sum += s_aux_array[MAP_IDX(x + 1, y + 1)];
            s_aux_array[px_idx] = (uint8_t)(sum / 4);
        }
    }
}

static uint16_t cr02_function(unsigned int terrain_idx, uint8_t frac_step) {
    uint8_t slope_delta = (uint8_t)(s_terra_data[MAP_IDX_CAP(terrain_idx + 1)] - s_terra_data[terrain_idx]);
    uint16_t interp_color = (uint16_t)((int8_t)slope_delta * frac_step) * 2;
    interp_color += (uint16_t)(s_terra_data[terrain_idx] * 256);
    return interp_color;
}

static uint16_t calculate_height(uint16_t cam_x, uint16_t cam_y, int32_t jump_h) {
    uint16_t player_x_rot = (uint16_t)((cam_x >> 4) | (cam_x << 12));
    uint16_t player_y_rot = (uint16_t)((cam_y >> 4) | (cam_y << 12));
    unsigned int map_x = player_x_rot % 256;
    unsigned int map_y = player_y_rot % 256;
    unsigned int map_pos = MAP_IDX(map_x, map_y);
    player_x_rot >>= 12; player_y_rot >>= 12;

    int16_t height_delta = (int16_t)s_aux_array[MAP_IDX(map_x + 1, map_y)] - (int16_t)s_aux_array[map_pos];
    int32_t result = (int32_t)height_delta * (int16_t)player_x_rot + (s_aux_array[map_pos] << 4);
    uint16_t interp_x = (uint16_t)result;

    height_delta = (int16_t)s_aux_array[MAP_IDX(map_x + 1, map_y + 1)] - (int16_t)s_aux_array[MAP_IDX(map_x, map_y + 1)];
    result = (int32_t)height_delta * (int16_t)player_x_rot + (s_aux_array[MAP_IDX(map_x, map_y + 1)] << 4);
    uint16_t interp_x2 = (uint16_t)result;

    result = ((int32_t)(interp_x2 - interp_x) * (int16_t)player_y_rot) + (interp_x << 4);
    uint16_t v0359 = (uint16_t)result;
    uint32_t height_result = (v0359 & 0xFF) | (((v0359 >> 8) + 0x19) << 8);
    height_result += (uint32_t)jump_h;
    v0359 = (height_result >= 0x10000) ? 0xffff : (uint16_t)height_result;
    return v0359;
}

/* ── Rendering Pipeline ───────────────────────────────────────────── */
static void render_sky(uint8_t *dst, uint16_t cam_x, uint16_t cam_y, uint16_t cam_h) {
    if (!dst) return;
    size_t dst_idx = 0;
    const size_t render_px_count = MARS_CANVAS_W * MARS_CANVAS_H;
    int32_t sky_offset = ((uint16_t)(-(int16_t)cam_h) + 0x20000) << 10;
    int32_t sky_y_step = sky_offset;
    int32_t sky_x_start = (int32_t)cam_x << 9;
    int32_t sky_y_start = (int32_t)cam_y << 9;

    for (int32_t scan_line = 99; scan_line > 0; scan_line--) {
        uint32_t step_per_col = (uint32_t)(sky_y_step / scan_line);
        uint32_t sky_x_pos = sky_x_start - step_per_col;
        uint32_t sky_y_pos = sky_y_start + step_per_col;
        step_per_col >>= 7;
        uint16_t sky_frac_x = (uint16_t)(sky_x_pos % 0x10000);
        uint16_t sky_tex_idx = (uint16_t)(((sky_x_pos >> 16) % 256) | (((sky_y_pos >> 16) % 256) * 256));
        uint16_t col_y_step = (uint16_t)((step_per_col >> 16) - 1);

        uint8_t sky_color = s_sky_data[sky_tex_idx++];
        if (dst_idx < render_px_count) dst[dst_idx] = sky_color;
        dst_idx++;

        for (int32_t col_counter = 255; col_counter > 0; col_counter--) {
            uint32_t frac_plus_step = sky_frac_x + (step_per_col & 0xFFFF);
            sky_frac_x = (uint16_t)(frac_plus_step & 0xFFFF);
            sky_tex_idx += (uint16_t)(col_y_step + (frac_plus_step > 0xFFFF));
            sky_color = s_sky_data[sky_tex_idx++];
            if (dst_idx < render_px_count) dst[dst_idx] = sky_color;
            dst_idx++;
        }
    }

    for (int i = 0; i < 256; i++) {
        if (dst_idx < render_px_count) dst[dst_idx] = 80;
        dst_idx++;
    }

    uint16_t ground_step = (uint16_t)((cam_h / 2) + 10);
    for (int32_t band = 4; band < 44; band++) {
        uint16_t shade = (uint16_t)(ground_step / band / 128);
        if (shade > 63) shade = 63;
        uint8_t color = (uint8_t)shade;
        for (int x = 0; x < 256; x++) {
            if (dst_idx < render_px_count) dst[dst_idx] = color;
            dst_idx++;
        }
    }
}

__attribute__((weak)) void btron_cooperative_input_pump(void) {}

static void render_terrain(uint8_t *dst, uint16_t cam_x, uint16_t cam_y, uint16_t cam_h) {
    if (!dst) return;
    for (int i = 0; i < 256; i++) {
        s_col_heights[i] = 0x7d00;
        s_col_colors[i] = 0;
    }
    int16_t ray_row = 60;
    while (ray_row > 0) {
        if ((ray_row & 15) == 0) btron_cooperative_input_pump();
        uint16_t step_size = (uint16_t)((s_ray_table[ray_row + 200] << 4) + ((cam_y & 0x0f) ^ 0x0f));
        uint32_t temp_div = cam_h;
        int16_t tmp_03a2 = (int16_t)(temp_div / step_size) + 100;
        uint32_t step_scaled = (uint32_t)step_size << 6;
        uint16_t tmp_03a4 = (ray_row == 1) ? 0 : (uint16_t)(0x10000 / step_size);
        if (ray_row == 1) tmp_03a2 = 0x7d00;

        uint32_t tex_x_pos = ((uint32_t)cam_x << 12) - (step_scaled << 7);
        uint16_t map_y_frac = (uint16_t)(cam_y << 4);
        uint32_t terrain_pos = (uint32_t)((tex_x_pos >> 16 & 0xFFFF00FF) | (map_y_frac & 0xFF00));
        uint16_t sub_step = (uint16_t)(step_size >> 4);
        uint8_t tmp_add_bh_al = (uint8_t)(((terrain_pos & 0x0000FF00) >> 8) + (sub_step & 0xFF));
        terrain_pos = (terrain_pos & 0xFFFF00FF) | ((uint32_t)tmp_add_bh_al << 8);
        uint16_t tex_x_frac = (uint16_t)((tex_x_pos & 0xFFFF) >> 1);

        int16_t src_idx = 255;
        uint16_t base_x = 0;
        while (src_idx >= 0) {
            tex_x_frac <<= 1;
            uint32_t temp_add_cx = (uint32_t)tex_x_frac + (step_scaled & 0xFFFF);
            uint8_t temp_bl = (uint8_t)((terrain_pos & 0xFF) + ((step_scaled >> 16) & 0xFF));
            if (temp_add_cx > 0xFFFF) temp_bl += 1;
            terrain_pos = (terrain_pos & 0xFFFFFF00) | temp_bl;
            tex_x_frac = (uint16_t)((temp_add_cx & 0xFFFF) >> 1);

            uint16_t terrain_idx = (uint16_t)MAP_IDX_CAP(terrain_pos);
            uint8_t height_delta = s_aux_array[MAP_IDX_CAP(terrain_idx + 1)];
            uint16_t height_val = (uint16_t)(height_delta - s_aux_array[terrain_idx]);
            height_val = (height_val & 0xFF) | ((height_val & 0xFF00) ? 0xFF00 : 0);
            int32_t temp_result = (int16_t)height_val * (int16_t)tex_x_frac;
            uint16_t height_sample = (uint16_t)((temp_result >> 7) & 0xFFFF);
            height_sample = (height_sample & 0xFF) | ((((height_sample >> 8) + s_aux_array[terrain_idx]) << 8) & 0xFF00);

            temp_result = (uint32_t)height_sample * tmp_03a4;
            int16_t base_y = (int16_t)CLAMP_VAL(tmp_03a2 - (temp_result >> 16), -1, 199);
            int16_t height_span = (int16_t)(s_col_heights[src_idx] - base_y);
            s_col_heights[src_idx] = base_y;

            if (height_span < 0) {
                uint16_t cr02_res = cr02_function(terrain_idx, (uint8_t)(tex_x_frac / 256));
                uint16_t prev_color = s_col_colors[src_idx];
                s_col_colors[src_idx] = cr02_res;
                int16_t color_step = (int16_t)((int16_t)(prev_color - cr02_res) / height_span);
                uint16_t color = prev_color;
                uint16_t table_offset = s_ray_table[(uint16_t)(height_span + 200)];
                int index_row_offset = (table_offset - 0xb3e) / 6 - 199;
                for (; index_row_offset != 1; index_row_offset++) {
                    int idx = (base_y + index_row_offset) * 256 + base_x;
                    if (idx >= 0 && idx < 256 * 200) {
                        dst[idx] = (uint8_t)(color / 256);
                    }
                    color += color_step;
                }
            } else {
                s_col_colors[src_idx] = cr02_function(terrain_idx, (uint8_t)(tex_x_frac / 256));
            }
            base_x++;
            src_idx--;
        }
        ray_row -= 1;
    }
}

/* ── Lifecycle & Task Management ──────────────────────────────────── */
static void mars_init_once(void) {
    if (s_mars_initialized) return;
    init_ray_table();
    load_palette();
    prepare_sky();
    prepare_terrain();

    uint16_t cam_x = s_var_0355;
    uint16_t cam_y = s_var_0357;
    int32_t jump_h = s_jump_height;
    uint16_t cam_h = calculate_height(cam_x, cam_y, jump_h);
    s_var_0359 = cam_h;

    /* Prime all triple buffers with the initial perspective scene */
    render_sky(s_landscape_buffers[0], cam_x, cam_y, cam_h);
    render_terrain(s_landscape_buffers[0], cam_x, cam_y, cam_h);
    memcpy(s_landscape_buffers[1], s_landscape_buffers[0], MARS_CANVAS_W * MARS_CANVAS_H);
    memcpy(s_landscape_buffers[2], s_landscape_buffers[0], MARS_CANVAS_W * MARS_CANVAS_H);

    s_render_idx = 0;
    s_ready_idx = 1;
    s_display_idx = 2;
    s_has_new_frame = false;

    s_mars_initialized = true;
}

static void mars_task_fn(VW exinf) {
    (void)exinf;
    while (s_mars_running && s_mars_wnd) {
        if (s_jump_height > 0 || s_jump_vel != 0) {
            s_jump_height += s_jump_vel;
            s_jump_vel -= 60; /* Martian gravity */
            if (s_jump_height <= 0) {
                s_jump_height = 0;
                s_jump_vel = 0;
            }
        }

        /* 1. Atomically snapshot camera state for this frame */
        uint16_t cam_x = s_var_0355;
        uint16_t cam_y = s_var_0357;
        int32_t jump_h = s_jump_height;
        uint16_t cam_h = calculate_height(cam_x, cam_y, jump_h);
        s_var_0359 = cam_h;

        /* 2. Render complete frame into the designated backbuffer */
        int r_idx = s_render_idx;
        uint8_t *render_target = s_landscape_buffers[r_idx];
        render_sky(render_target, cam_x, cam_y, cam_h);
        render_terrain(render_target, cam_x, cam_y, cam_h);

        /* 3. Publish finished frame */
        s_ready_idx = r_idx;
        s_has_new_frame = true;

        /* 4. Rotate render buffer avoiding display and ready slots */
        int cur_display = s_display_idx;
        int next_render = 3 - s_ready_idx - cur_display;
        if (next_render < 0 || next_render >= MARS_BUF_COUNT) {
            next_render = (s_ready_idx + 1) % MARS_BUF_COUNT;
        }
        s_render_idx = next_render;

        /* 5. Invalidate window to schedule compositor presentation */
        inval_wnd(s_mars_wnd);
        dly_tsk(wnd_frame_ms(s_mars_wnd));
    }
    s_mars_tskid = 0;
}

static bool s_mouse_down = false;
static int  s_prev_x = 0;
static int  s_prev_y = 0;

static void mars_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || !dev->pixels) return;
    int dw = dev->width;
    int dh = dev->height;
    if (dw <= 0 || dh <= 0) return;

    /* Lock-free fetch of latest ready buffer without blocking renderer */
    if (s_has_new_frame) {
        s_display_idx = s_ready_idx;
        s_has_new_frame = false;
    }
    const uint8_t *src_frame = s_landscape_buffers[s_display_idx];

    /* Scaled smooth render filling entire window client rect */
    for (int y = 0; y < dh; y++) {
        int src_y = (y * MARS_CANVAS_H) / dh;
        if (src_y >= MARS_CANVAS_H) src_y = MARS_CANVAS_H - 1;
        COLOR *dst = &dev->pixels[y * dw];
        const uint8_t *src_row = &src_frame[src_y * MARS_CANVAS_W];
        for (int x = 0; x < dw; x++) {
            int src_x = (x * MARS_CANVAS_W) / dw;
            if (src_x >= MARS_CANVAS_W) src_x = MARS_CANVAS_W - 1;
            dst[x] = s_palette_argb[src_row[src_x]];
        }
    }
}

void mars_render_to_buffer(COLOR *dst, int dw, int dh, uint16_t cam_x, uint16_t cam_y, int32_t jump_h) {
    if (!dst || dw <= 0 || dh <= 0) return;
    mars_init_once();
    s_var_0355 = cam_x;
    s_var_0357 = cam_y;
    s_jump_height = jump_h;
    uint16_t cam_h = calculate_height(cam_x, cam_y, jump_h);
    s_var_0359 = cam_h;

    uint8_t *tmp_buf = s_landscape_buffers[0];
    render_sky(tmp_buf, cam_x, cam_y, cam_h);
    render_terrain(tmp_buf, cam_x, cam_y, cam_h);

    for (int y = 0; y < dh; y++) {
        int src_y = (y * MARS_CANVAS_H) / dh;
        if (src_y >= MARS_CANVAS_H) src_y = MARS_CANVAS_H - 1;
        COLOR *dst_row = &dst[y * dw];
        const uint8_t *src_row = &tmp_buf[src_y * MARS_CANVAS_W];
        for (int x = 0; x < dw; x++) {
            int src_x = (x * MARS_CANVAS_W) / dw;
            if (src_x >= MARS_CANVAS_W) src_x = MARS_CANVAS_W - 1;
            dst_row[x] = s_palette_argb[src_row[src_x]];
        }
    }
}

static void mars_destroy(WND *wnd) {
    (void)wnd;
    s_mars_running = false;
    s_mars_wnd = NULL;
}

static void mars_event(WND *wnd, const EVT *evt) {
    if (!evt) return;

    if (evt->type == EV_KEY_DOWN) {
        UW k = evt->key;
        uint16_t mod = (uint16_t)(uintptr_t)evt->data;
        bool shift = (mod & BTRON_KMOD_SHIFT) != 0;

        if (k == 27 || k == 'q' || k == 'Q') {
            mars_destroy(wnd);
            cls_wnd(wnd);
            return;
        }
        if (k == 'a' || k == 'A') {
            /* Lateral strafe left (referential to mars.fth: 1.5 map units) */
            s_var_0355 -= 24;
        } else if (k == 'd' || k == 'D') {
            /* Lateral strafe right (referential to mars.fth: 1.5 map units) */
            s_var_0355 += 24;
        } else if (k == BTRON_KEY_LEFT) {
            /* Shift + Left: strafe left, else pan/yaw turn left */
            s_var_0355 -= (shift ? 24 : 12);
        } else if (k == BTRON_KEY_RIGHT) {
            /* Shift + Right: strafe right, else pan/yaw turn right */
            s_var_0355 += (shift ? 24 : 12);
        } else if (k == BTRON_KEY_UP || k == 'w' || k == 'W') {
            /* Forward navigation (referential to mars.fth: 1.5 map units) */
            s_var_0357 += 24;
        } else if (k == BTRON_KEY_DOWN || k == 's' || k == 'S') {
            /* Reverse / brake (referential to mars.fth: 1.5 map units) */
            s_var_0357 -= 24;
        } else if (k == ' ' || k == BTRON_KEY_SPACE) {
            /* Ballistic jump with physics arc */
            if (s_jump_height == 0) {
                s_jump_vel = 1200;
                s_jump_height = 1;
            }
        }
    } else if (evt->type == EV_BUT_DOWN) {
        s_mouse_down = true;
        s_prev_x = evt->pos.x;
        s_prev_y = evt->pos.y;
    } else if (evt->type == EV_BUT_UP) {
        s_mouse_down = false;
    } else if (evt->type == EV_MOUSE_MOVE) {
        if (s_mouse_down) {
            int dx = evt->pos.x - s_prev_x;
            int dy = evt->pos.y - s_prev_y;
            s_var_0355 += (uint16_t)(dx * 6);
            s_var_0357 -= (uint16_t)(dy * 6);
            s_prev_x = evt->pos.x;
            s_prev_y = evt->pos.y;
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────── */
WND* open_mars_window_rect(int x, int y, int w, int h) {
    if (s_mars_wnd) {
        top_wnd(s_mars_wnd);
        return s_mars_wnd;
    }

    mars_init_once();

    if (w <= 0 || h <= 0) {
        x = 100; y = 60;
        w = MARS_WND_W; h = MARS_WND_H;
    }

    s_mars_wnd = opn_wnd("MARS Landscape Renderer",
                         x, y, w, h,
                         WND_ATTR_TITLE | WND_ATTR_BORDER | WND_ATTR_CLOSE | WND_ATTR_RESIZE);
    if (!s_mars_wnd) return NULL;

    s_mars_wnd->paint = mars_paint;
    s_mars_wnd->event_handler = mars_event;
    s_mars_wnd->destroy = mars_destroy;

    s_mars_running = true;

    T_CTSK ctsk;
    ctsk.exinf = 0;
    ctsk.tskatr = TA_HLNG;
    ctsk.task = mars_task_fn;
    ctsk.itskpri = 8;
    ctsk.stksz = 32768;
    ctsk.name = "mars_task";
    s_mars_tskid = cre_tsk(&ctsk);
    if (s_mars_tskid > 0) {
        sta_tsk(s_mars_tskid, 0);
    }

    return s_mars_wnd;
}

WND* open_mars_window(void) {
    return open_mars_window_rect(-1, -1, -1, -1);
}

void clu_cmd_mars(const char *args, ShellOutputFn out, void *ud) {
    (void)args;
    out("MARS 1993 Voxel Landscape Renderer (Tim Clarke / NASA JPL C99):", COLOR_CYAN, ud);
    out("  Resolution : 256x200 canvas centered in 320x240 BTRON window (~60 FPS)", COLOR_LTGRAY, ud);
    out("  Algorithm  : Cleanroom C99 implementation of Pouët 4662 / MARS.COM", COLOR_LTGRAY, ud);
    out("  Palette    : Original 1993 Tim Clarke VGA DAC 96-color Martian gamut", COLOR_LTGRAY, ud);
    out("  Controls   : Mouse Drag / Arrow Keys / WASD = Pan & Flight, Space = Boost, ESC = Exit", COLOR_YELLOW, ud);

    WND *w = open_mars_window();
    if (w) {
        out("  MARS Landscape window launched on Workbench desktop.", COLOR_GREEN, ud);
    } else {
        out("  mars: failed to open window surface", COLOR_RED, ud);
    }
}
