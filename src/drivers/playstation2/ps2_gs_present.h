/*
 * ps2_gs_present.h - PlayStation 2 Graphics Synthesizer Presentation HAL
 *
 * Provides damage band arithmetic, double-buffering page rotation debt tracking,
 * and GIF DMA canvas upload blitting for B-System on PS2.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_PRESENT_H
#define PS2_GS_PRESENT_H

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>

#define PS2_CURSOR_ROWS 16
#define PS2_CURSOR_COLS 16
#define PS2_PANEL_ROWS  28
#define PS2_BAND_MAX    8u

typedef struct {
    int x0, x1;
    int top, end;
} ps2_band_t;

typedef struct {
    ps2_band_t b[PS2_BAND_MAX];
    unsigned n;
    int overflow;
} ps2_bandset_t;

/* Reasons a canvas upload is permitted to intersect a live GS layer's client rect. */
#define PS2_WHY_STREAM   0u
#define PS2_WHY_CUT      1u
#define PS2_WHY_NO_LAYER 2u
#define PS2_WHY_NO_RECT  3u
#define PS2_WHY_RELAY    4u
#define PS2_WHY_COVER    5u
#define PS2_WHY_PLATED   6u
#define PS2_WHY_NOCUT    7u

/* Band geometry operations */
int  ps2_band_is_full(const ps2_band_t *b);
void ps2_bandset_full(ps2_bandset_t *set);
void ps2_bandset_add(ps2_bandset_t *set, int x0, int x1, int y0, int y1);

/* Double-buffering page rotation debt operations */
void ps2_rot_owe_all(void);
void ps2_rot_clear(void);
void ps2_rot_owe(uint32_t other, const ps2_bandset_t *d);
int  ps2_rot_take(uint32_t page, ps2_bandset_t *set);
int  ps2_rot_debt(uint32_t page);
int  ps2_rot_step(ps2_bandset_t *set);
int  ps2_page_double_apply(uint32_t on);
uint32_t ps2_page_double_wanted(void);

/* Canvas & Region Presentation Blitting */
void ps2_paint_region(GDEV *screen, int x0, int y0, int x1, int y1, int full,
                      uint32_t *r_us, uint32_t *s_us, uint32_t *u_us);
void ps2_paint_bands(GDEV *screen, const ps2_bandset_t *set,
                     uint32_t *r_us, uint32_t *s_us, uint32_t *u_us,
                     uint32_t *px_out);
void ps2_bands_for_pass(ps2_bandset_t *set, int mouse_x, int mouse_y, int shown_x, int shown_y);

#endif /* PS2_GS_PRESENT_H */
