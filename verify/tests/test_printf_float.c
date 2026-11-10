/*
 * test_printf_float.c — tkl_vsnprintf binary32 + C99 (NULL, 0) length
 *
 * PS2 clu (be disassemblers, sps, tv/sc via the same printf family) format
 * floats through tkl_vsnprintf.  Two laws the old formatter broke:
 *
 *   1. vsnprintf(NULL, 0, ...) returned 0, so be concat / nv aprintf
 *      allocated a 1-byte line and overflowed it.
 *   2. %f consumed a C double and did double arithmetic (LAW 6 forbids
 *      double on the R5900).  The formatter now reduces the 8-byte
 *      vararg slot to binary32 with integer ops, and tkl_fmt_f32 is the
 *      non-vararg path the disassemblers use.
 *
 * This TU includes libstr.c so the test is the same source the image links.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "../../src/kernel/libstr.c"

static int s_checks, s_fail;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_fail++;
        printf("  [FAIL] %s\n", what);
    } else {
        printf("  [OK]   %s\n", what);
    }
}

static int sn(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = tkl_vsnprintf(dst, n, fmt, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    char buf[128];
    int n;
    union { float f; unsigned u; } bits;

    printf("==========================================================\n");
    printf(" tkl_vsnprintf binary32 / C99 (NULL, 0)\n");
    printf("==========================================================\n");

    n = sn(NULL, 0, "hello %d", 12);
    check("vsnprintf(NULL,0) hello 12 == 8", n == 8);

    n = sn(buf, sizeof buf, "hello %d", 12);
    check("hello 12", n == 8 && strcmp(buf, "hello 12") == 0);

    n = sn(buf, sizeof buf, "%.0f", 480.0f);
    check("%.0f 480", n == 3 && strcmp(buf, "480") == 0);

    n = sn(buf, sizeof buf, "%.0f", -352.0f);
    check("%.0f -352", n == 4 && strcmp(buf, "-352") == 0);

    n = sn(buf, sizeof buf, "%.2f", 1.5f);
    check("%.2f 1.50", n == 4 && strcmp(buf, "1.50") == 0);

    n = sn(buf, sizeof buf, "%f", 1.5f);
    check("%f 1.500000", n == 8 && strcmp(buf, "1.500000") == 0);

    n = sn(buf, sizeof buf, "%g", 1.5f);
    check("%g 1.5", n == 3 && strcmp(buf, "1.5") == 0);

    n = sn(buf, sizeof buf, "%g", 1.0f);
    check("%g 1", n == 1 && strcmp(buf, "1") == 0);

    n = sn(NULL, 0, "fmov %s, #%g", "s0", 1.5f);
    check("concat-style (NULL,0) fmov s0, #1.5", n == 13);

    n = sn(buf, sizeof buf, "fmov %s, #%g", "s0", 1.5f);
    check("fmov s0, #1.5", n == 13 && strcmp(buf, "fmov s0, #1.5") == 0);

    n = tkl_fmt_f32(buf, sizeof buf, 1.5f, 6, 'g');
    check("tkl_fmt_f32 1.5", n == 3 && strcmp(buf, "1.5") == 0);

    n = tkl_fmt_f32(buf, sizeof buf, -88.0f, 0, 'f');
    check("tkl_fmt_f32 -88", n == 3 && strcmp(buf, "-88") == 0);

    bits.f = 2.5f;
    n = sn(buf, sizeof buf, "%hf", bits.u);
    check("%hf 2.500000 from bits", n == 8 && strcmp(buf, "2.500000") == 0);

    n = sn(buf, 4, "%s", "abcdef");
    check("truncate writes 3 chars + NUL, C99 length 6",
          n == 6 && buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c' && buf[3] == '\0');

    n = sn(buf, sizeof buf, "%%");
    check("percent", n == 1 && strcmp(buf, "%") == 0);

    /* sc menus.c time column and header bar */
    n = sn(buf, sizeof buf, "%02u-%02u-%02u│%02u:%02u",
           5u, 3u, 26u, 9u, 4u);
    check("sc time %02u", strcmp(buf, "05-03-26│09:04") == 0);

    n = sn(buf, sizeof buf, "%-*s│%-*s│%-*s",
           8, "Name", 9, "Size/Type", 14, "Date    │Time");
    check("sc header %-*s Name padded to 8",
          memcmp(buf, "Name    │", 8 + sizeof("│") - 1) == 0);
    check("sc header Size/Type is 9 then bar",
          strstr(buf, "│Size/Type│") != NULL);

    n = sn(buf, sizeof buf, "%02x", 10);
    check("be hex %02x of 10", n == 2 && strcmp(buf, "0a") == 0);

    n = sn(buf, sizeof buf, "%02x", 0);
    check("be hex %02x of 0", n == 2 && strcmp(buf, "00") == 0);

    n = sn(buf, sizeof buf, "%5d", 12);
    check("%5d 12", n == 5 && strcmp(buf, "   12") == 0);

    n = sn(buf, sizeof buf, "%-5s", "ab");
    check("%-5s ab", n == 5 && strcmp(buf, "ab   ") == 0);

    n = sn(buf, sizeof buf, "%5s", "ab");
    check("%5s ab", n == 5 && strcmp(buf, "   ab") == 0);

    /* FILEIO cdrom0:\QUAKE\NAME.DEM;1 — a 0x5C in the format or in %s
     * must survive.  Host UART still eats raw backslash; the EE log
     * now prints '/' for it. */
    n = sn(buf, sizeof buf, "cdrom0:\\%s;1", "QUAKE\\DRONE1M1.DEM");
    check("cdrom0 backslash path",
          n == 28 && strcmp(buf, "cdrom0:\\QUAKE\\DRONE1M1.DEM;1") == 0);

    printf("----------------------------------------------------------\n");
    printf(" %d checks, %d failed\n", s_checks, s_fail);
    return s_fail ? 1 : 0;
}
