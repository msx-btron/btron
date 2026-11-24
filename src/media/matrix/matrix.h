/*
 * B-System Native Matrix Digital Rain Screensaver & TrueType Font SDK Demo
 *
 * Direct C99 port of Rezmason/matrix (https://github.com/Rezmason/matrix)
 * by Jeremy Sachs (MIT License), powered by B-System's clean-room Q16.16
 * fixed-point libschrift TrueType rasterizer (Thomas Oltmann, ISC License).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_MEDIA_MATRIX_H
#define BTRON_MEDIA_MATRIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MatrixMode
{
    MATRIX_MODE_TRINITY       = 0,  /* ?version=trinity       (Resurrections 3D volumetric) */
    MATRIX_MODE_CLASSIC       = 1,  /* ?version=classic       (1999/2003 opening titles)    */
    MATRIX_MODE_RESURRECTIONS = 2,  /* ?version=resurrections (2021 135-glyph updated code) */
    MATRIX_MODE_OPERATOR      = 3,  /* ?version=operator      (Nebuchadnezzar + box ripple) */
    MATRIX_MODE_MEGACITY      = 4,  /* ?version=megacity      (Revolutions large code)      */
    MATRIX_MODE_PRIDE         = 5,  /* ?effect=pride          (6-stripe Pride flag colors)  */
    MATRIX_MODE_COUNT         = 6
} MatrixMode;

/* Pure-integer mode parser shared by desktop CLU/gterm & PicoCalc shell_core.
 * Accepts mode name ("trinity", "classic", "resurrections", "operator",
 * "megacity", "pride") or "1".."6". Returns MATRIX_MODE_CLASSIC on NULL/empty,
 * or -1 on unrecognized argument. */
int matrix_parse_mode(const char *arg);
const char *matrix_mode_name(int mode);

#if !defined(PICO_REAL_HW) && !defined(MATRIX_PICO_PROFILE)
#include <btron/wnd.h>
WND *open_matrix_window(void);
WND *open_matrix_window_rect(int x, int y, int w, int h);
WND *open_matrix_window_mode(int mode);
#endif

/* PicoCalc / SegUI SDK Demo entry points */
void matrix_segui_set_mode(int mode);
int  matrix_segui_get_mode(void);
void matrix_segui_tick(void);
int  matrix_segui_key(int key);
void matrix_segui_paint_rect(void *gdev_ptr, int left, int top, int right, int bottom);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_MEDIA_MATRIX_H */
