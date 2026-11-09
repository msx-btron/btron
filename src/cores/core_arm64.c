/*
 * core_arm64.c — B-System BTRON3 3.20 RTOS Kernel for Raspberry Pi 3B/4B (BCM2711 / AArch64)
 *
 * Dedicated Target 6: BTRON_YOKOYAMA_AARCH64 (Raspberry Pi 3B/4B, Cortex-A72, AArch64)
 * Honoring: Takanori Yokoyama (横山 孝徳) — T-Kernel Pioneer
 *
 * Architecture:
 *   • Hardware Drivers:
 *       - VideoCore GPU Mailbox Framebuffer / Display (1024x768 32-bpp Double-Buffered)
 *       - Synopsys DesignWare DWC2 USB 2.0 Host Controller (USB Keyboard & Mouse)
 *       - ARM PrimeCell PL011 UART Serial Console Driver
 *       - BCM2711 / BCM2837 System Timer (60Hz System Tick)
 *       - EMMC2 / SD Storage Interface & HFDS Record Manager Status
 *   • Integrated B-System Workbench:
 *       - Plugs into core_init.c, desktop.c, wnd.c, vobj.c, global_menu.c, etc.
 *       - Launches authentic B-System Workbench desktop with live windows and desktop icons
 *       - Real-time mouse cursor tracking, window dragging, tabs, and menus
 *       - Interactive keyboard input from USB and PL011 serial console
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <btron/types.h>
#include <btron/error.h>
#include <btron/itron.h>
#include <btron/dp.h>
#include <btron/dp_accel.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/apps.h>
#include <btron/workbench.h>
#include <btron/fast_blit.h>
#include <btron/arm64_mem.h>
#include <libstr.h>
#include <drivers/broadcom/dwc2.h>
#include <drivers/broadcom/pcie.h>
#include <drivers/broadcom/xhci.h>
#include <drivers/broadcom/v3d.h>

extern void orchestra_ui_tick(void);
extern void cassette_service(void);

/* ═══════════════════════════════════════════════════════════════════
 * Dynamic Hardware Memory Map (Pi 3B: 0x3F000000, Pi 4B: 0xFE000000)
 * ═══════════════════════════════════════════════════════════════════ */
extern uintptr_t g_mmio_base;
#define PL011_BASE          (g_mmio_base + 0x00201000UL)
#define MBOX_BASE_ADDR      (g_mmio_base + 0x0000B880UL)
#define DWC2_BASE           (g_mmio_base + 0x00980000UL)
#define TIMER_BASE          (g_mmio_base + 0x00003000UL)

/* Kernel Heap Boundaries */
#define HEAP_BASE           ((uintptr_t)0x02000000)  /* 32 MB */
#define HEAP_LIMIT          ((uintptr_t)0x1B000000)  /* 432 MB limit */
extern uintptr_t heap_ptr;

/* The two [MEM] bands in this file (`mem` on the console, btron_core_mem_log()
 * on the serial line) print the heap and the non-cacheable window as literal
 * text, because fb_log() and uart_puts() take a plain string.  These make a
 * drift between that text and the addresses the page tables actually install a
 * compile error instead of a confident lie on the screen -- the window moved
 * twice already, and the console is what an operator reading the boot log trusts.
 * The second line is the layout invariant itself: .bss must end below the
 * window (link.ld enforces that at link time) and the window must end below the
 * heap, which is exactly what arm64_mmu_layout_ok() re-checks on the device. */
_Static_assert(BTRON_NOCACHE_BASE == 0x01C00000UL && BTRON_NOCACHE_END == 0x01E00000UL,
               "[MEM] band text no longer matches BTRON_NOCACHE_BASE/END");
_Static_assert(HEAP_BASE == 0x02000000UL && HEAP_BASE >= BTRON_NOCACHE_END,
               "[MEM] heap band text no longer matches HEAP_BASE, or the window moved past it");

/* Display Resolution (Standard VideoCore Framebuffer) */
#define BTRON_SCREEN_W      1024
#define BTRON_SCREEN_H      768

/* INPUT and UI are peers.  The fixed release periods and work budgets in
 * <btron/async_rt.h>—not a priority boost—bound the input-to-presentation
 * path on the Pi 400 (ASYNC.txt Tier-1). */
#include <btron/async_rt.h>
#define ASYNC_UI_EVENT_BUDGET       16u
#ifndef ASYNC_TELEMETRY
#define ASYNC_TELEMETRY              1
#endif

static async_rt_stats_t s_async_rt_stats;

/* Wait-free seqlock pointer snapshot: produced by the 1 kHz timer IRQ,
 * consumed by the 1 ms INPUT worker.  Replaces the old shared s_accum_*
 * read-and-clear handoff in the xHCI driver. */
static pointer_slot_t s_pointer_slot;
static pointer_snapshot_t s_input_last;
static uint8_t s_input_prev_buttons = 0;
#define BUTTON_EDGE_RING_SIZE 256u
static volatile uint8_t s_button_edge_ring[BUTTON_EDGE_RING_SIZE];
static volatile uint16_t s_button_edge_head = 0;
static volatile uint16_t s_button_edge_tail = 0;

/* 1 = the hardware 1 kHz IRQ input plane is armed (BCM2711 + VL805 xHCI).
 * 0 = legacy cooperative drain inside the GUI loop (QEMU / DWC2 / Pi 3). */
static volatile int s_async_irq_active = 0;
static uint32_t s_irq_prev_us = 0;

/* Mature Tested B-System Compositor Telemetry */
static uint32_t s_hud_comp_ms = 0;
static uint32_t s_hud_pres_ms = 0;
static uint32_t s_hud_wcet_ms = 0;
static char s_hud_path = '-';
static uint32_t s_hud_area = 0;
static char s_hud_wbranch = 'n';

void async_rt_format_status(char *buf, size_t len) {
    uint32_t gap = s_async_rt_stats.input_gap_us;
    uint32_t gap_max = s_async_rt_stats.input_gap_max_us;
    uint32_t blit = s_async_rt_stats.blit_us;
    uint32_t blit_max = s_async_rt_stats.blit_max_us;
    if (!buf || len == 0) return;
    tkl_snprintf(buf, len, "IN %u.%ums GAP %u/%ums BLIT %u.%ums",
                 gap / 1000u, (gap / 100u) % 10u, gap_max / 1000u,
                 blit / 1000u, (blit / 100u) % 10u, blit_max / 1000u);
}

/* DIAGNOSTIC compact HUD (temporary): one flash reveals the input topology.
 *   A = 1 when the 1 kHz IRQ plane is armed, 0 = cooperative fallback
 *   M = number of mouse slots the xHCI enumerator bound (0 => cursor frozen)
 *   K = 1 when a keyboard slot is bound
 *   G = worst INPUT cadence gap in ms (clamped 999)
 * Read as e.g. "A0M0K1G999": IRQ dead, no mouse enumerated, keyboard bound. */
void async_rt_format_compact_status(char *buf, size_t len) {
    extern uint32_t xhci_mouse_count(void);
    extern uint32_t xhci_kbd_bound(void);
    extern uint32_t arm64_irq_selftest_seen(void);
    uint32_t gap = s_async_rt_stats.input_gap_max_us / 1000u;
    if (gap > 99u) gap = 99u;
    if (!buf || len == 0) return;
    /* A1 = IRQ plane live.  When the tick never confirmed, the A field
     * carries the boot self-test verdict instead of a bare 0:
     *   V = forced-pending IRQ was NOT taken  -> GIC->CPU delivery broken
     *   T = forced-pending IRQ WAS taken      -> timer never asserts PPI */
    char a0 = arm64_irq_selftest_seen() ? 'T' : 'V';
    if (s_async_irq_active) {
        tkl_snprintf(buf, len, "A1M%uK%uG%u C%uP%uW%u %c%u/%c",
                     xhci_mouse_count(), xhci_kbd_bound(), gap,
                     s_hud_comp_ms, s_hud_pres_ms, s_hud_wcet_ms,
                     s_hud_path, s_hud_area, s_hud_wbranch);
    } else {
        tkl_snprintf(buf, len, "A%cM%uK%uG%u C%uP%uW%u %c%u/%c", a0,
                     xhci_mouse_count(), xhci_kbd_bound(), gap,
                     s_hud_comp_ms, s_hud_pres_ms, s_hud_wcet_ms,
                     s_hud_path, s_hud_area, s_hud_wbranch);
    }
}

/* Double-buffered 32-bpp Desktop Backbuffer */
static COLOR s_desktop_backbuffer[BTRON_SCREEN_W * BTRON_SCREEN_H] __attribute__((aligned(64)));

#define DMA_FB_SELFTEST_WORDS 32u
static uint32_t s_dma_fb_selftest_src[DMA_FB_SELFTEST_WORDS] __attribute__((aligned(64)));
static uint32_t s_dma_fb_selftest_saved[DMA_FB_SELFTEST_WORDS] __attribute__((aligned(64)));
static int s_dma_fb_selftest_passed;

/* Global interactive mouse coordinates */
static H s_mouse_x = 512;
static H s_mouse_y = 384;
static uint8_t g_prev_mouse_btns = 0;
static uint8_t g_prev_kbd_scancode = 0;

/* RISC OS Inspired Keyboard Auto-Repeat Parameters & State (defined in input.c) */
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;
static uint8_t  s_held_kbd_scancode = 0;
static uint8_t  s_held_kbd_modifiers = 0;
static uint32_t s_key_press_time_us = 0;
static uint32_t s_key_last_repeat_us = 0;

/* RISC OS Mouse Multiplier (MouseStep CMOS &C2) & 3-Button Layout (defined in input.c) */
extern int      g_mouse_step_mult;
extern int      g_mouse_swap_select_adjust;

/* USB host controller selection: 1 = VL805 xHCI (hardware), 0 = DWC2 (QEMU / legacy) */
int g_use_xhci = 0;

/* External driver APIs */
extern void uart_init(void);
extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern int  uart_has_char(void);
extern int  uart_getc(void);
extern void uart_hex32(uint32_t val);
extern uint32_t *init_pi_framebuffer(uint32_t w, uint32_t h);
extern uint32_t  g_pi_fb_size;   /* bytes the VideoCore allocated at g_pi_fb_ptr */
extern ER ScreenDrv(int ac, unsigned char *av[]);
extern ER KbPdDrv(int ac, unsigned char *av[]);
extern ER LowKbPdDrv(int ac, unsigned char *av[]);
extern void* tkl_memset(void *s, int c, size_t n);
extern void tkernel_init_subsystems(int full_suite);
extern const UB* get_glyph_bitmap(TC code, H *out_width, H *out_height);
extern WND* open_vobj_manager_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* launch_beos_chat(void);
extern WND* open_control_panel_window(void);
extern void* malloc(size_t sz);
extern void  free(void *ptr);
extern WND* open_mars_window(void);
extern WND* open_quake_window(int x, int y, int width, int height);
extern WND* open_xmb_window(void);
extern WND* open_glgears_window(void);
extern WND* open_lilcu64_demo_window(void);
extern WND* open_msx_window(void);
extern WND* open_drivesetup_window(void);
#include <fs/block.h>
#include <fs/vol_api.h>
#include <fs/fs_internal.h>
#include <btron/tty.h>
#include "../apps/clu/clu.h"

extern Volume *g_sys_vol;
extern Volume *g_anders_vol;
#include <drivers/broadcom/bcm2711_dma.h>
#include <btron/global_menu.h>
#include <btron/tracker.h>
extern int mailbox_set_virtual_offset(uint32_t x, uint32_t y);
extern int about_is_animating(void);
extern WND* about_get_wnd(void);
extern int  about_render_anim_dirty(GDEV *screen, volatile uint32_t *gpu_fb);
extern int  g_cursor_in_backbuffer;
extern void draw_baremetal_cursor_raw(volatile uint32_t *pixels, H mx, H my, H w, H h);
void blit_backbuffer_to_fb(volatile uint32_t *gpu_fb);
static int usb_poll_devices(GDEV *screen);
static void input_plane_task(GDEV *screen, uint32_t now_us);

/* ═══════════════════════════════════════════════════════════════════
 * Framebuffer Kernel Text Log Overlay
 * Uses existing BTRON troncode.c / jis_fonts.c 8×16 ASCII bitmaps.
 * Call fb_log_enable(gpu_fb) once the VideoCore mailbox returns a valid
 * framebuffer pointer.  After that, fb_log(msg) renders white text
 * on a semi-transparent black strip directly into GPU VRAM.
 * ═══════════════════════════════════════════════════════════════════ */

#define FB_LOG_W         BTRON_SCREEN_W   /* characters per row  = W/8     */
#define FB_LOG_GLYPH_H   16              /* glyph height in pixels        */
#define FB_LOG_ROWS      ((BTRON_SCREEN_H) / FB_LOG_GLYPH_H) /* 48 rows   */
#define FB_LOG_BG        0xCC000000u     /* semi-transparent black strip  */
#define FB_LOG_FG        0xFFFFFFFFu     /* white text                    */
#define FB_LOG_SHADOW    0xFF000000u     /* 1-pixel drop shadow           */

static volatile uint32_t *s_fb_log_fb   = NULL;
static int                s_fb_log_col  = 0;   /* current cursor X (chars)  */
static int                s_fb_log_row  = 0;   /* current cursor Y (rows)   */

static inline void arm64_fast_blit(volatile void *dst, const void *src, size_t bytes) {
    btron_row_blit((void *)dst, src, bytes);
    __asm__ volatile("dmb sy" : : : "memory");
}

/* The DMA engine reads RAM outside the CPU cache hierarchy. */
static inline void arm64_clean_cache_range(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(64UL - 1);
    uintptr_t end = (uintptr_t)addr + size;
    for (uintptr_t p = start; p < end; p += 64)
        __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
    __asm__ volatile("dsb sy" : : : "memory");
}

static inline void arm64_invalidate_cache_range(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(64UL - 1);
    uintptr_t end = (uintptr_t)addr + size;
    for (uintptr_t p = start; p < end; p += 64)
        __asm__ volatile("dc ivac, %0" : : "r"(p) : "memory");
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Strong counterpart of the weak stage timer in dp_core.c: the shared renderer
 * times its own composite stages with it (see g_render_stats). */
uint32_t btron_render_perf_us(void) {
    return *(volatile uint32_t *)(TIMER_BASE + 0x04);
}

/* Darken one text row in GPU VRAM (write-only, zero uncached reads). */
static void fb_log_darken_row(int row) {
    if (!s_fb_log_fb || row < 0 || row >= FB_LOG_ROWS) return;
    int y0 = row * FB_LOG_GLYPH_H;
    int stride = BTRON_SCREEN_W;
    uint32_t *gpu = (uint32_t *)s_fb_log_fb + y0 * stride;
    int total_px = FB_LOG_GLYPH_H * stride;
    for (int i = 0; i < total_px; i++) {
        gpu[i] = 0xFF080C14u;
    }
}

/* Fast SIMD Framebuffer Scroll:
 * Uses arm64_fast_blit to shift 47 text rows in ~1 ms, then clears bottom row. */
static void fb_log_scroll(void) {
    if (!s_fb_log_fb) return;
    int stride = BTRON_SCREEN_W;
    int scroll_bytes = (FB_LOG_ROWS - 1) * FB_LOG_GLYPH_H * stride * sizeof(uint32_t);

    /* Shift lines up using 64-byte burst blitter (only 1 ms!) */
    arm64_fast_blit((void *)s_fb_log_fb,
                    (const void *)(s_fb_log_fb + FB_LOG_GLYPH_H * stride),
                    scroll_bytes);

    /* Clear bottom row */
    fb_log_darken_row(FB_LOG_ROWS - 1);
}

/* Call once after init_pi_framebuffer returns a valid pointer. */
void fb_log_enable(volatile uint32_t *fb) {
    extern volatile uint32_t *g_irq_trace_fb;
    g_irq_trace_fb = fb;
    s_fb_log_fb  = fb;
    s_fb_log_col = 0;
    s_fb_log_row = 0;
    if (fb) {
        mailbox_set_virtual_offset(0, 0);
        /* Blank initial RAM backbuffer & VRAM cleanly */
        for (int i = 0; i < BTRON_SCREEN_W * BTRON_SCREEN_H; i++) {
            s_desktop_backbuffer[i] = 0xFF080C14u;
            ((uint32_t *)fb)[i] = 0xFF080C14u;
        }
    }
}

/* Render one ASCII character into GPU VRAM (write-only). */
static void fb_log_putchar(char c, int col, int row) {
    if (!s_fb_log_fb) return;
    H gw = 8, gh = 16;
    const UB *bmp = get_glyph_bitmap((TC)(unsigned char)c, &gw, &gh);
    if (!bmp) return;
    int x0 = col * 8;
    int y0 = row * FB_LOG_GLYPH_H;
    if (x0 + 8 > BTRON_SCREEN_W || row >= FB_LOG_ROWS) return;
    for (int row_i = 0; row_i < gh; row_i++) {
        UB bits = bmp[row_i];
        uint32_t *gpu_dst = (uint32_t *)s_fb_log_fb + (y0 + row_i) * BTRON_SCREEN_W + x0;
        for (int bit = 7; bit >= 0; bit--) {
            if (bits & (1u << bit)) {
                if (bit > 0) {
                    *(gpu_dst + (8 - bit)) = FB_LOG_SHADOW;
                }
                *gpu_dst = FB_LOG_FG;
            }
            gpu_dst++;
        }
    }
}

/*
 * fb_log(msg) — write a string to the on-screen kernel log.
 * Wraps at screen right edge; scrolls when reaching bottom row.
 * Call this in addition to (or instead of) uart_puts().
 */
void fb_log(const char *msg) {
    if (!msg) return;
    uart_puts(msg);          /* always mirror to serial */
    if (!s_fb_log_fb) return;
    for (const char *p = msg; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            s_fb_log_col = 0;
            if (*p == '\n') {
                s_fb_log_row++;
                if (s_fb_log_row >= FB_LOG_ROWS) {
                    fb_log_scroll();
                    s_fb_log_row = FB_LOG_ROWS - 1;
                } else {
                    fb_log_darken_row(s_fb_log_row);
                }
            }
            continue;
        }
        if (*p == '\t') {
            /* Tab: advance to next 8-column stop */
            s_fb_log_col = (s_fb_log_col + 8) & ~7;
        } else {
            int cols = BTRON_SCREEN_W / 8;
            if (s_fb_log_col >= cols) {
                s_fb_log_col = 0;
                s_fb_log_row++;
                if (s_fb_log_row >= FB_LOG_ROWS) {
                    fb_log_scroll();
                    s_fb_log_row = FB_LOG_ROWS - 1;
                } else {
                    fb_log_darken_row(s_fb_log_row);
                }
            }
            fb_log_putchar(*p, s_fb_log_col, s_fb_log_row);
            s_fb_log_col++;
        }
    }
}

void fb_log_hex32(uint32_t val) {
    char buf[12];
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    fb_log(buf);
}

void fb_log_hex64(uint64_t val) {
    char buf[20];
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[18] = '\0';
    fb_log(buf);
}

void fb_log_dec(uint32_t val) {
    char buf[16];
    char tmp[16];
    if (val == 0) {
        fb_log("0");
        return;
    }
    int p = 0;
    while (val > 0) {
        tmp[p++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = 0; i < p; i++) {
        buf[i] = tmp[p - 1 - i];
    }
    buf[p] = '\0';
    fb_log(buf);
}

/* Single-character echo to screen only (no UART double-echo). */
static void fb_log_putc(char c) {
    if (!s_fb_log_fb) return;
    if (c == '\n') {
        s_fb_log_col = 0;
        s_fb_log_row++;
        if (s_fb_log_row >= FB_LOG_ROWS) {
            fb_log_scroll();
            s_fb_log_row = FB_LOG_ROWS - 1;
        } else {
            fb_log_darken_row(s_fb_log_row);
        }
    } else if (c == '\r') {
        s_fb_log_col = 0;
    } else {
        if (s_fb_log_col == 0) fb_log_darken_row(s_fb_log_row);
        int cols = BTRON_SCREEN_W / 8;
        if (s_fb_log_col >= cols) {
            s_fb_log_col = 0;
            s_fb_log_row++;
            if (s_fb_log_row >= FB_LOG_ROWS) {
                fb_log_scroll();
                s_fb_log_row = FB_LOG_ROWS - 1;
            } else {
                fb_log_darken_row(s_fb_log_row);
            }
        }
        fb_log_putchar(c, s_fb_log_col, s_fb_log_row);
        s_fb_log_col++;
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Erase the last typed character on-screen (backspace). */
static void fb_log_backspace(void) {
    if (!s_fb_log_fb || s_fb_log_col <= 0) return;
    s_fb_log_col--;
    int x0 = s_fb_log_col * 8;
    int y0 = s_fb_log_row * FB_LOG_GLYPH_H;
    for (int py = y0; py < y0 + FB_LOG_GLYPH_H; py++) {
        uint32_t *line = (uint32_t *)s_fb_log_fb + py * BTRON_SCREEN_W;
        for (int px = x0; px < x0 + 8; px++) {
            line[px] = 0xFF080C14u;
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

/* ═══════════════════════════════════════════════════════════════════
 * Stage 1 First-Stage Console & Stage 2 B-System Workbench Session
 * Modelled on core_ps2.c: ps2_shell_exec / launch_ps2_desktop_session
 * Input: DWC2 USB keyboard only. No UART required.
 * ═══════════════════════════════════════════════════════════════════ */

static int  s_gui_active  = 0;   /* 1 = workbench is running */
static char s_cmd_buf[64];
static int  s_cmd_pos     = 0;

/* Forward declaration */
/* Forward declarations for InterCore non-blocking queues & scheduler */
void intercore_poll_signals(void);
void btron_scheduler_dispatch(void);

static void launch_pi4_desktop_session(uint32_t *gpu_fb);

static void pi4_shell_out(const char *text, COLOR col, void *user_data)
{
    (void)col; (void)user_data;
    if (!text) return;
    fb_log(text);
    fb_log("\n");
    uart_puts(text);
    uart_puts("\n");
}

static void pi4_gui_launcher_callback(void *ctx)
{
    uint32_t *gpu_fb = (uint32_t *)ctx;
    if (!s_gui_active && gpu_fb) {
        launch_pi4_desktop_session(gpu_fb);
    }
}

static void pi4_shell_exec(const char *cmd, uint32_t *gpu_fb)
{
    btron_register_gui_launcher(pi4_gui_launcher_callback, gpu_fb);

    if (tkl_strcmp(cmd, "exit") == 0 ||
        tkl_strcmp(cmd, "console") == 0 ||
        tkl_strcmp(cmd, "quit") == 0) {
        if (s_gui_active) {
            s_gui_active = 0;
        } else {
            fb_log("[CON] Already at Stage 1 console.\n");
        }
    } else if (tkl_strcmp(cmd, "gpu") == 0 || tkl_strcmp(cmd, "v3d") == 0) {
        const v3d_info_t *info = v3d_get_info();
        if (info && info->hardware_present) {
            fb_log("[V3D] VideoCore VI (V3D 4.2) 3D Pipeline: ACTIVE\n");
            fb_log("      Ident0 : 0x"); fb_log_hex32(info->ident0); fb_log("\n");
            fb_log("      Ident1 : 0x"); fb_log_hex32(info->ident1); fb_log("\n");
            fb_log("      QPUs   : ");   fb_log_dec(info->qpu_count); fb_log(" dual-issue SIMD16 cores\n");
            fb_log("      Clock  : ");   fb_log_dec(info->clock_hz / 1000000u); fb_log(" MHz\n");
            fb_log("      Status : Hardware CLE & PTB Tile Binning Ready\n");
        } else {
            fb_log("[V3D] VideoCore VI (V3D 4.2): Hardware core not present\n");
            fb_log("      Pipeline: TinyGL CPU software rasterizer active\n");
        }
    } else if (tkl_strcmp(cmd, "clear") == 0) {
        if (s_fb_log_fb) {
            for (int i = 0; i < BTRON_SCREEN_W * BTRON_SCREEN_H; i++)
                ((uint32_t *)s_fb_log_fb)[i] = 0xFF080C14u;
            s_fb_log_col = 0;
            s_fb_log_row = 0;
        }
    } else {
        shell_execute_cmd(cmd, pi4_shell_out, NULL, NULL);
    }
}

/* ─────────────────────────────────────────────────────────────────
 * Stage 1 Interactive Terminal Console: Keystroke Polling
 *
 * In Stage 1, BTRON boots into an interactive bare-metal diagnostics
 * and control console rendered directly onto the HDMI display.
 * Keystrokes are polled directly from:
 *   - xHCI Event Ring / Transfer Ring on physical BCM2711 (Pi 400 internal keyboard)
 *   - DWC2 USB Host Controller on emulated QEMU raspi3b / raspi4
 *   - PL011 UART serial line (if serial cable is attached)
 *
 * When the user types commands like 'startx' or 'gui', the system transitions
 * into the Stage 2 graphical Workbench desktop session.
 * ───────────────────────────────────────────────────────────────── */
static int pi4_shell_poll(uint32_t *gpu_fb)
{
    static uint8_t s_prev_scancode = 0;
    uint32_t k = 0;

    intercore_poll_signals();
    btron_scheduler_dispatch();

    /* Drain xHCI event ring FIRST (if on physical Pi 400 hardware).
     * When the 1 kHz IRQ input plane is armed, the ISR owns event-ring
     * draining and the shell must not touch it (ASYNC.txt §5: single
     * producer per ring). */
    if (g_use_xhci && !s_async_irq_active) {
        xhci_process_events();
    }

    /* 1. Poll USB Keyboard:
     *    On Pi 400 (BCM2711, mmio 0xFE000000), read HID reports from xHCI transfer ring.
     *    On QEMU / Pi 2 / Pi 3 (mmio 0x3F000000), read packets from DWC2 channel registers. */

    usb_kbd_report_t rep;
    int kbd_ready = 0;
    if (g_use_xhci) {
        kbd_ready = (xhci_poll_keyboard(&rep) > 0);
    } else {
        kbd_ready = (dwc2_poll_keyboard(&rep) > 0);
    }

    if (kbd_ready) {
        uint8_t sc = rep.keys[0];
        if (sc != s_prev_scancode) {
            s_prev_scancode = sc;
            if (sc != 0) {
                /* Translate standard USB HID scancode to ASCII / B-TRON character */
                k = dwc2_usb_to_btron_key(sc, rep.modifiers);
            }
        }
    }

    /* 2. Poll USB Mouse (motion/clicks in shell cancel autoboot).
     * With the IRQ plane armed, the ISR consumes the decoded report ring,
     * so the shell observes the seqlock snapshot instead of polling. */
    if (s_async_irq_active) {
        static uint16_t s_prev_motion_seq = 0;
        uint16_t mseq = s_pointer_slot.motion_seq;
        if (mseq != s_prev_motion_seq || s_pointer_slot.buttons != 0) {
            s_prev_motion_seq = mseq;
            return 1;
        }
    } else {
        usb_mouse_report_t mrep;
        int mouse_ready = 0;
        if (g_use_xhci) {
            mouse_ready = (xhci_poll_mouse(&mrep) > 0);
        } else {
            mouse_ready = (dwc2_poll_mouse(&mrep) > 0);
        }
        if (mouse_ready && (mrep.dx != 0 || mrep.dy != 0 || mrep.buttons != 0)) {
            return 1;
        }
    }

    /* 3. Poll UART Serial Console (if connected) */
    if (k == 0 && uart_has_char()) {
        int c = uart_getc();
        if (c == '\r') c = '\n';
        k = (uint32_t)(uint8_t)c;
    }

    if (k == 0) return 0;

    /* Pass keys through to workbench when GUI is active */
    if (s_gui_active) {
        EVT ev;
        ev.type   = EV_KEY_DOWN;
        ev.key    = k;
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.button = 0;
        ev.data   = 0;
        snd_evt(&ev);
        return 1;
    }

    /* Stage 1 console line editing */
    if (k == '\n' || k == '\r') {
        fb_log_putc('\n');
        uart_puts("\r\n");
        s_cmd_buf[s_cmd_pos] = '\0';
        pi4_shell_exec(s_cmd_buf, gpu_fb);
        s_cmd_pos = 0;
        /* Re-print prompt if still in Stage 1 */
        if (!s_gui_active) {
            fb_log("btron-pi400# ");
            uart_puts("btron-pi400# ");
        }
    } else if (k == 8 || k == 127) {       /* Backspace / DEL */
        if (s_cmd_pos > 0) {
            s_cmd_pos--;
            fb_log_backspace();
            uart_puts("\b \b");
        }
    } else if (k >= 0x20 && k < 0x7F &&
               s_cmd_pos < (int)sizeof(s_cmd_buf) - 1) {
        s_cmd_buf[s_cmd_pos++] = (char)k;
        fb_log_putc((char)k);
        uart_putc((char)k);
    }
    return 1;
}


/* ═══════════════════════════════════════════════════════════════════
 * B-System Mature Tested Compositor Desktop Session
 * ═══════════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════════
 * VideoCore GPU Framebuffer & Display Blitter (Single Physical Buffer)
 * ═══════════════════════════════════════════════════════════════════ */

void blit_backbuffer_to_fb(volatile uint32_t *gpu_fb) {
    if (!gpu_fb) return;
    uint32_t start_us = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    arm64_fast_blit((void *)gpu_fb, s_desktop_backbuffer,
                    BTRON_SCREEN_W * BTRON_SCREEN_H * sizeof(COLOR));
    s_async_rt_stats.blit_us = *(volatile uint32_t *)(TIMER_BASE + 0x04) - start_us;
    if (s_async_rt_stats.blit_us > s_async_rt_stats.blit_max_us)
        s_async_rt_stats.blit_max_us = s_async_rt_stats.blit_us;
}

static volatile uint32_t *s_active_gpu_fb = NULL;
static uint32_t s_last_paint_duration_us = 0;
static uint32_t s_uart_key_time_us = 0;
static UW       s_uart_held_key = 0;

static void poll_uart_input(uint32_t now) {
    if (uart_has_char()) {
        int c = uart_getc();
        if (c == 0x1B) {
            int wait_tries = 2000;
            while (!uart_has_char() && --wait_tries > 0) {
                __asm__ volatile("nop");
            }
            if (uart_has_char() && uart_getc() == '[') {
                wait_tries = 2000;
                while (!uart_has_char() && --wait_tries > 0) {
                    __asm__ volatile("nop");
                }
                if (uart_has_char()) {
                    int dir = uart_getc();
                    if (dir == 'A') s_mouse_y = (s_mouse_y > 16) ? s_mouse_y - 16 : 10;
                    else if (dir == 'B') s_mouse_y = (s_mouse_y < BTRON_SCREEN_H - 20) ? s_mouse_y + 16 : BTRON_SCREEN_H - 20;
                    else if (dir == 'C') s_mouse_x = (s_mouse_x < BTRON_SCREEN_W - 20) ? s_mouse_x + 16 : BTRON_SCREEN_W - 20;
                    else if (dir == 'D') s_mouse_x = (s_mouse_x > 16) ? s_mouse_x - 16 : 10;
                    set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);
                    EVT mev;
                    mev.type   = EV_MOUSE_MOVE;
                    mev.pos.x  = s_mouse_x;
                    mev.pos.y  = s_mouse_y;
                    mev.button = 0;
                    mev.data   = 0;
                    snd_evt(&mev);
                }
            } else {
                if (s_uart_held_key != 0 && s_uart_held_key != 0x1B) {
                    EVT uev; uev.type = EV_KEY_UP; uev.key = s_uart_held_key; uev.data = 0;
                    uev.pos.x = s_mouse_x; uev.pos.y = s_mouse_y; uev.button = 0; snd_evt(&uev);
                }
                s_uart_held_key = 0x1B;
                s_uart_key_time_us = now;
                EVT ev; ev.type = EV_KEY_DOWN; ev.key = 0x1B; ev.pos.x = s_mouse_x; ev.pos.y = s_mouse_y; ev.button = 0; ev.data = 0; snd_evt(&ev);
            }
        } else {
            if (c == '\r') c = '\n';
            UW k = (UW)(uint8_t)c;
            if (s_uart_held_key != 0 && s_uart_held_key != k) {
                EVT uev; uev.type = EV_KEY_UP; uev.key = s_uart_held_key; uev.data = 0;
                uev.pos.x = s_mouse_x; uev.pos.y = s_mouse_y; uev.button = 0; snd_evt(&uev);
            }
            s_uart_held_key = k;
            s_uart_key_time_us = now;
            EVT ev; ev.type = EV_KEY_DOWN; ev.key = k; ev.pos.x = s_mouse_x; ev.pos.y = s_mouse_y; ev.button = 0; ev.data = 0; snd_evt(&ev);
        }
    }

    if (s_uart_held_key != 0 && (now - s_uart_key_time_us) >= 150000u) {
        EVT ev; ev.type = EV_KEY_UP; ev.key = s_uart_held_key; ev.data = 0;
        ev.pos.x = s_mouse_x; ev.pos.y = s_mouse_y; ev.button = 0; snd_evt(&ev);
        s_uart_held_key = 0;
    }
}

void btron_cooperative_input_pump(void) {
    if (!s_gui_active) return;
    uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    if (g_use_xhci) {
        if (!s_async_irq_active) {
            xhci_process_events_bounded(ASYNC_XHCI_TRB_BUDGET);
        }
        input_plane_task(NULL, now);
    } else {
        usb_poll_devices(NULL);
    }
    poll_uart_input(now);

    /* Drain and dispatch events immediately for zero-lag response */
    EVT ev;
    while (get_evt(&ev, 0) == E_OK) {
        workbench_process_event(NULL, &ev);
    }
}

/* Stage 2: Launch full B-System Workbench desktop session. */
static void launch_pi4_desktop_session(uint32_t *gpu_fb)
{
    fb_log("\n[BOOT] Launching B-System Workbench...\n");
    s_gui_active = 1;

    /* Clean double-buffered atomic cursor: eliminates square bounding box & flicker */
    g_cursor_in_backbuffer = 1;
    s_active_gpu_fb = gpu_fb;

    GDEV *screen = init_baremetal_desktop(
        (uint32_t *)s_desktop_backbuffer, BTRON_SCREEN_W, BTRON_SCREEN_H);
    if (!screen) {
        fb_log("[FATAL] Desktop init failed.\n");
        s_gui_active = 0;
        return;
    }
    workbench_init(BTRON_SCREEN_W);
    set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);
    workbench_render(screen, BTRON_SCREEN_W, BTRON_SCREEN_H);
    blit_backbuffer_to_fb(gpu_fb);

    H prev_mx = s_mouse_x;
    H prev_my = s_mouse_y;

    /* Disable on-screen log: workbench owns the framebuffer now */
    fb_log_enable(NULL);

    uart_puts("[WB]  Workbench live. Press [Esc] or type 'exit' to return.\n");

    g_prev_kbd_scancode = 0;
    s_held_kbd_scancode = 0;
    s_held_kbd_modifiers = 0;
    g_prev_mouse_btns = 0;

    /* Resync the seqlock consumer baseline so Stage-1 pointer motion cannot
     * burst into the first GUI frame as one giant delta. */
    s_input_prev_buttons = s_pointer_slot.buttons;
    s_button_edge_tail = s_button_edge_head;
    s_input_last.acc_dx = s_pointer_slot.acc_dx;
    s_input_last.acc_dy = s_pointer_slot.acc_dy;
    s_input_last.acc_wheel = s_pointer_slot.acc_wheel;
    s_input_last.buttons = s_pointer_slot.buttons;

    uint32_t last_clock = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    uint32_t last_usb_poll = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    uint32_t last_present = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    int pending_redraw = 0;

    EVT ev;

    while (s_gui_active) {
        uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);

        /* 1. Poll USB / input devices (ASYNC.txt 1 ms cadence) */
        if (g_use_xhci) {
            if (!s_async_irq_active) {
                xhci_process_events_bounded(ASYNC_XHCI_TRB_BUDGET);
            }
            input_plane_task(screen, now);
        } else {
            if ((now - last_usb_poll) >= ASYNC_INPUT_PERIOD_US) {
                last_usb_poll = now;
                usb_poll_devices(screen);
            }
        }

        /* 2. Poll PL011 UART for interactive keys & arrows */
        poll_uart_input(now);

        /* 3. Dispatch queued BTRON events */
        while (get_evt(&ev, 0) == E_OK) {
            workbench_process_event(screen, &ev);
            if (ev.type != EV_MOUSE_MOVE || ev.button != 0 ||
                wnd_mgr_is_interacting() || ev.pos.y <= 25 ||
                global_menu_is_open() || tracker_is_menu_open()) {
                pending_redraw = 1;
            }
        }
        if (!s_gui_active) break;

        orchestra_ui_tick();
        cassette_service();

        /* 4. Non-blocking InterCore queue poll & non-blocking task scheduler */
        intercore_poll_signals();
        btron_scheduler_dispatch();

        /* 5. Detect state changes that require composite update */
        if (wnd_mgr_flush_resize()) pending_redraw = 1;
        if (wnd_any_image_invalid()) pending_redraw = 1;
        if (s_mouse_x != prev_mx || s_mouse_y != prev_my) {
            set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);
            pending_redraw = 1;
        }

        if ((now - last_clock) >= 1000000) {
            last_clock = now;
            pending_redraw = 1;
        }

        /* ASYNC presentation plane: budgeted cadence (ASYNC_UI_PERIOD_US) */
        uint32_t ui_interval = ASYNC_UI_PERIOD_US;
        if (s_last_paint_duration_us > 15000) {
            /* Heavy software 3D paint detected (e.g. Quake / XMB taking >15 ms).
             * Throttle to budgeted slice cadence (~30 Hz / 33 ms) so the CPU is not
             * 100% starved and the mouse task maintains dedicated budget. */
            ui_interval = 33333u;
        }

        if (pending_redraw && ((now - last_present) >= ui_interval)) {
            uint32_t t_render_start = *(volatile uint32_t *)(TIMER_BASE + 0x04);
            last_present = now;
            pending_redraw = 0;
            prev_mx = s_mouse_x;
            prev_my = s_mouse_y;
            workbench_render(screen, BTRON_SCREEN_W, BTRON_SCREEN_H);
            blit_backbuffer_to_fb(gpu_fb);
            s_last_paint_duration_us = *(volatile uint32_t *)(TIMER_BASE + 0x04) - t_render_start;
        }

        __asm__ volatile("dsb sy" : : : "memory");
    }

    s_active_gpu_fb = NULL;
    g_cursor_in_backbuffer = 1;

    /* Return to Stage 1 console */
    fb_log_enable((volatile uint32_t *)gpu_fb);
    for (int i = 0; i < BTRON_SCREEN_W * BTRON_SCREEN_H; i++)
        ((uint32_t *)gpu_fb)[i] = 0xFF0A0F18u;
    s_fb_log_col = 0; s_fb_log_row = 0;

    fb_log("+===============================================================+\n");
    fb_log("|  [BTRON] Exited Graphical Workbench Session                   |\n");
    fb_log("|  [BTRON] Returned to Stage 1 Terminal Console (1024x768)      |\n");
    fb_log("|  Type 'startx' or 'gui' to launch GUI session again.          |\n");
    fb_log("+===============================================================+\n\n");
    fb_log("btron-pi400# ");
}

/* ═══════════════════════════════════════════════════════════════════
 * Formatted Kernel Output: kprintf
 * ═══════════════════════════════════════════════════════════════════ */

static void print_num(uint32_t num, int base, int width, char pad) {
    char buf[32];
    int i = 0;
    const char digits[] = "0123456789ABCDEF";

    if (num == 0) {
        buf[i++] = '0';
    } else {
        while (num > 0) {
            buf[i++] = digits[num % base];
            num /= base;
        }
    }

    while (i < width) {
        buf[i++] = pad;
    }

    for (int j = i - 1; j >= 0; j--) {
        uart_putc(buf[j]);
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (*p == '\n') uart_putc('\r');
            uart_putc(*p);
            continue;
        }
        p++;
        int width = 0;
        char pad = ' ';
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                uart_puts(s ? s : "(null)");
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(ap, int32_t);
                if (val < 0) {
                    uart_putc('-');
                    val = -val;
                }
                print_num((uint32_t)val, 10, width, pad);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(ap, uint32_t);
                print_num(val, 10, width, pad);
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                uint32_t val = va_arg(ap, uint32_t);
                print_num(val, 16, width, pad);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                uart_putc(c);
                break;
            }
            case '%':
                uart_putc('%');
                break;
            default:
                uart_putc('%');
                uart_putc(*p);
                break;
        }
    }
    va_end(ap);
}


/* ═══════════════════════════════════════════════════════════════════
 * BCM2711 / BCM2837 System Timer & 60Hz Tick
 * ═══════════════════════════════════════════════════════════════════ */

static volatile uint32_t s_system_ticks = 0;

/* ═══════════════════════════════════════════════════════════════════
 * ASYNC.txt IRQ Input Plane (input_plane_on_irq)
 *
 * Runs in real 1 kHz timer interrupt context (GIC-400 PPI 30, armed by
 * arm64_irq_init after xhci_init).  Strictly bounded:
 *   1. drain at most ASYNC_ISR_TRB_BUDGET xHCI TRBs into SPSC rings
 *   2. fold at most ASYNC_ISR_MOUSE_BUDGET decoded mouse reports into the
 *      wait-free seqlock pointer snapshot
 *   3. record cadence/WCET telemetry and return
 * No rendering, no mailbox calls, no allocation, no event dispatch.
 * ═══════════════════════════════════════════════════════════════════ */
void rpi_timer_tick(void) {
    s_system_ticks++;
    if (!s_async_irq_active) return;

    uint32_t t0 = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    if (s_irq_prev_us != 0) {
        uint32_t gap = t0 - s_irq_prev_us;
        s_async_rt_stats.input_gap_us = gap;
        if (gap > s_async_rt_stats.input_gap_max_us)
            s_async_rt_stats.input_gap_max_us = gap;
    }
    s_irq_prev_us = t0;

    if (g_use_xhci) {
        uint32_t trbs = xhci_process_events_bounded(ASYNC_ISR_TRB_BUDGET);
        s_async_rt_stats.trb_per_input = trbs;
        if (trbs > s_async_rt_stats.trb_max)
            s_async_rt_stats.trb_max = trbs;
    }

    /* Fold decoded mouse reports into the monotonic seqlock accumulators.
     * The INPUT worker diffs against its own last-consumed snapshot, so
     * batches coalesce losslessly without a reset handshake. */
    usb_mouse_report_t rep;
    int32_t dx = 0, dy = 0, wheel = 0;
    uint8_t buttons = 0;
    int got = 0;
    for (uint32_t i = 0; i < ASYNC_ISR_MOUSE_BUDGET && xhci_poll_mouse(&rep); i++) {
        dx += (int32_t)rep.dx;
        dy += (int32_t)rep.dy;
        wheel += (int32_t)rep.wheel;
        if (rep.buttons != buttons) {
            uint16_t next = (uint16_t)((s_button_edge_head + 1u) &
                                       (BUTTON_EDGE_RING_SIZE - 1u));
            if (next != s_button_edge_tail) {
                s_button_edge_ring[s_button_edge_head] = rep.buttons;
                __asm__ volatile("dmb sy" : : : "memory");
                s_button_edge_head = next;
            }
        }
        buttons = rep.buttons;
        got = 1;
    }
    if (got) {
        uint32_t seq = s_pointer_slot.seq;
        s_pointer_slot.seq = seq + 1;             /* odd: update in progress */
        __asm__ volatile("dmb sy" : : : "memory");
        s_pointer_slot.acc_dx += dx;
        s_pointer_slot.acc_dy += dy;
        s_pointer_slot.acc_wheel += wheel;
        s_pointer_slot.buttons = buttons;
        s_pointer_slot.present = 1;
        if (dx != 0 || dy != 0) s_pointer_slot.motion_seq++;
        __asm__ volatile("dmb sy" : : : "memory");
        s_pointer_slot.seq = seq + 2;             /* even: stable */
    }

    uint32_t isr_us = *(volatile uint32_t *)(TIMER_BASE + 0x04) - t0;
    s_async_rt_stats.isr_us = isr_us;
    if (isr_us > s_async_rt_stats.isr_max_us)
        s_async_rt_stats.isr_max_us = isr_us;
}

/* ═══════════════════════════════════════════════════════════════════
 * InterCore Non-Blocking Queues & Signaling (doc/txt/ASYNC.txt)
 * Lock-free SPSC/MPSC ring topology with two-phase memory barrier commit.
 * ═══════════════════════════════════════════════════════════════════ */

#define MAX_ARM64_TASKS         8
#define ARM64_TASK_STACK_SIZE   24576u  /* 24 KB stack per worker task */

#define INTERCORE_QUEUE_CAPACITY 256u
#define INTERCORE_QUEUE_MASK     (INTERCORE_QUEUE_CAPACITY - 1u)

typedef struct {
    uint32_t type;    /* Signal/Event type (e.g. EVT_INPUT, SIG_WAKEUP, etc.) */
    uint32_t sender;  /* Sending task/core ID */
    uint32_t target;  /* Target task/core ID */
    uint32_t seq;     /* Monotonic sequence stamp */
    uint64_t data0;   /* Signal payload argument 0 */
    uint64_t data1;   /* Signal payload argument 1 */
} intercore_msg_t;

typedef struct {
    intercore_msg_t   slots[INTERCORE_QUEUE_CAPACITY];
    volatile uint32_t head;   /* Consumer read cursor */
    volatile uint32_t tail;   /* Producer write cursor */
    volatile uint32_t drops;  /* Drops under full capacity */
} intercore_queue_t;

static intercore_queue_t s_intercore_input_queue;
static intercore_queue_t s_intercore_task_queues[MAX_ARM64_TASKS];

int intercore_send(intercore_queue_t *q, const intercore_msg_t *msg) {
    if (!q || !msg) return -1;
    uint32_t head = q->head;
    uint32_t tail = q->tail;
    if ((tail - head) >= INTERCORE_QUEUE_CAPACITY) {
        q->drops++;
        return -2; /* SNDR_FULL: non-blocking drop */
    }
    uint32_t idx = tail & INTERCORE_QUEUE_MASK;
    q->slots[idx] = *msg;
    __asm__ volatile("dmb sy" : : : "memory");
    q->tail = tail + 1;
    return 0; /* SNDR_OK */
}

int intercore_recv(intercore_queue_t *q, intercore_msg_t *out_msg) {
    if (!q || !out_msg) return -1;
    uint32_t head = q->head;
    uint32_t tail = q->tail;
    if (head == tail) {
        return 0; /* RCVR_EMPTY: non-blocking empty */
    }
    uint32_t idx = head & INTERCORE_QUEUE_MASK;
    *out_msg = q->slots[idx];
    __asm__ volatile("dmb sy" : : : "memory");
    q->head = head + 1;
    return 1; /* RCVR_DATA */
}

int intercore_signal_task(ID tskid, uint32_t sig_type, uint64_t data0, uint64_t data1) {
    if (tskid <= 0 || tskid > MAX_ARM64_TASKS) return -1;
    intercore_msg_t msg;
    msg.type = sig_type;
    msg.sender = 0;
    msg.target = (uint32_t)tskid;
    msg.seq = s_system_ticks;
    msg.data0 = data0;
    msg.data1 = data1;
    int rc = intercore_send(&s_intercore_task_queues[tskid - 1], &msg);
    if (rc == 0) {
        wup_tsk(tskid);
    }
    return rc;
}

void intercore_poll_signals(void) {
    intercore_msg_t msg;
    while (intercore_recv(&s_intercore_input_queue, &msg) == 1) {
        if (msg.target > 0 && msg.target <= MAX_ARM64_TASKS) {
            intercore_send(&s_intercore_task_queues[msg.target - 1], &msg);
            wup_tsk((ID)msg.target);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * AArch64 Callee-Saved Context Switch (Zero Preemption Overhead)
 * Preserves AAPCS64 x19-x30 and d8-d15 with strict 16-byte SP alignment.
 * ═══════════════════════════════════════════════════════════════════ */

void btron_ctx_switch(void **old_sp, void *new_sp);

__asm__(
    ".text\n"
    ".align 4\n"
    ".global btron_ctx_switch\n"
    ".type btron_ctx_switch, %function\n"
    "btron_ctx_switch:\n"
    "    sub sp, sp, #160\n"
    "    stp x19, x20, [sp, #0]\n"
    "    stp x21, x22, [sp, #16]\n"
    "    stp x23, x24, [sp, #32]\n"
    "    stp x25, x26, [sp, #48]\n"
    "    stp x27, x28, [sp, #64]\n"
    "    stp x29, x30, [sp, #80]\n"
    "    stp d8,  d9,  [sp, #96]\n"
    "    stp d10, d11, [sp, #112]\n"
    "    stp d12, d13, [sp, #128]\n"
    "    stp d14, d15, [sp, #144]\n"
    "    mov x2, sp\n"
    "    str x2, [x0]\n"
    "    mov sp, x1\n"
    "    ldp d14, d15, [sp, #144]\n"
    "    ldp d12, d13, [sp, #128]\n"
    "    ldp d10, d11, [sp, #112]\n"
    "    ldp d8,  d9,  [sp, #96]\n"
    "    ldp x29, x30, [sp, #80]\n"
    "    ldp x27, x28, [sp, #64]\n"
    "    ldp x25, x26, [sp, #48]\n"
    "    ldp x23, x24, [sp, #32]\n"
    "    ldp x21, x22, [sp, #16]\n"
    "    ldp x19, x20, [sp, #0]\n"
    "    add sp, sp, #160\n"
    "    ret\n"
);

/* ═══════════════════════════════════════════════════════════════════
 * AArch64 Non-Blocking Cooperative Scheduler (μITRON 2.0 / 3.20)
 * Allows worker tasks (Lil Cu 64, XMB, GLGears, MARS, Quake, MSX)
 * to run concurrently with zero locks and zero priority inversion.
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    ID          tskid;
    T_CTSK      config;
    int         active;
    int         started;
    int         runnable;
    int         sleeping;
    uint32_t    wake_us;
    void       *sp;
    uint8_t    *stack;
} ARM64_TASK;

static uint8_t s_arm64_task_stacks[MAX_ARM64_TASKS][ARM64_TASK_STACK_SIZE] __attribute__((aligned(16)));
static ARM64_TASK s_arm64_tasks[MAX_ARM64_TASKS];
static void *s_scheduler_sp = NULL;
static int s_current_task_idx = -1;

static void btron_task_trampoline(void) {
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_ARM64_TASKS && s_arm64_tasks[idx].config.task) {
        s_arm64_tasks[idx].config.task(s_arm64_tasks[idx].config.exinf);
    }
    ext_tsk();
}

ID cre_tsk(const T_CTSK *pk_ctsk) {
    if (!pk_ctsk || !pk_ctsk->task) return E_PAR;
    for (int i = 0; i < MAX_ARM64_TASKS; i++) {
        if (!s_arm64_tasks[i].active) {
            uint8_t *stk_top = s_arm64_task_stacks[i] + ARM64_TASK_STACK_SIZE;
            stk_top = (uint8_t *)((uintptr_t)stk_top & ~0xFULL);
            stk_top -= 160;
            uint64_t *frame = (uint64_t *)stk_top;
            for (int k = 0; k < 20; k++) frame[k] = 0;
            frame[10] = 0; /* x29 (fp) */
            frame[11] = (uint64_t)(uintptr_t)btron_task_trampoline; /* x30 (lr) */

            s_arm64_tasks[i].sp = (void *)stk_top;
            s_arm64_tasks[i].stack = s_arm64_task_stacks[i];
            s_arm64_tasks[i].tskid = i + 1;
            s_arm64_tasks[i].config = *pk_ctsk;
            s_arm64_tasks[i].active = 1;
            s_arm64_tasks[i].started = 0;
            s_arm64_tasks[i].runnable = 0;
            s_arm64_tasks[i].sleeping = 0;
            s_arm64_tasks[i].wake_us = 0;
            return i + 1;
        }
    }
    return E_LIMIT;
}

ER sta_tsk(ID tskid, VW exinf) {
    if (tskid <= 0 || tskid > MAX_ARM64_TASKS) return E_ID;
    int idx = tskid - 1;
    if (!s_arm64_tasks[idx].active) return E_NOEXS;
    if (exinf != 0) s_arm64_tasks[idx].config.exinf = exinf;
    s_arm64_tasks[idx].started = 1;
    s_arm64_tasks[idx].runnable = 1;
    s_arm64_tasks[idx].sleeping = 0;
    return E_OK;
}

void ext_tsk(void) {
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_ARM64_TASKS) {
        s_arm64_tasks[idx].active = 0;
        s_arm64_tasks[idx].started = 0;
        s_arm64_tasks[idx].runnable = 0;
        s_arm64_tasks[idx].sleeping = 0;
    }
    s_current_task_idx = -1;
    if (s_scheduler_sp) {
        void *dummy = NULL;
        btron_ctx_switch(&dummy, s_scheduler_sp);
    }
    for (;;) {
        __asm__ volatile("wfe");
    }
}

void exd_tsk(void) {
    ext_tsk();
}

ER slp_tsk(void) {
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_ARM64_TASKS) {
        s_arm64_tasks[idx].sleeping = 1;
        s_arm64_tasks[idx].runnable = 0;
        s_arm64_tasks[idx].wake_us = 0xFFFFFFFFu;
        s_current_task_idx = -1;
        if (s_scheduler_sp) {
            btron_ctx_switch(&s_arm64_tasks[idx].sp, s_scheduler_sp);
        }
    }
    return E_OK;
}

ER wup_tsk(ID tskid) {
    if (tskid <= 0 || tskid > MAX_ARM64_TASKS) return E_ID;
    int idx = tskid - 1;
    if (!s_arm64_tasks[idx].active) return E_NOEXS;
    s_arm64_tasks[idx].sleeping = 0;
    s_arm64_tasks[idx].runnable = 1;
    return E_OK;
}

void dly_tsk(W dlytim) {
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_ARM64_TASKS) {
        uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);
        s_arm64_tasks[idx].sleeping = 1;
        s_arm64_tasks[idx].runnable = 0;
        s_arm64_tasks[idx].wake_us = now + (uint32_t)(dlytim > 0 ? dlytim : 1) * 1000U;
        s_current_task_idx = -1;
        if (s_scheduler_sp) {
            btron_ctx_switch(&s_arm64_tasks[idx].sp, s_scheduler_sp);
        }
    } else {
        uint32_t start = *(volatile uint32_t *)(TIMER_BASE + 0x04);
        uint32_t budget = (uint32_t)(dlytim > 0 ? dlytim : 1) * 1000U;
        while ((*(volatile uint32_t *)(TIMER_BASE + 0x04) - start) < budget) {
            __asm__ volatile("nop");
        }
    }
}

ER tk_dly_tsk(W dlytim) {
    dly_tsk(dlytim);
    return E_OK;
}

ER get_tim(SYSTIME *p_time) {
    if (!p_time) return E_PAR;
    uint32_t us = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    *p_time = (uint64_t)(us / 1000u);
    return E_OK;
}

W btron_list_tasks(BTRON_TSK_ROW *out, W max) {
    W n = 0;
    if (!out || max <= 0) return 0;

    /* PID 1: Real-time Input Worker Plane */
    if (n < max) {
        out[n].tskid = 1;
        out[n].name  = "pi4_input";
        out[n].pri   = 1;
        out[n].entry = (FP)(void *)input_plane_task;
        out[n].state = BTRON_TSK_RUN;
        n++;
    }

    /* PID 2: Desktop Presentation Plane */
    if (n < max) {
        out[n].tskid = 2;
        out[n].name  = "pi4_desktop";
        out[n].pri   = 5;
        out[n].entry = (FP)(void *)launch_pi4_desktop_session;
        out[n].state = s_gui_active ? BTRON_TSK_RUN : BTRON_TSK_SLEEP;
        n++;
    }

    /* PID 3: Serial Console Shell Dispatcher */
    if (n < max) {
        out[n].tskid = 3;
        out[n].name  = "pi4_shell";
        out[n].pri   = 4;
        out[n].entry = (FP)(void *)poll_uart_input;
        out[n].state = s_gui_active ? BTRON_TSK_SLEEP : BTRON_TSK_RUN;
        n++;
    }

    /* Dynamic Application Worker Tasks (PIDs 4+) */
    for (int i = 0; i < MAX_ARM64_TASKS && n < max; i++) {
        const ARM64_TASK *t = &s_arm64_tasks[i];
        if (!t->active) continue;
        out[n].tskid = (ID)(n + 1);
        out[n].name  = t->config.name ? t->config.name : "(worker)";
        out[n].pri   = t->config.itskpri;
        out[n].entry = t->config.task;
        if (!t->started) {
            out[n].state = BTRON_TSK_NEW;
        } else if (t->sleeping) {
            out[n].state = BTRON_TSK_SLEEP;
        } else if (t->runnable) {
            out[n].state = BTRON_TSK_RUN;
        } else {
            out[n].state = BTRON_TSK_DEAD;
        }
        n++;
    }
    return n;
}

void btron_scheduler_dispatch(void) {
    uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    for (int i = 0; i < MAX_ARM64_TASKS; i++) {
        if (!s_arm64_tasks[i].active || !s_arm64_tasks[i].started) continue;
        if (s_arm64_tasks[i].sleeping) {
            if (s_arm64_tasks[i].wake_us != 0xFFFFFFFFu &&
                (int32_t)(now - s_arm64_tasks[i].wake_us) >= 0) {
                s_arm64_tasks[i].sleeping = 0;
                s_arm64_tasks[i].runnable = 1;
            } else {
                continue;
            }
        }
        if (s_arm64_tasks[i].runnable) {
            s_current_task_idx = i;
            btron_ctx_switch(&s_scheduler_sp, s_arm64_tasks[i].sp);
            s_current_task_idx = -1;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Synopsys DWC2 USB Keyboard & Mouse Driver
 * ═══════════════════════════════════════════════════════════════════ */

static inline uint16_t usb_to_btron_modifiers(uint8_t usb_mod) {
    uint16_t bmod = BTRON_KMOD_NONE;
    if (usb_mod & 0x01) bmod |= BTRON_KMOD_LCTRL;
    if (usb_mod & 0x02) bmod |= BTRON_KMOD_LSHIFT;
    if (usb_mod & 0x04) bmod |= BTRON_KMOD_LALT;
    if (usb_mod & 0x10) bmod |= BTRON_KMOD_RCTRL;
    if (usb_mod & 0x20) bmod |= BTRON_KMOD_RSHIFT;
    if (usb_mod & 0x40) bmod |= BTRON_KMOD_RALT;
    return bmod;
}

/* Sub-pixel residual motion accumulator in 8.8 fixed-point */
static int32_t s_mouse_sub_x = 0;
static int32_t s_mouse_sub_y = 0;



/* Subpixel residual history for Haiku OS curve */
static float s_haiku_hist_x = 0.0f;
static float s_haiku_hist_y = 0.0f;

/* Acceleration Profile: 1 = RISC OS Stepped Curve, 2 = Haiku Continuous Smooth Curve (defined in input.c) */
extern int g_mouse_accel_profile;

static inline float fast_sqrtf(float val) {
    if (val <= 0.0f) return 0.0f;
#if defined(__aarch64__)
    float res;
    __asm__ volatile("fsqrt %s0, %s1" : "=w"(res) : "w"(val));
    return res;
#elif defined(__arm__) && defined(__ARM_FP)
    float res;
    __asm__ volatile("vsqrt.f32 %0, %1" : "=t"(res) : "t"(val));
    return res;
#else
    float x = val;
    float y = 0.5f * (x + 1.0f);
    for (int i = 0; i < 6; i++) {
        y = 0.5f * (y + x / y);
    }
    return y;
#endif
}

/*
 * Haiku OS / BeOS Continuous 2D Mouse Accelerator
 * Directly replicates MouseDevice::_ComputeAcceleration from Haiku OS
 * (src/add-ons/input_server/devices/mouse/MouseInputDevice.cpp)
 *
 * - speed: 65536 = 1.0x (16.16 fixed-point)
 * - accel_factor: 65536 to 262144 (scaled by 524288.0)
 * - Uses 2D velocity magnitude sqrt(dx*dx + dy*dy) for isotropic acceleration
 * - Truncates symmetrically towards zero (floor for positive, ceil for negative)
 * - Retains signed fractional residual without directional bias
 */
void mouse_accelerate_pair_haiku(int32_t raw_x, int32_t raw_y, int32_t *out_dx, int32_t *out_dy)
{
    if (raw_x == 0 && raw_y == 0) {
        /* Idle residual decay */
        s_haiku_hist_x *= 0.5f;
        s_haiku_hist_y *= 0.5f;
        if (s_haiku_hist_x > -0.05f && s_haiku_hist_x < 0.05f) s_haiku_hist_x = 0.0f;
        if (s_haiku_hist_y > -0.05f && s_haiku_hist_y < 0.05f) s_haiku_hist_y = 0.0f;
        if (out_dx) *out_dx = 0;
        if (out_dy) *out_dy = 0;
        return;
    }

    /* Clamp raw input bursts */
    if (raw_x > 512)  raw_x = 512;
    if (raw_x < -512) raw_x = -512;
    if (raw_y > 512)  raw_y = 512;
    if (raw_y < -512) raw_y = -512;

    /* Base speed: mapped from g_mouse_step_mult (1..4 -> 1.6x .. 2.8x) */
    float speed_mult = 1.2f + (float)g_mouse_step_mult * 0.4f;

    float deltaX = ((float)raw_x * speed_mult) + s_haiku_hist_x;
    float deltaY = ((float)raw_y * speed_mult) + s_haiku_hist_y;

    /* Haiku continuous acceleration curve */
    float accel_scale = 0.06f + ((float)g_mouse_step_mult * 0.03f);
    float speed_sq = (deltaX * deltaX) + (deltaY * deltaY);
    float speed_mag = fast_sqrtf(speed_sq);

    float acceleration = 1.0f + (speed_mag * accel_scale);
    if (acceleration > 4.5f) {
        acceleration = 4.5f;
    }

    deltaX *= acceleration;
    deltaY *= acceleration;

    /* Haiku integer quantization: floor for positive, ceil for negative */
    int32_t pix_x = (deltaX >= 0.0f) ? (int32_t)deltaX : -(int32_t)(-deltaX);
    int32_t pix_y = (deltaY >= 0.0f) ? (int32_t)deltaY : -(int32_t)(-deltaY);

    /* Output displacement clamp */
    if (pix_x > 512)  pix_x = 512;
    if (pix_x < -512) pix_x = -512;
    if (pix_y > 512)  pix_y = 512;
    if (pix_y < -512) pix_y = -512;

    s_haiku_hist_x = deltaX - (float)pix_x;
    s_haiku_hist_y = deltaY - (float)pix_y;

    /* Clamp residual within [-1.0f, +1.0f] */
    if (s_haiku_hist_x > 1.0f) s_haiku_hist_x = 1.0f;
    if (s_haiku_hist_x < -1.0f) s_haiku_hist_x = -1.0f;
    if (s_haiku_hist_y > 1.0f) s_haiku_hist_y = 1.0f;
    if (s_haiku_hist_y < -1.0f) s_haiku_hist_y = -1.0f;

    if (out_dx) *out_dx = pix_x;
    if (out_dy) *out_dy = pix_y;
}

static inline int32_t mouse_accelerate_subpixel_raw(int32_t raw, int32_t *subpixel) {
    (void)subpixel;
    return raw;          // pure 1:1, no residual, no boost, no mult
}

/* ─────────────────────────────────────────────────────────────────
 * Shared HID integration helpers.
 * Used by BOTH the legacy cooperative drainer (usb_poll_devices) and the
 * ASYNC.txt 1 ms INPUT worker (input_plane_task).  These run in task
 * context only — never in the ISR.
 * ───────────────────────────────────────────────────────────────── */

static uint8_t s_prev_kbd_keys[6] = {0};

/* 6-Key Rollover (6KRO) decoded keyboard report -> KEY_DOWN/KEY_UP events + repeat arming. */
static void kbd_report_to_events(const usb_kbd_report_t *rep, uint32_t now_us) {
    if (!rep) return;

    /* 1. Detect released keys: present in s_prev_kbd_keys but missing from rep->keys */
    for (int i = 0; i < 6; i++) {
        uint8_t old_k = s_prev_kbd_keys[i];
        if (old_k == 0) continue;
        bool still_down = false;
        for (int j = 0; j < 6; j++) {
            if (rep->keys[j] == old_k) {
                still_down = true;
                break;
            }
        }
        if (!still_down) {
            uint32_t k = dwc2_usb_to_btron_key(old_k, 0);
            if (k != 0) {
                EVT ev;
                ev.type   = EV_KEY_UP;
                ev.key    = k;
                ev.data   = 0;
                ev.pos.x  = s_mouse_x;
                ev.pos.y  = s_mouse_y;
                ev.button = 0;
                snd_evt(&ev);
            }
        }
    }

    /* 2. Detect newly pressed keys: present in rep->keys but missing from s_prev_kbd_keys */
    for (int i = 0; i < 6; i++) {
        uint8_t new_k = rep->keys[i];
        if (new_k == 0) continue;
        bool was_down = false;
        for (int j = 0; j < 6; j++) {
            if (s_prev_kbd_keys[j] == new_k) {
                was_down = true;
                break;
            }
        }
        if (!was_down) {
            uint32_t k = dwc2_usb_to_btron_key(new_k, rep->modifiers);
            if (k != 0) {
                uint16_t bmod = usb_to_btron_modifiers(rep->modifiers);
                EVT ev;
                ev.type   = EV_KEY_DOWN;
                ev.key    = k;
                ev.data   = (VW)(uintptr_t)bmod;
                ev.pos.x  = s_mouse_x;
                ev.pos.y  = s_mouse_y;
                ev.button = 0;
                snd_evt(&ev);
                s_async_rt_stats.key_enqueue_us = now_us;

                /* Arm hardware-like responsive key repeat for latest pressed key */
                s_held_kbd_scancode = new_k;
                s_held_kbd_modifiers = rep->modifiers;
                s_key_press_time_us = now_us;
                s_key_last_repeat_us = now_us;
            }
        }
    }

    /* 3. Disarm or retarget key repeat if held key is no longer pressed */
    if (s_held_kbd_scancode != 0) {
        bool held_found = false;
        for (int i = 0; i < 6; i++) {
            if (rep->keys[i] == s_held_kbd_scancode) {
                held_found = true;
                break;
            }
        }
        if (!held_found) {
            s_held_kbd_scancode = rep->keys[0];
            s_held_kbd_modifiers = rep->modifiers;
            s_key_press_time_us = now_us;
            s_key_last_repeat_us = now_us;
        }
    }

    /* 4. Update previous keys snapshot */
    for (int i = 0; i < 6; i++) {
        s_prev_kbd_keys[i] = rep->keys[i];
    }
    g_prev_kbd_scancode = rep->keys[0];
}

/* Key auto-repeat timer for the currently held key. */
static void kbd_repeat_check(uint32_t now_us) {
    if (!g_kbd_repeat_enabled || s_held_kbd_scancode == 0) return;
    if ((now_us - s_key_press_time_us) < g_kbd_repeat_delay_us) return;
    if ((now_us - s_key_last_repeat_us) < g_kbd_repeat_interval_us) return;
    s_key_last_repeat_us = now_us;
    uint32_t k = dwc2_usb_to_btron_key(s_held_kbd_scancode, s_held_kbd_modifiers);
    uint16_t bmod = usb_to_btron_modifiers(s_held_kbd_modifiers);
    if (k != 0) {
        EVT ev;
        ev.type   = EV_KEY_DOWN;
        ev.key    = k;
        ev.data   = (VW)(uintptr_t)bmod;
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.button = 0;
        snd_evt(&ev);
    }
}

/* Archimedes / RISC OS MouseStep curve for Pi 400. */
int32_t dp_ptr_riscos(int32_t raw, int step, int32_t *carry)
{
    int32_t mag = (raw < 0) ? -raw : raw;
    int32_t mult_fp;

    if (raw == 0) return dp_ptr_scale(0, 0, carry);
    if (mag <= 2) mag = (mag * 3) / 2;
    if (step < 1 || step > 3) step = 4;
    mult_fp = 256 + step * 128 + (mag > 4 ? 128 : 0);
    return dp_ptr_scale((raw < 0) ? -mag : mag, mult_fp, carry);
}

/* Raw HID delta batch -> accelerate -> integrate -> clamp -> gated MOVE. */
static void mouse_motion_apply(int32_t rdx, int32_t rdy) {
    if (rdx == 0 && rdy == 0) return;

    int32_t move_x = 0, move_y = 0;
    if (g_mouse_accel_profile == 0) {
        /* Profile 0: RISC OS MouseStep Stepped Accelerator (Archimedes 2.0x default) */
        move_x = dp_ptr_riscos(rdx, g_mouse_step_mult, &s_mouse_sub_x);
        move_y = dp_ptr_riscos(rdy, g_mouse_step_mult, &s_mouse_sub_y);
    } else if (g_mouse_accel_profile == 1) {
        /* Profile 1: Haiku OS / BeOS 2D Velocity Vector Accelerator */
        mouse_accelerate_pair_haiku(rdx, rdy, &move_x, &move_y);
    } else {
        /* Profile 2: Raw 1:1 unaccelerated */
        move_x = mouse_accelerate_subpixel_raw(rdx, &s_mouse_sub_x);
        move_y = mouse_accelerate_subpixel_raw(rdy, &s_mouse_sub_y);
    }

    int32_t nx = (int32_t)s_mouse_x + move_x;
    int32_t ny = (int32_t)s_mouse_y + move_y;
    if (nx < 0) nx = 0;
    else if (nx >= BTRON_SCREEN_W) nx = BTRON_SCREEN_W - 1;
    if (ny < 0) ny = 0;
    else if (ny >= BTRON_SCREEN_H) ny = BTRON_SCREEN_H - 1;

    int pos_changed = ((H)nx != s_mouse_x || (H)ny != s_mouse_y);
    s_mouse_x = (H)nx;
    s_mouse_y = (H)ny;
    set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);

    if (pos_changed || move_x != 0 || move_y != 0) {
        EVT ev;
        ev.type   = EV_MOUSE_MOVE;
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.button = (g_prev_mouse_btns & 1u) ? 1 : ((g_prev_mouse_btns & 2u) ? 2 : ((g_prev_mouse_btns & 4u) ? 3 : 0));
        ev.data   = (VW)(((uint32_t)(uint16_t)(int16_t)move_x) | ((uint32_t)(uint16_t)(int16_t)move_y << 16));
        snd_evt(&ev);
    }
}

/* Latest raw HID button byte -> RISC OS Select/Adjust/Menu edge events. */
static void mouse_buttons_apply(uint8_t raw) {
    uint8_t btn_left   = (raw & 1u);
    uint8_t btn_right  = (raw & 2u) >> 1;
    uint8_t btn_middle = (raw & 4u) >> 2;

    /* RISC OS 3-Button Model:
     * Button 1: Select (Left, or Right if swapped)
     * Button 2: Adjust (Right, or Left if swapped)
     * Button 3: Menu   (Middle / Wheel Click)
     */
    uint8_t sel_raw = g_mouse_swap_select_adjust ? btn_right : btn_left;
    uint8_t adj_raw = g_mouse_swap_select_adjust ? btn_left  : btn_right;

    uint8_t sel_prev = (g_prev_mouse_btns & 1u);
    uint8_t adj_prev = (g_prev_mouse_btns & 2u) >> 1;
    uint8_t mid_prev = (g_prev_mouse_btns & 4u) >> 2;
    g_prev_mouse_btns = (sel_raw) | (adj_raw << 1) | (btn_middle << 2);

    /* Select Button (Button 1) */
    if (sel_raw != sel_prev) {
        EVT ev;
        ev.type   = sel_raw ? EV_BUT_DOWN : EV_BUT_UP;
        ev.button = 1; /* Select */
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.key    = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }

    /* Adjust Button (Button 2) */
    if (adj_raw != adj_prev) {
        EVT ev;
        ev.type   = adj_raw ? EV_BUT_DOWN : EV_BUT_UP;
        ev.button = 2; /* Adjust */
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.key    = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }

    /* Menu Button (Button 3) */
    if (btn_middle != mid_prev) {
        EVT ev;
        ev.type   = btn_middle ? EV_BUT_DOWN : EV_BUT_UP;
        ev.button = 3; /* Menu */
        ev.pos.x  = s_mouse_x;
        ev.pos.y  = s_mouse_y;
        ev.key    = 0;
        ev.data   = 0;
        snd_evt(&ev);
    }
}

/* Legacy cooperative HID drain (QEMU / DWC2 / Pi 3, or xHCI without the
 * timer IRQ armed).  The caller has already performed the bounded
 * host-controller drain for this period. */
static int usb_poll_devices(GDEV *screen) {
    (void)screen;
    int activity = 0;

    uint32_t now_us = *(volatile uint32_t *)(TIMER_BASE + 0x04);

    /* 1. Drain pending USB HID Keyboard reports (bounded) */
    usb_kbd_report_t kbd_rep;
    for (uint32_t kbd_iter = 0; kbd_iter < ASYNC_HID_REPORT_BUDGET; kbd_iter++) {
        int kbd_got = 0;
        if (g_use_xhci) {
            kbd_got = (xhci_poll_keyboard(&kbd_rep) > 0);
        } else {
            kbd_got = (dwc2_poll_keyboard(&kbd_rep) > 0);
        }
        if (!kbd_got) break;
        kbd_report_to_events(&kbd_rep, now_us);
        activity = 1;
    }

    /* 1b. Key auto-repeat */
    kbd_repeat_check(now_us);

    /* 2. Drain USB HID Mouse reports (bounded) */
    usb_mouse_report_t mouse_rep;
    int32_t accum_dx = 0, accum_dy = 0;
    uint8_t latest_buttons = 0;
    int mouse_activity = 0;

    for (uint32_t m_iter = 0; m_iter < ASYNC_HID_REPORT_BUDGET; m_iter++) {
        int mouse_got = 0;
        if (g_use_xhci) {
            mouse_got = (xhci_poll_mouse(&mouse_rep) > 0);
        } else {
            mouse_got = (dwc2_poll_mouse(&mouse_rep) > 0);
        }
        if (!mouse_got) break;
        accum_dx += (int32_t)mouse_rep.dx;
        accum_dy += (int32_t)mouse_rep.dy;
        latest_buttons = mouse_rep.buttons;
        mouse_activity = 1;
    }

    if (mouse_activity) {
        mouse_buttons_apply(latest_buttons);
        mouse_motion_apply(accum_dx, accum_dy);
        activity = 1;
    }

    return activity;
}

/* ─────────────────────────────────────────────────────────────────
 * ASYNC.txt INPUT worker (TASK_INPUT, fixed 1 ms period).
 *
 * IRQ plane armed: seqlock snapshot read (wait-free, bounded retry),
 * diff monotonic accumulators, integrate/accelerate, emit KEY/BUTTON
 * edges and gated MOVE.  Never touches the xHCI event ring — the ISR
 * owns it.
 *
 * IRQ plane inactive: legacy bounded cooperative drain.
 * ───────────────────────────────────────────────────────────────── */
static void input_plane_task(GDEV *screen, uint32_t now_us) {
    if (!s_async_irq_active) {
        (void)usb_poll_devices(screen);
        return;
    }

    /* Wait-free seqlock read of the pointer snapshot */
    pointer_snapshot_t snap;
    uint32_t s1, s2;
    do {
        s1 = s_pointer_slot.seq;
        __asm__ volatile("dmb sy" : : : "memory");
        snap.acc_dx    = s_pointer_slot.acc_dx;
        snap.acc_dy    = s_pointer_slot.acc_dy;
        snap.acc_wheel = s_pointer_slot.acc_wheel;
        snap.buttons   = s_pointer_slot.buttons;
        snap.present   = s_pointer_slot.present;
        snap.motion_seq = s_pointer_slot.motion_seq;
        __asm__ volatile("dmb sy" : : : "memory");
        s2 = s_pointer_slot.seq;
    } while ((s1 & 1u) != 0 || s1 != s2);

    /* Diff against last-consumed snapshot: lossless MOVE coalescing */
    int32_t rdx = snap.acc_dx - s_input_last.acc_dx;
    int32_t rdy = snap.acc_dy - s_input_last.acc_dy;
    s_input_last = snap;

    /* 1. Update button state and emit edges */
    int button_edge_seen = 0;
    while (s_button_edge_tail != s_button_edge_head) {
        __asm__ volatile("dmb sy" : : : "memory");
        mouse_buttons_apply(s_button_edge_ring[s_button_edge_tail]);
        s_button_edge_tail = (uint16_t)((s_button_edge_tail + 1u) &
                                        (BUTTON_EDGE_RING_SIZE - 1u));
        button_edge_seen = 1;
    }
    if (!button_edge_seen && snap.buttons != s_input_prev_buttons)
        mouse_buttons_apply(snap.buttons);
    s_input_prev_buttons = snap.buttons;

    /* 2. Coalesced motion application with current button state */
    if (snap.present && (rdx != 0 || rdy != 0))
        mouse_motion_apply(rdx, rdy);

    /* Keyboard: drain the SPSC ring (ISR is the sole producer) */
    usb_kbd_report_t kbd_rep;
    for (uint32_t i = 0; i < ASYNC_HID_REPORT_BUDGET && xhci_poll_keyboard(&kbd_rep); i++) {
        kbd_report_to_events(&kbd_rep, now_us);
    }
    kbd_repeat_check(now_us);
}

/* ═══════════════════════════════════════════════════════════════════
 * Platform Query & RTOS Services
 * ═══════════════════════════════════════════════════════════════════ */
void btron_core_banner(void) {
    uint64_t midr = 0;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (midr >> 4) & 0xFFF;
    if (part == 0xD08) {
        uart_puts("[CORE] B-System/BTRON3 3.20 (aarch64-bcm2711) Takanori Yokoyama — T-Kernel 2.0\n");
        uart_puts("[CORE] Copyright 2026 Synrc Research Center. MIT License.\n");
        uart_puts("[BOOT] Machine: Raspberry Pi 4B / BCM2711  AArch64 Cortex-A72  T-Kernel 2.0\n\n");
    } else {
        uart_puts("[CORE] B-System/BTRON3 3.20 (aarch64-bcm2837) Takanori Yokoyama — T-Kernel 2.0\n");
        uart_puts("[CORE] Copyright 2026 Synrc Research Center. MIT License.\n");
        uart_puts("[BOOT] Machine: Raspberry Pi 3B / BCM2837  AArch64 Cortex-A53  T-Kernel 2.0\n\n");
    }
}

void btron_core_mem_log(void) {
    uint64_t midr = 0;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uint32_t part = (midr >> 4) & 0xFFF;
    if (part == 0xD08) {
        uart_puts("[MEM] BCM2711 Physical Memory Map (Pi 4B, 2 GB / 4 GB RAM):\n");
        uart_puts("[MEM]   0x00000000-0xFCFFFFFF  RAM (Usable 4048 MB)\n");
        uart_puts("[MEM]   0xFD000000-0xFFFFFFFF  Peripherals / PCIe / MMIO (48 MB)\n");
    } else {
        uart_puts("[MEM] BCM2837 Physical Memory Map (Pi 3B, 1 GB RAM):\n");
        uart_puts("[MEM]   0x00000000-0x3EFFFFFF  RAM (Usable 1008 MB)\n");
        uart_puts("[MEM]   0x3F000000-0x3FFFFFFF  Peripherals / VideoCore Mailbox / MMIO (16 MB)\n");
    }
    uart_puts("[MEM] Heap: 0x02000000-0x38000000 (864 MB Kernel Heap)\n");
    uart_puts("[MEM] Non-cacheable DMA window: 0x01C00000-0x01E00000 (XHCI rings, DMA CBs, mailboxes)\n");
}

void btron_core_hfds_log(void) {
    uart_puts("[HFDS] EMMC2 / SD Storage Interface: INIT  [OK]\n");
    uart_puts("[HFDS] HFDS Hierarchical File/Data Set: INIT  [OK]\n");

    /* ── Mount /SYS volume ─────────────────────────────────────────── */
    if (!g_sys_vol) {
        const uint16_t *magic_ptr = (const uint16_t *)0x03000000UL;
        if (*magic_ptr == 0xFE42 || *magic_ptr == 0x42FE) {
            BlkDev *sys_blk = blk_mem_create((void *)0x03000000UL, 1024 * 1024, 0);
            if (sys_blk) {
                g_sys_vol = vol_mount(sys_blk);
                if (g_sys_vol) {
                    uart_puts("[FS  ] /SYS mounted from RAM 0x03000000 (btron_sys.vol)  [OK]\n");
                    fb_log("[FS  ] /SYS mounted from RAM 0x03000000  [OK]\n");
                } else {
                    blk_destroy(sys_blk);
                }
            }
        }
        if (!g_sys_vol) {
            void *ram_vol = malloc(1024 * 1024);
            if (ram_vol) {
                BlkDev *mem_blk = blk_mem_create(ram_vol, 1024 * 1024, 0);
                if (mem_blk) {
                    vol_format(mem_blk, 256, 1024, "SYS");
                    g_sys_vol = vol_mount(mem_blk);
                    if (g_sys_vol) {
                        uart_puts("[FS  ] /SYS mounted on fallback RAM disk (1 MiB)  [OK]\n");
                        fb_log("[FS  ] /SYS mounted on RAM disk (1 MiB)  [OK]\n");
                    }
                }
            }
        }
    }

    /* ── Mount /ANDERS volume ───────────────────────────────────────── */
    if (!g_anders_vol) {
        const uint16_t *anders_magic = (const uint16_t *)0x03200000UL;
        if (*anders_magic == 0xFE42 || *anders_magic == 0x42FE) {
            BlkDev *anders_blk = blk_mem_create((void *)0x03200000UL, 1024 * 1024, 0);
            if (anders_blk) {
                g_anders_vol = vol_mount(anders_blk);
                if (g_anders_vol) {
                    uart_puts("[FS  ] /ANDERS mounted from RAM 0x03200000 (btron_anders.vol)  [OK]\n");
                    fb_log("[FS  ] /ANDERS mounted from RAM 0x03200000  [OK]\n");
                } else {
                    blk_destroy(anders_blk);
                }
            }
        }
    }

    if (g_sys_vol) {
        uart_puts("[HFDS] Root Volume: /SYS mounted  [OK]\n");
    }
}

void btron_core_init(void) {
    uart_puts("[CORE] Yokoyama T-Kernel 2.0 Engine (AArch64)  BTRON_YOKOYAMA_AARCH64\n");
    tkernel_init_subsystems(1);
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    if (!out_fn) return;
    if (arg && tkl_strcmp(arg, "-a") == 0) {
        out_fn("BTRON3 btron-pi400 2.0 T-Kernel-BCM2711 aarch64 B-System", COLOR_CYAN, user_data);
    } else if (arg && (tkl_strcmp(arg, "-r") == 0 || tkl_strcmp(arg, "-v") == 0)) {
        out_fn("2.0.0-tkernel-aarch64", COLOR_CYAN, user_data);
    } else {
        out_fn("B-System 3.0 Workstation System (BTRON3 Specification 3.20)", COLOR_CYAN, user_data);
        out_fn("Kernel: Sakamura T-Kernel 2.0 Real-Time Executive (AArch64 / BCM2711)", COLOR_GREEN, user_data);
        out_fn("Hardware Target: Raspberry Pi 400 Bare-Metal AArch64 Kernel (Cortex-A72)", COLOR_LTGRAY, user_data);
        out_fn("Build Timestamp: " __DATE__ " " __TIME__, COLOR_LTGRAY, user_data);
        out_fn("Display Compositor: VideoCore GPU Framebuffer Engine (1024x768 32-bpp Double-Buffered)", COLOR_LTGRAY, user_data);
        out_fn("Japanese IME: B-System Mozc / TIP Kana-Kanji Conversion Subsystem", COLOR_LTGRAY, user_data);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Kernel Main Entry Point
 * ═══════════════════════════════════════════════════════════════════ */

static int dma_framebuffer_selftest(volatile uint32_t *gpu_fb) {
    if (!gpu_fb || g_mmio_base != 0xFE000000UL) return 0;

    volatile uint32_t *dst = gpu_fb +
        (BTRON_SCREEN_H - 1) * BTRON_SCREEN_W + (BTRON_SCREEN_W - DMA_FB_SELFTEST_WORDS);
    for (uint32_t i = 0; i < DMA_FB_SELFTEST_WORDS; i++) {
        s_dma_fb_selftest_saved[i] = dst[i];
        s_dma_fb_selftest_src[i] = 0xD04A0000u ^ (i * 0x01010101u);
    }

    arm64_clean_cache_range(s_dma_fb_selftest_src, sizeof(s_dma_fb_selftest_src));
    arm64_clean_cache_range((const void *)dst, sizeof(s_dma_fb_selftest_saved));
    if (bcm2711_dma_init() != 0) return 0;
    if (bcm2711_dma_blit_linear_async(7, (uintptr_t)dst,
                                      (uintptr_t)s_dma_fb_selftest_src,
                                      sizeof(s_dma_fb_selftest_src)) != 0)
        return 0;

    int pass = bcm2711_dma_wait_timeout(7, 1000000u) == 0;
    arm64_invalidate_cache_range((const void *)dst, sizeof(s_dma_fb_selftest_saved));
    if (pass) {
        for (uint32_t i = 0; i < DMA_FB_SELFTEST_WORDS; i++) {
            if (dst[i] != s_dma_fb_selftest_src[i]) {
                pass = 0;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < DMA_FB_SELFTEST_WORDS; i++)
        dst[i] = s_dma_fb_selftest_saved[i];
    arm64_clean_cache_range((const void *)dst, sizeof(s_dma_fb_selftest_saved));
    return pass;
}

void btron_main(void) {
    /* 1. Reset heap pointer */
    heap_ptr = HEAP_BASE;
    tkl_memset((void*)HEAP_BASE, 0, 4096);

    /* 2. Initialize PL011 UART */
    uart_init();

    /* 3. Initialize Video Display Framebuffer (1024x768 32-bpp) */
    uint32_t *gpu_fb = init_pi_framebuffer(BTRON_SCREEN_W, BTRON_SCREEN_H);
    fb_log_enable((volatile uint32_t *)gpu_fb);
    s_dma_fb_selftest_passed = dma_framebuffer_selftest((volatile uint32_t *)gpu_fb);

    /* Visual confirmation: immediately paint vivid electric blue alive bar across top */
    if (gpu_fb) {
        for (int i = 0; i < BTRON_SCREEN_W * 12; i++) {
            gpu_fb[i] = 0xFF00B0FFu;
        }
    }

    /* Boot console budget: 768px / 16px glyphs = 48 rows, 1024px / 8px = 128
     * columns.  Everything above the shell prompt has to fit there, so each
     * subsystem gets one row and a line stays open (no newline) while its
     * bring-up runs -- a hang then leaves a half-row on the screen, which
     * localises it for free. */
    fb_log(g_mmio_base == 0xFE000000UL
           ? "[BOOT] BTRON3 Pi 400  T-Kernel 2.0  BCM2711 Cortex-A72 AArch64\n"
           : "[BOOT] BTRON3 Pi 3B   T-Kernel 2.0  BCM2837 Cortex-A53 AArch64\n");

    fb_log(s_dma_fb_selftest_passed ? "[DMA] framebuffer coherency: PASS\n"
                                    : "[DMA] framebuffer coherency: FAIL (DMA stays disabled)\n");

    /* Turn on the MMU and both caches.  Until this point the kernel has run
     * with SCTLR M=C=I=0, i.e. every RAM access went to DRAM at ~100 MB/s and
     * every byte loop ran ~30x below the core's rate.  Two regions must not be
     * cached once that changes: the attr2 window that holds the xHCI rings,
     * DMA control blocks and every mailbox buffer, and the scanout framebuffer
     * the VideoCore just allocated at an address it chose (see
     * arm64_mem.h), which is passed in here rather than assumed.  Placed after
     * the framebuffer and DMA self-test have finished their mailbox
     * transactions, before PCIe/USB bring-up.  The markers bracket the call so
     * a console photo localises a hang either way.
     *
     * arm64_mmu_init() invalidates the I-cache but not the D-cache by set/way
     * (the AArch64 encoding has no all-lines D invalidation the assembler
     * accepts, and a hand-rolled CCSIDR walk is a worse failure mode than the
     * hazard it avoids).  So stamp one word in each of .bss, the stack and the
     * bump heap while caches are off and read them back afterwards: a surviving
     * stale line shows up as a mismatch here, on the console, in this boot. */
    static volatile uint32_t s_bss_stamp;
    volatile uint32_t stack_stamp = 0x57A7F00Du;
    s_bss_stamp = 0xB05F11D5u;
    const uint32_t heap_stamp = *(volatile uint32_t *)HEAP_BASE;

    /* The console font is drawn into the framebuffer with fb_log() below, so
     * map a span wide enough for the double scanout page that
     * mailbox_set_virtual_offset() can select. */
    uint32_t fb_span = (uint32_t)BTRON_SCREEN_W * BTRON_SCREEN_H * 4u * 2u;
    if (g_pi_fb_size > fb_span) fb_span = g_pi_fb_size;

    fb_log("[MMU] enabling");
    arm64_mmu_init((uintptr_t)gpu_fb, fb_span);

    /* Read the pre-enable stamps back before overwriting them: a D-cache line
     * that survived M=1 shows up as a mismatch here. */
    const int mmu_read_ok = (s_bss_stamp == 0xB05F11D5u) &&
                            (stack_stamp == 0x57A7F00Du) &&
                            (*(volatile uint32_t *)HEAP_BASE == heap_stamp);

    /* Then prove a store lands.  QEMU is what caught the need for this: a
     * descriptor whose shareability field overlaps AP[2:1] makes every RAM
     * block read-only, so every load still works, the next push faults, the
     * vector table's fault-skip handler steps over the store, and a check that
     * only reads reports success.  The heap word is 1MB into the bump heap --
     * past anything allocated so far, and in a different 2MB block than .bss
     * and the stack, which both sit in the first. */
    volatile uint32_t * const heap_probe = (volatile uint32_t *)(HEAP_BASE + 0x100000UL);
    s_bss_stamp = 0x67E11E71u;
    *heap_probe = 0x67E11E71u;

    const int mmu_store_ok = (s_bss_stamp == 0x67E11E71u) && (*heap_probe == 0x67E11E71u);
    const int mmu_integrity_ok = mmu_read_ok && mmu_store_ok;
    /* Completes the row "[MMU] enabling" opened.  Read the installed tables
     * back and print the attribute each region actually got, so the descriptor
     * encoding is verified on the device instead of on the screen of the person
     * who wrote it.  Expected: bss=1 heap=1 dma=2 mmio=0 fb=2 layout=1 -- fb=2
     * because the firmware puts the scanout at 0x3C000000 (960MB), inside the
     * 1GB the L2 table covers, so its attr2 span is what the walk reaches.  (An
     * unmapped address walks back as -1 and prints as 4294967295.) */
    fb_log(mmu_integrity_ok ? " on" : " INTEGRITY FAIL");
    if (!mmu_integrity_ok) {
        fb_log(" rd="); fb_log_dec((uint32_t)mmu_read_ok);
        fb_log(" st="); fb_log_dec((uint32_t)mmu_store_ok);
    }
    fb_log(" attr bss=");
    fb_log_dec((uint32_t)arm64_mmu_attr_of((uintptr_t)&s_bss_stamp));
    fb_log(" heap=");
    fb_log_dec((uint32_t)arm64_mmu_attr_of(HEAP_BASE));
    fb_log(" dma=");
    fb_log_dec((uint32_t)arm64_mmu_attr_of(BTRON_NOCACHE_BASE + 0x80000UL));
    fb_log(" mmio=");
    fb_log_dec((uint32_t)arm64_mmu_attr_of(g_mmio_base));
    fb_log(" fb=");
    fb_log_dec((uint32_t)arm64_mmu_attr_of((uintptr_t)gpu_fb));
    fb_log(" layout=");
    fb_log_dec((uint32_t)arm64_mmu_layout_ok());
    fb_log("\n");

    btron_core_banner();
    btron_core_init();
    btron_core_mem_log();
    btron_core_hfds_log();

    /* 4. Initialize BCM2711 Hardware Device Drivers */

    /* One row for all three: the open "[DRV]" prefix is the hang marker, and
     * each driver appends its own verdict.  Failures print their code, which
     * uart_hex32() used to send to a serial port nobody is watching. */
    fb_log("[DRV] ");
    ER sdrv_res = ScreenDrv(0, NULL);
    fb_log("ScreenDrv=");
    fb_log(sdrv_res >= 0 ? "OK" : "FAIL");
    fb_log_dec((uint32_t)sdrv_res);

    ER kbpd_res = KbPdDrv(0, NULL);
    fb_log(" KbPdDrv=");
    fb_log(kbpd_res >= 0 ? "OK" : "FAIL");
    fb_log_dec((uint32_t)kbpd_res);

    ER lkb_res = LowKbPdDrv(0, NULL);
    fb_log(" LowKbPdDrv=");
    fb_log(lkb_res >= 0 ? "OK" : "FAIL");
    fb_log_dec((uint32_t)lkb_res);
    fb_log("\n");

    /* Initialize USB Subsystem */
    /* The board revision is what picks the host controller, so print it on the
     * same row that says which one was chosen.  The row is written before the
     * mailbox call and completed after it, so a firmware that never answers
     * leaves this open as the hang marker. */
    fb_log("[USB] probing host controllers, board rev=");
    if (g_mmio_base == 0xFE000000UL) {
        extern uint32_t bcm283x_get_board_revision(void);
        uint32_t board_rev = bcm283x_get_board_revision();
        fb_log_hex32(board_rev);
        fb_log("\n");

        /* QEMU raspi4b identifies as 0xB03111 or 0xB03115 without PCIe hardware.
         * Real physical hardware (Pi 400 0xC03130/1, Pi 4B 0xC0311x) has Broadcom PCIe + VL805.
         *
         * If board_rev == 0, the mailbox returned no data (possible timing issue or
         * firmware not responding yet). Treat 0 as physical hardware — QEMU always returns
         * a valid non-zero revision code.
         */
        bool is_qemu = (board_rev != 0) && (
            ((board_rev & 0x00F00000u) == 0x00B00000u) ||
            (board_rev == 0x00B03115u) || (board_rev == 0x00B03111u));
        if (!is_qemu) {
            /* No row narrates this: the [PCIE] and [XHCI] rows it triggers say
             * the same thing with the timings, and each of their failure paths
             * already prints its own verdict. */
            if (bcm2711_pcie_init() == 0) {
                uintptr_t vl805_mmio = bcm2711_pcie_get_vl805_mmio();
                if (vl805_mmio) {
                    if (xhci_init(vl805_mmio) == 0) {
                        g_use_xhci = 1;
                    }
                }
            }
            if (!g_use_xhci) {
                fb_log("[USB] no usable xHCI host controller\n");
            }
        } else {
            /* QEMU raspi4b model connects virtual USB keyboard/mouse to DWC2 */
            fb_log("[USB] QEMU virtual machine: DWC2 host controller\n");
            dwc2_init();
        }

        /* Arm the ASYNC.txt 1 kHz IRQ input plane — only after the xHCI
         * host controller is fully enumerated, so the ISR never observes
         * half-initialized rings. */
        if (g_use_xhci) {
            extern int arm64_irq_init(uint32_t hz);
            extern void arm64_irq_disable(void);
            extern void (*g_arm64_timer_hook)(void);
            g_arm64_timer_hook = rpi_timer_tick;
            int irq_ret = arm64_irq_init(1000);
            if (irq_ret == 0) {
                /* Confirm ticks actually arrive before trusting the IRQ plane.
                 * Arming s_async_irq_active gates the cooperative xHCI drain
                 * off everywhere, so if the timer/GIC never delivers (an EL or
                 * firmware variant we did not anticipate) input would be dead.
                 * Poll for up to 50 ms; if s_system_ticks never advances, tear
                 * the timer down and keep the cooperative path. */
                uint32_t tick0 = s_system_ticks;
                uint32_t t_start = *(volatile uint32_t *)(TIMER_BASE + 0x04);
                int confirmed = 0;
                for (int i = 0; i < 400000; i++) {
                    if (s_system_ticks != tick0) { confirmed = 1; break; }
                    if ((*(volatile uint32_t *)(TIMER_BASE + 0x04) - t_start) > 50000u) break;
                }
                if (confirmed) {
                    s_async_irq_active = 1;
                    fb_log("[IRQ] tick confirmed -> 1 kHz ASYNC input plane armed\n");
                } else {
                    /* No real tick: say whether the CPU ever took an interrupt
                     * (entries/hits) rather than re-dumping the GIC, whose
                     * per-config pending and HPPIR readings arm64_irq_init()
                     * already printed on its probe rows.  hits=0 with
                     * selftest=1 is a timer fault; entries=0 is the vector
                     * path. */
                    typedef struct {
                        uint32_t el, timer_intid, dispatch_hits, stub_entries;
                        uint32_t selftest_seen;
                        uint32_t cfg_idx;
                        uint32_t gicd_typer, gicd_ctlr, gicd_igroupr0;
                        uint32_t gicd_isenabler0, gicd_ispendr0;
                        uint32_t gicc_ctlr, gicc_pmr, gicc_hppir, gicc_iidr;
                        uint32_t cnthp_ctl, cntp_ctl, cntfrq;
                    } irqdiag_t;
                    extern void arm64_irq_get_diag(irqdiag_t *);
                    irqdiag_t d;
                    arm64_irq_get_diag(&d);
                    fb_log("[IRQ] no tick in 50ms -> cooperative path  cfg=");
                    fb_log_dec(d.cfg_idx);
                    fb_log(" selftest=");  fb_log_dec(d.selftest_seen);
                    fb_log(" hits=");      fb_log_dec(d.dispatch_hits);
                    fb_log(" entries=");   fb_log_dec(d.stub_entries);
                    fb_log("\n");
                    arm64_irq_disable();
                    g_arm64_timer_hook = 0;
                }
            } else {
                g_arm64_timer_hook = 0;
                fb_log("[IRQ] init ret=");
                fb_log_dec((uint32_t)irq_ret);
                fb_log(" -> cooperative input path\n");
            }
        }
    } else {
        fb_log("[USB] legacy MMIO map: DWC2 USB 2.0 host controller\n");
        dwc2_init();
    }

    /* 5. Initialize B-Audio HDMI Subsystem on BCM2711 */
    if (g_mmio_base == 0xFE000000UL) {
        extern int bcm2711_hdmi_audio_init(void);
        bcm2711_hdmi_audio_init();
    }

    /* 6. Initialize B-System Workbench Desktop Subsystem & V3D 4.2 */
    init_baremetal_desktop((uint32_t *)s_desktop_backbuffer, BTRON_SCREEN_W, BTRON_SCREEN_H);
    v3d_init();

    /* 6. Stage 1 Interactive Terminal Shell on GPU Framebuffer */
    /* This is the part of the log that stays on screen under the prompt, so it
     * is the one place decoration costs visible rows: four instead of the
     * boxed ten. */
    fb_log("[CORE] B-System / BTRON3 3.20  Raspberry Pi 400 / Pi 4B  [BCM2711 Cortex-A72 AArch64]\n");
    fb_log("[CORE] Cleanroom TRON kernel, Stage 1 terminal console.  Input: built-in USB keyboard / UART\n");
    fb_log("[CORE] Commands: help, mem, ver, clear, gui, startx, desktop, reboot\n");
    fb_log("[CORE] Autoboot: launching Desktop in 12s (Press any key to stay in shell)\n\n");
    fb_log("btron-pi400# ");

    uart_puts("+=================================================================+\n");
    uart_puts("|  B-System / BTRON3 3.20 (Raspberry Pi 400 / Pi 4B AArch64)      |\n");
    uart_puts("|  Stage 1: Terminal Console Active (HDMI On-Screen Debug Trace)  |\n");
    uart_puts("|  Type 'startx' or 'gui' to launch Graphical Workbench GUI!      |\n");
    uart_puts("+=================================================================+\n\n");

#if defined(BTRON_AUTO_GUI) && (BTRON_AUTO_GUI == 1)
    fb_log("[BOOT] AUTO_GUI=1 -> launching B-System Desktop\n");
    launch_pi4_desktop_session(gpu_fb);
#endif

    /* 6. Stage 1 Interactive Terminal Shell Loop */
    uint32_t last_sec_tick = *(volatile uint32_t *)(TIMER_BASE + 0x04);
    int autoboot_secs = 12;

    while (1) {
        if (autoboot_secs > 0 && !s_gui_active) {
            uint32_t now = *(volatile uint32_t *)(TIMER_BASE + 0x04);
            if ((now - last_sec_tick) >= 1000000) {
                last_sec_tick = now;
                autoboot_secs--;
                if (autoboot_secs == 0) {
                    fb_log("\n[BOOT] Autoboot timer expired -> Launching B-System Workbench GUI...\n");
                    launch_pi4_desktop_session(gpu_fb);
                }
            }
        }

        if (pi4_shell_poll(gpu_fb)) {
            /* Any keystroke immediately cancels autoboot timer */
            autoboot_secs = 0;
        }

        /* Brief memory barrier pause — keeps the polling rate high without
         * burning excessive CPU.  Shorter than the old 200-nop spin so
         * keyboard events are captured with <10 µs latency. */
        __asm__ volatile("dsb sy" : : : "memory");
    }
}
