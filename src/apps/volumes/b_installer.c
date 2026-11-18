/*
 * B-System BTRON3 — b_installer.c
 * Clean-Room Authentic Haiku-Style BTRON Installer Application.
 *
 * Implements:
 *  - Authentic Haiku / BeOS Installer workflow formatted as a Japanese Card (名刺/Meishi).
 *  - Red Dot Award Design proportions: 91x55 card ratio, subtle hairline cards,
 *    Japanese lacquer vermilion brand seal, clean letterspaced typography.
 *  - Responsive geometry ("little responsive but not much", clamped and stable).
 *  - Top "Tools" menu bar: About, DriveSetup, Rescan, Quit.
 *  - Dual Pop-up Combobox controls:
 *      * "Install from:" Source volume selector (/SYS, /ANDERS, Both)
 *      * "Onto:" Destination disk selector (VirtIO Raw Disks, Partitions)
 *  - "› Show optional packages" disclosure toggle.
 *  - "Set up partitions..." button opening DriveSetup (open_drivesetup_window).
 *  - Step-by-step Real Body & bootloader deployment pipeline.
 *  - NASA/JPL Rule 3: 100% static non-allocating memory model.
 */

#include "b_installer.h"
#include "b_drivesetup.h"
#include <btron/core.h>
#include <drivers/virtio/virtio_blk.h>
#include <btron/troncode.h>
#include <btron/desktop.h>
#include <btron/event.h>
#include <fs/vol_api.h>
#include <btron/libc_shim.h>

extern void uart_puts_raw(const char *s);
extern Volume *g_sys_vol;
extern Volume *g_anders_vol;

extern WND* open_vobj_manager_window(void) __attribute__((weak, weak_import));
extern WND* open_t_editor_window(void) __attribute__((weak, weak_import));
extern WND* open_gterm_window(void) __attribute__((weak, weak_import));
extern WND* open_glgears_window(void) __attribute__((weak, weak_import));
extern WND* open_drivesetup_window(void) __attribute__((weak, weak_import));

static InstallerState g_installer_state;
static WND *g_installer_wnd = NULL;

/* ── Color Palette: Japanese Card Aesthetic meets BeOS/Haiku Cleanliness ── */
#define INST_COL_BG           0xFFE8E5DC   /* Warm card desktop face */
#define INST_COL_MENUBAR_BG   0xFFDFDAD2   /* Clean menu bar */
#define INST_COL_CARD_WHITE   0xFFD0CCC4   /* Pure white inner card */
#define INST_COL_CARD_BORDER  0xFFD0CCC4  /* 1px hairline card border */
#define INST_COL_TEXT_BLACK   0xFF141923   /* Deep Charcoal for high legibility */
#define INST_COL_TEXT_MUTED   0xFF5C6573   /* Slate Gray for secondary text */
#define INST_COL_TEXT_FAINT   0xFF8C93A0   /* Soft Gray for tertiary info */
#define INST_COL_BLUE_ACCENT  0xFF2B608A   /* BTRON / Haiku Steel Blue */
#define INST_COL_BLUE_LIGHT   0xFFEBF3FC   /* Light Blue Card Tint */
#define INST_COL_SEL_BG       0xFF1D5C96   /* System Navy Selection */
#define INST_COL_HIGHLIGHT    0xFFD0CCC4   /* 3D Bevel Top-Left Light */
#define INST_COL_SHADOW       0xFF8A857A   /* 3D Bevel Bottom-Right Shadow */
#define INST_COL_SHADOW_DARK  0xFF504C44  /* Deep Inset Shadow */
#define INST_COL_GREEN        0xFF167C3B   /* Success Forest Green */
#define INST_COL_GREEN_BG     0xFFEBF7EE   /* Success Card Tint */
#define INST_COL_RED_SEAL     0xFFCC2222   /* Japanese Vermilion Lacquer Seal */
#define INST_COL_RED          0xFFC02424   /* Alert Red */

static void safe_strcpy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool pt_in_rect(int x, int y, const RECT *r) {
    return (x >= r->left && x <= r->right && y >= r->top && y <= r->bottom);
}

/* ── 3D Bevel Rendering Engine ───────────────────────────────────────── */

static void draw_3d_frame(GDEV *dev, const RECT *r, bool inset) {
    if (inset) {
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        drw_lin(dev, r->left, r->top, r->right, r->top);
        drw_lin(dev, r->left, r->top, r->left, r->bottom);
        set_col(dev, COLOR_WHITE, COLOR_WHITE);
        drw_lin(dev, r->right, r->top, r->right, r->bottom);
        drw_lin(dev, r->left, r->bottom, r->right, r->bottom);
    } else {
        app_menu_draw_3d_bevel_box(dev, r);
    }
}

static void draw_3d_bevel_box_custom(GDEV *dev, const RECT *r, COLOR fill_col) {
    fill_rec(dev, r, fill_col);
    app_menu_draw_3d_bevel_box(dev, r);
}

static void draw_down_triangle(GDEV *dev, int cx, int cy, int size, COLOR col) {
    set_col(dev, col, col);
    for (int r = 0; r < size; r++) {
        drw_lin(dev, cx - (size - 1 - r), cy - (size / 2) + r,
                     cx + (size - 1 - r), cy - (size / 2) + r);
    }
}

static void draw_bevel_btn(GDEV *dev, const RECT *r, const char *label, bool pressed, bool enabled, bool primary) {
    COLOR bg_col = pressed ? 0xFFCFCAC0 : (primary ? 0xFFDFE6F0 : COLOR_LTGRAY);
    fill_rec(dev, r, bg_col);

    if (pressed) {
        set_col(dev, COLOR_DKGRAY, bg_col);
        drw_lin(dev, r->left, r->top, r->right - 1, r->top);
        drw_lin(dev, r->left, r->top, r->left, r->bottom - 1);
        set_col(dev, COLOR_WHITE, bg_col);
        drw_lin(dev, r->left + 1, r->bottom - 1, r->right - 1, r->bottom - 1);
        drw_lin(dev, r->right - 1, r->top + 1, r->right - 1, r->bottom - 1);
    } else {
        app_menu_draw_3d_bevel_box(dev, r);
        if (primary) {
            set_col(dev, 0xFF2B608A, bg_col);
            RECT inner = { (H)(r->left + 1), (H)(r->top + 1), (H)(r->right - 1), (H)(r->bottom - 1) };
            drw_rec(dev, &inner);
        }
    }

    if (label) {
        int text_w = (int)strlen(label) * 8;
        int tx = r->left + (r->right - r->left - text_w) / 2;
        int ty = r->top + (r->bottom - r->top - 14) / 2;
        if (pressed) { tx++; ty++; }

        COLOR fg = enabled ? (primary ? 0xFF1B4F8A : COLOR_BLACK) : COLOR_GRAY;
        drw_tc_string(dev, tx, ty, label, fg, bg_col);
    }
}

static void draw_haiku_combobox(GDEV *dev, const RECT *r, const char *text, bool is_open) {
    /* Native BTRON 3D Beveled Outer Plate */
    fill_rec(dev, r, COLOR_WHITE);
    app_menu_draw_3d_bevel_box(dev, r);

    /* Right Down-Arrow Tab using standard BTRON bevel */
    int tab_w = 24;
    RECT tab_r = { (H)(r->right - tab_w), (H)(r->top + 1), (H)(r->right - 1), (H)(r->bottom - 1) };
    fill_rec(dev, &tab_r, is_open ? 0xFFCFCAC0 : COLOR_LTGRAY);
    app_menu_draw_3d_bevel_box(dev, &tab_r);

    int mid_x = tab_r.left + tab_w / 2;
    int mid_y = r->top + (r->bottom - r->top) / 2;
    draw_down_triangle(dev, mid_x, mid_y, 4, COLOR_BLACK);

    if (text) {
        drw_tc_string(dev, r->left + 8, r->top + 6, text, COLOR_BLACK, INST_COL_CARD_BORDER);
    }
}

/* ── Layout Calculations: Japanese Card Proportions (91x55 ratio) ──── */

static void installer_calc_layout(InstallerState *st, int w, int h) {
    if (w < 480) w = 480;
    if (h < 310) h = 310;

    /* Tools Menu bar at top */
    st->tools_menu_rect.left   = 0;
    st->tools_menu_rect.top    = 0;
    st->tools_menu_rect.right  = (H)w;
    st->tools_menu_rect.bottom = 20;

    /* Form rows: Install from & Onto */
    int combo_l = 204;
    int combo_r = w - 16;

    st->source_combobox_rect.left   = (H)combo_l;
    st->source_combobox_rect.top    = 130;
    st->source_combobox_rect.right  = (H)combo_r;
    st->source_combobox_rect.bottom = 158;

    st->target_combobox_rect.left   = (H)combo_l;
    st->target_combobox_rect.top    = 168;
    st->target_combobox_rect.right  = (H)combo_r;
    st->target_combobox_rect.bottom = 196;

    int row_h = 26;
    int src_drop_h = (st->source_count > 0 ? st->source_count : 2) * row_h + 6;
    st->source_dropdown_rect.left   = st->source_combobox_rect.left;
    st->source_dropdown_rect.top    = st->source_combobox_rect.bottom;
    st->source_dropdown_rect.right  = st->source_combobox_rect.right;
    st->source_dropdown_rect.bottom = (H)(st->source_combobox_rect.bottom + src_drop_h);

    int tgt_drop_h = (st->target_count > 0 ? st->target_count : 2) * row_h + 6;
    st->target_dropdown_rect.left   = st->target_combobox_rect.left;
    st->target_dropdown_rect.top    = st->target_combobox_rect.bottom;
    st->target_dropdown_rect.right  = st->target_combobox_rect.right;
    st->target_dropdown_rect.bottom = (H)(st->target_combobox_rect.bottom + tgt_drop_h);

    /* Disclosure: Show optional packages */
    st->disclosure_rect.left   = 16;
    st->disclosure_rect.top    = 206;
    st->disclosure_rect.right  = 230;
    st->disclosure_rect.bottom = 224;

    st->optional_box_rect.left   = 16;
    st->optional_box_rect.top    = 228;
    st->optional_box_rect.right  = (H)(w - 16);
    st->optional_box_rect.bottom = 286;

    /* Bottom Buttons */
    int btn_y = h - 42;
    int btn_b = h - 12;

    st->btn_setup_partitions.left   = 16;
    st->btn_setup_partitions.top    = (H)btn_y;
    st->btn_setup_partitions.right  = 186;
    st->btn_setup_partitions.bottom = (H)btn_b;

    st->btn_begin.left   = (H)(w - 126);
    st->btn_begin.top    = (H)btn_y;
    st->btn_begin.right  = (H)(w - 16);
    st->btn_begin.bottom = (H)btn_b;

    st->btn_close.left   = 16;
    st->btn_close.top    = (H)btn_y;
    st->btn_close.right  = 136;
    st->btn_close.bottom = (H)btn_b;

    st->btn_restart.left   = (H)(w - 180);
    st->btn_restart.top    = (H)btn_y;
    st->btn_restart.right  = (H)(w - 16);
    st->btn_restart.bottom = (H)btn_b;

    st->btn_refresh.left   = (H)(w - 230);
    st->btn_refresh.top    = (H)btn_y;
    st->btn_refresh.right  = (H)(w - 136);
    st->btn_refresh.bottom = (H)btn_b;

    /* Compatibility rects for automated tests */
    st->btn_try_live     = st->btn_close;
    st->btn_install_mode = st->btn_begin;
}

/* ── Public API Implementation ───────────────────────────────────────── */

InstallerState* b_installer_get_state(void) {
    return &g_installer_state;
}

void b_installer_init(InstallerState *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));

    st->mode = INSTALLER_STATE_WELCOME;
    st->progress_percent = 0;
    st->current_step = 1;

    st->source_dropdown_open = false;
    st->source_dropdown_hover = -1;
    st->target_dropdown_open = false;
    st->target_dropdown_hover = -1;
    st->tools_menu_open = false;
    st->tools_menu_hover = -1;
    st->show_optional_packages = false;

    /* Initialize Native B-System Application Menu Bar */
    app_menu_init(&st->menu_bar, APP_MENU_STYLE_CLASSIC_3D);
    int h_tools = app_menu_add_header(&st->menu_bar, "Tools", 60);
    app_menu_add_item(&st->menu_bar, h_tools, "About B-System Installer...", "", INST_CMD_ABOUT, TRUE);
    app_menu_add_separator(&st->menu_bar, h_tools);
    app_menu_add_item(&st->menu_bar, h_tools, "Set up partitions (DriveSetup)...", "", INST_CMD_DRIVESETUP, TRUE);
    app_menu_add_item(&st->menu_bar, h_tools, "Terminal (GTerm Console)...", "T", INST_CMD_TERMINAL, TRUE);
    app_menu_add_item(&st->menu_bar, h_tools, "Rescan Disks", "R", INST_CMD_RESCAN, TRUE);
    app_menu_add_separator(&st->menu_bar, h_tools);
    app_menu_add_item(&st->menu_bar, h_tools, "Quit Installer", "Q", INST_CMD_QUIT, TRUE);

    /* Populate exactly two sources: /SYS and /ANDERS */
    st->source_count = 0;

    InstallerSource *s0 = &st->sources[st->source_count++];
    safe_strcpy(s0->name, "/SYS", sizeof(s0->name));
    safe_strcpy(s0->desc, "Live Media: /SYS - 24 MB (Core System)", sizeof(s0->desc));
    s0->vol = g_sys_vol;
    s0->size_mb = 24;

    InstallerSource *s1 = &st->sources[st->source_count++];
    safe_strcpy(s1->name, "/ANDERS", sizeof(s1->name));
    safe_strcpy(s1->desc, "Live Media: /ANDERS - 24 MB (Applications)", sizeof(s1->desc));
    s1->vol = g_anders_vol;
    s1->size_mb = 24;

    st->selected_source_idx = 0;
    st->source_vol = g_sys_vol;
    safe_strcpy(st->source_name, s0->desc, sizeof(st->source_name));
    safe_strcpy(st->status_msg, "Ready to configure installation.", sizeof(st->status_msg));

    b_installer_scan_targets(st);
    installer_calc_layout(st, 640, 480);
}

static uint32_t s_installer_label_seed = 0x19840420;

void b_installer_generate_unique_label(char *out, size_t out_sz) {
    if (!out || out_sz < 18) return;

    s_installer_label_seed = s_installer_label_seed * 1664525u + 1013904223u;
    uint32_t seed = s_installer_label_seed;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    uint32_t lo = 0, hi = 0;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    seed ^= (lo ^ (hi << 16));
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0 || defined(BTRON_UEFI_TARGET)
    /* Mix CMOS RTC wall-clock registers so separate QEMU runs never collide */
    for (uint8_t reg = 0; reg <= 9; reg += 2) {
        __asm__ volatile("outb %0, $0x70" : : "a"(reg));
        uint8_t v = 0;
        __asm__ volatile("inb $0x71, %0" : "=a"(v));
        seed = (seed * 131u) ^ v;
    }
#endif
#endif
    seed ^= (uint32_t)btron_fs_now_stamp();

    /* Mix existing on-disk MBR/VolumeHeader bytes across all VirtIO disks */
    int vcnt = virtio_blk_get_device_count();
    for (int d = 0; d < vcnt; d++) {
        uint8_t sec[512];
        if (virtio_blk_read_sectors(d, 0, sec, 1) == 0) {
            for (int i = 446; i < 512; i++) {
                seed = (seed * 33u) ^ sec[i];
            }
            for (int p = 0; p < 4; p++) {
                const uint8_t *ent = sec + 446 + p * 16;
                uint32_t st_lba = (uint32_t)ent[8] | ((uint32_t)ent[9] << 8) |
                                  ((uint32_t)ent[10] << 16) | ((uint32_t)ent[11] << 24);
                if (st_lba > 0 && virtio_blk_read_sectors(d, st_lba, sec, 1) == 0) {
                    for (int k = 28; k < 68; k++) {
                        seed = (seed * 33u) ^ sec[k];
                    }
                }
            }
        }
    }

    /* Alphanumeric charset containing digits (0-9) and letters (A-Z) */
    static const char charset[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    const int charset_len = (int)sizeof(charset) - 1;

    char part1[6];
    char part2[6];
    for (int i = 0; i < 5; i++) {
        seed = seed * 1664525u + 1013904223u;
        part1[i] = charset[((seed >> 16) ^ (seed & 0xFFFF)) % charset_len];
        seed = seed * 1664525u + 1013904223u;
        part2[i] = charset[((seed >> 16) ^ (seed & 0xFFFF)) % charset_len];
    }
    part1[5] = '\0';
    part2[5] = '\0';

    snprintf(out, out_sz, "BTRON-%s-%s", part1, part2);
}

void b_installer_scan_targets(InstallerState *st) {
    if (!st) return;
    st->target_count = 0;
    st->selected_target_idx = -1;

    /* Always synchronize DriveSetup devices with on-disk MBR & slices */
    b_drivesetup_scan_devices(&g_drivesetup_state);

    /* Scan DriveSetup devices and partitions:
     * - Partitions mounted by DriveSetup (excluding running /SYS and /ANDERS)
     * - Empty (raw) or formatted slices prepared in DriveSetup
     */
    for (int d = 0; d < g_drivesetup_state.device_count && st->target_count < INSTALLER_MAX_TARGETS; d++) {
        DriveSetupDevice *dev = &g_drivesetup_state.devices[d];
        int v_unit = d;
        if (strncmp(dev->raw_path, "/dev/virtio", 11) == 0) {
            v_unit = dev->raw_path[11] - '0';
        }

        /* Check for blank / unpartitioned raw disk */
        if (dev->partition_count == 0) {
            InstallerTarget *t = &st->targets[st->target_count++];
            memset(t, 0, sizeof(*t));
            t->dev_idx = v_unit;
            t->part_idx = -1;
            t->start_lba = 0;
            t->size_bytes = dev->total_bytes ? dev->total_bytes : (64ULL * 1024 * 1024);
            t->is_btron_formatted = false;
            t->is_mounted = false;
            safe_strcpy(t->label, "Blank Disk", sizeof(t->label));
            uint32_t mb = (uint32_t)(t->size_bytes / (1024 * 1024));
            if (mb == 0) mb = 1;
            snprintf(t->dev_path, sizeof(t->dev_path), "%s - %u MB (Whole Raw Disk)",
                     dev->raw_path, mb);
            continue;
        }

        for (int p = 0; p < dev->partition_count && st->target_count < INSTALLER_MAX_TARGETS; p++) {
            DriveSetupPartition *part = &dev->partitions[p];

            /* Skip EFI System Partitions (0xEF) */
            if (part->type_code == 0xEF) continue;

            /* Filter out installer source media (/SYS, /ANDERS, or any source volume) */
            bool is_src = false;
            for (int s = 0; s < st->source_count; s++) {
                if (st->sources[s].vol && part->vol_handle && part->vol_handle == st->sources[s].vol) is_src = true;
                if (part->mount_point[0] && strcmp(part->mount_point, st->sources[s].name) == 0) is_src = true;
                if (part->label[0] && strcmp(part->label, st->sources[s].name + 1) == 0) is_src = true;
            }
            if (g_sys_vol && part->vol_handle && part->vol_handle == g_sys_vol) is_src = true;
            if (g_anders_vol && part->vol_handle && part->vol_handle == g_anders_vol) is_src = true;
            if (strcmp(part->mount_point, "/SYS") == 0 || strcmp(part->mount_point, "/ANDERS") == 0) is_src = true;
            if (strcmp(part->label, "SYS") == 0 || strcmp(part->label, "ANDERS") == 0) is_src = true;
            if (is_src) continue;

            /* Check if partition is mounted OR empty (raw) prepared in DriveSetup */
            bool is_empty_raw = (part->fs_type == FS_RAW || part->fs_type == FS_UNKNOWN ||
                                 part->type_code == BTRON_PART_TYPE_RAW || !part->mounted);

            if (part->mounted || is_empty_raw) {
                InstallerTarget *t = &st->targets[st->target_count++];
                memset(t, 0, sizeof(*t));
                t->dev_idx  = v_unit;
                t->part_idx = p;
                {   /* DriveSetup list is compacted; the real MBR slot is encoded in /dev/virtioNpM */
                    size_t dl = strlen(part->dev_path);
                    if (dl > 0 && part->dev_path[dl-1] >= '1' && part->dev_path[dl-1] <= '4' && dl > 2 && part->dev_path[dl-2] == 'p')
                        t->part_idx = part->dev_path[dl-1] - '1';
                }
                t->start_lba = part->start_lba;
                t->size_bytes = part->block_count * (part->block_size ? part->block_size : 512);
                t->is_btron_formatted = part->mounted && (part->fs_type == FS_BFS_V1 || part->fs_type == FS_BFS_V2 || part->type_code == 0x13);
                t->is_mounted = part->mounted;
                safe_strcpy(t->label, part->label[0] ? part->label : (is_empty_raw ? "Empty (Raw)" : "Target Partition"), sizeof(t->label));

                uint32_t mb = (uint32_t)(t->size_bytes / (1024 * 1024));
                if (mb == 0) mb = 1;

                if (is_empty_raw && !part->mounted) {
                    const char *slice_desc = (part->label[0] && strcmp(part->label, "BTRON Partition") != 0 &&
                                              strcmp(part->label, "Raw Partition") != 0)
                                             ? part->label : "Empty Raw Slice";
                    snprintf(t->dev_path, sizeof(t->dev_path), "%s - %u MB (%s)",
                             part->dev_path[0] ? part->dev_path : "Raw Partition", mb, slice_desc);
                } else {
                    snprintf(t->dev_path, sizeof(t->dev_path), "%s - %u MB (%s)",
                             part->dev_path[0] ? part->dev_path : part->mount_point,
                             mb, part->label[0] ? part->label : "Mounted");
                }
            }
        }
    }

    if (st->target_count > 0) {
        if (st->selected_target_idx < 0 || st->selected_target_idx >= st->target_count) {
            st->selected_target_idx = 0;
        }
    } else {
        st->selected_target_idx = -1;
    }
}

bool b_installer_select_target(InstallerState *st, int idx) {
    if (!st || idx < 0 || idx >= st->target_count) return false;
    st->selected_target_idx = idx;
    return true;
}

bool b_installer_start_install(InstallerState *st) {
    if (!st || st->selected_target_idx < 0 || st->selected_target_idx >= st->target_count) {
        safe_strcpy(st->error_msg, "Please choose a destination disk.", sizeof(st->error_msg));
        return false;
    }

    InstallerTarget *t = &st->targets[st->selected_target_idx];

    if (t->part_idx >= 0 && t->size_bytes > 0 && st->source_vol &&
        (uint64_t)vol_total_blocks(st->source_vol) * 1024ULL > t->size_bytes) {
        snprintf(st->error_msg, sizeof(st->error_msg), "Target slice too small: need %u MB.",
                 (unsigned)(((uint64_t)vol_total_blocks(st->source_vol) * 1024ULL) >> 20));
        return false;
    }

    /* Generate unique disk label (e.g. BTRON-X45IJ-37GFX) on each installation */
    b_installer_generate_unique_label(st->unique_disk_label, sizeof(st->unique_disk_label));

    uint8_t mbr[512];
    int r_mbr = virtio_blk_read_sectors(t->dev_idx, 0, mbr, 1);
    bool has_mbr = (r_mbr == 0 && mbr[510] == 0x55 && mbr[511] == 0xAA);

    if (!has_mbr || t->part_idx < 0) {
        /* Auto-initialize blank / unpartitioned VirtIO disk */
        memset(mbr, 0, sizeof(mbr));
        mbr[0] = 0xEB;
        mbr[1] = 0xFE;
        mbr[510] = 0x55;
        mbr[511] = 0xAA;

        /* Partition 1 (slot 0): ESP (0xEF), start=2048, size=32768 (16MB) */
        uint8_t *p1 = mbr + 446;
        p1[0] = 0x00;
        p1[4] = 0xEF;
        p1[8] = 0x00; p1[9] = 0x08; p1[10] = 0x00; p1[11] = 0x00; /* LBA 2048 */
        p1[12] = 0x00; p1[13] = 0x80; p1[14] = 0x00; p1[15] = 0x00; /* 32768 sectors */

        /* Partition 2 (slot 1): BTRON /SYS (0x13), active 0x80, start=34816 */
        uint64_t cap_sec = virtio_blk_get_capacity(t->dev_idx);
        uint32_t sys_sec = 65536;
        if (cap_sec > 34816) {
            uint32_t rem = (uint32_t)(cap_sec - 34816);
            if (rem < sys_sec) sys_sec = rem;
        }
        uint8_t *p2 = mbr + 446 + 16;
        p2[0] = 0x80; /* Active / Bootable */
        p2[4] = 0x13; /* BTRON */
        p2[8] = 0x00; p2[9] = 0x88; p2[10] = 0x00; p2[11] = 0x00; /* LBA 34816 */
        p2[12] = (uint8_t)(sys_sec & 0xFF);
        p2[13] = (uint8_t)((sys_sec >> 8) & 0xFF);
        p2[14] = (uint8_t)((sys_sec >> 16) & 0xFF);
        p2[15] = (uint8_t)((sys_sec >> 24) & 0xFF);

        if (virtio_blk_write_sectors(t->dev_idx, 0, mbr, 1) != 0) {
            safe_strcpy(st->error_msg, "Failed to write partition table to target disk.", sizeof(st->error_msg));
            return false;
        }

        t->part_idx = 1; /* Partition 2 */
        t->start_lba = 34816;
        t->size_bytes = (uint64_t)sys_sec * 512ULL;
        t->is_btron_formatted = true;
        snprintf(t->dev_path, sizeof(t->dev_path), "/dev/virtio%dp2 - %u MB (%s)",
                 t->dev_idx, (unsigned)(t->size_bytes >> 20), st->unique_disk_label);
    } else {
        /* Existing MBR partition table present on disk */
        if (t->part_idx >= 0 && t->part_idx < 4) {
            uint8_t *ent = mbr + 446 + t->part_idx * 16;
            uint32_t cnt = (uint32_t)ent[12] | ((uint32_t)ent[13] << 8) |
                           ((uint32_t)ent[14] << 16) | ((uint32_t)ent[15] << 24);
            if (cnt == 0 && t->size_bytes > 0) {
                uint32_t st_lba = (uint32_t)(t->start_lba ? t->start_lba : 2048);
                uint32_t sc_cnt = (uint32_t)(t->size_bytes / 512ULL);
                ent[8]  = (uint8_t)(st_lba & 0xFF);
                ent[9]  = (uint8_t)((st_lba >> 8) & 0xFF);
                ent[10] = (uint8_t)((st_lba >> 16) & 0xFF);
                ent[11] = (uint8_t)((st_lba >> 24) & 0xFF);
                ent[12] = (uint8_t)(sc_cnt & 0xFF);
                ent[13] = (uint8_t)((sc_cnt >> 8) & 0xFF);
                ent[14] = (uint8_t)((sc_cnt >> 16) & 0xFF);
                ent[15] = (uint8_t)((sc_cnt >> 24) & 0xFF);
            }
            /* Keep earlier installs' flags: mark this partition active BTRON (0x13) */
            ent[0] = 0x80;
            ent[4] = 0x13;
        }
        if (!b_drivesetup_ensure_esp(mbr, virtio_blk_get_capacity(t->dev_idx), t->part_idx)) {
            uart_puts_raw("[INSTALL] Note: all 4 MBR slots occupied by user slices (no separate ESP slot needed for Ski)\r\n");
        }
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        virtio_blk_write_sectors(t->dev_idx, 0, mbr, 1);
    }

    st->mode = INSTALLER_STATE_COPYING;
    st->progress_percent = 10;
    st->current_step = 1;
    st->error_msg[0] = '\0';
    snprintf(st->status_msg, sizeof(st->status_msg),
             "Step 1/4: Initializing destination partition (%s)...", st->unique_disk_label);

    return true;
}

bool b_installer_step_copy(InstallerState *st) {
    if (!st || st->mode != INSTALLER_STATE_COPYING) return false;

    InstallerTarget *t = &st->targets[st->selected_target_idx];

    if (st->current_step == 1) {
        /* Step 1: Validate / format partition */
        st->progress_percent = 30;
        st->current_step = 2;
        safe_strcpy(st->status_msg, "Step 2/4: Copying system Real Bodies and TAD records...", sizeof(st->status_msg));
        return true;
    } else if (st->current_step == 2) {
        /* Step 2: Copy volume blocks */
        BlkDev *vdev = blk_virtio_create(t->dev_idx);
        if (vdev) {
            BlkDev *part = blk_mbr_find_partition_by_slot(vdev, t->part_idx >= 0 ? t->part_idx : 1, 1024);
            if (!part) {
                part = blk_mbr_find_btron_partition_idx(vdev, 0, 1024);
            }
            if (part && st->source_vol) {
                uint8_t blk_buf[1024];
                UW src_blocks = vol_total_blocks(st->source_vol);
                UW copy_blocks = (src_blocks < part->nblocks) ? src_blocks : part->nblocks;
                for (UW b = 0; b < copy_blocks; b++) {
                    if (vol_read_blk(st->source_vol, (BLK)b, blk_buf) == 0) {
                        if (b == 0 && st->unique_disk_label[0]) {
                            /* Overlay unique disk label onto block 0 VolumeHeader */
                            VolumeHeader *hdr = (VolumeHeader *)blk_buf;
                            memset(hdr->vol_name, 0, sizeof(hdr->vol_name));
                            safe_strcpy((char *)hdr->vol_name, st->unique_disk_label, sizeof(hdr->vol_name));
                        }
                        part->write(part, b, blk_buf, 1);
                    }
                }
            } else if (part) {
                vol_format(part, 256, part->nblocks, st->unique_disk_label[0] ? st->unique_disk_label : "SYS");
            }
            if (part) blk_partition_release(part);
        }

        st->progress_percent = 75;
        st->current_step = 3;
        safe_strcpy(st->status_msg, "Step 3/4: Writing bootloader stage (Ski & ESP)...", sizeof(st->status_msg));
        return true;
    } else if (st->current_step == 3) {
        /* Step 3: Install Ski stage / MBR boot flag */
        uint8_t sec0[512];
        if (virtio_blk_read_sectors(t->dev_idx, 0, sec0, 1) == 0) {
            sec0[510] = 0x55;
            sec0[511] = 0xAA;
            if (t->part_idx >= 0 && t->part_idx < 4) {
                sec0[446 + t->part_idx * 16] = 0x80;
                sec0[446 + t->part_idx * 16 + 4] = 0x13;
                virtio_blk_write_sectors(t->dev_idx, 0, sec0, 1);
            }
        }

        st->progress_percent = 95;
        st->current_step = 4;
        safe_strcpy(st->status_msg, "Step 4/4: Finalizing volume header and syncing...", sizeof(st->status_msg));
        return true;
    } else if (st->current_step == 4) {
        /* Step 4: Complete and finalize labels */
        BlkDev *vdev = blk_virtio_create(t->dev_idx);
        if (vdev) {
            BlkDev *part = blk_mbr_find_partition_by_slot(vdev, t->part_idx >= 0 ? t->part_idx : 1, 1024);
            if (!part) {
                part = blk_mbr_find_btron_partition_idx(vdev, 0, 1024);
            }
            if (part && st->unique_disk_label[0]) {
                uint8_t blk0[1024];
                if (part->read(part, 0, blk0, 1) == 0) {
                    VolumeHeader *hdr = (VolumeHeader *)blk0;
                    memset(hdr->vol_name, 0, sizeof(hdr->vol_name));
                    safe_strcpy((char *)hdr->vol_name, st->unique_disk_label, sizeof(hdr->vol_name));
                    part->write(part, 0, blk0, 1);
                }
            }
            if (part) blk_partition_release(part);
        }

        /* Update in-memory target label and DriveSetup partition state */
        if (st->unique_disk_label[0]) {
            safe_strcpy(t->label, st->unique_disk_label, sizeof(t->label));
            t->is_btron_formatted = true;
            t->is_mounted = true;
            uint32_t mb = (uint32_t)(t->size_bytes / (1024 * 1024));
            if (mb == 0) mb = 1;
            snprintf(t->dev_path, sizeof(t->dev_path), "/dev/virtio%dp%d - %u MB (%s)",
                     t->dev_idx, (t->part_idx >= 0 ? t->part_idx + 1 : 2), mb, st->unique_disk_label);

            char want_raw[32];
            snprintf(want_raw, sizeof(want_raw), "/dev/virtio%d", t->dev_idx);
            for (int di = 0; di < g_drivesetup_state.device_count; di++) {
                DriveSetupDevice *dsd = &g_drivesetup_state.devices[di];
                if (strcmp(dsd->raw_path, want_raw) == 0) {
                    for (int pi = 0; pi < dsd->partition_count; pi++) {
                        DriveSetupPartition *dsp = &dsd->partitions[pi];
                        size_t dl = strlen(dsp->dev_path);
                        int slot = pi;
                        if (dl > 2 && dsp->dev_path[dl - 2] == 'p' && dsp->dev_path[dl - 1] >= '1' && dsp->dev_path[dl - 1] <= '4') {
                            slot = dsp->dev_path[dl - 1] - '1';
                        }
                        if (slot == t->part_idx) {
                            safe_strcpy(dsp->label, st->unique_disk_label, sizeof(dsp->label));
                            dsp->type_code = 0x13;
                            dsp->fs_type = FS_BFS_V2;
                            dsp->bootable = true;
                        }
                    }
                }
            }
        }

        st->progress_percent = 100;
        st->mode = INSTALLER_STATE_COMPLETE;
        snprintf(st->status_msg, sizeof(st->status_msg),
                 "Installation Complete! Disk Label: %s. Reboot into B-System.", st->unique_disk_label);
        return true;
    }

    return false;
}

/* ── UI Painting Engine: Haiku Style + Japanese Card Format ─────────── */

void installer_paint(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    InstallerState *st = &g_installer_state;

    int w = wnd->client.right - wnd->client.left;
    int h = wnd->client.bottom - wnd->client.top;

    installer_calc_layout(st, w, h);

    /* Background: Japanese card warm gray face */
    RECT client_r = { 0, 0, (H)w, (H)h };
    fill_rec(dev, &client_r, INST_COL_BG);

    /* ── 1. Top Menu Bar (Native B-System APP_MENU_BAR) ── */
    app_menu_paint_bar(&st->menu_bar, dev);

    /* ── 2. Top Header Card (Japanese Card Brand + Haiku Instruction) ── */
    RECT banner_r = { 12, 32, (H)(w - 12), 122 };
    fill_rec(dev, &banner_r, INST_COL_CARD_WHITE);

    /* Refined Japanese hairline border */
    set_col(dev, INST_COL_CARD_BORDER, INST_COL_CARD_WHITE);
    drw_rec(dev, &banner_r);
    draw_3d_frame(dev, &banner_r, true);

    /* Left: Japanese Vermilion Lacquer Seal Mark (角印: 40x40) */
    RECT seal_r = { 24, 38, 64, 78 };
    fill_rec(dev, &seal_r, INST_COL_RED_SEAL);
    set_col(dev, 0xFFE27A7A, INST_COL_RED_SEAL);
    drw_rec(dev, &seal_r);
    drw_tc_string(dev, 38, 48, "ヨ", COLOR_WHITE, INST_COL_RED_SEAL);

    /* Left: B·SYSTEM Brand Typography */
    drw_tc_string(dev, 76, 44, "B-System", INST_COL_TEXT_BLACK, INST_COL_CARD_WHITE);
    drw_tc_string(dev, 76, 60, "BTRON 3.20 Workstation", INST_COL_TEXT_MUTED, INST_COL_CARD_WHITE);

    /* Right: poetic copy */
    int instr_x = (w >= 540) ? (w / 2 + 10) : 230;
    drw_tc_string(dev, instr_x, 40, "空白の器に、世界を。", INST_COL_RED_SEAL, INST_COL_CARD_WHITE);
    drw_tc_string(dev, instr_x, 65, "Give an empty disk a world.", INST_COL_TEXT_BLACK, INST_COL_CARD_WHITE);
    drw_tc_string(dev, instr_x, 80, "Choose where it will live.", INST_COL_TEXT_MUTED, INST_COL_CARD_WHITE);
    drw_tc_string(dev, instr_x, 95, "Press Begin.", INST_COL_TEXT_MUTED, INST_COL_CARD_WHITE);

    /* ── 3. Main Form Section (Install from / Onto) ── */
    if (st->mode == INSTALLER_STATE_SETUP || st->mode == INSTALLER_STATE_WELCOME) {
        /* Row 1: "Install from:" */
        drw_tc_string(dev, 18, 138, "Install from source:", INST_COL_TEXT_BLACK, INST_COL_BG);

        const char *src_label = "(No source)";
        if (st->selected_source_idx >= 0 && st->selected_source_idx < st->source_count) {
            src_label = st->sources[st->selected_source_idx].desc;
        }
        draw_haiku_combobox(dev, &st->source_combobox_rect, src_label, st->source_dropdown_open);

        /* Row 2: "        Onto:" */
        drw_tc_string(dev, 82, 176, "Onto target:", INST_COL_TEXT_BLACK, INST_COL_BG);

        const char *tgt_label = "Please choose target";
        if (st->selected_target_idx >= 0 && st->selected_target_idx < st->target_count) {
            tgt_label = st->targets[st->selected_target_idx].dev_path;
        }
        draw_haiku_combobox(dev, &st->target_combobox_rect, tgt_label, st->target_dropdown_open);

        /* Row 3: "› Show optional packages" Disclosure */
        const char *disc_arrow = st->show_optional_packages ? "v " : "> ";
        const char *disc_text  = st->show_optional_packages ? "Hide optional packages" : "Show optional packages";
        drw_tc_string(dev, st->disclosure_rect.left, st->disclosure_rect.top, disc_arrow, INST_COL_BLUE_ACCENT, INST_COL_BG);
        drw_tc_string(dev, st->disclosure_rect.left + 14, st->disclosure_rect.top, disc_text, INST_COL_TEXT_BLACK, INST_COL_BG);

        if (st->show_optional_packages) {
            /* Optional Packages Card */
            fill_rec(dev, &st->optional_box_rect, INST_COL_CARD_WHITE);
            set_col(dev, INST_COL_CARD_BORDER, INST_COL_CARD_WHITE);
            drw_rec(dev, &st->optional_box_rect);

            drw_tc_string(dev, st->optional_box_rect.left + 12, st->optional_box_rect.top + 8,
                          "[X] Base System (Editor, Browser, Cabinet)", INST_COL_TEXT_BLACK, INST_COL_CARD_WHITE);
            drw_tc_string(dev, st->optional_box_rect.left + 12, st->optional_box_rect.top + 24,
                          "[X] SDK (Lil Cu 64, MSX, Quake)", INST_COL_TEXT_BLACK, INST_COL_CARD_WHITE);
            drw_tc_string(dev, st->optional_box_rect.left + 12, st->optional_box_rect.top + 40,
                          "[X] TAD Books", INST_COL_TEXT_BLACK, INST_COL_CARD_WHITE);
        }

        /* Bottom Action Buttons */
        draw_bevel_btn(dev, &st->btn_setup_partitions, "Set up targets...", false, true, false);
        draw_bevel_btn(dev, &st->btn_begin, "Begin", false, (st->target_count > 0), false);

    } else if (st->mode == INSTALLER_STATE_COPYING) {
        /* Copying / Progress Screen */
        drw_tc_string(dev, 24, 134, "種をまく。 Planting a world...", INST_COL_BLUE_ACCENT, INST_COL_BG);

        /* Progress Bar */
        RECT pb_frame = { 24, 154, (H)(w - 24), 182 };
        fill_rec(dev, &pb_frame, INST_COL_CARD_WHITE);
        draw_3d_frame(dev, &pb_frame, true);

        int pb_w = pb_frame.right - pb_frame.left - 4;
        int fill_w = (pb_w * st->progress_percent) / 100;
        if (fill_w > 0) {
            RECT pb_fill = { (H)(pb_frame.left + 2), (H)(pb_frame.top + 2),
                             (H)(pb_frame.left + 2 + fill_w), (H)(pb_frame.bottom - 2) };
            fill_rec(dev, &pb_fill, INST_COL_BLUE_ACCENT);
        }

        /* Status Label */
        drw_tc_string(dev, 24, 196, st->status_msg, INST_COL_TEXT_BLACK, INST_COL_BG);

        /* Cancel Button */
        draw_bevel_btn(dev, &st->btn_close, "Cancel", false, true, false);

    } else if (st->mode == INSTALLER_STATE_COMPLETE) {
        /* Complete Screen: Japanese Card Congratulatory Panel */
        RECT comp_box = { 16, 130, (H)(w - 16), (H)(h - 54) };
        fill_rec(dev, &comp_box, INST_COL_CARD_WHITE);
        set_col(dev, INST_COL_CARD_BORDER, INST_COL_CARD_WHITE);
        drw_rec(dev, &comp_box);
        draw_3d_frame(dev, &comp_box, true);

        drw_tc_string(dev, 32, 140, "夜明け。 It lives.", INST_COL_GREEN, INST_COL_CARD_WHITE);
        drw_tc_string(dev, 32, 160, "Your disk now carries a world of its own.",
                      INST_COL_TEXT_BLACK, INST_COL_CARD_WHITE);

        /* Prominent Unique Disk Label Badge for Ski Bootloader identification */
        RECT lbl_box = { 32, 182, (H)(w - 32), 232 };
        fill_rec(dev, &lbl_box, INST_COL_GREEN_BG);
        set_col(dev, INST_COL_GREEN, INST_COL_GREEN_BG);
        drw_rec(dev, &lbl_box);
        drw_tc_string(dev, 44, 189, "Disk Label to remember for Ski Bootloader:",
                      INST_COL_TEXT_MUTED, INST_COL_GREEN_BG);
        drw_tc_string(dev, 44, 209,
                      st->unique_disk_label[0] ? st->unique_disk_label : "BTRON-SYSTEM",
                      INST_COL_RED_SEAL, INST_COL_GREEN_BG);

        drw_tc_string(dev, 32, 240, "Restart, and choose this label from Ski.",
                      INST_COL_TEXT_MUTED, INST_COL_CARD_WHITE);

        /* Action Buttons */
        draw_bevel_btn(dev, &st->btn_close, "Continue Testing", false, true, false);
        draw_bevel_btn(dev, &st->btn_restart, "Restart Computer", false, true, false);
    }

    /* ── Dropdown Overlays (Rendered last so they sit on top of everything) ── */

    /* 1. Source Combobox Dropdown */
    if (st->source_dropdown_open && st->source_count > 0) {
        int row_h = 26;
        int drop_h = st->source_count * row_h + 6;
        st->source_dropdown_rect.left   = st->source_combobox_rect.left;
        st->source_dropdown_rect.top    = st->source_combobox_rect.bottom;
        st->source_dropdown_rect.right  = st->source_combobox_rect.right;
        st->source_dropdown_rect.bottom = (H)(st->source_combobox_rect.bottom + drop_h);

        RECT drop_sh = { (H)(st->source_dropdown_rect.left + 2), (H)(st->source_dropdown_rect.top + 2),
                         (H)(st->source_dropdown_rect.right + 2), (H)(st->source_dropdown_rect.bottom + 2) };
        fill_rec(dev, &drop_sh, 0xFFB0ACA2);
        draw_3d_bevel_box_custom(dev, &st->source_dropdown_rect, INST_COL_CARD_WHITE);

        for (int i = 0; i < st->source_count; i++) {
            RECT row_r = { (H)(st->source_dropdown_rect.left + 3),
                           (H)(st->source_dropdown_rect.top + 3 + i * row_h),
                           (H)(st->source_dropdown_rect.right - 3),
                           (H)(st->source_dropdown_rect.top + 3 + (i + 1) * row_h) };
            bool is_hov = (i == st->source_dropdown_hover);
            COLOR r_bg = is_hov ? INST_COL_SEL_BG : INST_COL_CARD_WHITE;
            COLOR r_fg = is_hov ? COLOR_WHITE : INST_COL_TEXT_BLACK;
            fill_rec(dev, &row_r, r_bg);

            if (i == st->selected_source_idx) {
                drw_tc_string(dev, row_r.left + 6, row_r.top + 6, "*", r_fg, r_bg);
            }
            drw_tc_string(dev, row_r.left + 22, row_r.top + 6, st->sources[i].desc, r_fg, r_bg);
        }
    }

    /* 3. Target Combobox Dropdown */
    if (st->target_dropdown_open && st->target_count > 0) {
        int row_h = (st->target_count > 5) ? 20 : 26;
        int drop_h = st->target_count * row_h + 6;
        int d_top = st->target_combobox_rect.bottom;
        if (d_top + drop_h > h - 4) {
            d_top = (h - 4) - drop_h;
            if (d_top < 22) d_top = 22;
        }
        st->target_dropdown_rect.left   = st->target_combobox_rect.left;
        st->target_dropdown_rect.top    = (H)d_top;
        st->target_dropdown_rect.right  = st->target_combobox_rect.right;
        st->target_dropdown_rect.bottom = (H)(d_top + drop_h);

        RECT drop_sh = { (H)(st->target_dropdown_rect.left + 2), (H)(st->target_dropdown_rect.top + 2),
                         (H)(st->target_dropdown_rect.right + 2), (H)(st->target_dropdown_rect.bottom + 2) };
        fill_rec(dev, &drop_sh, 0xFFB0ACA2);
        draw_3d_bevel_box_custom(dev, &st->target_dropdown_rect, INST_COL_CARD_WHITE);

        int ty_off = (row_h >= 24) ? 6 : 3;
        for (int i = 0; i < st->target_count; i++) {
            RECT row_r = { (H)(st->target_dropdown_rect.left + 3),
                           (H)(st->target_dropdown_rect.top + 3 + i * row_h),
                           (H)(st->target_dropdown_rect.right - 3),
                           (H)(st->target_dropdown_rect.top + 3 + (i + 1) * row_h) };
            bool is_hov = (i == st->target_dropdown_hover);
            COLOR r_bg = is_hov ? INST_COL_SEL_BG : INST_COL_CARD_WHITE;
            COLOR r_fg = is_hov ? COLOR_WHITE : INST_COL_TEXT_BLACK;
            fill_rec(dev, &row_r, r_bg);

            if (i == st->selected_target_idx) {
                drw_tc_string(dev, row_r.left + 6, row_r.top + ty_off, "*", r_fg, r_bg);
            }
            drw_tc_string(dev, row_r.left + 22, row_r.top + ty_off, st->targets[i].dev_path, r_fg, r_bg);
        }
    }

    /* 3. Native Application Menu Bar Dropdown Overlay */
    if (st->menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&st->menu_bar, dev);
    }
}

/* ── Event Dispatcher ────────────────────────────────────────────────── */

static void installer_destroy(WND *wnd) {
    (void)wnd;
    g_installer_wnd = NULL;
    desktop_set_icons_visible(TRUE);
}

void installer_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    InstallerState *st = &g_installer_state;

    if (evt->type == EV_MOUSE_MOVE) {
        int mx = evt->pos.x - wnd->client.left;
        int my = evt->pos.y - wnd->client.top;

        if (st->menu_bar.active_menu >= 0 || my < APP_MENU_BAR_HEIGHT) {
            if (app_menu_handle_mouse_move(&st->menu_bar, (H)mx, (H)my)) {
                inval_wnd(wnd);
                return;
            }
        }

        if (st->source_dropdown_open) {
            if (pt_in_rect(mx, my, &st->source_dropdown_rect)) {
                int row_h = 26;
                int item = (my - (st->source_dropdown_rect.top + 3)) / row_h;
                if (item >= 0 && item < st->source_count && item != st->source_dropdown_hover) {
                    st->source_dropdown_hover = item;
                    inval_wnd(wnd);
                }
            }
        } else if (st->target_dropdown_open) {
            if (pt_in_rect(mx, my, &st->target_dropdown_rect)) {
                int row_h = (st->target_count > 5) ? 20 : 26;
                int item = (my - (st->target_dropdown_rect.top + 3)) / row_h;
                if (item >= 0 && item < st->target_count && item != st->target_dropdown_hover) {
                    st->target_dropdown_hover = item;
                    inval_wnd(wnd);
                }
            }
        }
    } else if (evt->type == EV_KEY_DOWN) {
        int cmd = 0;
        if (app_menu_handle_key(&st->menu_bar, (UW)evt->key, 0, &cmd)) {
            if (cmd == INST_CMD_ABOUT) {
                    app_menu_create_about_dialog("B-System Installer", "B-System インストーラ",
                                                 "BTRON Workstation Installer.",
                                                 "Brought to you by 5HT",
                                                wnd->client.left + 50, wnd->client.top + 50);
            } else if (cmd == INST_CMD_DRIVESETUP) {
                if (&open_drivesetup_window && open_drivesetup_window) open_drivesetup_window();
            } else if (cmd == INST_CMD_TERMINAL) {
                if (&open_gterm_window && open_gterm_window) open_gterm_window();
            } else if (cmd == INST_CMD_RESCAN) {
                if (st->mode == INSTALLER_STATE_COMPLETE) st->mode = INSTALLER_STATE_SETUP;
                b_installer_scan_targets(st);
            } else if (cmd == INST_CMD_QUIT) {
                cls_wnd(wnd);
                g_installer_wnd = NULL;
                desktop_set_icons_visible(TRUE);
                return;
            }
            inval_wnd(wnd);
            return;
        }

        if (st->source_dropdown_open || st->target_dropdown_open) {
            if (evt->key == BTRON_KEY_ESCAPE || evt->key == 27) {
                st->source_dropdown_open = false;
                st->target_dropdown_open = false;
                inval_wnd(wnd);
            }
        }
    } else if (evt->type == EV_BUT_DOWN) {
        int mx = evt->pos.x - wnd->client.left;
        int my = evt->pos.y - wnd->client.top;

        /* 1. Delegate to native APP_MENU_BAR if menu open or clicked in menu bar */
        if (st->menu_bar.active_menu >= 0 || my < APP_MENU_BAR_HEIGHT) {
            int cmd = 0, subitem = 0;
            if (app_menu_handle_mouse_down(&st->menu_bar, (H)mx, (H)my, &cmd, &subitem)) {
                if (cmd == INST_CMD_ABOUT) {
                    app_menu_create_about_dialog("B-System Installer", "B-System インストーラ",
                                                 "BTRON Workstation Installer.",
                                                 "Brought to you by 5HT",
                                                wnd->client.left + 50, wnd->client.top + 50);
                } else if (cmd == INST_CMD_DRIVESETUP) {
                    if (&open_drivesetup_window && open_drivesetup_window) {
                        open_drivesetup_window();
                    }
                } else if (cmd == INST_CMD_TERMINAL) {
                    if (&open_gterm_window && open_gterm_window) {
                        open_gterm_window();
                    }
                } else if (cmd == INST_CMD_RESCAN) {
                    if (st->mode == INSTALLER_STATE_COMPLETE) st->mode = INSTALLER_STATE_SETUP;
                    b_installer_scan_targets(st);
                } else if (cmd == INST_CMD_QUIT) {
                    cls_wnd(wnd);
                    g_installer_wnd = NULL;
                    desktop_set_icons_visible(TRUE);
                    return;
                }
                inval_wnd(wnd);
                return;
            }
            if (my < APP_MENU_BAR_HEIGHT) {
                inval_wnd(wnd);
                return;
            }
        }

        /* If Source Dropdown is open, handle selection */
        if (st->source_dropdown_open) {
            if (pt_in_rect(mx, my, &st->source_dropdown_rect)) {
                int row_h = 26;
                int item = (my - (st->source_dropdown_rect.top + 3)) / row_h;
                if (item >= 0 && item < st->source_count) {
                    st->selected_source_idx = item;
                    st->source_vol = st->sources[item].vol;
                    safe_strcpy(st->source_name, st->sources[item].desc, sizeof(st->source_name));
                }
            }
            st->source_dropdown_open = false;
            inval_wnd(wnd);
            if (!pt_in_rect(mx, my, &st->target_combobox_rect) &&
                !pt_in_rect(mx, my, &st->source_combobox_rect)) {
                return;
            }
        }

        /* If Target Dropdown is open, handle selection */
        if (st->target_dropdown_open) {
            if (pt_in_rect(mx, my, &st->target_dropdown_rect)) {
                int row_h = (st->target_count > 5) ? 20 : 26;
                int item = (my - (st->target_dropdown_rect.top + 3)) / row_h;
                if (item >= 0 && item < st->target_count) {
                    st->selected_target_idx = item;
                }
            }
            st->target_dropdown_open = false;
            inval_wnd(wnd);
            if (!pt_in_rect(mx, my, &st->target_combobox_rect) &&
                !pt_in_rect(mx, my, &st->source_combobox_rect)) {
                return;
            }
        }

        /* Combobox toggles (Clickable in all setup/welcome states) */
        if (pt_in_rect(mx, my, &st->source_combobox_rect)) {
            st->source_dropdown_open = !st->source_dropdown_open;
            st->target_dropdown_open = false;
            st->source_dropdown_hover = st->selected_source_idx;
            inval_wnd(wnd);
            return;
        } else if (pt_in_rect(mx, my, &st->target_combobox_rect)) {
            st->target_dropdown_open = !st->target_dropdown_open;
            st->source_dropdown_open = false;
            st->target_dropdown_hover = st->selected_target_idx;
            inval_wnd(wnd);
            return;
        } else if (pt_in_rect(mx, my, &st->disclosure_rect)) {
            st->show_optional_packages = !st->show_optional_packages;
            inval_wnd(wnd);
            return;
        }

        if (st->mode == INSTALLER_STATE_WELCOME) {
            if (pt_in_rect(mx, my, &st->btn_try_live)) {
                /* User selected Try Live Session -> close window and restore full desktop */
                cls_wnd(wnd);
                g_installer_wnd = NULL;
                desktop_set_icons_visible(TRUE);
                if (&open_vobj_manager_window && open_vobj_manager_window) open_vobj_manager_window();
                if (&open_t_editor_window && open_t_editor_window)         open_t_editor_window();
                if (&open_gterm_window && open_gterm_window)               open_gterm_window();
#if defined(BTRON_UEFI_TARGET)
                if (&open_glgears_window && open_glgears_window)           open_glgears_window();
#endif
                return;
            } else if (pt_in_rect(mx, my, &st->btn_install_mode)) {
                st->mode = INSTALLER_STATE_SETUP;
                b_installer_scan_targets(st);
                inval_wnd(wnd);
                return;
            }
        }

        if (st->mode == INSTALLER_STATE_SETUP || st->mode == INSTALLER_STATE_WELCOME) {
            if (pt_in_rect(mx, my, &st->btn_setup_partitions)) {
                if (&open_drivesetup_window && open_drivesetup_window) {
                    open_drivesetup_window();
                }
            } else if (pt_in_rect(mx, my, &st->btn_begin)) {
                if (b_installer_start_install(st)) {
                    inval_wnd(wnd);
                    while (st->mode == INSTALLER_STATE_COPYING) {
                        b_installer_step_copy(st);
                    }
                    inval_wnd(wnd);
                } else {
                    inval_wnd(wnd);
                }
            } else if (pt_in_rect(mx, my, &st->btn_refresh)) {
                b_installer_scan_targets(st);
                inval_wnd(wnd);
            }
        } else if (st->mode == INSTALLER_STATE_COMPLETE) {
            if (pt_in_rect(mx, my, &st->btn_restart)) {
                btron_core_reboot();
                cls_wnd(wnd);
                g_installer_wnd = NULL;
                return;
            } else if (pt_in_rect(mx, my, &st->btn_close)) {
                cls_wnd(wnd);
                g_installer_wnd = NULL;
                desktop_set_icons_visible(TRUE);
                return;
            }
        }
    }
}

static BOOL installer_menu_open(WND *wnd) {
    (void)wnd;
    return (g_installer_state.menu_bar.active_menu >= 0 ||
            g_installer_state.source_dropdown_open ||
            g_installer_state.target_dropdown_open);
}

WND* open_installer_window(void) {
    if (g_installer_wnd) {
        top_wnd(g_installer_wnd);
        return g_installer_wnd;
    }

    /* Hide desktop icons for clean standalone installer environment */
    desktop_set_icons_visible(FALSE);

    /* Close all other application windows so Installer stands alone on desktop */
    while (get_wnd_list()) {
        cls_wnd(get_wnd_list());
    }

    b_installer_init(&g_installer_state);
    g_installer_state.mode = INSTALLER_STATE_SETUP;

    int win_w = 640;
    int win_h = 480;
    int win_x = (1024 - win_w) / 2;
    int win_y = (768 - win_h) / 2;

    g_installer_wnd = opn_wnd("Installer", win_x, win_y, win_w, win_h,
                              WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (!g_installer_wnd) return NULL;

    g_installer_wnd->paint = installer_paint;
    g_installer_wnd->event_handler = installer_event_handler;
    g_installer_wnd->destroy = installer_destroy;
    g_installer_wnd->menu_open = installer_menu_open;
    top_wnd(g_installer_wnd); /* ensure focus (gold title tab) */

    return g_installer_wnd;
}
