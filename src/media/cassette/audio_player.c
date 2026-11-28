/*
 * B-System (BTRON 3.20) — src/media/cassette/audio_player.c
 * Pro B-System DJ/VJ Event Console & Hierarchical BSF Track Deck.
 *
 * Clean pro-audio instrument layout: real-time Stereo Peak/RMS/LIM metering,
 * WCET & transport telemetry, real transport controls, synced menu bar with
 * Synth Visualizer (File / Primitives / Shims / Transport / Options), and
 * Clarity-style hierarchical DJ/VJ TOC (26 BSF tracks with expandable Parts,
 * Orchestra/Legacy deck, 16 Shims, and 15 Primitives).
 * Pure C99, NASA/JPL Power of Ten.
 */

#include <btron/apps.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>
#include <btron/bsf.h>
#include <btron/t_editor.h>
#include <media/orchestra.h>
#include <media/synth.h>
#include "../sdk/lilcu64_synth.h"
#include <btron/libc_shim.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define DECK_WND_W      680
#define DECK_WND_H      580
#define DECK_MENU_H     21
#define DECK_TOP_H      82
#define DECK_TRANS_H    38
#define DECK_FOOT_H     32
#define CASSETTE_PL_N   6
#define TOC_ROW_H       18
#define TOC_SB_W        12
#define TOC_MAX_ROWS    320

/* Pro Audio Console Color Palette (Harmonized with Music.html / HAL-9000) */
#define PRO_COL_BG       0xFF0B0E14u
#define PRO_COL_PANEL    0xFF12161Fu
#define PRO_COL_INSET    0xFF080A0Fu
#define PRO_COL_BORDER   0xFF283040u
#define PRO_COL_BTN      0xFF1B212Du
#define PRO_COL_BTN_ACT  0xFF243044u
#define PRO_COL_AMBER    0xFFE8A454u
#define PRO_COL_CYAN     0xFF7FD4CCu
#define PRO_COL_GREEN    0xFF22C55Eu
#define PRO_COL_RED      0xFFD93829u
#define PRO_COL_PAPER    0xFFE6E1D6u
#define PRO_COL_SILVER   0xFFCBD5E1u
#define PRO_COL_DIM      0xFF6B7280u

typedef enum {
    DECK_STOP = 0,
    DECK_PLAY,
    DECK_PAUSE
} DeckState;

typedef struct {
    DeckState state;
    int       seconds_elapsed;
    int       vu_left;       /* 0..100 */
    int       vu_right;      /* 0..100 */
    int       vu_peak_l;
    int       vu_peak_r;
    float     rms_l;
    float     rms_r;
    bool      limiter_hit;
    int       pl_sel;        /* 0..CASSETTE_PL_N-1 for legacy */
    int       pl_on;         /* -1 or legacy row sounding */
    int       pl_repeat;
    int       pl_shuffle;
} ProDeck;

static ProDeck      g_deck = { .pl_on = -1 };
static WND         *g_audio_wnd = NULL;
static APP_MENU_BAR s_menu;
static int          s_menu_ok = 0;
static int          s_owner = 0;      /* 0 none, 1 demo score, 2 orchestra, 3 BSF synth */
static int          s_seen = -1;
static int          s_run = 0;
static uint32_t     s_rng = 1u;

/* ── Clarity-Style Hierarchical DJ/VJ TOC ────────────────────────── */
typedef enum {
    TOC_HEADER = 0,
    TOC_BSF_TRACK,
    TOC_BSF_PART,
    TOC_LEGACY,
    TOC_SHIM,
    TOC_PRIM
} TocRowKind;

typedef struct {
    TocRowKind kind;
    int        a;
    int        b;
    char       label[80];
    char       tag[16];
} TocRow;

static TocRow s_toc[TOC_MAX_ROWS];
static int    s_toc_count = 0;
static int    s_toc_scroll = 0;
static int    s_toc_sel = 1;
static int    s_toc_hover = -1;
static bool   s_toc_expanded[BSF_MAX_TRACKS];
static bool   s_toc_sb_drag = false;
static H      s_toc_sb_start_y = 0;
static int    s_toc_sb_orig_scroll = 0;
static int    s_last_click_idx = -1;
static uint64_t s_last_click_us = 0;

/* Menu Command IDs (Synced with Synth Visualizer) */
enum {
    C_NONE = 0,
    C_SYNTH_VIS = 1,
    C_EDIT_BSF = 2,
    C_RELOAD_BSF = 3,
    C_OPEN_ORCH = 4,
    C_LOAD_SMF = 5,
    C_CLOSE = 6,
    C_PLAY_TOGGLE = 10,
    C_STOP = 11,
    C_PREV_TRACK = 12,
    C_NEXT_TRACK = 13,
    C_PREV_PART = 14,
    C_NEXT_PART = 15,
    C_AUTO_ADV = 16,
    C_MUTE = 17,
    C_REPEAT = 20,
    C_SHUFFLE = 21,
    C_ABOUT = 22,
    C_PRIM_BASE = 100, /* 100..114 */
    C_SHIM_BASE = 200  /* 200..215 */
};

static const char *k_leg_title[CASSETTE_PL_N] = {
    "Carol of the Bells (Built-in)",
    "Green Caterpillar (Built-in)",
    "House of the Rising Sun (Built-in)",
    "TRON Init (Built-in)",
    "orchestra.orch",
    "orchestra.mid"
};
static const char *k_leg_tag[CASSETTE_PL_N] = {
    "0:26", "0:28", "0:27", "0:26", "ORCH", "SMF"
};

static void toc_push(TocRowKind kind, int a, int b, const char *label, const char *tag) {
    if (s_toc_count >= TOC_MAX_ROWS) return;
    TocRow *r = &s_toc[s_toc_count++];
    r->kind = kind;
    r->a = a;
    r->b = b;
    strncpy(r->label, label ? label : "", sizeof(r->label) - 1);
    r->label[sizeof(r->label) - 1] = '\0';
    strncpy(r->tag, tag ? tag : "", sizeof(r->tag) - 1);
    r->tag[sizeof(r->tag) - 1] = '\0';
}

static void toc_append_bsf_cat(BsfCategory cat, const char *hdr_title) {
    int n = bsf_get_track_count();
    toc_push(TOC_HEADER, (int)cat, 0, hdr_title, "BSF");
    for (int i = 0; i < n; i++) {
        const BsfTrackInfo *tr = bsf_get_track_info(i);
        if (!tr || tr->category != cat) continue;
        char lbl[80], tag[16];
        snprintf(lbl, sizeof(lbl), "%s %02d. %s",
                 s_toc_expanded[i] ? "v" : ">", tr->id, tr->title);
        snprintf(tag, sizeof(tag), "%dP", tr->part_count);
        toc_push(TOC_BSF_TRACK, i, 0, lbl, tag);
        if (s_toc_expanded[i]) {
            for (int p = 0; p < tr->part_count; p++) {
                char plbl[80];
                snprintf(plbl, sizeof(plbl), "   P%02d: %s", p + 1, tr->part_names[p]);
                toc_push(TOC_BSF_PART, i, p, plbl, "PART");
            }
        }
    }
}

static void cassette_toc_rebuild(void) {
    s_toc_count = 0;
    (void)bsf_init(BSF_SAMPLE_RATE);
    toc_append_bsf_cat(BSF_CAT_COMPOSITION, "COMPOSITIONS (/SYS/synth/compositions)");
    toc_append_bsf_cat(BSF_CAT_MATH_SYNTH,  "SAMPLES: MATH-SYNTH (01-16)");
    toc_append_bsf_cat(BSF_CAT_BSOUND_DSP,  "SAMPLES: B-SOUND DSP (17-20)");

    toc_push(TOC_HEADER, 99, 0, "ORCHESTRA / LEGACY DECK", "DECK");
    for (int i = 0; i < CASSETTE_PL_N; i++) {
        toc_push(TOC_LEGACY, i, 0, k_leg_title[i], k_leg_tag[i]);
    }

    toc_push(TOC_HEADER, 100, 0, "DSP SHIMS (/SYS/synth/shims) [Edit]", "SHIM");
    int ns = bsf_get_shim_count();
    for (int i = 0; i < ns; i++) {
        const BsfLibItem *si = bsf_get_shim_info(i);
        if (!si) continue;
        char lbl[80];
        snprintf(lbl, sizeof(lbl), "  %02d. %s (%s)", si->id, si->name, si->word);
        toc_push(TOC_SHIM, i, 0, lbl, si->marker);
    }

    toc_push(TOC_HEADER, 101, 0, "PRIMITIVES (/SYS/synth/primitives) [Edit]", "PRIM");
    int np = bsf_get_prim_count();
    for (int i = 0; i < np; i++) {
        const BsfLibItem *pi = bsf_get_prim_info(i);
        if (!pi) continue;
        char lbl[80];
        snprintf(lbl, sizeof(lbl), "  %02d. %s (%s)", pi->id, pi->name, pi->word);
        toc_push(TOC_PRIM, i, 0, lbl, pi->marker);
    }
    if (s_toc_sel >= s_toc_count) s_toc_sel = (s_toc_count > 1) ? 1 : 0;
}

/* ── Audio Engine Transport Control ──────────────────────────────── */
static void cassette_halt(void) {
    if (s_owner == 1) lilcu64_synth_stop_track();
    else if (s_owner == 2) { (void)orch_cmd_stop(); (void)orch_cmd_stop(); }
    else if (s_owner == 3) bsf_stop();
    g_deck.pl_on = -1;
    g_deck.state = DECK_STOP;
    g_deck.vu_left = g_deck.vu_right = 0;
    g_deck.vu_peak_l = g_deck.vu_peak_r = 0;
    g_deck.rms_l = g_deck.rms_r = 0.0f;
    g_deck.limiter_hit = false;
    s_seen = -1;
}

static void cassette_release(void) {
    cassette_halt();
    if (s_owner == 1) lilcu64_synth_close();
    else if (s_owner == 2) orch_shutdown_desk();
    else if (s_owner == 3) bsf_close();
    s_owner = 0;
}

static void cassette_play_demo(int track) {
    if (s_owner == 3) bsf_stop();
    if (s_owner != 1) {
        if (s_owner == 2) orch_shutdown_desk();
        lilcu64_synth_close();
        (void)lilcu64_synth_init(LILCU64_AUDIO_RATE);
        s_owner = 1;
    }
    lilcu64_synth_stop_track();
    lilcu64_synth_play_track(track);
    g_deck.pl_on = track - 1;
    g_deck.state = DECK_PLAY;
    if (open_synth_window) {
        (void)open_synth_window();
        if (g_audio_wnd) top_wnd(g_audio_wnd);
    }
    synth_set_lilcu64_track(track);
}

static void cassette_play_file(int row) {
    OrchView *v;
    int ok;
    if (s_owner == 3) bsf_stop();
    if (s_owner == 1) {
        lilcu64_synth_stop_track();
        lilcu64_synth_close();
        s_owner = 0;
    }
    orch_boot_desk();
    s_owner = 2;
    v = gst_view();
    if (v && v->state == ORCH_GST_PLAYING) {
        (void)orch_cmd_stop();
        (void)orch_cmd_stop();
    }
    ok = (row == 4) ? orch_cmd_open() : orch_cmd_smf_load();
    if (!ok || !orch_cmd_play()) {
        g_deck.pl_on = -1;
        g_deck.state = DECK_STOP;
        return;
    }
    g_deck.pl_on = row;
    g_deck.state = DECK_PLAY;
}

static void cassette_begin_legacy(int row, int manual) {
    if (row < 0 || row >= CASSETTE_PL_N) return;
    if (manual) s_run = 0;
    g_deck.pl_sel = row;
    g_deck.seconds_elapsed = 0;
    g_deck.vu_left = g_deck.vu_right = 0;
    g_deck.vu_peak_l = g_deck.vu_peak_r = 0;
    s_seen = -1;
    if (row < 4) cassette_play_demo(row + 1);
    else cassette_play_file(row);
}

static void cassette_play_bsf(int track_idx, int part_idx) {
    if (s_owner == 1) { lilcu64_synth_stop_track(); lilcu64_synth_close(); }
    else if (s_owner == 2) { orch_shutdown_desk(); }
    s_owner = 3;
    (void)bsf_init(BSF_SAMPLE_RATE);
    if (bsf_select_track(track_idx, part_idx) == 0) {
        bsf_play();
        g_deck.state = DECK_PLAY;
        g_deck.seconds_elapsed = 0;
        g_deck.pl_on = -1;
        g_deck.vu_peak_l = g_deck.vu_peak_r = 0;
        if (open_synth_window) {
            (void)open_synth_window();
            /* Keep keyboard focus on Cassette so cursor keys stay in the TOC */
            if (g_audio_wnd) top_wnd(g_audio_wnd);
        }
        synth_notify_track_changed();
    }
}

void cassette_notify_bsf_state(void) {
    const BsfTelemetry *t = bsf_get_telemetry();
    if (!t) return;
    if (t->playing) {
        if (s_owner == 1) lilcu64_synth_stop_track();
        else if (s_owner == 2) { (void)orch_cmd_stop(); (void)orch_cmd_stop(); }
        s_owner = 3;
        g_deck.state = DECK_PLAY;
    } else if (s_owner == 3) {
        g_deck.state = DECK_PAUSE;
    }
    if (g_audio_wnd) inval_wnd(g_audio_wnd);
}

static void cassette_edit_selected_bsf(void) {
    const char *path = NULL;
    if (s_toc_sel >= 0 && s_toc_sel < s_toc_count) {
        const TocRow *r = &s_toc[s_toc_sel];
        if (r->kind == TOC_BSF_TRACK || r->kind == TOC_BSF_PART) {
            const BsfTrackInfo *tr = bsf_get_track_info(r->a);
            if (tr) path = tr->filepath;
        } else if (r->kind == TOC_SHIM) {
            const BsfLibItem *si = bsf_get_shim_info(r->a);
            if (si) path = si->filepath;
        } else if (r->kind == TOC_PRIM) {
            const BsfLibItem *pi = bsf_get_prim_info(r->a);
            if (pi) path = pi->filepath;
        }
    }
    if (!path) {
        const BsfTelemetry *t = bsf_get_telemetry();
        const BsfTrackInfo *tr = t ? bsf_get_track_info(t->track_idx) : NULL;
        if (tr) path = tr->filepath;
    }
    if (path) (void)open_t_editor_window_with_file(path);
}

static void cassette_step_track(int dir) {
    int n = bsf_get_track_count();
    if (s_owner == 3 || s_owner == 0) {
        if (n <= 0) return;
        const BsfTelemetry *t = bsf_get_telemetry();
        int cur = t ? t->track_idx : 0;
        int nxt;
        if (g_deck.pl_shuffle) {
            s_rng = s_rng * 1664525u + 1013904223u;
            nxt = (int)(s_rng % (uint32_t)n);
        } else {
            nxt = (cur + dir + n) % n;
        }
        cassette_play_bsf(nxt, 0);
    } else {
        int nxt = (g_deck.pl_sel + dir + CASSETTE_PL_N) % CASSETTE_PL_N;
        cassette_begin_legacy(nxt, 1);
    }
}

static void cassette_toggle_play_pause(void) {
    if (s_owner == 3) {
        bsf_toggle_play();
        g_deck.state = bsf_is_playing() ? DECK_PLAY : DECK_PAUSE;
        synth_notify_track_changed();
        return;
    }
    if (s_owner == 1) {
        lilcu64_synth_toggle_mute();
        g_deck.state = lilcu64_synth_is_muted() ? DECK_PAUSE : DECK_PLAY;
        return;
    }
    if (s_owner == 2) {
        OrchView *v = gst_view();
        if (!v) return;
        if (v->state == ORCH_GST_PLAYING) {
            (void)gst_rt_set_state(ORCH_GST_PAUSED);
            g_deck.state = DECK_PAUSE;
        } else if (gst_rt_set_state(ORCH_GST_PLAYING)) {
            g_deck.state = DECK_PLAY;
        }
        return;
    }
    /* Nothing active yet: play selected TOC row or first BSF track */
    if (s_toc_sel >= 0 && s_toc_sel < s_toc_count) {
        const TocRow *r = &s_toc[s_toc_sel];
        if (r->kind == TOC_BSF_TRACK) { cassette_play_bsf(r->a, 0); return; }
        if (r->kind == TOC_BSF_PART)  { cassette_play_bsf(r->a, r->b); return; }
        if (r->kind == TOC_LEGACY)    { cassette_begin_legacy(r->a, 1); return; }
    }
    if (bsf_get_track_count() > 0) cassette_play_bsf(0, 0);
}

/* ── Synced Menu Bar (Identical Primitives/Shims/Transport to Synth) ─ */
static void cassette_opt_text(void) {
    if (!s_menu_ok || s_menu.header_count < 5) return;
    snprintf(s_menu.headers[4].items[0].label, 64, "Repeat: %s", g_deck.pl_repeat ? "On" : "Off");
    snprintf(s_menu.headers[4].items[1].label, 64, "Shuffle: %s", g_deck.pl_shuffle ? "On" : "Off");
}

static void cassette_menu_build(void) {
    if (s_menu_ok) return;
    app_menu_init(&s_menu, APP_MENU_STYLE_CLASSIC_3D);

    int h = app_menu_add_header(&s_menu, "File(F)", 68);
    (void)app_menu_add_item(&s_menu, h, "Synth Visualizer (V)", "V", C_SYNTH_VIS, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Edit Selected .bsf (E)", "E", C_EDIT_BSF, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Reload & Rerun .bsf (R)", "R", C_RELOAD_BSF, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Open orchestra.orch", "", C_OPEN_ORCH, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Load orchestra.mid (SMF)", "", C_LOAD_SMF, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Close", "", C_CLOSE, TRUE);

    h = app_menu_add_header(&s_menu, "Primitives(P)", 106);
    int np = bsf_get_prim_count();
    for (int i = 0; i < np && i < 15; i++) {
        const BsfLibItem *pi = bsf_get_prim_info(i);
        if (!pi) continue;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "%02d %s (%s)", pi->id, pi->marker, pi->name);
        (void)app_menu_add_item(&s_menu, h, lbl, "", C_PRIM_BASE + i, TRUE);
    }

    h = app_menu_add_header(&s_menu, "Shims(S)", 78);
    int ns = bsf_get_shim_count();
    for (int i = 0; i < ns && i < 16; i++) {
        const BsfLibItem *si = bsf_get_shim_info(i);
        if (!si) continue;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "%02d %s", si->id, si->name);
        (void)app_menu_add_item(&s_menu, h, lbl, "", C_SHIM_BASE + i, TRUE);
    }

    h = app_menu_add_header(&s_menu, "Transport(T)", 98);
    (void)app_menu_add_item(&s_menu, h, "Play / Pause", "Space", C_PLAY_TOGGLE, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Stop", "", C_STOP, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Prev Track", "<", C_PREV_TRACK, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Next Track", ">", C_NEXT_TRACK, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Prev Part", "[", C_PREV_PART, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Next Part", "]", C_NEXT_PART, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "Auto-Advance", "A", C_AUTO_ADV, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Mute", "M", C_MUTE, TRUE);

    h = app_menu_add_header(&s_menu, "Options(O)", 90);
    (void)app_menu_add_item(&s_menu, h, "Repeat: Off", "", C_REPEAT, TRUE);
    (void)app_menu_add_item(&s_menu, h, "Shuffle: Off", "", C_SHUFFLE, TRUE);
    (void)app_menu_add_separator(&s_menu, h);
    (void)app_menu_add_item(&s_menu, h, "About Audio Console...", "", C_ABOUT, TRUE);

    s_menu_ok = 1;
    cassette_opt_text();
}

static void cassette_cmd(int cmd) {
    if (cmd >= C_PRIM_BASE && cmd < C_PRIM_BASE + BSF_MAX_PRIMS) {
        const BsfLibItem *pi = bsf_get_prim_info(cmd - C_PRIM_BASE);
        if (pi) (void)open_t_editor_window_with_file(pi->filepath);
        return;
    }
    if (cmd >= C_SHIM_BASE && cmd < C_SHIM_BASE + BSF_MAX_SHIMS) {
        const BsfLibItem *si = bsf_get_shim_info(cmd - C_SHIM_BASE);
        if (si) (void)open_t_editor_window_with_file(si->filepath);
        return;
    }
    const BsfTelemetry *bt = bsf_get_telemetry();
    switch (cmd) {
        case C_SYNTH_VIS:
            if (open_synth_window) (void)open_synth_window();
            break;
        case C_EDIT_BSF:
            cassette_edit_selected_bsf();
            break;
        case C_RELOAD_BSF:
            (void)bsf_reload();
            cassette_toc_rebuild();
            synth_notify_track_changed();
            break;
        case C_OPEN_ORCH:
            cassette_begin_legacy(4, 1);
            break;
        case C_LOAD_SMF:
            cassette_begin_legacy(5, 1);
            break;
        case C_CLOSE:
            cassette_release();
            if (g_audio_wnd) cls_wnd(g_audio_wnd);
            return;
        case C_PLAY_TOGGLE:
            cassette_toggle_play_pause();
            break;
        case C_STOP:
            cassette_halt();
            synth_notify_track_changed();
            break;
        case C_PREV_TRACK:
            cassette_step_track(-1);
            break;
        case C_NEXT_TRACK:
            cassette_step_track(1);
            break;
        case C_PREV_PART:
            if (bt && bt->part_count > 0) {
                (void)bsf_set_part((bt->part_idx - 1 + bt->part_count) % bt->part_count);
                synth_notify_track_changed();
            }
            break;
        case C_NEXT_PART:
            if (bt && bt->part_count > 0) {
                (void)bsf_set_part((bt->part_idx + 1) % bt->part_count);
                synth_notify_track_changed();
            }
            break;
        case C_AUTO_ADV:
            bsf_toggle_auto_advance();
            synth_notify_track_changed();
            break;
        case C_MUTE:
            if (s_owner == 3) bsf_toggle_mute();
            else if (s_owner == 1) lilcu64_synth_toggle_mute();
            synth_notify_track_changed();
            break;
        case C_REPEAT:
            g_deck.pl_repeat = !g_deck.pl_repeat;
            cassette_opt_text();
            break;
        case C_SHUFFLE:
            g_deck.pl_shuffle = !g_deck.pl_shuffle;
            cassette_opt_text();
            break;
        case C_ABOUT:
            (void)open_cassette_about_window();
            break;
        default:
            break;
    }
    if (g_audio_wnd) inval_wnd(g_audio_wnd);
}

/* ── Pro Layout & Rendering ──────────────────────────────────────── */
typedef struct {
    RECT top_box;
    RECT vu_l;
    RECT vu_r;
    RECT trans_box;
    RECT btn[8];
    RECT toc_box;
    RECT foot_box;
    RECT fbtn[4];
} ProLayout;

static void calc_pro_layout(ProLayout *ly, H w, H h) {
    ly->top_box = (RECT){ 10, (H)(DECK_MENU_H + 6), (H)(w - 10), (H)(DECK_MENU_H + 6 + DECK_TOP_H) };
    H meter_w = (w >= 540) ? 210 : ((w >= 420) ? 156 : 108);
    H mx = (H)(ly->top_box.right - meter_w - 10);
    if (mx < ly->top_box.left + 120) mx = (H)(ly->top_box.left + 120);
    ly->vu_l = (RECT){ mx, (H)(ly->top_box.top + 26), (H)(ly->top_box.right - 10), (H)(ly->top_box.top + 40) };
    ly->vu_r = (RECT){ mx, (H)(ly->top_box.top + 46), (H)(ly->top_box.right - 10), (H)(ly->top_box.top + 60) };

    ly->trans_box = (RECT){ 10, (H)(ly->top_box.bottom + 6), (H)(w - 10), (H)(ly->top_box.bottom + 6 + DECK_TRANS_H) };
    H tw = (H)(ly->trans_box.right - ly->trans_box.left - 16);
    H bw = (H)((tw - 7 * 6) / 8);
    if (bw < 36) bw = 36;
    H bx = (H)(ly->trans_box.left + 8);
    H by = (H)(ly->trans_box.top + 6);
    for (int i = 0; i < 8; i++) {
        ly->btn[i] = (RECT){ bx, by, (H)(bx + bw), (H)(by + 26) };
        bx = (H)(bx + bw + 6);
    }

    ly->foot_box = (RECT){ 0, (H)(h - DECK_FOOT_H), w, h };
    ly->toc_box = (RECT){ 10, (H)(ly->trans_box.bottom + 6), (H)(w - 10), (H)(ly->foot_box.top - 6) };

    H fy = (H)(ly->foot_box.top + 5);
    H rx = (H)(w - 10);
    ly->fbtn[3] = (RECT){ (H)(rx - 64), fy, rx, (H)(fy + 22) }; rx = (H)(rx - 70); /* Close */
    ly->fbtn[2] = (RECT){ (H)(rx - 80), fy, rx, (H)(fy + 22) }; rx = (H)(rx - 86); /* Reload */
    ly->fbtn[1] = (RECT){ (H)(rx - 80), fy, rx, (H)(fy + 22) }; rx = (H)(rx - 86); /* Edit .bsf */
    ly->fbtn[0] = (RECT){ (H)(rx - 90), fy, rx, (H)(fy + 22) };                    /* Visualizer */
}

static void deck_draw_clipped(GDEV *dev, H x, H y, H max_right, const char *s, COLOR fg, COLOR bg) {
    if (!s || !s[0] || max_right <= x + 8) return;
    int avail = (int)(max_right - x);
    int len = (int)strlen(s);
    if (tc_calc_string_width(s, len) <= avail) {
        drw_tc_string(dev, x, y, s, fg, bg);
        return;
    }
    char buf[160];
    int n = (len < (int)sizeof(buf) - 4) ? len : (int)sizeof(buf) - 4;
    while (n > 0) {
        memcpy(buf, s, (size_t)n);
        buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = '.'; buf[n + 3] = '\0';
        if (tc_calc_string_width(buf, n + 3) <= avail) break;
        n--;
    }
    if (n > 0) drw_tc_string(dev, x, y, buf, fg, bg);
}

static void draw_vu_meter(GDEV *dev, const RECT *r, int val, int peak) {
    fill_rec(dev, r, PRO_COL_INSET);
    set_col(dev, PRO_COL_BORDER, PRO_COL_INSET);
    drw_rec(dev, r);
    int segs = 16;
    int tw = (int)(r->right - r->left) - 4;
    int sw = (tw / segs) - 2;
    if (sw < 3) sw = 3;
    int act = (val * segs) / 100;
    int pk  = (peak * segs) / 100;
    for (int i = 0; i < segs; i++) {
        H sx = (H)(r->left + 2 + i * (sw + 2));
        RECT sr = { sx, (H)(r->top + 2), (H)(sx + sw), (H)(r->bottom - 2) };
        COLOR col = (i < 9) ? PRO_COL_GREEN : ((i < 12) ? PRO_COL_CYAN : ((i < 14) ? PRO_COL_AMBER : PRO_COL_RED));
        if (i < act) fill_rec(dev, &sr, col);
        else if (i == pk && pk > 0) fill_rec(dev, &sr, PRO_COL_RED);
        else fill_rec(dev, &sr, 0xFF151922u);
    }
}

static void draw_pro_btn(GDEV *dev, const RECT *r, const char *lbl, bool active, COLOR accent) {
    COLOR bg = active ? PRO_COL_BTN_ACT : PRO_COL_BTN;
    COLOR fg = active ? accent : PRO_COL_PAPER;
    fill_rec(dev, r, bg);
    set_col(dev, active ? accent : PRO_COL_BORDER, bg);
    drw_rec(dev, r);
    int tw = tc_calc_string_width(lbl, (int)strlen(lbl));
    H tx = (H)(r->left + ((r->right - r->left - tw) / 2));
    if (tx < r->left + 3) tx = (H)(r->left + 3);
    drw_tc_string(dev, tx, (H)(r->top + 5), lbl, fg, bg);
}

static void audio_deck_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev || dev->width < 80 || dev->height < 160) return;
    if (s_toc_count == 0) cassette_toc_rebuild();

    ProLayout ly;
    calc_pro_layout(&ly, dev->width, dev->height);

    RECT bg_r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &bg_r, PRO_COL_BG);

    const BsfTelemetry *bt = bsf_get_telemetry();
    const BsfTrackInfo *tr = bt ? bsf_get_track_info(bt->track_idx) : NULL;

    /* 1. Top Pro Telemetry & Metering Console */
    fill_rec(dev, &ly.top_box, PRO_COL_PANEL);
    set_col(dev, PRO_COL_BORDER, PRO_COL_PANEL);
    drw_rec(dev, &ly.top_box);

    /* Status LED + Title */
    RECT led = { (H)(ly.top_box.left + 10), (H)(ly.top_box.top + 10),
                 (H)(ly.top_box.left + 20), (H)(ly.top_box.top + 20) };
    fill_ovl(dev, &led, (g_deck.state == DECK_PLAY) ? PRO_COL_GREEN : PRO_COL_DIM);

    char title_line[96], part_line[96], eng_line[96];
    if (s_owner == 3 && tr && bt) {
        snprintf(title_line, sizeof(title_line), "%02d/%02d  %s", tr->id, bsf_get_track_count(), tr->title);
        snprintf(part_line, sizeof(part_line), "PART %d/%d · %s", bt->part_idx + 1, tr->part_count,
                 tr->part_names[bt->part_idx]);
        snprintf(eng_line, sizeof(eng_line), "BSF Q16.16 · 44.1kHz S16 · X=%+.2f · WCET %u us · %02d:%02d",
                 fix16_to_float(bt->x_pos), (unsigned)bt->wcet_us,
                 g_deck.seconds_elapsed / 60, g_deck.seconds_elapsed % 60);
    } else if ((s_owner == 1 || s_owner == 2) && g_deck.pl_on >= 0 && g_deck.pl_on < CASSETTE_PL_N) {
        snprintf(title_line, sizeof(title_line), "DECK  %s", k_leg_title[g_deck.pl_on]);
        snprintf(part_line, sizeof(part_line), "%s", s_owner == 2 ? "ORCHESTRA ENGINE" : "LILCU64 SCORE");
        snprintf(eng_line, sizeof(eng_line), "44.1kHz S16 · %02d:%02d",
                 g_deck.seconds_elapsed / 60, g_deck.seconds_elapsed % 60);
    } else {
        snprintf(title_line, sizeof(title_line), "B-SYSTEM PRO AUDIO CONSOLE");
        snprintf(part_line, sizeof(part_line), "Select a BSF Composition, Sample, or Part below");
        snprintf(eng_line, sizeof(eng_line), "BSF Q16.16 · 15 Primitives · 16 Shims · 26 Tracks · 44.1kHz");
    }
    H left_max = (H)(ly.vu_l.left - 24);
    deck_draw_clipped(dev, (H)(ly.top_box.left + 28), (H)(ly.top_box.top + 8), left_max, title_line, PRO_COL_PAPER, PRO_COL_PANEL);
    deck_draw_clipped(dev, (H)(ly.top_box.left + 28), (H)(ly.top_box.top + 28), left_max, part_line, PRO_COL_CYAN, PRO_COL_PANEL);
    deck_draw_clipped(dev, (H)(ly.top_box.left + 12), (H)(ly.top_box.top + 56), left_max, eng_line, PRO_COL_AMBER, PRO_COL_PANEL);

    /* Right: Pro Stereo Peak/RMS/LIM Meters */
    deck_draw_clipped(dev, ly.vu_l.left, (H)(ly.top_box.top + 8), (H)(ly.top_box.right - 8), "OUTPUT L/R (dBFS)", PRO_COL_DIM, PRO_COL_PANEL);
    drw_tc_string(dev, (H)(ly.vu_l.left - 16), (H)(ly.vu_l.top + 1), "L", PRO_COL_CYAN, PRO_COL_PANEL);
    drw_tc_string(dev, (H)(ly.vu_r.left - 16), (H)(ly.vu_r.top + 1), "R", PRO_COL_AMBER, PRO_COL_PANEL);
    draw_vu_meter(dev, &ly.vu_l, g_deck.vu_left, g_deck.vu_peak_l);
    draw_vu_meter(dev, &ly.vu_r, g_deck.vu_right, g_deck.vu_peak_r);
    char mline[64];
    snprintf(mline, sizeof(mline), "PK %2d%%  RMS %2d%%  %s",
             (g_deck.vu_left > g_deck.vu_right ? g_deck.vu_left : g_deck.vu_right),
             (int)(fmaxf(g_deck.rms_l, g_deck.rms_r) * 100.0f),
             g_deck.limiter_hit ? "[LIM]" : "LIM");
    deck_draw_clipped(dev, ly.vu_l.left, (H)(ly.vu_r.bottom + 4), (H)(ly.top_box.right - 8), mline,
                      g_deck.limiter_hit ? PRO_COL_RED : PRO_COL_SILVER, PRO_COL_PANEL);

    /* 2. Pro Transport Strip */
    fill_rec(dev, &ly.trans_box, PRO_COL_PANEL);
    set_col(dev, PRO_COL_BORDER, PRO_COL_PANEL);
    drw_rec(dev, &ly.trans_box);
    bool is_play = (g_deck.state == DECK_PLAY);
    bool is_auto = bt ? bt->auto_advance : false;
    bool is_mute = (s_owner == 3 && bt) ? bt->muted : (s_owner == 1 ? lilcu64_synth_is_muted() : false);
    draw_pro_btn(dev, &ly.btn[0], "|< PREV", false, PRO_COL_CYAN);
    draw_pro_btn(dev, &ly.btn[1], "<< PART", false, PRO_COL_CYAN);
    draw_pro_btn(dev, &ly.btn[2], is_play ? "PAUSE" : "PLAY >", is_play, PRO_COL_GREEN);
    draw_pro_btn(dev, &ly.btn[3], "STOP", g_deck.state == DECK_STOP, PRO_COL_PAPER);
    draw_pro_btn(dev, &ly.btn[4], "PART >>", false, PRO_COL_CYAN);
    draw_pro_btn(dev, &ly.btn[5], "NEXT >|", false, PRO_COL_CYAN);
    draw_pro_btn(dev, &ly.btn[6], "AUTO", is_auto, PRO_COL_AMBER);
    draw_pro_btn(dev, &ly.btn[7], "MUTE", is_mute, PRO_COL_RED);

    /* 3. Hierarchical DJ/VJ TOC Panel */
    const RECT *box = &ly.toc_box;
    fill_rec(dev, box, PRO_COL_INSET);
    set_col(dev, PRO_COL_BORDER, PRO_COL_INSET);
    drw_rec(dev, box);

    RECT thdr = { (H)(box->left + 1), (H)(box->top + 1), (H)(box->right - 1), (H)(box->top + 18) };
    fill_rec(dev, &thdr, PRO_COL_PANEL);
    deck_draw_clipped(dev, (H)(box->left + 8), (H)(box->top + 2), (H)(box->right - 8),
                      "DJ/VJ EVENT TOC  (Left/Right: Tree · Double-Click/Enter: Play/Edit)",
                      PRO_COL_AMBER, PRO_COL_PANEL);

    H list_top = (H)(box->top + 19);
    H list_bot = (H)(box->bottom - 2);
    H view_h = (H)(list_bot - list_top);
    H content_r = (H)(box->right - TOC_SB_W - 2);
    int total_h = s_toc_count * TOC_ROW_H;
    int max_sc = (total_h > view_h) ? (total_h - view_h) : 0;
    if (s_toc_scroll < 0) s_toc_scroll = 0;
    if (s_toc_scroll > max_sc) s_toc_scroll = max_sc;

    for (int i = 0; i < s_toc_count; i++) {
        int ry = list_top + i * TOC_ROW_H - s_toc_scroll;
        if (ry + TOC_ROW_H <= list_top || ry >= list_bot) continue;
        const TocRow *r = &s_toc[i];
        RECT rr = { (H)(box->left + 2), (H)(ry < list_top ? list_top : ry),
                    content_r, (H)(ry + TOC_ROW_H - 1 > list_bot ? list_bot : ry + TOC_ROW_H - 1) };

        bool is_sel = (i == s_toc_sel);
        bool is_hov = (i == s_toc_hover && !is_sel);
        bool is_on = false;
        if (s_owner == 3 && bt && bt->playing) {
            if (r->kind == TOC_BSF_TRACK && r->a == bt->track_idx) is_on = true;
            if (r->kind == TOC_BSF_PART && r->a == bt->track_idx && r->b == bt->part_idx) is_on = true;
        } else if (r->kind == TOC_LEGACY && g_deck.pl_on == r->a) {
            is_on = true;
        }

        COLOR bg = PRO_COL_INSET;
        COLOR fg = PRO_COL_SILVER;
        if (r->kind == TOC_HEADER) { bg = 0xFF161C28u; fg = PRO_COL_AMBER; }
        else if (is_on)            { bg = 0xFF102A1Cu; fg = PRO_COL_GREEN; }
        else if (is_sel)           { bg = 0xFF1E293Bu; fg = PRO_COL_PAPER; }
        else if (is_hov)           { bg = 0xFF141B26u; fg = PRO_COL_PAPER; }
        else if (r->kind == TOC_BSF_PART) { fg = PRO_COL_CYAN; }
        else if (r->kind == TOC_SHIM || r->kind == TOC_PRIM) { fg = PRO_COL_DIM; }

        fill_rec(dev, &rr, bg);
        if (ry >= list_top && ry + 14 <= list_bot) {
            drw_tc_string(dev, (H)(rr.left + 6), (H)(ry + 2), r->label, fg, bg);
            int tw = tc_calc_string_width(r->tag, (int)strlen(r->tag));
            if (tw < 0) tw = 0;
            if (rr.right - tw - 6 > rr.left + 12) {
                drw_tc_string(dev, (H)(rr.right - tw - 6), (H)(ry + 2), r->tag,
                              (r->kind == TOC_HEADER) ? PRO_COL_AMBER : PRO_COL_CYAN, bg);
            }
        }
    }

    /* Clarity-Style 12px Mini Vertical Scrollbar */
    H sb_x = (H)(box->right - TOC_SB_W - 1);
    RECT sb_r = { sb_x, list_top, (H)(box->right - 1), list_bot };
    fill_rec(dev, &sb_r, 0xFF11151Eu);
    RECT up_r = { sb_x, list_top, (H)(box->right - 1), (H)(list_top + 12) };
    RECT dn_r = { sb_x, (H)(list_bot - 12), (H)(box->right - 1), list_bot };
    fill_rec(dev, &up_r, PRO_COL_BTN);
    fill_rec(dev, &dn_r, PRO_COL_BTN);
    drw_tc_string(dev, (H)(sb_x + 2), (H)(list_top - 1), "^", PRO_COL_SILVER, PRO_COL_BTN);
    drw_tc_string(dev, (H)(sb_x + 2), (H)(list_bot - 13), "v", PRO_COL_SILVER, PRO_COL_BTN);

    int track_h = (int)(list_bot - list_top) - 24;
    if (track_h > 12) {
        int thumb_h = (max_sc > 0) ? ((track_h * view_h) / (view_h + max_sc)) : track_h;
        if (thumb_h < 14) thumb_h = 14;
        if (thumb_h > track_h) thumb_h = track_h;
        int thumb_y = list_top + 12 + ((max_sc > 0) ? ((s_toc_scroll * (track_h - thumb_h)) / max_sc) : 0);
        RECT th_r = { (H)(sb_x + 1), (H)thumb_y, (H)(box->right - 2), (H)(thumb_y + thumb_h) };
        fill_rec(dev, &th_r, s_toc_sb_drag ? PRO_COL_AMBER : 0xFF475569u);
    }

    /* 4. Pro Footer Bar */
    fill_rec(dev, &ly.foot_box, PRO_COL_PANEL);
    set_col(dev, PRO_COL_BORDER, PRO_COL_PANEL);
    drw_lin(dev, 0, ly.foot_box.top, dev->width, ly.foot_box.top);

    const char *status_msg = (bt && bt->last_error[0]) ? bt->last_error : "B-Audio Ready · Space:Play  V:Visualizer  E:Edit  R:Reload";
    drw_tc_string(dev, 12, (H)(ly.foot_box.top + 8), status_msg,
                  (bt && bt->last_error[0]) ? PRO_COL_RED : PRO_COL_DIM, PRO_COL_PANEL);

    draw_pro_btn(dev, &ly.fbtn[0], "Visualizer", false, PRO_COL_CYAN);
    draw_pro_btn(dev, &ly.fbtn[1], "Edit .bsf", false, PRO_COL_AMBER);
    draw_pro_btn(dev, &ly.fbtn[2], "Reload", false, PRO_COL_PAPER);
    draw_pro_btn(dev, &ly.fbtn[3], "Close", false, PRO_COL_SILVER);

    if (s_menu_ok) {
        app_menu_paint_bar(&s_menu, dev);
        app_menu_paint_dropdown(&s_menu, dev);
    }
}

static BOOL cassette_menu_is_open(WND *wnd) {
    (void)wnd;
    return (s_menu_ok && s_menu.active_menu >= 0) ? TRUE : FALSE;
}

static bool hit_rect(H x, H y, const RECT *r) {
    return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}

static int toc_max_scroll(H cw, H ch) {
    ProLayout ly;
    calc_pro_layout(&ly, cw, ch);
    int view_h = (int)(ly.toc_box.bottom - ly.toc_box.top - 21);
    if (view_h < TOC_ROW_H) view_h = TOC_ROW_H;
    int total_h = s_toc_count * TOC_ROW_H;
    return (total_h > view_h) ? (total_h - view_h) : 0;
}

static void toc_ensure_visible(H cw, H ch) {
    ProLayout ly;
    calc_pro_layout(&ly, cw, ch);
    int view_h = (int)(ly.toc_box.bottom - ly.toc_box.top - 21);
    if (view_h < TOC_ROW_H) view_h = TOC_ROW_H;
    if (s_toc_sel < 0) s_toc_sel = 0;
    if (s_toc_sel >= s_toc_count && s_toc_count > 0) s_toc_sel = s_toc_count - 1;
    int row_top = s_toc_sel * TOC_ROW_H;
    int row_bot = row_top + TOC_ROW_H;
    if (row_top < s_toc_scroll) {
        s_toc_scroll = row_top;
    } else if (row_bot > s_toc_scroll + view_h) {
        s_toc_scroll = row_bot - view_h;
    }
    int max_sc = toc_max_scroll(cw, ch);
    if (s_toc_scroll < 0) s_toc_scroll = 0;
    if (s_toc_scroll > max_sc) s_toc_scroll = max_sc;
}

static void audio_deck_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    H rel_x = (H)(evt->pos.x - wnd->client.left);
    H rel_y = (H)(evt->pos.y - wnd->client.top);
    H cw = (H)(wnd->client.right - wnd->client.left);
    H ch = (H)(wnd->client.bottom - wnd->client.top);
    int cmd = 0, sub = 0;

    if (evt->type == EV_BUT_UP) {
        if (s_toc_sb_drag) { s_toc_sb_drag = false; inval_wnd(wnd); }
        return;
    }

    if (evt->type == EV_MOUSE_MOVE) {
        if (s_toc_sb_drag) {
            ProLayout ly;
            calc_pro_layout(&ly, cw, ch);
            int view_h = (int)(ly.toc_box.bottom - ly.toc_box.top - 21);
            int total_h = s_toc_count * TOC_ROW_H;
            int max_sc = (total_h > view_h) ? (total_h - view_h) : 0;
            int track_h = view_h - 24;
            if (max_sc > 0 && track_h > 14) {
                int thumb_h = (track_h * view_h) / (view_h + max_sc);
                if (thumb_h < 14) thumb_h = 14;
                int avail = track_h - thumb_h;
                if (avail > 0) {
                    s_toc_scroll = s_toc_sb_orig_scroll + ((int)(evt->pos.y - s_toc_sb_start_y) * max_sc) / avail;
                    if (s_toc_scroll < 0) s_toc_scroll = 0;
                    if (s_toc_scroll > max_sc) s_toc_scroll = max_sc;
                    inval_wnd(wnd);
                }
            }
            return;
        }
        if (s_menu_ok && app_menu_handle_mouse_move(&s_menu, rel_x, rel_y)) {
            inval_wnd(wnd);
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        if (evt->button == EV_KEY_FROM_WHEEL) {
            int max_sc = toc_max_scroll(cw, ch);
            s_toc_scroll += (evt->key == BTRON_KEY_DOWN) ? (TOC_ROW_H * 3) : -(TOC_ROW_H * 3);
            if (s_toc_scroll < 0) s_toc_scroll = 0;
            if (s_toc_scroll > max_sc) s_toc_scroll = max_sc;
            inval_wnd(wnd);
            return;
        }
        UW k = evt->key;
        if (k == BTRON_KEY_SPACE || k == ' ') { cassette_cmd(C_PLAY_TOGGLE); return; }
        if (k == '<' || k == ',') { cassette_cmd(C_PREV_TRACK); return; }
        if (k == '>' || k == '.') { cassette_cmd(C_NEXT_TRACK); return; }
        if (k == '[')             { cassette_cmd(C_PREV_PART); return; }
        if (k == ']')             { cassette_cmd(C_NEXT_PART); return; }
        if (k == 'v' || k == 'V') { cassette_cmd(C_SYNTH_VIS); return; }
        if (k == 'e' || k == 'E') { cassette_cmd(C_EDIT_BSF); return; }
        if (k == 'r' || k == 'R') { cassette_cmd(C_RELOAD_BSF); return; }
        if (k == 'a' || k == 'A') { cassette_cmd(C_AUTO_ADV); return; }
        if (k == 'm' || k == 'M') { cassette_cmd(C_MUTE); return; }
        /* Lynx-like Right/Left tree navigation in TOC (never changes active playback) */
        if (k == BTRON_KEY_RIGHT) {
            if (s_toc_sel >= 0 && s_toc_sel < s_toc_count) {
                TocRow r = s_toc[s_toc_sel];
                if (r.kind == TOC_BSF_TRACK) {
                    if (!s_toc_expanded[r.a]) {
                        s_toc_expanded[r.a] = true;
                        cassette_toc_rebuild();
                    } else if (s_toc_sel + 1 < s_toc_count && s_toc[s_toc_sel + 1].kind == TOC_BSF_PART) {
                        s_toc_sel++;
                    }
                }
                toc_ensure_visible(cw, ch);
                inval_wnd(wnd);
            }
            return;
        }
        if (k == BTRON_KEY_LEFT) {
            if (s_toc_sel >= 0 && s_toc_sel < s_toc_count) {
                TocRow r = s_toc[s_toc_sel];
                if (r.kind == TOC_BSF_TRACK && s_toc_expanded[r.a]) {
                    s_toc_expanded[r.a] = false;
                    cassette_toc_rebuild();
                } else if (r.kind == TOC_BSF_PART) {
                    /* Jump back up to parent track row */
                    for (int j = s_toc_sel - 1; j >= 0; j--) {
                        if (s_toc[j].kind == TOC_BSF_TRACK && s_toc[j].a == r.a) {
                            s_toc_sel = j;
                            break;
                        }
                    }
                }
                toc_ensure_visible(cw, ch);
                inval_wnd(wnd);
            }
            return;
        }
        if (k == BTRON_KEY_UP || k == BTRON_KEY_DOWN) {
            int dir = (k == BTRON_KEY_DOWN) ? 1 : -1;
            int nxt = s_toc_sel + dir;
            while (nxt >= 0 && nxt < s_toc_count && s_toc[nxt].kind == TOC_HEADER) {
                nxt += dir;
            }
            if (nxt >= 0 && nxt < s_toc_count) {
                s_toc_sel = nxt;
                toc_ensure_visible(cw, ch);
                inval_wnd(wnd);
            }
            return;
        }
        if (k == BTRON_KEY_RETURN || k == '\r' || k == '\n') {
            if (s_toc_sel >= 0 && s_toc_sel < s_toc_count) {
                const TocRow *r = &s_toc[s_toc_sel];
                if (r->kind == TOC_BSF_TRACK) cassette_play_bsf(r->a, 0);
                else if (r->kind == TOC_BSF_PART) cassette_play_bsf(r->a, r->b);
                else if (r->kind == TOC_LEGACY) cassette_begin_legacy(r->a, 1);
                else if (r->kind == TOC_SHIM || r->kind == TOC_PRIM) cassette_edit_selected_bsf();
                inval_wnd(wnd);
            }
            return;
        }
        if (s_menu_ok && app_menu_handle_key(&s_menu, k, (uint16_t)(uintptr_t)evt->data, &cmd)) {
            if (cmd > 0) cassette_cmd(cmd);
        }
        return;
    }

    if (evt->type != EV_BUT_DOWN) return;

    if (s_menu_ok && (rel_y < DECK_MENU_H || s_menu.active_menu >= 0)) {
        if (app_menu_handle_mouse_down(&s_menu, rel_x, rel_y, &cmd, &sub)) {
            if (cmd > 0) cassette_cmd(cmd);
            else if (g_audio_wnd) inval_wnd(wnd);
            return;
        }
    }

    ProLayout ly;
    calc_pro_layout(&ly, cw, ch);

    /* Transport Buttons */
    static const int k_tcmds[8] = {
        C_PREV_TRACK, C_PREV_PART, C_PLAY_TOGGLE, C_STOP,
        C_NEXT_PART, C_NEXT_TRACK, C_AUTO_ADV, C_MUTE
    };
    for (int i = 0; i < 8; i++) {
        if (hit_rect(rel_x, rel_y, &ly.btn[i])) {
            cassette_cmd(k_tcmds[i]);
            return;
        }
    }

    /* Footer Buttons */
    static const int k_fcmds[4] = { C_SYNTH_VIS, C_EDIT_BSF, C_RELOAD_BSF, C_CLOSE };
    for (int i = 0; i < 4; i++) {
        if (hit_rect(rel_x, rel_y, &ly.fbtn[i])) {
            cassette_cmd(k_fcmds[i]);
            return;
        }
    }

    /* Hierarchical DJ/VJ TOC & Scrollbar */
    if (rel_x >= ly.toc_box.left && rel_x <= ly.toc_box.right &&
        rel_y >= ly.toc_box.top + 19 && rel_y <= ly.toc_box.bottom) {
        H sb_x = (H)(ly.toc_box.right - TOC_SB_W - 1);
        H list_top = (H)(ly.toc_box.top + 19);
        H list_bot = (H)(ly.toc_box.bottom - 2);
        if (rel_x >= sb_x) {
            if (rel_y < list_top + 12) {
                s_toc_scroll -= TOC_ROW_H * 2;
                if (s_toc_scroll < 0) s_toc_scroll = 0;
            } else if (rel_y > list_bot - 12) {
                int max_sc = toc_max_scroll(cw, ch);
                s_toc_scroll += TOC_ROW_H * 2;
                if (s_toc_scroll > max_sc) s_toc_scroll = max_sc;
            } else {
                s_toc_sb_drag = true;
                s_toc_sb_start_y = evt->pos.y;
                s_toc_sb_orig_scroll = s_toc_scroll;
            }
            inval_wnd(wnd);
            return;
        }
        int idx = (int)((rel_y - list_top + s_toc_scroll) / TOC_ROW_H);
        if (idx >= 0 && idx < s_toc_count) {
            uint64_t now_us = btron_render_perf_us();
            bool is_dbl = (idx == s_last_click_idx) && (now_us >= s_last_click_us) &&
                          ((now_us - s_last_click_us) <= 450000ull);
            s_last_click_idx = idx;
            s_last_click_us = now_us;
            s_toc_sel = idx;
            const TocRow *r = &s_toc[idx];
            if (r->kind == TOC_BSF_TRACK && rel_x < ly.toc_box.left + 22) {
                /* Single-click on > / v chevron toggles expand/collapse */
                s_toc_expanded[r->a] = !s_toc_expanded[r->a];
                s_last_click_idx = -1;
                cassette_toc_rebuild();
            } else if (is_dbl) {
                /* Double-click activates playback (Synth) or source editor (Shims/Primitives) */
                s_last_click_idx = -1;
                if (r->kind == TOC_BSF_TRACK) {
                    cassette_play_bsf(r->a, 0);
                } else if (r->kind == TOC_BSF_PART) {
                    cassette_play_bsf(r->a, r->b);
                } else if (r->kind == TOC_LEGACY) {
                    cassette_begin_legacy(r->a, 1);
                } else if (r->kind == TOC_SHIM) {
                    const BsfLibItem *si = bsf_get_shim_info(r->a);
                    if (si) (void)open_t_editor_window_with_file(si->filepath);
                } else if (r->kind == TOC_PRIM) {
                    const BsfLibItem *pi = bsf_get_prim_info(r->a);
                    if (pi) (void)open_t_editor_window_with_file(pi->filepath);
                }
            }
            inval_wnd(wnd);
        }
    }
}

static void destroy_audio_player(WND *wnd) {
    (void)wnd;
    /* Only halt legacy/orchestra playback on Cassette close; keep BSF Synth playing if active */
    if (s_owner == 1 || s_owner == 2) {
        cassette_release();
    }
    g_audio_wnd = NULL;
}

WND *open_audio_player_window(void) {
    if (g_audio_wnd) {
        top_wnd(g_audio_wnd);
        return g_audio_wnd;
    }
    bool bsf_active = bsf_is_playing();
    memset(&g_deck, 0, sizeof(g_deck));
    g_deck.pl_on = -1;
    if (bsf_active || s_owner == 3) {
        s_owner = 3;
        g_deck.state = bsf_active ? DECK_PLAY : DECK_PAUSE;
    } else {
        g_deck.state = DECK_STOP;
    }

    H win_x = 40;
    H win_y = 60;
    g_audio_wnd = opn_wnd("B-System Audio Console (DJ/VJ Deck)",
                          win_x, win_y, DECK_WND_W, DECK_WND_H,
                          WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (!g_audio_wnd) return NULL;

    cassette_toc_rebuild();
    cassette_menu_build();
    g_audio_wnd->paint = audio_deck_paint;
    g_audio_wnd->event_handler = audio_deck_event_handler;
    g_audio_wnd->destroy = destroy_audio_player;
    g_audio_wnd->menu_open = cassette_menu_is_open;
    return g_audio_wnd;
}

void cassette_service(void) {
    int sec = 0;
    OrchView *v = gst_view();
    if (v && v->state == ORCH_GST_PLAYING) {
        orchestra_audio_service(btron_render_perf_us());
    }

    if (bsf_is_playing()) {
        static uint64_t s_last_ui_sample = 0;
        (void)bsf_audio_pump();
        const BsfTelemetry *bt = bsf_get_telemetry();
        if (bt && (bt->sample_counter < s_last_ui_sample ||
                   bt->sample_counter - s_last_ui_sample >= 1470u)) {
            s_last_ui_sample = bt->sample_counter;
            synth_ui_tick();
            if (s_owner == 3) {
                g_deck.vu_left  = (int)(bt->peak_l * 100.0f);
                g_deck.vu_right = (int)(bt->peak_r * 100.0f);
                if (g_deck.vu_left  > g_deck.vu_peak_l) g_deck.vu_peak_l = g_deck.vu_left;
                if (g_deck.vu_right > g_deck.vu_peak_r) g_deck.vu_peak_r = g_deck.vu_right;
                g_deck.rms_l = bt->rms_l;
                g_deck.rms_r = bt->rms_r;
                g_deck.limiter_hit = bt->limiter_hit;
                g_deck.seconds_elapsed = (int)(bt->sample_counter / BSF_SAMPLE_RATE);
                if (g_audio_wnd) inval_wnd(g_audio_wnd);
            }
        }
        if (s_owner == 3) return;
    }

    /* Standalone LilCu64 Demo audio pump + 60 Hz Synth Visualizer sync (even when Cassette is stopped) */
    if (lilcu64_synth_get_active_track() > 0 && !(s_owner == 1 && g_deck.state == DECK_PLAY)) {
        static uint64_t s_lc_demo_ui_us = 0;
        (void)lilcu64_synth_pump_virtio(0);
        uint64_t now_us = btron_render_perf_us();
        if (now_us < s_lc_demo_ui_us || now_us - s_lc_demo_ui_us >= 16000ull) {
            s_lc_demo_ui_us = now_us;
            synth_ui_tick();
        }
    }

    if (g_deck.pl_on < 0 || g_deck.pl_on >= CASSETTE_PL_N) return;
    if (g_deck.state != DECK_PLAY) return;

    if (s_owner == 1 && g_deck.pl_on < 4) {
        static uint64_t s_lc_ui_us = 0;
        (void)lilcu64_synth_pump_virtio(0);
        uint64_t now_us = btron_render_perf_us();
        if (now_us < s_lc_ui_us || now_us - s_lc_ui_us >= 16000ull) {
            s_lc_ui_us = now_us;
            float pk_l = 0.0f, pk_r = 0.0f, rms_l = 0.0f, rms_r = 0.0f;
            bool lim = false;
            lilcu64_synth_get_meters(&pk_l, &pk_r, &rms_l, &rms_r, &lim);
            g_deck.vu_left  = (int)(pk_l * 100.0f);
            g_deck.vu_right = (int)(pk_r * 100.0f);
            if (g_deck.vu_left  > g_deck.vu_peak_l) g_deck.vu_peak_l = g_deck.vu_left;
            if (g_deck.vu_right > g_deck.vu_peak_r) g_deck.vu_peak_r = g_deck.vu_right;
            g_deck.rms_l = rms_l;
            g_deck.rms_r = rms_r;
            g_deck.limiter_hit = lim;
            synth_ui_tick();
            if (g_audio_wnd) inval_wnd(g_audio_wnd);
        }
        sec = (int)lilcu64_synth_get_track_time();
        if (sec < 0) sec = 0;
        if (s_seen > 1 && sec < s_seen) {
            cassette_step_track(1);
            if (g_audio_wnd) inval_wnd(g_audio_wnd);
            return;
        }
        s_seen = sec;
    } else if (s_owner == 2 && g_deck.pl_on >= 4) {
        v = gst_view();
        if (v && v->sample_rate != 0u) sec = (int)(v->npos / v->sample_rate);
        if (v) {
            g_deck.vu_left = (int)v->meter_l;
            g_deck.vu_right = (int)v->meter_r;
            if (g_deck.vu_left > g_deck.vu_peak_l) g_deck.vu_peak_l = g_deck.vu_left;
            if (g_deck.vu_right > g_deck.vu_peak_r) g_deck.vu_peak_r = g_deck.vu_right;
        }
    } else {
        return;
    }
    if (sec != g_deck.seconds_elapsed) {
        g_deck.seconds_elapsed = sec;
        if (g_audio_wnd) inval_wnd(g_audio_wnd);
    }
}

WND *open_cassette_about_window(void) {
    return app_menu_create_about_dialog("Audio Console", "音響機器 (DJ/VJ Deck)",
        "Pro B-System Audio Console & BSF Event TOC",
        "44.1 kHz S16 · Synced with HAL-9000 BSF Synth",
        280, 160);
}
