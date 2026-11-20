/*
 * shell_core.c — Shared Headless B-System Shell Dispatcher
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "shell_core.h"
#include "../cabinet/cabinet_core.h"

static int s_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a++ != *b++) return 0;
    }
    return *a == *b;
}

static int s_starts(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s++ != *pfx++) return 0;
    }
    return 1;
}

static void out_u32(BtronShellOutFn out, void *ctx, uint32_t v)
{
    char tmp[12], buf[12];
    int n = 0, i;
    if (v == 0) tmp[n++] = '0';
    else while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    out(buf, ctx);
}

int btron_shell_core_exec(const char *line, BtronShellOutFn out, void *ctx,
                          const BtronShellHooks *hooks)
{
    if (!line || !out) return 0;
    while (*line == ' ') line++;
    if (*line == '\0') return 1;

    if (s_streq(line, "help") || s_streq(line, "?")) {
        out("B-System Shell: help ver mem ls cat df diag echo ps reboot\n", ctx);
        return 1;
    }
    if (s_streq(line, "diag")) {
        int bi = cabinet_core_find("BOOT.LOG");
        out("=== B-SYSTEM HARDWARE DIAG ===\n", ctx);
        if (bi >= 0) {
            uint32_t sz = 0;
            const char *log = cabinet_core_read(bi, &sz);
            if (log && sz > 0) out(log, ctx);
            else out("(BOOT.LOG empty)\n", ctx);
        }
        return 1;
    }
    if (s_streq(line, "ls")) {
        int n = cabinet_core_count();
        out("/SYS Real-Object Volume:\n", ctx);
        for (int i = 0; i < n; i++) {
            const CAB_ENTRY *e = cabinet_core_get(i);
            if (!e) continue;
            out("  ", ctx);
            out(e->name, ctx);
            out(e->rw_data ? "  [RW] " : "  [RO] ", ctx);
            out_u32(out, ctx, e->size);
            out(" B\n", ctx);
        }
        return 1;
    }
    if (s_starts(line, "cat ")) {
        const char *name = line + 4;
        int idx;
        while (*name == ' ') name++;
        idx = cabinet_core_find(name);
        if (idx < 0) {
            out("cat: not found: ", ctx);
            out(name, ctx);
            out("\n", ctx);
        } else {
            uint32_t sz = 0;
            const char *txt = cabinet_core_read(idx, &sz);
            if (txt && sz >= 2u && (unsigned char)txt[0] == 0x00u && (unsigned char)txt[1] == 0x01u) {
                out("[Binary TAD: ", ctx);
                out_u32(out, ctx, sz);
                out(" B — open in Browser]\n", ctx);
            } else {
                if (txt) out(txt, ctx);
                out("\n", ctx);
            }
        }
        return 1;
    }
    if (s_streq(line, "df")) {
        uint32_t u = cabinet_core_used_bytes();
        uint32_t t = cabinet_core_total_bytes();
        out("/SYS used ", ctx); out_u32(out, ctx, u);
        out(" B / ", ctx);     out_u32(out, ctx, t);
        out(" B\n", ctx);
        return 1;
    }
    if (s_starts(line, "echo ")) {
        out(line + 5, ctx);
        out("\n", ctx);
        return 1;
    }
    if (hooks && hooks->fallback_exec) {
        hooks->fallback_exec(line, out, ctx);
        return 1;
    }
    if (s_streq(line, "ver")) {
        out("B-System/BTRON3 3.20 (SegUI ", ctx);
        out(hooks && hooks->board_name ? hooks->board_name : "PicoCalc", ctx);
        out(")\n", ctx);
        return 1;
    }
    if (s_streq(line, "mem") && hooks && hooks->get_sram_used) {
        out("SRAM: ", ctx); out_u32(out, ctx, hooks->get_sram_used());
        out(" / ", ctx);    out_u32(out, ctx, hooks->get_sram_total ? hooks->get_sram_total() : 0);
        out(" B\n", ctx);
        return 1;
    }
    out("Unknown command: ", ctx);
    out(line, ctx);
    out("\n", ctx);
    return 0;
}
