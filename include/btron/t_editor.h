#ifndef _BTRON_T_EDITOR_H_
#define _BTRON_T_EDITOR_H_

#include <btron/types.h>
#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(BTRON_PS2_TARGET)
#include <btron/ps2_tile.h>
#define TEDITOR_MAX_ROWS  PS2_BUDGET_TEDITOR_ROWS
#define TEDITOR_MAX_LINES PS2_BUDGET_TEDITOR_ROWS
#define TEDITOR_MAX_COLS  PS2_BUDGET_TEDITOR_COLS
#else
#define TEDITOR_MAX_ROWS  1000
#define TEDITOR_MAX_LINES 1000
#define TEDITOR_MAX_COLS  512
#endif
#define TEDITOR_VIEW_ROWS 14
#define TEDITOR_VIEW_COLS 80

#include <btron/app_menu.h>

/* Editor Menu Command IDs */
enum {
    TCMD_NONE = 0,
    TCMD_FILE_NEW,
    TCMD_FILE_OPEN_SUBMENU,
    TCMD_FILE_OPEN_ASSET,
    TCMD_FILE_SAVE,
    TCMD_FILE_CLOSE,
    TCMD_EDIT_UNDO,
    TCMD_EDIT_CUT,
    TCMD_EDIT_COPY,
    TCMD_EDIT_PASTE,
    TCMD_EDIT_SELECT_ALL,
    TCMD_VIEW_ZOOM_IN,
    TCMD_VIEW_ZOOM_OUT,
    TCMD_VIEW_TOGGLE_LINES,
    TCMD_VIEW_WRAP_TOGGLE,
    TCMD_VOBJ_INSERT,
    TCMD_VOBJ_CABINET,
    TCMD_HELP_ABOUT
};

/* ── Hierarchical Tree Open Menu Subsystem ───────────────────────── */
#define TEDITOR_TREE_MAX_LEVELS  6
#define TEDITOR_MENU_NAME_LEN    64
#define TEDITOR_MENU_PATH_LEN   256

typedef struct {
    char lines[TEDITOR_MAX_ROWS][TEDITOR_MAX_COLS];
    int total_lines;

    int cursor_row;
    int cursor_col;

    /* Selection */
    BOOL sel_active;
    int sel_start_r, sel_start_c;
    int sel_end_r, sel_end_c;
    int sel_anchor_r, sel_anchor_c;

    /* Scrolling */
    int scroll_row;
    int scroll_col;
    int scroll_x;              /* Horizontal pixel scroll in unwrapped mode */
    int max_content_width;     /* Widest line width in pixels */

    /* File State */
    char filename[128];
    BOOL is_modified;

    /* Word Wrap */
    BOOL wrap_text;

    /* VOBJ Embed */
    BOOL has_vobj;
    char vobj_name[64];

    /* Menu Bar State (BeOS BMenuBar / BTRON 3.20 Menu Manager) */
    int active_menu;       /* -1 = closed, 0=File, 1=Edit, 2=View, 3=Objects, 4=Help */
    int hover_menu;        /* -1 = none, 0..4 = hovered top-level menu header */
    int hover_item;        /* -1 = none, 0..N = hovered item in active menu */
    int active_submenu;    /* -1 = none, 0..N = item index spawning cascading submenu */
    int hover_subitem;     /* -1 = none, 0..N = hovered item in cascading submenu */
    int tree_hover[TEDITOR_TREE_MAX_LEVELS]; /* Cascading tree menu hover indices for Open menu levels */
    BOOL show_line_nums;   /* TRUE = display gutter line numbers */
    APP_MENU_BAR menu_bar;
} TEditor;

WND* open_t_editor_window(void);
WND* open_t_editor_window_with_file(const char *filepath);
TEditor* teditor_get_current(void);
int teditor_load_file(TEditor *ed, const char *filepath);
int teditor_save_file(TEditor *ed, const char *filepath);
int teditor_close_file(TEditor *ed);

/* Menu Manager APIs */

typedef struct {
    char name[TEDITOR_MENU_NAME_LEN];
    char path[TEDITOR_MENU_PATH_LEN];
    BOOL is_dir;
    BOOL is_sep;
} TMenuTreeItem;

int teditor_get_asset_files(char files[][64], int max_files);
int teditor_scan_fs_dir(const char *dir_path, TMenuTreeItem *out_items, int max_items);
void teditor_get_level_box(const TEditor *ed, GDEV *dev, int lvl, RECT *out_box, int *out_count);
void teditor_open_menu(TEditor *ed, int menu_idx);
void teditor_close_menu(TEditor *ed);
WND* open_teditor_about_window(void);

/* CUA Selection & Word Wrap APIs */
void teditor_get_selection_range(const TEditor *ed, int *r1, int *c1, int *r2, int *c2);
void teditor_toggle_wrap(TEditor *ed);
void teditor_set_hscroll_clamped(TEditor *ed, int ns, H dev_w);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_T_EDITOR_H_ */
