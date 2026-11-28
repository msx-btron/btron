/*
 * B-System (BTRON 3.20) — src/media/synth/synth_app.c
 * Native B-System Synth Visualizer Window (HAL-9000 / Music.html Conformance)
 * Pure C99, NASA/JPL Power of Ten, zero heap allocation.
 */

#include <media/synth.h>
#include <btron/bsf.h>
#include <btron/apps.h>
#include <btron/t_editor.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/libc_shim.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define SYNTH_WND_W     760
#define SYNTH_WND_H     520
#define SYNTH_MENU_H    21
#define SYNTH_TOP_H     38
#define SYNTH_BOT_H     88
#define SYNTH_PLOT_COLS 320

/* HAL-9000 / Music.html Exact Vector CRT Palette */
#define HAL_COL_BG      0xFF050609u
#define HAL_COL_PANEL   0xFF0A0D13u
#define HAL_COL_PAPER   0xFFE6E1D6u
#define HAL_COL_AMBER   0xFFE8A454u
#define HAL_COL_CYAN    0xFF7FD4CCu
#define HAL_COL_RED     0xFFD93829u
#define HAL_COL_RED_HI  0xFFFF8877u
#define HAL_COL_DIM     0xFF6B6862u
#define HAL_COL_LINE    0xFF2E2418u
#define HAL_COL_GRID    0xFF141922u
#define HAL_COL_GRID_C4 0xFF283242u
#define HAL_COL_PILL_BG 0xFF0F131Cu

/* 12-Voice Vector Palette (1:1 with Music.html V_COLS) */
static const COLOR k_vcols[BSF_MAX_VOICES] = {
    0xFFE8A454u, /* amber */
    0xFF7FD4CCu, /* cyan */
    0xFFFF6B57u, /* hal coral */
    0xFF9BC5FFu, /* ice blue */
    0xFFD9E67Au, /* lime gold */
    0xFFE394FFu, /* violet */
    0xFFF4EFE6u, /* paper white */
    0xFF5CE09Eu, /* emerald */
    0xFFFFB870u, /* warm peach */
    0xFF7AA2FFu, /* cobalt */
    0xFFFF8FB1u, /* rose */
    0xFFA3F7BFu  /* mint */
};

/* Weak hooks to sync with Cassette Deck & Editor */
__attribute__((weak)) void cassette_notify_bsf_state(void) {}

static WND          *s_synth_wnd = NULL;
static APP_MENU_BAR  s_menu;
static int           s_menu_ok = 0;
static bool          s_scrubbing = false;
static bool          s_plot_dirty = true;
static int           s_plot_track = -1;
static int           s_plot_part  = -1;

/* Pre-evaluated analytical curves y[col][voice] + per-voice mode & echo parameters */
static fix16_t       s_plot_y[SYNTH_PLOT_COLS][BSF_MAX_VOICES];
static uint8_t       s_plot_modes[BSF_MAX_VOICES];
static float         s_plot_echo_sec[BSF_MAX_VOICES];
static float         s_plot_echo_fb[BSF_MAX_VOICES];
static float         s_plot_echo_wet[BSF_MAX_VOICES];
static int           s_plot_voices = 0;

/* Menu Command IDs */
enum {
    SCMD_NONE = 0,
    SCMD_DECK = 1,
    SCMD_EDIT_TRACK = 2,
    SCMD_RELOAD = 3,
    SCMD_CLOSE = 4,
    SCMD_PLAY_TOGGLE = 10,
    SCMD_STOP = 18,
    SCMD_PREV_TRACK = 11,
    SCMD_NEXT_TRACK = 12,
    SCMD_PREV_PART = 13,
    SCMD_NEXT_PART = 14,
    SCMD_AUTO_ADV = 15,
    SCMD_MUTE = 16,
    SCMD_ABOUT = 17,
    SCMD_PRIM_BASE = 100, /* 100..114 */
    SCMD_SHIM_BASE = 200  /* 200..215 */
};

static void synth_rebuild_plot(void) {
    const BsfTelemetry *t = bsf_get_telemetry();
    if (!t) return;
    s_plot_voices = 0;
    for (int v = 0; v < BSF_MAX_VOICES; v++) {
        s_plot_modes[v]    = BSF_MODE_SINE;
        s_plot_echo_sec[v] = 0.0f;
        s_plot_echo_fb[v]  = 0.0f;
        s_plot_echo_wet[v] = 0.0f;
    }
    for (int col = 0; col < SYNTH_PLOT_COLS; col++) {
        /* x in [-8.0, +8.0] */
        float xf = -8.0f + (16.0f * (float)col) / (float)(SYNTH_PLOT_COLS - 1);
        fix16_t x = fix16_from_float(xf);
        int nv = bsf_eval_preview(x, t->part_idx, s_plot_y[col],
                                  (col == 0) ? s_plot_modes : NULL, BSF_MAX_VOICES);
        if (nv > s_plot_voices) s_plot_voices = nv;
        if (col == 0) {
            for (int v = 0; v < BSF_MAX_VOICES; v++) {
                bsf_get_preview_echo(v, &s_plot_echo_sec[v], &s_plot_echo_fb[v], &s_plot_echo_wet[v]);
            }
        }
    }
    s_plot_track = t->track_idx;
    s_plot_part  = t->part_idx;
    s_plot_dirty = false;
}

void synth_notify_track_changed(void) {
    s_plot_dirty = true;
    if (s_synth_wnd) inval_wnd(s_synth_wnd);
}

void synth_ui_tick(void) {
    if (!s_synth_wnd) return;
    const BsfTelemetry *t = bsf_get_telemetry();
    if (t && (t->track_idx != s_plot_track || t->part_idx != s_plot_part)) {
        s_plot_dirty = true;
    }
    if (t && t->playing) {
        inval_wnd(s_synth_wnd);
    }
}

static void synth_menu_build(void) {
    if (s_menu_ok) return;
    app_menu_init(&s_menu, APP_MENU_STYLE_CLASSIC_3D);

    int h = app_menu_add_header(&s_menu, "File(F)", 68);
    (void)app_menu_add_item(&s_menu, h, "Audio Console (C)", "C", SCMD_DECK, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Edit Track .bsf (E)", "E", SCMD_EDIT_TRACK, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Reload & Rerun (R)", "R", SCMD_RELOAD, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Close", "", SCMD_CLOSE, TRUE);

    h = app_menu_add_header(&s_menu, "Primitives(P)", 106);
    int np = bsf_get_prim_count();
    for (int i = 0; i < np && i < 15; i++) {
        const BsfLibItem *pi = bsf_get_prim_info(i);
        if (!pi) continue;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "%02d %s (%s)", pi->id, pi->marker, pi->name);
        (void)app_menu_add_item(&s_menu, h, lbl, "", SCMD_PRIM_BASE + i, TRUE);
    }

    h = app_menu_add_header(&s_menu, "Shims(S)", 78);
    int ns = bsf_get_shim_count();
    for (int i = 0; i < ns && i < 16; i++) {
        const BsfLibItem *si = bsf_get_shim_info(i);
        if (!si) continue;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "%02d %s", si->id, si->name);
        (void)app_menu_add_item(&s_menu, h, lbl, "", SCMD_SHIM_BASE + i, TRUE);
    }

    h = app_menu_add_header(&s_menu, "Transport(T)", 98);
    (void)app_menu_add_item(&s_menu, h, "Play / Pause", "Space", SCMD_PLAY_TOGGLE, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Stop", "", SCMD_STOP, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Prev Track", "Left", SCMD_PREV_TRACK, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Next Track", "Right", SCMD_NEXT_TRACK, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Prev Part", "Up", SCMD_PREV_PART, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Next Part", "Down", SCMD_NEXT_PART, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Auto-Advance", "A", SCMD_AUTO_ADV, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Mute", "M", SCMD_MUTE, TRUE);

    h = app_menu_add_header(&s_menu, "Options(O)", 90);
    (void)app_menu_add_item(&s_menu, h, "About Synth...", "", SCMD_ABOUT, TRUE);

    s_menu_ok = 1;
}

static void synth_open_file_in_editor(const char *vfs_path) {
    if (!vfs_path || !vfs_path[0]) return;
    (void)open_t_editor_window_with_file(vfs_path);
}

static void synth_exec_cmd(int cmd) {
    const BsfTelemetry *t = bsf_get_telemetry();
    int ntracks = bsf_get_track_count();

    if (cmd >= SCMD_PRIM_BASE && cmd < SCMD_PRIM_BASE + BSF_MAX_PRIMS) {
        const BsfLibItem *pi = bsf_get_prim_info(cmd - SCMD_PRIM_BASE);
        if (pi) synth_open_file_in_editor(pi->filepath);
        return;
    }
    if (cmd >= SCMD_SHIM_BASE && cmd < SCMD_SHIM_BASE + BSF_MAX_SHIMS) {
        const BsfLibItem *si = bsf_get_shim_info(cmd - SCMD_SHIM_BASE);
        if (si) synth_open_file_in_editor(si->filepath);
        return;
    }

    switch (cmd) {
        case SCMD_DECK:
            if (open_audio_player_window) (void)open_audio_player_window();
            break;
        case SCMD_EDIT_TRACK:
            if (t) {
                const BsfTrackInfo *tr = bsf_get_track_info(t->track_idx);
                if (tr) synth_open_file_in_editor(tr->filepath);
            }
            break;
        case SCMD_RELOAD:
            (void)bsf_reload();
            s_plot_dirty = true;
            cassette_notify_bsf_state();
            break;
        case SCMD_CLOSE:
            if (s_synth_wnd) cls_wnd(s_synth_wnd);
            return;
        case SCMD_PLAY_TOGGLE:
            bsf_toggle_play();
            cassette_notify_bsf_state();
            break;
        case SCMD_STOP:
            bsf_stop();
            cassette_notify_bsf_state();
            break;
        case SCMD_PREV_TRACK:
            if (t && ntracks > 0) {
                int nt = (t->track_idx - 1 + ntracks) % ntracks;
                (void)bsf_select_track(nt, 0);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_NEXT_TRACK:
            if (t && ntracks > 0) {
                int nt = (t->track_idx + 1) % ntracks;
                (void)bsf_select_track(nt, 0);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_PREV_PART:
            if (t && t->part_count > 0) {
                int np = (t->part_idx - 1 + t->part_count) % t->part_count;
                (void)bsf_set_part(np);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_NEXT_PART:
            if (t && t->part_count > 0) {
                int np = (t->part_idx + 1) % t->part_count;
                (void)bsf_set_part(np);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_AUTO_ADV:
            bsf_toggle_auto_advance();
            break;
        case SCMD_MUTE:
            bsf_toggle_mute();
            break;
        case SCMD_ABOUT:
            (void)open_synth_about_window();
            break;
        default:
            break;
    }
    if (s_synth_wnd) inval_wnd(s_synth_wnd);
}

/* Pill button layout helper */
static void get_top_pills(H w, RECT *r_play, RECT *r_deck, RECT *r_edit, RECT *r_run) {
    H y0 = SYNTH_MENU_H + 8;
    H y1 = SYNTH_MENU_H + 28;
    H x = w - 10;
    *r_run  = (RECT){ (H)(x - 44), y0, x, y1 }; x = (H)(x - 50);
    *r_edit = (RECT){ (H)(x - 48), y0, x, y1 }; x = (H)(x - 54);
    *r_deck = (RECT){ (H)(x - 48), y0, x, y1 }; x = (H)(x - 54);
    *r_play = (RECT){ (H)(x - 68), y0, x, y1 };
}

static void draw_pill(GDEV *dev, const RECT *r, const char *lbl, COLOR fg, COLOR border) {
    fill_rec(dev, r, HAL_COL_PILL_BG);
    set_col(dev, border, HAL_COL_PILL_BG);
    drw_rec(dev, r);
    drw_tc_string(dev, (H)(r->left + 6), (H)(r->top + 3), lbl, fg, HAL_COL_PILL_BG);
}

static H y_to_py(fix16_t y, H plot_top, H plot_h) {
    float yf = fix16_to_float(y);
    /* Y_MIN = -3.1, Y_MAX = +3.1 (matches Music.html) */
    float norm = (3.1f - yf) / 6.2f;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return (H)(plot_top + (int)(norm * (float)plot_h));
}

/* Blend RGB color over HAL obsidian background for ghost previews & echo trails */
static COLOR blend_hal_col(COLOR fg, COLOR bg, float alpha) {
    if (alpha <= 0.0f) return bg;
    if (alpha >= 1.0f) return fg;
    uint32_t fr = (fg >> 16) & 0xFFu, fg_ = (fg >> 8) & 0xFFu, fb = fg & 0xFFu;
    uint32_t br = (bg >> 16) & 0xFFu, bg_ = (bg >> 8) & 0xFFu, bb = bg & 0xFFu;
    uint32_t r = (uint32_t)((float)br + ((float)fr - (float)br) * alpha);
    uint32_t g = (uint32_t)((float)bg_ + ((float)fg_ - (float)bg_) * alpha);
    uint32_t b = (uint32_t)((float)bb + ((float)fb - (float)bb) * alpha);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Draw text truncated cleanly with "..." so it never overflows max_right */
static void synth_draw_clipped(GDEV *dev, H x, H y, H max_right, const char *s, COLOR fg, COLOR bg) {
    if (!s || !s[0] || max_right <= x + 8) return;
    int avail = (int)(max_right - x);
    int len = (int)strlen(s);
    if (tc_calc_string_width(s, len) <= avail) {
        drw_tc_string(dev, x, y, s, fg, bg);
        return;
    }
    char buf[256];
    int n = (len < (int)sizeof(buf) - 4) ? len : (int)sizeof(buf) - 4;
    while (n > 0) {
        memcpy(buf, s, (size_t)n);
        buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = '.'; buf[n + 3] = '\0';
        if (tc_calc_string_width(buf, n + 3) <= avail) break;
        n--;
    }
    if (n > 0) drw_tc_string(dev, x, y, buf, fg, bg);
}

/* Dotted / punctured line segment ([4 px on, 3 px off], matching synth-engine.js setLineDash([4, 3])) */
static void draw_dashed_seg(GDEV *dev, H x0, H y0, H x1, H y1, int *phase) {
    int dx = (int)x1 - (int)x0;
    int dy = (int)y1 - (int)y0;
    int steps = (dx < 0 ? -dx : dx);
    int ady = (dy < 0 ? -dy : dy);
    if (ady > steps) steps = ady;
    if (steps == 0) {
        if ((*phase % 7) < 4) drw_pnt(dev, x0, y0);
        (*phase)++;
        return;
    }
    for (int s = 0; s <= steps; s++) {
        if ((*phase % 7) < 4) {
            H px = (H)((int)x0 + (dx * s) / steps);
            H py = (H)((int)y0 + (dy * s) / steps);
            drw_pnt(dev, px, py);
        }
        (*phase)++;
    }
}

static void synth_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || dev->width < 80 || dev->height < 120) return;
    const BsfTelemetry *t = bsf_get_telemetry();
    const BsfTrackInfo *tr = t ? bsf_get_track_info(t->track_idx) : NULL;

    if (s_plot_dirty || (t && (t->track_idx != s_plot_track || t->part_idx != s_plot_part))) {
        synth_rebuild_plot();
    }

    H w = dev->width;
    H h = dev->height;

    /* 1. Obsidian Background */
    RECT full_r = { 0, 0, w, h };
    fill_rec(dev, &full_r, HAL_COL_BG);

    /* 2. Top HAL-9000 Telemetry Strip */
    H top_y = SYNTH_MENU_H;
    RECT top_r = { 0, top_y, w, (H)(top_y + SYNTH_TOP_H) };
    fill_rec(dev, &top_r, HAL_COL_PANEL);
    set_col(dev, HAL_COL_LINE, HAL_COL_PANEL);
    drw_lin(dev, 0, (H)(top_r.bottom - 1), w, (H)(top_r.bottom - 1));

    /* HAL 9000 Eye */
    RECT eye_r = { 12, (H)(top_y + 12), 24, (H)(top_y + 24) };
    fill_ovl(dev, &eye_r, (t && t->playing) ? HAL_COL_RED : 0xFF5A1610u);
    if (t && t->playing) {
        RECT eye_in = { 16, (H)(top_y + 15), 20, (H)(top_y + 19) };
        fill_ovl(dev, &eye_in, HAL_COL_RED_HI);
    }
    drw_tc_string(dev, 30, (H)(top_y + 6), "HAL 9000", HAL_COL_PAPER, HAL_COL_PANEL);
    drw_tc_string(dev, 30, (H)(top_y + 20), "BSF SYNTH", HAL_COL_AMBER, HAL_COL_PANEL);

    /* Quick Action Pills (compute first so title truncates cleanly before pills) */
    RECT r_play, r_deck, r_edit, r_run;
    get_top_pills(w, &r_play, &r_deck, &r_edit, &r_run);

    /* Track Title & Part Meta */
    if (tr && t) {
        H title_max = (H)(r_play.left - 10);
        char head[96];
        snprintf(head, sizeof(head), "%02d/%02d  %s", tr->id, bsf_get_track_count(), tr->title);
        synth_draw_clipped(dev, 130, (H)(top_y + 5), title_max, head, HAL_COL_PAPER, HAL_COL_PANEL);
        char sub[96];
        snprintf(sub, sizeof(sub), "PART %d/%d · %s", t->part_idx + 1, tr->part_count,
                 tr->part_names[t->part_idx]);
        synth_draw_clipped(dev, 130, (H)(top_y + 20), title_max, sub, HAL_COL_CYAN, HAL_COL_PANEL);
    }

    draw_pill(dev, &r_play, (t && t->playing) ? "AUDIO ON" : "PLAY",
              (t && t->playing) ? HAL_COL_RED_HI : HAL_COL_AMBER,
              (t && t->playing) ? HAL_COL_RED : HAL_COL_LINE);
    draw_pill(dev, &r_deck, "DECK", HAL_COL_CYAN, 0xFF234A48u);
    draw_pill(dev, &r_edit, "EDIT", HAL_COL_AMBER, HAL_COL_LINE);
    draw_pill(dev, &r_run,  "RUN",  HAL_COL_PAPER, HAL_COL_LINE);

    /* 3. Main Vector Plot Canvas */
    H plot_l = 36;
    H plot_r = (H)(w - 14);
    H plot_t = (H)(top_r.bottom + 8);
    H plot_b = (H)(h - SYNTH_BOT_H - 10);
    H plot_w = (H)(plot_r - plot_l);
    H plot_h = (H)(plot_b - plot_t);

    if (plot_w > 40 && plot_h > 40) {
        /* ISO-16 Pitch Grid (C2=-2, C3=-1, C4=0, C5=+1, C6=+2) */
        static const char *k_oct_lbl[5] = { "C6", "C5", "C4", "C3", "C2" };
        for (int oi = 0; oi < 5; oi++) {
            int oct = 2 - oi;
            H py = y_to_py(fix16_from_int(oct), plot_t, plot_h);
            set_col(dev, (oct == 0) ? HAL_COL_GRID_C4 : HAL_COL_GRID, HAL_COL_BG);
            drw_lin(dev, plot_l, py, plot_r, py);
            drw_tc_string(dev, 8, (H)(py - 6), k_oct_lbl[oi],
                          (oct == 0) ? HAL_COL_AMBER : HAL_COL_DIM, HAL_COL_BG);
        }
        /* Vertical beat grid (-8, -4, 0, +4, +8) */
        for (int xi = 0; xi <= 4; xi++) {
            H px = (H)(plot_l + (xi * plot_w) / 4);
            set_col(dev, HAL_COL_GRID, HAL_COL_BG);
            drw_lin(dev, px, plot_t, px, plot_b);
        }

        /* Constant always-visible A4 = 440 Hz dotted reference level (y = 9/12 = +0.75 oct, Music.html) */
        {
            H py_a4 = y_to_py(fix16_from_float(0.75f), plot_t, plot_h);
            COLOR a4_line = blend_hal_col(HAL_COL_AMBER, HAL_COL_BG, 0.24f);
            COLOR a4_lbl  = blend_hal_col(HAL_COL_AMBER, HAL_COL_BG, 0.52f);
            set_col(dev, a4_line, HAL_COL_BG);
            for (H px = plot_l; px < plot_r; px = (H)(px + 7)) {
                H ex = (H)(px + 2);
                if (ex > plot_r) ex = plot_r;
                drw_lin(dev, px, py_a4, ex, py_a4);
            }
            const char *a4_txt = "A4 · 440";
            int a4_w = tc_calc_string_width(a4_txt, (int)strlen(a4_txt));
            drw_tc_string(dev, (H)(plot_r - a4_w - 4), (H)(py_a4 - 12), a4_txt, a4_lbl, HAL_COL_BG);
        }

        /* Compute current playhead column index for active vs ghost preview & echo trails */
        int head_col = SYNTH_PLOT_COLS - 1;
        if (t) {
            float xf = fix16_to_float(t->x_pos);
            float xn = (xf + 8.0f) / 16.0f;
            if (xn < 0.0f) xn = 0.0f;
            if (xn > 1.0f) xn = 1.0f;
            head_col = (int)(xn * (float)(SYNTH_PLOT_COLS - 1));
        }
        float speed_f = tr ? fix16_to_float(tr->speed) : 2.0f;

        /* Plot Pre-Evaluated Multi-Voice Vector Curves + Dotted Per-Voice ECHO Trails */
        int nv = s_plot_voices;
        if (nv > BSF_MAX_VOICES) nv = BSF_MAX_VOICES;
        for (int v = 0; v < nv; v++) {
            /* 1) Shifted dotted/punctured ghost ECHO trails (k = 1..4 at +k * speed * echo_sec) */
            if (s_plot_echo_wet[v] > 0.005f && s_plot_echo_sec[v] > 0.005f) {
                int echo_step = (int)((speed_f * s_plot_echo_sec[v] / 16.0f) * (float)(SYNTH_PLOT_COLS - 1) + 0.5f);
                if (echo_step < 1) echo_step = 1;
                for (int k = 1; k <= 4; k++) {
                    int sh = k * echo_step;
                    if (sh >= SYNTH_PLOT_COLS) break;
                    float a_traced = s_plot_echo_wet[v] * powf(s_plot_echo_fb[v], (float)(k - 1)) * 1.10f;
                    if (a_traced < 0.08f) a_traced = 0.08f;
                    if (a_traced > 0.58f) a_traced = 0.58f;
                    COLOR col_active = blend_hal_col(k_vcols[v], HAL_COL_BG, a_traced);
                    COLOR col_ahead  = blend_hal_col(k_vcols[v], HAL_COL_BG, a_traced * 0.34f);

                    H prev_px = 0, prev_py = 0;
                    bool has_prev = false;
                    fix16_t prev_y = FIX16_QUIET;
                    int dash_phase = 0;
                    for (int c = sh; c < SYNTH_PLOT_COLS; c++) {
                        fix16_t y = s_plot_y[c - sh][v];
                        if (y == FIX16_QUIET) {
                            has_prev = false;
                            dash_phase = 0;
                            continue;
                        }
                        set_col(dev, (c <= head_col) ? col_active : col_ahead, HAL_COL_BG);
                        H px = (H)(plot_l + (c * plot_w) / (SYNTH_PLOT_COLS - 1));
                        H py = y_to_py(y, plot_t, plot_h);
                        if (has_prev && fix16_abs(y - prev_y) < fix16_from_float(0.65f)) {
                            draw_dashed_seg(dev, prev_px, prev_py, px, py, &dash_phase);
                        } else {
                            if ((dash_phase % 7) < 4) drw_pnt(dev, px, py);
                            dash_phase++;
                        }
                        prev_px = px;
                        prev_py = py;
                        prev_y  = y;
                        has_prev = true;
                    }
                }
            }

            /* 2) Primary dry curve (bright behind playhead, faint ghost ahead) */
            COLOR col_ghost = blend_hal_col(k_vcols[v], HAL_COL_BG, 0.24f);
            H prev_px = 0, prev_py = 0;
            bool has_prev = false;
            fix16_t prev_y = FIX16_QUIET;
            for (int c = 0; c < SYNTH_PLOT_COLS; c++) {
                fix16_t y = s_plot_y[c][v];
                if (y == FIX16_QUIET) {
                    has_prev = false;
                    continue;
                }
                set_col(dev, (c <= head_col) ? k_vcols[v] : col_ghost, HAL_COL_BG);
                H px = (H)(plot_l + (c * plot_w) / (SYNTH_PLOT_COLS - 1));
                H py = y_to_py(y, plot_t, plot_h);
                if (has_prev && fix16_abs(y - prev_y) < fix16_from_float(1.6f)) {
                    drw_lin(dev, prev_px, prev_py, px, py);
                } else {
                    drw_pnt(dev, px, py);
                }
                prev_px = px;
                prev_py = py;
                prev_y  = y;
                has_prev = true;
            }
        }

        /* Live Red Playhead Cursor, Active Voice Dots & Separate Hollow ECHO Rings */
        bool echo_live[BSF_MAX_VOICES];
        memset(echo_live, 0, sizeof(echo_live));
        if (t) {
            float xf = fix16_to_float(t->x_pos);
            float xn = (xf + 8.0f) / 16.0f;
            if (xn < 0.0f) xn = 0.0f;
            if (xn > 1.0f) xn = 1.0f;
            H cur_x = (H)(plot_l + (int)(xn * (float)plot_w));
            set_col(dev, HAL_COL_RED, HAL_COL_BG);
            drw_lin(dev, cur_x, plot_t, cur_x, plot_b);

            /* Progress hairline at top of canvas (Music.html #prog) */
            RECT prog_r = { 0, top_y, cur_x, (H)(top_y + 2) };
            fill_rec(dev, &prog_r, HAL_COL_RED);

            for (int v = 0; v < nv; v++) {
                fix16_t y = t->voice_y[v];
                if (y != FIX16_QUIET) {
                    H py = y_to_py(y, plot_t, plot_h);
                    RECT dot = { (H)(cur_x - 3), (H)(py - 3), (H)(cur_x + 3), (H)(py + 3) };
                    fill_ovl(dev, &dot, k_vcols[v]);
                }
                /* Separate live ECHO head rings on playhead when taps k=1..4 are sounding */
                if (s_plot_echo_wet[v] > 0.005f && s_plot_echo_sec[v] > 0.005f) {
                    int echo_step = (int)((speed_f * s_plot_echo_sec[v] / 16.0f) * (float)(SYNTH_PLOT_COLS - 1) + 0.5f);
                    if (echo_step < 1) echo_step = 1;
                    for (int k = 1; k <= 4; k++) {
                        int src_c = head_col - k * echo_step;
                        if (src_c < 0) break;
                        fix16_t ey = s_plot_y[src_c][v];
                        if (ey != FIX16_QUIET) {
                            echo_live[v] = true;
                            H epy = y_to_py(ey, plot_t, plot_h);
                            float ea = s_plot_echo_wet[v] * powf(s_plot_echo_fb[v], (float)(k - 1)) * 1.35f;
                            if (ea < 0.25f) ea = 0.25f;
                            if (ea > 0.90f) ea = 0.90f;
                            COLOR ec = blend_hal_col(k_vcols[v], HAL_COL_BG, ea);
                            set_col(dev, ec, HAL_COL_BG);
                            RECT r_out = { (H)(cur_x - 5), (H)(epy - 5), (H)(cur_x + 5), (H)(epy + 5) };
                            RECT r_in  = { (H)(cur_x - 2), (H)(epy - 2), (H)(cur_x + 2), (H)(epy + 2) };
                            drw_ovl(dev, &r_out);
                            drw_ovl(dev, &r_in);
                            break;
                        }
                    }
                }
            }
        }
    } else {
        /* Fallback when canvas is tiny */
    }

    /* 4. Bottom HAL-9000 Formula & DSP Telemetry Screen */
    RECT bot_r = { 10, (H)(h - SYNTH_BOT_H - 4), (H)(w - 10), (H)(h - 6) };
    fill_rec(dev, &bot_r, HAL_COL_PANEL);
    set_col(dev, HAL_COL_LINE, HAL_COL_PANEL);
    drw_rec(dev, &bot_r);

    if (t) {
        H pad_l = (H)(bot_r.left + 12);
        H pad_r = (H)(bot_r.right - 12);

        /* Row 1: Part + X/Hz/WCET */
        char r1_l[48], r1_r[80];
        snprintf(r1_l, sizeof(r1_l), "PART %d/%d  %s",
                 t->part_idx + 1, t->part_count, t->auto_advance ? "[AUTO]" : "[LOOP]");

        float hz0 = 261.63f;
        for (int v = 0; v < BSF_MAX_VOICES; v++) {
            if (t->voice_y[v] != FIX16_QUIET) {
                hz0 = fix16_to_float(fix16_y_to_hz(t->voice_y[v]));
                break;
            }
        }
        snprintf(r1_r, sizeof(r1_r), "X=%+.2f · %.1f HZ · WCET %u us",
                 fix16_to_float(t->x_pos), hz0, (unsigned)t->wcet_us);
        int r1_rw = tc_calc_string_width(r1_r, (int)strlen(r1_r));
        H r1_rx = (H)(pad_r - r1_rw);
        if (r1_rx < pad_l + 110) r1_rx = (H)(pad_l + 110);
        synth_draw_clipped(dev, pad_l, (H)(bot_r.top + 6), (H)(r1_rx - 10), r1_l, HAL_COL_AMBER, HAL_COL_PANEL);
        synth_draw_clipped(dev, r1_rx, (H)(bot_r.top + 6), pad_r, r1_r, HAL_COL_CYAN, HAL_COL_PANEL);

        /* Row 2: Active DSP Voice Badges (Solid Dry + Dotted ECHO xxms) + Stereo Peak/LIM Meter */
        H r2_y = (H)(bot_r.top + 26);
        H bar_w = 56;
        char mtxt[48];
        snprintf(mtxt, sizeof(mtxt), "PK %2d%% %s",
                 (int)(fmaxf(t->peak_l, t->peak_r) * 100.0f),
                 t->limiter_hit ? "[LIM]" : "LIM");
        int mtxt_w = tc_calc_string_width(mtxt, (int)strlen(mtxt));
        H mtxt_x = (H)(pad_r - mtxt_w);
        H bar_x  = (H)(mtxt_x - bar_w - 10);
        H max_cx = (H)(bar_x - 14);

        H cx = pad_l;
        drw_tc_string(dev, cx, r2_y, "DSP:", HAL_COL_CYAN, HAL_COL_PANEL);
        cx = (H)(cx + tc_calc_string_width("DSP:", 4) + 10);

        int show_v = (s_plot_voices > 0) ? s_plot_voices : t->voice_count;
        if (show_v > BSF_MAX_VOICES) show_v = BSF_MAX_VOICES;
        float spd = tr ? fix16_to_float(tr->speed) : 2.0f;
        int hc = SYNTH_PLOT_COLS - 1;
        {
            float xn = (fix16_to_float(t->x_pos) + 8.0f) / 16.0f;
            if (xn < 0.0f) xn = 0.0f;
            if (xn > 1.0f) xn = 1.0f;
            hc = (int)(xn * (float)(SYNTH_PLOT_COLS - 1));
        }

        bool has_rvb_voice = false;
        for (int v = 0; v < show_v; v++) {
            uint8_t vm = (s_plot_voices > 0) ? s_plot_modes[v] : t->voice_mode[v];
            if (vm == BSF_MODE_RVB) has_rvb_voice = true;
            /* Dry voice badge: solid line swatch + mode label (BSF_MODE_ECHO dry source is PNO) */
            const char *mname = (vm == BSF_MODE_ECHO) ? "PNO" : bsf_mode_name(vm);
            int m_w = 14 + tc_calc_string_width(mname, (int)strlen(mname));
            if (cx + m_w > max_cx) break;

            bool dry_on = t->voice_active[v];
            COLOR scol = dry_on ? k_vcols[v] : blend_hal_col(k_vcols[v], HAL_COL_PANEL, 0.45f);
            COLOR tcol = dry_on ? k_vcols[v] : HAL_COL_DIM;

            /* Solid 10px swatch */
            set_col(dev, scol, HAL_COL_PANEL);
            drw_lin(dev, cx, (H)(r2_y + 6), (H)(cx + 9), (H)(r2_y + 6));
            drw_lin(dev, cx, (H)(r2_y + 7), (H)(cx + 9), (H)(r2_y + 7));
            drw_tc_string(dev, (H)(cx + 14), r2_y, mname, tcol, HAL_COL_PANEL);
            cx = (H)(cx + m_w + 12);

            /* Separate dotted ECHO xxms badge when this voice has custom echo delay */
            if (s_plot_echo_wet[v] > 0.005f && s_plot_echo_sec[v] > 0.005f) {
                char elbl[20];
                snprintf(elbl, sizeof(elbl), "ECHO %dms", (int)(s_plot_echo_sec[v] * 1000.0f + 0.5f));
                int e_w = 14 + tc_calc_string_width(elbl, (int)strlen(elbl));
                if (cx + e_w > max_cx) break;

                bool e_on = false;
                int estep = (int)((spd * s_plot_echo_sec[v] / 16.0f) * (float)(SYNTH_PLOT_COLS - 1) + 0.5f);
                if (estep < 1) estep = 1;
                for (int k = 1; k <= 4; k++) {
                    int sc = hc - k * estep;
                    if (sc < 0) break;
                    if (s_plot_y[sc][v] != FIX16_QUIET) { e_on = true; break; }
                }
                COLOR ecol = e_on ? k_vcols[v] : blend_hal_col(k_vcols[v], HAL_COL_PANEL, 0.40f);
                COLOR etxt = e_on ? k_vcols[v] : HAL_COL_DIM;
                /* Dotted / punctured 10px swatch (3 dots: [0..1], [4..5], [8..9]) */
                set_col(dev, ecol, HAL_COL_PANEL);
                for (int d = 0; d < 10; d += 4) {
                    drw_lin(dev, (H)(cx + d), (H)(r2_y + 6), (H)(cx + d + 1), (H)(r2_y + 6));
                    drw_lin(dev, (H)(cx + d), (H)(r2_y + 7), (H)(cx + d + 1), (H)(r2_y + 7));
                }
                drw_tc_string(dev, (H)(cx + 14), r2_y, elbl, etxt, HAL_COL_PANEL);
                cx = (H)(cx + e_w + 12);
            }
        }
        if (tr && tr->reverb_mix > 0.005f && !has_rvb_voice) {
            char rlbl[16];
            snprintf(rlbl, sizeof(rlbl), "RVB %d%%", (int)(tr->reverb_mix * 100.0f + 0.5f));
            int r_w = 14 + tc_calc_string_width(rlbl, (int)strlen(rlbl));
            if (cx + r_w <= max_cx) {
                COLOR rcol = 0xFF9BC5FFu;
                set_col(dev, rcol, HAL_COL_PANEL);
                for (int d = 0; d < 10; d += 4) {
                    drw_lin(dev, (H)(cx + d), (H)(r2_y + 6), (H)(cx + d + 1), (H)(r2_y + 6));
                }
                drw_tc_string(dev, (H)(cx + 14), r2_y, rlbl, rcol, HAL_COL_PANEL);
            }
        }

        /* Stereo L/R Peak Bars (right-aligned) */
        if (bar_x > pad_l + 60) {
            RECT bl_bg = { bar_x, (H)(r2_y + 2), (H)(bar_x + bar_w), (H)(r2_y + 6) };
            RECT br_bg = { bar_x, (H)(r2_y + 8), (H)(bar_x + bar_w), (H)(r2_y + 12) };
            fill_rec(dev, &bl_bg, 0xFF1A1F28u);
            fill_rec(dev, &br_bg, 0xFF1A1F28u);
            int wl = (int)(t->peak_l * (float)bar_w); if (wl > bar_w) wl = bar_w;
            int wr = (int)(t->peak_r * (float)bar_w); if (wr > bar_w) wr = bar_w;
            if (wl > 0) {
                RECT fl = { bar_x, bl_bg.top, (H)(bar_x + wl), bl_bg.bottom };
                fill_rec(dev, &fl, (t->peak_l > 0.85f) ? HAL_COL_RED : HAL_COL_CYAN);
            }
            if (wr > 0) {
                RECT fr = { bar_x, br_bg.top, (H)(bar_x + wr), br_bg.bottom };
                fill_rec(dev, &fr, (t->peak_r > 0.85f) ? HAL_COL_RED : HAL_COL_AMBER);
            }
            drw_tc_string(dev, mtxt_x, r2_y, mtxt,
                          t->limiter_hit ? HAL_COL_RED_HI : HAL_COL_PAPER, HAL_COL_PANEL);
        }

        /* Row 3: Formula or BSF Compile Error */
        H r3_y = (H)(bot_r.top + 48);
        if (t->last_error[0] != '\0') {
            synth_draw_clipped(dev, pad_l, r3_y, pad_r, t->last_error, HAL_COL_RED_HI, HAL_COL_PANEL);
        } else if (tr && tr->formula[0] != '\0') {
            synth_draw_clipped(dev, pad_l, r3_y, pad_r, tr->formula, HAL_COL_PAPER, HAL_COL_PANEL);
        }
        synth_draw_clipped(dev, pad_l, (H)(bot_r.top + 66), pad_r,
                           "Space:Play  Left/Right:Track  Up/Down:Part  Drag:Scrub X  E:Edit .bsf  R:Reload  C:Console",
                           HAL_COL_DIM, HAL_COL_PANEL);
    }

    /* 5. Menu Bar & Dropdown on Top */
    if (s_menu_ok) {
        app_menu_paint_bar(&s_menu, dev);
        app_menu_paint_dropdown(&s_menu, dev);
    }
}

static BOOL synth_menu_is_open(WND *wnd) {
    (void)wnd;
    return (s_menu_ok && s_menu.active_menu >= 0) ? TRUE : FALSE;
}

static bool pt_in_rect(H x, H y, const RECT *r) {
    return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}

static void synth_scrub_at(WND *wnd, H rel_x) {
    H w = (H)(wnd->client.right - wnd->client.left);
    H plot_l = 36, plot_r = (H)(w - 14);
    if (plot_r <= plot_l) return;
    float norm = (float)(rel_x - plot_l) / (float)(plot_r - plot_l);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    bsf_set_x(fix16_from_float(-8.0f + norm * 16.0f));
    inval_wnd(wnd);
}

static void synth_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    H rel_x = (H)(evt->pos.x - wnd->client.left);
    H rel_y = (H)(evt->pos.y - wnd->client.top);
    H w = (H)(wnd->client.right - wnd->client.left);
    H h = (H)(wnd->client.bottom - wnd->client.top);
    int cmd = 0, sub = 0;

    if (evt->type == EV_MOUSE_MOVE) {
        if (s_scrubbing) {
            synth_scrub_at(wnd, rel_x);
            return;
        }
        if (s_menu_ok && app_menu_handle_mouse_move(&s_menu, rel_x, rel_y)) {
            inval_wnd(wnd);
        }
        return;
    }

    if (evt->type == EV_BUT_UP) {
        s_scrubbing = false;
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        UW k = evt->key;
        if (k == BTRON_KEY_SPACE || k == ' ') { synth_exec_cmd(SCMD_PLAY_TOGGLE); return; }
        if (k == BTRON_KEY_LEFT)  { synth_exec_cmd(SCMD_PREV_TRACK); return; }
        if (k == BTRON_KEY_RIGHT) { synth_exec_cmd(SCMD_NEXT_TRACK); return; }
        if (k == BTRON_KEY_UP)    { synth_exec_cmd(SCMD_PREV_PART); return; }
        if (k == BTRON_KEY_DOWN)  { synth_exec_cmd(SCMD_NEXT_PART); return; }
        if (k == 'e' || k == 'E') { synth_exec_cmd(SCMD_EDIT_TRACK); return; }
        if (k == 'r' || k == 'R') { synth_exec_cmd(SCMD_RELOAD); return; }
        if (k == 'c' || k == 'C') { synth_exec_cmd(SCMD_DECK); return; }
        if (k == 'm' || k == 'M') { synth_exec_cmd(SCMD_MUTE); return; }
        if (k == 'a' || k == 'A') { synth_exec_cmd(SCMD_AUTO_ADV); return; }
        if (k >= '1' && k <= '8') {
            (void)bsf_set_part((int)(k - '1'));
            s_plot_dirty = true;
            cassette_notify_bsf_state();
            inval_wnd(wnd);
            return;
        }
        if (s_menu_ok && app_menu_handle_key(&s_menu, k, (uint16_t)(uintptr_t)evt->data, &cmd)) {
            if (cmd > 0) synth_exec_cmd(cmd);
        }
        return;
    }

    if (evt->type != EV_BUT_DOWN) return;

    if (s_menu_ok && (rel_y < SYNTH_MENU_H || s_menu.active_menu >= 0)) {
        if (app_menu_handle_mouse_down(&s_menu, rel_x, rel_y, &cmd, &sub)) {
            if (cmd > 0) synth_exec_cmd(cmd);
            else if (s_synth_wnd) inval_wnd(wnd);
            return;
        }
    }

    /* Top Pill Clicks */
    RECT r_play, r_deck, r_edit, r_run;
    get_top_pills(w, &r_play, &r_deck, &r_edit, &r_run);
    if (pt_in_rect(rel_x, rel_y, &r_play)) { synth_exec_cmd(SCMD_PLAY_TOGGLE); return; }
    if (pt_in_rect(rel_x, rel_y, &r_deck)) { synth_exec_cmd(SCMD_DECK); return; }
    if (pt_in_rect(rel_x, rel_y, &r_edit)) { synth_exec_cmd(SCMD_EDIT_TRACK); return; }
    if (pt_in_rect(rel_x, rel_y, &r_run))  { synth_exec_cmd(SCMD_RELOAD); return; }

    /* Graph Scrubbing */
    H plot_t = (H)(SYNTH_MENU_H + SYNTH_TOP_H + 8);
    H plot_b = (H)(h - SYNTH_BOT_H - 10);
    if (rel_y >= plot_t && rel_y <= plot_b) {
        s_scrubbing = true;
        synth_scrub_at(wnd, rel_x);
    }
}

static void synth_destroy(WND *wnd) {
    (void)wnd;
    s_synth_wnd = NULL;
    s_scrubbing = false;
}

WND *open_synth_window(void) {
    if (s_synth_wnd) {
        top_wnd(s_synth_wnd);
        return s_synth_wnd;
    }
    (void)bsf_init(BSF_SAMPLE_RATE);

    H win_x = 460;
    H win_y = 60;
    s_synth_wnd = opn_wnd("HAL 9000 — BSF Synth Visualizer",
                          win_x, win_y, SYNTH_WND_W, SYNTH_WND_H,
                          WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (!s_synth_wnd) return NULL;

    synth_menu_build();
    s_synth_wnd->paint = synth_paint;
    s_synth_wnd->event_handler = synth_event_handler;
    s_synth_wnd->destroy = synth_destroy;
    s_synth_wnd->menu_open = synth_menu_is_open;
    s_plot_dirty = true;
    return s_synth_wnd;
}

WND *open_synth_about_window(void) {
    return app_menu_create_about_dialog("BSF Synth", "B-Sound Forth",
        "HAL-9000 Analytical & B-Sound Forth Visualizer",
        "15 Primitives · 16 Shims · 26 BSF Tracks (Q16.16)",
        300, 160);
}
