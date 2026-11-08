/*
 * ps2_kbd.c — PlayStation 2 Cleanroom Keyboard Driver & Auto-Repeat
 *
 * Implements hardware keyboard typematic auto-repeat conforming to
 * BTRON *FX 11/12 settings and 1 ms input worker cadence.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_kbd.h"
#include "ps2_console.h"

extern uint32_t btron_render_perf_us(void);
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;

static uint32_t s_kbd_held_key;
static uint8_t  s_kbd_held_mod;
static uint32_t s_kbd_press_us;
static uint32_t s_kbd_repeat_us;
static uint32_t s_kbd_repeats;

static ps2_kbd_inject_fn s_inject_cb = NULL;
static ps2_kbd_shell_fn  s_shell_cb  = NULL;

void ps2_kbd_init(ps2_kbd_inject_fn inject_cb, ps2_kbd_shell_fn shell_cb)
{
    s_inject_cb = inject_cb;
    s_shell_cb  = shell_cb;
    s_kbd_held_key = 0u;
    s_kbd_held_mod = 0u;
    s_kbd_press_us = 0u;
    s_kbd_repeat_us = 0u;
    s_kbd_repeats = 0u;
}

void ps2_kbd_arm(uint32_t btron_key, uint8_t mod)
{
    s_kbd_held_key = btron_key;
    s_kbd_held_mod = mod;
    s_kbd_press_us = s_kbd_repeat_us = btron_render_perf_us();
}

void ps2_kbd_disarm(uint32_t btron_key)
{
    if (s_kbd_held_key != 0u && s_kbd_held_key == btron_key) {
        s_kbd_held_key = 0u;
    }
}

uint32_t ps2_kbd_due(uint32_t since_us, uint32_t interval_us, uint32_t *advance_us)
{
    if (interval_us == 0u) { *advance_us = 0u; return 0u; }

    const uint32_t due = since_us / interval_us;
    if (due == 0u) { *advance_us = 0u; return 0u; }
    if (due > PS2_KBD_REPEAT_MAX) { *advance_us = since_us; return PS2_KBD_REPEAT_MAX; }

    *advance_us = due * interval_us;
    return due;
}

void ps2_kbd_repeat_tick(void)
{
    const uint32_t now = btron_render_perf_us();

    if (!g_kbd_repeat_enabled || s_kbd_held_key == 0u) return;
    if ((uint32_t)(now - s_kbd_press_us) < g_kbd_repeat_delay_us) return;

    uint32_t advance_us;
    const uint32_t due = ps2_kbd_due((uint32_t)(now - s_kbd_repeat_us),
                                     g_kbd_repeat_interval_us, &advance_us);
    if (due == 0u) return;

    s_kbd_repeat_us += advance_us;
    s_kbd_repeats += due;

    for (uint32_t i = 0u; i < due; i++) {
        if (ps2_console_get_gui_active()) {
            if (s_inject_cb) s_inject_cb(s_kbd_held_key, 1);
        } else if (s_kbd_held_key < 0x100u) {
            if (s_shell_cb) s_shell_cb((int)s_kbd_held_key);
        }
    }
}

