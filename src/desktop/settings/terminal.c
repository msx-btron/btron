/*
 * B-System (BTRON 3.20) Settings Applet: terminal (Terminal / 端末・通信)
 * Conforming to ./b-system/settings/Terminal.html and B-right/V / Chokanji specs.
 */

#include <btron/settings.h>
#include <btron/terminal_settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

static TERMINAL_SETTINGS g_term_settings = {
    .theme            = TERM_THEME_WHITE,
    .fg_color         = 0xFFFFFFFF,
    .bg_color         = 0xCC000000,
    .font_size        = TERM_FONT_16,
    .scrollback_lines = 300,
    .cursor_style     = TERM_CURSOR_BLOCK,
    .transparency     = TERM_TRANSPARENCY_80
};

COLOR terminal_get_effective_bg(const TERMINAL_SETTINGS *st) {
    if (!st) return 0xCC000000;
    uint32_t alpha = 0xCC;
    if (st->transparency == TERM_TRANSPARENCY_OPAQUE) alpha = 0xFF;
    else if (st->transparency == TERM_TRANSPARENCY_60) alpha = 0x99;

    uint32_t base_rgb = 0x000000;
    if (st->theme == TERM_THEME_CYAN)  base_rgb = 0x0A1120;
    else if (st->theme == TERM_THEME_LIGHT) base_rgb = 0xE2E8F0;

    return (alpha << 24) | (base_rgb & 0x00FFFFFF);
}

void terminal_get_settings(TERMINAL_SETTINGS *out) {
    if (!out) return;
    *out = g_term_settings;
}

void terminal_set_settings(const TERMINAL_SETTINGS *in) {
    if (!in) return;
    g_term_settings = *in;
    g_term_settings.bg_color = terminal_get_effective_bg(&g_term_settings);
}

void terminal_reset_settings(void) {
    g_term_settings.theme = TERM_THEME_WHITE;
    g_term_settings.fg_color = 0xFFFFFFFF;
    g_term_settings.bg_color = 0xCC000000;
    g_term_settings.font_size = TERM_FONT_16;
    g_term_settings.scrollback_lines = 300;
    g_term_settings.cursor_style = TERM_CURSOR_BLOCK;
    g_term_settings.transparency = TERM_TRANSPARENCY_80;
}

typedef struct {
    WND *wnd;
    TERMINAL_SETTINGS local_cfg;
    int theme_sel;
    int font_size_sel;
    int scroll_sel;
    int cursor_sel;
    int trans_sel;
    BOOL is_dirty;
} AppletState_terminal;

static AppletState_terminal g_state_terminal;

static void sync_terminal_state(void) {
    g_state_terminal.theme_sel     = (int)g_state_terminal.local_cfg.theme;
    g_state_terminal.font_size_sel = (int)g_state_terminal.local_cfg.font_size;
    g_state_terminal.scroll_sel    = (int)g_state_terminal.local_cfg.scrollback_lines;
    g_state_terminal.cursor_sel    = (int)g_state_terminal.local_cfg.cursor_style;
    g_state_terminal.trans_sel     = (int)g_state_terminal.local_cfg.transparency;
}

static void on_term_theme_green(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_GREEN; g_state_terminal.local_cfg.fg_color = 0xFF22C55E; sync_terminal_state(); }
static void on_term_theme_amber(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_AMBER; g_state_terminal.local_cfg.fg_color = 0xFFF59E0B; sync_terminal_state(); }
static void on_term_theme_white(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_WHITE; g_state_terminal.local_cfg.fg_color = 0xFFFFFFFF; sync_terminal_state(); }
static void on_term_theme_cyan(WND *wnd, SETTINGS_CONTROL *ctl)  { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_CYAN;  g_state_terminal.local_cfg.fg_color = 0xFF38BDF8; sync_terminal_state(); }
static void on_term_theme_light(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_LIGHT; g_state_terminal.local_cfg.fg_color = 0xFF0F172A; sync_terminal_state(); }

static void on_term_font_12(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.font_size = TERM_FONT_12; sync_terminal_state(); }
static void on_term_font_16(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.font_size = TERM_FONT_16; sync_terminal_state(); }
static void on_term_font_20(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.font_size = TERM_FONT_20; sync_terminal_state(); }

static void on_term_scroll_100k(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 100000; sync_terminal_state(); }
static void on_term_scroll_300(WND *wnd, SETTINGS_CONTROL *ctl)  { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 300; sync_terminal_state(); }
static void on_term_scroll_1000(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 1000; sync_terminal_state(); }
static void on_term_scroll_4096(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 4096; sync_terminal_state(); }
static void on_term_scroll_10k(WND *wnd, SETTINGS_CONTROL *ctl)  { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 10000; sync_terminal_state(); }

static void on_term_cur_underline(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.cursor_style = TERM_CURSOR_UNDERLINE; sync_terminal_state(); }
static void on_term_cur_block(WND *wnd, SETTINGS_CONTROL *ctl)     { (void)wnd; (void)ctl; g_state_terminal.local_cfg.cursor_style = TERM_CURSOR_BLOCK; sync_terminal_state(); }
static void on_term_cur_bar(WND *wnd, SETTINGS_CONTROL *ctl)       { (void)wnd; (void)ctl; g_state_terminal.local_cfg.cursor_style = TERM_CURSOR_BAR; sync_terminal_state(); }

static void on_term_trans_opaque(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.transparency = TERM_TRANSPARENCY_OPAQUE; sync_terminal_state(); }
static void on_term_trans_80(WND *wnd, SETTINGS_CONTROL *ctl)     { (void)wnd; (void)ctl; g_state_terminal.local_cfg.transparency = TERM_TRANSPARENCY_80; sync_terminal_state(); }
static void on_term_trans_60(WND *wnd, SETTINGS_CONTROL *ctl)     { (void)wnd; (void)ctl; g_state_terminal.local_cfg.transparency = TERM_TRANSPARENCY_60; sync_terminal_state(); }

static void on_term_default(WND *wnd) {
    (void)wnd;
    terminal_reset_settings();
    terminal_get_settings(&g_state_terminal.local_cfg);
    sync_terminal_state();
    g_state_terminal.is_dirty = TRUE;
}

static void on_term_apply(WND *wnd) {
    (void)wnd;
    terminal_set_settings(&g_state_terminal.local_cfg);
    g_state_terminal.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_term_controls[] = {
    /* Section 1: Themes */
    { 0, SETTINGS_CTL_RADIO, 1, 0, {0}, "グリーン (Matrix Green)", NULL, &g_state_terminal.theme_sel, &g_state_terminal.is_dirty, on_term_theme_green },
    { 1, SETTINGS_CTL_RADIO, 1, 1, {0}, "アンバー (Amber CRT)", NULL, &g_state_terminal.theme_sel, &g_state_terminal.is_dirty, on_term_theme_amber },
    { 2, SETTINGS_CTL_RADIO, 1, 2, {0}, "白黒 (White on Black)", NULL, &g_state_terminal.theme_sel, &g_state_terminal.is_dirty, on_term_theme_white },
    { 3, SETTINGS_CTL_RADIO, 1, 3, {0}, "BTRON青 (Cyan on Navy)", NULL, &g_state_terminal.theme_sel, &g_state_terminal.is_dirty, on_term_theme_cyan },
    { 4, SETTINGS_CTL_RADIO, 1, 4, {0}, "ライト (Paper Light)", NULL, &g_state_terminal.theme_sel, &g_state_terminal.is_dirty, on_term_theme_light },

    /* Section 2: Font Size */
    { 5, SETTINGS_CTL_RADIO, 2, 12, {0}, "小 12px (32行×100桁)", NULL, &g_state_terminal.font_size_sel, &g_state_terminal.is_dirty, on_term_font_12 },
    { 6, SETTINGS_CTL_RADIO, 2, 16, {0}, "標準 16px (24行×80桁)", NULL, &g_state_terminal.font_size_sel, &g_state_terminal.is_dirty, on_term_font_16 },
    { 7, SETTINGS_CTL_RADIO, 2, 20, {0}, "大 20px (18行×64桁)", NULL, &g_state_terminal.font_size_sel, &g_state_terminal.is_dirty, on_term_font_20 },

    /* Section 3: Scrollback */
    { 8, SETTINGS_CTL_RADIO, 3, 100000, {0}, "履歴 100K行", NULL, &g_state_terminal.scroll_sel, &g_state_terminal.is_dirty, on_term_scroll_100k },
    { 9, SETTINGS_CTL_RADIO, 3, 300,    {0}, "履歴 300行 (標準)", NULL, &g_state_terminal.scroll_sel, &g_state_terminal.is_dirty, on_term_scroll_300 },
    { 10, SETTINGS_CTL_RADIO, 3, 1000,  {0}, "履歴 1000行 (大)", NULL, &g_state_terminal.scroll_sel, &g_state_terminal.is_dirty, on_term_scroll_1000 },
    { 11, SETTINGS_CTL_RADIO, 3, 4096,  {0}, "履歴 4096行", NULL, &g_state_terminal.scroll_sel, &g_state_terminal.is_dirty, on_term_scroll_4096 },
    { 12, SETTINGS_CTL_RADIO, 3, 10000, {0}, "履歴 10K行 (最大)", NULL, &g_state_terminal.scroll_sel, &g_state_terminal.is_dirty, on_term_scroll_10k },

    /* Section 4: Cursor */
    { 13, SETTINGS_CTL_RADIO, 4, 0, {0}, "カーソル下線 (_)", NULL, &g_state_terminal.cursor_sel, &g_state_terminal.is_dirty, on_term_cur_underline },
    { 14, SETTINGS_CTL_RADIO, 4, 1, {0}, "ブロックカーソル (█)", NULL, &g_state_terminal.cursor_sel, &g_state_terminal.is_dirty, on_term_cur_block },
    { 15, SETTINGS_CTL_RADIO, 4, 2, {0}, "バーカーソル (|)", NULL, &g_state_terminal.cursor_sel, &g_state_terminal.is_dirty, on_term_cur_bar },

    /* Section 5: Transparency */
    { 16, SETTINGS_CTL_RADIO, 5, 0, {0}, "不透明 (100% Opaque)", NULL, &g_state_terminal.trans_sel, &g_state_terminal.is_dirty, on_term_trans_opaque },
    { 17, SETTINGS_CTL_RADIO, 5, 1, {0}, "20%減光 (80% Dimmed)", NULL, &g_state_terminal.trans_sel, &g_state_terminal.is_dirty, on_term_trans_80 },
    { 18, SETTINGS_CTL_RADIO, 5, 2, {0}, "40%減光 (60% Dimmed)", NULL, &g_state_terminal.trans_sel, &g_state_terminal.is_dirty, on_term_trans_60 },
};

static SETTINGS_SECTION s_term_sections[] = {
    { " [1] 配色テーマ (Colour Theme) ", 2, 0, 5, 64, 80, {0}, 10, 20, 0 },
    { " [2] 文字・行間 (Font Size) ", 3, 5, 3, 154, 42, {0}, 10, 20, 0 },
    { " [3] スクロール履歴 (Scrollback Buffer) ", 5, 8, 5, 206, 42, {0}, 10, 20, 0 },
    { " [4] カーソル形状 (Cursor Style) ", 3, 13, 3, 258, 42, {0}, 10, 20, 0 },
    { " [5] 背景透過・減光 (Background Transparency) ", 3, 16, 3, 310, 42, {0}, 10, 20, 0 },
};

static void paint_terminal_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    sync_terminal_state();
    if (dev->height < 390) {
        s_term_sections[0].start_y = 44;
        s_term_sections[0].height  = 64;
        s_term_sections[1].start_y = 112;
        s_term_sections[1].height  = 36;
        s_term_sections[2].start_y = 152;
        s_term_sections[2].height  = 36;
        s_term_sections[3].start_y = 192;
        s_term_sections[3].height  = 36;
        s_term_sections[4].start_y = 232;
        s_term_sections[4].height  = 36;
    } else {
        s_term_sections[0].start_y = 64;
        s_term_sections[0].height  = 80;
        s_term_sections[1].start_y = 154;
        s_term_sections[1].height  = 42;
        s_term_sections[2].start_y = 206;
        s_term_sections[2].height  = 42;
        s_term_sections[3].start_y = 258;
        s_term_sections[3].height  = 42;
        s_term_sections[4].start_y = 310;
        s_term_sections[4].height  = 42;
    }
    settings_draw_dialog(dev, "terminal", "Terminal", "端末・通信", "VT100 Emulator & Colours",
                         s_term_sections, 5,
                         s_term_controls, 19,
                         g_state_terminal.is_dirty);
}

static void handle_terminal_settings_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd,
                                   s_term_sections, 5,
                                   s_term_controls, 19,
                                   evt, on_term_default, on_term_apply);
}

WND* open_terminal_settings_window(void) {
    terminal_get_settings(&g_state_terminal.local_cfg);
    g_state_terminal.is_dirty = FALSE;

    GDEV *scr = wnd_mgr_get_screen();
    H scr_w = scr ? scr->width : 800;
    H scr_h = scr ? scr->height : 600;
#if defined(BTRON_PS2_TARGET)
    H win_w = 480;
    H win_h = 360;
#else
    H win_w = (scr_w <= 800) ? 500 : 540;
    H win_h = (scr_h <= 600) ? 360 : 450;
#endif
    H win_x = (scr_w - win_w) / 2;
    H win_y = (scr_h - win_h) / 2;
    if (win_x < 10) win_x = 10;
    if (win_y < 30) win_y = 30;

    WND *wnd = opn_wnd("端末・通信環境設定 (Terminal Settings)",
                       win_x, win_y, win_w, win_h,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd || !wnd->dev) {
        if (wnd) cls_wnd(wnd);
        return NULL;
    }

    g_state_terminal.wnd = wnd;
    wnd->paint = paint_terminal_settings;
    wnd->event_handler = handle_terminal_settings_event;
    inval_wnd(wnd);
    return wnd;
}
