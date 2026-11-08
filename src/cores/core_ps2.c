/*
 * core_ps2.c — B-System BTRON3 3.20 RTOS Kernel for Sony PlayStation 2
 *
 *   • Target: Emotion Engine (R5900 MIPS-III LE), 32 MB RDRAM
 *   • Display: Graphics Synthesizer 800x600 CT32 via GIF DMA
 *   • Compositor: B-System Desktop/Workbench (shared with all cores)
 *   • Input: 1 ms real-time plane (USB HID mouse/keyboard, DualShock 2)
 *   • Shell: shared shell_execute_cmd (same as core_arm64.c)
 *
 * All hardware lives in src/drivers/playstation2/; this file is the kernel
 * glue only.  Probes and benchmarks are excluded from release builds.
 *
 * Specification-derived cleanroom port; no vendor SDK code is imported.
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <btron/async_rt.h>
#include <btron/types.h>
#include <btron/error.h>
#include <btron/itron.h>
#include <btron/dp.h>
#include <btron/dp_accel.h>
#include <btron/dp_cal.h>
#include <btron/fast_blit.h>
#include <btron/fused_row.h>
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/workbench.h>
#include <btron/global_menu.h>
#include <btron/tracker.h>
#include <btron/vobj.h>
#include <btron/tip.h>
#include <btron/event.h>
#include <btron/apps.h>
#include <btron/settings.h>
#include <fs/vol_api.h>
#include <libstr.h>

extern void orchestra_ui_tick(void);
extern void cassette_service(void);

#include "ps2_gs.h"
#include "ps2_gif_ring.h"
#include "ps2_paint_worker.h"
#include "ps2_gs_tex.h"
#include "ps2_gs_env.h"
#include "ps2_gs_vram.h"
#include "ps2_gs_poster.h"
#include "ps2_gs_pack.h"
#include "ps2_gs_read.h"
#include "ps2_sio.h"
#include "ps2_console.h"
#include "ps2_pad.h"
#include "ps2_fp.h"
#include "ps2_usb.h"
#include "ps2_kbd.h"
#include "ps2_heap.h"
#include "ps2_boot.h"
#include "ps2_gs_present.h"
#include "ps2_iopram.h"
#include "ps2_irq.h"

#include "core_ps2_probe.h"

#include "../desktop/graphics/eegs/eegs_present.h"
#include "../desktop/graphics/eegs/backend_eegs.h"
#include "../desktop/graphics/gles/gl_backend_choice.h"
#include "../apps/clu/vfs.h"
#include <btron/tronmsx.h>

/* libc_shim.h (via vfs.h) renames malloc & co.; ps2_heap.c defines them. */
#undef malloc
#undef calloc
#undef free

extern void ps2_halt(void);
extern uint32_t ps2_count_read(void);

extern GDEV* init_baremetal_desktop(uint32_t *fb, uint32_t w, uint32_t h);
extern void  redraw_baremetal_desktop(GDEV *screen, H w, H h);
extern void  set_baremetal_mouse_pos(H x, H y);
extern void  get_baremetal_mouse_pos(H *x, H *y);
extern void  draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H w, H h);
extern int   g_cursor_in_backbuffer;

/* ── State ───────────────────────────────────────────────────────── */

static COLOR s_desktop_backbuffer[PS2_SCREEN_WIDTH * PS2_SCREEN_HEIGHT] __attribute__((aligned(128)));

static int      s_mouse_x = 400, s_mouse_y = 300;   /* hardware pointer */
static int      s_shown_x = 400, s_shown_y = 300;   /* last presented position */
static int      s_gui_active;
static int      s_timebase_ok;
static uint8_t  s_prev_mouse_btn;

void launch_ps2_desktop_session(void);
static void ps2_cursor_input_task(GDEV *screen, uint32_t now_us);
static void ps2_shell_exec(const char *cmd);
static ER   ps2_inject_key(UW keycode, int down);

static void ps2_set_gui(int on) { s_gui_active = on; ps2_console_set_gui_active(on); }
static uint32_t ps2_us_since(uint32_t start) { return (uint32_t)(ps2_count_read() - start) / btron_ee_ticks_per_us(); }

/* ── Real-time planes (ASYNC Tier-1) ─────────────────────────────── */

static const btron_plane_cfg_t s_ps2_planes = {
    .input_period_us     = 1000u,
    .ui_period_us        = 16667u,
    .panel_refresh_us    = 200000u,
    .full_convergence_us = 2000000u,
    .input_slices_max    = 16u
};

typedef struct { uint32_t runs, us_max, over; } ps2_plane_run_t;
static ps2_plane_run_t s_plane_input, s_plane_ui;
static uint32_t s_plane_ui_last, s_plane_ui_late;

static void ps2_plane_charge(ps2_plane_run_t *c, uint32_t t0, uint32_t budget_us)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    if (!s_timebase_ok || tpu == 0u) return;
    uint32_t us = (uint32_t)(ps2_count_read() - t0) / tpu;
    c->runs++;
    if (us > c->us_max) c->us_max = us;
    if (us > budget_us) c->over++;
}

static void ps2_plane_ui_deadline(void)
{
    const uint32_t tpu = btron_ee_ticks_per_us();
    const uint32_t now = ps2_count_read();
    if (!s_timebase_ok || tpu == 0u) return;
    if (s_plane_ui_last && (uint32_t)(now - s_plane_ui_last) > btron_planes()->ui_period_us * tpu)
        s_plane_ui_late++;
    s_plane_ui_last = now;
}

uint32_t btron_render_perf_us(void)
{
    static uint32_t s_ticks_hi, s_ticks_last;
    const uint32_t tpu = btron_ee_ticks_per_us();
    const uint32_t now = ps2_count_read();
    if (now < s_ticks_last) s_ticks_hi++;
    s_ticks_last = now;
    return s_timebase_ok ? s_ticks_hi * (0xFFFFFFFFu / tpu) + now / tpu : 0u;
}

/* ── Input: 1:1 hardware pointer, no curves ──────────────────────── */

static void ps2_move_mouse(int dx, int dy)
{
    int32_t nx, ny;
    EVT ev;
    if (dx == 0 && dy == 0) return;
    nx = (int32_t)s_mouse_x + dx;
    ny = (int32_t)s_mouse_y + dy;
    if (nx < 0) nx = 0; else if (nx >= PS2_SCREEN_WIDTH)  nx = PS2_SCREEN_WIDTH - 1;
    if (ny < 0) ny = 0; else if (ny >= PS2_SCREEN_HEIGHT) ny = PS2_SCREEN_HEIGHT - 1;
    s_mouse_x = (int)nx;
    s_mouse_y = (int)ny;
    set_baremetal_mouse_pos((H)s_mouse_x, (H)s_mouse_y);

    tkl_memset(&ev, 0, sizeof(ev));
    ev.type   = EV_MOUSE_MOVE;
    ev.pos.x  = (H)s_mouse_x;
    ev.pos.y  = (H)s_mouse_y;
    ev.button = (s_prev_mouse_btn & 1u) ? 1 : (s_prev_mouse_btn & 2u) ? 2 : (s_prev_mouse_btn & 4u) ? 3 : 0;
    ev.data   = (VW)(((uint32_t)(uint16_t)(int16_t)dx) | ((uint32_t)(uint16_t)(int16_t)dy << 16));
    snd_evt(&ev);
}

static void ps2_click_mouse(int button, int down)
{
    const uint8_t bit = (uint8_t)(1u << (button - 1));
    EVT ev;
    if (down) s_prev_mouse_btn |= bit; else s_prev_mouse_btn &= (uint8_t)~bit;
    tkl_memset(&ev, 0, sizeof(ev));
    ev.type   = down ? EV_BUT_DOWN : EV_BUT_UP;
    ev.pos.x  = (H)s_mouse_x;
    ev.pos.y  = (H)s_mouse_y;
    ev.button = button;
    snd_evt(&ev);
}

static ER ps2_inject_key(UW keycode, int down)
{
    EVT ev;
    tkl_memset(&ev, 0, sizeof(ev));
    ev.type  = down ? EV_KEY_DOWN : EV_KEY_UP;
    ev.pos.x = (H)s_mouse_x;
    ev.pos.y = (H)s_mouse_y;
    ev.key   = keycode;
    return snd_evt(&ev);
}

/* Probe builds override this; release builds report it as unavailable. */
__attribute__((weak)) void ps2_shell_gsstat(void) {
    ps2_kprintf("[GS] gsstat is available in probe builds only.\n");
}

static void ps2_key_cb(uint32_t key, int down) { (void)ps2_inject_key((UW)key, down); }
static void ps2_tap_key(UW key) { ps2_inject_key(key, 1); ps2_inject_key(key, 0); }

/* ps2_pad_on_move - driver hook: left-stick delta. */
void ps2_pad_on_move(int dx, int dy) { ps2_move_mouse(dx, dy); }

/* ps2_pad_on_button - driver hook: DualShock button edges. */
void ps2_pad_on_button(uint16_t pressed, uint16_t released)
{
    /* While a menu is open the D-pad navigates by selection, not by pointer. */
    const int menu_nav = tracker_is_menu_open() || global_menu_is_open();

    if (menu_nav) {
        if (pressed & PAD_CROSS) ps2_tap_key(BTRON_KEY_RETURN);
    } else {
        if (pressed  & PAD_CROSS) ps2_click_mouse(1, 1);
        if (released & PAD_CROSS) ps2_click_mouse(1, 0);
    }
    if (pressed  & PAD_SQUARE) ps2_click_mouse(2, 1);
    if (released & PAD_SQUARE) ps2_click_mouse(2, 0);
    if (pressed  & PAD_TRIANGLE) tip_toggle_mode();
    if (pressed  & PAD_CIRCLE)   ps2_tap_key(BTRON_KEY_ESCAPE);
    if (pressed  & PAD_START) {
        if (s_gui_active) tracker_toggle_menu(); else launch_ps2_desktop_session();
    }
    if (pressed & (PAD_SELECT | PAD_L1 | PAD_R1)) wnd_cycle_focus();

    if (pressed & PAD_UP)    { if (!menu_nav) ps2_move_mouse(0, -16); ps2_tap_key(BTRON_KEY_UP); }
    if (pressed & PAD_DOWN)  { if (!menu_nav) ps2_move_mouse(0,  16); ps2_tap_key(BTRON_KEY_DOWN); }
    if (pressed & PAD_LEFT)  { if (!menu_nav) ps2_move_mouse(-16, 0); ps2_tap_key(BTRON_KEY_LEFT); }
    if (pressed & PAD_RIGHT) { if (!menu_nav) ps2_move_mouse( 16, 0); ps2_tap_key(BTRON_KEY_RIGHT); }
}

#define PS2_MOD_CTRL 0x11u

/* ps2_usb_on_key - driver hook: decoded HID key edge. */
void ps2_usb_on_key(uint32_t key, int down, uint8_t mod)
{
    const int ctrl = (mod & PS2_MOD_CTRL) != 0u;

    if (s_gui_active && down && ctrl) {
        if (key == 'q') { ps2_set_gui(0); return; }
        if (key == 'p') { ps2_page_double_apply(gs_disp_is_double() ? 0u : 1u); return; }
#if BTRON_PS2_PAINT_WORKER
        if (key == 'o') { ps2_pw_apply(s_pw_on ? 0u : 1u); return; }
#endif
        if (key == '[' || key == ']' || key == '\\') return;
    }
    if (s_gui_active) {
        ps2_inject_key((UW)key, down);
        if (down) ps2_kbd_arm(key, mod); else ps2_kbd_disarm(key);
        return;
    }
    if (down && key < 0x100u) { ps2_console_shell_char((int)key); ps2_kbd_arm(key, mod); }
    else if (!down)           ps2_kbd_disarm(key);
}

/* ps2_usb_on_mouse - driver hook: HID mouse report, buttons by edge, motion 1:1. */
void ps2_usb_on_mouse(int dx, int dy, uint8_t buttons)
{
    for (int b = 0; b < 3; b++) {
        const uint8_t bit = (uint8_t)(1u << b);
        if ((buttons & bit) && !(s_prev_mouse_btn & bit)) ps2_click_mouse(b + 1, 1);
        if (!(buttons & bit) && (s_prev_mouse_btn & bit)) ps2_click_mouse(b + 1, 0);
    }
    ps2_move_mouse(dx, dy);
}

/* ── Shell: platform commands, then the shared dispatcher ────────── */

static void ps2_shell_out(const char *line, COLOR col, void *ud)
{
    (void)col; (void)ud;
    ps2_kprintf("%s\n", line);
}

static void ps2_gui_launcher_callback(void *ctx)
{
    (void)ctx;
    if (!s_gui_active) launch_ps2_desktop_session();
}

/* ps2_shell_exec - exit/clear/usb/page handled here, everything else shared. */
static void ps2_shell_exec(const char *cmd)
{
    btron_register_gui_launcher(ps2_gui_launcher_callback, NULL);

    if (!tkl_strcmp(cmd, "exit") || !tkl_strcmp(cmd, "console") || !tkl_strcmp(cmd, "quit")) {
        if (s_gui_active) { ps2_set_gui(0); return; }
        ps2_kprintf("[PS2] Already at console.\n");
    } else if (!tkl_strcmp(cmd, "clear")) {
        if (!s_gui_active) { ps2_gs_text_clear(); ps2_gs_text_flush(); }
    } else if (!tkl_strcmp(cmd, "usb")) {
        (void)ps2_usb_log_host();
    } else if (!tkl_strcmp(cmd, "heapstat")) {
        ps2_heap_holders("heapstat");
    } else if (!tkl_strncmp(cmd, "page", 4)) {
        const char *p = cmd + 4;
        while (*p == ' ') p++;
        if (!tkl_strcmp(p, "double"))      ps2_page_double_apply(1u);
        else if (!tkl_strcmp(p, "single")) ps2_page_double_apply(0u);
        ps2_kprintf("[PAGE] %s\n", gs_disp_is_double() ? "double" : "single");
    } else {
        shell_execute_cmd(cmd, ps2_shell_out, NULL, NULL);
    }
    ps2_kprintf(s_gui_active ? "btron-ps2> " : "btron-ps2# ");
}

/* ── Platform services required by the shared shell ──────────────── */

void btron_core_banner(void)
{
    ps2_kprintf("[CORE] B-System / BTRON3 3.20  Sony PlayStation 2  [R5900, 32 MB]\n");
    ps2_kprintf("[CORE] Type 'startx' for the 800x600 desktop, 'help' for commands.\n");
}

void btron_core_mem_log(void)
{
    ps2_kprintf("[MEM] heap %u MB, %u used, %u kB peak, largest hole %u kB\n",
                (unsigned)(PS2_HEAP_SIZE / (1024u * 1024u)),
                (unsigned)ps2_heap_used_bytes(),
                (unsigned)(ps2_heap_peak_bytes() / 1024u),
                (unsigned)(ps2_heap_largest_hole() / 1024u));
}

extern unsigned char ps2_sys_vol_image[];
extern unsigned char ps2_sys_vol_image_end[];

/* btron_core_hfds_log - mount the /SYS volume carried in .data. */
void btron_core_hfds_log(void)
{
    const long bytes = ps2_sys_vol_image_end - ps2_sys_vol_image;
    BlkDev *dev;

    if (!g_sys_vol) {
        dev = blk_mem_create(ps2_sys_vol_image, (UW)bytes, 0);
        if (!dev) { ps2_kprintf("[FS] /SYS not mounted: no block device\n"); return; }
        g_sys_vol = vol_mount(dev);
        if (!g_sys_vol) {
            blk_destroy(dev);
            ps2_kprintf("[FS] /SYS not mounted: bad volume header\n");
            return;
        }
    }
    ps2_kprintf("[FS] /SYS \"%s\" %u kB, %u blocks used\n", vol_name(g_sys_vol),
                (unsigned)(vol_total_blocks(g_sys_vol) * vol_block_size(g_sys_vol) / 1024u),
                (unsigned)(vol_total_blocks(g_sys_vol) - vol_free_blocks(g_sys_vol)));
}

void btron_core_init(void) { ps2_kprintf("[CORE] uITRON 3.0 / BTRON 3.20 (ps2)\n"); }

void btron_core_print_ver(ShellOutputFn out_fn, void *ud, const char *arg)
{
    if (!out_fn) return;
    if (arg && !tkl_strcmp(arg, "-a")) {
        out_fn("BTRON3 btron-ps2 3.20 Emotion Engine MIPS R5900", COLOR_CYAN, ud);
    } else if (arg && (!tkl_strcmp(arg, "-r") || !tkl_strcmp(arg, "-v"))) {
        out_fn("3.20.0-ps2", COLOR_CYAN, ud);
    } else {
        out_fn("B-System 3.0 Workstation System (BTRON3 Specification 3.20)", COLOR_CYAN, ud);
        out_fn("Hardware: Sony PlayStation 2 (GS, DualShock 2, OHCI USB)", COLOR_LTGRAY, ud);
        out_fn("Build: " __DATE__ " " __TIME__, COLOR_LTGRAY, ud);
    }
}

ER slp_tsk(void)         { return E_OK; }
ER wup_tsk(ID tskid)     { (void)tskid; return E_OK; }

/* ── Desktop session: input plane + presentation pass ────────────── */

static uint32_t s_pass_panel_due, s_pass_full_due;
static GDEV    *s_gui_screen;
static int      s_task_driving;

/* ps2_cursor_input_task - the 1 ms input worker: pad, USB, key repeat, pointer. */
static void ps2_cursor_input_task(GDEV *screen, uint32_t now_us)
{
    const uint32_t t0 = ps2_count_read();

    (void)screen; (void)now_us;
    ps2_pad_poll();
    ps2_usb_poll();
    ps2_kbd_repeat_tick();
    set_baremetal_mouse_pos((H)s_mouse_x, (H)s_mouse_y);
    ps2_plane_charge(&s_plane_input, t0, ASYNC_INPUT_BUDGET_US);
}

/* btron_cooperative_input_pump - lets long-running apps keep the pointer live. */
void btron_cooperative_input_pump(void)
{
    EVT ev;

    if (!s_gui_active || !s_gui_screen) return;
    ps2_cursor_input_task(s_gui_screen, 0);
    while (get_evt(&ev, 0) == E_OK) workbench_process_event(s_gui_screen, &ev);
}

/* ps2_gui_pass - one pass: input, events, damage decision, present. */
static void ps2_gui_pass(GDEV *screen)
{
    ps2_bandset_t set = { { { 0, 0, 0, 0 } }, 0, 0 };
    EVT ev;
    int force_full = 0;
    uint32_t ui_t0, ov = 0u, flip_wait_us = 0u;
    int dbl;

    ps2_cycle_begin(s_task_driving);
    ps2_cursor_input_task(screen, 0);

    ui_t0 = ps2_count_read();
    ps2_console_shell_poll();
    ps2_cycle_mark();
#if BTRON_PS2_BENCH_APP
    ps2_bench_app_frame_hook(screen);
#endif
    ps2_cycle_mark();

    while (get_evt(&ev, 0) == E_OK) {
        workbench_process_event(screen, &ev);
        if (ev.type != EV_MOUSE_MOVE) { force_full = 1; ps2_cycle_why(PS2_CYCLE_WHY_EVENT); }
    }

    orchestra_ui_tick();
    cassette_service();

    {   /* GL layers refresh every pass; software GL windows take turns. */
        static uint32_t turn;
        WND *sw[4];
        int n = 0;

        for (WND *w = get_wnd_list(); w; w = w->next) {
            if (!w->visible || !w->gl_surf) continue;
            if (w->gl_layer) inval_wnd(w); else if (n < 4) sw[n++] = w;
        }
        if (n > 0) inval_wnd(sw[turn++ % (uint32_t)n]);
    }

    ps2_bands_for_pass(&set, s_mouse_x, s_mouse_y, s_shown_x, s_shown_y);

    if (s_timebase_ok) {
        const uint32_t now = ps2_count_read();
        const uint32_t tpu = btron_ee_ticks_per_us();
        if ((uint32_t)(now - s_pass_panel_due) >= btron_planes()->panel_refresh_us * tpu) {
            ps2_bandset_add(&set, 0, PS2_SCREEN_WIDTH, 0, PS2_PANEL_ROWS);
            s_pass_panel_due = now;
        }
        if ((uint32_t)(now - s_pass_full_due) >= btron_planes()->full_convergence_us * tpu) {
            force_full = 1;
            ps2_cycle_why(PS2_CYCLE_WHY_TIMER);
            s_pass_full_due = now;
        }
    }

    /* Overlays are drawn only by the whole-canvas composite. */
    if (global_menu_is_open())    ov |= PS2_CYCLE_WHY_GMENU;
    if (tracker_is_menu_open())   ov |= PS2_CYCLE_WHY_TRACKER;
    if (wnd_mgr_is_interacting()) ov |= PS2_CYCLE_WHY_WM;
    if (tip_get_state() == TIP_STATE_CONVERTING ||
        tip_get_state() == TIP_STATE_CANDIDATE_SELECT) ov |= PS2_CYCLE_WHY_TIP;

    ps2_cycle_why(ov);
    if (set.overflow) ps2_cycle_why(PS2_CYCLE_WHY_OVERFLOW);
    ps2_cycle_damage(&set);
    if (force_full || set.overflow || ov) ps2_bandset_full(&set);

    dbl = (int)gs_disp_is_double();
    if (dbl && ps2_rot_step(&set)) ps2_cycle_why(PS2_CYCLE_WHY_ROTATE);
    ps2_cycle_merged(&set);

    if (!set.n) {
        ps2_cycle_stamp(0u, 0u, 0u, 0u, 0, 0);
        __asm__ volatile("nop" : : : "memory");
        return;
    }
    ps2_cycle_mark();

#if BTRON_PS2_PAINT_WORKER
    ps2_pw_plan(&set, ps2_pw_us(ui_t0, ps2_count_read()));
#endif
    {
        uint32_t r_us, s_us, u_us, px;

        ps2_paint_bands(screen, &set, &r_us, &s_us, &u_us, &px);
#if BTRON_PS2_PAINT_WORKER
        if (s_pw_on)
            s_pw_upload_us = s_pw_upload_us ? ((3u * s_pw_upload_us + u_us + 2u) >> 2) : u_us;
        ps2_pw_freeze(0);
        s_pw_layer = NULL;
#endif
        if (dbl) flip_wait_us = ps2_gs_flip(force_full ? 1 : 0);
        ps2_cycle_mark();
        if (ps2_band_is_full(&set.b[0])) s_pass_full_due = ps2_count_read();
        ps2_cycle_mark();
        ps2_cycle_stamp(r_us, s_us, u_us, px, (int)set.n, ps2_band_is_full(&set.b[0]));
    }

    /* The flip wait is the GS scanning, not bus work: excluded from the charge. */
    ps2_plane_charge(&s_plane_ui, ui_t0 + flip_wait_us * btron_ee_ticks_per_us(), ASYNC_UI_BUDGET_US);
    ps2_plane_ui_deadline();
    s_shown_x = s_mouse_x;
    s_shown_y = s_mouse_y;
    __asm__ volatile("nop" : : : "memory");
}

/* ── µITRON tasks: pass-driven, bodies run on the caller's stack ─── */

#define PS2_MAX_TASKS 4

typedef struct {
    T_CTSK config;
    int    active, started, holding;
} PS2_TASK;

static PS2_TASK  s_ps2_tasks[PS2_MAX_TASKS];
static ID        s_ps2_task_at[PS2_MAX_TASKS + 1];
static uint32_t  s_task_ms, s_task_us_rem, s_task_count_last;
static int       s_task_clock_running;

static PS2_TASK *ps2_task_self(void)
{
    ID id;

    if (s_task_driving <= 0 || s_task_driving > PS2_MAX_TASKS) return NULL;
    id = s_ps2_task_at[s_task_driving];
    return (id > 0 && id <= PS2_MAX_TASKS) ? &s_ps2_tasks[id - 1] : NULL;
}

ID cre_tsk(const T_CTSK *pk)
{
    if (!pk || !pk->task) return E_PAR;
    for (int i = 0; i < PS2_MAX_TASKS; i++) {
        if (s_ps2_tasks[i].active) continue;
        s_ps2_tasks[i].config  = *pk;
        s_ps2_tasks[i].active  = 1;
        s_ps2_tasks[i].started = s_ps2_tasks[i].holding = 0;
        return i + 1;
    }
    return E_LIMIT;
}

ER sta_tsk(ID tskid, VW exinf)
{
    if (tskid <= 0 || tskid > PS2_MAX_TASKS) return E_ID;
    PS2_TASK *t = &s_ps2_tasks[tskid - 1];
    if (!t->active || !t->config.task) return E_NOEXS;
    if (exinf != 0) t->config.exinf = exinf;
    t->started = 1;
#if BTRON_PS2_BENCH_APP
    if (s_bench_app_active) {
        s_ps2_task_at[s_task_driving + 1] = tskid;
        s_task_driving++;
        t->config.task(t->config.exinf);
        s_task_driving--;
        s_ps2_task_at[s_task_driving + 1] = 0;
        t->started = t->holding = t->active = 0;
    }
#endif
    return E_OK;
}

/* ps2_task_ms - ms since boot, folded over Count wraps (32-bit math only). */
static uint32_t ps2_task_ms(void)
{
    if (s_timebase_ok) {
        const uint32_t now = ps2_count_read();
        if (s_task_clock_running) {
            const uint32_t us = (uint32_t)(now - s_task_count_last) / btron_ee_ticks_per_us() + s_task_us_rem;
            s_task_ms += us / 1000u;
            s_task_us_rem = us % 1000u;
        } else {
            s_task_clock_running = 1;
        }
        s_task_count_last = now;
    }
    return s_task_ms;
}

void btron_scheduler_dispatch(void)
{
    (void)ps2_task_ms();
}

ER get_tim(SYSTIME *t)
{
    if (!t) return E_PAR;
    *t = (SYSTIME)ps2_task_ms();
    return E_OK;
}

/* ps2_task_hold - spend ms (cap 250) polling input every 1 ms. */
static void ps2_task_hold(uint32_t ms)
{
    PS2_TASK *self = ps2_task_self();
    uint32_t start, budget, period, last;

    if (!s_timebase_ok || ms == 0) return;
    if (ms > 250u) ms = 250u;
    period = 1000u * btron_ee_ticks_per_us();
    budget = ms * period;
    start  = last = ps2_count_read();

    if (self) self->holding = 1;
    while ((uint32_t)(ps2_count_read() - start) < budget) {
        const uint32_t now = ps2_count_read();
        if ((uint32_t)(now - last) >= period) {
            last = now;
            if (s_gui_active && s_gui_screen) ps2_cursor_input_task(s_gui_screen, 0);
        }
        btron_scheduler_dispatch();
        __asm__ volatile("nop" : : : "memory");
    }
    if (self) self->holding = 0;
}

static void ps2_task_dly(W dlytim)
{
    uint32_t t0;

    if (s_gui_screen) ps2_gui_pass(s_gui_screen);
    t0 = ps2_cycle_hold_begin();
    ps2_task_hold(dlytim > 0 ? (uint32_t)dlytim : 0u);
    ps2_cycle_hold_end(t0, dlytim > 0 ? (int)dlytim : 0);
}

void dly_tsk(W dlytim)   { (void)ps2_task_ms(); ps2_task_dly(dlytim); }
ER   tk_dly_tsk(W dlytim) { ps2_task_dly(dlytim); return E_OK; }

/* btron_list_tasks - `ps` census: three system planes (Option B), then app bodies. */
W btron_list_tasks(BTRON_TSK_ROW *out, W max)
{
    W n = 0;
    if (!out || max <= 0) return 0;

    if (n < max) { out[n] = (BTRON_TSK_ROW){ 1, "ps2_input", 1, (FP)(void *)ps2_cursor_input_task, BTRON_TSK_RUN }; n++; }
    if (n < max) { out[n] = (BTRON_TSK_ROW){ 2, "ps2_desktop", 5, (FP)(void *)launch_ps2_desktop_session, s_gui_active ? BTRON_TSK_RUN : BTRON_TSK_SLEEP }; n++; }
    if (n < max) { out[n] = (BTRON_TSK_ROW){ 3, "ps2_shell", 4, (FP)(void *)ps2_console_shell_poll, s_gui_active ? BTRON_TSK_SLEEP : BTRON_TSK_RUN }; n++; }
    for (int i = 0; i < PS2_MAX_TASKS && n < max; i++) {
        const PS2_TASK *t = &s_ps2_tasks[i];
        if (!t->active) continue;
        out[n].tskid = (ID)(n + 1);
        out[n].name  = t->config.name ? t->config.name : "(worker)";
        out[n].pri   = t->config.itskpri;
        out[n].entry = t->config.task;
        out[n].state = !t->started ? BTRON_TSK_NEW : t->holding ? BTRON_TSK_SLEEP : BTRON_TSK_RUN;
        n++;
    }
    return n;
}

/* launch_ps2_desktop_session - cold paint, then run passes until Ctrl+Q. */
void launch_ps2_desktop_session(void)
{
    GDEV *screen;

    ps2_set_gui(1);
    g_cursor_in_backbuffer = 1;

    screen = init_baremetal_desktop((uint32_t *)s_desktop_backbuffer, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    if (!screen) {
        ps2_kprintf("[FATAL] desktop init failed\n");
        ps2_set_gui(0);
        return;
    }
    btron_desktop_note_size(PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    workbench_init(PS2_SCREEN_WIDTH);

    ps2_page_double_apply(ps2_page_double_wanted());
    ps2_rot_owe_all();
#if BTRON_PS2_PAINT_WORKER
    ps2_pw_reset(0u);
    s_pw_on = 1; s_pw_passes = 0u; s_pw_defer_passes = 0u;
#endif

    {   /* Cold paint; with two pages, plate the second one too. */
        ps2_bandset_t whole = { { { 0, 0, 0, 0 } }, 0, 0 };
        uint32_t r, s, u, px;

        ps2_bandset_full(&whole);
        ps2_paint_bands(screen, &whole, &r, &s, &u, &px);
        if (gs_disp_is_double()) {
            ps2_gs_flip(1);
            ps2_bandset_full(&whole);
            ps2_paint_bands(screen, &whole, &r, &s, &u, &px);
            ps2_rot_clear();
        }
        s_shown_x = s_mouse_x;
        s_shown_y = s_mouse_y;
    }

    s_pass_panel_due = s_pass_full_due = ps2_count_read();
    s_gui_screen = screen;
    {
        RENDER_STATS discard;
        btron_render_stats_take(&discard);
    }
    ps2_kprintf("[PS2] Desktop up. Ctrl+Q returns to the console.\n");

#if BTRON_PS2_BENCH
    ps2_bench_run(screen);
    s_gui_active = 0;
#endif

    while (s_gui_active) {
        btron_scheduler_dispatch();
        ps2_gui_pass(screen);
        __asm__ volatile("nop" : : : "memory");
    }

    ps2_page_double_apply(0u);
    ps2_rot_clear();
#if BTRON_PS2_PAINT_WORKER
    ps2_pw_freeze(0);
    s_pw_on = 0;
#endif
    s_gui_screen = NULL;
    ps2_gs_text_clear();
    ps2_kprintf("[DESK] Back at the console. 'startx' again anytime.\nbtron-ps2# ");
}

/* ── Kernel entry ────────────────────────────────────────────────── */

/* ps2_shell_poll - Stage 1 interactive terminal console polling (RTOS zero-latency) */
static int ps2_shell_poll(void)
{
    btron_scheduler_dispatch();
    ps2_pad_poll();
    ps2_usb_poll();
    ps2_kbd_repeat_tick();
    ps2_console_shell_poll();
    return 1;
}

void ps2_kernel_main(void)
{
    uint32_t c0, gs0, gs1, band0, probe0, full_us, band_us, gs_us, probe_us;
    int verdict;

    ps2_stack_probe_arm();
    ps2_gs_set_framebuffer((uint32_t *)s_desktop_backbuffer);
    ps2_sio_init();
    ps2_console_shell_init(ps2_shell_exec, ps2_key_cb, ps2_move_mouse);
    ps2_kbd_init(ps2_key_cb, ps2_console_shell_char);

    btron_planes_install(&s_ps2_planes);
    btron_fp_init();

    c0 = ps2_count_read();
    for (volatile int i = 0; i < 20000; i++) { }
    s_timebase_ok = (ps2_count_read() != c0);
    if (s_timebase_ok && ps2_irq_input_arm() == 0) (void)ps2_irq_calib_wait();

    gs0 = ps2_count_read();
    ps2_gs_init(PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    gs1 = ps2_count_read();
    ps2_gs_flush();
    full_us = ps2_us_since(gs1);
    band0 = ps2_count_read();
    ps2_gs_upload(0, 0, PS2_SCREEN_WIDTH, 16);
    band_us = ps2_us_since(band0);
    gs_us = ps2_us_since(gs0);
    ps2_log_boot_head(gs_us, full_us, band_us, s_timebase_ok);

    ps2_pad_init();
    ps2_iopram_claim(NULL);
    probe0 = ps2_count_read();
    verdict = ps2_usb_probe(NULL);
    probe_us = ps2_us_since(probe0);
    if (verdict == PS2_OHCI_IOP_DMA) ps2_usb_host_start();
    ps2_log_boot_tail(probe_us, ps2_usb_log_host());
    ps2_log_async_calibration();

    btron_core_hfds_log();
    btron_core_banner();

#if BTRON_PS2_BENCH_GS == 1 || BTRON_PS2_BENCH_GS >= 6
    ps2_bench_gs_census();
#elif defined(BTRON_AUTO_GUI) && (BTRON_AUTO_GUI == 1)
    launch_ps2_desktop_session();
#else
    ps2_kprintf("btron-ps2# ");
#endif

    for (;;) {
        ps2_shell_poll();
        __asm__ volatile("nop" : : : "memory");
    }
}

#if defined(BTRON_PS2_PROBE) || defined(BTRON_PS2_BENCH)
#include "core_ps2_probe.c"
#endif
