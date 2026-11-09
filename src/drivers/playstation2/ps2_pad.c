/*
 * ps2_pad.c — Cleanroom Sony PlayStation 2 DualShock 2 Controller Driver
 *
 * Implements analog stick velocity integration, deadband filtering,
 * button edge detection, and event mapping.
 *
 * Cleanroom implementation referencing open specifications in third_party/ps2sdk
 * and ps2tek. Zero proprietary Sony SDK dependencies.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_pad.h"
#include "ps2_irq.h"

extern uint32_t ps2_count_read(void);

static ps2_pad_status_t s_current_pad;
static uint16_t s_last_btns = 0xFFFF;
static uint32_t s_pad_t0;

/* External hooks implemented in core_ps2.c */
extern void ps2_pad_on_move(int dx, int dy);
extern void ps2_pad_on_button(uint16_t pressed_mask, uint16_t released_mask);

void ps2_pad_init(void)
{
    s_current_pad.btns   = 0xFFFF; /* All released */
    s_current_pad.ljoy_h = 128;    /* Centered */
    s_current_pad.ljoy_v = 128;
    s_current_pad.rjoy_h = 128;
    s_current_pad.rjoy_v = 128;
    s_last_btns          = 0xFFFF;
}

int ps2_pad_read(ps2_pad_status_t *status)
{
    if (!status) return -1;
    *status = s_current_pad;
    return 0;
}

void ps2_pad_set_state(uint16_t btns, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry)
{
    s_current_pad.btns   = btns;
    s_current_pad.ljoy_h = lx;
    s_current_pad.ljoy_v = ly;
    s_current_pad.rjoy_h = rx;
    s_current_pad.rjoy_v = ry;
}

void ps2_pad_poll(void)
{
    /* Analog is velocity per sample.  INPUT (1 kHz) and the compositor
     * both call this; skip a second sample in the same millisecond so
     * the stick does not double when both run. */
    {
        uint32_t tpu = btron_ee_ticks_per_us();
        uint32_t now = ps2_count_read();
        if (tpu && s_pad_t0 && (uint32_t)(now - s_pad_t0) < tpu * 1000u)
            return;
        s_pad_t0 = now;
    }

    /* 1. Left Analog Stick -> Cursor Velocity (dx, dy) */
    int lx = (int)s_current_pad.ljoy_h - 128;
    int ly = (int)s_current_pad.ljoy_v - 128;
    int dx = 0;
    int dy = 0;

    /* Deadzone filter (+/- 16) */
    if (lx > 16 || lx < -16) {
        dx = (lx > 0) ? ((lx - 16) / 10 + 1) : ((lx + 16) / 10 - 1);
    }
    if (ly > 16 || ly < -16) {
        dy = (ly > 0) ? ((ly - 16) / 10 + 1) : ((ly + 16) / 10 - 1);
    }

    if (dx != 0 || dy != 0) {
        ps2_pad_on_move(dx, dy);
    }

    /* 2. Button Edge Detection (Active-low: 0 is pressed) */
    uint16_t current_pressed = ~s_current_pad.btns;
    uint16_t last_pressed    = ~s_last_btns;

    uint16_t newly_pressed  = current_pressed & ~last_pressed;
    uint16_t newly_released = ~current_pressed & last_pressed;

    if (newly_pressed || newly_released) {
        ps2_pad_on_button(newly_pressed, newly_released);
    }

    s_last_btns = s_current_pad.btns;
}
