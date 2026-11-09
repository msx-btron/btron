/*
 * B-System BTRON3 — clu.c
 * Terminal shell command dispatcher & application launcher.
 *
 * All POSIX/CLU filesystem commands are modularized into the C files under
 * src/clu and called via their respective clu_cmd_foo entry points declared
 * in cluc.h.  (The literal glob used to be written here; the "star" that
 * followed a slash inside this comment opened a nested comment and -Wcomment
 * said so on every build of this file.)
 *
 * Compile with: -Iinclude
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/libc_shim.h>
#include "clu.h"
#include "cluc.h"
#include <btron/dp.h>   /* COLOR_* constants */
#include <btron/wnd.h>
#include <btron/apps.h>

/* ── POSIX & CLU Filesystem Command Rerouters ────────────────────── */

void clu_cd(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_cd(args, out, ud);
}

void clu_ls(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_ls(args, out, ud);
}

void clu_fs_cmd(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_fs(args, out, ud);
}

void clu_stat(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_stat(args, out, ud);
}

void clu_info(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_info(args, out, ud);
}

void clu_finfo(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_finfo(args, out, ud);
}

void clu_tp(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_tp(args, out, ud);
}

void clu_mkf(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_mkf(args, out, ud);
}

void clu_cp(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_cp(args, out, ud);
}

void clu_ln(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_ln(args, out, ud);
}

void clu_rm(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_rm(args, out, ud);
}

void clu_ren(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_mv(args, out, ud);
}

void clu_empf(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_empf(args, out, ud);
}

void clu_apd(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_apd(args, out, ud);
}

void clu_chmod(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_chmod(args, out, ud);
}

void clu_touch(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_touch(args, out, ud);
}

void clu_chtime(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_chtime(args, out, ud);
}

void clu_df(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_df(args, out, ud);
}

void clu_sync_cmd(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_sync(args, out, ud);
}

void clu_forth(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_forth(args, out, ud);
}

/* ── 3D Gaming & Applications ───────────────────────────────────── */

__attribute__((weak)) WND *open_quake_window(int x, int y, int width, int height)
{
    (void)x; (void)y; (void)width; (void)height;
    return NULL;
}

__attribute__((weak)) WND *open_xmb_window(void) { return NULL; }
__attribute__((weak)) WND *open_glgears_window(void) { return NULL; }
__attribute__((weak)) WND *open_lilcu64_demo_window(void) { return NULL; }
__attribute__((weak)) WND *open_msx_window(void) { return NULL; }
__attribute__((weak)) WND *open_msx_window_with_rom(const char *path)
{
    (void)path;
    return open_msx_window();
}
__attribute__((weak)) int btron_msx_linked(void) { return 0; }
__attribute__((weak)) int btron_quake_linked(void) { return 0; }
__attribute__((weak)) WND *open_mars_window(void) { return NULL; }
__attribute__((weak)) WND *open_vobj_manager_window(void) { return NULL; }
__attribute__((weak)) WND *open_vobj_manager_window_rect(H x, H y, H w, H h)
{
    (void)x; (void)y; (void)w; (void)h;
    return NULL;
}
__attribute__((weak)) WND *open_t_editor_window(void) { return NULL; }
__attribute__((weak)) WND *open_gterm_window(void) { return NULL; }
__attribute__((weak)) WND *open_audio_player_window(void) { return NULL; }
__attribute__((weak)) WND *launch_beos_chat(void) { return NULL; }
__attribute__((weak)) WND *open_control_panel_window(void) { return NULL; }
__attribute__((weak)) WND *open_drivesetup_window(void) { return NULL; }
__attribute__((weak)) WND *open_paint_window(void) { return NULL; }
__attribute__((weak)) void clu_cmd_mars(const char *args, ShellOutputFn out, void *ud)
{
    (void)args; (void)out; (void)ud;
}

void clu_mars(const char *args, ShellOutputFn out, void *ud)
{
    clu_cmd_mars(args, out, ud);
}

void clu_quake(const char *args, ShellOutputFn out, void *ud)
{
    (void)args;
    out("Quake 3D for B-System (OpenGL ES 1.1 / TinyGL & VirtIO-GPU):", COLOR_CYAN, ud);
    out("  Renderer : Dual-Backend (TinyGL Software & VirtIO 3D)", COLOR_LTGRAY, ud);
    out("  Asset    : assets/quake/pak/pak0.pak (18.6 MB, E1M1 Slipgate Complex)", COLOR_LTGRAY, ud);
#if defined(BTRON_PS2_TARGET)
    out("  Window   : 320x240 WND client (PS2 heap); PAK on ISO QUAKE/PAK0.PAK", COLOR_LTGRAY, ud);
#else
    out("  Window   : 480x360 WND client surface (~60 FPS)", COLOR_LTGRAY, ud);
#endif
    out("  Controls : WASD/Arrows = Move, Mouse Drag = Look, Space = Jump", COLOR_YELLOW, ud);

    WND *w = NULL;
#if defined(BTRON_PS2_TARGET)
    const int qw = 320, qh = 240;
#else
    const int qw = 480, qh = 360;
#endif
    if (open_quake_window)
        w = open_quake_window(120, 60, qw, qh);

    if (w) {
        out("Quake 3D window launched on Workbench desktop.", COLOR_GREEN, ud);
    } else if (!btron_quake_linked()) {
        out("quake: not linked (build with PS2_QUAKE=1)", COLOR_RED, ud);
    } else {
        out("quake: window/EGL surface failed (heap or startx first)", COLOR_RED, ud);
    }
}

void clu_msx(const char *args, ShellOutputFn out, void *ud)
{
    const char *rom = args ? args : "";
    while (*rom == ' ' || *rom == '\t') rom++;

    out("B-MSX Game System (MSX Cartridge Player):", COLOR_CYAN, ud);
    if (!btron_msx_linked()) {
        out("  not linked (rebuild with PS2_MSX=1)", COLOR_RED, ud);
        return;
    }

    WND *w = NULL;
    if (rom[0])
        w = open_msx_window_with_rom(rom);
    else
        w = open_msx_window();

    if (w) {
        out("  Window opened on the Workbench desktop", COLOR_GREEN, ud);
    } else {
        out("  Window surface failed (startx first, or heap full)", COLOR_RED, ud);
    }
}

/* ── clu_launch_app ───────────────────────────────────────────────── */

typedef struct {
    const char *verb;
    const char *title;
    WND       *(*open)(void);
} CluApp;

static WND *open_quake_app_wnd(void) {
    return open_quake_window ? open_quake_window(120, 60, 480, 360) : NULL;
}

static const CluApp s_clu_apps[] = {
    { "xmb",        "Cross Media Bar (XMB)",                    open_xmb_window },
    { "gears",      "GLGears (OpenGL ES 1.1 / TinyGL)",         open_glgears_window },
    { "cu",         "Lil Cu 64 Demoscene (Hopf field)",         open_lilcu64_demo_window },
    { "lilcu",      "Lil Cu 64 Demoscene (Hopf field)",         open_lilcu64_demo_window },
    { "lilcu64",    "Lil Cu 64 Demoscene (Hopf field)",         open_lilcu64_demo_window },
    { "msx",        "B-MSX Game System (MSX Cartridge Player)", open_msx_window },
    { "mars",       "MARS 1993 Voxel Landscape (NASA JPL C99)", open_mars_window },
    { "quake",      "Quake 3D (OpenGL ES 1.1 / TinyGL)",        open_quake_app_wnd },
    { "cabinet",    "Real Body & Virtual Object Cabinet",       open_vobj_manager_window },
    { "editor",     "Multi-Line Text & TAD Editor",             open_t_editor_window },
    { "term",       "B-System Terminal Console",                open_gterm_window },
    { "player",     "Audio Player / Cassette Deck",             open_audio_player_window },
    { "chat",       "Interactive Dialogue / Blabber Chat",      launch_beos_chat },
    { "settings",   "Control Panel & System Settings",          open_control_panel_window },
    { "volumes",    "Disk & Volume Partition Setup",            open_drivesetup_window },
    { "paint",      "Paint Drawing Tool",                       open_paint_window },
};

void clu_list_apps(ShellOutputFn out, void *ud)
{
    if (!out) return;
    out("Available B-System Applications:", COLOR_CYAN, ud);
    for (unsigned i = 0; i < sizeof(s_clu_apps) / sizeof(s_clu_apps[0]); i++) {
        const CluApp *a = &s_clu_apps[i];
        char line[160];
        snprintf(line, sizeof(line), "  %-12s - %s", a->verb, a->title);
        out(line, COLOR_LTGRAY, ud);
    }
}

BOOL clu_launch_app(const char *verb, ShellOutputFn out, void *ud)
{
    if (!verb || !verb[0]) return FALSE;

    const char *v = verb;
    while (*v == ' ') v++;
    if (strncmp(v, "open ", 5) == 0) {
        v += 5;
        while (*v == ' ') v++;
    }
    if (strncmp(v, "./", 2) == 0) v += 2;
    if (strncmp(v, "bin/", 4) == 0) v += 4;
    if (strncmp(v, "/SYS/", 5) == 0) v += 5;
    if (strncmp(v, "SYS/", 4) == 0) v += 4;

    for (unsigned i = 0; i < sizeof(s_clu_apps) / sizeof(s_clu_apps[0]); i++) {
        const CluApp *a = &s_clu_apps[i];
        char line[160];

        if (strcmp(v, a->verb) != 0) continue;

        snprintf(line, sizeof(line), "%s:", a->title);
        out(line, COLOR_CYAN, ud);
        if (!a->open) {
            out("  Not linked into this build", COLOR_YELLOW, ud);
            return TRUE;
        }
        WND *w = a->open();
        if (w) out("  Window opened on the Workbench desktop", COLOR_GREEN, ud);
        else   out("  Failed to open window surface", COLOR_RED, ud);
        return TRUE;
    }
    return FALSE;
}

/* ── clu_sc ──────────────────────────────────────────────────────── */

__attribute__((weak)) int sc_main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    return -1;
}

void clu_sc(const char *args, ShellOutputFn out, void *ud)
{
    char path_buf[256];
    if (args && args[0]) {
        const char *p = args;
        while (*p == ' ' || *p == '\t') p++;
        strncpy(path_buf, p, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
    } else {
        strcpy(path_buf, g_cwd_path[0] ? g_cwd_path : "/SYS");
    }

    out("Sokhatsky Commander (sc) — Dual-Pane File Manager", COLOR_CYAN, ud);
    out("  Source   : src/clu/sc/sc.c (NASA JPL Power of Ten, BTRON3 VFS)", COLOR_LTGRAY, ud);
    char msg[320];
    snprintf(msg, sizeof(msg), "  Target   : %s", path_buf);
    out(msg, COLOR_YELLOW, ud);
    out("  Status   : In-window termios apps (sc, tv) inside gterm not available yet.", COLOR_YELLOW, ud);
    out("             Run 'sc' in terminal console for full-screen TUI.", COLOR_LTGRAY, ud);
}

/* ── clu_tv ──────────────────────────────────────────────────────── */

__attribute__((weak)) int tv_run(const char *filepath, int view_only)
{
    (void)filepath; (void)view_only;
    return -1;
}

void clu_tv(const char *args, ShellOutputFn out, void *ud)
{
    int view_only = 0;
    const char *p = args ? args : "";
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "-v", 2) == 0 && (p[2] == ' ' || p[2] == '\0')) {
        view_only = 1;
        p += 2;
        while (*p == ' ' || *p == '\t') p++;
    }
    char file_path[256];
    if (*p == '\0') {
        snprintf(file_path, sizeof(file_path), "%s/new.txt", g_cwd_path[0] ? g_cwd_path : "/SYS");
    } else if (*p == '/') {
        strncpy(file_path, p, sizeof(file_path) - 1);
        file_path[sizeof(file_path) - 1] = '\0';
    } else {
        snprintf(file_path, sizeof(file_path), "%s/%s", g_cwd_path[0] ? g_cwd_path : "/SYS", p);
    }

    out("Terminal Vision (tv) — Multilingual Asian & Kinsoku Editor", COLOR_CYAN, ud);
    out("  Source   : src/clu/tv/tv.c (CJK double-width, Tibetan combining, Kinsoku)", COLOR_LTGRAY, ud);
    char msg[320];
    snprintf(msg, sizeof(msg), "  File     : %s (%s mode)", file_path, view_only ? "View" : "Edit");
    out(msg, COLOR_YELLOW, ud);
    out("  Status   : In-window termios apps (sc, tv) inside gterm not available yet.", COLOR_YELLOW, ud);
    out("             Run 'tv <file>' in terminal console for full-screen TUI.", COLOR_LTGRAY, ud);
}

/* ── clu_be ──────────────────────────────────────────────────────── */

__attribute__((weak)) void be_native_command_info(const char *args, char *out, size_t out_size)
{
    (void)args;
    if (out && out_size > 0) out[0] = '\0';
}

void clu_be(const char *args, ShellOutputFn out, void *ud)
{
    char info_buf[256];
    info_buf[0] = '\0';
    out("Binary Editor (be) — Infosec Hex Editor with DASM", COLOR_CYAN, ud);
    out("  Source   : src/clu/be/native.c (Interactive DASM/Hex Editor)", COLOR_LTGRAY, ud);
    be_native_command_info(args, info_buf, sizeof(info_buf));
    if (info_buf[0]) {
        out(info_buf, COLOR_YELLOW, ud);
    } else {
        out("  Usage    : be [-vhdbao] <filename>", COLOR_LTGRAY, ud);
    }
}
