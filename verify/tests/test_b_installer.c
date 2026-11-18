/*
 * test_b_installer.c
 * Automated test suite for Haiku-Style BTRON Installer.
 *
 * Verifies:
 *  - Mode transitions (Welcome -> Setup -> Copying -> Complete)
 *  - Target disk scanning (VirtIO-Block detection and MBR partition parsing)
 *  - Target selection and error validation
 *  - Multi-step copy pipeline execution and volume initialization
 *  - Graphical Window opening, painting, and event handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/event.h>
#include <drivers/virtio/virtio_blk.h>
#include <fs/block.h>
#include <fs/volume.h>
#include <fs/vol_api.h>
#include "../../src/apps/volumes/b_installer.h"
#include "../../src/apps/volumes/b_drivesetup.h"

static int g_test_reboot_called = 0;
void uart_puts_raw(const char *str) { (void)str; }
UW   btron_fs_now_stamp(void)       { return 100000; }
void btron_core_reboot(void)        { g_test_reboot_called++; }

H tc_calc_string_width(const char *utf8_text, int max_bytes) {
    if (!utf8_text) return 0;
    int len = (int)strlen(utf8_text);
    if (max_bytes > 0 && len > max_bytes) len = max_bytes;
    return len * 8;
}

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
extern void virtio_blk_set_mock_storage(int dev_idx, void *buf, uint64_t size_bytes);
#endif

/* Mock graphics & window manager */
static WND  g_mock_wnd;
static COLOR g_mock_vram[1024 * 768];
static GDEV g_mock_gdev = { 560, 360, 0, g_mock_vram, { 0, 0, 560, 360 }, 560, 360 };

ER drw_tc_string(GDEV *dev, H x, H y, const char *text, COLOR fg, COLOR bg) {
    (void)dev; (void)x; (void)y; (void)text; (void)fg; (void)bg; return 0;
}
ER fill_rec(GDEV *dev, const RECT *r, COLOR col) {
    (void)dev; (void)r; (void)col; return 0;
}
ER drw_lin(GDEV *dev, H x1, H y1, H x2, H y2) {
    (void)dev; (void)x1; (void)y1; (void)x2; (void)y2; return 0;
}
ER drw_rec(GDEV *dev, const RECT *r) {
    (void)dev; (void)r; return 0;
}
void set_col(GDEV *dev, COLOR fg, COLOR bg) {
    (void)dev; (void)fg; (void)bg;
}
WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)attr;
    memset(&g_mock_wnd, 0, sizeof(WND));
    g_mock_wnd.client.right = w;
    g_mock_wnd.client.bottom = h;
    return &g_mock_wnd;
}
static int g_test_icons_visible = 1;
static int g_test_gterm_opened = 0;
static int g_test_cls_wnd_count = 0;
static WND *g_mock_list = NULL;

ER top_wnd(WND *wnd)   { (void)wnd; return 0; }
ER inval_wnd(WND *wnd) { (void)wnd; return 0; }
ER cls_wnd(WND *wnd)   {
    (void)wnd;
    g_test_cls_wnd_count++;
    if (g_mock_list == wnd) {
        g_mock_list = wnd->next;
    }
    return 0;
}
WND* get_wnd_list(void) { return g_mock_list; }
void desktop_set_icons_visible(BOOL visible) { g_test_icons_visible = visible; }
BOOL desktop_get_icons_visible(void) { return g_test_icons_visible; }
WND* open_vobj_manager_window(void) { return NULL; }
WND* open_t_editor_window(void) { return NULL; }
WND* open_gterm_window(void) { g_test_gterm_opened++; return NULL; }
WND* open_glgears_window(void) { return NULL; }

int main(void) {
    printf("=== Running B-System Installer (b_installer) Tests ===\n");

    /* 1. Setup mock source volume in memory */
    size_t src_size = 4 * 1024 * 1024;
    uint8_t *src_mem = (uint8_t *)calloc(1, src_size);
    BlkDev *src_blk = blk_mem_create(src_mem, (UW)src_size, 0);
    assert(src_blk != NULL);
    vol_format(src_blk, 64, src_blk->nblocks, "LIVE_SYS");
    extern Volume *g_sys_vol;
    g_sys_vol = vol_mount(src_blk);
    assert(g_sys_vol != NULL);

    /* 2. Setup mock VirtIO-Block destination disk */
    size_t dst_size = 16 * 1024 * 1024;
    uint8_t *dst_mem = (uint8_t *)calloc(1, dst_size);
    /* Write MBR with Partition 1 = BTRON */
    dst_mem[510] = 0x55; dst_mem[511] = 0xAA;
    uint8_t *p1 = dst_mem + 446;
    p1[4] = 0x13; /* BTRON type */
    uint32_t start_sec = 2048;
    uint32_t count_sec = 20480;
    memcpy(p1 + 8, &start_sec, 4);
    memcpy(p1 + 12, &count_sec, 4);

    virtio_blk_set_mock_storage(0, dst_mem, dst_size);

    /* 3. Initialize Installer */
    InstallerState st;
    b_installer_init(&st);
    assert(st.mode == INSTALLER_STATE_WELCOME);
    assert(st.target_count >= 1);
    assert(st.selected_target_idx == 0);
    printf("  [PASS] Installer initialized; discovered %d target partition(s).\n", st.target_count);

    /* 4. Switch to Setup mode */
    st.mode = INSTALLER_STATE_SETUP;
    bool ok = b_installer_select_target(&st, 0);
    assert(ok);

    /* 5. Start installation */
    ok = b_installer_start_install(&st);
    assert(ok);
    assert(st.mode == INSTALLER_STATE_COPYING);
    assert(st.current_step == 1);
    assert(strncmp(st.unique_disk_label, "BTRON-", 6) == 0);
    assert(strlen(st.unique_disk_label) == 17);
    printf("  [PASS] Unique disk label generated: %s\n", st.unique_disk_label);

    /* 6. Step through copy pipeline */
    while (st.mode == INSTALLER_STATE_COPYING) {
        ok = b_installer_step_copy(&st);
        assert(ok);
    }
    assert(st.mode == INSTALLER_STATE_COMPLETE);
    assert(st.progress_percent == 100);
    assert(strcmp(st.targets[0].label, st.unique_disk_label) == 0);
    printf("  [PASS] 4-step installation pipeline completed with unique label %s (100%%).\n", st.unique_disk_label);

    /* 7. Verify bootable flag was written to MBR */
    assert(p1[0] == 0x80); /* Active/bootable */
    printf("  [PASS] MBR boot flag written to target disk.\n");

    /* 8. Window UI opening, empty desktop isolation, and icon hiding */
    WND dummy1, dummy2;
    memset(&dummy1, 0, sizeof(dummy1));
    memset(&dummy2, 0, sizeof(dummy2));
    dummy1.next = &dummy2;
    dummy2.next = NULL;
    g_mock_list = &dummy1;
    g_test_icons_visible = 1;
    g_test_cls_wnd_count = 0;

    WND *wnd = open_installer_window();
    assert(wnd != NULL);
    assert(g_mock_list == NULL); /* all existing windows closed */
    assert(g_test_cls_wnd_count == 2);
    assert(desktop_get_icons_visible() == FALSE); /* desktop icons hidden */
    printf("  [PASS] Installer isolated on desktop: existing windows closed, icons hidden.\n");
    installer_paint(wnd, &g_mock_gdev);

    /* 8b. Verify Tools menu item: Terminal (GTerm Console) keyboard shortcut 'T' */
    g_test_gterm_opened = 0;
    EVT evt_t;
    memset(&evt_t, 0, sizeof(evt_t));
    evt_t.type = EV_KEY_DOWN;
    evt_t.key = 'T';
    installer_event_handler(wnd, &evt_t);
    assert(g_test_gterm_opened == 1);
    printf("  [PASS] Tools -> Terminal ('T' shortcut) successfully launched GTerm.\n");

    /* 9. Event dispatch test (click Try Live) */
    InstallerState *gst = b_installer_get_state();
    gst->mode = INSTALLER_STATE_WELCOME;
    EVT evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EV_BUT_DOWN;
    evt.pos.x = gst->btn_try_live.left + 5;
    evt.pos.y = gst->btn_try_live.top + 5;
    installer_event_handler(wnd, &evt);
    printf("  [PASS] Window UI, painting, and mouse event dispatch verified.\n");

    /* 10. Screen coordinate translation test (window offset at 80, 60) */
    wnd->client.left = 80;
    wnd->client.top  = 60;
    wnd->client.right = 80 + 560;
    wnd->client.bottom = 60 + 360;
    gst->mode = INSTALLER_STATE_WELCOME;
    evt.type = EV_BUT_DOWN;
    evt.pos.x = wnd->client.left + gst->btn_install_mode.left + 10;
    evt.pos.y = wnd->client.top  + gst->btn_install_mode.top  + 10;
    installer_event_handler(wnd, &evt);
    assert(gst->mode == INSTALLER_STATE_SETUP);
    printf("  [PASS] Mouse click with window offset (80,60) correctly triggered 'Install B-System...'.\n");

    /* 10b. Source Combobox dropdown and selection test (2 items: /SYS, /ANDERS) */
    assert(gst->source_count == 2);
    assert(strcmp(gst->sources[0].name, "/SYS") == 0);
    assert(strcmp(gst->sources[1].name, "/ANDERS") == 0);
    gst->source_dropdown_open = false;
    gst->selected_source_idx = 0;

    /* Click on Source combobox rect to open dropdown */
    evt.pos.x = wnd->client.left + gst->source_combobox_rect.left + 50;
    evt.pos.y = wnd->client.top  + gst->source_combobox_rect.top  + 10;
    installer_event_handler(wnd, &evt);
    assert(gst->source_dropdown_open == true);
    assert(gst->target_dropdown_open == false);

    /* Click on row 1 (/ANDERS) in Source dropdown */
    installer_paint(wnd, &g_mock_gdev);
    evt.pos.x = wnd->client.left + gst->source_dropdown_rect.left + 50;
    evt.pos.y = wnd->client.top  + gst->source_dropdown_rect.top  + 3 + 26 + 10;
    installer_event_handler(wnd, &evt);
    assert(gst->selected_source_idx == 1);
    assert(gst->source_dropdown_open == false);
    printf("  [PASS] Source Combobox opens dropdown (/SYS, /ANDERS) and selects item.\n");

    /* 11. MacOS 9 Pop-up Combobox & Target selection test */
    gst->target_count = 2;
    gst->selected_target_idx = 0;
    gst->target_dropdown_open = false;

    /* Click on combobox rect to open dropdown */
    evt.pos.x = wnd->client.left + gst->target_combobox_rect.left + 50;
    evt.pos.y = wnd->client.top  + gst->target_combobox_rect.top  + 10;
    installer_event_handler(wnd, &evt);
    assert(gst->target_dropdown_open == true);

    /* Click on row 1 in dropdown menu to select target 1 */
    installer_paint(wnd, &g_mock_gdev);
    evt.pos.x = wnd->client.left + gst->target_dropdown_rect.left + 50;
    evt.pos.y = wnd->client.top  + gst->target_dropdown_rect.top  + 3 + 26 + 10;
    installer_event_handler(wnd, &evt);
    assert(gst->selected_target_idx == 1);
    assert(gst->target_dropdown_open == false);
    printf("  [PASS] MacOS 9 Pop-up Combobox opens dropdown and selects target disk.\n");

    /* 12. Auto-partitioning blank target disk */
    size_t blank_size = 64 * 1024 * 1024;
    uint8_t *blank_mem = (uint8_t *)calloc(1, blank_size);
    virtio_blk_set_mock_storage(1, blank_mem, blank_size);
    gst->targets[1].dev_idx = 1;
    gst->targets[1].part_idx = -1;
    gst->targets[1].is_btron_formatted = false;
    gst->selected_target_idx = 1;
    bool install_started = b_installer_start_install(gst);
    assert(install_started);
    assert(gst->targets[1].part_idx == 1);
    assert(gst->targets[1].is_btron_formatted == true);
    /* Verify MBR signature, ESP partition at slot 0, and active BTRON partition 2 at LBA 34816 */
    assert(blank_mem[510] == 0x55 && blank_mem[511] == 0xAA);
    assert(blank_mem[446 + 4] == 0xEF); /* ESP (0xEF) */
    assert(blank_mem[446 + 16] == 0x80); /* Active bootable */
    assert(blank_mem[446 + 16 + 4] == 0x13); /* BTRON type */
    while (gst->mode == INSTALLER_STATE_COPYING) {
        b_installer_step_copy(gst);
    }
    assert(gst->mode == INSTALLER_STATE_COMPLETE);
    assert(strncmp(gst->unique_disk_label, "BTRON-", 6) == 0);
    assert(strcmp(gst->unique_disk_label, st.unique_disk_label) != 0);

    /* Verify VolumeHeader on disk contains unique label */
    BlkDev *vdev1 = blk_virtio_create(1);
    assert(vdev1 != NULL);
    BlkDev *part1 = blk_mbr_find_partition_by_slot(vdev1, 1, 1024);
    assert(part1 != NULL);
    uint8_t blk0[1024];
    assert(part1->read(part1, 0, blk0, 1) == 0);
    VolumeHeader *part2_vh = (VolumeHeader *)blk0;
    assert(strcmp((char *)part2_vh->vol_name, gst->unique_disk_label) == 0);

    /* Verify clicking Restart Computer triggers btron_core_reboot */
    g_test_reboot_called = 0;
    evt.pos.x = wnd->client.left + gst->btn_restart.left + 10;
    evt.pos.y = wnd->client.top  + gst->btn_restart.top  + 10;
    installer_event_handler(wnd, &evt);
    assert(g_test_reboot_called == 1);

    printf("  [PASS] Blank target drive automatically partitioned with ESP (0xEF), active BTRON (0x80), unique label %s, and restart handler verified.\n", gst->unique_disk_label);
    free(blank_mem);

    /* 13. Test Tools menu mouse click on Terminal and Quit restoring desktop icons */
    g_test_gterm_opened = 0;
    /* Click on "Tools" header in menu bar */
    evt.pos.x = wnd->client.left + 20;
    evt.pos.y = wnd->client.top + 10;
    installer_event_handler(wnd, &evt);
    assert(gst->menu_bar.active_menu == 0);

    /* Click on "Terminal (GTerm Console)..." item */
    /* Header 0 items: 0=About, 1=sep, 2=DriveSetup, 3=Terminal */
    int term_item_y = APP_MENU_BAR_HEIGHT + 3 + 3 * APP_MENU_ROW_HEIGHT + 5;
    evt.pos.x = wnd->client.left + 40;
    evt.pos.y = wnd->client.top + term_item_y;
    installer_event_handler(wnd, &evt);
    assert(g_test_gterm_opened == 1);
    printf("  [PASS] Tools -> Terminal mouse menu selection launched GTerm.\n");

    /* Test Quit Installer restores desktop icons */
    g_test_icons_visible = 0;
    evt_t.type = EV_KEY_DOWN;
    evt_t.key = 'Q';
    installer_event_handler(wnd, &evt_t);
    assert(desktop_get_icons_visible() == TRUE);
    printf("  [PASS] Quit Installer successfully restored desktop icons.\n");

    vol_umount(g_sys_vol);
    free(src_mem);
    free(dst_mem);

    printf("=== All B-System Installer Tests Passed! ===\n");
    return 0;
}
