/*
 * core_segui.c — Freestanding Clean-Room Real Hardware Core for PicoCalc
 *
 * Analogous to core_arm64.c (Raspberry Pi 400): layers the 320x320 Red Dot
 * Palm OS-like SegUI hardware shell on top of core_pico.c's Cortex-M RTOS.
 *
 * Battery-First Execution (doc/txt/PICO.txt):
 *   - Demand-driven 20-bit dirty_bands flush (0 FPS / 0 SPI1 traffic when idle)
 *   - Adaptive I2C1 key polling (20 ms active -> 100 ms after 3 s idle)
 *   - 15 s STM32 battery poll (never per frame)
 *   - Keyboard backlight defaults OFF (0)
 *   - 60 s inactivity LCD dim -> 5 min ST7365P SLPIN sleep (instant wake on key)
 *   - Yields via pico_tsk_delay() so core_pico's Idle task sleeps in `wfi`
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <btron/pico_rtos.h>
#include <btron/segui.h>
#include <btron/segui_palm.h>
#include "../apps/cabinet/cabinet_core.h"
#include "pico_hal.h"
#include "pico_regs.h"

extern void picogl_log_frame(const char *client, uint32_t ms, int bands);

static void pico_log(const char *s)
{
    pico_hal_uart_puts(s);
    cabinet_bootlog_append(s);
}

/* Driver entry points (st7365p_spi.c, i2ckbd_stm32.c, dp_pico.c) */
extern void st7365p_init(void);
extern void st7365p_sleep(int sleep_on);
extern void i2ckbd_init(void);
extern int  i2ckbd_poll_key(void);
extern int  i2ckbd_poll_event(int *down);
#if defined(PICOCALC_APP_QUAKE)
#include "start_sim.h"
#endif
extern int  i2ckbd_read_battery(void);
extern void i2ckbd_set_lcd_backlight(uint8_t level);
extern void i2ckbd_set_kbd_backlight(uint8_t level);
extern void i2ckbd_power_off(void);
extern void dp_pico_init_gdev(GDEV *dev);
extern void dp_pico_present(GDEV *dev, uint32_t dirty_mask, void (*paint_fn)(GDEV *));

#define ALL_BANDS_DIRTY     0x000FFFFFUL  /* 20 bands * 16 px = 320 px */
#define IDLE_SLOW_POLL_MS   3000u         /* step I2C poll 50Hz -> 10Hz */
#define IDLE_DIM_MS         60000u        /* dim LCD backlight at 60 s  */
#define IDLE_SLEEP_MS       300000u       /* ST7365P SLPIN at 5 min     */
#define BAT_POLL_MS         15000u        /* battery poll every 15 s    */

static GDEV     s_screen;
static uint8_t  s_user_lcd_bkl = 200u;
static int      s_cached_bat   = 86;

/* Title-bar FPS. Same rule as the desktop overlay: capture the app caption
 * once, replace it with "<caption>  <nnn> FPS", and do not invent a number
 * when the clock does not move. The Palm caption is the status pill, which
 * sits in bands 0 and 1, so a new number asks the next present for those. */
static int s_fps_open;
static int s_fps_dead;
static int s_fps_app = -1;
static int s_fps_bar;
static uint32_t s_fps_win_us;
static uint32_t s_fps_frames;
static char s_fps_base[32];
static char s_fps_last[32];

static int fps_eq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void fps_copy(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap < 1) return;
    if (!src) src = "";
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void title_fps(void)
{
    SEGUI_APP *app = segui_palm_app();
    uint32_t now, elapsed, ms, fps;
    int id, n, i;
    char t[32];
    char d0, d1, d2;
    if (!app || !segui_palm_is_animated()) {
        s_fps_open = 0;
        s_fps_dead = 0;
        s_fps_app = -1;
        return;
    }
    id = (int)segui_palm_active_id();
    now = TIMER0_TIMERAWL;
    if (!s_fps_open || id != s_fps_app) {
        s_fps_open = 1;
        s_fps_dead = 0;
        s_fps_app = id;
        s_fps_last[0] = '\0';
        s_fps_frames = 0;
        fps_copy(s_fps_base, (int)sizeof s_fps_base, app->title);
        s_fps_win_us = now;
    } else if (!fps_eq(app->title, s_fps_last[0] ? s_fps_last : s_fps_base)) {
        fps_copy(s_fps_base, (int)sizeof s_fps_base, app->title);
        s_fps_last[0] = '\0';
        s_fps_win_us = now;
        s_fps_frames = 0;
    }
    if (s_fps_dead) return;
    s_fps_frames++;
    elapsed = now - s_fps_win_us;
    if (elapsed < 250000u && s_fps_frames < 30u) return;
    if (elapsed == 0u) {
        s_fps_dead = 1;
        return;
    }
    ms = elapsed / 1000u;
    if (ms == 0u) ms = 1u;
    fps = (s_fps_frames * 1000u) / ms;
    if (fps > 999u) fps = 999u;
    s_fps_frames = 0;
    s_fps_win_us = now;
    d0 = (char)('0' + (fps / 100u) % 10u);
    d1 = (char)('0' + (fps / 10u) % 10u);
    d2 = (char)('0' + fps % 10u);
    if (d0 == '0') d0 = ' ';
    if (d0 == ' ' && d1 == '0') d1 = ' ';
    n = 0;
    for (i = 0; s_fps_base[i] && n < 22; i++) t[n++] = s_fps_base[i];
    t[n++] = ' ';
    t[n++] = ' ';
    t[n++] = d0;
    t[n++] = d1;
    t[n++] = d2;
    t[n++] = ' ';
    t[n++] = 'F';
    t[n++] = 'P';
    t[n++] = 'S';
    t[n] = '\0';
    segui_set_title(app, t);
    fps_copy(s_fps_last, (int)sizeof s_fps_last, t);
    s_fps_bar = 1;
}

/* ── SEGUI_PALM_HW_OPS Bridge to Real Silicon ──────────────────────────── */

static void hw_set_lcd_bkl(uint8_t lvl)
{
    s_user_lcd_bkl = lvl;
    i2ckbd_set_lcd_backlight(lvl);
}

static void hw_set_kbd_bkl(uint8_t lvl) { i2ckbd_set_kbd_backlight(lvl); }
static void hw_power_off(void)          { i2ckbd_power_off(); }
static void hw_reboot_bootsel(void)     { pico_hal_reboot(1); }
static int  hw_get_bat(void)            { return s_cached_bat; }

static void hw_shell_exec(const char *cmd, SEGUI_PALM_OUT_FN out, void *ctx)
{
    pico_shell_exec(cmd, (pico_out_fn)out, ctx);
}

static void segui_task(void *arg)
{
    SEGUI_PALM_HW_OPS hw;
    uint32_t dirty = ALL_BANDS_DIRTY;
    uint32_t last_input_ms = 0;
    uint32_t last_bat_ms   = 0;
    int lcd_dimmed = 0;
    int lcd_asleep = 0;
    (void)arg;

    cabinet_core_init();
    pico_log("[BOOT] B-System/BTRON3 PicoCalc (");
    pico_log(pico_hal_board_name());
    pico_log(" / ");
    pico_log(pico_hal_cpu_name());
    pico_log(")\n");
    pico_log("[LCD ] ST7365P 320x320 SPI1 init...\n");
    st7365p_init();
    pico_log("[I2C ] STM32 0x1F 10kHz init...\n");
    i2ckbd_init();

    /* Battery-first boot defaults: LCD 78%, keyboard backlight OFF */
    i2ckbd_set_lcd_backlight(s_user_lcd_bkl);
    i2ckbd_set_kbd_backlight(0u);
    {
        int b = i2ckbd_read_battery();
        if (b >= 0) {
            s_cached_bat = b;
            pico_log("[I2C ] STM32 battery probe OK\n");
        } else {
            pico_log("[I2C ] STM32 battery probe NACK (using default)\n");
        }
    }

    dp_pico_init_gdev(&s_screen);

    hw.board_name        = pico_hal_board_name();
    hw.cpu_name          = pico_hal_cpu_name();
    hw.get_uptime_ms     = pico_ticks_ms;
    hw.get_sram_used     = pico_static_sram_used;
    hw.get_sram_total    = pico_hal_board_sram_bytes;
    hw.get_task_count    = pico_active_task_count;
    hw.get_battery_pct   = hw_get_bat;
    hw.set_lcd_backlight = hw_set_lcd_bkl;
    hw.set_kbd_backlight = hw_set_kbd_bkl;
    hw.set_uart_mirror   = NULL;
    hw.power_off         = hw_power_off;
    hw.reboot_bootsel    = hw_reboot_bootsel;
    hw.shell_exec        = hw_shell_exec;

    segui_palm_init(&segui_profile_picocalc, &hw);
    segui_palm_layout(&s_screen);
    last_input_ms = pico_ticks_ms();
    last_bat_ms   = last_input_ms;

    pico_log("[FS  ] /SYS BROM mounted (OSAL_BOOK + NORTIA + HEART_SUTRA + PICO.TXT + BOOT.LOG)\n");
#if defined(PICOCALC_APP_QUAKE)
    pico_log("[SEGUI] Palm OS 320x320 UI active (center is Quake)\n");
#else
    pico_log("[SEGUI] Palm OS 320x320 UI active (center is XMB)\n");
#endif

    for (;;) {
        uint32_t now = pico_ticks_ms();
        int key = -1;
        /* One poll per loop. The driver emits one edge, then repeats on
         * the Prefs delay and gap. A release is not a second character. */
        {
#if defined(PICOCALC_APP_QUAKE)
            int down = 1;
            key = i2ckbd_poll_event(&down);
            if (key >= 0) {
                last_input_ms = now;
                if (lcd_asleep) {
                    st7365p_sleep(0);
                    i2ckbd_set_lcd_backlight(s_user_lcd_bkl);
                    lcd_asleep = 0;
                    lcd_dimmed = 0;
                    dirty = ALL_BANDS_DIRTY;
                    key = -1;
                } else if (lcd_dimmed) {
                    i2ckbd_set_lcd_backlight(s_user_lcd_bkl);
                    lcd_dimmed = 0;
                }
                if (key >= 0 && segui_palm_active_id() == PALM_APP_GLGEARS &&
                    (key == START_KEY_FIRE || !down)) {
                    start_sim_key(key, down);
                    /* The menu does not repaint on its own. Fire has to
                     * mark the panel or a resolution change never shows. */
                    if (down) dirty = ALL_BANDS_DIRTY;
                    key = -1;
                } else if (key >= 0 && !down) {
                    key = -1;
                }
            }
#else
            key = i2ckbd_poll_key();
#endif
        }

        if (key >= 0) {
            last_input_ms = now;
            if (lcd_asleep) {
                st7365p_sleep(0);
                i2ckbd_set_lcd_backlight(s_user_lcd_bkl);
                lcd_asleep = 0;
                lcd_dimmed = 0;
                dirty = ALL_BANDS_DIRTY;
            } else if (lcd_dimmed) {
                i2ckbd_set_lcd_backlight(s_user_lcd_bkl);
                lcd_dimmed = 0;
            } else if (segui_palm_handle_key(key, &s_screen)) {
                dirty = ALL_BANDS_DIRTY;
            }
        }

        /* Periodic battery poll (every 15 s) */
        if ((uint32_t)(now - last_bat_ms) >= BAT_POLL_MS) {
            int b = i2ckbd_read_battery();
            last_bat_ms = now;
            if (b >= 0 && b != s_cached_bat) {
                s_cached_bat = b;
                segui_set_status(segui_palm_app(), "12:00",
                                 segui_palm_app_name(segui_palm_active_id()),
                                 s_cached_bat);
                dirty |= 0x00000003UL; /* Top 2 bands (0..31 px) only */
            }
        }

        /* Inactivity power management (skip while GLGears is running) */
        if (!segui_palm_is_animated()) {
            uint32_t idle_ms = now - last_input_ms;
            if (!lcd_asleep && idle_ms >= IDLE_SLEEP_MS) {
                i2ckbd_set_lcd_backlight(0u);
                st7365p_sleep(1);
                lcd_asleep = 1;
            } else if (!lcd_dimmed && !lcd_asleep && idle_ms >= IDLE_DIM_MS) {
                i2ckbd_set_lcd_backlight((uint8_t)(s_user_lcd_bkl / 4u));
                lcd_dimmed = 1;
            }
        } else if (!lcd_asleep && segui_palm_tick(now)) {
            dirty |= 0x0007FFFCUL; /* Body bands only (skip status & softkey) */
#if defined(PICOCALC_APP_QUAKE)
            /* Fullscreen is the whole 320x320 panel, including the bands
             * the window chrome used to own. */
            if (start_sim_fullscreen())
                dirty |= ALL_BANDS_DIRTY;
#else
            if (segui_palm_gl_fullscreen())
                dirty |= ALL_BANDS_DIRTY;
#endif
        }

        /* The Palm title is the status pill. A new FPS string needs bands 0 and 1. */
        if (s_fps_bar) {
            dirty |= 0x00000003UL;
            s_fps_bar = 0;
        }

        if (!lcd_asleep && dirty != 0u) {
            if (segui_palm_is_animated()) {
                uint32_t t0 = TIMER0_TIMERAWL;
                uint32_t mask = dirty;
                int bands = 0;
                dp_pico_present(&s_screen, dirty, segui_palm_paint);
                while (mask) {
                    bands += (int)(mask & 1u);
                    mask >>= 1;
                }
                {
#if defined(PICOCALC_APP_QUAKE)
                    const char *client = "quake";
#else
                    const char *client = "gears";
                    if (segui_palm_active_id() == PALM_APP_XMB) client = "xmb";
#endif
                    picogl_log_frame(client, (TIMER0_TIMERAWL - t0) / 1000u, bands);
                    title_fps();
                }
            } else {
                dp_pico_present(&s_screen, dirty, segui_palm_paint);
            }
            dirty = 0u;
        }

        /* Yield to RTOS -> core_pico Idle task executes `wfi` */
        if (segui_palm_is_animated()) {
            pico_tsk_delay(1);   /* Full-speed 3D rendering, minimal 1 ms yield */
        } else if ((uint32_t)(now - last_input_ms) < IDLE_SLOW_POLL_MS) {
            pico_tsk_delay(20);  /* 50 Hz active typing */
        } else {
            pico_tsk_delay(100); /* 10 Hz low-power idle poll */
        }
    }
}

/* Override weak pico_ui_start() in core_pico.c */
void pico_ui_start(void)
{
    pico_tsk_create("segui", 15, segui_task, NULL);
}
