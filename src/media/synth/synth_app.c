/*
 * B-System (BTRON 3.20) — src/media/synth/synth_app.c
 * Native B-System Synth Visualizer Window (HAL-9000 / Music.html Conformance)
 * Pure C99, NASA/JPL Power of Ten, zero heap allocation.
 */

#include <media/synth.h>
#include <media/gst_rt.h>
#include <media/audio_nexus.h>
#include <btron/bsf.h>
#include <btron/apps.h>
#include <btron/t_editor.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/libc_shim.h>
#include "lilcu64_synth.h"
#include "lilcu64_score.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define SYNTH_WND_W     760
#define SYNTH_WND_H     520
#define SYNTH_MENU_H    21
#define SYNTH_TOP_H     38
#define SYNTH_BOT_H     88
#if defined(PICO_REAL_HW)
#define SYNTH_PLOT_COLS 160
#else
#define SYNTH_PLOT_COLS 320
#endif

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

/* C99 LilCu64 & Orchestra/SMF visualization sources */
static int           s_lc_track = 0;   /* 0 = BSF Forth, 1..4 = C99 LilCu64 */
static bool          s_orch_mode = false;
static char          s_orch_label[48] = "ORCHESTRA / SMF";
static int           s_lc_pages = 8;   /* Scalable page window count (4, 8, or 16 for 320x320 MCU) */
static int           s_lc_last_cap = -1;
static uint32_t      s_orch_last_ev = 0;

static uint8_t orch_kind_to_mode(uint8_t kind) {
    switch (kind) {
        case ORCH_KIND_PIANO:   return BSF_MODE_PNO;
        case ORCH_KIND_WADDLE:
        case ORCH_KIND_PEDAL:   return BSF_MODE_LPF;
        case ORCH_KIND_SNARE:   return BSF_MODE_NSE;
        case ORCH_KIND_HAT:
        case ORCH_KIND_ZAP:     return BSF_MODE_HPF;
        case ORCH_KIND_BASS:
        case ORCH_KIND_INDKICK: return BSF_MODE_SAT;
        case ORCH_KIND_SWARM:   return BSF_MODE_LFO;
        case ORCH_KIND_BELL:    return BSF_MODE_FM;
        default:                return BSF_MODE_SINE;
    }
}

static uint32_t orch_total_ticks(const OrchView *ov) {
    if (!ov) return 0u;
    uint32_t max_t = ov->loop_tick;
    if (max_t == 0u) {
        uint32_t n = ov->event_count;
        if (n > ORCH_MAX_EVENTS) n = ORCH_MAX_EVENTS;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t e = ov->ev[i].start_tick + (ov->ev[i].dur_tick ? ov->ev[i].dur_tick : (uint32_t)(ORCH_PPQ / 4));
            if (e > max_t) max_t = e;
        }
    }
    if (max_t < (uint32_t)(ORCH_PPQ * 4)) max_t = (uint32_t)(ORCH_PPQ * 4);
    return max_t;
}

static uint32_t orch_cur_tick_heard(const OrchView *ov) {
    AudioNexusTelem nt;
    if (audio_nexus_is_async() && audio_nexus_read_telem(&nt) && nt.active_engine == AUDIO_NEXUS_ENGINE_ORCH) {
        return nt.orch_tick_heard;
    }
    if (!ov || ov->sample_rate == 0u) return 0u;
    uint64_t q_samp = ((uint64_t)ov->queued_ms * (uint64_t)ov->sample_rate) / 1000ull;
    uint64_t hpos = (ov->npos > q_samp) ? (ov->npos - q_samp) : 0ull;
    return gst_tick_of_sample(hpos, ov->sample_rate, ov->tempo, ov->den);
}

static const char *const k_lc_titles[4] = {
    "CAROL OF THE BELLS (C99)",
    "GREEN CATERPILLAR (C99)",
    "HOUSE OF THE RISING SUN (C99)",
    "TRON INIT (C99)"
};

static uint8_t lc_shim_to_mode(int shim) {
    switch (shim) {
        case LC_SHIM_PIANO:     return BSF_MODE_PNO;
        case LC_SHIM_FROG:      return BSF_MODE_AM;
        case LC_SHIM_ADD_BELL:
        case LC_SHIM_DING:      return BSF_MODE_FM;
        case LC_SHIM_WADDLE:
        case LC_SHIM_PEDAL:
        case LC_SHIM_CELLO:     return BSF_MODE_LPF;
        case LC_SHIM_IND_BASS:
        case LC_SHIM_IND_KICK:
        case LC_SHIM_CYBER:     return BSF_MODE_SAT;
        case LC_SHIM_SNARE:
        case LC_SHIM_IND_SNARE: return BSF_MODE_NSE;
        case LC_SHIM_HAT:
        case LC_SHIM_ZAP:       return BSF_MODE_HPF;
        case LC_SHIM_SWARM:
        case LC_SHIM_VIOLIN:
        case LC_SHIM_SAX:       return BSF_MODE_LFO;
        case LC_SHIM_GUITAR:    return BSF_MODE_TRI;
        case LC_CAP_SWOOSH:     return BSF_MODE_RVB;
        default:                return BSF_MODE_SINE;
    }
}

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

static void synth_rebuild_plot_lilcu64(int track, int page) {
    for (int c = 0; c < SYNTH_PLOT_COLS; c++) {
        for (int v = 0; v < BSF_MAX_VOICES; v++) s_plot_y[c][v] = FIX16_QUIET;
    }
    for (int v = 0; v < BSF_MAX_VOICES; v++) {
        s_plot_modes[v]    = BSF_MODE_SINE;
        s_plot_echo_sec[v] = 0.0f;
        s_plot_echo_fb[v]  = 0.0f;
        s_plot_echo_wet[v] = 0.0f;
    }
    s_plot_voices = 0;

    double len = lc_score_length(track);
    if (len <= 0.0) { s_plot_dirty = false; return; }
    if (lilcu64_synth_get_cap_track() != track || lc_cap_count() == 0) {
        /* Capture populates on the next audio block; stay dirty until ready */
        s_plot_track = -1;
        s_plot_part = -1;
        s_plot_dirty = true;
        return;
    }

    int pages = (s_lc_pages >= 2) ? s_lc_pages : 8;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    double win_len = len / (double)pages;
    double t0 = (double)page * win_len;
    double t1 = t0 + win_len;

    int lane_shim[BSF_MAX_VOICES];
    int n_lanes = 0;
    int cap_n = lc_cap_count();
    for (int i = 0; i < cap_n; i++) {
        int shim = 0, art = 0;
        double et = 0.0;
        float hz = 0.0f, vol = 0.0f, dur = 0.0f;
        if (!lc_cap_at(i, &shim, &art, &et, &hz, &vol, &dur)) continue;
        (void)vol;
        /* Find or allocate visual lane by shim ID */
        int lane = -1;
        for (int l = 0; l < n_lanes; l++) {
            if (lane_shim[l] == shim) { lane = l; break; }
        }
        if (lane < 0) {
            if (n_lanes >= BSF_MAX_VOICES) continue;
            lane = n_lanes++;
            lane_shim[lane] = shim;
            s_plot_modes[lane] = lc_shim_to_mode(shim);
        }
        double e_end = et + (double)(dur > 0.04f ? dur : 0.04f);
        if (e_end < t0 || et > t1) continue;

        float yf;
        if (shim == LC_SHIM_SNARE || shim == LC_SHIM_IND_SNARE) {
            yf = 2.25f;
        } else if (shim == LC_SHIM_HAT || shim == LC_SHIM_ZAP) {
            yf = 2.75f;
        } else if (art == LC_CAP_MARK && hz <= 20.0f) {
            yf = -2.20f;
        } else if (hz > 16.0f) {
            yf = log2f(hz / 261.6256f);
            if (yf < -3.0f) yf = -3.0f;
            if (yf >  3.0f) yf =  3.0f;
        } else {
            continue;
        }
        fix16_t y16 = fix16_from_float(yf);
        int c0 = (int)(((et - t0) / win_len) * (double)(SYNTH_PLOT_COLS - 1));
        int c1 = (int)(((e_end - t0) / win_len) * (double)(SYNTH_PLOT_COLS - 1));
        if (c0 < 0) c0 = 0;
        if (c1 >= SYNTH_PLOT_COLS) c1 = SYNTH_PLOT_COLS - 1;
        for (int c = c0; c <= c1; c++) {
            s_plot_y[c][lane] = y16;
        }
    }
    s_plot_voices = n_lanes;
    s_plot_track  = track;
    s_plot_part   = page;
    s_lc_last_cap = cap_n;
    s_plot_dirty  = false;
}

static void synth_rebuild_plot_orch(int page) {
    for (int c = 0; c < SYNTH_PLOT_COLS; c++) {
        for (int v = 0; v < BSF_MAX_VOICES; v++) s_plot_y[c][v] = FIX16_QUIET;
    }
    for (int v = 0; v < BSF_MAX_VOICES; v++) {
        s_plot_modes[v]    = BSF_MODE_SINE;
        s_plot_echo_sec[v] = 0.0f;
        s_plot_echo_fb[v]  = 0.0f;
        s_plot_echo_wet[v] = 0.0f;
    }
    s_plot_voices = 0;

    OrchView *ov = gst_view();
    if (!ov) { s_plot_dirty = false; return; }
    uint32_t total = orch_total_ticks(ov);
    int pages = (s_lc_pages >= 2) ? s_lc_pages : 8;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    double win_len = (double)total / (double)pages;
    if (win_len < 1.0) win_len = 1.0;
    double t0 = (double)page * win_len;
    double t1 = t0 + win_len;

    int lane_trk[BSF_MAX_VOICES];
    int n_lanes = 0;
    uint32_t ev_n = ov->event_count;
    if (ev_n > ORCH_MAX_EVENTS) ev_n = ORCH_MAX_EVENTS;
    for (uint32_t i = 0; i < ev_n; i++) {
        const OrchEvent *e = &ov->ev[i];
        int trk = (int)e->track;
        int lane = -1;
        for (int l = 0; l < n_lanes; l++) {
            if (lane_trk[l] == trk) { lane = l; break; }
        }
        if (lane < 0) {
            if (n_lanes >= BSF_MAX_VOICES) continue;
            lane = n_lanes++;
            lane_trk[lane] = trk;
            uint8_t kind = (trk >= 0 && trk < ov->track_count && trk < ORCH_MAX_TRACKS)
                           ? ov->trk[trk].kind : ORCH_KIND_PHRASE;
            s_plot_modes[lane] = orch_kind_to_mode(kind);
        }
        double et = (double)e->start_tick;
        double ed = (double)(e->dur_tick > 30u ? e->dur_tick : 60u);
        double e_end = et + ed;
        if (e_end < t0 || et > t1) continue;

        float yf = ((float)e->key - 60.0f) / 12.0f;
        if (yf < -3.0f) yf = -3.0f;
        if (yf >  3.0f) yf =  3.0f;
        fix16_t y16 = fix16_from_float(yf);
        int c0 = (int)(((et - t0) / win_len) * (double)(SYNTH_PLOT_COLS - 1));
        int c1 = (int)(((e_end - t0) / win_len) * (double)(SYNTH_PLOT_COLS - 1));
        if (c0 < 0) c0 = 0;
        if (c1 >= SYNTH_PLOT_COLS) c1 = SYNTH_PLOT_COLS - 1;
        for (int c = c0; c <= c1; c++) {
            s_plot_y[c][lane] = y16;
        }
    }
    s_plot_voices  = n_lanes;
    s_plot_track   = -2; /* Orchestra sentinel */
    s_plot_part    = page;
    s_orch_last_ev = ev_n;
    s_plot_dirty   = false;
}

static void synth_rebuild_plot(void) {
    if (s_orch_mode) {
        OrchView *ov = gst_view();
        uint32_t total = orch_total_ticks(ov);
        int pages = (s_lc_pages >= 2) ? s_lc_pages : 8;
        int page = 0;
        if (total > 0u) {
            uint32_t tk = orch_cur_tick_heard(ov) % total;
            page = (int)(((double)tk / (double)total) * (double)pages);
            if (page < 0) page = 0;
            if (page >= pages) page = pages - 1;
        }
        synth_rebuild_plot_orch(page);
        return;
    }
    if (s_lc_track > 0) {
        double len = lc_score_length(s_lc_track);
        int pages = (s_lc_pages >= 2) ? s_lc_pages : 8;
        int page = 0;
        if (len > 0.0) {
            double t = (double)lilcu64_synth_get_track_time();
            page = (int)((t / len) * (double)pages);
            if (page < 0) page = 0;
            if (page >= pages) page = pages - 1;
        }
        synth_rebuild_plot_lilcu64(s_lc_track, page);
        return;
    }
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
    s_lc_track  = 0;
    s_orch_mode = false;
    s_plot_dirty = true;
    if (s_synth_wnd) inval_wnd(s_synth_wnd);
}

void synth_set_lilcu64_track(int track_num) {
    s_orch_mode = false;
    s_lc_track = (track_num >= 1 && track_num <= 4) ? track_num : 0;
    s_plot_track = -1;
    s_plot_part  = -1;
    s_plot_dirty = true;
    if (s_synth_wnd) inval_wnd(s_synth_wnd);
}

void synth_set_orch_mode(const char *label) {
    s_lc_track  = 0;
    s_orch_mode = true;
    if (label && label[0]) {
        strncpy(s_orch_label, label, sizeof(s_orch_label) - 1);
        s_orch_label[sizeof(s_orch_label) - 1] = '\0';
    } else {
        strncpy(s_orch_label, "ORCHESTRA / SMF", sizeof(s_orch_label) - 1);
    }
    s_plot_track = -1;
    s_plot_part  = -1;
    s_plot_dirty = true;
    if (s_synth_wnd) inval_wnd(s_synth_wnd);
}

void synth_ui_tick(void) {
    if (!s_synth_wnd) return;
    int lc_active = lilcu64_synth_get_active_track();
    OrchView *ov = gst_view();
    bool orch_playing = (ov && ov->state == ORCH_GST_PLAYING);

    if (!bsf_is_playing()) {
        if (lc_active >= 1 && lc_active <= 4) {
            if (s_orch_mode || s_lc_track != lc_active) {
                s_orch_mode = false;
                s_lc_track = lc_active;
                s_plot_track = -1;
                s_plot_part  = -1;
                s_plot_dirty = true;
            }
        } else if (orch_playing) {
            if (!s_orch_mode) {
                s_orch_mode = true;
                s_lc_track = 0;
                s_plot_track = -1;
                s_plot_part  = -1;
                s_plot_dirty = true;
            }
        }
    }
    if (s_orch_mode) {
        if (!orch_playing) return;
        uint32_t total = orch_total_ticks(ov);
        int pages = (s_lc_pages >= 2) ? s_lc_pages : 8;
        int page = 0;
        if (total > 0u) {
            uint32_t tk = orch_cur_tick_heard(ov) % total;
            page = (int)(((double)tk / (double)total) * (double)pages);
            if (page < 0) page = 0;
            if (page >= pages) page = pages - 1;
        }
        if (s_plot_track != -2 || s_plot_part != page || s_orch_last_ev != ov->event_count) {
            s_plot_dirty = true;
        }
        inval_wnd(s_synth_wnd);
        return;
    }
    if (s_lc_track > 0) {
        if (lc_active != s_lc_track) {
            return;
        }
        double len = lc_score_length(s_lc_track);
        int pages = (s_lc_pages >= 2) ? s_lc_pages : 8;
        int page = 0;
        if (len > 0.0) {
            double t = (double)lilcu64_synth_get_track_time();
            page = (int)((t / len) * (double)pages);
            if (page < 0) page = 0;
            if (page >= pages) page = pages - 1;
        }
        if (s_plot_track != s_lc_track || s_plot_part != page ||
            s_lc_last_cap != lc_cap_count()) {
            s_plot_dirty = true;
        }
        inval_wnd(s_synth_wnd);
        return;
    }
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
            if (s_lc_track > 0) {
                if (lilcu64_synth_get_active_track() == s_lc_track) lilcu64_synth_stop_track();
                else lilcu64_synth_play_track(s_lc_track);
            } else {
                bsf_toggle_play();
            }
            cassette_notify_bsf_state();
            break;
        case SCMD_STOP:
            if (s_lc_track > 0) lilcu64_synth_stop_track();
            else bsf_stop();
            cassette_notify_bsf_state();
            break;
        case SCMD_PREV_TRACK:
            if (s_lc_track > 0) {
                int nt = (s_lc_track > 1) ? (s_lc_track - 1) : 4;
                s_lc_track = nt;
                lilcu64_synth_play_track(nt);
                s_plot_dirty = true;
            } else if (t && ntracks > 0) {
                int nt = (t->track_idx - 1 + ntracks) % ntracks;
                (void)bsf_select_track(nt, 0);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_NEXT_TRACK:
            if (s_lc_track > 0) {
                int nt = (s_lc_track < 4) ? (s_lc_track + 1) : 1;
                s_lc_track = nt;
                lilcu64_synth_play_track(nt);
                s_plot_dirty = true;
            } else if (t && ntracks > 0) {
                int nt = (t->track_idx + 1) % ntracks;
                (void)bsf_select_track(nt, 0);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_PREV_PART:
            if (s_orch_mode || s_lc_track > 0) {
                /* Zoom out window scale (16 -> 8 -> 4 pages) */
                s_lc_pages = (s_lc_pages >= 16) ? 8 : 4;
                s_plot_dirty = true;
            } else if (t && t->part_count > 0) {
                int np = (t->part_idx - 1 + t->part_count) % t->part_count;
                (void)bsf_set_part(np);
                s_plot_dirty = true;
                cassette_notify_bsf_state();
            }
            break;
        case SCMD_NEXT_PART:
            if (s_orch_mode || s_lc_track > 0) {
                /* Zoom in window scale (4 -> 8 -> 16 pages, MCU 320x320 scale) */
                s_lc_pages = (s_lc_pages <= 4) ? 8 : 16;
                s_plot_dirty = true;
            } else if (t && t->part_count > 0) {
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
            if (s_lc_track > 0) lilcu64_synth_toggle_mute();
            else bsf_toggle_mute();
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
    bool lc_mode   = (!s_orch_mode && s_lc_track >= 1 && s_lc_track <= 4);
    bool orch_mode = s_orch_mode;
    bool win_mode  = (lc_mode || orch_mode);
    const BsfTelemetry *t = bsf_get_telemetry();
    const BsfTrackInfo *tr = (!win_mode && t) ? bsf_get_track_info(t->track_idx) : NULL;
    OrchView *ov = orch_mode ? gst_view() : NULL;
    bool compact = (dev->width <= 360 || dev->height <= 360);

    double lc_len = lc_mode ? lc_score_length(s_lc_track) : 0.0;
    double lc_t   = lc_mode ? (double)lilcu64_synth_get_track_time() : 0.0;
    uint32_t orch_total = orch_mode ? orch_total_ticks(ov) : 0u;
    uint32_t orch_tk    = (orch_mode && orch_total > 0u) ? (orch_cur_tick_heard(ov) % orch_total) : 0u;
    int lc_pages  = (s_lc_pages >= 2) ? s_lc_pages : 8;
    if (compact && lc_pages < 12) lc_pages = 12; /* Auto-zoom for 320x320 PicoCalc MCU */
    int lc_page   = 0;
    float lc_xn   = 0.0f;
    if (orch_mode && orch_total > 0u) {
        double win_len = (double)orch_total / (double)lc_pages;
        lc_page = (int)((double)orch_tk / win_len);
        if (lc_page < 0) lc_page = 0;
        if (lc_page >= lc_pages) lc_page = lc_pages - 1;
        lc_xn = (float)(((double)orch_tk - (double)lc_page * win_len) / win_len);
        if (lc_xn < 0.0f) lc_xn = 0.0f;
        if (lc_xn > 1.0f) lc_xn = 1.0f;
        if (s_plot_track != -2 || s_plot_part != lc_page) s_plot_dirty = true;
    } else if (lc_mode && lc_len > 0.0) {
        double win_len = lc_len / (double)lc_pages;
        lc_page = (int)(lc_t / win_len);
        if (lc_page < 0) lc_page = 0;
        if (lc_page >= lc_pages) lc_page = lc_pages - 1;
        lc_xn = (float)((lc_t - (double)lc_page * win_len) / win_len);
        if (lc_xn < 0.0f) lc_xn = 0.0f;
        if (lc_xn > 1.0f) lc_xn = 1.0f;
        if (s_plot_track != s_lc_track || s_plot_part != lc_page) s_plot_dirty = true;
    } else if (!win_mode && t && (t->track_idx != s_plot_track || t->part_idx != s_plot_part)) {
        s_plot_dirty = true;
    }

    if (s_plot_dirty) {
        synth_rebuild_plot();
    }

    bool is_playing = orch_mode ? (ov && ov->state == ORCH_GST_PLAYING)
                    : lc_mode   ? (lilcu64_synth_get_active_track() == s_lc_track)
                    : (t && t->playing);
    float pk_l = 0.0f, pk_r = 0.0f, rms_l = 0.0f, rms_r = 0.0f;
    bool lim_hit = false;
    if (orch_mode && ov) {
        pk_l = (float)ov->meter_l * 0.01f;
        pk_r = (float)ov->meter_r * 0.01f;
        lim_hit = (ov->master_lim != 0u);
    } else if (lc_mode) {
        lilcu64_synth_get_meters(&pk_l, &pk_r, &rms_l, &rms_r, &lim_hit);
    } else if (t) {
        pk_l = t->peak_l; pk_r = t->peak_r; rms_l = t->rms_l; rms_r = t->rms_r; lim_hit = t->limiter_hit;
    }
    (void)rms_l; (void)rms_r;

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
    fill_ovl(dev, &eye_r, is_playing ? HAL_COL_RED : 0xFF5A1610u);
    if (is_playing) {
        RECT eye_in = { 16, (H)(top_y + 15), 20, (H)(top_y + 19) };
        fill_ovl(dev, &eye_in, HAL_COL_RED_HI);
    }
    drw_tc_string(dev, 30, (H)(top_y + 6), "HAL 9000", HAL_COL_PAPER, HAL_COL_PANEL);
    drw_tc_string(dev, 30, (H)(top_y + 20),
                  orch_mode ? "ORCH / SMF" : (lc_mode ? "C99 SCORE" : "BSF SYNTH"),
                  HAL_COL_AMBER, HAL_COL_PANEL);

    /* Quick Action Pills (compute first so title truncates cleanly before pills) */
    RECT r_play, r_deck, r_edit, r_run;
    get_top_pills(w, &r_play, &r_deck, &r_edit, &r_run);

    /* Track Title & Part/Window Meta */
    {
        H title_max = (H)(r_play.left - 10);
        char head[96], sub[96];
        if (orch_mode && ov) {
            snprintf(head, sizeof(head), "%s  (%u TRK · %u BPM)",
                     s_orch_label, (unsigned)ov->track_count, (unsigned)ov->tempo);
            snprintf(sub, sizeof(sub), "WINDOW %d/%d · %u EVENTS · ISOLATED AUDIO CORE",
                     lc_page + 1, lc_pages, (unsigned)ov->event_count);
            synth_draw_clipped(dev, 130, (H)(top_y + 5), title_max, head, HAL_COL_PAPER, HAL_COL_PANEL);
            synth_draw_clipped(dev, 130, (H)(top_y + 20), title_max, sub, HAL_COL_CYAN, HAL_COL_PANEL);
        } else if (lc_mode) {
            double win_s = (lc_len > 0.0) ? (lc_len / (double)lc_pages) : 0.0;
            snprintf(head, sizeof(head), "C99 %02d/04  %s", s_lc_track, k_lc_titles[s_lc_track - 1]);
            snprintf(sub, sizeof(sub), "WINDOW %d/%d (%.1fs) · LILCU64 HARDCODED SCORE",
                     lc_page + 1, lc_pages, win_s);
            synth_draw_clipped(dev, 130, (H)(top_y + 5), title_max, head, HAL_COL_PAPER, HAL_COL_PANEL);
            synth_draw_clipped(dev, 130, (H)(top_y + 20), title_max, sub, HAL_COL_CYAN, HAL_COL_PANEL);
        } else if (tr && t) {
            snprintf(head, sizeof(head), "%02d/%02d  %s", tr->id, bsf_get_track_count(), tr->title);
            snprintf(sub, sizeof(sub), "PART %d/%d · %s", t->part_idx + 1, tr->part_count,
                     tr->part_names[t->part_idx]);
            synth_draw_clipped(dev, 130, (H)(top_y + 5), title_max, head, HAL_COL_PAPER, HAL_COL_PANEL);
            synth_draw_clipped(dev, 130, (H)(top_y + 20), title_max, sub, HAL_COL_CYAN, HAL_COL_PANEL);
        }
    }

    draw_pill(dev, &r_play, is_playing ? "AUDIO ON" : "PLAY",
              is_playing ? HAL_COL_RED_HI : HAL_COL_AMBER,
              is_playing ? HAL_COL_RED : HAL_COL_LINE);
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
        float cur_xn = 0.0f;
        if (win_mode) {
            cur_xn = lc_xn;
            head_col = (int)(cur_xn * (float)(SYNTH_PLOT_COLS - 1));
        } else if (t) {
            float xf = fix16_to_float(t->x_pos);
            cur_xn = (xf + 8.0f) / 16.0f;
            if (cur_xn < 0.0f) cur_xn = 0.0f;
            if (cur_xn > 1.0f) cur_xn = 1.0f;
            head_col = (int)(cur_xn * (float)(SYNTH_PLOT_COLS - 1));
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
        if (win_mode || t) {
            H cur_x = (H)(plot_l + (int)(cur_xn * (float)plot_w));
            set_col(dev, HAL_COL_RED, HAL_COL_BG);
            drw_lin(dev, cur_x, plot_t, cur_x, plot_b);

            /* Progress hairline at top of canvas (Music.html #prog) */
            RECT prog_r = { 0, top_y, cur_x, (H)(top_y + 2) };
            fill_rec(dev, &prog_r, HAL_COL_RED);

            for (int v = 0; v < nv; v++) {
                fix16_t y = win_mode ? s_plot_y[head_col][v] : t->voice_y[v];
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

    if (win_mode || t) {
        H pad_l = (H)(bot_r.left + 12);
        H pad_r = (H)(bot_r.right - 12);

        /* Row 1: Part/Window + Fixed-Column Telemetry Placeholders (zero horizontal jitter) */
        char r1_l[48];
        if (orch_mode) {
            snprintf(r1_l, sizeof(r1_l), "WIN  %2d/%-2d [ORCH]", lc_page + 1, lc_pages);
        } else if (lc_mode) {
            snprintf(r1_l, sizeof(r1_l), "WIN  %2d/%-2d [C99]", lc_page + 1, lc_pages);
        } else {
            snprintf(r1_l, sizeof(r1_l), "PART %2d/%-2d %s",
                     t->part_idx + 1, t->part_count, t->auto_advance ? "[AUTO]" : "[LOOP]");
        }

        int hc_bot = (int)((win_mode ? lc_xn : ((fix16_to_float(t->x_pos) + 8.0f) / 16.0f)) * (float)(SYNTH_PLOT_COLS - 1));
        if (hc_bot < 0) hc_bot = 0;
        if (hc_bot >= SYNTH_PLOT_COLS) hc_bot = SYNTH_PLOT_COLS - 1;

        float hz0 = 261.63f;
        for (int v = 0; v < BSF_MAX_VOICES; v++) {
            fix16_t vy = win_mode ? s_plot_y[hc_bot][v] : t->voice_y[v];
            if (vy != FIX16_QUIET) {
                hz0 = fix16_to_float(fix16_y_to_hz(vy));
                break;
            }
        }
        H r1_y     = (H)(bot_r.top + 6);
        H col_wcet = (H)(pad_r - 88);
        H col_hz   = (H)(col_wcet - 96);
        H col_x    = (H)(col_hz - 80);

        synth_draw_clipped(dev, pad_l, r1_y, (H)(col_x - 12), r1_l, HAL_COL_AMBER, HAL_COL_PANEL);
        if (col_x > pad_l + 90) {
            char s_x[24], s_hz[24], s_wcet[24];
            if (orch_mode && ov) {
                unsigned sec = (ov->sample_rate > 0u) ? (unsigned)(ov->npos / ov->sample_rate) : 0u;
                snprintf(s_x,    sizeof(s_x),    "T=%02u:%02u ", sec / 60u, sec % 60u);
                snprintf(s_hz,   sizeof(s_hz),   "%6.1f HZ",     hz0);
                snprintf(s_wcet, sizeof(s_wcet), "WCET %3u us",  (unsigned)ov->wcet_us);
            } else if (lc_mode) {
                snprintf(s_x,    sizeof(s_x),    "T=%5.2fs",   (float)lc_t);
                snprintf(s_hz,   sizeof(s_hz),   "%6.1f HZ",   hz0);
                snprintf(s_wcet, sizeof(s_wcet), "EV %4d   ",  lc_cap_count());
            } else {
                snprintf(s_x,    sizeof(s_x),    "X=%+6.2f",   fix16_to_float(t->x_pos));
                snprintf(s_hz,   sizeof(s_hz),   "%6.1f HZ",   hz0);
                snprintf(s_wcet, sizeof(s_wcet), "WCET %3u us", (unsigned)t->wcet_us);
            }
            drw_tc_string(dev, col_x,    r1_y, s_x,    HAL_COL_CYAN, HAL_COL_PANEL);
            drw_tc_string(dev, (H)(col_hz - 12), r1_y, "·", HAL_COL_DIM, HAL_COL_PANEL);
            drw_tc_string(dev, col_hz,   r1_y, s_hz,   HAL_COL_CYAN, HAL_COL_PANEL);
            drw_tc_string(dev, (H)(col_wcet - 12), r1_y, "·", HAL_COL_DIM, HAL_COL_PANEL);
            drw_tc_string(dev, col_wcet, r1_y, s_wcet, HAL_COL_CYAN, HAL_COL_PANEL);
        }

        /* Row 2: Active DSP Voice Badges (Solid Dry + Dotted ECHO xxms) + Stereo Peak/LIM Meter */
        H r2_y = (H)(bot_r.top + 26);
        H bar_w = compact ? 36 : 56;
        char mtxt[48];
        snprintf(mtxt, sizeof(mtxt), "PK %2d%% %s",
                 (int)(fmaxf(pk_l, pk_r) * 100.0f),
                 lim_hit ? "[LIM]" : "LIM");
        int mtxt_w = tc_calc_string_width(mtxt, (int)strlen(mtxt));
        H mtxt_x = (H)(pad_r - mtxt_w);
        H bar_x  = (H)(mtxt_x - bar_w - 10);
        H max_cx = (H)(bar_x - 14);

        H cx = pad_l;
        drw_tc_string(dev, cx, r2_y, "DSP:", HAL_COL_CYAN, HAL_COL_PANEL);
        cx = (H)(cx + tc_calc_string_width("DSP:", 4) + 10);

        int show_v = (s_plot_voices > 0) ? s_plot_voices : (t ? t->voice_count : 0);
        if (show_v > BSF_MAX_VOICES) show_v = BSF_MAX_VOICES;
        float spd = tr ? fix16_to_float(tr->speed) : 2.0f;
        int hc = hc_bot;

        bool has_rvb_voice = false;
        for (int v = 0; v < show_v; v++) {
            uint8_t vm = (s_plot_voices > 0) ? s_plot_modes[v] : (t ? t->voice_mode[v] : BSF_MODE_SINE);
            if (vm == BSF_MODE_RVB) has_rvb_voice = true;
            /* Dry voice badge: solid line swatch + mode label (BSF_MODE_ECHO dry source is PNO) */
            const char *mname = (vm == BSF_MODE_ECHO) ? "PNO" : bsf_mode_name(vm);
            int m_w = 14 + tc_calc_string_width(mname, (int)strlen(mname));
            if (cx + m_w > max_cx) break;

            bool dry_on = win_mode ? (s_plot_y[hc][v] != FIX16_QUIET) : (t && t->voice_active[v]);
            COLOR scol = dry_on ? k_vcols[v] : blend_hal_col(k_vcols[v], HAL_COL_PANEL, 0.45f);
            COLOR tcol = dry_on ? k_vcols[v] : HAL_COL_DIM;

            /* Solid 10px swatch */
            set_col(dev, scol, HAL_COL_PANEL);
            drw_lin(dev, cx, (H)(r2_y + 6), (H)(cx + 9), (H)(r2_y + 6));
            drw_lin(dev, cx, (H)(r2_y + 7), (H)(cx + 9), (H)(r2_y + 7));
            drw_tc_string(dev, (H)(cx + 14), r2_y, mname, tcol, HAL_COL_PANEL);
            cx = (H)(cx + m_w + 12);

            /* Separate dotted ECHO xxms badge when this voice has custom echo delay */
            if (!win_mode && s_plot_echo_wet[v] > 0.005f && s_plot_echo_sec[v] > 0.005f) {
                char elbl[20];
                snprintf(elbl, sizeof(elbl), "ECHO %dms", (int)(s_plot_echo_sec[v] * 1000.0f + 0.5f));
                int e_w = 14 + tc_calc_string_width(elbl, (int)strlen(elbl));
                if (cx + e_w > max_cx) break;

                bool e_on = t->voice_echo_active[v];
                if (!e_on) {
                    int estep = (int)((spd * s_plot_echo_sec[v] / 16.0f) * (float)(SYNTH_PLOT_COLS - 1) + 0.5f);
                    if (estep < 1) estep = 1;
                    for (int k = 1; k <= 4 && !e_on; k++) {
                        int sc = hc - k * estep;
                        if (sc < 0) sc += SYNTH_PLOT_COLS;
                        for (int off = -1; off <= 1; off++) {
                            int idx_c = sc + off;
                            if (idx_c >= 0 && idx_c < SYNTH_PLOT_COLS && s_plot_y[idx_c][v] != FIX16_QUIET) {
                                e_on = true;
                                break;
                            }
                        }
                    }
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
        if (!win_mode && tr && tr->reverb_mix > 0.005f && !has_rvb_voice) {
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
            int wl = (int)(pk_l * (float)bar_w); if (wl > bar_w) wl = bar_w;
            int wr = (int)(pk_r * (float)bar_w); if (wr > bar_w) wr = bar_w;
            if (wl > 0) {
                RECT fl = { bar_x, bl_bg.top, (H)(bar_x + wl), bl_bg.bottom };
                fill_rec(dev, &fl, (pk_l > 0.85f) ? HAL_COL_RED : HAL_COL_CYAN);
            }
            if (wr > 0) {
                RECT fr = { bar_x, br_bg.top, (H)(bar_x + wr), br_bg.bottom };
                fill_rec(dev, &fr, (pk_r > 0.85f) ? HAL_COL_RED : HAL_COL_AMBER);
            }
            drw_tc_string(dev, mtxt_x, r2_y, mtxt,
                          lim_hit ? HAL_COL_RED_HI : HAL_COL_PAPER, HAL_COL_PANEL);
        }

        /* Row 3: Formula or C99/ORCH Info */
        H r3_y = (H)(bot_r.top + 48);
        if (orch_mode && ov) {
            char o_info[128];
            snprintf(o_info, sizeof(o_info),
                     "Orchestra / SMF Isolated Audio Core · %u tracks · %u events · Up/Down: Window Zoom (%d pages)",
                     (unsigned)ov->track_count, (unsigned)ov->event_count, lc_pages);
            synth_draw_clipped(dev, pad_l, r3_y, pad_r, o_info, HAL_COL_PAPER, HAL_COL_PANEL);
        } else if (lc_mode) {
            char lc_info[128];
            snprintf(lc_info, sizeof(lc_info),
                     "LilCu64 C99 WebAudio Engine · %.2fs loop · %d captured events · Up/Down: Window Zoom (%d pages)",
                     lc_len, lc_cap_count(), lc_pages);
            synth_draw_clipped(dev, pad_l, r3_y, pad_r, lc_info, HAL_COL_PAPER, HAL_COL_PANEL);
        } else if (t && t->last_error[0] != '\0') {
            synth_draw_clipped(dev, pad_l, r3_y, pad_r, t->last_error, HAL_COL_RED_HI, HAL_COL_PANEL);
        } else if (tr && tr->formula[0] != '\0') {
            synth_draw_clipped(dev, pad_l, r3_y, pad_r, tr->formula, HAL_COL_PAPER, HAL_COL_PANEL);
        }
        if (!compact) {
            synth_draw_clipped(dev, pad_l, (H)(bot_r.top + 66), pad_r,
                               lc_mode
                               ? "Space:Play  Left/Right:Track  Up/Down:Window Scale (4/8/16)  C:Console"
                               : "Space:Play  Left/Right:Track  Up/Down:Part  Drag:Scrub X  E:Edit .bsf  R:Reload  C:Console",
                               HAL_COL_DIM, HAL_COL_PANEL);
        }
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
    if (!bsf_is_playing()) {
        int lc_active = lilcu64_synth_get_active_track();
        OrchView *ov = gst_view();
        if (lc_active >= 1 && lc_active <= 4) {
            s_orch_mode = false;
            s_lc_track = lc_active;
            s_plot_track = -1;
            s_plot_part  = -1;
        } else if (ov && ov->state == ORCH_GST_PLAYING) {
            s_orch_mode = true;
            s_lc_track = 0;
            s_plot_track = -1;
            s_plot_part  = -1;
        }
    }
    s_plot_dirty = true;
    return s_synth_wnd;
}

WND *open_synth_about_window(void) {
    return app_menu_create_about_dialog("BSF Synth", "B-Sound Forth",
        "HAL-9000 Analytical & B-Sound Forth Visualizer",
        "15 Primitives · 16 Shims · 26 BSF Tracks (Q16.16)",
        300, 160);
}
