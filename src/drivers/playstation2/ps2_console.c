/*
 * ps2_console.c — PlayStation 2 Cleanroom Debug Console & Shell Editor
 *
 * Implements SIO0 UART + GS Text Console output, kprintf formatting,
 * and Stage 1 ANSI terminal line editing.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_console.h"
#include "ps2_sio.h"
#include "ps2_gs.h"
#include <btron/event.h>

static int s_gui_active = 0;
static ps2_shell_exec_fn s_exec_cb = NULL;
static ps2_shell_key_fn  s_key_cb  = NULL;
static ps2_shell_move_fn s_move_cb = NULL;

static char cmd_buf[64];
static int  cmd_pos = 0;
static int  esc_state = 0;
static int  esc_num = 0;

void ps2_console_set_gui_active(int active)
{
    s_gui_active = active;
}

int ps2_console_get_gui_active(void)
{
    return s_gui_active;
}

void ps2_console_putc(char c)
{
    ps2_sio_putc(c);
    if (!s_gui_active) {
        ps2_gs_text_putc(c, 0xFFE0E0E0, 0xFF121B29);
    }
}

void ps2_console_flush(void)
{
    if (!s_gui_active) {
        ps2_gs_text_flush();
    }
}

void uart_puts_raw(const char *s)
{
    if (!s) return;
    while (*s) ps2_console_putc(*s++);
}

static void ps2_emit_num(uint32_t val, unsigned base, unsigned width,
                         int zero_pad, int upper, int negative)
{
    char buf[12];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;

    if (val == 0) buf[n++] = '0';
    while (val != 0 && n < (int)sizeof(buf)) {
        buf[n++] = digits[val % base];
        val /= base;
    }

    unsigned filled = (unsigned)n + (negative ? 1u : 0u);
    for (unsigned i = filled; i < width; i++) ps2_console_putc(zero_pad ? '0' : ' ');
    if (negative) ps2_console_putc('-');
    while (n > 0) ps2_console_putc(buf[--n]);
}

void ps2_kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            ps2_console_putc(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '\0') break;

        if (*fmt == '%') {
            ps2_console_putc('%');
            fmt++;
            continue;
        }

        int zero_pad = 0;
        int left = 0;
        unsigned width = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10u + (unsigned)(*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            unsigned n = 0;
            if (!s) s = "(null)";
            while (*s) { ps2_console_putc(*s++); n++; }
            while (left && n++ < width) ps2_console_putc(' ');
            break;
        }
        case 'c':
            ps2_console_putc((char)va_arg(ap, int));
            break;
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            uint32_t mag = (v < 0) ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
            ps2_emit_num(mag, 10, width, zero_pad, 0, v < 0);
            break;
        }
        case 'u':
            ps2_emit_num(va_arg(ap, unsigned int), 10, width, zero_pad, 0, 0);
            break;
        case 'x':
            ps2_emit_num(va_arg(ap, unsigned int), 16, width, zero_pad, 0, 0);
            break;
        case 'X':
            ps2_emit_num(va_arg(ap, unsigned int), 16, width, zero_pad, 1, 0);
            break;
        default:
            break;
        }
        fmt++;
    }
    va_end(ap);

    ps2_console_flush();
}

void ps2_console_shell_init(ps2_shell_exec_fn exec_cb, ps2_shell_key_fn key_cb, ps2_shell_move_fn move_cb)
{
    s_exec_cb = exec_cb;
    s_key_cb  = key_cb;
    s_move_cb = move_cb;
    cmd_pos = 0;
    esc_state = 0;
}

void ps2_console_shell_char(int c)
{
    if (esc_state == 0) {
        if (c == 0x1B) {
            esc_state = 1;
            return;
        }
    } else if (esc_state == 1) {
        if (c == '[') {
            esc_state = 2;
            return;
        } else {
            esc_state = 0;
        }
    } else if (esc_state == 2) {
        if (c == 'A') {
            esc_state = 0;
            if (s_move_cb) s_move_cb(0, -16);
            if (s_key_cb) { s_key_cb(BTRON_KEY_UP, 1); s_key_cb(BTRON_KEY_UP, 0); }
            return;
        } else if (c == 'B') {
            esc_state = 0;
            if (s_move_cb) s_move_cb(0, 16);
            if (s_key_cb) { s_key_cb(BTRON_KEY_DOWN, 1); s_key_cb(BTRON_KEY_DOWN, 0); }
            return;
        } else if (c == 'C') {
            esc_state = 0;
            if (s_move_cb) s_move_cb(16, 0);
            if (s_key_cb) { s_key_cb(BTRON_KEY_RIGHT, 1); s_key_cb(BTRON_KEY_RIGHT, 0); }
            return;
        } else if (c == 'D') {
            esc_state = 0;
            if (s_move_cb) s_move_cb(-16, 0);
            if (s_key_cb) { s_key_cb(BTRON_KEY_LEFT, 1); s_key_cb(BTRON_KEY_LEFT, 0); }
            return;
        } else if (c == 'H') {
            esc_state = 0;
            if (s_key_cb) { s_key_cb(BTRON_KEY_HOME, 1); s_key_cb(BTRON_KEY_HOME, 0); }
            return;
        } else if (c == 'F') {
            esc_state = 0;
            if (s_key_cb) { s_key_cb(BTRON_KEY_END, 1); s_key_cb(BTRON_KEY_END, 0); }
            return;
        } else if (c >= '0' && c <= '9') {
            esc_num = c - '0';
            esc_state = 3;
            return;
        } else {
            esc_state = 0;
        }
    } else if (esc_state == 3) {
        esc_state = 0;
        if (c == '~' && s_key_cb) {
            if (esc_num == 1) {
                s_key_cb(BTRON_KEY_HOME, 1); s_key_cb(BTRON_KEY_HOME, 0);
            } else if (esc_num == 3) {
                s_key_cb(BTRON_KEY_DELETE, 1); s_key_cb(BTRON_KEY_DELETE, 0);
            } else if (esc_num == 4) {
                s_key_cb(BTRON_KEY_END, 1); s_key_cb(BTRON_KEY_END, 0);
            } else if (esc_num == 5) {
                s_key_cb(BTRON_KEY_PAGE_UP, 1); s_key_cb(BTRON_KEY_PAGE_UP, 0);
            } else if (esc_num == 6) {
                s_key_cb(BTRON_KEY_PAGE_DOWN, 1); s_key_cb(BTRON_KEY_PAGE_DOWN, 0);
            }
        }
        return;
    }

    if (c == '\r' || c == '\n') {
        ps2_console_putc('\n');
        if (!s_gui_active) ps2_gs_text_flush();
        cmd_buf[cmd_pos] = '\0';
        if (s_exec_cb) s_exec_cb(cmd_buf);
        cmd_pos = 0;
    } else if (c == 0x08 || c == 0x7F) {
        if (cmd_pos > 0) {
            cmd_pos--;
            ps2_console_putc('\b');
            if (!s_gui_active) ps2_gs_text_flush();
        }
        if (s_gui_active && s_key_cb) {
            s_key_cb(BTRON_KEY_BACKSPACE, 1);
            s_key_cb(BTRON_KEY_BACKSPACE, 0);
        }
    } else if (c >= 0x20 && c <= 0x7E) {
        if (s_gui_active && s_key_cb) {
            s_key_cb((UW)(uint8_t)c, 1);
            s_key_cb((UW)(uint8_t)c, 0);
        }
        if (cmd_pos < (int)sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_pos++] = (char)c;
            ps2_console_putc((char)c);
            if (!s_gui_active) ps2_gs_text_flush();
        }
    }
}

void ps2_console_shell_poll(void)
{
    while (ps2_sio_has_char()) {
        int c = ps2_sio_getc();
        if (c < 0) break;
        ps2_console_shell_char(c);
    }
}

