/*
 * segui_palm.c — Palm OS-Like Application Runtime & 8 Frozen PicoCalc Apps
 *
 * Clean-slate replacement of legacy segui_demo.c and segui_phone.c.
 * Zero libc, zero post-boot heap, zero pixel coordinates in any app.
 *
 * Frozen 8-App Suite (4x2 Home Launcher):
 *   1. Terminal    (SEGUI_W_TEXT)
 *   2. Editor      (SEGUI_W_LABEL + SEGUI_W_TEXT)
 *   3. Browser     (SEGUI_W_LIST + SEGUI_W_TEXT)
 *   4. Cabinet     (SEGUI_W_LIST)
 *   5. GLGears     (SEGUI_W_CANVAS — banded 16-bit Z-buffer 3D gears)
 *   6. Contacts    (SEGUI_W_LIST — opens directly into Phone)
 *   7. Phone       (SEGUI_W_CHAT + SEGUI_W_LIST)
 *   8. Preferences (SEGUI_W_LIST — custom PicoCalc hardware/theme prefs)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <btron/segui_palm.h>
#include <btron/ui_mode.h>
#include <btron/troncode.h>
#include <btron/tip.h>
#include <btron/mozc_engine.h>
#include <btron/wylie.h>
#include "../../../apps/cabinet/cabinet_core.h"
#include "../../../apps/editor/teditor_core.h"
#include "../../../apps/browser/tad_parser.h"
#include "../../../apps/chat/chat_core.h"
#include "../../../apps/terminal/shell_core.h"
#if (defined(PICO_REAL_HW) && !defined(PICOCALC_APP_QUAKE)) || defined(SEGUI_HOST_XMB)
#include "../../../media/xmb/xmb_pico.h"
#define SEGUI_HAVE_XMB 1
#endif

#if !defined(BTRON_SEGUI_COMPACT)
BTRON_UI_MODE g_btron_ui_mode = BTRON_UI_SEGUI;
#endif

/* ── Minimal Freestanding Helpers (no libc) ────────────────────────────── */

static int p_strlen(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void p_strlcpy(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0) return;
    if (src) {
        for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void p_strlcat(char *dst, int cap, const char *src)
{
    int n = p_strlen(dst);
    int i = 0;
    if (!dst || !src || n >= cap - 1) return;
    for (; n + i < cap - 1 && src[i]; i++) dst[n + i] = src[i];
    dst[n + i] = '\0';
}

static void p_u32_dec(uint32_t v, char *buf, int cap)
{
    char tmp[12];
    int n = 0, i;
    if (!buf || cap <= 0) return;
    if (v == 0) tmp[n++] = '0';
    else while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (i = 0; i < n && i < cap - 1; i++) buf[i] = tmp[n - 1 - i];
    buf[i] = '\0';
}

static void p_format_pct(char *dst, int cap, uint32_t val, uint32_t maxv)
{
    char num[12];
    uint32_t pct = maxv > 0 ? (val * 100u) / maxv : 0;
    p_u32_dec(pct, num, (int)sizeof(num));
    p_strlcpy(dst, cap, num);
    p_strlcat(dst, cap, "%");
}

/* ── Runtime State (100% Static, Zero Heap) ────────────────────────────── */

static SEGUI_APP                s_app;
static const SEGUI_PROFILE     *s_prof = NULL;
static const SEGUI_THEME       *s_theme = NULL;
static int                      s_theme_idx = 0;
static SEGUI_PALM_HW_OPS        s_hw;
static SEGUI_PALM_APP_ID        s_active_id = PALM_APP_LAUNCHER;

/* Pull-down Menu & Modal Alert Overlay State */
static BOOL                     s_menu_open = FALSE;
static int                      s_menu_focus = 0;
static BOOL                     s_alert_open = FALSE;
static char                     s_alert_title[32];
static char                     s_alert_msg[64];
static void                   (*s_alert_confirm)(void) = NULL;

/* Hardware preference state */
static uint8_t                  s_lcd_bkl = 200;
static uint8_t                  s_kbd_bkl = 0;    /* Battery-first: OFF at boot */
static int                      s_uart_mirror = 1;
static int                      s_battery_pct = 86;

/* ── Shared Virtual RAM /SYS Cabinet Store ─────────────────────────────── */

#define TERM_LINES   32
#define TERM_STRIDE  40
static char           s_term_buf[TERM_LINES * TERM_STRIDE];
static SEGUI_TEXT_DOC s_term_doc;
static char           s_term_input[32];
static int            s_term_in_len = 0;

#define EDIT_LINES   48
#define EDIT_STRIDE  84
static char           s_edit_buf[EDIT_LINES * EDIT_STRIDE];
static TEditorCore    s_ed_core;
static SEGUI_TEXT_DOC s_edit_doc;
static char           s_edit_name[CAB_NAME_MAX] = "NORTIA.TAD";
static char           s_edit_hdr[SEGUI_LABEL_MAX];
static char           s_edit_save_tmp[CAB_RW_BUF_CAP];

#define BROWSE_LINES  64
#define BROWSE_STRIDE 108
static char           s_browse_buf[BROWSE_LINES * BROWSE_STRIDE];
static TAD_VIEW       s_tad_view;
static SEGUI_TEXT_DOC s_browse_doc;
static int            s_browse_file_idx = -1; /* defaults to OSAL_BOOK.TAD */
static int            s_browse_link_idx = -1; /* active walkable TAD link  */

/* Editor /tip/ IME state (0=EN, 1=JP Mozc KKC, 2=BO Wylie Tibetan) */
static int            s_tip_mode = 0;
static char           s_tip_raw[24];
static int            s_tip_len  = 0;
static char           s_tip_out[48];
static TIP_CANDIDATE  s_tip_cands[6];
static int            s_tip_ncands = 0;
static int            s_tip_cand_i = 0;

/* ── 1. Terminal App ───────────────────────────────────────────────────── */

static void term_append_line(const char *s)
{
    int dst_idx;
    if (s_term_doc.n_lines < TERM_LINES) {
        dst_idx = s_term_doc.n_lines++;
    } else {
        int i;
        for (i = 0; i < TERM_LINES - 1; i++) {
            p_strlcpy(s_term_buf + i * TERM_STRIDE, TERM_STRIDE,
                      s_term_buf + (i + 1) * TERM_STRIDE);
        }
        dst_idx = TERM_LINES - 1;
    }
    p_strlcpy(s_term_buf + dst_idx * TERM_STRIDE, TERM_STRIDE, s);
    if (s_term_doc.n_lines > 13) s_term_doc.top_line = s_term_doc.n_lines - 13;
}

static void term_out_cb(const char *text, void *ctx)
{
    static char cur_line[TERM_STRIDE];
    static int  cur_len = 0;
    (void)ctx;
    if (!text) return;
    while (*text) {
        if (*text == '\n' || cur_len >= TERM_STRIDE - 1) {
            cur_line[cur_len] = '\0';
            term_append_line(cur_line);
            cur_len = 0;
            if (*text == '\n') text++;
        } else {
            cur_line[cur_len++] = *text++;
        }
    }
}

static void term_update_prompt(void)
{
    char p[TERM_STRIDE];
    int last = s_term_doc.n_lines > 0 ? s_term_doc.n_lines - 1 : 0;
    p_strlcpy(p, (int)sizeof(p), "btron-pico# ");
    p_strlcat(p, (int)sizeof(p), s_term_input);
    p_strlcpy(s_term_buf + last * TERM_STRIDE, TERM_STRIDE, p);
}

static void term_init_once(void)
{
    if (s_term_doc.lines) return;
    s_term_doc.lines        = s_term_buf;
    s_term_doc.line_stride  = TERM_STRIDE;
    s_term_doc.max_lines    = TERM_LINES;
    s_term_doc.n_lines      = 0;
    s_term_doc.top_line     = 0;
    s_term_doc.editable     = FALSE;
    s_term_doc.monospace_bg = TRUE;
    term_append_line("B-System/BTRON3 3.20 PicoCalc");
    term_append_line("Cortex-M Cleanroom RTOS + SegUI");
    term_append_line("Type 'help' or press F1 Home.");
    term_append_line("btron-pico# ");
}

static void term_menu_clear(void)
{
    s_term_doc.n_lines = 0;
    s_term_doc.top_line = 0;
    s_term_in_len = 0;
    s_term_input[0] = '\0';
    term_append_line("btron-pico# ");
}

static void term_run_cmd(const char *cmd)
{
    BtronShellHooks hooks;
    hooks.board_name     = s_hw.board_name;
    hooks.cpu_name       = s_hw.cpu_name;
    hooks.get_sram_used  = s_hw.get_sram_used;
    hooks.get_sram_total = s_hw.get_sram_total;
    hooks.fallback_exec  = (void (*)(const char *, BtronShellOutFn, void *))s_hw.shell_exec;
    (void)btron_shell_core_exec(cmd, term_out_cb, NULL, &hooks);
}

static void term_menu_ver(void)
{
    term_run_cmd("ver");
    term_append_line("btron-pico# ");
}

static void build_terminal(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    term_init_once();
    segui_app_init(app, "端末 (Terminal)", prof);
    tab = segui_add_tab(app, "Shell", FALSE);
    segui_add_text(app, tab, &s_term_doc);
}

static BOOL key_terminal(int key)
{
    if (key == PALM_KEY_ENTER) {
        s_term_input[s_term_in_len] = '\0';
        if (s_term_in_len > 0) {
            term_run_cmd(s_term_input);
        }
        s_term_in_len = 0;
        s_term_input[0] = '\0';
        term_append_line("btron-pico# ");
        return TRUE;
    }
    if (key == PALM_KEY_BS && s_term_in_len > 0) {
        s_term_input[--s_term_in_len] = '\0';
        term_update_prompt();
        return TRUE;
    }
    if (key == PALM_KEY_UP && s_term_doc.top_line > 0) {
        s_term_doc.top_line--;
        return TRUE;
    }
    if (key == PALM_KEY_DOWN && s_term_doc.top_line + 1 < s_term_doc.n_lines) {
        s_term_doc.top_line++;
        return TRUE;
    }
    if (key >= 0x20 && key < 0x7F && s_term_in_len + 1 < (int)sizeof(s_term_input)) {
        s_term_input[s_term_in_len++] = (char)key;
        s_term_input[s_term_in_len]   = '\0';
        term_update_prompt();
        return TRUE;
    }
    return FALSE;
}

/* ── 2. Editor App (Backed by Real Shared teditor_core.c + cabinet_core) ─ */

static void tip_update_preedit(void)
{
    s_tip_out[0] = '\0';
    s_tip_ncands = 0;
    s_tip_cand_i = 0;
    if (s_tip_len <= 0) return;
    if (s_tip_mode == 1) {
        mozc_romaji_to_hiragana(s_tip_raw, s_tip_out, (int)sizeof(s_tip_out));
        s_tip_ncands = mozc_get_candidates(s_tip_out, s_tip_cands, 6);
    } else if (s_tip_mode == 2) {
        wylie_to_tibetan(s_tip_raw, s_tip_out, sizeof(s_tip_out));
    }
}

static void tip_commit(void)
{
    const char *txt = s_tip_out;
    if (s_tip_mode == 1 && s_tip_ncands > 0 && s_tip_cand_i < s_tip_ncands) {
        txt = s_tip_cands[s_tip_cand_i].value;
    }
    if (txt && txt[0]) {
        ted_core_insert_utf8(&s_ed_core, txt);
    } else if (s_tip_len > 0) {
        ted_core_insert_utf8(&s_ed_core, s_tip_raw);
    }
    s_tip_len = 0;
    s_tip_raw[0] = '\0';
    s_tip_out[0] = '\0';
    s_tip_ncands = 0;
    s_tip_cand_i = 0;
}

static void edit_sync_doc(void)
{
    char num[12];
    s_edit_doc.n_lines     = s_ed_core.n_lines;
    s_edit_doc.top_line    = s_ed_core.top_line;
    s_edit_doc.cursor_line = s_ed_core.cursor_line;
    s_edit_doc.cursor_col  = s_ed_core.cursor_col;

    if (s_tip_mode > 0 && s_tip_len > 0) {
        const char *preview = (s_tip_mode == 1 && s_tip_ncands > 0 && s_tip_cand_i < s_tip_ncands)
                              ? s_tip_cands[s_tip_cand_i].value : s_tip_out;
        p_strlcpy(s_edit_hdr, (int)sizeof(s_edit_hdr), s_tip_mode == 1 ? "[日] " : "[བོད] ");
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), s_tip_raw);
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), ">");
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), preview);
    } else {
        p_strlcpy(s_edit_hdr, (int)sizeof(s_edit_hdr), s_edit_name);
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), s_ed_core.dirty ? "*" : " ");
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr),
                  s_tip_mode == 1 ? "[日] L" : (s_tip_mode == 2 ? "[བོད] L" : "[EN] L"));
        p_u32_dec((uint32_t)(s_ed_core.cursor_line + 1), num, (int)sizeof(num));
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), num);
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), ":C");
        p_u32_dec((uint32_t)(s_ed_core.cursor_col + 1), num, (int)sizeof(num));
        p_strlcat(s_edit_hdr, (int)sizeof(s_edit_hdr), num);
    }
    if (s_active_id == PALM_APP_EDITOR && s_app.n_tab > 0 && s_app.tabs[0].n_ctrl > 0) {
        p_strlcpy(s_app.tabs[0].ctrl[0].label, SEGUI_LABEL_MAX, s_edit_hdr);
    }
}

static void edit_cycle_tip(void)
{
    if (s_tip_len > 0) tip_commit();
    s_tip_mode = (s_tip_mode + 1) % 3;
    edit_sync_doc();
}

static void edit_open_file(const char *name)
{
    int idx = cabinet_core_find(name);
    const char *txt = idx >= 0 ? cabinet_core_read(idx, NULL) : NULL;
    p_strlcpy(s_edit_name, (int)sizeof(s_edit_name), name ? name : "NORTIA.TAD");
    ted_core_init(&s_ed_core, s_edit_buf, EDIT_STRIDE, EDIT_LINES);
    ted_core_load(&s_ed_core, txt);
    /* Start view at line 0 so document header is immediately visible */
    s_ed_core.top_line    = 0;
    s_ed_core.cursor_line = 0;
    s_ed_core.cursor_col  = 0;
    s_edit_doc.lines        = s_edit_buf;
    s_edit_doc.line_stride  = EDIT_STRIDE;
    s_edit_doc.max_lines    = EDIT_LINES;
    s_edit_doc.editable     = TRUE;
    s_edit_doc.monospace_bg = FALSE;
    edit_sync_doc();
}

static void edit_init_once(void)
{
    if (s_edit_doc.lines) return;
    cabinet_core_init();
    edit_open_file("NORTIA.TAD");
}

static void edit_menu_save(void)
{
    uint32_t sz = ted_core_save(&s_ed_core, s_edit_save_tmp, (uint32_t)sizeof(s_edit_save_tmp));
    if (cabinet_core_write(s_edit_name, s_edit_save_tmp, sz) == 0) {
        s_ed_core.dirty = 0;
        edit_sync_doc();
        segui_palm_alert("Editor", "Saved to /SYS Cabinet.", NULL);
    } else {
        segui_palm_alert("Editor", "Read-only Flash file.", NULL);
    }
}

static void build_editor(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    edit_init_once();
    edit_sync_doc();
    segui_app_init(app, "文書 (Editor)", prof);
    tab = segui_add_tab(app, "Edit", FALSE);
    segui_add_label(app, tab, s_edit_hdr);
    segui_add_text(app, tab, &s_edit_doc);
}

static BOOL key_editor(int key)
{
    int changed = 0;
    if (key == PALM_KEY_TAB) {
        edit_cycle_tip();
        return TRUE;
    }
    /* Active /tip/ preedit handling (Japanese Mozc KKC / Tibetan Wylie) */
    if (s_tip_mode > 0) {
        if (key == PALM_KEY_ESC && s_tip_len > 0) {
            s_tip_len = 0; s_tip_raw[0] = '\0'; s_tip_out[0] = '\0';
            s_tip_ncands = 0; s_tip_cand_i = 0;
            edit_sync_doc();
            return TRUE;
        }
        if (key == PALM_KEY_BS && s_tip_len > 0) {
            s_tip_raw[--s_tip_len] = '\0';
            tip_update_preedit();
            edit_sync_doc();
            return TRUE;
        }
        if (key == ' ' && s_tip_len > 0) {
            if (s_tip_mode == 1 && s_tip_ncands > 1) {
                s_tip_cand_i = (s_tip_cand_i + 1) % s_tip_ncands;
            } else {
                tip_commit();
                if (s_tip_mode == 2) ted_core_insert_utf8(&s_ed_core, "\xE0\xBC\x8B"); /* Tibetan Tsheg */
            }
            edit_sync_doc();
            return TRUE;
        }
        if (key == PALM_KEY_ENTER && s_tip_len > 0) {
            tip_commit();
            edit_sync_doc();
            return TRUE;
        }
        if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
            key == '\'' || key == '-' || key == '+') {
            if (s_tip_len < (int)sizeof(s_tip_raw) - 1) {
                s_tip_raw[s_tip_len++] = (char)key;
                s_tip_raw[s_tip_len]   = '\0';
                tip_update_preedit();
                edit_sync_doc();
                return TRUE;
            }
        }
    }

    if (key == PALM_KEY_UP)    changed = ted_core_move(&s_ed_core, -1,  0, 10);
    else if (key == PALM_KEY_DOWN)  changed = ted_core_move(&s_ed_core, +1,  0, 10);
    else if (key == PALM_KEY_LEFT)  changed = ted_core_move(&s_ed_core,  0, -1, 10);
    else if (key == PALM_KEY_RIGHT) changed = ted_core_move(&s_ed_core,  0, +1, 10);
    else if (key == PALM_KEY_ENTER) changed = ted_core_newline(&s_ed_core);
    else if (key == PALM_KEY_BS)    changed = ted_core_backspace(&s_ed_core);
    else if (key >= 0x20 && key < 0x7F) changed = ted_core_insert_char(&s_ed_core, (char)key);
    if (changed) {
        edit_sync_doc();
        return TRUE;
    }
    return FALSE;
}

/* ── 3. Browser App (Real tad_parser.c + Walkable TAD Hypertext Links) ─── */

static void browse_sync_link_scroll(void)
{
    if (s_browse_link_idx >= 0 && s_browse_link_idx < s_tad_view.n_links) {
        int ln = s_tad_view.links[s_browse_link_idx].line_idx;
        s_browse_doc.active_line = ln;
        if (ln < s_browse_doc.top_line) {
            s_browse_doc.top_line = ln;
        } else if (ln >= s_browse_doc.top_line + 10) {
            s_browse_doc.top_line = ln - 9;
        }
    } else {
        s_browse_doc.active_line = -1;
    }
}

static void browse_load_doc(int cab_idx)
{
    const char *raw;
    uint32_t sz = 0;
    int i;
    cabinet_core_init();
    if (cab_idx < 0) cab_idx = cabinet_core_find("OSAL_BOOK.TAD");
    if (cab_idx < 0 || cab_idx >= cabinet_core_count()) cab_idx = 0;
    s_browse_file_idx = cab_idx;
    raw = cabinet_core_read(cab_idx, &sz);

    s_tad_view.lines     = s_browse_buf;
    s_tad_view.stride    = BROWSE_STRIDE;
    s_tad_view.max_lines = BROWSE_LINES;
    tad_parse_stream(&s_tad_view, (const uint8_t *)raw, sz, 34);

    s_browse_doc.lines        = s_browse_buf;
    s_browse_doc.line_stride  = BROWSE_STRIDE;
    s_browse_doc.max_lines    = BROWSE_LINES;
    s_browse_doc.n_lines      = s_tad_view.n_lines;
    s_browse_doc.top_line     = 0;
    s_browse_doc.editable     = FALSE;
    s_browse_doc.monospace_bg = FALSE;
    s_browse_doc.link_mask[0] = 0UL;
    s_browse_doc.link_mask[1] = 0UL;
    for (i = 0; i < s_tad_view.n_links; i++) {
        int ln = s_tad_view.links[i].line_idx;
        if (ln >= 0 && ln < 64) {
            s_browse_doc.link_mask[ln >> 5] |= (1UL << (ln & 31));
        }
    }
    s_browse_link_idx = (s_tad_view.n_links > 0) ? 0 : -1;
    s_browse_doc.active_line = (s_browse_link_idx >= 0) ? s_tad_view.links[0].line_idx : -1;
}

static void browser_open_row(SEGUI_APP *app, SEGUI_CTRL *list, int row)
{
    (void)list;
    if (row < 1 || row > cabinet_core_count()) return;
    browse_load_doc(row - 1);
    segui_select_tab(app, 1);
}

static SEGUI_ROW s_browser_rows[CAB_MAX_FILES + 1];

static void build_browser(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int t_idx, t_read, n, i;
    SEGUI_CTRL *lst;
    cabinet_core_init();
    browse_load_doc(s_browse_file_idx);

    s_browser_rows[0].type = SEGUI_ROW_HEADER;
    p_strlcpy(s_browser_rows[0].caption, SEGUI_LABEL_MAX, "/SYS TAD HYPERTEXT INDEX");
    s_browser_rows[0].value[0] = '\0';
    s_browser_rows[0].state = 0;
    s_browser_rows[0].on_activate = NULL;

    n = cabinet_core_count();
    for (i = 0; i < n; i++) {
        const CAB_ENTRY *e = cabinet_core_get(i);
        s_browser_rows[i + 1].type = SEGUI_ROW_ARROW;
        p_strlcpy(s_browser_rows[i + 1].caption, SEGUI_LABEL_MAX, e->name);
        p_strlcpy(s_browser_rows[i + 1].value,   SEGUI_LABEL_MAX, e->is_tad ? "TAD" : "TXT");
        s_browser_rows[i + 1].state = 0;
        s_browser_rows[i + 1].on_activate = browser_open_row;
    }

    segui_app_init(app, "閲覧 (Browser)", prof);
    t_idx  = segui_add_tab(app, "Index", FALSE);
    t_read = segui_add_tab(app, "Read",  FALSE);
    lst = segui_add_list(app, t_idx, s_browser_rows, n + 1);
    if (lst) { lst->row_focus = 1; lst->row_top = 0; }
    segui_add_text(app, t_read, &s_browse_doc);
    segui_select_tab(app, t_read);
}

static BOOL key_browser(int key)
{
    if (s_app.active_tab == 1) {
        if (key == PALM_KEY_ESC || key == PALM_KEY_BS) {
            int osal = cabinet_core_find("OSAL_BOOK.TAD");
            if (osal >= 0 && s_browse_file_idx != osal) {
                browse_load_doc(osal);
            } else {
                segui_select_tab(&s_app, 0);
            }
            return TRUE;
        }
        if (key == PALM_KEY_ENTER && s_browse_link_idx >= 0 &&
            s_browse_link_idx < s_tad_view.n_links) {
            const char *tgt = s_tad_view.links[s_browse_link_idx].target;
            int target = cabinet_core_find(tgt);
            if (target >= 0) {
                browse_load_doc(target);
            } else {
                segui_palm_alert("TAD Link", "Chapter on SD (Phase 4):", NULL);
            }
            return TRUE;
        }
        if (key == PALM_KEY_RIGHT || key == PALM_KEY_DOWN) {
            if (s_tad_view.n_links > 0 && s_browse_link_idx + 1 < s_tad_view.n_links) {
                s_browse_link_idx++;
                browse_sync_link_scroll();
                return TRUE;
            }
            if (s_browse_doc.top_line + 1 < s_browse_doc.n_lines) {
                s_browse_doc.top_line++;
                return TRUE;
            }
        }
        if (key == PALM_KEY_LEFT || key == PALM_KEY_UP) {
            if (s_tad_view.n_links > 0 && s_browse_link_idx > 0) {
                s_browse_link_idx--;
                browse_sync_link_scroll();
                return TRUE;
            }
            if (s_browse_doc.top_line > 0) {
                s_browse_doc.top_line--;
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* ── 4. Cabinet App (Live B-System /SYS Real-Object Volume Manager) ────── */

static SEGUI_ROW s_cabinet_rows[CAB_MAX_FILES + 3];

static void cabinet_open_row(SEGUI_APP *app, SEGUI_CTRL *list, int row)
{
    int n = cabinet_core_count();
    (void)app; (void)list;
    if (row >= 1 && row <= n) {
        const CAB_ENTRY *e = cabinet_core_get(row - 1);
        if (e && e->rw_data) {
            edit_open_file(e->name);
            segui_palm_switch(PALM_APP_EDITOR);
        } else if (e) {
            browse_load_doc(row - 1);
            segui_palm_switch(PALM_APP_BROWSER);
            segui_select_tab(&s_app, 1);
        }
    } else if (row == n + 2) {
        segui_palm_alert("Cabinet", "SD HFDS mount ready for Phase 4.", NULL);
    }
}

static void build_cabinet(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab, n, i, r = 0;
    SEGUI_CTRL *lst;
    char num[12];
    cabinet_core_init();
    n = cabinet_core_count();

    s_cabinet_rows[r].type = SEGUI_ROW_HEADER;
    p_strlcpy(s_cabinet_rows[r].caption, SEGUI_LABEL_MAX, "/SYS (REAL-OBJECT VOL)");
    s_cabinet_rows[r].value[0] = '\0';
    s_cabinet_rows[r].state = 0;
    s_cabinet_rows[r].on_activate = NULL;
    r++;

    for (i = 0; i < n; i++) {
        const CAB_ENTRY *e = cabinet_core_get(i);
        s_cabinet_rows[r].type = SEGUI_ROW_ARROW;
        p_strlcpy(s_cabinet_rows[r].caption, SEGUI_LABEL_MAX, e->name);
        p_u32_dec(e->size, num, (int)sizeof(num));
        p_strlcpy(s_cabinet_rows[r].value, SEGUI_LABEL_MAX, num);
        p_strlcat(s_cabinet_rows[r].value, SEGUI_LABEL_MAX, " B");
        s_cabinet_rows[r].state = 0;
        s_cabinet_rows[r].on_activate = cabinet_open_row;
        r++;
    }

    s_cabinet_rows[r].type = SEGUI_ROW_HEADER;
    p_strlcpy(s_cabinet_rows[r].caption, SEGUI_LABEL_MAX, "/SD0 (SPI0 REMOVABLE)");
    s_cabinet_rows[r].value[0] = '\0';
    s_cabinet_rows[r].state = 0;
    s_cabinet_rows[r].on_activate = NULL;
    r++;

    s_cabinet_rows[r].type = SEGUI_ROW_VALUE;
    p_strlcpy(s_cabinet_rows[r].caption, SEGUI_LABEL_MAX, "SDHC Slot (GP16-19)");
    p_strlcpy(s_cabinet_rows[r].value,   SEGUI_LABEL_MAX, "Idle");
    s_cabinet_rows[r].state = 0;
    s_cabinet_rows[r].on_activate = cabinet_open_row;
    r++;

    segui_app_init(app, "書庫 (Cabinet)", prof);
    tab = segui_add_tab(app, "Volumes", FALSE);
    lst = segui_add_list(app, tab, s_cabinet_rows, r);
    if (lst) { lst->row_focus = 1; lst->row_top = 0; }
}

/* ── 5. Center tile. GLGears on make picocalc. Quake on make picocalc-quake. */

#if defined(PICOCALC_APP_QUAKE)
#include "start_sim.h"
extern void dp_pico_blit_rgb565(const RECT *dst, const uint16_t *src, int src_w, int src_h);

static void draw_quake_canvas(SEGUI_APP *app, GDEV *dev, const RECT *bounds, void *ctx)
{
    (void)app;
    (void)ctx;
    if (!bounds) return;
    start_sim_render();
#if defined(PICO_REAL_HW)
    (void)dev;
    dp_pico_blit_rgb565(bounds, start_sim_pixels(),
                        start_sim_view_w(), start_sim_view_h());
#else
    (void)dev;
#endif
}

static BOOL key_quake(int key)
{
    if (key == PALM_KEY_ESC) return FALSE;
    start_sim_key(key, 1);
    return TRUE;
}

static void build_quake(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    start_sim_init();
    segui_app_init(app, "Quake", prof);
    tab = segui_add_tab(app, "Start", FALSE);
    segui_add_canvas(app, tab, draw_quake_canvas, NULL);
}
#else
static int32_t s_gear_angle = 18;
static int32_t s_gear_yaw   = 22;
static BOOL    s_gear_paused = FALSE;

extern void glgears_segui_render(GDEV *dev, const RECT *bounds, float angle_deg,
                                 float view_rotx, float view_roty);

static void draw_glgears_canvas(SEGUI_APP *app, GDEV *dev, const RECT *bounds, void *ctx)
{
    (void)app; (void)ctx;
    glgears_segui_render(dev, bounds, (float)s_gear_angle * 1.4f, 20.0f,
                         (float)s_gear_yaw * 1.4f);
}

static void gears_menu_pause(void) { s_gear_paused = !s_gear_paused; }
static void gears_menu_reset(void) { s_gear_angle = 18; s_gear_yaw = 22; s_gear_paused = FALSE; }

static void build_glgears(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    segui_app_init(app, "歯車 (GLGears)", prof);
    tab = segui_add_tab(app, "3D", FALSE);
    segui_add_canvas(app, tab, draw_glgears_canvas, NULL);
}

static BOOL key_glgears(int key)
{
    if (key == ' ') { s_gear_paused = !s_gear_paused; return TRUE; }
    if (key == PALM_KEY_LEFT)  { s_gear_yaw = (s_gear_yaw - 8) & 255; return TRUE; }
    if (key == PALM_KEY_RIGHT) { s_gear_yaw = (s_gear_yaw + 8) & 255; return TRUE; }
    return FALSE;
}
#endif

#if defined(SEGUI_HAVE_XMB)
static void draw_xmb_canvas(SEGUI_APP *app, GDEV *dev, const RECT *bounds, void *ctx)
{
    (void)app; (void)ctx;
    if (!bounds) return;
#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
    xmb_pico_host_bind(dev ? (void *)dev->pixels : 0,
                       dev ? (int)dev->width : 0,
                       dev ? (int)dev->height : 0);
#else
    (void)dev;
#endif
    xmb_pico_paint((int)bounds->left, (int)bounds->top,
                   (int)bounds->right, (int)bounds->bottom);
}

static SEGUI_PALM_APP_ID xmb_go_palm(int go)
{
    switch (go) {
    case XMB_GO_TERMINAL: return PALM_APP_TERMINAL;
    case XMB_GO_EDITOR:   return PALM_APP_EDITOR;
    case XMB_GO_BROWSER:  return PALM_APP_BROWSER;
    case XMB_GO_CABINET:  return PALM_APP_CABINET;
    case XMB_GO_GEARS:    return PALM_APP_GLGEARS;
    case XMB_GO_CHAT:     return PALM_APP_CHAT;
    case XMB_GO_PREFS:    return PALM_APP_PREFS;
    default:              return PALM_APP_LAUNCHER;
    }
}

static void build_xmb(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    segui_app_init(app, "XMB", prof);
    tab = segui_add_tab(app, "Bar", FALSE);
    segui_add_canvas(app, tab, draw_xmb_canvas, NULL);
}

static BOOL key_xmb(int key)
{
    int go;
    if (key == PALM_KEY_LEFT)  { xmb_pico_move(0, -1); return TRUE; }
    if (key == PALM_KEY_RIGHT) { xmb_pico_move(0, +1); return TRUE; }
    if (key == PALM_KEY_UP)    { xmb_pico_move(-1, 0); return TRUE; }
    if (key == PALM_KEY_DOWN)  { xmb_pico_move(+1, 0); return TRUE; }
    if (key == PALM_KEY_ENTER) {
        go = xmb_pico_go();
        if (go != XMB_GO_NONE) segui_palm_switch(xmb_go_palm(go));
        return TRUE;
    }
    return FALSE;
}
#endif

#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
/* Host screenshot only. The picocalc elf does not have this app.
 * tools/capture_segui.c paints the start hall into the canvas. */
extern void segui_host_quake_paint(GDEV *dev, const RECT *bounds);

static void draw_quake_canvas(SEGUI_APP *app, GDEV *dev, const RECT *bounds, void *ctx)
{
    (void)app; (void)ctx;
    if (!bounds || !dev) return;
    segui_host_quake_paint(dev, bounds);
}

static void build_quake_preview(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    segui_app_init(app, "Quake", prof);
    tab = segui_add_tab(app, "Start", FALSE);
    segui_add_canvas(app, tab, draw_quake_canvas, NULL);
}
#endif

/* ── 6, 7 & 8. Contacts, Phone & Chat (Backed by Real Shared chat_core.c) ─ */

static char s_dial_hdr[SEGUI_LABEL_MAX];
static SEGUI_ROW s_contacts_rows[CHAT_CORE_MAX_CONTACTS + 2];

static void contacts_open_chat(SEGUI_APP *app, SEGUI_CTRL *list, int row)
{
    (void)app;
    if (!list || row < 0 || row >= list->n_row) return;
    if (list->rows[row].type != SEGUI_ROW_PERSON) return;
    chat_core_select_peer(list->rows[row].caption, list->rows[row].value);
    segui_palm_switch(PALM_APP_CHAT);
}

static SEGUI_ROW s_recents_rows[] = {
    { SEGUI_ROW_HEADER, "RECENT CALLS",     "",       0, NULL },
    { SEGUI_ROW_PERSON, "Ken Sakamura",     "10:15",  0, contacts_open_chat },
    { SEGUI_ROW_PERSON, "Ada Lovelace",     "09:42",  0, contacts_open_chat },
    { SEGUI_ROW_PERSON, "Grace Hopper",     "Yest.",  0, contacts_open_chat },
    { SEGUI_ROW_HEADER, "VOICEMAIL / LINE", "",       0, NULL },
    { SEGUI_ROW_VALUE,  "Radio",            "LoRa",   0, NULL },
    { SEGUI_ROW_VALUE,  "Signal",           "-84 dBm",0, NULL }
};

static SEGUI_ROW s_circles_rows[] = {
    { SEGUI_ROW_HEADER, "CIRCLES",          "",       0, NULL },
    { SEGUI_ROW_ARROW,  "B-System Core",    "6",      0, NULL },
    { SEGUI_ROW_ARROW,  "LoRa Mesh CH23",   "23",     0, NULL },
    { SEGUI_ROW_HEADER, "RECENTLY SEEN",    "",       0, NULL },
    { SEGUI_ROW_PERSON, "Ada Lovelace",     "2 min",  0, contacts_open_chat },
    { SEGUI_ROW_PERSON, "Claude Shannon",   "18 min", 0, contacts_open_chat }
};

static void phone_dial_cb(SEGUI_APP *app, SEGUI_CTRL *c)
{
    char buf[SEGUI_LABEL_MAX];
    (void)app;
    if (!c) return;
    if (c->label[0] == 'C') {
        chat_core_set_dial("");
    } else if (c->label[0] == 'O') {
        chat_core_log_call(chat_core_dial_str(), "Now");
        segui_palm_alert("Phone", "Calling peer on LoRa voice...", NULL);
        return;
    } else {
        p_strlcpy(buf, (int)sizeof(buf), chat_core_dial_str());
        if (p_strlen(buf) < 16) {
            p_strlcat(buf, (int)sizeof(buf), c->label);
            chat_core_set_dial(buf);
        }
    }
    p_strlcpy(s_dial_hdr, (int)sizeof(s_dial_hdr), "Dial: ");
    p_strlcat(s_dial_hdr, (int)sizeof(s_dial_hdr), chat_core_dial_str());
}

static void build_contacts(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab, n, i, r = 0;
    SEGUI_CTRL *lst;
    chat_core_init();
    n = chat_core_contact_count();

    s_contacts_rows[r].type = SEGUI_ROW_HEADER;
    p_strlcpy(s_contacts_rows[r].caption, SEGUI_LABEL_MAX, "FAVOURITES");
    s_contacts_rows[r].value[0] = '\0';
    s_contacts_rows[r].state = 0;
    s_contacts_rows[r].on_activate = NULL;
    r++;
    for (i = 0; i < n; i++) {
        const CHAT_CONTACT *ct = chat_core_contact(i);
        if (!ct || !ct->favourite) continue;
        s_contacts_rows[r].type = SEGUI_ROW_PERSON;
        p_strlcpy(s_contacts_rows[r].caption, SEGUI_LABEL_MAX, ct->name);
        p_strlcpy(s_contacts_rows[r].value,   SEGUI_LABEL_MAX, ct->addr);
        s_contacts_rows[r].state = 0;
        s_contacts_rows[r].on_activate = contacts_open_chat;
        r++;
    }
    s_contacts_rows[r].type = SEGUI_ROW_HEADER;
    p_strlcpy(s_contacts_rows[r].caption, SEGUI_LABEL_MAX, "ALL CONTACTS");
    s_contacts_rows[r].value[0] = '\0';
    s_contacts_rows[r].state = 0;
    s_contacts_rows[r].on_activate = NULL;
    r++;
    for (i = 0; i < n; i++) {
        const CHAT_CONTACT *ct = chat_core_contact(i);
        if (!ct || ct->favourite) continue;
        s_contacts_rows[r].type = SEGUI_ROW_PERSON;
        p_strlcpy(s_contacts_rows[r].caption, SEGUI_LABEL_MAX, ct->name);
        p_strlcpy(s_contacts_rows[r].value,   SEGUI_LABEL_MAX, ct->addr);
        s_contacts_rows[r].state = 0;
        s_contacts_rows[r].on_activate = contacts_open_chat;
        r++;
    }

    segui_app_init(app, "連絡 (Contacts)", prof);
    tab = segui_add_tab(app, "Directory", FALSE);
    lst = segui_add_list(app, tab, s_contacts_rows, r);
    if (lst) { lst->row_focus = 1; lst->row_top = 0; }
}

static void build_phone(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int t_dial, t_rec;
    SEGUI_CTRL *lst;
    chat_core_init();
    p_strlcpy(s_dial_hdr, (int)sizeof(s_dial_hdr), "Dial: ");
    p_strlcat(s_dial_hdr, (int)sizeof(s_dial_hdr), chat_core_dial_str());
    segui_app_init(app, "電話 (Phone)", prof);
    t_dial = segui_add_tab(app, "Keypad",  TRUE);
    t_rec  = segui_add_tab(app, "Recents", FALSE);
    segui_add_label(app, t_dial, s_dial_hdr);
    segui_add_button(app, t_dial, "1", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "2", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "3", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "4", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "5", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "6", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "7", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "8", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "9", SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "Clr",  SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "0",    SEGUI_ICON_NONE, phone_dial_cb);
    segui_add_button(app, t_dial, "Open", SEGUI_ICON_NONE, phone_dial_cb);
    lst = segui_add_list(app, t_rec, s_recents_rows,
                         (int)(sizeof(s_recents_rows) / sizeof(s_recents_rows[0])));
    if (lst) { lst->row_focus = 1; lst->row_top = 0; }
}

static void build_chat(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int t_chat, t_circles, n_msg = 0;
    SEGUI_MSG *msgs;
    SEGUI_CTRL *lst;
    chat_core_init();
    msgs = chat_core_msgs(&n_msg);
    segui_app_init(app, "対話 (Chat)", prof);
    t_chat    = segui_add_tab(app, "Thread",  FALSE);
    t_circles = segui_add_tab(app, "Circles", FALSE);
    segui_add_chat(app, t_chat, msgs, n_msg, chat_core_peer_name());
    lst = segui_add_list(app, t_circles, s_circles_rows,
                         (int)(sizeof(s_circles_rows) / sizeof(s_circles_rows[0])));
    if (lst) { lst->row_focus = 1; lst->row_top = 0; }
}

/* ── 9. Custom PicoCalc Preferences Window ─────────────────────────────── */

static const char *const s_theme_names[] = { "Palm", "Handset", "Retro", "Appliance" };

static void prefs_refresh_values(void);
static void prefs_confirm_poweroff(void)
{
    if (s_hw.power_off) s_hw.power_off();
}
static void prefs_confirm_bootsel(void)
{
    if (s_hw.reboot_bootsel) s_hw.reboot_bootsel();
}

static void prefs_activate(SEGUI_APP *app, SEGUI_CTRL *list, int row)
{
    (void)app; (void)list;
    switch (row) {
    case 1: /* LCD Backlight */
        s_lcd_bkl = (uint8_t)((s_lcd_bkl + 32u) & 0xFFu);
        if (s_lcd_bkl < 32u) s_lcd_bkl = 32u;
        if (s_hw.set_lcd_backlight) s_hw.set_lcd_backlight(s_lcd_bkl);
        break;
    case 2: /* Theme */
        s_theme_idx = (s_theme_idx + 1) % 4;
        s_theme = segui_theme_named(s_theme_names[s_theme_idx]);
        s_app.theme = s_theme;
        break;
    case 4: /* Key Backlight */
        s_kbd_bkl = (s_kbd_bkl == 0) ? 128 : (s_kbd_bkl < 240 ? 255 : 0);
        if (s_hw.set_kbd_backlight) s_hw.set_kbd_backlight(s_kbd_bkl);
        break;
    case 6: /* Power Off */
        segui_palm_alert("Power Off", "Shut down PicoCalc via STM32?", prefs_confirm_poweroff);
        break;
    case 10: /* UART0 Mirror */
        s_uart_mirror = !s_uart_mirror;
        if (s_hw.set_uart_mirror) s_hw.set_uart_mirror(s_uart_mirror);
        break;
    case 11: /* Reboot to BOOTSEL */
        segui_palm_alert("BOOTSEL", "Reboot Pico into USB UF2 mode?", prefs_confirm_bootsel);
        break;
    default:
        break;
    }
    prefs_refresh_values();
}

static SEGUI_ROW s_prefs_rows[] = {
    { SEGUI_ROW_HEADER, "DISPLAY (ST7365P 320x320)", "",          0, NULL },
    { SEGUI_ROW_VALUE,  "LCD Backlight (0x05)",      "78%",       0, prefs_activate },
    { SEGUI_ROW_VALUE,  "SegUI Theme",               "Handset",   0, prefs_activate },
    { SEGUI_ROW_HEADER, "KEYBOARD & POWER (0x1F)",   "",          0, NULL },
    { SEGUI_ROW_VALUE,  "Key Backlight (0x0A)",      "OFF",       0, prefs_activate },
    { SEGUI_ROW_VALUE,  "Battery (0x0B)",            "86%",       0, NULL },
    { SEGUI_ROW_ARROW,  "Power Off (0x0E)...",       "",          0, prefs_activate },
    { SEGUI_ROW_HEADER, "SYSTEM & RTOS",             "",          0, NULL },
    { SEGUI_ROW_VALUE,  "Board",                     "RP2350",    0, NULL },
    { SEGUI_ROW_VALUE,  "Static SRAM",               "58 KiB",    0, NULL },
    { SEGUI_ROW_TOGGLE, "UART0 Console Mirror",      "",          SEGUI_ST_SELECTED, prefs_activate },
    { SEGUI_ROW_ARROW,  "Reboot to BOOTSEL...",      "",          0, prefs_activate }
};

static void prefs_refresh_values(void)
{
    char num[16];
    p_format_pct(s_prefs_rows[1].value, SEGUI_LABEL_MAX, s_lcd_bkl, 255);
    p_strlcpy(s_prefs_rows[2].value, SEGUI_LABEL_MAX, s_theme_names[s_theme_idx]);
    if (s_kbd_bkl == 0) p_strlcpy(s_prefs_rows[4].value, SEGUI_LABEL_MAX, "OFF");
    else p_format_pct(s_prefs_rows[4].value, SEGUI_LABEL_MAX, s_kbd_bkl, 255);

    if (s_hw.get_battery_pct) s_battery_pct = s_hw.get_battery_pct();
    p_u32_dec((uint32_t)s_battery_pct, num, (int)sizeof(num));
    p_strlcpy(s_prefs_rows[5].value, SEGUI_LABEL_MAX, num);
    p_strlcat(s_prefs_rows[5].value, SEGUI_LABEL_MAX, "%");

    p_strlcpy(s_prefs_rows[8].value, SEGUI_LABEL_MAX,
              s_hw.board_name ? s_hw.board_name : "RP2350");

    if (s_hw.get_sram_used) {
        p_u32_dec(s_hw.get_sram_used() / 1024u, num, (int)sizeof(num));
        p_strlcpy(s_prefs_rows[9].value, SEGUI_LABEL_MAX, num);
        p_strlcat(s_prefs_rows[9].value, SEGUI_LABEL_MAX, " KiB");
    }
    if (s_uart_mirror) s_prefs_rows[10].state |= SEGUI_ST_SELECTED;
    else               s_prefs_rows[10].state &= ~SEGUI_ST_SELECTED;
}

static void build_prefs(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    SEGUI_CTRL *lst;
    prefs_refresh_values();
    segui_app_init(app, "設定 (Prefs)", prof);
    tab = segui_add_tab(app, "Prefs", FALSE);
    lst = segui_add_list(app, tab, s_prefs_rows,
                         (int)(sizeof(s_prefs_rows) / sizeof(s_prefs_rows[0])));
    if (lst) { lst->row_focus = 1; lst->row_top = 0; }
}

/* ── 0. Palm Home Launcher (Symmetric 3x3 Autolaid-Out Tile Grid) ──────── */

static void launch_cb_term(SEGUI_APP *a, SEGUI_CTRL *c)     { (void)a;(void)c; segui_palm_switch(PALM_APP_TERMINAL); }
static void launch_cb_edit(SEGUI_APP *a, SEGUI_CTRL *c)     { (void)a;(void)c; segui_palm_switch(PALM_APP_EDITOR);   }
static void launch_cb_browse(SEGUI_APP *a, SEGUI_CTRL *c)   { (void)a;(void)c; segui_palm_switch(PALM_APP_BROWSER);  }
static void launch_cb_cab(SEGUI_APP *a, SEGUI_CTRL *c)      { (void)a;(void)c; segui_palm_switch(PALM_APP_CABINET);  }
static void launch_cb_gears(SEGUI_APP *a, SEGUI_CTRL *c)    { (void)a;(void)c; segui_palm_switch(PALM_APP_GLGEARS);  }
static void launch_cb_contacts(SEGUI_APP *a, SEGUI_CTRL *c) { (void)a;(void)c; segui_palm_switch(PALM_APP_CONTACTS); }
static void launch_cb_phone(SEGUI_APP *a, SEGUI_CTRL *c)    { (void)a;(void)c; segui_palm_switch(PALM_APP_PHONE);    }
static void launch_cb_chat(SEGUI_APP *a, SEGUI_CTRL *c)     { (void)a;(void)c; segui_palm_switch(PALM_APP_CHAT);     }
static void launch_cb_prefs(SEGUI_APP *a, SEGUI_CTRL *c)    { (void)a;(void)c; segui_palm_switch(PALM_APP_PREFS);    }

#if defined(SEGUI_HAVE_XMB)
static int s_launch_page; /* 0 = the nine apps, 1 = the XMB tile */

static void launch_cb_xmb(SEGUI_APP *a, SEGUI_CTRL *c)
{
    (void)a; (void)c;
    segui_palm_switch(PALM_APP_XMB);
}
#endif

static void build_launcher(SEGUI_APP *app, const SEGUI_PROFILE *prof)
{
    int tab;
    segui_app_init(app, "B-System", prof);
#if defined(SEGUI_HAVE_XMB)
    if (s_launch_page) {
        tab = segui_add_tab(app, "XMB", TRUE);
        segui_add_button(app, tab, "XMB", SEGUI_ICON_WINDOW, launch_cb_xmb);
        return;
    }
#endif
    tab = segui_add_tab(app, "Apps", TRUE);
    segui_add_button(app, tab, "Terminal", SEGUI_ICON_TERMINAL, launch_cb_term);
    segui_add_button(app, tab, "Editor",   SEGUI_ICON_EDITOR,   launch_cb_edit);
    segui_add_button(app, tab, "Browser",  SEGUI_ICON_BROWSER,  launch_cb_browse);
    segui_add_button(app, tab, "Cabinet",  SEGUI_ICON_CABINET,  launch_cb_cab);
#if defined(PICOCALC_APP_QUAKE)
    segui_add_button(app, tab, "Quake",    SEGUI_ICON_BOLT,     launch_cb_gears);
#else
    segui_add_button(app, tab, "GLGears",  SEGUI_ICON_GEARS,    launch_cb_gears);
#endif
    segui_add_button(app, tab, "Contacts", SEGUI_ICON_CONTACTS, launch_cb_contacts);
    segui_add_button(app, tab, "Phone",    SEGUI_ICON_PHONE,    launch_cb_phone);
    segui_add_button(app, tab, "Chat",     SEGUI_ICON_CHAT,     launch_cb_chat);
    segui_add_button(app, tab, "Prefs",    SEGUI_ICON_PREFS,    launch_cb_prefs);
}

/* ── App Registry & Menus ──────────────────────────────────────────────── */

static void menu_about(void)
{
    segui_palm_alert("B-System 3.20", "PicoCalc Palm SegUI (Zero-Heap)", NULL);
}

static void menu_go_home(void)
{
    segui_palm_switch(PALM_APP_LAUNCHER);
}

static void menu_term_ls(void)       { term_run_cmd("ls"); term_append_line("btron-pico# "); }
static void menu_term_diag(void)     { term_run_cmd("diag"); term_append_line("btron-pico# "); }
static void menu_edit_nortia(void)   { edit_open_file("NORTIA.TAD"); }
static void menu_edit_heart(void)    { edit_open_file("HEART_SUTRA.TAD"); }
static void menu_browse_osal(void)   { browse_load_doc(cabinet_core_find("OSAL_BOOK.TAD"));   segui_select_tab(&s_app, 1); }
static void menu_browse_heart(void)  { browse_load_doc(cabinet_core_find("HEART_SUTRA.TAD")); segui_select_tab(&s_app, 1); }
static void menu_browse_boot(void)   { browse_load_doc(cabinet_core_find("BOOT.LOG"));        segui_select_tab(&s_app, 1); }
static void menu_browse_index(void)  { segui_select_tab(&s_app, 0); }
static void menu_cab_df(void)        { segui_palm_alert("/SYS BROM", "OSAL + NORTIA + HEART + BOOT", NULL); }
static void menu_open_chat(void)     { segui_palm_switch(PALM_APP_CHAT); }
static void menu_open_phone(void)    { segui_palm_switch(PALM_APP_PHONE); }
static void menu_open_contacts(void) { segui_palm_switch(PALM_APP_CONTACTS); }
static void menu_phone_clr(void)     { chat_core_set_dial(""); segui_palm_switch(PALM_APP_PHONE); }
static void menu_chat_ack(void)      { chat_core_send_msg("ACK from PicoCalc RP2350."); segui_palm_switch(PALM_APP_CHAT); }
static void menu_prefs_theme(void)   { prefs_activate(&s_app, NULL, 2); }

static const SEGUI_PALM_MENU_ITEM s_menu_default[] = {
    { "Home Launcher (F1)", menu_go_home },
    { "About B-System",     menu_about   }
};
static const SEGUI_PALM_MENU_ITEM s_menu_term[] = {
    { "Run ls (/SYS)",      menu_term_ls    },
    { "Run diag (BOOT)",    menu_term_diag  },
    { "Version Info",       term_menu_ver   },
    { "Clear Console",      term_menu_clear },
    { "Home Launcher (F1)", menu_go_home    }
};
static const SEGUI_PALM_MENU_ITEM s_menu_edit[] = {
    { "Cycle TIP (EN/日/བོད)", edit_cycle_tip   },
    { "Save File",             edit_menu_save   },
    { "Open NORTIA.TAD",       menu_edit_nortia },
    { "Open HEART_SUTRA",      menu_edit_heart  },
    { "Home Launcher (F1)",    menu_go_home     }
};
static const SEGUI_PALM_MENU_ITEM s_menu_browser[] = {
    { "Read OSAL_BOOK",     menu_browse_osal   },
    { "Read HEART_SUTRA",   menu_browse_heart  },
    { "Read BOOT.LOG",      menu_browse_boot   },
    { "Show Index Tab",     menu_browse_index  },
    { "Home Launcher (F1)", menu_go_home       }
};
static const SEGUI_PALM_MENU_ITEM s_menu_cabinet[] = {
    { "Volume Info",        menu_cab_df  },
    { "Home Launcher (F1)", menu_go_home }
};
#if !defined(PICOCALC_APP_QUAKE)
static const SEGUI_PALM_MENU_ITEM s_menu_gears[] = {
    { "Pause / Resume",     gears_menu_pause },
    { "Reset Camera",       gears_menu_reset },
    { "Home Launcher (F1)", menu_go_home     }
};
#endif
static const SEGUI_PALM_MENU_ITEM s_menu_contacts[] = {
    { "Open Chat",          menu_open_chat  },
    { "Open Phone",         menu_open_phone },
    { "Home Launcher (F1)", menu_go_home    }
};
static const SEGUI_PALM_MENU_ITEM s_menu_phone[] = {
    { "Clear Number",       menu_phone_clr   },
    { "Contacts",           menu_open_contacts },
    { "Home Launcher (F1)", menu_go_home     }
};
static const SEGUI_PALM_MENU_ITEM s_menu_chat[] = {
    { "Send Quick ACK",     menu_chat_ack      },
    { "Contacts",           menu_open_contacts },
    { "Home Launcher (F1)", menu_go_home       }
};
static const SEGUI_PALM_MENU_ITEM s_menu_prefs[] = {
    { "Cycle Theme",        menu_prefs_theme },
    { "About B-System",     menu_about       },
    { "Home Launcher (F1)", menu_go_home     }
};

typedef struct {
    const char *name;
    const char *tag;
    void (*build)(SEGUI_APP *app, const SEGUI_PROFILE *prof);
    BOOL (*on_key)(int key);
    const SEGUI_PALM_MENU_ITEM *menu;
    int n_menu;
} PALM_DESC;

static const PALM_DESC s_apps[PALM_APP_COUNT] = {
    { "Launcher",    "Home",    build_launcher, NULL,         s_menu_default,  2 },
    { "Terminal",    "Term",    build_terminal, key_terminal, s_menu_term,     5 },
    { "Editor",      "Editor",  build_editor,   key_editor,   s_menu_edit,     5 },
    { "Browser",     "Browse",  build_browser,  key_browser,  s_menu_browser,  5 },
    { "Cabinet",     "Cabinet", build_cabinet,  NULL,         s_menu_cabinet,  2 },
#if defined(PICOCALC_APP_QUAKE)
    { "Quake",       "Start",   build_quake,    key_quake,    s_menu_default,  2 },
#else
    { "GLGears",     "GLGears", build_glgears,  key_glgears,  s_menu_gears,    3 },
#endif
    { "Contacts",    "Contact", build_contacts, NULL,         s_menu_contacts, 3 },
    { "Phone",       "Phone",   build_phone,    NULL,         s_menu_phone,    3 },
    { "Chat",        "Chat",    build_chat,     NULL,         s_menu_chat,     3 },
    { "Preferences", "Prefs",   build_prefs,    NULL,         s_menu_prefs,    3 }
#if defined(SEGUI_HAVE_XMB)
    ,{ "XMB",        "XMB",     build_xmb,      key_xmb,      s_menu_default,  2 }
#endif
#if defined(SEGUI_HOST_XMB) && !defined(PICO_REAL_HW)
    ,{ "Quake",      "Start",   build_quake_preview, NULL,    s_menu_default,  2 }
#endif
};

/* ── Public Runtime API ────────────────────────────────────────────────── */

void segui_palm_init(const SEGUI_PROFILE *prof, const SEGUI_PALM_HW_OPS *hw)
{
    s_prof = prof ? prof : &segui_profile_picocalc;
    if (hw) s_hw = *hw;
    s_theme = s_prof->theme;
    s_theme_idx = 0;
    s_menu_open = FALSE;
    s_alert_open = FALSE;
    segui_palm_switch(PALM_APP_LAUNCHER);
}

void segui_palm_switch(SEGUI_PALM_APP_ID id)
{
    if (id < 0 || id >= PALM_APP_COUNT) id = PALM_APP_LAUNCHER;
    s_menu_open = FALSE;
    s_alert_open = FALSE;
    s_active_id = id;
    s_apps[id].build(&s_app, s_prof);
    s_app.theme = s_theme;
    if (s_hw.get_battery_pct) s_battery_pct = s_hw.get_battery_pct();
    segui_set_status(&s_app, "12:00", s_apps[id].tag, s_battery_pct);
}

SEGUI_PALM_APP_ID segui_palm_active_id(void) { return s_active_id; }
const char *segui_palm_app_name(SEGUI_PALM_APP_ID id)
{
    return (id >= 0 && id < PALM_APP_COUNT) ? s_apps[id].name : "Unknown";
}
SEGUI_APP *segui_palm_app(void) { return &s_app; }
BOOL segui_palm_is_animated(void)
{
#if defined(PICOCALC_APP_QUAKE)
    if (s_active_id == PALM_APP_GLGEARS) return TRUE;
#else
    if (s_active_id == PALM_APP_GLGEARS && !s_gear_paused) return TRUE;
#endif
#if defined(SEGUI_HAVE_XMB)
    if (s_active_id == PALM_APP_XMB) return TRUE;
#endif
    return FALSE;
}

void segui_palm_alert(const char *title, const char *msg, void (*on_confirm)(void))
{
    p_strlcpy(s_alert_title, (int)sizeof(s_alert_title), title ? title : "Alert");
    p_strlcpy(s_alert_msg,   (int)sizeof(s_alert_msg),   msg   ? msg   : "");
    s_alert_confirm = on_confirm;
    s_alert_open = TRUE;
    s_menu_open = FALSE;
}

void segui_palm_layout(GDEV *dev)
{
    if (dev) segui_layout(&s_app, dev);
}

/* ── Palm Overlay Painting (Menu Dropdown & Modal Alert) ───────────────── */

static void paint_overlays(GDEV *dev)
{
    const SEGUI_THEME *th = segui_active_theme(&s_app);
    const PALM_DESC *d = &s_apps[s_active_id];

    if (s_menu_open && d->n_menu > 0) {
        int row_h = segui_row_h(&s_app);
        int mh = d->n_menu * row_h + 6;
        RECT mr = { (H)(s_app.lay.body.left + 4), s_app.lay.body.top,
                    (H)(s_app.lay.body.left + 196), (H)(s_app.lay.body.top + mh) };
        int i;
        fill_rec(dev, &mr, th->panel_bg);
        segui_draw_bevel(dev, &mr, th->frame_hi, th->frame_sh, 2);
        for (i = 0; i < d->n_menu; i++) {
            SEGUI_ROW r;
            RECT rr = { (H)(mr.left + 3), (H)(mr.top + 3 + i * row_h),
                        (H)(mr.right - 3), (H)(mr.top + 3 + (i + 1) * row_h) };
            r.type = SEGUI_ROW_PLAIN;
            p_strlcpy(r.caption, SEGUI_LABEL_MAX, d->menu[i].label);
            r.value[0] = '\0';
            r.state = 0;
            r.on_activate = NULL;
            segui_draw_row(dev, &rr, &r, row_h, i == s_menu_focus, th, s_app.prof->flags);
        }
    }

    if (s_alert_open) {
        int bw = s_app.lay.body.right - s_app.lay.body.left;
        int bh = s_app.lay.body.bottom - s_app.lay.body.top;
        RECT ar = { (H)(s_app.lay.body.left + (bw - 248) / 2),
                    (H)(s_app.lay.body.top  + (bh - 92)  / 2),
                    (H)(s_app.lay.body.left + (bw + 248) / 2),
                    (H)(s_app.lay.body.top  + (bh + 92)  / 2) };
        RECT hdr = { (H)(ar.left + 2), (H)(ar.top + 2), (H)(ar.right - 2), (H)(ar.top + 24) };
        RECT r_msg = { (H)(ar.left + 8), (H)(ar.top + 32), (H)(ar.right - 8), (H)(ar.top + 56) };
        RECT r_ftr = { (H)(ar.left + 8), (H)(ar.bottom - 24), (H)(ar.right - 8), (H)(ar.bottom - 4) };
        SEGUI_CTRL tmp;
        SEGUI_THEME hdr_th = *th;
        hdr_th.text = th->btn_sel_fg;
        hdr_th.panel_bg = th->accent;
        fill_rec(dev, &ar, th->panel_bg);
        segui_draw_bevel(dev, &ar, th->frame_hi, th->frame_sh, 2);
        fill_rec(dev, &hdr, th->accent);
        tmp.type = SEGUI_W_LABEL;
        p_strlcpy(tmp.label, SEGUI_LABEL_MAX, s_alert_title);
        segui_draw_label_text(dev, &hdr, &tmp, &hdr_th);
        p_strlcpy(tmp.label, SEGUI_LABEL_MAX, s_alert_msg);
        segui_draw_label_text(dev, &r_msg, &tmp, th);
        p_strlcpy(tmp.label, SEGUI_LABEL_MAX, "[Enter] OK   [Esc] Cancel");
        segui_draw_label_text(dev, &r_ftr, &tmp, th);
    }
}

void segui_palm_paint(GDEV *dev)
{
    if (!dev) return;
    segui_paint(&s_app, dev);
    paint_overlays(dev);
}

/* ── Event Dispatch (Home -> Alert -> Menu -> App -> SegUI) ────────────── */

BOOL segui_palm_handle_key(int key, GDEV *dev)
{
    const PALM_DESC *d = &s_apps[s_active_id];

    /* 1. F1 = Palm Home Launcher */
    if (key == PALM_KEY_HOME) {
        if (s_active_id != PALM_APP_LAUNCHER) {
            segui_palm_switch(PALM_APP_LAUNCHER);
            segui_palm_layout(dev);
            return TRUE;
        }
        return FALSE;
    }

    /* 2. Modal Alert captures Enter/Esc */
    if (s_alert_open) {
        if (key == PALM_KEY_ENTER) {
            void (*cb)(void) = s_alert_confirm;
            s_alert_open = FALSE;
            s_alert_confirm = NULL;
            if (cb) cb();
            segui_palm_layout(dev);
            return TRUE;
        }
        if (key == PALM_KEY_ESC) {
            s_alert_open = FALSE;
            s_alert_confirm = NULL;
            return TRUE;
        }
        return FALSE;
    }

    /* 3. F2 = Toggle Palm Pull-Down Menu */
    if (key == PALM_KEY_MENU) {
        s_menu_open = !s_menu_open;
        s_menu_focus = 0;
        return TRUE;
    }

    /* 4. Open Menu captures Up/Down/Enter/Esc */
    if (s_menu_open) {
        if (key == PALM_KEY_ESC) { s_menu_open = FALSE; return TRUE; }
        if (key == PALM_KEY_UP && s_menu_focus > 0) { s_menu_focus--; return TRUE; }
        if (key == PALM_KEY_DOWN && s_menu_focus + 1 < d->n_menu) { s_menu_focus++; return TRUE; }
        if (key == PALM_KEY_ENTER && s_menu_focus < d->n_menu) {
            void (*act)(void) = d->menu[s_menu_focus].action;
            s_menu_open = FALSE;
            if (act) act();
            segui_palm_layout(dev);
            return TRUE;
        }
        return FALSE;
    }

    /* 5. Launcher direct hotkeys 1..9. These stay the page-1 apps. */
    if (s_active_id == PALM_APP_LAUNCHER && key >= '1' && key <= '9') {
        segui_palm_switch((SEGUI_PALM_APP_ID)(PALM_APP_TERMINAL + (key - '1')));
        segui_palm_layout(dev);
        return TRUE;
    }

#if defined(SEGUI_HAVE_XMB)
    /* Left/Right on the launcher switches pages. Up/Down still walks the grid.
     * GLGears keeps its own yaw keys; this runs only while the launcher is up. */
    if (s_active_id == PALM_APP_LAUNCHER &&
        (key == PALM_KEY_LEFT || key == PALM_KEY_RIGHT)) {
        int page = (key == PALM_KEY_RIGHT) ? 1 : 0;
        if (page != s_launch_page) {
            s_launch_page = page;
            segui_palm_switch(PALM_APP_LAUNCHER);
            segui_palm_layout(dev);
        }
        return TRUE;
    }
#endif

    /* 6. App-specific key handler */
    if (d->on_key && d->on_key(key)) {
#if defined(SEGUI_HAVE_XMB)
        if (key == PALM_KEY_ENTER)
            segui_palm_layout(dev);
#endif
        return TRUE;
    }

    /* 7. Generic SegUI autolayout navigation */
    if (key == PALM_KEY_UP) {
        if (segui_list_of(&s_app)) segui_list_move(&s_app, -1);
        else segui_move_focus(&s_app, -1);
        return TRUE;
    }
    if (key == PALM_KEY_DOWN) {
        if (segui_list_of(&s_app)) segui_list_move(&s_app, +1);
        else segui_move_focus(&s_app, +1);
        return TRUE;
    }
    if (key == PALM_KEY_LEFT)  { segui_move_focus(&s_app, -1); return TRUE; }
    if (key == PALM_KEY_RIGHT) { segui_move_focus(&s_app, +1); return TRUE; }
    if (key == PALM_KEY_TAB && s_app.n_tab > 1) {
        segui_select_tab(&s_app, s_app.active_tab + 1);
        segui_palm_layout(dev);
        return TRUE;
    }
    if (key == PALM_KEY_ENTER) {
        SEGUI_CTRL *f = segui_focused(&s_app);
        if (f) segui_activate(&s_app, f);
        segui_palm_layout(dev);
        return TRUE;
    }
    if (key == PALM_KEY_ESC && s_active_id != PALM_APP_LAUNCHER) {
        segui_palm_switch(PALM_APP_LAUNCHER);
        segui_palm_layout(dev);
        return TRUE;
    }
    return FALSE;
}

BOOL segui_palm_tick(uint32_t now_ms)
{
#if defined(PICOCALC_APP_QUAKE)
    if (s_active_id == PALM_APP_GLGEARS) {
        start_sim_tick(now_ms);
        return TRUE;
    }
#else
    (void)now_ms;
    if (s_active_id == PALM_APP_GLGEARS && !s_gear_paused) {
        s_gear_angle = (s_gear_angle + 4) & 255;
        return TRUE;
    }
#endif
#if defined(SEGUI_HAVE_XMB)
    if (s_active_id == PALM_APP_XMB) {
        xmb_pico_tick();
        return TRUE;
    }
#endif
    return FALSE;
}
