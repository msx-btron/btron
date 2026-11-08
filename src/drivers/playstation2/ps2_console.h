/*
 * ps2_console.h — PlayStation 2 Cleanroom Debug Console & Shell Editor
 *
 * Implements SIO0 UART + GS Text Console output, kprintf formatting,
 * and Stage 1 ANSI terminal line editing.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_CONSOLE_H
#define PS2_CONSOLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

void ps2_console_set_gui_active(int active);
int  ps2_console_get_gui_active(void);

void ps2_console_putc(char c);
void ps2_console_flush(void);
void uart_puts_raw(const char *s);
void ps2_kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

typedef void (*ps2_shell_exec_fn)(const char *cmd);
typedef void (*ps2_shell_key_fn)(uint32_t keycode, int down);
typedef void (*ps2_shell_move_fn)(int dx, int dy);

void ps2_console_shell_init(ps2_shell_exec_fn exec_cb, ps2_shell_key_fn key_cb, ps2_shell_move_fn move_cb);
void ps2_console_shell_char(int c);
void ps2_console_shell_poll(void);

#endif /* PS2_CONSOLE_H */

