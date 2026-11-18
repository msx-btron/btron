/*
 * B-System (BTRON 3.20) Clarity Publishing System (src/apps/clarity.c)
 * Professional DTP publishing environment:
 *  - Interactive frame creation, selection, move, and 8-handle resizing
 *  - Dynamic mouse pointer changes (SIZENWSE, SIZENESW, SIZENS, SIZEWE, IBEAM, MOVE)
 *  - Horizontal & Vertical 3D Scrollbars with draggable elevator thumbs & steppers
 *  - View menu with Zoom In (+), Zoom Out (-), 100% Actual, Fit to Window, presets
 *  - Calibrated paper sizing & multi-page support with Page Break Separators
 *  - Real-time text typing with insertion caret, backspace, enter, and arrows
 *  - Format selection (A4, Shiroku, Pecha) and VOBJ TAD Real Body export
 */

#include "clarity_doc.h"
#include "clarity_book.h"
#include <btron/dnd.h>
#include <btron/wnd.h>

extern WND* open_tad_browser_window(const char *path, const char *title);
extern WND* open_t_editor_window(void);
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/app_menu.h>
#include <btron/tip.h>
#include <btron/event.h>
#include <btron/troncode.h>

#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <sys/time.h>
#include <sys/stat.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define HAVE_CLARITY_SDL 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define HAVE_CLARITY_SDL 1
#endif
#endif
#endif

/* ------------------------------------------------------------------ */
/* Layout and Render module forward declarations                       */
/* ------------------------------------------------------------------ */

extern void clarity_fmt_dimensions(ClarityDoc *doc);
extern int  clarity_mm_to_px(int mm, int zoom_pct);
extern void clarity_draw_page(GDEV *dev, const ClarityDoc *doc, int ox, int oy);
extern void clarity_draw_frames(GDEV *dev, const ClarityDoc *doc, int ox, int oy);
extern int  clarity_hittest_frame(const ClarityDoc *doc, H x, H y, int ox, int oy);
extern int  clarity_hittest_handle(const ClarityFrame *f, H x, H y, int ox, int oy, int zoom_pct);
extern void clarity_resize_frame_handle(ClarityFrame *f, int h, H mx, H my, int ox, int oy, int zoom_pct);
extern void clarity_move_frame(ClarityFrame *f, H dx, H dy);

#define CLARITY_SAMPLE_PATH "/SYS/Clarity-Sample.TAD"

extern void clarity_render_key(ClarityDoc *doc, int fidx, UH tc);
extern void clarity_handle_text_action(ClarityDoc *doc, int fidx, int action, UH tc);
extern void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom, BOOL is_selected);
extern void clarity_render_image(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom);
extern void clarity_render_tad(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom);
extern void clarity_init_sample_page(ClarityDoc *doc);
extern WND* open_t_editor_window_with_file(const char *filepath);
extern ER   clarity_export_save(const ClarityDoc *doc, const char *name);
extern ER   clarity_export_load(ClarityDoc *doc, ID robj_id);
extern ER   clarity_export_save_file(const ClarityDoc *doc, const char *filepath);
extern ER   clarity_export_load_file(ClarityDoc *doc, const char *filepath);
void        clarity_set_pecha_mode(ClarityDoc *doc, int size);

/* Text action enum matching clarity_render.c */
enum {
    ACT_CHAR = 0,
    ACT_BACKSPACE,
    ACT_DELETE,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_HOME,
    ACT_END,
    ACT_ENTER,
    ACT_UP,
    ACT_DOWN,
    ACT_PAGEUP,
    ACT_PAGEDOWN
};

/* ------------------------------------------------------------------ */
/* UI Constants & Commands                                             */
/* ------------------------------------------------------------------ */

#define SCROLLBAR_SIZE           16
#define CLARITY_STEP_SCROLL      32

enum {
    /* File */
    CMD_FILE_NEW = 100,
    CMD_FILE_OPEN,
    CMD_FILE_SAVE,
    CMD_BOOK_OPEN,
    CMD_BOOK_EXPORT_TAD,
    CMD_BOOK_PREVIEW_TAD,
    CMD_BOOK_TOC,
    CMD_BOOK_NEXT_CHAP,
    CMD_BOOK_PREV_CHAP,

    /* View / Zoom */
    CMD_VIEW_ZOOM_IN = 200,
    CMD_VIEW_ZOOM_OUT,
    CMD_VIEW_ACTUAL,
    CMD_VIEW_FIT,
    CMD_VIEW_ZOOM_50,
    CMD_VIEW_ZOOM_75,
    CMD_VIEW_ZOOM_100,
    CMD_VIEW_ZOOM_125,
    CMD_VIEW_ZOOM_150,

    /* Format */
    CMD_FMT_A4 = 300,
    CMD_FMT_SHIROKU,
    CMD_FMT_PECHA,
    CMD_FMT_PECHA_KANGYUR,
    CMD_FMT_PECHA_RINCHEN,
    CMD_FMT_PECHA_DERGE,
    CMD_FMT_PECHA_POCKET,
    CMD_FMT_PECHA_DERGE_WIDE,
    CMD_PAGE_ADD,
    CMD_PAGE_REMOVE,

    /* Insert */
    CMD_INS_TEXT = 400,
    CMD_INS_IMAGE,
    CMD_INS_FLIP_FLOW,

    /* Arrange / Z-Order */
    CMD_ARRANGE_FRONT = 500,
    CMD_ARRANGE_BACK,
    CMD_ARRANGE_FORWARD,
    CMD_ARRANGE_BACKWARD,
    CMD_ARRANGE_DUPLICATE
};

typedef enum {
    CLARITY_DLG_NONE = 0,
    CLARITY_DLG_OPEN_BOOK = 1
} ClarityDialogType;

typedef struct {
    ClarityDialogType type;
    char path[256];
    int  path_len;
    int  pecha_size;      /* 0: Kangyur, 1: Rinchen, 2: Derge, 3: Pocket, 4: Derge Wide */
    BOOL auto_tibetan;
    int  focus_field;     /* 0: path, 1: pecha radios, 2: check, 3: OK, 4: Cancel */
    RECT box;
    RECT path_box;
    RECT radio_boxes[5];
    RECT check_box;
    RECT btn_ok;
    RECT btn_cancel;
} ClarityDialogState;

/* ------------------------------------------------------------------ */
/* Application state                                                   */
/* ------------------------------------------------------------------ */

static ClarityDoc         g_doc;
static ClarityDialogState g_dlg;
static WND               *g_wnd        = NULL;
static WND               *g_toc_wnd    = NULL;   /* TOC sidebar window */
static int                g_toc_scroll = 0;       /* TOC window scroll */
static APP_MENU_BAR       g_menu;

/* Scroll state (offsets in canvas space) */
static int  g_scroll_x   = 0;
static int  g_scroll_y   = 0;

/* Scrollbar dragging */
static BOOL g_sb_drag_v  = FALSE;
static BOOL g_sb_drag_h  = FALSE;
static int  g_sb_start_y = 0;
static int  g_sb_start_x = 0;
static int  g_sb_orig_y  = 0;
static int  g_sb_orig_x  = 0;

/* Frame drag move state */
static BOOL g_drag_move   = FALSE;
static H    g_drag_prev_x = 0;
static H    g_drag_prev_y = 0;

/* Current active mouse cursor type */
static ClarityCursorType g_curr_cursor = CLARITY_CURSOR_ARROW;

/* Forward declarations */
static void clarity_paint(WND *wnd, GDEV *dev);
static void clarity_event(WND *wnd, const EVT *evt);
static void build_menu(void);
static void doc_new(ClarityPageFmt fmt);
static void clarity_set_cursor(ClarityCursorType type);
static void clarity_fit_window(void);
static void open_clarity_toc_window(void);

/* ------------------------------------------------------------------ */
/* Dynamic Mouse Cursor System                                         */
/* ------------------------------------------------------------------ */

static void clarity_set_cursor(ClarityCursorType type)
{
    if (g_curr_cursor == type) return;
    g_curr_cursor = type;

#ifdef HAVE_CLARITY_SDL
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) SDL_Cursor* SDL_CreateSystemCursor(SDL_SystemCursor id);
__attribute__((weak)) void        SDL_SetCursor(SDL_Cursor *cursor);
#endif

    if (!SDL_CreateSystemCursor || !SDL_SetCursor) return;

    static SDL_Cursor *s_cursors[8] = { NULL };
    static BOOL s_inited = FALSE;
    if (!s_inited) {
        s_cursors[CLARITY_CURSOR_ARROW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        s_cursors[CLARITY_CURSOR_IBEAM] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
        s_cursors[CLARITY_CURSOR_MOVE]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
        s_cursors[CLARITY_CURSOR_NWSE]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
        s_cursors[CLARITY_CURSOR_NESW]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
        s_cursors[CLARITY_CURSOR_NS]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
        s_cursors[CLARITY_CURSOR_WE]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
        s_cursors[CLARITY_CURSOR_HAND]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        s_inited = TRUE;
    }
    if (type >= 0 && type < 8 && s_cursors[type]) {
        SDL_SetCursor(s_cursors[type]);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Document helpers                                                    */
/* ------------------------------------------------------------------ */

static void doc_new(ClarityPageFmt fmt)
{
    for (int i = 0; i < g_doc.frame_count; i++) {
        if (g_doc.frames[i].bitmap) {
            free(g_doc.frames[i].bitmap);
            g_doc.frames[i].bitmap = NULL;
        }
    }
    memset(&g_doc, 0, sizeof(ClarityDoc));
    g_doc.fmt            = fmt;
    g_doc.page_count     = 2;   /* 2 pages by default */
    g_doc.zoom_pct       = 75;  /* 75% default fit */
    g_doc.selected_frame = -1;
    g_doc.tool           = TOOL_SELECT;
    clarity_fmt_dimensions(&g_doc);

    g_scroll_x = 0;
    g_scroll_y = 0;
}

static ClarityFrame *doc_add_frame(ClarityFrameType type, H x, H y, H w, H h)
{
    return clarity_doc_add_frame(&g_doc, type, x, y, w, h);
}

/* ------------------------------------------------------------------ */
/* Scrollbar Rendering (Canonical BTRON 3D Steppers & Elevators)       */
/* ------------------------------------------------------------------ */

static void paint_scrollbar_v(GDEV *dev, int x, int y, int w, int h,
                              int scroll, int max_scroll, int view_h)
{
    RECT bg = { (H)x, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &bg, COLOR_LTGRAY);
    drw_lin(dev, (H)x, (H)y, (H)x, (H)(y + h));

    /* Up Button */
    RECT up_btn = { (H)x, (H)y, (H)(x + w), (H)(y + 16) };
    fill_rec(dev, &up_btn, COLOR_LTGRAY);
    drw_rec(dev, &up_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(x + 8), (H)(y + 4), (H)(x + 4),  (H)(y + 11));
    drw_lin(dev, (H)(x + 8), (H)(y + 4), (H)(x + 12), (H)(y + 11));
    drw_lin(dev, (H)(x + 4), (H)(y + 11), (H)(x + 12), (H)(y + 11));

    /* Down Button */
    int dy_b = y + h - 16;
    RECT dn_btn = { (H)x, (H)dy_b, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &dn_btn, COLOR_LTGRAY);
    drw_rec(dev, &dn_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(x + 4),  (H)(dy_b + 5), (H)(x + 12), (H)(dy_b + 5));
    drw_lin(dev, (H)(x + 4),  (H)(dy_b + 5), (H)(x + 8),  (H)(dy_b + 12));
    drw_lin(dev, (H)(x + 12), (H)(dy_b + 5), (H)(x + 8),  (H)(dy_b + 12));

    /* Elevator Thumb */
    int track_y = y + 16;
    int track_h = h - 32;
    if (track_h > 20) {
        int thumb_h = (max_scroll > 0) ? (track_h * view_h) / (view_h + max_scroll) : track_h;
        if (thumb_h < 16) thumb_h = 16;
        if (thumb_h > track_h) thumb_h = track_h;

        int thumb_y = track_y;
        if (max_scroll > 0) {
            thumb_y = track_y + (scroll * (track_h - thumb_h)) / max_scroll;
        }

        RECT thumb = { (H)(x + 1), (H)thumb_y, (H)(x + w - 1), (H)(thumb_y + thumb_h) };
        fill_rec(dev, &thumb, COLOR_GRAY);
        drw_rec(dev, &thumb);
        set_col(dev, COLOR_WHITE, COLOR_GRAY);
        drw_lin(dev, (H)(x + 2), (H)(thumb_y + 1), (H)(x + w - 3), (H)(thumb_y + 1));
        drw_lin(dev, (H)(x + 2), (H)(thumb_y + 1), (H)(x + 2), (H)(thumb_y + thumb_h - 2));
    }
}

static void paint_scrollbar_h(GDEV *dev, int x, int y, int w, int h,
                              int scroll, int max_scroll, int view_w)
{
    RECT bg = { (H)x, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &bg, COLOR_LTGRAY);
    drw_lin(dev, (H)x, (H)y, (H)(x + w), (H)y);

    /* Left Button */
    RECT lt_btn = { (H)x, (H)y, (H)(x + 16), (H)(y + h) };
    fill_rec(dev, &lt_btn, COLOR_LTGRAY);
    drw_rec(dev, &lt_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(x + 4), (H)(y + 8), (H)(x + 11), (H)(y + 4));
    drw_lin(dev, (H)(x + 4), (H)(y + 8), (H)(x + 11), (H)(y + 12));
    drw_lin(dev, (H)(x + 11), (H)(y + 4), (H)(x + 11), (H)(y + 12));

    /* Right Button */
    int rx_b = x + w - 16;
    RECT rt_btn = { (H)rx_b, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &rt_btn, COLOR_LTGRAY);
    drw_rec(dev, &rt_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 4),  (H)(rx_b + 12), (H)(y + 8));
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 12), (H)(rx_b + 12), (H)(y + 8));
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 4),  (H)(rx_b + 5),  (H)(y + 12));

    /* Elevator Thumb */
    int track_x = x + 16;
    int track_w = w - 32;
    if (track_w > 20) {
        int thumb_w = (max_scroll > 0) ? (track_w * view_w) / (view_w + max_scroll) : track_w;
        if (thumb_w < 16) thumb_w = 16;
        if (thumb_w > track_w) thumb_w = track_w;

        int thumb_x = track_x;
        if (max_scroll > 0) {
            thumb_x = track_x + (scroll * (track_w - thumb_w)) / max_scroll;
        }

        RECT thumb = { (H)thumb_x, (H)(y + 1), (H)(thumb_x + thumb_w), (H)(y + h - 1) };
        fill_rec(dev, &thumb, COLOR_GRAY);
        drw_rec(dev, &thumb);
        set_col(dev, COLOR_WHITE, COLOR_GRAY);
        drw_lin(dev, (H)(thumb_x + 1), (H)(y + 2), (H)(thumb_x + thumb_w - 2), (H)(y + 2));
        drw_lin(dev, (H)(thumb_x + 1), (H)(y + 2), (H)(thumb_x + 1), (H)(y + h - 3));
    }
}

/* ------------------------------------------------------------------ */
/* Viewport and Fit Window Calculation                                 */
/* ------------------------------------------------------------------ */

static void get_viewport_and_content_bounds(int *out_vw, int *out_vh,
                                            int *out_cw, int *out_ch,
                                            int *out_max_x, int *out_max_y)
{
    int win_w = g_wnd ? (g_wnd->client.right - g_wnd->client.left) : 900;
    int win_h = g_wnd ? (g_wnd->client.bottom - g_wnd->client.top) : 600;

    int vw = win_w - SCROLLBAR_SIZE;
    int vh = win_h - APP_MENU_BAR_HEIGHT - SCROLLBAR_SIZE;
    if (vw < 100) vw = 100;
    if (vh < 100) vh = 100;

    int zoom = g_doc.zoom_pct > 0 ? g_doc.zoom_pct : 100;
    int unzoomed_pw = (g_doc.page_w_mm * CLARITY_DPI * 10) / 254;
    int unzoomed_ph = (g_doc.page_h_mm * CLARITY_DPI * 10) / 254;
    int unzoomed_gap = CLARITY_PAGE_GAP_PX;

    int pw = (unzoomed_pw * zoom) / 100;
    int pages = g_doc.page_count > 0 ? g_doc.page_count : 1;

    int cw = pw + CLARITY_CANVAS_MARGIN_PX * 2;
    int total_unzoomed_h = unzoomed_ph * pages + unzoomed_gap * (pages - 1);
    int ch = (total_unzoomed_h * zoom) / 100 + CLARITY_CANVAS_MARGIN_PX * 2;

    int max_x = (cw > vw) ? (cw - vw) : 0;
    int max_y = (ch > vh) ? (ch - vh) : 0;

    if (out_vw) *out_vw = vw;
    if (out_vh) *out_vh = vh;
    if (out_cw) *out_cw = cw;
    if (out_ch) *out_ch = ch;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
}

static void clarity_fit_window(void)
{
    int win_w = g_wnd ? (g_wnd->client.right - g_wnd->client.left) : 900;
    int win_h = g_wnd ? (g_wnd->client.bottom - g_wnd->client.top) : 600;

    int avail_w = win_w - SCROLLBAR_SIZE - CLARITY_CANVAS_MARGIN_PX * 2;
    int avail_h = win_h - APP_MENU_BAR_HEIGHT - SCROLLBAR_SIZE - CLARITY_CANVAS_MARGIN_PX * 2;
    if (avail_w < 100) avail_w = 100;
    if (avail_h < 100) avail_h = 100;

    /* Base unzoomed sizes */
    int base_pw = clarity_mm_to_px(g_doc.page_w_mm, 100);
    int base_ph = clarity_mm_to_px(g_doc.page_h_mm, 100);
    if (base_pw <= 0) base_pw = 1;
    if (base_ph <= 0) base_ph = 1;

    int fit_w_pct = (avail_w * 100) / base_pw;
    int fit_h_pct = (avail_h * 100) / base_ph;
    int fit_pct = (fit_w_pct < fit_h_pct) ? fit_w_pct : fit_h_pct;

    if (g_doc.fmt == FMT_PECHA) {
        /* In Tibetan Pecha mode, folios have a calibrated number of visible rows (e.g. 12-13 rows).
         * Since bitmap glyphs are fixed 16px (19px line height) and do not scale down below 16px,
         * the desk zoom must ensure the frame height on screen is tall enough to display all
         * required rows without clipping. */
        int rows = 13;
        if (g_doc.pecha_size == PECHA_SIZE_KANGYUR) rows = 8;
        else if (g_doc.pecha_size == PECHA_SIZE_RINCHEN) rows = 10;
        else if (g_doc.pecha_size == PECHA_SIZE_DERGE) rows = 13;
        else if (g_doc.pecha_size == PECHA_SIZE_POCKET) rows = 6;
        else if (g_doc.pecha_size == PECHA_SIZE_DERGE_WIDE) rows = 25;

        int req_frame_h = rows * 19 + 8;
        int unz_frame_h = base_ph - 40;
        if (unz_frame_h > 0) {
            int min_pecha_zoom = (req_frame_h * 100 + unz_frame_h - 1) / unz_frame_h;
            if (fit_pct < min_pecha_zoom) {
                fit_pct = min_pecha_zoom;
            }
        }
    }

    if (fit_pct < 25) fit_pct = 25;
    if (fit_pct > 150) fit_pct = 150;

    g_doc.zoom_pct = fit_pct;
    g_scroll_x = 0;
    g_scroll_y = 0;
}

/* ------------------------------------------------------------------ */
/* Modal Dialog Rendering (Early Apple / BeOS Class Minimalism)       */
/* ------------------------------------------------------------------ */

static void paint_clarity_dialog(GDEV *dev, int win_w, int win_h)
{
    if (g_dlg.type == CLARITY_DLG_NONE) return;

    int dlg_w = 560, dlg_h = 330;
    int dlg_x = (win_w - dlg_w) / 2;
    int dlg_y = (win_h - dlg_h) / 2;
    if (dlg_y < APP_MENU_BAR_HEIGHT + 10) dlg_y = APP_MENU_BAR_HEIGHT + 10;

    g_dlg.box.left = (H)dlg_x;
    g_dlg.box.top = (H)dlg_y;
    g_dlg.box.right = (H)(dlg_x + dlg_w);
    g_dlg.box.bottom = (H)(dlg_y + dlg_h);

    /* 1. Modal Dialog Frame */
    fill_rec(dev, &g_dlg.box, COLOR_LTGRAY);
    set_col(dev, COLOR_WHITE, COLOR_LTGRAY);
    drw_lin(dev, (H)dlg_x, (H)dlg_y, (H)(dlg_x + dlg_w), (H)dlg_y);
    drw_lin(dev, (H)dlg_x, (H)dlg_y, (H)dlg_x, (H)(dlg_y + dlg_h));
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)dlg_x, (H)(dlg_y + dlg_h), (H)(dlg_x + dlg_w), (H)(dlg_y + dlg_h));
    drw_lin(dev, (H)(dlg_x + dlg_w), (H)dlg_y, (H)(dlg_x + dlg_w), (H)(dlg_y + dlg_h));
    set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
    RECT border_in = { (H)(dlg_x + 1), (H)(dlg_y + 1), (H)(dlg_x + dlg_w - 1), (H)(dlg_y + dlg_h - 1) };
    drw_rec(dev, &border_in);

    /* 2. Navy Title Bar */
    RECT title_r = { (H)(dlg_x + 2), (H)(dlg_y + 2), (H)(dlg_x + dlg_w - 2), (H)(dlg_y + 24) };
    fill_rec(dev, &title_r, COLOR_NAVY);
    drw_tc_string(dev, dlg_x + 8, dlg_y + 4, "書籍を開く (Open Tibetan Book / Osal)", COLOR_WHITE, COLOR_NAVY);

    /* 3. Field 1: Book Directory Path */
    drw_tc_string(dev, dlg_x + 20, dlg_y + 36, "書籍ディレクトリ (Book Source Directory):", COLOR_BLACK, COLOR_LTGRAY);
    g_dlg.path_box.left = (H)(dlg_x + 20);
    g_dlg.path_box.top = (H)(dlg_y + 54);
    g_dlg.path_box.right = (H)(dlg_x + dlg_w - 20);
    g_dlg.path_box.bottom = (H)(dlg_y + 78);
    fill_rec(dev, &g_dlg.path_box, COLOR_WHITE);
    set_col(dev, (g_dlg.focus_field == 0) ? COLOR_NAVY : COLOR_DKGRAY, COLOR_WHITE);
    drw_rec(dev, &g_dlg.path_box);
    drw_tc_string(dev, dlg_x + 26, dlg_y + 58, g_dlg.path[0] ? g_dlg.path : "./assets/osal", COLOR_BLACK, COLOR_WHITE);
    if (g_dlg.focus_field == 0) {
        int cx = dlg_x + 26 + (int)strlen(g_dlg.path) * 8;
        drw_lin(dev, (H)cx, (H)(dlg_y + 56), (H)cx, (H)(dlg_y + 74));
    }

    /* 4. Field 2: Pecha Format Radios */
    drw_tc_string(dev, dlg_x + 20, dlg_y + 88, "Pecha 判型寸法 (Tibetan Pecha Format & Proportions):", COLOR_BLACK, COLOR_LTGRAY);
    const char *pecha_opts[] = {
        "Kangyur Pecha (560 × 110 mm · 8行)",
        "Rinchen Terdzö (650 × 140 mm · 10行)",
        "Derge Woodblock (700 × 180 mm · 13行)",
        "Pocket Dharma (320 × 85 mm · 6行)",
        "Derge Wide (720 × 200 mm · 25行)"
    };
    int r_xs[] = { dlg_x + 24, dlg_x + 290, dlg_x + 24, dlg_x + 290, dlg_x + 24 };
    int r_ys[] = { dlg_y + 110, dlg_y + 110, dlg_y + 132, dlg_y + 132, dlg_y + 154 };

    for (int i = 0; i < 5; i++) {
        g_dlg.radio_boxes[i].left = (H)r_xs[i];
        g_dlg.radio_boxes[i].top = (H)r_ys[i];
        g_dlg.radio_boxes[i].right = (H)(r_xs[i] + 250);
        g_dlg.radio_boxes[i].bottom = (H)(r_ys[i] + 18);

        char opt_buf[64];
        snprintf(opt_buf, sizeof(opt_buf), "%s %s", (g_dlg.pecha_size == i) ? "(•)" : "( )", pecha_opts[i]);
        COLOR tcol = (g_dlg.pecha_size == i) ? COLOR_NAVY : COLOR_BLACK;
        drw_tc_string(dev, r_xs[i], r_ys[i], opt_buf, tcol, COLOR_LTGRAY);
    }

    /* 5. Field 3: Auto-Transliteration Checkbox */
    g_dlg.check_box.left = (H)(dlg_x + 20);
    g_dlg.check_box.top = (H)(dlg_y + 180);
    g_dlg.check_box.right = (H)(dlg_x + dlg_w - 20);
    g_dlg.check_box.bottom = (H)(dlg_y + 204);

    char chk_buf[128];
    snprintf(chk_buf, sizeof(chk_buf), "[%s] Wylie 自動チベット文字変換 (Convert Wylie to Tibetan script)",
             g_dlg.auto_tibetan ? "✓" : " ");
    drw_tc_string(dev, dlg_x + 20, dlg_y + 184, chk_buf, COLOR_NAVY, COLOR_LTGRAY);

    /* 6. Info line */
    drw_tc_string(dev, dlg_x + 20, dlg_y + 214,
                  "※ Osal形式（可視領域比例裁断・12-13行木版再現・TAD出力対応）", COLOR_DKGRAY, COLOR_LTGRAY);

    /* 7. Action Buttons */
    int btn_w = 120, btn_h = 28;
    g_dlg.btn_ok.left = (H)(dlg_x + dlg_w - btn_w * 2 - 30);
    g_dlg.btn_ok.top = (H)(dlg_y + dlg_h - btn_h - 14);
    g_dlg.btn_ok.right = (H)(g_dlg.btn_ok.left + btn_w);
    g_dlg.btn_ok.bottom = (H)(g_dlg.btn_ok.top + btn_h);

    g_dlg.btn_cancel.left = (H)(dlg_x + dlg_w - btn_w - 16);
    g_dlg.btn_cancel.top = (H)(dlg_y + dlg_h - btn_h - 14);
    g_dlg.btn_cancel.right = (H)(g_dlg.btn_cancel.left + btn_w);
    g_dlg.btn_cancel.bottom = (H)(g_dlg.btn_cancel.top + btn_h);

    /* Draw OK button */
    fill_rec(dev, &g_dlg.btn_ok, (g_dlg.focus_field == 3) ? 0xFFCCE0FF : COLOR_LTGRAY);
    set_col(dev, COLOR_WHITE, COLOR_LTGRAY);
    drw_lin(dev, g_dlg.btn_ok.left, g_dlg.btn_ok.top, g_dlg.btn_ok.right, g_dlg.btn_ok.top);
    drw_lin(dev, g_dlg.btn_ok.left, g_dlg.btn_ok.top, g_dlg.btn_ok.left, g_dlg.btn_ok.bottom);
    set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
    drw_lin(dev, g_dlg.btn_ok.left, g_dlg.btn_ok.bottom, g_dlg.btn_ok.right, g_dlg.btn_ok.bottom);
    drw_lin(dev, g_dlg.btn_ok.right, g_dlg.btn_ok.top, g_dlg.btn_ok.right, g_dlg.btn_ok.bottom);
    if (g_dlg.focus_field == 3) {
        set_col(dev, COLOR_NAVY, COLOR_LTGRAY);
        drw_rec(dev, &g_dlg.btn_ok);
    }
    drw_tc_string(dev, g_dlg.btn_ok.left + 22, g_dlg.btn_ok.top + 6, "読込 (Open)", COLOR_BLACK, COLOR_LTGRAY);

    /* Draw Cancel button */
    fill_rec(dev, &g_dlg.btn_cancel, (g_dlg.focus_field == 4) ? 0xFFCCE0FF : COLOR_LTGRAY);
    set_col(dev, COLOR_WHITE, COLOR_LTGRAY);
    drw_lin(dev, g_dlg.btn_cancel.left, g_dlg.btn_cancel.top, g_dlg.btn_cancel.right, g_dlg.btn_cancel.top);
    drw_lin(dev, g_dlg.btn_cancel.left, g_dlg.btn_cancel.top, g_dlg.btn_cancel.left, g_dlg.btn_cancel.bottom);
    set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
    drw_lin(dev, g_dlg.btn_cancel.left, g_dlg.btn_cancel.bottom, g_dlg.btn_cancel.right, g_dlg.btn_cancel.bottom);
    drw_lin(dev, g_dlg.btn_cancel.right, g_dlg.btn_cancel.top, g_dlg.btn_cancel.right, g_dlg.btn_cancel.bottom);
    if (g_dlg.focus_field == 4) {
        set_col(dev, COLOR_NAVY, COLOR_LTGRAY);
        drw_rec(dev, &g_dlg.btn_cancel);
    }
    drw_tc_string(dev, g_dlg.btn_cancel.left + 22, g_dlg.btn_cancel.top + 6, "取消 (Cancel)", COLOR_BLACK, COLOR_LTGRAY);
}

/* ------------------------------------------------------------------ */
/* TOC Sidebar Window (Book Table of Contents)                         */
/* ------------------------------------------------------------------ */

#define TOC_ENTRY_H   18   /* pixels per TOC row                      */
#define TOC_HEADER_H  36   /* title header height                     */
#define TOC_INDENT_CL  8   /* indent per class header (px)            */
#define TOC_INDENT_CH 20   /* indent per chapter entry (px)           */

static void paint_toc_wnd(WND *wnd, GDEV *dev)
{
    if (!wnd || !dev) return;
    int ww = wnd->client.right  - wnd->client.left;
    int wh = wnd->client.bottom - wnd->client.top;

    /* Background */
    RECT bg = { 0, 0, (H)ww, (H)wh };
    fill_rec(dev, &bg, COLOR_WHITE);

    ClarityBook *bk = clarity_get_book();
    if (!bk || !bk->is_loaded) {
        drw_tc_string(dev, 8, 10, "目録 (TOC) — 書籍未読込", COLOR_DKGRAY, COLOR_WHITE);
        return;
    }

    /* Title bar */
    RECT hdr = { 0, 0, (H)ww, (H)TOC_HEADER_H };
    fill_rec(dev, &hdr, COLOR_NAVY);
    char hdr_buf[64];
    snprintf(hdr_buf, sizeof(hdr_buf), "目録 (%d 章)", bk->chapter_count);
    drw_tc_string(dev, 6, 4,  hdr_buf,          COLOR_WHITE, COLOR_NAVY);
    drw_tc_string(dev, 6, 20, bk->tibetan_title, COLOR_LTGRAY, COLOR_NAVY);

    /* Chapter rows */
    int y = TOC_HEADER_H - g_toc_scroll;
    int active_idx = bk->active_chapter_idx;

    for (int ci = 0; ci < bk->class_count; ci++) {
        const ClarityBookClass *cls = &bk->classes[ci];

        /* Class header row */
        if (y + TOC_ENTRY_H >= 0 && y < wh) {
            RECT cr = { 0, (H)y, (H)ww, (H)(y + TOC_ENTRY_H) };
            fill_rec(dev, &cr, 0xFFD8E4F0);
            set_col(dev, COLOR_DKGRAY, 0xFFD8E4F0);
            drw_lin(dev, 0, (H)(y + TOC_ENTRY_H - 1), (H)ww, (H)(y + TOC_ENTRY_H - 1));
            char cl_buf[128];
            const char *cl_title = (cls->tibetan[0] != '\0') ? cls->tibetan : cls->wylie;
            snprintf(cl_buf, sizeof(cl_buf), "%s. %s", cls->roman, cl_title);
            drw_tc_string(dev, TOC_INDENT_CL, y + 3, cl_buf, COLOR_NAVY, 0xFFD8E4F0);
        }
        y += TOC_ENTRY_H;

        /* Chapter rows within class */
        for (int ci2 = 0; ci2 < cls->chapter_count; ci2++) {
            int chidx = cls->chapter_indices[ci2];
            if (chidx < 0 || chidx >= bk->chapter_count) continue;
            const ClarityBookChapter *ch = &bk->chapters[chidx];

            if (y + TOC_ENTRY_H >= 0 && y < wh) {
                BOOL is_active = (chidx == active_idx);
                COLOR bg_col = is_active ? 0xFFCCE0FF : COLOR_WHITE;
                COLOR fg_col = is_active ? COLOR_NAVY   : COLOR_BLACK;
                RECT rr = { 0, (H)y, (H)ww, (H)(y + TOC_ENTRY_H) };
                fill_rec(dev, &rr, bg_col);
                if (is_active) {
                    set_col(dev, COLOR_NAVY, bg_col);
                    drw_rec(dev, &rr);
                }
                set_col(dev, COLOR_DKGRAY, bg_col);
                drw_lin(dev, (H)TOC_INDENT_CH, (H)(y + TOC_ENTRY_H - 1),
                             (H)ww, (H)(y + TOC_ENTRY_H - 1));

                /* Chapter number badge */
                char num_buf[8];
                snprintf(num_buf, sizeof(num_buf), "%02d", chidx + 1);
                drw_tc_string(dev, TOC_INDENT_CH, y + 3, num_buf, COLOR_GRAY, bg_col);

                /* Tibetan title or wylie fallback */
                const char *lbl = ch->tibetan_title[0] ? ch->tibetan_title : ch->wylie_title;
                drw_tc_string(dev, TOC_INDENT_CH + 24, y + 3, lbl, fg_col, bg_col);
            }
            y += TOC_ENTRY_H;
        }
    }

    /* Scrollbar groove on right */
    int total_h = TOC_HEADER_H + (bk->chapter_count + bk->class_count) * TOC_ENTRY_H;
    if (total_h > wh) {
        int sb_x = ww - 10;
        RECT sb_bg = { (H)sb_x, 0, (H)ww, (H)wh };
        fill_rec(dev, &sb_bg, COLOR_LTGRAY);
        set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
        drw_rec(dev, &sb_bg);
        int max_sc = total_h - wh;
        int thumb_h = (wh * wh) / total_h;
        if (thumb_h < 12) thumb_h = 12;
        int thumb_y = (g_toc_scroll * (wh - thumb_h)) / max_sc;
        RECT thumb = { (H)(sb_x + 1), (H)thumb_y, (H)(ww - 1), (H)(thumb_y + thumb_h) };
        fill_rec(dev, &thumb, COLOR_GRAY);
    }
}

static void toc_event(WND *wnd, const EVT *evt)
{
    if (!wnd || !evt) return;
    ClarityBook *bk = clarity_get_book();
    if (!bk || !bk->is_loaded) return;

    H rel_y = (H)(evt->pos.y - wnd->client.top);

    if (evt->type == EV_BUT_DOWN) {
        int wh = wnd->client.bottom - wnd->client.top;
        int total_h = TOC_HEADER_H + (bk->chapter_count + bk->class_count) * TOC_ENTRY_H;
        int max_sc = (total_h > wh) ? (total_h - wh) : 0;

        /* 1. Hit-test chapter rows first */
        int y = TOC_HEADER_H - g_toc_scroll;
        int hit = 0;
        for (int ci = 0; ci < bk->class_count && !hit; ci++) {
            const ClarityBookClass *cls = &bk->classes[ci];
            y += TOC_ENTRY_H; /* skip class header row */
            for (int ci2 = 0; ci2 < cls->chapter_count && !hit; ci2++) {
                int chidx = cls->chapter_indices[ci2];
                if (chidx < 0 || chidx >= bk->chapter_count) { y += TOC_ENTRY_H; continue; }
                if (rel_y >= (H)y && rel_y < (H)(y + TOC_ENTRY_H)) {
                    hit = 1;
                    clarity_book_load_chapter_into_doc(bk, chidx, &g_doc);
                    g_scroll_y = 0;
                    if (g_wnd) inval_wnd(g_wnd);
                    inval_wnd(wnd);
                }
                y += TOC_ENTRY_H;
            }
        }

        /* 2. No chapter row hit — treat as scroll-zone click */
        if (!hit) {
            if (rel_y < (H)(wh / 3)) {
                g_toc_scroll -= TOC_ENTRY_H * 3;
            } else if (rel_y > (H)(wh * 2 / 3)) {
                g_toc_scroll += TOC_ENTRY_H * 3;
            }
            if (g_toc_scroll < 0) g_toc_scroll = 0;
            if (g_toc_scroll > max_sc) g_toc_scroll = max_sc;
            inval_wnd(wnd);
        }
    }

    if (evt->type == EV_KEY_DOWN) {
        int wh = wnd->client.bottom - wnd->client.top;
        int total_h = TOC_HEADER_H + (bk->chapter_count + bk->class_count) * TOC_ENTRY_H;
        int max_sc = (total_h > wh) ? (total_h - wh) : 0;
        UW key = evt->key;
        if (key == BTRON_KEY_DOWN || key == BTRON_KEY_PAGE_DOWN) g_toc_scroll += TOC_ENTRY_H * 3;
        if (key == BTRON_KEY_UP   || key == BTRON_KEY_PAGE_UP)   g_toc_scroll -= TOC_ENTRY_H * 3;
        if (g_toc_scroll < 0) g_toc_scroll = 0;
        if (g_toc_scroll > max_sc) g_toc_scroll = max_sc;
        inval_wnd(wnd);
    }
}

static void destroy_toc_wnd(WND *wnd)
{
    (void)wnd;
    g_toc_wnd = NULL;
}

static void open_clarity_toc_window(void)
{
    ClarityBook *bk = clarity_get_book();
    if (!bk || !bk->is_loaded) return;

    /* Bring existing TOC window to top if alive */
    if (g_toc_wnd) {
        WND *w = get_wnd_list();
        while (w && w != g_toc_wnd) w = w->next;
        if (w == g_toc_wnd) {
            top_wnd(g_toc_wnd);
            inval_wnd(g_toc_wnd);
            return;
        }
        g_toc_wnd = NULL;
    }

    /* Position TOC window to the left of Clarity window */
    int toc_x = 10, toc_y = 36;
    int toc_h = 620;
    if (g_wnd) {
        toc_x = g_wnd->bounds.left - 186;
        toc_y = g_wnd->bounds.top;
        toc_h = g_wnd->bounds.bottom - g_wnd->bounds.top;
        if (toc_x < 4) toc_x = 4;
    }

    g_toc_scroll = 0;
    g_toc_wnd = opn_wnd("目録 – TOC",
                        toc_x, toc_y, 180, toc_h,
                        WND_ATTR_TITLE | WND_ATTR_CLOSE |
                        WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (g_toc_wnd) {
        g_toc_wnd->paint         = paint_toc_wnd;
        g_toc_wnd->event_handler = toc_event;
        g_toc_wnd->destroy       = destroy_toc_wnd;
        top_wnd(g_toc_wnd);
        inval_wnd(g_toc_wnd);
    }
}

WND* clarity_get_toc_wnd(void)
{
    return g_toc_wnd;
}

/* ------------------------------------------------------------------ */
/* Paint callback                                                      */
/* ------------------------------------------------------------------ */

static void clarity_paint(WND *wnd, GDEV *dev)
{
    if (!wnd || !dev) return;

    int win_w = wnd->client.right  - wnd->client.left;
    int win_h = wnd->client.bottom - wnd->client.top;

    int vw = 0, vh = 0, cw = 0, ch = 0, max_x = 0, max_y = 0;
    get_viewport_and_content_bounds(&vw, &vh, &cw, &ch, &max_x, &max_y);

    if (g_scroll_x > max_x) g_scroll_x = max_x;
    if (g_scroll_x < 0) g_scroll_x = 0;
    if (g_scroll_y > max_y) g_scroll_y = max_y;
    if (g_scroll_y < 0) g_scroll_y = 0;

    int ox = CLARITY_CANVAS_MARGIN_PX - g_scroll_x;
    int oy = CLARITY_CANVAS_MARGIN_PX + APP_MENU_BAR_HEIGHT - g_scroll_y;

    /* 1. Canvas Background Plate */
    RECT canvas_viewport = { 0, APP_MENU_BAR_HEIGHT, (H)vw, (H)(APP_MENU_BAR_HEIGHT + vh) };
    fill_rec(dev, &canvas_viewport, COLOR_LTGRAY);

    /* 2. Pages (Sheets, Dropshadows, Margin Guides, Separators) */
    clarity_draw_page(dev, &g_doc, ox, oy);

    /* 3. Text & Image Frames with Zoom */
    int zoom = g_doc.zoom_pct > 0 ? g_doc.zoom_pct : 100;
    for (int i = 0; i < g_doc.frame_count; i++) {
        ClarityFrame *f = &g_doc.frames[i];
        if (f->id == 0) continue;
        int fy0 = oy + (f->bounds.top    * zoom) / 100;
        int fy1 = oy + (f->bounds.bottom * zoom) / 100;
        if (fy1 < dev->clip.top || fy0 > dev->clip.bottom) continue; /* Viewport culling */
        BOOL is_sel = (i == g_doc.selected_frame);
        if (f->type == FRAME_TEXT) {
            clarity_render_text(dev, f, ox, oy, zoom, is_sel);
        } else if (f->type == FRAME_TAD) {
            clarity_render_tad(dev, f, ox, oy, zoom);
        } else {
            clarity_render_image(dev, f, ox, oy, zoom);
        }
    }

    /* 4. Frame Outlines & 8-point Resize Handles */
    clarity_draw_frames(dev, &g_doc, ox, oy);

    /* 5. Right Margin Vertical Scrollbar */
    paint_scrollbar_v(dev, vw, APP_MENU_BAR_HEIGHT, SCROLLBAR_SIZE, vh,
                      g_scroll_y, max_y, vh);

    /* 6. Bottom Horizontal Scrollbar */
    paint_scrollbar_h(dev, 0, APP_MENU_BAR_HEIGHT + vh, vw, SCROLLBAR_SIZE,
                      g_scroll_x, max_x, vw);

    /* 7. Bottom-Right Corner Filler */
    RECT corner = { (H)vw, (H)(APP_MENU_BAR_HEIGHT + vh), (H)win_w, (H)win_h };
    fill_rec(dev, &corner, COLOR_LTGRAY);
    drw_rec(dev, &corner);

    /* 8. Modal Dialog Overlay (if active) */
    if (g_dlg.type != CLARITY_DLG_NONE) {
        paint_clarity_dialog(dev, win_w, win_h);
    }

    /* 9. Menu Bar & Status Strip */
    const char *fmt_name = "A4";
    if (g_doc.fmt == FMT_SHIROKU) fmt_name = "四六判";
    else if (g_doc.fmt == FMT_PECHA) fmt_name = "Pecha";

    const char *mode_str = (tip_get_mode() == TIP_MODE_HIRAGANA) ? "あ" :
                           ((tip_get_mode() == TIP_MODE_KATAKANA) ? "ア" :
                            ((tip_get_mode() == TIP_MODE_TIBETAN) ? "བོད" : "A"));

    char status[128];
    ClarityBook *bk = clarity_get_book();
    if (bk && bk->is_loaded && bk->chapter_count > 0 &&
        bk->active_chapter_idx >= 0 && bk->active_chapter_idx < bk->chapter_count) {
        snprintf(status, sizeof(status), "  [TIP: %s] | %s | %d%% | ལེའུ %02d/%02d (%s)",
                 mode_str, fmt_name, g_doc.zoom_pct,
                 bk->active_chapter_idx + 1, bk->chapter_count,
                 bk->chapters[bk->active_chapter_idx].wylie_title);
    } else {
        snprintf(status, sizeof(status), "  [TIP: %s] | %s | %d%% | %d 頁 | %d 個",
                 mode_str, fmt_name, g_doc.zoom_pct, g_doc.page_count, g_doc.frame_count);
    }
    app_menu_set_right_text(&g_menu, status);

    app_menu_paint_bar(&g_menu, dev);
    if (g_menu.active_menu >= 0) {
        app_menu_paint_dropdown(&g_menu, dev);
    }
}

/* ------------------------------------------------------------------ */
/* Menu Command Dispatch                                               */
/* ------------------------------------------------------------------ */

static void handle_cmd(int cmd)
{
    switch (cmd) {
        case CMD_FILE_NEW:
            clarity_init_sample_page(&g_doc);
            g_doc.dirty = FALSE;
            break;
        case CMD_FILE_OPEN:
            g_dlg.type = CLARITY_DLG_OPEN_BOOK;
            if (!g_dlg.path[0]) strcpy(g_dlg.path, "./assets/osal");
            g_dlg.path_len = (int)strlen(g_dlg.path);
            g_dlg.pecha_size = g_doc.pecha_size;
            g_dlg.auto_tibetan = TRUE;
            g_dlg.focus_field = 0;
            break;
        case CMD_FILE_SAVE:
            clarity_export_save(&g_doc, "Clarity-Sample");
            clarity_export_save_file(&g_doc, CLARITY_SAMPLE_PATH);
            g_doc.dirty = FALSE;
            break;

        /* Book Operations */
        case CMD_BOOK_OPEN:
            g_dlg.type = CLARITY_DLG_OPEN_BOOK;
            if (!g_dlg.path[0]) strcpy(g_dlg.path, "./assets/osal");
            g_dlg.path_len = (int)strlen(g_dlg.path);
            g_dlg.pecha_size = g_doc.pecha_size;
            g_dlg.auto_tibetan = TRUE;
            g_dlg.focus_field = 0;
            break;

        case CMD_BOOK_EXPORT_TAD: {
            ClarityBook *bk = clarity_get_book();
            if (!bk->is_loaded) {
                clarity_book_open_dir(bk, g_dlg.path[0] ? g_dlg.path : "./assets/osal",
                                      (ClarityPechaSize)g_doc.pecha_size, TRUE);
            }
            if (bk->is_loaded) {
                clarity_book_export_tad(bk, "tad_bin/osal", "tad_bin/osal_shog_dril.tad");
            }
            break;
        }

        case CMD_BOOK_PREVIEW_TAD: {
            ClarityBook *bk = clarity_get_book();
            if (!bk->is_loaded) {
                clarity_book_open_dir(bk, g_dlg.path[0] ? g_dlg.path : "./assets/osal",
                                      (ClarityPechaSize)g_doc.pecha_size, TRUE);
            }
            if (bk->is_loaded) {
                clarity_book_export_tad(bk, "tad_bin/osal", "tad_bin/osal_shog_dril.tad");
                open_tad_browser_window("tad_bin/osal/index.tad", "【目録】Лонгчен Осал (TAD Browser)");
            }
            break;
        }

        case CMD_BOOK_TOC:
            open_clarity_toc_window();
            break;

        case CMD_BOOK_NEXT_CHAP: {
            ClarityBook *bk = clarity_get_book();
            if (bk->is_loaded) {
                clarity_book_next_chapter(bk, &g_doc);
                g_scroll_y = 0;
                if (g_toc_wnd) inval_wnd(g_toc_wnd);
            }
            break;
        }

        case CMD_BOOK_PREV_CHAP: {
            ClarityBook *bk = clarity_get_book();
            if (bk->is_loaded) {
                clarity_book_prev_chapter(bk, &g_doc);
                g_scroll_y = 0;
                if (g_toc_wnd) inval_wnd(g_toc_wnd);
            }
            break;
        }

        /* View / Zoom */
        case CMD_VIEW_ZOOM_IN:
            if (g_doc.zoom_pct < 200) g_doc.zoom_pct += 25;
            break;
        case CMD_VIEW_ZOOM_OUT:
            if (g_doc.zoom_pct > 25) g_doc.zoom_pct -= 25;
            break;
        case CMD_VIEW_ACTUAL:
        case CMD_VIEW_ZOOM_100:
            g_doc.zoom_pct = 100;
            break;
        case CMD_VIEW_FIT:
            clarity_fit_window();
            break;
        case CMD_VIEW_ZOOM_50:
            g_doc.zoom_pct = 50;
            break;
        case CMD_VIEW_ZOOM_75:
            g_doc.zoom_pct = 75;
            break;
        case CMD_VIEW_ZOOM_125:
            g_doc.zoom_pct = 125;
            break;
        case CMD_VIEW_ZOOM_150:
            g_doc.zoom_pct = 150;
            break;

        /* Formats */
        case CMD_FMT_A4:
            g_doc.fmt = FMT_A4;
            clarity_fmt_dimensions(&g_doc);
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_SHIROKU:
            g_doc.fmt = FMT_SHIROKU;
            clarity_fmt_dimensions(&g_doc);
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_PECHA:
        case CMD_FMT_PECHA_KANGYUR:
            clarity_set_pecha_mode(&g_doc, PECHA_SIZE_KANGYUR);
            clarity_fit_window();
            {
                ClarityBook *bk = clarity_get_book();
                if (bk && bk->is_loaded && bk->chapter_count > 0) {
                    bk->pecha_size = PECHA_SIZE_KANGYUR;
                    clarity_book_load_chapter_into_doc(bk, bk->active_chapter_idx, &g_doc);
                }
            }
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_PECHA_RINCHEN:
            clarity_set_pecha_mode(&g_doc, PECHA_SIZE_RINCHEN);
            clarity_fit_window();
            {
                ClarityBook *bk = clarity_get_book();
                if (bk && bk->is_loaded && bk->chapter_count > 0) {
                    bk->pecha_size = PECHA_SIZE_RINCHEN;
                    clarity_book_load_chapter_into_doc(bk, bk->active_chapter_idx, &g_doc);
                }
            }
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_PECHA_DERGE:
            clarity_set_pecha_mode(&g_doc, PECHA_SIZE_DERGE);
            clarity_fit_window();
            {
                ClarityBook *bk = clarity_get_book();
                if (bk && bk->is_loaded && bk->chapter_count > 0) {
                    bk->pecha_size = PECHA_SIZE_DERGE;
                    clarity_book_load_chapter_into_doc(bk, bk->active_chapter_idx, &g_doc);
                }
            }
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_PECHA_POCKET:
            clarity_set_pecha_mode(&g_doc, PECHA_SIZE_POCKET);
            clarity_fit_window();
            {
                ClarityBook *bk = clarity_get_book();
                if (bk && bk->is_loaded && bk->chapter_count > 0) {
                    bk->pecha_size = PECHA_SIZE_POCKET;
                    clarity_book_load_chapter_into_doc(bk, bk->active_chapter_idx, &g_doc);
                }
            }
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_PECHA_DERGE_WIDE:
            clarity_set_pecha_mode(&g_doc, PECHA_SIZE_DERGE_WIDE);
            clarity_fit_window();
            {
                ClarityBook *bk = clarity_get_book();
                if (bk && bk->is_loaded && bk->chapter_count > 0) {
                    bk->pecha_size = PECHA_SIZE_DERGE_WIDE;
                    clarity_book_load_chapter_into_doc(bk, bk->active_chapter_idx, &g_doc);
                }
            }
            g_doc.dirty = TRUE;
            break;

        /* Pages */
        case CMD_PAGE_ADD:
            if (g_doc.page_count < CLARITY_MAX_FRAMES) {
                int p = g_doc.page_count;
                g_doc.page_count++;
                g_doc.dirty = TRUE;
                /* Ensure newly added page has a text frame placeholder */
                int px_w = (g_doc.page_w_mm * CLARITY_DPI * 10) / 254;
                int px_h = (g_doc.page_h_mm * CLARITY_DPI * 10) / 254;
                int margin_x = 40, margin_y = 20;
                int p_gap = CLARITY_PAGE_GAP_PX;
                int frame_y = margin_y + p * (px_h + p_gap);
                clarity_doc_add_frame(&g_doc, FRAME_TEXT,
                                      margin_x, frame_y,
                                      px_w - margin_x * 2,
                                      px_h - margin_y * 2);
            }
            break;
        case CMD_PAGE_REMOVE:
            if (g_doc.page_count > 1) {
                g_doc.page_count--;
                g_doc.dirty = TRUE;
                if (g_doc.frame_count > g_doc.page_count) {
                    g_doc.frame_count = g_doc.page_count;
                }
                if (g_doc.selected_frame >= g_doc.frame_count) {
                    g_doc.selected_frame = g_doc.frame_count - 1;
                }
            }
            break;

        /* Insert */
        case CMD_INS_TEXT:
            g_doc.tool = TOOL_TEXT_FRAME;
            break;
        case CMD_INS_IMAGE:
            g_doc.tool = TOOL_IMAGE_FRAME;
            break;
        case CMD_INS_FLIP_FLOW:
            if (g_doc.selected_frame >= 0) {
                ClarityFrame *f = &g_doc.frames[g_doc.selected_frame];
                f->flow = (f->flow == FLOW_H_LTR) ? FLOW_V_RTL : FLOW_H_LTR;
                g_doc.dirty = TRUE;
            }
            break;

        /* Arrange / Z-Order */
        case CMD_ARRANGE_FRONT:
            if (g_doc.selected_frame >= 0) {
                clarity_doc_send_to_front(&g_doc, g_doc.selected_frame);
            }
            break;
        case CMD_ARRANGE_BACK:
            if (g_doc.selected_frame >= 0) {
                clarity_doc_send_to_back(&g_doc, g_doc.selected_frame);
            }
            break;
        case CMD_ARRANGE_FORWARD:
            if (g_doc.selected_frame >= 0) {
                clarity_doc_send_forward(&g_doc, g_doc.selected_frame);
            }
            break;
        case CMD_ARRANGE_BACKWARD:
            if (g_doc.selected_frame >= 0) {
                clarity_doc_send_backward(&g_doc, g_doc.selected_frame);
            }
            break;
        case CMD_ARRANGE_DUPLICATE:
            if (g_doc.selected_frame >= 0) {
                clarity_doc_duplicate_frame(&g_doc, g_doc.selected_frame);
            }
            break;

        default:
            break;
    }
    if (g_wnd) inval_wnd(g_wnd);
}

/* ------------------------------------------------------------------ */
/* Event handler                                                       */
/* ------------------------------------------------------------------ */

static void clarity_event(WND *wnd, const EVT *evt)
{
    if (!wnd || !evt) return;

    H rel_x = (H)(evt->pos.x - wnd->client.left);
    H rel_y = (H)(evt->pos.y - wnd->client.top);

    int vw = 0, vh = 0, cw = 0, ch = 0, max_x = 0, max_y = 0;
    get_viewport_and_content_bounds(&vw, &vh, &cw, &ch, &max_x, &max_y);

    int ox = CLARITY_CANVAS_MARGIN_PX - g_scroll_x;
    int oy = CLARITY_CANVAS_MARGIN_PX + APP_MENU_BAR_HEIGHT - g_scroll_y;
    int zoom = g_doc.zoom_pct > 0 ? g_doc.zoom_pct : 100;

    /* ── 1. Mouse Move ─────────────────────────────────────────────── */
    if (evt->type == EV_MOUSE_MOVE) {
        if (btron_dnd_is_active()) {
            int hf = clarity_hittest_frame(&g_doc, rel_x, rel_y, ox, oy);
            if (hf != g_doc.hover_drop_frame) {
                g_doc.hover_drop_frame = hf;
                inval_wnd(wnd);
            }
            clarity_set_cursor(CLARITY_CURSOR_HAND);
            return;
        }

        if (g_doc.selected_frame >= 0 && g_doc.selected_frame < g_doc.frame_count) {
            int vi = clarity_frame_find_vobj_at(&g_doc.frames[g_doc.selected_frame], rel_x, rel_y);
            if (vi >= 0) {
                clarity_set_cursor(CLARITY_CURSOR_HAND);
                return;
            }
        }

        /* Menu bar hover */
        if (app_menu_handle_mouse_move(&g_menu, rel_x, rel_y)) {
            clarity_set_cursor(CLARITY_CURSOR_ARROW);
            inval_wnd(wnd);
            return;
        }

        /* Vertical Scrollbar Dragging */
        if (g_sb_drag_v) {
            int track_h = vh - 32;
            int dy = evt->pos.y - g_sb_start_y;
            if (track_h > 0 && max_y > 0) {
                g_scroll_y = g_sb_orig_y + (dy * max_y) / track_h;
                if (g_scroll_y < 0) g_scroll_y = 0;
                if (g_scroll_y > max_y) g_scroll_y = max_y;
            }
            inval_wnd(wnd);
            return;
        }

        /* Horizontal Scrollbar Dragging */
        if (g_sb_drag_h) {
            int track_w = vw - 32;
            int dx = evt->pos.x - g_sb_start_x;
            if (track_w > 0 && max_x > 0) {
                g_scroll_x = g_sb_orig_x + (dx * max_x) / track_w;
                if (g_scroll_x < 0) g_scroll_x = 0;
                if (g_scroll_x > max_x) g_scroll_x = max_x;
            }
            inval_wnd(wnd);
            return;
        }

        /* Frame Move Dragging */
        if (g_drag_move && g_doc.selected_frame >= 0) {
            H dx = (H)(((rel_x - g_drag_prev_x) * 100) / zoom);
            H dy = (H)(((rel_y - g_drag_prev_y) * 100) / zoom);
            if (dx != 0 || dy != 0) {
                clarity_move_frame(&g_doc.frames[g_doc.selected_frame], dx, dy);
                g_drag_prev_x = rel_x;
                g_drag_prev_y = rel_y;
                g_doc.dirty   = TRUE;
            }
            clarity_set_cursor(CLARITY_CURSOR_MOVE);
            inval_wnd(wnd);
            return;
        }

        /* Frame Handle Resize Dragging */
        if (g_doc.dragging && g_doc.selected_frame >= 0 && g_doc.drag_handle >= 0) {
            clarity_resize_frame_handle(
                &g_doc.frames[g_doc.selected_frame],
                g_doc.drag_handle,
                rel_x, rel_y,
                ox, oy, zoom);
            g_doc.dirty = TRUE;
            inval_wnd(wnd);
            return;
        }

        /* Frame Creation Dragging */
        if (g_doc.dragging && g_doc.drag_handle < 0 && g_doc.selected_frame < 0) {
            clarity_set_cursor(CLARITY_CURSOR_MOVE);
            inval_wnd(wnd);
            return;
        }

        /* Dynamic Cursor Update on Hover */
        if (rel_y < APP_MENU_BAR_HEIGHT) {
            clarity_set_cursor(CLARITY_CURSOR_ARROW);
        } else if (rel_x >= vw || rel_y >= APP_MENU_BAR_HEIGHT + vh) {
            clarity_set_cursor(CLARITY_CURSOR_ARROW);
        } else {
            /* Inside Canvas Viewport */
            ClarityHitInfo hinfo;
            clarity_hittest_full(&g_doc, rel_x, rel_y, ox, oy, &hinfo);

            if (hinfo.target == CLARITY_HIT_HANDLE) {
                switch (hinfo.handle_idx) {
                    case 0:
                    case 4: clarity_set_cursor(CLARITY_CURSOR_NWSE); break;
                    case 2:
                    case 6: clarity_set_cursor(CLARITY_CURSOR_NESW); break;
                    case 1:
                    case 5: clarity_set_cursor(CLARITY_CURSOR_NS);   break;
                    case 3:
                    case 7: clarity_set_cursor(CLARITY_CURSOR_WE);   break;
                    default: clarity_set_cursor(CLARITY_CURSOR_ARROW); break;
                }
            } else if (hinfo.target == CLARITY_HIT_PERIMETER) {
                clarity_set_cursor(CLARITY_CURSOR_MOVE);
            } else if (hinfo.target == CLARITY_HIT_INTERIOR) {
                if (g_doc.frames[hinfo.frame_idx].type == FRAME_TEXT) {
                    clarity_set_cursor(CLARITY_CURSOR_IBEAM);
                } else {
                    clarity_set_cursor(CLARITY_CURSOR_ARROW);
                }
            } else {
                clarity_set_cursor(CLARITY_CURSOR_ARROW);
            }
        }
        return;
    }

    /* ── 2. Mouse Button Down ───────────────────────────────────────── */
    if (evt->type == EV_BUT_DOWN) {

        /* Modal Dialog Click Handling */
        if (g_dlg.type != CLARITY_DLG_NONE) {
            if (rel_x >= g_dlg.btn_ok.left && rel_x <= g_dlg.btn_ok.right &&
                rel_y >= g_dlg.btn_ok.top && rel_y <= g_dlg.btn_ok.bottom) {
                const char *tgt_path = g_dlg.path[0] ? g_dlg.path : "./assets/osal";
                ClarityFileIntegrity fi;
                if (clarity_check_file_integrity(tgt_path, &fi) == 0 && fi.is_valid) {
                    ClarityBook *bk = clarity_get_book();
                    clarity_book_open_dir(bk, tgt_path,
                                          (ClarityPechaSize)g_dlg.pecha_size, g_dlg.auto_tibetan);
                    clarity_set_pecha_mode(&g_doc, g_dlg.pecha_size);
                    clarity_fit_window(); /* Recalculate Desk */
                    clarity_book_load_chapter_into_doc(bk, 0, &g_doc); /* Refill Pages on Open */
                    g_dlg.type = CLARITY_DLG_NONE;
                    g_scroll_x = 0;
                    g_scroll_y = 0;
                    open_clarity_toc_window();
                    inval_wnd(wnd);
                    return;
                }
            }
            if (rel_x >= g_dlg.btn_cancel.left && rel_x <= g_dlg.btn_cancel.right &&
                rel_y >= g_dlg.btn_cancel.top && rel_y <= g_dlg.btn_cancel.bottom) {
                g_dlg.type = CLARITY_DLG_NONE;
                inval_wnd(wnd);
                return;
            }
            if (rel_x >= g_dlg.path_box.left && rel_x <= g_dlg.path_box.right &&
                rel_y >= g_dlg.path_box.top && rel_y <= g_dlg.path_box.bottom) {
                g_dlg.focus_field = 0;
                inval_wnd(wnd);
                return;
            }
            for (int r = 0; r < 5; r++) {
                if (rel_x >= g_dlg.radio_boxes[r].left && rel_x <= g_dlg.radio_boxes[r].right &&
                    rel_y >= g_dlg.radio_boxes[r].top && rel_y <= g_dlg.radio_boxes[r].bottom) {
                    g_dlg.pecha_size = r;
                    g_dlg.focus_field = 1;
                    inval_wnd(wnd);
                    return;
                }
            }
            if (rel_x >= g_dlg.check_box.left && rel_x <= g_dlg.check_box.right &&
                rel_y >= g_dlg.check_box.top && rel_y <= g_dlg.check_box.bottom) {
                g_dlg.auto_tibetan = !g_dlg.auto_tibetan;
                g_dlg.focus_field = 2;
                inval_wnd(wnd);
                return;
            }
            if (rel_x >= g_dlg.box.left && rel_x <= g_dlg.box.right &&
                rel_y >= g_dlg.box.top && rel_y <= g_dlg.box.bottom) {
                return; /* Consume click within modal dialog */
            }
            g_dlg.type = CLARITY_DLG_NONE;
            inval_wnd(wnd);
            return;
        }

        /* Menu Bar clicks */
        int cmd = -1, sub = -1;
        if (app_menu_handle_mouse_down(&g_menu, rel_x, rel_y, &cmd, &sub)) {
            if (cmd >= 0) handle_cmd(cmd);
            inval_wnd(wnd);
            return;
        }

        /* Vertical Scrollbar clicks */
        if (rel_x >= vw && rel_x < vw + SCROLLBAR_SIZE &&
            rel_y >= APP_MENU_BAR_HEIGHT && rel_y < APP_MENU_BAR_HEIGHT + vh) {
            int sy = rel_y - APP_MENU_BAR_HEIGHT;
            if (sy < 16) {
                /* Up button */
                g_scroll_y -= CLARITY_STEP_SCROLL;
                if (g_scroll_y < 0) g_scroll_y = 0;
            } else if (sy >= vh - 16) {
                /* Down button */
                g_scroll_y += CLARITY_STEP_SCROLL;
                if (g_scroll_y > max_y) g_scroll_y = max_y;
            } else {
                /* Track or Thumb */
                int track_h = vh - 32;
                int thumb_h = (max_y > 0) ? (track_h * vh) / (vh + max_y) : track_h;
                if (thumb_h < 16) thumb_h = 16;
                if (thumb_h > track_h) thumb_h = track_h;

                int thumb_y = 16 + (max_y > 0 ? (g_scroll_y * (track_h - thumb_h)) / max_y : 0);
                if (sy >= thumb_y && sy <= thumb_y + thumb_h) {
                    g_sb_drag_v  = TRUE;
                    g_sb_start_y = evt->pos.y;
                    g_sb_orig_y  = g_scroll_y;
                } else if (sy < thumb_y) {
                    g_scroll_y -= vh;
                    if (g_scroll_y < 0) g_scroll_y = 0;
                } else {
                    g_scroll_y += vh;
                    if (g_scroll_y > max_y) g_scroll_y = max_y;
                }
            }
            inval_wnd(wnd);
            return;
        }

        /* Horizontal Scrollbar clicks */
        if (rel_y >= APP_MENU_BAR_HEIGHT + vh && rel_y < APP_MENU_BAR_HEIGHT + vh + SCROLLBAR_SIZE &&
            rel_x >= 0 && rel_x < vw) {
            int sx = rel_x;
            if (sx < 16) {
                /* Left button */
                g_scroll_x -= CLARITY_STEP_SCROLL;
                if (g_scroll_x < 0) g_scroll_x = 0;
            } else if (sx >= vw - 16) {
                /* Right button */
                g_scroll_x += CLARITY_STEP_SCROLL;
                if (g_scroll_x > max_x) g_scroll_x = max_x;
            } else {
                /* Track or Thumb */
                int track_w = vw - 32;
                int thumb_w = (max_x > 0) ? (track_w * vw) / (vw + max_x) : track_w;
                if (thumb_w < 16) thumb_w = 16;
                if (thumb_w > track_w) thumb_w = track_w;

                int thumb_x = 16 + (max_x > 0 ? (g_scroll_x * (track_w - thumb_w)) / max_x : 0);
                if (sx >= thumb_x && sx <= thumb_x + thumb_w) {
                    g_sb_drag_h  = TRUE;
                    g_sb_start_x = evt->pos.x;
                    g_sb_orig_x  = g_scroll_x;
                } else if (sx < thumb_x) {
                    g_scroll_x -= vw;
                    if (g_scroll_x < 0) g_scroll_x = 0;
                } else {
                    g_scroll_x += vw;
                    if (g_scroll_x > max_x) g_scroll_x = max_x;
                }
            }
            inval_wnd(wnd);
            return;
        }

        /* Canvas Click (using client-relative coordinates) */
        H cx = rel_x;
        H cy = rel_y;

        if (g_doc.tool == TOOL_SELECT) {
            ClarityHitInfo hinfo;
            clarity_hittest_full(&g_doc, cx, cy, ox, oy, &hinfo);

            if (hinfo.target == CLARITY_HIT_HANDLE) {
                g_doc.dragging    = TRUE;
                g_doc.drag_handle = hinfo.handle_idx;
                inval_wnd(wnd);
                return;
            }

            if (hinfo.target == CLARITY_HIT_PERIMETER) {
                g_doc.selected_frame = hinfo.frame_idx;
                g_drag_move   = TRUE;
                g_drag_prev_x = cx;
                g_drag_prev_y = cy;
                inval_wnd(wnd);
                return;
            }

            if (hinfo.target == CLARITY_HIT_INTERIOR) {
                g_doc.selected_frame = hinfo.frame_idx;
                ClarityFrame *f = &g_doc.frames[hinfo.frame_idx];

                static int s_last_clarity_fidx = -1;
                static UW s_last_clarity_click = 0;
                UW cur_time = (UW)(uintptr_t)evt->data;
                if (cur_time == 0) {
#if BTRON_HOSTED
                    struct timeval tv;
                    gettimeofday(&tv, NULL);
                    cur_time = (UW)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
#endif
                }
                BOOL is_double = (s_last_clarity_fidx == hinfo.frame_idx &&
                                  s_last_clarity_click != 0 &&
                                  (cur_time - s_last_clarity_click < 500));

                if (f->type == FRAME_TEXT) {
                    int vi = clarity_frame_find_vobj_at(f, cx, cy);
                    if (vi >= 0) {
                        ClarityVObjLink *vl = &f->vobjs[vi];
                        int plen = (int)strlen(vl->path);
                        if ((plen > 4 && strcmp(vl->path + plen - 4, ".tad") == 0) ||
                            (plen > 4 && strcmp(vl->path + plen - 4, ".TAD") == 0)) {
                            open_tad_browser_window(vl->path, vl->label);
                        } else {
                            open_t_editor_window_with_file(vl->path);
                        }
                    } else {
                        f->cursor_pos = clarity_text_xy_to_pos(f, cx, cy, ox, oy, zoom);
                    }
                } else if (f->type == FRAME_TAD) {
                    if (is_double || cy >= (H)(oy + (f->bounds.top * zoom) / 100 + 60)) {
                        open_tad_browser_window(f->tad_path, f->tad_title);
                    }
                }

                if (is_double) {
                    s_last_clarity_fidx = -1;
                    s_last_clarity_click = 0;
                } else {
                    s_last_clarity_fidx = hinfo.frame_idx;
                    s_last_clarity_click = cur_time;
                }

                inval_wnd(wnd);
                return;
            }

            /* Canvas background click deselects */
            g_doc.selected_frame = -1;
            inval_wnd(wnd);
            return;
        }

        /* Tool active: start drag-creation */
        if (g_doc.tool == TOOL_TEXT_FRAME || g_doc.tool == TOOL_IMAGE_FRAME) {
            g_doc.dragging     = TRUE;
            g_doc.drag_start_x = (H)(((cx - ox) * 100) / zoom);
            g_doc.drag_start_y = (H)(((cy - oy) * 100) / zoom);
            g_doc.drag_handle  = -1;
            return;
        }
        return;
    }

    /* ── 3. Mouse Button Up ─────────────────────────────────────────── */
    if (evt->type == EV_BUT_UP) {
        g_sb_drag_v = FALSE;
        g_sb_drag_h = FALSE;
        g_drag_move = FALSE;
        g_doc.drag_handle = -1;

        if (btron_dnd_is_active()) {
            clarity_handle_dnd_drop(&g_doc, btron_dnd_get(), rel_x, rel_y, ox, oy);
            g_doc.hover_drop_frame = -1;
            g_doc.dragging = FALSE;
            inval_wnd(wnd);
            return;
        }

        if (g_doc.dragging &&
            (g_doc.tool == TOOL_TEXT_FRAME || g_doc.tool == TOOL_IMAGE_FRAME)) {
            g_doc.dragging = FALSE;
            H cur_unzoomed_x = (H)(((rel_x - ox) * 100) / zoom);
            H cur_unzoomed_y = (H)(((rel_y - oy) * 100) / zoom);
            H fx = g_doc.drag_start_x;
            H fy = g_doc.drag_start_y;
            H fw = (H)(cur_unzoomed_x - fx);
            H fh = (H)(cur_unzoomed_y - fy);
            if (fw < 0) { fx += fw; fw = -fw; }
            if (fh < 0) { fy += fh; fh = -fh; }
            if (fw < 32) fw = 32;
            if (fh < 24) fh = 24;

            ClarityFrameType ft = (g_doc.tool == TOOL_TEXT_FRAME)
                                  ? FRAME_TEXT : FRAME_IMAGE;
            ClarityFrame *nf = doc_add_frame(ft, fx, fy, fw, fh);
            if (nf) {
                g_doc.selected_frame = g_doc.frame_count - 1;
            }
            g_doc.tool = TOOL_SELECT;
            inval_wnd(wnd);
            return;
        }

        g_doc.dragging = FALSE;
        return;
    }

    /* ── 4. Key Down / Typing ───────────────────────────────────────── */
    if (evt->type == EV_KEY_DOWN) {
        UW key = evt->key;
        uint16_t mod = (uint16_t)(uintptr_t)evt->data;
        BOOL ctrl = (mod & BTRON_KMOD_CTRL) != 0;
        BOOL shift = (mod & BTRON_KMOD_SHIFT) != 0;

        /* Modal Dialog Key Handling */
        if (g_dlg.type != CLARITY_DLG_NONE) {
            if (key == BTRON_KEY_ESCAPE) {
                g_dlg.type = CLARITY_DLG_NONE;
                inval_wnd(wnd);
                return;
            }
            if (key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER || key == '\r' || key == '\n') {
                const char *tgt_path = g_dlg.path[0] ? g_dlg.path : "./assets/osal";
                ClarityFileIntegrity fi;
                if (clarity_check_file_integrity(tgt_path, &fi) == 0 && fi.is_valid) {
                    ClarityBook *bk = clarity_get_book();
                    clarity_book_open_dir(bk, tgt_path,
                                          (ClarityPechaSize)g_dlg.pecha_size, g_dlg.auto_tibetan);
                    clarity_set_pecha_mode(&g_doc, g_dlg.pecha_size);
                    clarity_fit_window(); /* Recalculate Desk */
                    clarity_book_load_chapter_into_doc(bk, 0, &g_doc); /* Refill Pages on Open */
                    g_dlg.type = CLARITY_DLG_NONE;
                    g_scroll_x = 0;
                    g_scroll_y = 0;
                    open_clarity_toc_window();
                    inval_wnd(wnd);
                    return;
                }
            }
            if (key == '\t' || key == BTRON_KEY_TAB) {
                g_dlg.focus_field = (g_dlg.focus_field + 1) % 5;
                inval_wnd(wnd);
                return;
            }
            if (key == BTRON_KEY_UP) {
                if (g_dlg.pecha_size >= 2) g_dlg.pecha_size -= 2;
                else if (g_dlg.pecha_size > 0) g_dlg.pecha_size--;
                inval_wnd(wnd);
                return;
            }
            if (key == BTRON_KEY_DOWN) {
                if (g_dlg.pecha_size + 2 <= 4) g_dlg.pecha_size += 2;
                else if (g_dlg.pecha_size < 4) g_dlg.pecha_size++;
                inval_wnd(wnd);
                return;
            }
            if (key == BTRON_KEY_LEFT) {
                if (g_dlg.pecha_size % 2 == 1) g_dlg.pecha_size--;
                inval_wnd(wnd);
                return;
            }
            if (key == BTRON_KEY_RIGHT) {
                if (g_dlg.pecha_size % 2 == 0 && g_dlg.pecha_size + 1 <= 4) g_dlg.pecha_size++;
                inval_wnd(wnd);
                return;
            }
            if (key == ' ') {
                if (g_dlg.focus_field == 2) {
                    g_dlg.auto_tibetan = !g_dlg.auto_tibetan;
                    inval_wnd(wnd);
                    return;
                }
            }
            if (g_dlg.focus_field == 0) {
                if (key == BTRON_KEY_BACKSPACE || key == 0x08) {
                    if (g_dlg.path_len > 0) {
                        g_dlg.path[--g_dlg.path_len] = '\0';
                        inval_wnd(wnd);
                    }
                    return;
                }
                if (key >= 32 && key <= 126) {
                    if (g_dlg.path_len < (int)sizeof(g_dlg.path) - 1) {
                        g_dlg.path[g_dlg.path_len++] = (char)key;
                        g_dlg.path[g_dlg.path_len] = '\0';
                        inval_wnd(wnd);
                    }
                    return;
                }
            }
            return; /* Absorb all keys when modal dialog is visible */
        }

        /* Menu navigation shortcuts */
        int menu_cmd = 0;
        if (app_menu_handle_key(&g_menu, key, mod, &menu_cmd)) {
            if (menu_cmd != 0) handle_cmd(menu_cmd);
            inval_wnd(wnd);
            return;
        }

        /* Ctrl shortcuts */
        if (ctrl) {
            if (key == '=' || key == '+') {
                handle_cmd(CMD_VIEW_ZOOM_IN);
                return;
            } else if (key == '-' || key == '_') {
                handle_cmd(CMD_VIEW_ZOOM_OUT);
                return;
            } else if (key == '0') {
                handle_cmd(CMD_VIEW_ACTUAL);
                return;
            } else if (key == 'f' || key == 'F') {
                handle_cmd(CMD_VIEW_FIT);
                return;
            } else if (key == 's' || key == 'S') {
                handle_cmd(CMD_FILE_SAVE);
                return;
            } else if (key == 'n' || key == 'N') {
                handle_cmd(CMD_FILE_NEW);
                return;
            } else if (key == 'b' || key == 'B') {
                handle_cmd(CMD_BOOK_OPEN);
                return;
            } else if (key == 'e' || key == 'E') {
                handle_cmd(CMD_BOOK_EXPORT_TAD);
                return;
            } else if (key == 'p' || key == 'P') {
                handle_cmd(CMD_BOOK_PREVIEW_TAD);
                return;
            } else if (key == 'd' || key == 'D') {
                handle_cmd(CMD_ARRANGE_DUPLICATE);
                return;
            } else if (key == ']' || key == '}') {
                if (shift) handle_cmd(CMD_ARRANGE_FRONT);
                else handle_cmd(CMD_ARRANGE_FORWARD);
                return;
            } else if (key == '[' || key == '{') {
                if (shift) handle_cmd(CMD_ARRANGE_BACK);
                else handle_cmd(CMD_ARRANGE_BACKWARD);
                return;
            }
        }

        if (key == BTRON_KEY_PAGE_DOWN) {
            handle_cmd(CMD_BOOK_NEXT_CHAP);
            return;
        }
        if (key == BTRON_KEY_PAGE_UP) {
            handle_cmd(CMD_BOOK_PREV_CHAP);
            return;
        }

        /* Check Mozc / Tibetan TIP input method */
        char tip_buf[128] = "";
        if (tip_process_key(key, mod, tip_buf, sizeof(tip_buf))) {
            if (tip_buf[0] != '\0' && g_doc.selected_frame >= 0) {
                clarity_insert_tip_text(&g_doc, g_doc.selected_frame, tip_buf);
            }
            inval_wnd(wnd);
            return;
        }

        /* Escape key: clear selection or tool */
        if (key == BTRON_KEY_ESCAPE) {
            g_doc.tool = TOOL_SELECT;
            app_menu_close(&g_menu);
            inval_wnd(wnd);
            return;
        }

        /* If no text frame is selected, UP/DOWN arrow keys scroll the canvas */
        int fidx = g_doc.selected_frame;
        if (fidx < 0 || g_doc.frames[fidx].type != FRAME_TEXT) {
            int vw = 0, vh = 0, cw = 0, ch = 0, max_x = 0, max_y = 0;
            get_viewport_and_content_bounds(&vw, &vh, &cw, &ch, &max_x, &max_y);
            if (key == BTRON_KEY_UP) {
                g_scroll_y -= CLARITY_STEP_SCROLL;
                if (g_scroll_y < 0) g_scroll_y = 0;
                inval_wnd(wnd);
                return;
            } else if (key == BTRON_KEY_DOWN) {
                g_scroll_y += CLARITY_STEP_SCROLL;
                if (g_scroll_y > max_y) g_scroll_y = max_y;
                inval_wnd(wnd);
                return;
            }
        }

        /* Interactive Text Entry into Selected TextFrame */
        if (fidx >= 0 && g_doc.frames[fidx].type == FRAME_TEXT) {
            if (key == BTRON_KEY_BACKSPACE || key == 0x08) {
                clarity_handle_text_action(&g_doc, fidx, ACT_BACKSPACE, 0);
            } else if (key == BTRON_KEY_DELETE || key == 0x7F) {
                clarity_handle_text_action(&g_doc, fidx, ACT_DELETE, 0);
            } else if (key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER || key == '\r' || key == '\n') {
                clarity_handle_text_action(&g_doc, fidx, ACT_ENTER, 0);
            } else if (key == BTRON_KEY_LEFT) {
                clarity_handle_text_action(&g_doc, fidx, ACT_LEFT, 0);
            } else if (key == BTRON_KEY_RIGHT) {
                clarity_handle_text_action(&g_doc, fidx, ACT_RIGHT, 0);
            } else if (key == BTRON_KEY_UP) {
                clarity_handle_text_action(&g_doc, fidx, ACT_UP, 0);
            } else if (key == BTRON_KEY_DOWN) {
                clarity_handle_text_action(&g_doc, fidx, ACT_DOWN, 0);
            } else if (key == BTRON_KEY_HOME) {
                clarity_handle_text_action(&g_doc, fidx, ACT_HOME, 0);
            } else if (key == BTRON_KEY_END) {
                clarity_handle_text_action(&g_doc, fidx, ACT_END, 0);
            } else if (key == BTRON_KEY_PAGE_UP) {
                clarity_handle_text_action(&g_doc, fidx, ACT_PAGEUP, 0);
            } else if (key == BTRON_KEY_PAGE_DOWN) {
                clarity_handle_text_action(&g_doc, fidx, ACT_PAGEDOWN, 0);
            } else if (key >= 32 && key <= 126) {
                /* Printable ASCII with shift handling */
                char ch = (char)key;
                if (shift) {
                    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
                    else {
                        switch (ch) {
                            case '1': ch = '!'; break;
                            case '2': ch = '@'; break;
                            case '3': ch = '#'; break;
                            case '4': ch = '$'; break;
                            case '5': ch = '%'; break;
                            case '6': ch = '^'; break;
                            case '7': ch = '&'; break;
                            case '8': ch = '*'; break;
                            case '9': ch = '('; break;
                            case '0': ch = ')'; break;
                            case '-': ch = '_'; break;
                            case '=': ch = '+'; break;
                            case '[': ch = '{'; break;
                            case ']': ch = '}'; break;
                            case '\\': ch = '|'; break;
                            case ';': ch = ':'; break;
                            case '\'': ch = '"'; break;
                            case ',': ch = '<'; break;
                            case '.': ch = '>'; break;
                            case '/': ch = '?'; break;
                            case '`': ch = '~'; break;
                        }
                    }
                }
                clarity_handle_text_action(&g_doc, fidx, ACT_CHAR, (UH)(unsigned char)ch);
            } else if (key >= 0x0100 && key <= 0xFFFF) {
                /* Direct TRON code / Unicode */
                clarity_handle_text_action(&g_doc, fidx, ACT_CHAR, (UH)key);
            }
            inval_wnd(wnd);
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Menu Construction                                                  */
/* ------------------------------------------------------------------ */

static void build_menu(void)
{
    app_menu_init(&g_menu, APP_MENU_STYLE_CLASSIC_3D);

    /* 1. File */
    int fi = app_menu_add_header(&g_menu, "ファイル(F)", 100);
    app_menu_add_item(&g_menu, fi, "新規作成 (New)",            "Ctrl+N", CMD_FILE_NEW,          TRUE);
    app_menu_add_item(&g_menu, fi, "開く (Open)...",             "Ctrl+O", CMD_FILE_OPEN,         TRUE);
    app_menu_add_item(&g_menu, fi, "書籍を開く (Open Book)...",  "Ctrl+B", CMD_BOOK_OPEN,         TRUE);
    app_menu_add_item(&g_menu, fi, "TAD 書籍出力 (Produce TAD)...","Ctrl+E", CMD_BOOK_EXPORT_TAD,  TRUE);
    app_menu_add_item(&g_menu, fi, "TAD Browser で閲覧 (Preview)","Ctrl+P", CMD_BOOK_PREVIEW_TAD, TRUE);
    app_menu_add_separator(&g_menu, fi);
    app_menu_add_item(&g_menu, fi, "保存 (Save)",                "Ctrl+S", CMD_FILE_SAVE,         TRUE);

    /* 2. View / Zoom */
    int vi = app_menu_add_header(&g_menu, "表示(V)", 80);
    app_menu_add_item(&g_menu, vi, "拡大 (Zoom In +)",     "Ctrl++", CMD_VIEW_ZOOM_IN,  TRUE);
    app_menu_add_item(&g_menu, vi, "縮小 (Zoom Out -)",    "Ctrl+-", CMD_VIEW_ZOOM_OUT, TRUE);
    app_menu_add_item(&g_menu, vi, "等倍 (Actual 100%)",   "Ctrl+0", CMD_VIEW_ACTUAL,   TRUE);
    app_menu_add_item(&g_menu, vi, "全体表示 (Fit Window)", "Ctrl+F", CMD_VIEW_FIT,      TRUE);
    app_menu_add_separator(&g_menu, vi);
    app_menu_add_item(&g_menu, vi, "50% 表示",   "", CMD_VIEW_ZOOM_50,  TRUE);
    app_menu_add_item(&g_menu, vi, "75% 表示",   "", CMD_VIEW_ZOOM_75,  TRUE);
    app_menu_add_item(&g_menu, vi, "100% 表示",  "", CMD_VIEW_ZOOM_100, TRUE);
    app_menu_add_item(&g_menu, vi, "125% 表示",  "", CMD_VIEW_ZOOM_125, TRUE);
    app_menu_add_item(&g_menu, vi, "150% 表示",  "", CMD_VIEW_ZOOM_150, TRUE);

    /* 3. Format */
    int fmti = app_menu_add_header(&g_menu, "判型(P)", 80);
    app_menu_add_item(&g_menu, fmti, "A4 判型 (210×297 mm)", "", CMD_FMT_A4, TRUE);
    app_menu_add_item(&g_menu, fmti, "四六判 (127×188 mm・縦書き)", "", CMD_FMT_SHIROKU, TRUE);
    app_menu_add_separator(&g_menu, fmti);
    app_menu_add_item(&g_menu, fmti, "Pecha 標準経典判 (560×110 mm)", "", CMD_FMT_PECHA_KANGYUR, TRUE);
    app_menu_add_item(&g_menu, fmti, "Pecha 大蔵経・宝庫判 (650×140 mm)", "", CMD_FMT_PECHA_RINCHEN, TRUE);
    app_menu_add_item(&g_menu, fmti, "Pecha デルゲ木版大判 (700×180 mm)", "", CMD_FMT_PECHA_DERGE, TRUE);
    app_menu_add_item(&g_menu, fmti, "Pecha 行者暗誦判 (320×85 mm)", "", CMD_FMT_PECHA_POCKET, TRUE);
    app_menu_add_item(&g_menu, fmti, "Pecha デルゲ広幅木版 (720×200 mm・25行)", "", CMD_FMT_PECHA_DERGE_WIDE, TRUE);
    app_menu_add_separator(&g_menu, fmti);
    app_menu_add_item(&g_menu, fmti, "ページ追加 (Add Page)", "+", CMD_PAGE_ADD, TRUE);
    app_menu_add_item(&g_menu, fmti, "ページ削除 (Remove Page)", "-", CMD_PAGE_REMOVE, TRUE);

    /* 4. Chapter Navigation */
    int ci = app_menu_add_header(&g_menu, "章(C)", 70);
    app_menu_add_item(&g_menu, ci, "目録 (TOC Window)", "Ctrl+T", CMD_BOOK_TOC,       TRUE);
    app_menu_add_separator(&g_menu, ci);
    app_menu_add_item(&g_menu, ci, "次の章 (Next Chapter)",      "PgDn", CMD_BOOK_NEXT_CHAP, TRUE);
    app_menu_add_item(&g_menu, ci, "前の章 (Previous Chapter)",  "PgUp", CMD_BOOK_PREV_CHAP, TRUE);

    /* 5. Insert */
    int ii = app_menu_add_header(&g_menu, "挿入(I)", 80);
    app_menu_add_item(&g_menu, ii, "文字列枠 (Text Frame)",  "T", CMD_INS_TEXT,  TRUE);
    app_menu_add_item(&g_menu, ii, "画像枠 (Image Frame)",   "I", CMD_INS_IMAGE, TRUE);
    app_menu_add_separator(&g_menu, ii);
    app_menu_add_item(&g_menu, ii, "書字方向切替 (横↔縦)", "F", CMD_INS_FLIP_FLOW, TRUE);

    /* 6. Arrange / Z-Order */
    int ai = app_menu_add_header(&g_menu, "配置(A)", 80);
    app_menu_add_item(&g_menu, ai, "最前面へ (Send to Front)",  "Ctrl+Shift+]", CMD_ARRANGE_FRONT,     TRUE);
    app_menu_add_item(&g_menu, ai, "最背面へ (Send to Back)",   "Ctrl+Shift+[", CMD_ARRANGE_BACK,      TRUE);
    app_menu_add_item(&g_menu, ai, "前面へ (Bring Forward)",    "Ctrl+]",       CMD_ARRANGE_FORWARD,   TRUE);
    app_menu_add_item(&g_menu, ai, "背面へ (Send Backward)",    "Ctrl+[",       CMD_ARRANGE_BACKWARD,  TRUE);
    app_menu_add_separator(&g_menu, ai);
    app_menu_add_item(&g_menu, ai, "枠複製 (Duplicate Frame)",  "Ctrl+D",       CMD_ARRANGE_DUPLICATE, TRUE);
}

/* ------------------------------------------------------------------ */
/* Window Destruction Hook                                             */
/* ------------------------------------------------------------------ */

static void destroy_clarity(WND *wnd)
{
    (void)wnd;
    g_wnd = NULL;
    clarity_set_cursor(CLARITY_CURSOR_ARROW);
}

/* ------------------------------------------------------------------ */
/* Public Window Opener (Canonical Non-Blocking BTRON Convention)     */
/* ------------------------------------------------------------------ */

static BOOL clarity_menu_open(WND *wnd) {
    (void)wnd;
    return (g_menu.active_menu >= 0) ? TRUE : FALSE;
}

WND* open_clarity_window(void)
{
    if (g_wnd) {
        WND *w = get_wnd_list();
        while (w && w != g_wnd) w = w->next;
        if (w == g_wnd) {
            top_wnd(g_wnd);
            return g_wnd;
        }
    }
    g_wnd = NULL;

    /* Make Osal Book opened by default in Clarity if available */
    BOOL has_osal = FALSE;
    const char *osal_dir = "./assets/osal";
#if BTRON_HOSTED
    struct stat st_osal;
    if (stat(osal_dir, &st_osal) == 0 && S_ISDIR(st_osal.st_mode)) {
        has_osal = TRUE;
    } else if (stat("assets/osal", &st_osal) == 0 && S_ISDIR(st_osal.st_mode)) {
        osal_dir = "assets/osal";
        has_osal = TRUE;
    }
#endif

    int clr_x = has_osal ? 196 : 40;
    int clr_y = 36;
    int clr_w = has_osal ? 820 : 900;
    int clr_h = 620;

    g_wnd = opn_wnd("電子帳票 – Clarity",
                    clr_x, clr_y, clr_w, clr_h,
                    WND_ATTR_TITLE | WND_ATTR_CLOSE |
                    WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (!g_wnd) return NULL;

    g_wnd->paint         = clarity_paint;
    g_wnd->event_handler = clarity_event;
    g_wnd->destroy       = destroy_clarity;
    g_wnd->menu_open     = clarity_menu_open;

    build_menu();

    if (has_osal) {
        ClarityBook *bk = clarity_get_book();
        clarity_book_open_dir(bk, osal_dir, PECHA_SIZE_DERGE, TRUE);
        clarity_set_pecha_mode(&g_doc, PECHA_SIZE_DERGE);
        clarity_fit_window(); /* Recalculate Desk */
        if (bk->is_loaded && bk->chapter_count > 0) {
            clarity_book_load_chapter_into_doc(bk, 0, &g_doc); /* Refill Pages on Open */
        } else {
            doc_new(FMT_A4);
            clarity_init_sample_page(&g_doc);
            clarity_fit_window();
        }
    } else {
        doc_new(FMT_A4);
        /* Try loading saved sample file from /SYS/Clarity-Sample.TAD */
        if (clarity_export_load_file(&g_doc, CLARITY_SAMPLE_PATH) == E_OK && g_doc.frame_count > 0) {
            g_doc.selected_frame = 0;
        } else {
            clarity_init_sample_page(&g_doc);
            clarity_export_save_file(&g_doc, CLARITY_SAMPLE_PATH);
        }
        clarity_fit_window(); /* Recalculate Desk */
    }

    top_wnd(g_wnd);
    inval_wnd(g_wnd);

    if (has_osal) {
        open_clarity_toc_window();
    }

    return g_wnd;
}

ClarityDoc* clarity_get_doc(void)
{
    return &g_doc;
}

void clarity_app_open(void)
{
    open_clarity_window();
}

/* ------------------------------------------------------------------ */
/* Legacy init helper (spec API surface; kept for header compatibility)*/
/* ------------------------------------------------------------------ */

void clarity_init(ClarityDoc *doc)
{
    if (!doc) return;
    memset(doc, 0, sizeof(ClarityDoc));
    doc->fmt             = FMT_A4;
    doc->page_count      = 2;
    doc->zoom_pct        = 75;
    doc->selected_frame  = -1;
    doc->tool            = TOOL_SELECT;
    clarity_fmt_dimensions(doc);
}

void clarity_set_pecha_mode(ClarityDoc *doc, int size)
{
    if (!doc) return;
    doc->fmt = FMT_PECHA;
    doc->pecha_size = size;
    clarity_fmt_dimensions(doc);
}
