/*
 * matrix_app.c — Native B-System Desktop Application for Matrix Screensaver
 *
 * Direct C99 port of Rezmason/matrix (https://github.com/Rezmason/matrix)
 * by Jeremy Sachs (MIT License), using native B-System wnd.c + GDEV raw
 * integer BitBlitting and clean-room Q16.16 libschrift TrueType rasterization
 * (Thomas Oltmann, ISC License).
 *
 * Defaults:
 *   - Window: 640x480 resizable B-System window ('f' toggles fullscreen)
 *   - Mode:   MATRIX_MODE_CLASSIC (keys 1-6 switch modes)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "matrix_internal.h"
#include <btron/btron.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <string.h>

extern void uart_puts_raw(const char *s);

static WND        *s_matrix_wnd   = NULL;
static ID          s_matrix_tskid = 0;
static MatrixState s_matrix_st;
static uint8_t     s_st_ready     = 0u;
static uint8_t     s_fullscreen   = 0u;
static uint8_t     s_show_fps     = 1u;
static RECT        s_saved_bounds;
static SYSTIME     s_last_tick_ms = 0;

/* Title-bar FPS counter (32-bit math, zero stdio) */
static uint32_t    s_fps_win_us   = 0u;
static uint32_t    s_fps_frames   = 0u;
static uint32_t    s_fps_val      = 0u;
static uint8_t     s_fps_open     = 0u;

static void matrix_update_title(void)
{
    char buf[64];
    const char *mname;
    int n = 0;
    int i;
    const char *pfx = "Matrix - ";

    if (s_matrix_wnd == NULL) {
        return;
    }
    mname = matrix_mode_name((int)s_matrix_st.mode);
    for (i = 0; pfx[i] != '\0' && n < 50; i++) {
        buf[n++] = pfx[i];
    }
    for (i = 0; mname[i] != '\0' && n < 48; i++) {
        buf[n++] = mname[i];
    }
    if (s_show_fps != 0u && s_fps_val > 0u) {
        uint32_t f = (s_fps_val > 999u) ? 999u : s_fps_val;
        char d0 = (char)('0' + ((f / 100u) % 10u));
        char d1 = (char)('0' + ((f / 10u)  % 10u));
        char d2 = (char)('0' + (f % 10u));
        if (d0 == '0') { d0 = ' '; }
        if (d0 == ' ' && d1 == '0') { d1 = ' '; }
        buf[n++] = ' '; buf[n++] = ' ';
        buf[n++] = d0;  buf[n++] = d1;  buf[n++] = d2;
        buf[n++] = ' '; buf[n++] = 'F'; buf[n++] = 'P'; buf[n++] = 'S';
    }
    buf[n] = '\0';
    wnd_set_title(s_matrix_wnd, buf);
}

static void matrix_fps_tick(void)
{
    uint32_t now = btron_render_perf_us();
    uint32_t elapsed;
    uint32_t ms;

    if (now == 0u) {
        return;
    }
    if (s_fps_open == 0u) {
        s_fps_open   = 1u;
        s_fps_win_us = now;
        s_fps_frames = 0u;
    }
    s_fps_frames++;
    elapsed = now - s_fps_win_us;
    if (elapsed < 500000u && s_fps_frames < 60u) {
        return;
    }
    ms = elapsed / 1000u;
    if (ms == 0u) { ms = 1u; }
    s_fps_val    = (s_fps_frames * 1000u) / ms;
    s_fps_frames = 0u;
    s_fps_win_us = now;
    if (s_show_fps != 0u) {
        matrix_update_title();
    }
}

static void matrix_toggle_fullscreen(WND *wnd)
{
    GDEV *scr;
    if (wnd == NULL) {
        return;
    }
    scr = wnd_mgr_get_screen();
    if (scr == NULL || scr->width <= 0 || scr->height <= 0) {
        return;
    }
    if (s_fullscreen == 0u) {
        s_saved_bounds = wnd->bounds;
        s_fullscreen = 1u;
        mov_wnd(wnd, 0, 0);
        rsz_wnd(wnd, scr->width, scr->height);
    } else {
        H sw = (H)(s_saved_bounds.right - s_saved_bounds.left);
        H sh = (H)(s_saved_bounds.bottom - s_saved_bounds.top);
        s_fullscreen = 0u;
        if (sw < 160) { sw = 640; }
        if (sh < 120) { sh = 480; }
        mov_wnd(wnd, s_saved_bounds.left, s_saved_bounds.top);
        rsz_wnd(wnd, sw, sh);
    }
    s_matrix_st.full_redraw = 1u;
    inval_wnd(wnd);
}

static void matrix_paint(WND *wnd, GDEV *dev)
{
    SYSTIME now = 0;
    uint32_t dt_ms = 16u;

    if (wnd == NULL || dev == NULL || dev->pixels == NULL) {
        return;
    }
    if (s_st_ready == 0u) {
        matrix_sim_init(&s_matrix_st, MATRIX_MODE_CLASSIC, (int)dev->width, (int)dev->height);
        s_st_ready = 1u;
    } else if (s_matrix_st.view_w != (int)dev->width || s_matrix_st.view_h != (int)dev->height) {
        matrix_sim_resize(&s_matrix_st, (int)dev->width, (int)dev->height);
    }

    get_tim(&now);
    if (now > 0 && s_last_tick_ms > 0 && now >= s_last_tick_ms) {
        uint64_t diff = (uint64_t)(now - s_last_tick_ms);
        dt_ms = (diff > 100u) ? 100u : (uint32_t)diff;
    }
    if (now > 0) {
        s_last_tick_ms = now;
    }

    matrix_sim_step(&s_matrix_st, dt_ms);
    matrix_render_argb32(&s_matrix_st, (uint32_t *)dev->pixels, (int)dev->width, (int)dev->height);
    matrix_fps_tick();
}

static void matrix_event(WND *wnd, const EVT *evt)
{
    if (wnd == NULL || evt == NULL) {
        return;
    }
    if (evt->type == EV_KEY_DOWN) {
        int k = evt->key;
        if (k == 0x1B) {
            if (s_fullscreen != 0u) {
                matrix_toggle_fullscreen(wnd);
            } else {
                cls_wnd(wnd);
            }
            return;
        }
        if (k == 'q' || k == 'Q') {
            cls_wnd(wnd);
            return;
        }
        if (k == 'f' || k == 'F') {
            matrix_toggle_fullscreen(wnd);
            return;
        }
        if (k >= '1' && k <= '6') {
            matrix_sim_set_mode(&s_matrix_st, (MatrixMode)(k - '1'));
            matrix_update_title();
            inval_wnd(wnd);
            return;
        }
        if (k == '\t' || k == 'l' || k == 'L') {
            int next = ((int)s_matrix_st.mode + 1) % (int)MATRIX_MODE_COUNT;
            matrix_sim_set_mode(&s_matrix_st, (MatrixMode)next);
            matrix_update_title();
            inval_wnd(wnd);
            return;
        }
        if (k == 'h' || k == 'H') {
            int prev = ((int)s_matrix_st.mode + (int)MATRIX_MODE_COUNT - 1) % (int)MATRIX_MODE_COUNT;
            matrix_sim_set_mode(&s_matrix_st, (MatrixMode)prev);
            matrix_update_title();
            inval_wnd(wnd);
            return;
        }
        if (k == ' ') {
            s_matrix_st.paused = (s_matrix_st.paused == 0u) ? 1u : 0u;
            return;
        }
        if (k == 'b' || k == 'B') {
            s_matrix_st.bloom_on = (s_matrix_st.bloom_on == 0u) ? 1u : 0u;
            s_matrix_st.full_redraw = 1u;
            inval_wnd(wnd);
            return;
        }
        if (k == 'i' || k == 'I') {
            s_show_fps = (s_show_fps == 0u) ? 1u : 0u;
            matrix_update_title();
            return;
        }
        if (k == '+' || k == '=') {
            int cur = (s_matrix_st.font_px_override > 0) ? s_matrix_st.font_px_override : (int)s_matrix_st.atlas.cell_w;
            if (cur < (int)MATRIX_MAX_CELL_W) {
                s_matrix_st.font_px_override = cur + 2;
                matrix_sim_resize(&s_matrix_st, s_matrix_st.view_w, s_matrix_st.view_h);
                inval_wnd(wnd);
            }
            return;
        }
        if (k == '-' || k == '_') {
            int cur = (s_matrix_st.font_px_override > 0) ? s_matrix_st.font_px_override : (int)s_matrix_st.atlas.cell_w;
            if (cur > 8) {
                s_matrix_st.font_px_override = cur - 2;
                matrix_sim_resize(&s_matrix_st, s_matrix_st.view_w, s_matrix_st.view_h);
                inval_wnd(wnd);
            }
            return;
        }
    } else if (evt->type == EV_BUT_DOWN) {
        int lx = (int)evt->pos.x - (int)wnd->client.left;
        int ly = (int)evt->pos.y - (int)wnd->client.top;
        matrix_sim_trigger_ripple(&s_matrix_st, lx, ly);
        inval_wnd(wnd);
    }
}

static void matrix_destroy(WND *wnd)
{
    (void)wnd;
    s_matrix_wnd = NULL;
    s_fullscreen = 0u;
    if (s_matrix_tskid > 0) {
        wup_tsk(s_matrix_tskid);
    }
}

static void matrix_task_fn(void *exinf)
{
    (void)exinf;
    while (s_matrix_wnd != NULL) {
        if (s_matrix_wnd->visible && s_matrix_st.paused == 0u) {
            inval_wnd(s_matrix_wnd);
        }
        dly_tsk(16); /* ~60 FPS pacing with minimal idle CPU */
    }
    s_matrix_tskid = 0;
    ext_tsk();
}

WND *open_matrix_window_rect(int x, int y, int w, int h)
{
    if (s_matrix_wnd != NULL) {
        top_wnd(s_matrix_wnd);
        return s_matrix_wnd;
    }
    if (w <= 0 || h <= 0) {
        x = 120; y = 60; w = 640; h = 480;
    }

    s_matrix_wnd = opn_wnd("Matrix - Classic", (H)x, (H)y, (H)w, (H)h,
                           WND_ATTR_TITLE | WND_ATTR_BORDER | WND_ATTR_CLOSE |
                           WND_ATTR_MAX | WND_ATTR_RESIZE);
    if (s_matrix_wnd == NULL) {
        uart_puts_raw("[MATRIX] ERROR: opn_wnd failed\n");
        return NULL;
    }

    s_matrix_wnd->paint         = matrix_paint;
    s_matrix_wnd->event_handler = matrix_event;
    s_matrix_wnd->destroy       = matrix_destroy;

    s_fullscreen   = 0u;
    s_fps_open     = 0u;
    s_fps_val      = 0u;
    s_last_tick_ms = 0;
    if (s_matrix_wnd->dev != NULL) {
        matrix_sim_init(&s_matrix_st, MATRIX_MODE_CLASSIC,
                        (int)s_matrix_wnd->dev->width,
                        (int)s_matrix_wnd->dev->height);
        s_st_ready = 1u;
    }
    matrix_update_title();

    {
        T_CTSK ctsk;
        ctsk.exinf   = 0;
        ctsk.tskatr  = TA_HLNG;
        ctsk.task    = matrix_task_fn;
        ctsk.itskpri = 10;
        ctsk.stksz   = 16384;
        ctsk.name    = "matrix_task";
        s_matrix_tskid = cre_tsk(&ctsk);
        if (s_matrix_tskid > 0) {
            sta_tsk(s_matrix_tskid, 0);
        }
    }

    inval_wnd(s_matrix_wnd);
    uart_puts_raw("[MATRIX] Digital Rain screensaver opened (Rezmason/matrix port)\n");
    return s_matrix_wnd;
}

WND *open_matrix_window(void)
{
    return open_matrix_window_rect(-1, -1, -1, -1);
}

WND *open_matrix_window_mode(int mode)
{
    WND *w = open_matrix_window();
    if (w != NULL && mode >= 0 && mode < (int)MATRIX_MODE_COUNT) {
        matrix_sim_set_mode(&s_matrix_st, (MatrixMode)mode);
        matrix_update_title();
        inval_wnd(w);
    }
    return w;
}
