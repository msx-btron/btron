/*
 * B-System (BTRON 3.20) Settings Applet: network (Network / 通信網)
 * Conforming to ./b-system/settings/Network.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

typedef struct {
    WND *wnd;
    BOOL checks[9];
    BOOL is_dirty;
} AppletState_network;

static AppletState_network g_state_network;

static void on_network_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 5; i++) g_state_network.checks[i] = TRUE;
    g_state_network.is_dirty = TRUE;
}

static void on_network_apply(WND *wnd) {
    (void)wnd;
    g_state_network.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_network_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable Primary Interface: eth0 (VirtIO-Net / Realtek)", &g_state_network.checks[0], NULL, &g_state_network.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Automatic DHCP Configuration (IPv4 / IPv6)", &g_state_network.checks[1], NULL, &g_state_network.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Primary DNS Resolution: 1.1.1.1, 8.8.8.8", &g_state_network.checks[2], NULL, &g_state_network.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable XMPP Instant Messaging Client Protocol", &g_state_network.checks[3], NULL, &g_state_network.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Non-blocking Sockets Subsystem (T-Kernel TCP/IP)", &g_state_network.checks[4], NULL, &g_state_network.is_dirty, NULL }
};

static SETTINGS_SECTION s_network_sections[] = {
    { " [1. Network Interface Configuration] ", 1, 0, 3, 56, 94, {0}, 0, 0, 0 },
    { " [2. Messaging & Remote Services] ",      1, 3, 2, 164, 72, {0}, 0, 0, 0 }
};

static void paint_network_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "network", "Network", "通信網", "Interfaces, DHCP, DNS & VirtIO-Net",
                         s_network_sections, 2, s_network_controls, 5, g_state_network.is_dirty);
}

static void handle_network_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd, s_network_sections, 2, s_network_controls, 5, evt,
                                   on_network_default, on_network_apply);
}

WND* open_network_settings_window(void) {
    memset(&g_state_network, 0, sizeof(AppletState_network));
    for (int i = 0; i < 5; i++) g_state_network.checks[i] = TRUE;

    GDEV *scr = wnd_mgr_get_screen();
    H scr_w = scr ? scr->width : 800;
    H scr_h = scr ? scr->height : 600;
#if defined(BTRON_PS2_TARGET)
    H win_w = 480;
    H win_h = 340;
#else
    H win_w = (scr_w <= 800) ? 500 : 640;
    H win_h = (scr_h <= 600) ? 350 : 430;
#endif
    H win_x = (scr_w - win_w) / 2;
    H win_y = (scr_h - win_h) / 2;
    if (win_x < 10) win_x = 10;
    if (win_y < 30) win_y = 30;

    WND *wnd = opn_wnd("通信ネットワーク設定 (Network Settings)",
                       win_x, win_y, win_w, win_h,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd || !wnd->dev) {
        if (wnd) cls_wnd(wnd);
        return NULL;
    }
    g_state_network.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_network;
    wnd->paint = paint_network_settings;
    wnd->event_handler = handle_network_event;
    inval_wnd(wnd);
    return wnd;
}
