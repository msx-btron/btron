/*
 * B-System BTRON3 — b_installer.h
 * Clean-Room Authentic Haiku-Style BTRON Installer Application.
 *
 * Modeled on the classic Haiku / BeOS Installer workflow:
 *  - Welcome / Try Live Desktop or Install mode
 *  - Source volume selector (Live USB/CD /SYS)
 *  - Destination partition selector (VirtIO Disk / Partition)
 *  - "Set up partitions..." button opening DriveSetup (open_drivesetup_window)
 *  - Step-by-step Real Body & bootloader deployment pipeline
 *  - NASA/JPL Rule 3: 100% static non-allocating memory model
 */

#ifndef BTRON_APPS_VOLUMES_B_INSTALLER_H
#define BTRON_APPS_VOLUMES_B_INSTALLER_H

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
#include <fs/block.h>
#include <fs/volume.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INSTALLER_MAX_TARGETS       8
#define INSTALLER_MAX_SOURCES       4
#define INSTALLER_MAX_PATH_LEN      64

enum {
    INST_CMD_NONE = 0,
    INST_CMD_ABOUT = 1,
    INST_CMD_DRIVESETUP = 2,
    INST_CMD_TERMINAL = 3,
    INST_CMD_RESCAN = 4,
    INST_CMD_QUIT = 5
};

typedef enum {
    INSTALLER_STATE_WELCOME = 0,    /* Welcome / Initial Mode */
    INSTALLER_STATE_SETUP,          /* Main Haiku-style Japanese Card Setup View */
    INSTALLER_STATE_CONFIRM,        /* Warning & confirmation dialog */
    INSTALLER_STATE_COPYING,        /* Active installation progress */
    INSTALLER_STATE_COMPLETE,       /* Finish & Reboot prompt */
    INSTALLER_STATE_ERROR           /* Error reporting */
} InstallerStateMode;

typedef struct {
    char        name[32];           /* e.g. "/SYS" */
    char        desc[64];           /* e.g. "Live USB (/SYS) - 24 MB" */
    Volume      *vol;
    uint32_t    size_mb;
} InstallerSource;

typedef struct {
    char        dev_path[INSTALLER_MAX_PATH_LEN];   /* e.g. "VirtIO Disk 1 (Raw Disk, 64 MB)" */
    char        label[32];                          /* e.g. "Raw Disk" */
    uint64_t    size_bytes;
    int         dev_idx;                            /* VirtIO unit index */
    int         part_idx;                           /* Partition index (0..3, -1 for raw) */
    bool        is_btron_formatted;
    bool        is_mounted;
} InstallerTarget;

typedef struct {
    InstallerStateMode  mode;

    /* Source Media Selector (Pop-up Combobox) */
    int                 source_count;
    int                 selected_source_idx;
    InstallerSource     sources[INSTALLER_MAX_SOURCES];
    bool                source_dropdown_open;
    int                 source_dropdown_hover;
    RECT                source_combobox_rect;
    RECT                source_dropdown_rect;

    /* Destination Disk Selector (Pop-up Combobox) */
    int                 target_count;
    int                 selected_target_idx;
    InstallerTarget     targets[INSTALLER_MAX_TARGETS];
    bool                target_dropdown_open;
    int                 target_dropdown_hover;
    RECT                target_combobox_rect;
    RECT                target_dropdown_rect;

    char                source_name[32];            /* e.g. "Live /SYS" */
    Volume              *source_vol;

    int                 progress_percent;           /* 0..100 */
    int                 current_step;               /* 1..4 */
    char                unique_disk_label[32];      /* e.g. "BTRON-X45IJ-37GFX" */
    char                status_msg[96];
    char                error_msg[96];

    /* Haiku-style Optional Packages Disclosure */
    bool                show_optional_packages;
    RECT                disclosure_rect;
    RECT                optional_box_rect;

    /* Native B-System Application Menu Bar */
    APP_MENU_BAR        menu_bar;

    /* Menu Bar: Tools */
    bool                tools_menu_open;
    int                 tools_menu_hover;
    RECT                tools_menu_rect;
    RECT                tools_dropdown_rect;

    /* Button hit-test rects */
    RECT                btn_try_live;
    RECT                btn_install_mode;
    RECT                btn_setup_partitions;
    RECT                btn_refresh;
    RECT                btn_begin;
    RECT                btn_restart;
    RECT                btn_close;
} InstallerState;

/* Public API */
void            b_installer_init(InstallerState *st);
InstallerState* b_installer_get_state(void);
void            b_installer_generate_unique_label(char *out, size_t out_sz);
void            b_installer_scan_targets(InstallerState *st);
bool            b_installer_select_target(InstallerState *st, int idx);
bool            b_installer_start_install(InstallerState *st);
bool            b_installer_step_copy(InstallerState *st);
WND*            open_installer_window(void);

/* Window callbacks */
void            installer_paint(WND *wnd, GDEV *dev);
void            installer_event_handler(WND *wnd, const EVT *evt);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_APPS_VOLUMES_B_INSTALLER_H */
