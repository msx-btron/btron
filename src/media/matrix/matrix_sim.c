/*
 * matrix_sim.c — Deterministic Q16.16 Fixed-Point Digital Rain Simulation
 *
 * Zero heap allocation, zero float/double, zero inner-loop divisions.
 * Supports all 6 Rezmason/matrix modes including Chebyshev box ripple
 * (Operator) and 3-plane volumetric parallax drops (Trinity).
 */

#include "matrix_internal.h"
#include <string.h>

static uint32_t m_rng_next(uint32_t *state)
{
    uint32_t x = *state;
    if (x == 0u) { x = 0x1337C0DEu; }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint8_t m_rand_glyph(MatrixState *st)
{
    uint32_t cnt = (uint32_t)st->atlas.glyph_count;
    uint32_t r;
    if (cnt == 0u) { return 0u; }
    r = m_rng_next(&st->rng);
    /* Pure 32-bit MULS range reduction: zero division, zero 64-bit __aeabi_lmul */
    return (uint8_t)(((r & 0xFFFFu) * cnt) >> 16);
}

static void m_spawn_column(MatrixState *st, uint16_t c, int initial)
{
    const MatrixModeSpec *spec = matrix_get_mode_spec(st->mode);
    MatrixCol *col = &st->col_state[c];
    uint32_t r1 = m_rng_next(&st->rng);
    uint32_t r2 = m_rng_next(&st->rng);
    uint32_t len_span = (uint32_t)(spec->drop_len_max - spec->drop_len_min + 1u);
    uint32_t speed_var = 192u + (r1 & 127u); /* 0.75x .. 1.25x in Q8 */

    /* fall_speed_q8 (Q8 rows/s) * speed_var (Q8 0.75..1.25) = Q16 rows/s */
    col->speed_q16 = (int32_t)((uint32_t)spec->fall_speed_q8 * speed_var);
    col->length    = (uint8_t)(spec->drop_len_min + (uint8_t)(((r2 & 0xFFFFu) * len_span) >> 16));
    if (initial != 0) {
        /* Pre-distribute heads across screen so first frames look lush */
        int32_t start_row = (int32_t)(((r1 & 0xFFFFu) * (uint32_t)(st->rows + 12u)) >> 16) - 6;
        col->head_y_q16 = start_row << 16;
        col->delay_ms   = 0;
        col->active     = 1u;
    } else {
        col->head_y_q16 = -(1 << 16);
        col->delay_ms   = (int16_t)(r1 & 0x3FFu); /* 0..1023 ms stagger */
        col->active     = 1u;
    }
}

static void m_spawn_vol_drop(MatrixState *st, uint32_t idx, int initial)
{
    MatrixVolDrop *d = &st->vol_drops[idx];
    uint32_t r1 = m_rng_next(&st->rng);
    uint32_t r2 = m_rng_next(&st->rng);
    uint8_t plane = (uint8_t)(idx % MATRIX_VOL_PLANES);
    int32_t plane_speed;

    d->plane  = plane;
    d->length = (uint8_t)(7u + (r1 & 11u));
    d->seed   = (uint8_t)(r2 & 0xFFu);
    d->active = 1u;

    /* Far plane slower, near plane faster (parallax depth) */
    plane_speed = (plane == 0u) ? 75 : ((plane == 1u) ? 125 : 190);
    d->vy_q16 = (plane_speed + (int32_t)(r1 & 47u)) << 9;

    if (st->view_w > 0) {
        int32_t px = (int32_t)(((uint64_t)r2 * (uint64_t)st->view_w) >> 32);
        d->x_q16 = px << 16;
    } else {
        d->x_q16 = 0;
    }

    if (initial != 0 && st->view_h > 0) {
        int32_t py = (int32_t)(((uint64_t)(r1 >> 4) * (uint64_t)st->view_h) >> 28);
        d->y_q16 = py << 16;
    } else {
        d->y_q16 = -((int32_t)d->length * (int32_t)st->atlas.cell_h) << 15;
    }
}

static void m_compute_cell_size(const MatrixState *st, int view_w, int *out_w, int *out_h)
{
#if defined(PICO_REAL_HW) || defined(MATRIX_PICO_PROFILE)
    (void)st; (void)view_w;
    *out_w = (int)MATRIX_PICO_CELL_W;
    *out_h = (int)MATRIX_PICO_CELL_H;
#else
    const MatrixModeSpec *spec = matrix_get_mode_spec(st->mode);
    int cw;
    int ch;
    if (st->font_px_override > 0) {
        cw = st->font_px_override;
    } else {
        int ref_cols = (int)spec->num_cols_ref;
        if (ref_cols < 20) { ref_cols = 20; }
        cw = view_w / ref_cols;
    }
    if (cw < 8)  { cw = 8; }
    if (cw > (int)MATRIX_MAX_CELL_W) { cw = (int)MATRIX_MAX_CELL_W; }
    ch = (cw * 5) >> 2;
    if (ch < 10) { ch = 10; }
    if (ch > (int)MATRIX_MAX_CELL_H) { ch = (int)MATRIX_MAX_CELL_H; }
    *out_w = cw;
    *out_h = ch;
#endif
}

static void m_reset_grid(MatrixState *st)
{
    uint16_t r;
    uint16_t c;
    uint32_t i;

    for (r = 0u; r < st->rows; r++) {
        for (c = 0u; c < st->cols; c++) {
            MatrixCell *cell = &st->cells[(r * st->cols) + c];
            cell->glyph    = m_rand_glyph(st);
            cell->bright   = 0u;
            cell->cursor   = 0u;
            cell->glint    = 0u;
#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)
            cell->prev_key = 0xFFFFFFFFu;
#endif
        }
    }
    for (c = 0u; c < st->cols; c++) {
        m_spawn_column(st, c, 1);
    }
    for (i = 0u; i < MATRIX_VOL_DROPS; i++) {
        m_spawn_vol_drop(st, i, 1);
    }
    st->ripple_cx      = (int16_t)(st->cols >> 1);
    st->ripple_cy      = (int16_t)(st->rows >> 1);
    st->ripple_rad_q16 = 0;
    st->ripple_active  = ((matrix_get_mode_spec(st->mode)->flags & MAT_FLAG_BOX_RIPPLE) != 0u) ? 1u : 0u;
    st->full_redraw    = 1u;
}

void matrix_sim_resize(MatrixState *st, int view_w, int view_h)
{
    int cw = 10;
    int ch = 14;
    int cols;
    int rows;

    if ((st == NULL) || (view_w <= 0) || (view_h <= 0)) {
        return;
    }
    st->view_w = view_w;
    st->view_h = view_h;

    m_compute_cell_size(st, view_w, &cw, &ch);
    (void)matrix_atlas_build(&st->atlas, st->mode, cw, ch);

    cols = view_w / (int)st->atlas.cell_w;
    rows = view_h / (int)st->atlas.cell_h;
    if (cols < 1) { cols = 1; }
    if (rows < 1) { rows = 1; }
    if (cols > (int)MATRIX_MAX_COLS) { cols = (int)MATRIX_MAX_COLS; }
    if (rows > (int)MATRIX_MAX_ROWS) { rows = (int)MATRIX_MAX_ROWS; }

    st->cols = (uint16_t)cols;
    st->rows = (uint16_t)rows;
    m_reset_grid(st);
}

void matrix_sim_init(MatrixState *st, MatrixMode mode, int view_w, int view_h)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->mode      = ((uint32_t)mode < (uint32_t)MATRIX_MODE_COUNT) ? mode : MATRIX_MODE_CLASSIC;
    st->rng       = 0x5A17C391u ^ ((uint32_t)st->mode * 0x9E3779B9u);
    st->bloom_on  = 1u;
    matrix_sim_resize(st, view_w, view_h);
}

void matrix_sim_set_mode(MatrixState *st, MatrixMode mode)
{
    if ((st == NULL) || ((uint32_t)mode >= (uint32_t)MATRIX_MODE_COUNT)) {
        return;
    }
    st->mode = mode;
    matrix_sim_resize(st, st->view_w, st->view_h);
}

void matrix_sim_trigger_ripple(MatrixState *st, int px_x, int px_y)
{
    if ((st == NULL) || (st->atlas.cell_w == 0u) || (st->atlas.cell_h == 0u)) {
        return;
    }
    if ((matrix_get_mode_spec(st->mode)->flags & MAT_FLAG_BOX_RIPPLE) != 0u) {
        st->ripple_cx      = (int16_t)(px_x / (int)st->atlas.cell_w);
        st->ripple_cy      = (int16_t)(px_y / (int)st->atlas.cell_h);
        st->ripple_rad_q16 = 0;
        st->ripple_active  = 1u;
    } else {
        int col = px_x / (int)st->atlas.cell_w;
        if (col >= 0 && col < (int)st->cols) {
            st->col_state[col].head_y_q16 = (px_y / (int)st->atlas.cell_h) << 16;
            st->col_state[col].delay_ms   = 0;
            st->col_state[col].active     = 1u;
        }
    }
}

void matrix_sim_step(MatrixState *st, uint32_t dt_ms)
{
    const MatrixModeSpec *spec;
    uint32_t decay_step;
    uint32_t cursor_decay;
    uint16_t r;
    uint16_t c;

    if ((st == NULL) || (st->paused != 0u)) {
        return;
    }
    if (dt_ms == 0u)   { dt_ms = 16u; }
    if (dt_ms > 100u)  { dt_ms = 100u; }

    st->frame_count++;
    st->elapsed_ms += dt_ms;
    spec = matrix_get_mode_spec(st->mode);

    /* Volumetric 3-plane parallax update (Trinity mode on desktop; MCU uses fast grid) */
#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)
    if ((spec->flags & MAT_FLAG_VOLUMETRIC) != 0u) {
        uint32_t i;
        int32_t max_y_q16 = (st->view_h + ((int)st->atlas.cell_h * 20)) << 16;
        for (i = 0u; i < MATRIX_VOL_DROPS; i++) {
            MatrixVolDrop *d = &st->vol_drops[i];
            d->y_q16 += (d->vy_q16 >> 10) * (int32_t)dt_ms;
            if (d->y_q16 > max_y_q16) {
                m_spawn_vol_drop(st, i, 0);
            }
        }
        return;
    }
#endif

    /* Update Chebyshev box ripple (Operator mode) */
    if ((spec->flags & MAT_FLAG_BOX_RIPPLE) != 0u) {
        int32_t max_rad = ((st->cols > st->rows) ? (int32_t)st->cols : (int32_t)st->rows) << 16;
        st->ripple_rad_q16 += (int32_t)(dt_ms * 1200u);
        if (st->ripple_rad_q16 > max_rad) {
            st->ripple_rad_q16 = 0;
            st->ripple_cx = (int16_t)(st->cols >> 1);
            st->ripple_cy = (int16_t)(st->rows >> 1);
        }
    }

    decay_step   = (spec->decay_rate_q8 * dt_ms) >> 10;
    cursor_decay = (dt_ms * 10u);
    if (decay_step == 0u)   { decay_step = 1u; }
    if (cursor_decay == 0u) { cursor_decay = 8u; }

    /* Advance raindrop heads */
    for (c = 0u; c < st->cols; c++) {
        MatrixCol *col = &st->col_state[c];
        int32_t prev_row;
        int32_t next_row;

        if (col->delay_ms > 0) {
            col->delay_ms = (int16_t)(col->delay_ms - (int16_t)dt_ms);
            continue;
        }
        prev_row = col->head_y_q16 >> 16;
        col->head_y_q16 += (col->speed_q16 >> 10) * (int32_t)dt_ms;
        next_row = col->head_y_q16 >> 16;

        if (next_row != prev_row) {
            int32_t rr;
            int32_t step_limit = prev_row + 4;
            if (next_row < step_limit) { step_limit = next_row; }
            for (rr = prev_row + 1; rr <= step_limit; rr++) {
                if (rr >= 0 && rr < (int32_t)st->rows) {
                    MatrixCell *cell = &st->cells[((uint32_t)rr * st->cols) + c];
                    cell->glyph  = m_rand_glyph(st);
                    cell->bright = 255u;
                    cell->cursor = 255u;
                    if ((spec->flags & MAT_FLAG_GLINT) != 0u && ((m_rng_next(&st->rng) & 7u) == 0u)) {
                        cell->glint = 220u;
                    }
                }
            }
        }
        if (next_row > ((int32_t)st->rows + (int32_t)col->length)) {
            m_spawn_column(st, c, 0);
        }
    }

    /* Decay trails, mutate active glyphs, and apply Chebyshev ripple */
    for (r = 0u; r < st->rows; r++) {
        MatrixCell *row_ptr = &st->cells[(uint32_t)r * st->cols];
        for (c = 0u; c < st->cols; c++) {
            MatrixCell *cell = &row_ptr[c];
            uint32_t b = cell->bright;

            if (b > spec->base_bright) {
                b = (b > (spec->base_bright + decay_step)) ? (b - decay_step) : spec->base_bright;
                cell->bright = (uint8_t)b;
            } else if (spec->base_bright > 0u && b < spec->base_bright) {
                cell->bright = spec->base_bright;
            }

            if (cell->cursor > 0u) {
                cell->cursor = (cell->cursor > cursor_decay) ? (uint8_t)(cell->cursor - cursor_decay) : 0u;
            }
            if (cell->glint > 0u) {
                cell->glint = (cell->glint > (decay_step << 1)) ? (uint8_t)(cell->glint - (decay_step << 1)) : 0u;
            }

            /* Random glyph mutation in visible cells */
            if (cell->bright > 40u) {
                uint32_t rnd = m_rng_next(&st->rng);
                if ((rnd & 0xFFu) < ((spec->cycle_speed_q8 * dt_ms) >> 9)) {
                    cell->glyph = m_rand_glyph(st);
                }
            }

            /* Operator Chebyshev box ripple wave */
            if ((spec->flags & MAT_FLAG_BOX_RIPPLE) != 0u && st->ripple_active != 0u) {
                int32_t dx = (int32_t)c - (int32_t)st->ripple_cx;
                int32_t dy = (int32_t)r - (int32_t)st->ripple_cy;
                int32_t cheb;
                int32_t rad = st->ripple_rad_q16 >> 16;
                int32_t diff;
                if (dx < 0) { dx = -dx; }
                if (dy < 0) { dy = -dy; }
                cheb = (dx > dy) ? dx : dy;
                diff = cheb - rad;
                if (diff < 0) { diff = -diff; }
                if (diff <= 1) {
                    uint32_t boost = (uint32_t)cell->bright + ((diff == 0) ? 72u : 36u);
                    cell->bright = (boost > 255u) ? 255u : (uint8_t)boost;
                }
            }
        }
    }
}
