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
#include <btron/ps2_tile.h>
#ifndef PS2_TILE_UI_PACE_US
#define PS2_TILE_UI_PACE_US 1000u
#endif
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
#include <btron/nexus_ring.h>
#include <btron/compositor.h>

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

/* Focused TinyGL is paint-rate limited (1 ms UI while it owes).  Idle
 * compositor sleeps a vsync so empty passes do not steal the raster.
 * 16 ms * 4 GL fair-share was ~12 FPS each (the observed frame drop). */
static const btron_plane_cfg_t s_ps2_planes = {
    .input_period_us     = 1000u,
    .ui_period_us        = PS2_TILE_UI_PACE_US,
    .panel_refresh_us    = 200000u,
    .full_convergence_us = 2000000u,
    .input_slices_max    = 16u,
    .ui_period_slow_us   = 0u,   /* do not drop to 30 Hz while hunting FPS */
    .ui_slow_paint_us    = 0u
};

#define PS2_UI_BUDGET_US 15000u

typedef struct { uint32_t runs, us_max, over; } ps2_plane_run_t;
static ps2_plane_run_t s_plane_input, s_plane_ui;
static uint32_t s_plane_ui_last, s_plane_ui_late;
static uint32_t s_rt_log_us;

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
#ifndef PS2_MOUSE_VELOCITY_FP
#define PS2_MOUSE_VELOCITY_FP 32 /* 8.8 fixed-point velocity (256=1.0x, 128=0.5x, 64=0.25x) */
#endif
#ifndef PS2_MOUSE_DELAY_US
#define PS2_MOUSE_DELAY_US 150u
#endif
#ifndef PS2_MOUSE_COMP_MAX_US
#define PS2_MOUSE_COMP_MAX_US BTRON_PTR_DELAY_COMP_MAX_US
#endif

extern int32_t  g_ptr_gain_fp;
extern uint32_t g_ptr_delay_us;
static int32_t s_mouse_rem_x, s_mouse_rem_y;

/* Never-drop edge ring (ASYNC.txt Phase 2: BUT/KEY are never dropped).  snd_evt()
 * may refuse when the system queue is full; the edge then waits here, in order,
 * and is retried by the input plane.  Ordering is preserved: while the ring is
 * non-empty new edges queue behind it. */
NEXUS_RING_DEFINE(ps2_edge_ring_t, EVT, 64)
static ps2_edge_ring_t s_edge_ring;
static int32_t s_move_dx, s_move_dy;      /* coalesced, one EV_MOUSE_MOVE per input period */
static int      s_ptr_followed;           /* pump already applied this motion visually */

static void ps2_edge_flush(void)
{
    EVT ev;
    while (ps2_edge_ring_t_len(&s_edge_ring)) {
        ev = s_edge_ring.slot[s_edge_ring.tail & 63u];
        if (snd_evt(&ev) != E_OK) return;          /* still full: retry next period */
        (void)ps2_edge_ring_t_rcv(&s_edge_ring, &ev);
    }
}

static ER ps2_post_edge(EVT *ev)
{
    ps2_edge_flush();
    if (ps2_edge_ring_t_len(&s_edge_ring) == 0u && snd_evt(ev) == E_OK) return E_OK;
    return ps2_edge_ring_t_snd(&s_edge_ring, ev) == 0 ? E_OK : E_LIMIT;   /* counted in .refused */
}

/* Emit the coalesced motion as a single event (bounded: no per-report storm). */
static void ps2_move_flush(void)
{
    EVT ev;
    if (!s_move_dx && !s_move_dy) return;
    tkl_memset(&ev, 0, sizeof(ev));
    ev.type   = EV_MOUSE_MOVE;
    ev.pos.x  = (H)s_mouse_x;
    ev.pos.y  = (H)s_mouse_y;
    ev.button = (s_prev_mouse_btn & 1u) ? 1 : (s_prev_mouse_btn & 2u) ? 2 : (s_prev_mouse_btn & 4u) ? 3 : 0;
    ev.data   = (VW)(((uint32_t)(uint16_t)(int16_t)s_move_dx) | ((uint32_t)(uint16_t)(int16_t)s_move_dy << 16));
    if (snd_evt(&ev) == E_OK) s_move_dx = s_move_dy = 0;   /* MOVE may be dropped; keep accumulating */
}

/* Pump follow already wrote s_mouse_*; still post MOVE so drag/hover see it. */
static void ps2_move_emit_now(void)
{
    EVT ev;
    tkl_memset(&ev, 0, sizeof(ev));
    ev.type   = EV_MOUSE_MOVE;
    ev.pos.x  = (H)s_mouse_x;
    ev.pos.y  = (H)s_mouse_y;
    ev.button = (s_prev_mouse_btn & 1u) ? 1 : (s_prev_mouse_btn & 2u) ? 2 : (s_prev_mouse_btn & 4u) ? 3 : 0;
    ev.data   = (VW)(((uint32_t)(uint16_t)(int16_t)s_move_dx) | ((uint32_t)(uint16_t)(int16_t)s_move_dy << 16));
    if (snd_evt(&ev) == E_OK) s_move_dx = s_move_dy = 0;
}

static void ps2_apply_delta(int dx, int dy, int enqueue)
{
    const int32_t gain = g_ptr_gain_fp > 0 ? g_ptr_gain_fp : PS2_MOUSE_VELOCITY_FP;
    int32_t nx, ny;
    dx = (int)dp_ptr_scale((int32_t)dx, gain, &s_mouse_rem_x);
    dy = (int)dp_ptr_scale((int32_t)dy, gain, &s_mouse_rem_y);
    if (dx == 0 && dy == 0) return;
    nx = (int32_t)s_mouse_x + dx;
    ny = (int32_t)s_mouse_y + dy;
    if (nx < 0) nx = 0; else if (nx >= PS2_SCREEN_WIDTH)  nx = PS2_SCREEN_WIDTH - 1;
    if (ny < 0) ny = 0; else if (ny >= PS2_SCREEN_HEIGHT) ny = PS2_SCREEN_HEIGHT - 1;
    s_mouse_x = (int)nx;
    s_mouse_y = (int)ny;
    set_baremetal_mouse_pos((H)s_mouse_x, (H)s_mouse_y);
    if (enqueue) {
        s_move_dx += dx;
        s_move_dy += dy;
    }
}

static __attribute__((noinline)) void ps2_move_mouse(int dx, int dy)
{
    ps2_apply_delta(dx, dy, 1);
}

static void ps2_pointer_follow(void)
{
    int32_t dx = 0, dy = 0;
    if (ps2_usb_pointer_follow(&dx, &dy)) {
        ps2_apply_delta((int)dx, (int)dy, 0);
        s_ptr_followed = 1;
    }
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
    (void)ps2_post_edge(&ev);
}

static ER ps2_inject_key(UW keycode, int down)
{
    EVT ev;
    tkl_memset(&ev, 0, sizeof(ev));
    ev.type  = down ? EV_KEY_DOWN : EV_KEY_UP;
    ev.pos.x = (H)s_mouse_x;
    ev.pos.y = (H)s_mouse_y;
    ev.key   = keycode;
    return ps2_post_edge(&ev);
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
    if (s_ptr_followed) {
        s_ptr_followed = 0;
        if (dx || dy) ps2_move_emit_now();
        return;
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

/* ── Desktop session: input plane + presentation pass ────────────── */

static uint32_t s_pass_panel_due, s_pass_full_due;   /* us, btron_render_perf_us() clock */
static GDEV    *s_gui_screen;
static int      s_in_gui_pass;   /* nested compositor latch */

/* ps2_cursor_input_task - the 1 ms input worker: pad, USB, key repeat, pointer. */
static void ps2_cursor_input_task(GDEV *screen, uint32_t now_us)
{
    const uint32_t t0 = ps2_count_read();

    (void)screen; (void)now_us;
    ps2_pad_poll();
    ps2_usb_poll();
    ps2_kbd_repeat_tick();
    set_baremetal_mouse_pos((H)s_mouse_x, (H)s_mouse_y);
    ps2_edge_flush();
    ps2_move_flush();
    ps2_plane_charge(&s_plane_input, t0, ASYNC_INPUT_BUDGET_US);
}

static ID s_input_tskid;

/* btron_cooperative_input_pump - collect HID, follow the seqlock, wake INPUT.
 * Follow never snd_evt; INPUT still produces MOVE. */
void btron_cooperative_input_pump(void)
{
    static volatile int s_in_pump;
    if (!s_gui_active || !s_gui_screen || s_in_pump) return;   /* never re-enter */
    s_in_pump = 1;
    ps2_usb_collect();
    if (s_input_tskid > 0) wup_tsk(s_input_tskid);
    ps2_pointer_follow();
    s_in_pump = 0;
}

/* ── Compositor: shared policy (include/btron/compositor.h), PS2 machine ops ──────────
 *
 * The pass is the same one ARM64 and POSIX run; what is PS2 is below:
 *   poll_input   the 1 ms input worker
 *   drain_events console, BTRON events, app ticks, pointer snapshot
 *   render       damage -> band set -> ps2_paint_bands into the BACK page (never flips)
 *   present      the single place the page is exposed: ps2_gs_flip, then the plane charge
 * The band set lives in this context between render and present, so the GL layer's stream,
 * the canvas upload and the flip are ordered by construction, not by who calls whom.
 *
 * Cadence is 1 ms while the focused GL owes or the pointer moved, else one
 * NTSC frame.  Panel / convergence timers stay band-aware inside render
 * (they apply only while the pointer is still). */
typedef struct {
    GDEV          *screen;
    ps2_bandset_t  set;           /* owed bands: written by render, consumed by present */
    int            force_full;
    int            pass_mx, pass_my;
    int            dbl;
    int            painted;       /* render produced bands that present must expose     */
    uint32_t       ui_t0;         /* start of the UI plane's work this pass (EE count)  */
} ps2_comp_ctx_t;

static ps2_comp_ctx_t   s_ps2_ctx;
static compositor_t     s_ps2_comp;

static uint32_t ps2_comp_now(void *x) { (void)x; return btron_render_perf_us(); }

static void ps2_comp_poll_input(void *x)
{
    (void)x;
    /* USB collect+consume so MOVE is in the queue before drain (window drag).
     * Pad is 1 ms gated in ps2_pad_poll, so compositor and INPUT can both
     * call it without doubling the stick. */
    ps2_usb_poll();
    ps2_pad_poll();
    ps2_kbd_repeat_tick();
    set_baremetal_mouse_pos((H)s_mouse_x, (H)s_mouse_y);
    ps2_edge_flush();
    ps2_move_flush();
}

static int ps2_comp_drain(void *x)
{
    ps2_comp_ctx_t *c = x;
    EVT ev;
    int redraw = 0;

    c->ui_t0 = ps2_count_read();
    ps2_console_shell_poll();

    c->force_full = 0;
    while (get_evt(&ev, 0) == E_OK) {
        workbench_process_event(c->screen, &ev);
        if (comp_evt_redraw(ev.type == EV_MOUSE_MOVE, ev.button,
                            wnd_mgr_is_interacting(),
                            global_menu_is_open() || tracker_is_menu_open())) {
            redraw = 1;
            if (ev.type != EV_MOUSE_MOVE) c->force_full = 1;
        }
    }
    c->pass_mx = s_mouse_x;
    c->pass_my = s_mouse_y;

    orchestra_ui_tick();
    cassette_service();
    return redraw;
}

static int ps2_comp_detect(void *x)
{
    (void)x;
    WND *lay;
    /* One GDEV realloc per pass, after OHCI MOVE bursts have coalesced
     * into chrome.  Pump HID right after so a long cls_dev/opn_dev/ZB_resize
     * does not leave the pointer frozen until the next NTSC frame. */
    if (wnd_mgr_flush_resize()) {
        btron_cooperative_input_pump();
        return 1;
    }
    if (s_mouse_x != s_shown_x || s_mouse_y != s_shown_y) return 1;
    lay = wnd_gl_layer();
    for (WND *w = get_wnd_list(); w; w = w->next) {
        if (!w->visible || !w->paint) continue;
        /* Invalid GL stays dirty until the round-robin paints it.
         * Closing one therefore shortens the rotation of the rest. */
        if (w->gl_layer && w == lay) {
            if (!w->img_valid) return 1;   /* app task asked for a 3D frame */
            continue;
        }
        if (!w->img_valid) return 1;
    }
    return 0;
}

static void ps2_comp_render(void *x, int full)
{
    ps2_comp_ctx_t *c = x;
    GDEV *screen = c->screen;
    uint32_t ov = 0u;

    (void)full;   /* PS2 damage is a band set, not a flag */
    c->painted = 0;
    c->set = (ps2_bandset_t){ { { 0, 0, 0, 0 } }, 0, 0 };

    {   /* 3D rate is the app task (inval_wnd + dly_tsk).  Cursor-only, and
         * an unfocused GS layer, freeze so present replays the last stream. */
        WND *lay = wnd_gl_layer();
        WND *top = get_top_wnd();
        /* Freeze an unfocused layer so present replays the last stream.
         * wnd.c still paints an unrecorded layer once: XMB is the first
         * GL tile, and freeze-before-record left a hole. */
        if (!lay)
            wnd_gl_freeze(0);
        else if (lay == top && lay->visible && !lay->img_valid)
            wnd_gl_freeze(0);
        else
            wnd_gl_freeze(1);
    }

    ps2_bands_for_pass(&c->set, c->pass_mx, c->pass_my, s_shown_x, s_shown_y);

    if (s_timebase_ok && c->pass_mx == s_shown_x && c->pass_my == s_shown_y && !wnd_mgr_is_interacting()) {
        /* Same period test as ARM64 and POSIX, applied band-aware. */
        const uint32_t now_us = btron_render_perf_us();
        if (comp_period_due(now_us, &s_pass_panel_due, btron_planes()->panel_refresh_us))
            ps2_bandset_add(&c->set, 0, PS2_SCREEN_WIDTH, 0, PS2_PANEL_ROWS);
        if (comp_period_due(now_us, &s_pass_full_due, btron_planes()->full_convergence_us))
            c->force_full = 1;
    }

    /* Overlays are drawn only by the whole-canvas composite. */
    if (global_menu_is_open() || tracker_is_menu_open() || wnd_mgr_is_interacting() ||
        tip_get_state() == TIP_STATE_CONVERTING || tip_get_state() == TIP_STATE_CANDIDATE_SELECT)
        ov = 1u;

    if (c->force_full || c->set.overflow || ov) ps2_bandset_full(&c->set);

    c->dbl = (int)gs_disp_is_double();
    if (c->dbl) (void)ps2_rot_step(&c->set);

    if (!c->set.n) {
        wnd_gl_freeze(0);
        __asm__ volatile("nop" : : : "memory");
        return;   /* nothing owed: present() sees painted == 0 and does not flip */
    }

#if BTRON_PS2_PAINT_WORKER
    ps2_pw_plan(&c->set, ps2_pw_us(c->ui_t0, ps2_count_read()));
#endif
    {
        uint32_t r_us, s_us, u_us, px;
        ps2_paint_bands(screen, &c->set, &r_us, &s_us, &u_us, &px);
        wnd_gl_freeze(0);
#if BTRON_PS2_PAINT_WORKER
        if (s_pw_on)
            s_pw_upload_us = s_pw_upload_us ? ((3u * s_pw_upload_us + u_us + 2u) >> 2) : u_us;
        ps2_pw_freeze(0);
        s_pw_layer = NULL;
#endif
        c->painted = 1;
    }
}

static uint32_t ps2_comp_present(void *x)
{
    ps2_comp_ctx_t *c = x;
    uint32_t flip_wait_us = 0u;

    if (!c->painted) return 0u;
    c->painted = 0;

    (void)ps2_gif_drain();
    (void)ps2_gif_reap();
    if (c->dbl) flip_wait_us = ps2_gs_flip(c->force_full ? 1 : 0);
    if (ps2_band_is_full(&c->set.b[0])) s_pass_full_due = btron_render_perf_us();

    /* The flip wait is the GS scanning, not bus work: excluded from the charge. */
    ps2_plane_charge(&s_plane_ui, c->ui_t0 + flip_wait_us * btron_ee_ticks_per_us(), PS2_UI_BUDGET_US);
    ps2_plane_ui_deadline();
    s_shown_x = c->pass_mx;
    s_shown_y = c->pass_my;
    __asm__ volatile("nop" : : : "memory");
    return flip_wait_us;
}

static const compositor_ops_t s_ps2_comp_ops = {
    .ctx = &s_ps2_ctx, .now_us = ps2_comp_now, .poll_input = ps2_comp_poll_input,
    .drain_events = ps2_comp_drain, .detect_damage = ps2_comp_detect,
    .render = ps2_comp_render, .present = ps2_comp_present, .idle = NULL
};

/* ps2_gui_pass - one compositor pass. */
static void ps2_gui_pass(GDEV *screen)
{
    if (s_in_gui_pass) return;
    s_in_gui_pass = 1;
    if (s_ps2_ctx.screen != screen || !s_ps2_comp.ops) {
        s_ps2_ctx.screen = screen;
        comp_init(&s_ps2_comp, &s_ps2_comp_ops, btron_planes(), PS2_UI_BUDGET_US);
    }
    (void)comp_pass(&s_ps2_comp);
    wnd_gl_freeze(0);          /* single release point for every exit path of a pass */
    s_in_gui_pass = 0;
}

/* ── µITRON tasks: cooperative stacks + btron_ctx_switch (boot_ps2.s) ─── */

#define PS2_MAX_TASKS       16
#define PS2_TASK_STACK_SIZE 16384u

typedef struct {
    T_CTSK   config;
    int      active, started, runnable, sleeping;
    uint32_t wake_count;
    void    *sp;
} PS2_TASK;

static uint8_t  s_ps2_task_stacks[PS2_MAX_TASKS][PS2_TASK_STACK_SIZE] __attribute__((aligned(16)));
static PS2_TASK s_ps2_tasks[PS2_MAX_TASKS];
static void    *s_scheduler_sp;
static int      s_current_task_idx = -1;
static uint32_t s_task_ms, s_task_us_rem, s_task_count_last;
static int      s_task_clock_running;

extern char _gp[];
void btron_ctx_switch(void **old_sp, void *new_sp);

static void btron_task_trampoline(void)
{
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < PS2_MAX_TASKS && s_ps2_tasks[idx].config.task)
        s_ps2_tasks[idx].config.task(s_ps2_tasks[idx].config.exinf);
    ext_tsk();
}

static PS2_TASK *ps2_task_self(void)
{
    int idx = s_current_task_idx;
    if (idx < 0 || idx >= PS2_MAX_TASKS) return NULL;
    return s_ps2_tasks[idx].active ? &s_ps2_tasks[idx] : NULL;
}

ID cre_tsk(const T_CTSK *pk)
{
    if (!pk || !pk->task) return E_PAR;
    for (int i = 0; i < PS2_MAX_TASKS; i++) {
        uint32_t *frame;
        uint8_t *stk_top;
        int k;
        if (s_ps2_tasks[i].active) continue;
        stk_top = s_ps2_task_stacks[i] + PS2_TASK_STACK_SIZE;
        stk_top = (uint8_t *)((uintptr_t)stk_top & ~7u);
        frame = (uint32_t *)(stk_top - 48);
        for (k = 0; k < 12; k++) frame[k] = 0;
        frame[9]  = (uint32_t)(uintptr_t)_gp;
        frame[10] = (uint32_t)(uintptr_t)btron_task_trampoline;
        s_ps2_tasks[i].sp       = frame;
        s_ps2_tasks[i].config   = *pk;
        s_ps2_tasks[i].active   = 1;
        s_ps2_tasks[i].started  = 0;
        s_ps2_tasks[i].runnable = 0;
        s_ps2_tasks[i].sleeping = 0;
        s_ps2_tasks[i].wake_count = 0;
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
    t->started  = 1;
    t->runnable = 1;
    t->sleeping = 0;
    return E_OK;
}

ER ter_tsk(ID tskid)
{
    if (tskid <= 0 || tskid > PS2_MAX_TASKS) return E_ID;
    PS2_TASK *t = &s_ps2_tasks[tskid - 1];
    if (!t->active) return E_NOEXS;
    t->started = t->runnable = t->sleeping = 0;
    return E_OK;
}

ER del_tsk(ID tskid)
{
    if (tskid <= 0 || tskid > PS2_MAX_TASKS) return E_ID;
    PS2_TASK *t = &s_ps2_tasks[tskid - 1];
    if (!t->active) return E_NOEXS;
    t->active = t->started = t->runnable = t->sleeping = 0;
    return E_OK;
}

void ext_tsk(void)
{
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < PS2_MAX_TASKS) {
        s_ps2_tasks[idx].active = s_ps2_tasks[idx].started = 0;
        s_ps2_tasks[idx].runnable = s_ps2_tasks[idx].sleeping = 0;
    }
    s_current_task_idx = -1;
    if (s_scheduler_sp) {
        void *dummy = NULL;
        btron_ctx_switch(&dummy, s_scheduler_sp);
    }
    for (;;) { __asm__ volatile("nop"); }
}

void exd_tsk(void)
{
    ext_tsk();
}

ER slp_tsk(void)
{
    PS2_TASK *t = ps2_task_self();
    if (!t) return E_OBJ;
    t->sleeping = 1;
    t->runnable = 0;
    t->wake_count = 0xFFFFFFFFu;
    s_current_task_idx = -1;
    if (s_scheduler_sp) btron_ctx_switch(&t->sp, s_scheduler_sp);
    return E_OK;
}

ER wup_tsk(ID tskid)
{
    if (tskid <= 0 || tskid > PS2_MAX_TASKS) return E_ID;
    if (!s_ps2_tasks[tskid - 1].active) return E_NOEXS;
    s_ps2_tasks[tskid - 1].sleeping = 0;
    s_ps2_tasks[tskid - 1].runnable = 1;
    return E_OK;
}

ID   tk_cre_tsk(const T_CTSK *pk) { return cre_tsk(pk); }
ER   tk_sta_tsk(ID tskid, VW exinf) { return sta_tsk(tskid, exinf); }
ER   tk_ter_tsk(ID tskid) { return ter_tsk(tskid); }
ER   tk_del_tsk(ID tskid) { return del_tsk(tskid); }
void tk_ext_tsk(void) { ext_tsk(); }
void tk_exd_tsk(void) { exd_tsk(); }

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
    uint32_t now = ps2_count_read();
    int self = s_current_task_idx;
    (void)ps2_task_ms();
    for (int i = 0; i < PS2_MAX_TASKS; i++) {
        if (!s_ps2_tasks[i].active || !s_ps2_tasks[i].started) continue;
        if (i == self) continue;
        if (s_ps2_tasks[i].sleeping) {
            if (s_ps2_tasks[i].wake_count != 0xFFFFFFFFu &&
                (int32_t)(now - s_ps2_tasks[i].wake_count) >= 0) {
                s_ps2_tasks[i].sleeping = 0;
                s_ps2_tasks[i].runnable = 1;
            } else {
                continue;
            }
        }
        if (s_ps2_tasks[i].runnable) {
            s_current_task_idx = i;
            btron_ctx_switch(&s_scheduler_sp, s_ps2_tasks[i].sp);
            s_current_task_idx = -1;
        }
    }
}

ER get_tim(SYSTIME *t)
{
    if (!t) return E_PAR;
    *t = (SYSTIME)ps2_task_ms();
    return E_OK;
}

void dly_tsk(W dlytim)
{
    PS2_TASK *t = ps2_task_self();
    (void)ps2_task_ms();
    if (t) {
        uint32_t tpu = btron_ee_ticks_per_us();
        uint32_t ticks = (dlytim > 0 ? (uint32_t)dlytim : 0u) * 1000u * (tpu ? tpu : 1u);
        t->sleeping = 1;
        t->runnable = 0;
        t->wake_count = ps2_count_read() + ticks;
        s_current_task_idx = -1;
        if (s_scheduler_sp) btron_ctx_switch(&t->sp, s_scheduler_sp);
    } else {
        uint32_t tpu = btron_ee_ticks_per_us();
        uint32_t start = ps2_count_read();
        uint32_t budget = (dlytim > 0 ? (uint32_t)dlytim : 1u) * 1000u * (tpu ? tpu : 1u);
        while ((uint32_t)(ps2_count_read() - start) < budget)
            __asm__ volatile("nop");
    }
}

ER tk_dly_tsk(W dlytim) { dly_tsk(dlytim); return E_OK; }

/* btron_list_tasks - `ps` census: live cre_tsk slots. */
W btron_list_tasks(BTRON_TSK_ROW *out, W max)
{
    W n = 0;
    if (!out || max <= 0) return 0;

    if (!s_gui_active && n < max) {
        out[n] = (BTRON_TSK_ROW){ 3, "ps2_shell", 4, (FP)(void *)ps2_console_shell_poll, BTRON_TSK_RUN };
        n++;
    }
    for (int i = 0; i < PS2_MAX_TASKS && n < max; i++) {
        const PS2_TASK *t = &s_ps2_tasks[i];
        if (!t->active) continue;
        out[n].tskid = (ID)(i + 1);
        out[n].name  = t->config.name ? t->config.name : "(worker)";
        out[n].pri   = t->config.itskpri;
        out[n].entry = t->config.task;
        out[n].state = !t->started ? BTRON_TSK_NEW : t->sleeping ? BTRON_TSK_SLEEP : BTRON_TSK_RUN;
        n++;
    }
    return n;
}

static void ps2_input_entry(VW exinf)
{
    (void)exinf;
    while (s_gui_active) {
        ps2_cursor_input_task(s_gui_screen, 0);
        dly_tsk(1);
    }
    ext_tsk();
}

static void ps2_ui_entry(VW exinf)
{
    (void)exinf;
    while (s_gui_active) {
        uint32_t t0 = btron_render_perf_us();
        uint32_t dt, period, sleep_ms;
        if (s_gui_screen) ps2_gui_pass(s_gui_screen);
        dt = btron_render_perf_us() - t0;
        period = PS2_TILE_UI_PACE_US;
        if (s_ps2_comp.cfg) period = comp_ui_interval(&s_ps2_comp);
        if (period < 1000u) period = 1000u;
        /* 1 ms only while the focused GL still owes, or the pointer moved.
         * Unfocused GL rests (wnd_frame_ms); spinning the compositor at 1 kHz
         * for their stale img_valid stole the raster from the app in use. */
        if (wnd_gl_focused_owes() || s_mouse_x != s_shown_x || s_mouse_y != s_shown_y)
            sleep_ms = 1u;
        else if (dt < 16667u)
            sleep_ms = (16667u - dt) / 1000u;
        else
            sleep_ms = (dt < period) ? (period - dt) / 1000u : 1u;
        if (sleep_ms < 1u) sleep_ms = 1u;
        if ((uint32_t)(t0 - s_rt_log_us) >= 2000000u && s_ps2_comp.ops) {
            unsigned last = (unsigned)s_ps2_comp.ui.last_us;
            int ngl = wnd_gl_count();
            unsigned fps = 0u;
            s_rt_log_us = t0;
            if (last > 0u && ngl > 0)
                fps = 1000000u / last / (unsigned)ngl;
            ps2_kprintf("[RT] gl=%d ui=%uus max=%uus fps~=%u over=%u late=%u runs=%u\n",
                        ngl, last,
                        (unsigned)s_ps2_comp.ui.max_us, fps,
                        (unsigned)s_ps2_comp.ui.over,
                        (unsigned)s_ps2_comp.ui.late,
                        (unsigned)s_ps2_comp.ui.runs);
        }
        dly_tsk((W)sleep_ms);
    }
    ext_tsk();
}

static void ps2_spawn_planes(void)
{
    T_CTSK ctsk;
    tkl_memset(&ctsk, 0, sizeof(ctsk));
    ctsk.task = ps2_input_entry;
    ctsk.itskpri = 1;
    ctsk.stksz = (W)PS2_TASK_STACK_SIZE;
    ctsk.name = "ps2_input";
    s_input_tskid = cre_tsk(&ctsk);
    if (s_input_tskid > 0) sta_tsk(s_input_tskid, 0);
    tkl_memset(&ctsk, 0, sizeof(ctsk));
    ctsk.task = ps2_ui_entry;
    ctsk.itskpri = 1;
    ctsk.stksz = (W)PS2_TASK_STACK_SIZE;
    ctsk.name = "ps2_desktop";
    {
        ID ui = cre_tsk(&ctsk);
        if (ui > 0) sta_tsk(ui, 0);
    }
}

/* launch_ps2_desktop_session - cold paint, then dispatch INPUT/UI until Ctrl+Q. */
void launch_ps2_desktop_session(void)
{
    GDEV *screen;

    ps2_set_gui(1);
    g_cursor_in_backbuffer = 1;

    screen = init_baremetal_desktop((uint32_t *)s_desktop_backbuffer, PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    if (!screen) { ps2_kprintf("[FATAL] desktop init failed\n"); ps2_set_gui(0); return; }
    s_gui_screen = screen;
    ps2_spawn_planes();
    btron_desktop_note_size(PS2_SCREEN_WIDTH, PS2_SCREEN_HEIGHT);
    workbench_init(PS2_SCREEN_WIDTH);
    /* Quake 80% of the screen plus a command-line terminal under it.
     * Surfaces are the RAM working set; PAK/ROMs stay on the ISO. */
    {
        int n = ps2_tile_open_standard();
        ps2_kprintf("[PS2] tile desktop %d/%d\n", n, PS2_TILE_N);
    }
    btron_core_mem_log();
    ps2_heap_holders("desktop up");

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
        s_shown_x = s_mouse_x; s_shown_y = s_mouse_y;
    }
    s_pass_panel_due = s_pass_full_due = btron_render_perf_us();
    { RENDER_STATS discard; btron_render_stats_take(&discard); }
    ps2_kprintf("[PS2] Desktop up. Ctrl+Q returns to the console.\n");
#if BTRON_PS2_BENCH
    ps2_bench_run(screen);
    s_gui_active = 0;
#endif
    while (s_gui_active) {
        btron_scheduler_dispatch();
        __asm__ volatile("nop" : : : "memory");
    }
    ps2_page_double_apply(0u);
    ps2_rot_clear();
#if BTRON_PS2_PAINT_WORKER
    ps2_pw_freeze(0); s_pw_on = 0;
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

#if defined(BTRON_AUTO_GUI) && (BTRON_AUTO_GUI == 1)
    launch_ps2_desktop_session();
#else
    ps2_kprintf("btron-ps2# ");
#endif

    for (;;) {
        ps2_shell_poll();
        __asm__ volatile("nop" : : : "memory");
    }
}
