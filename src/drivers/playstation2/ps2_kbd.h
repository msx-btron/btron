/*
 * ps2_kbd.h — PlayStation 2 Cleanroom Keyboard Driver & Auto-Repeat
 *
 * Implements hardware keyboard typematic auto-repeat conforming to
 * BTRON *FX 11/12 settings and 1 ms input worker cadence.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_KBD_H
#define PS2_KBD_H

#include <stdint.h>
#include <stddef.h>

#define PS2_KBD_REPEAT_MAX 8u

typedef void (*ps2_kbd_inject_fn)(uint32_t btron_key, int down);
typedef void (*ps2_kbd_shell_fn)(int c);

void ps2_kbd_init(ps2_kbd_inject_fn inject_cb, ps2_kbd_shell_fn shell_cb);
void ps2_kbd_arm(uint32_t btron_key, uint8_t mod);
void ps2_kbd_disarm(uint32_t btron_key);
uint32_t ps2_kbd_due(uint32_t since_us, uint32_t interval_us, uint32_t *advance_us);
void ps2_kbd_repeat_tick(void);

#endif /* PS2_KBD_H */

